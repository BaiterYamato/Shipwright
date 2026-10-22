#include "OotNativeSkeletons.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "OotNativePlayerAction.h"
#include "OotNativeView.h"
#include "oot_layout_id.h"

namespace ShipLuaHost {
namespace {

struct Skeleton {
    void* instance = nullptr;
    std::string owner; // vazio = criado pela V1
    uint64_t epoch = 0;
    uint64_t tick = 0;
    uint64_t serial = 1;
    float assetLastFrame = 0.0f;
    uint8_t finished = 0;
    ShipOotSkeletonLimbFnV2 post = nullptr;
    void* user = nullptr;
    uint32_t postFailures = 0;
};

struct SkeletonsState {
    std::thread::id ownerThread;
    OotSkeletonsBridge bridge;
    uint64_t nextHandle = 1;
    std::map<uint64_t, Skeleton> skeletons;
    // V2 destruídos: a instância espera o frame seguinte.
    std::vector<void*> retired;
};

SkeletonsState& State() {
    static SkeletonsState state;
    return state;
}

bool OnOwnerThread() {
    return State().ownerThread == std::this_thread::get_id();
}

// "__OTR__" + caminho, internado até o fim do processo: o SkelAnime guarda o ponteiro.
const char* InternPath(const char* path) {
    if (!path || !*path || std::strlen(path) >= LINKSPAN_OOT_SKELETONS_MAX_PATH ||
        std::strncmp(path, "__OTR__", 7) == 0) {
        return nullptr;
    }
    static std::set<std::string> paths;
    return paths.insert(std::string("__OTR__") + path).first->c_str();
}

Skeleton* Find(uint64_t handle) {
    const auto found = State().skeletons.find(handle);
    return found == State().skeletons.end() ? nullptr : &found->second;
}

void Remove(std::map<uint64_t, Skeleton>::iterator found) {
    auto& state = State();
    void* instance = found->second.instance;
    const bool deferred = !found->second.owner.empty();
    state.skeletons.erase(found);
    if (deferred) {
        state.retired.push_back(instance);
    } else if (state.bridge.destroy) {
        state.bridge.destroy(instance);
    }
}

ShipNativeStatus Register(void* instance, std::string owner, float assetLastFrame, uint64_t* skeleton) {
    auto& state = State();
    const uint64_t handle = state.nextHandle++;
    Skeleton record;
    record.instance = instance;
    record.owner = std::move(owner);
    record.epoch = OotSceneEpoch();
    record.assetLastFrame = assetLastFrame;
    state.skeletons.emplace(handle, std::move(record));
    *skeleton = handle;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Create(const char* skeletonPath, const char* animationPath, uint64_t* skeleton) {
    if (!OnOwnerThread() || !skeleton) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *skeleton = 0;
    const char* skel = InternPath(skeletonPath);
    const char* anim = InternPath(animationPath);
    if (!skel || !anim) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    if (!state.bridge.create) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (state.skeletons.size() >= LINKSPAN_OOT_SKELETONS_MAX) {
        return SHIP_NATIVE_LIMIT;
    }
    void* instance = nullptr;
    const auto status = state.bridge.create(skel, anim, &instance);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    if (!instance) {
        return SHIP_NATIVE_FAILURE;
    }
    try {
        return Register(instance, {}, 0.0f, skeleton);
    } catch (...) {
        if (state.bridge.destroy) {
            state.bridge.destroy(instance);
        }
        return SHIP_NATIVE_FAILURE;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL Destroy(uint64_t skeleton) {
    if (!OnOwnerThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    const auto found = state.skeletons.find(skeleton);
    if (found == state.skeletons.end()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    Remove(found);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL PlayAnimation(uint64_t skeleton, const char* animationPath, float speed, uint8_t mode,
                                                float morphFrames) {
    if (!OnOwnerThread() || !std::isfinite(speed) || !std::isfinite(morphFrames) || morphFrames < 0.0f ||
        (mode != LINKSPAN_OOT_ANIM_LOOP && mode != LINKSPAN_OOT_ANIM_ONCE)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    Skeleton* record = Find(skeleton);
    const char* anim = InternPath(animationPath);
    if (!record || !anim) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!bridge.play) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const ShipNativeStatus status = bridge.play(record->instance, anim, speed, mode, morphFrames);
    if (status == SHIP_NATIVE_OK) {
        ++record->serial;
        record->finished = 0;
        if (bridge.frame) {
            float frame = 0.0f; // o play V1 vai de 0 ao último quadro: o endFrame é o limite do asset
            bridge.frame(record->instance, &frame, &record->assetLastFrame);
        }
    }
    return status;
}

ShipNativeStatus SHIP_NATIVE_CALL Update(uint64_t skeleton, uint8_t* finished) {
    Skeleton* record = OnOwnerThread() ? Find(skeleton) : nullptr;
    if (!record) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!bridge.update) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const uint8_t done = bridge.update(record->instance);
    record->finished = done ? 1 : 0;
    record->epoch = OotSceneEpoch();
    record->tick = bridge.tick ? bridge.tick() : 0;
    if (finished) {
        *finished = record->finished;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL GetFrame(uint64_t skeleton, float* frame, float* lastFrame) {
    Skeleton* record = OnOwnerThread() ? Find(skeleton) : nullptr;
    if (!record || !frame || !lastFrame) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!bridge.frame) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    bridge.frame(record->instance, frame, lastFrame);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Draw(void* play, uint64_t skeleton) {
    Skeleton* record = OnOwnerThread() ? Find(skeleton) : nullptr;
    if (!play || !record) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!InOotRenderScope() || !bridge.draw) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    bridge.draw(play, record->instance);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL CreateEx(const char* owner, const ShipOotSkeletonSpecV2* spec, uint64_t* skeleton) {
    if (skeleton) {
        *skeleton = 0;
    }
    if (!OnOwnerThread() || !skeleton || !ValidOotServiceOwner(owner) || !spec ||
        spec->size < sizeof(ShipOotSkeletonSpecV2) || spec->format > SHIP_OOT_SKELETON_LINK) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const char* skel = InternPath(spec->skeleton_path);
    const char* anim = InternPath(spec->initial_animation_path);
    if (!skel || !anim) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    if (OotServiceOwnerBlocked(owner) || !state.bridge.createEx) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (state.skeletons.size() >= LINKSPAN_OOT_SKELETONS_MAX) {
        return SHIP_NATIVE_LIMIT;
    }
    void* instance = nullptr;
    float assetLastFrame = 0.0f;
    const auto status =
        state.bridge.createEx(spec->format, skel, anim, spec->base_translation, &instance, &assetLastFrame);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    if (!instance) {
        return SHIP_NATIVE_FAILURE;
    }
    try {
        return Register(instance, owner, assetLastFrame, skeleton);
    } catch (...) {
        if (state.bridge.destroy) {
            state.bridge.destroy(instance);
        }
        return SHIP_NATIVE_FAILURE;
    }
}

bool ValidPlay(const ShipOotAnimationPlayV2* play) {
    return play && play->size >= sizeof(ShipOotAnimationPlayV2) && std::isfinite(play->speed) && play->speed > 0.0f &&
           std::isfinite(play->morph_frames) && play->morph_frames >= 0.0f && play->reverse <= 1 &&
           (play->mode == LINKSPAN_OOT_ANIM_LOOP || play->mode == LINKSPAN_OOT_ANIM_ONCE);
}

ShipNativeStatus SHIP_NATIVE_CALL PlayEx(uint64_t skeleton, const char* animationPath,
                                         const ShipOotAnimationPlayV2* play) {
    if (!OnOwnerThread() || !ValidPlay(play)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    Skeleton* record = Find(skeleton);
    const char* anim = InternPath(animationPath);
    if (!record || !anim) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!bridge.playEx) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    float assetLastFrame = 0.0f;
    const ShipNativeStatus status = bridge.playEx(record->instance, anim, play->speed, play->mode, play->morph_frames,
                                                  play->reverse != 0, &assetLastFrame);
    if (status == SHIP_NATIVE_OK) {
        ++record->serial;
        record->finished = 0;
        record->assetLastFrame = assetLastFrame;
    }
    return status;
}

ShipNativeStatus SHIP_NATIVE_CALL AnimationInfo(uint64_t skeleton, const char* animationPath,
                                                ShipOotAnimationInfoV2* info) {
    if (!OnOwnerThread() || !info || info->size < sizeof(ShipOotAnimationInfoV2)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    Skeleton* record = Find(skeleton);
    const char* anim = InternPath(animationPath);
    if (!record || !anim) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    ShipOotAnimationInfoV2 result{};
    result.size = sizeof(result);
    const auto& bridge = State().bridge;
    const ShipNativeStatus status =
        bridge.animationInfo ? bridge.animationInfo(record->instance, anim, &result) : SHIP_NATIVE_UNSUPPORTED;
    if (status == SHIP_NATIVE_OK) {
        result.size = sizeof(result);
        *info = result;
    }
    return status;
}

ShipNativeStatus SHIP_NATIVE_CALL GetState(uint64_t skeleton, ShipOotAnimationStateV2* out) {
    if (!OnOwnerThread() || !out || out->size < sizeof(ShipOotAnimationStateV2)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    Skeleton* record = Find(skeleton);
    if (!record) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!bridge.state) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    ShipOotAnimationStateV2 result{};
    result.size = sizeof(result);
    result.scene_epoch = record->epoch;
    result.tick = record->tick;
    result.animation_serial = record->serial;
    result.asset_last_frame = record->assetLastFrame;
    result.finished_previous_update = record->finished;
    bridge.state(record->instance, &result.current_frame, &result.target_end_frame, &result.speed);
    *out = result;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL SetPostLimb(uint64_t skeleton, ShipOotSkeletonLimbFnV2 callback, void* user) {
    if (!OnOwnerThread() || (!callback && user)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    Skeleton* record = Find(skeleton);
    if (!record) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (record->owner.empty()) {
        return SHIP_NATIVE_UNSUPPORTED; // V1 não tem dono para limpar o callback no unload
    }
    record->post = callback;
    record->user = user;
    record->postFailures = 0;
    return SHIP_NATIVE_OK;
}

struct LimbContext {
    uint64_t skeleton;
    void* play;
    void* actor;
    bool callbackThrew = false;
};

// Callback do dono depois de cada membro. O registro é procurado de novo a cada membro: destruído no meio do draw,
// os callbacks param e a instância segue viva na lista de aposentados.
void DispatchLimb(void* raw, int32_t limb) {
    auto& context = *static_cast<LimbContext*>(raw);
    Skeleton* record = Find(context.skeleton);
    if (!record || !record->post || OotServiceOwnerBlocked(record->owner)) {
        return;
    }
    const ShipOotSkeletonLimbFnV2 callback = record->post;
    void* const user = record->user;
    ShipOotSkeletonLimbV2 payload{};
    payload.size = sizeof(payload);
    payload.scene_epoch = OotSceneEpoch();
    payload.skeleton = context.skeleton;
    payload.play_state = context.play;
    payload.actor = context.actor;
    payload.limb = limb;
    ShipNativeStatus status = SHIP_NATIVE_FAILURE;
    try {
        status = callback(user, &payload);
    } catch (...) {
        context.callbackThrew = true;
        status = SHIP_NATIVE_FAILURE;
    }
    record = Find(context.skeleton);
    if (!record || record->post != callback || record->user != user) {
        return;
    }
    if (status == SHIP_NATIVE_OK || status == SHIP_NATIVE_UNSUPPORTED) {
        record->postFailures = 0;
    } else if (++record->postFailures >= 3) {
        record->post = nullptr;
        record->user = nullptr;
    }
}

ShipNativeStatus SHIP_NATIVE_CALL DrawEx(void* play, void* actor, uint64_t skeleton) {
    Skeleton* record = OnOwnerThread() ? Find(skeleton) : nullptr;
    if (!play || !record) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!InOotRenderScope() || !bridge.drawEx) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    LimbContext context{ skeleton, play, actor };
    try {
        bridge.drawEx(play, actor, record->instance, DispatchLimb, &context);
    } catch (...) { return SHIP_NATIVE_FAILURE; }
    // Retornos normais do callback conservam o fallback; uma excecao e observavel pelo chamador C.
    return context.callbackThrew ? SHIP_NATIVE_FAILURE : SHIP_NATIVE_OK;
}

const ShipOotSkeletonsV1 skeletonsV1{
    sizeof(ShipOotSkeletonsV1), Create, Destroy, PlayAnimation, Update, GetFrame, Draw
};
const ShipOotSkeletonsV2 skeletonsV2{
    sizeof(ShipOotSkeletonsV2), Create,   Destroy, PlayAnimation, Update,   GetFrame,    Draw,
    LINKSPAN_OOT_LAYOUT_ID,     CreateEx, PlayEx,  AnimationInfo, GetState, SetPostLimb, DrawEx
};
static_assert(offsetof(ShipOotSkeletonsV2, layout_id) == sizeof(ShipOotSkeletonsV1), "V2 conserva a V1");

} // namespace

void SetOotSkeletonsBridge(const OotSkeletonsBridge& bridge) {
    State().bridge = bridge;
}

void InitializeOotNativeSkeletons(std::thread::id ownerThread) {
    State().ownerThread = ownerThread;
}

void ResetOotNativeSkeletons() {
    auto& state = State();
    for (const auto& [handle, record] : state.skeletons) {
        if (state.bridge.destroy) {
            state.bridge.destroy(record.instance);
        }
    }
    state.skeletons.clear();
    ReleaseRetiredOotSkeletons();
}

const ShipOotSkeletonsV1& GetOotNativeSkeletonsService() {
    return skeletonsV1;
}

const ShipOotSkeletonsV2& GetOotNativeSkeletonsServiceV2() {
    return skeletonsV2;
}

uint32_t OotSkeletonCount() {
    return static_cast<uint32_t>(State().skeletons.size());
}

void ReleaseOotSkeletonOwner(std::string_view owner) {
    auto& skeletons = State().skeletons;
    for (auto it = skeletons.begin(); it != skeletons.end();) {
        if (!it->second.owner.empty() && it->second.owner == owner) {
            Remove(it++);
        } else {
            ++it;
        }
    }
}

void ReleaseRetiredOotSkeletons() {
    auto& state = State();
    std::vector<void*> retired;
    retired.swap(state.retired);
    for (void* instance : retired) {
        if (state.bridge.destroy) {
            state.bridge.destroy(instance);
        }
    }
}

void* OotSkeletonInstance(uint64_t skeleton) {
    const Skeleton* record = Find(skeleton);
    return record ? record->instance : nullptr;
}

} // namespace ShipLuaHost
