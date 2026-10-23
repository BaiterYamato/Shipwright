#include "OotNativePlayerBody.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <optional>

#include "OotNativePlayerAction.h"
#include "OotNativeSkeletons.h"
#include "oot_layout_id.h"

namespace ShipLuaHost {
namespace {

constexpr uint64_t kControlDomain = UINT64_C(0xB100000000000000);
constexpr uint64_t kSequenceMask = UINT64_C(0x0000FFFFFFFFFFFF);
constexpr uint32_t kMaxAnimations = 256;

struct BodyAnimation {
    std::string path;
    bool reverse = false;
};

struct Body {
    std::string owner;
    uint32_t format = SHIP_OOT_SKELETON_LINK;
    std::string skeletonPath;
    std::string initialPath;
    int16_t base[3]{};
    std::map<std::string, BodyAnimation, std::less<>> animations;
    std::string defaultAnimation;
    std::map<uint32_t, std::string> segments;
    float groundOffset = 0.0f;
    // Na cena: esqueleto, ator do jogo e geração.
    uint64_t skeleton = 0;
    void* game = nullptr;
    bool active = false;
    uint64_t generation = 0;
};

struct Control {
    uint64_t id = 0;
    std::string owner;
    uint64_t generation = 0;
    ShipOotBodyControllerV1 callbacks{};
    uint32_t postFailures = 0;
    uint32_t drawFailures = 0;
};

struct Request {
    std::string name;
    ShipOotAnimationPlayV2 play{};
    uint64_t serial = 0;
};

std::thread::id gameThread;
OotPlayerBodyBridge bridge;
std::unique_ptr<Body> body;
std::optional<Control> control;
uint64_t nextControl = 1;
uint64_t nextGeneration = 1;
uint64_t nextRequest = 1;
std::optional<Request> overrideRequest;
std::optional<Request> fallbackRequest;
std::string playingName;
uint64_t playingSerial = 0;
bool playingOnce = false;
bool ticking = false;
uint64_t rootAction = 0;
uint32_t rootMode = SHIP_OOT_ROOT_DISABLED;
bool rootWasOn = false;
uint32_t callbackDepth = 0;

// A DLL pode falhar sem deixar o arbitro preso no escopo de callback ou de tick.
struct BodyCallbackScope {
    bool previousTicking = ticking;
    explicit BodyCallbackScope(bool isTick = false) {
        ++callbackDepth;
        if (isTick)
            ticking = true;
    }
    ~BodyCallbackScope() {
        ticking = previousTicking;
        --callbackDepth;
    }
    BodyCallbackScope(const BodyCallbackScope&) = delete;
    BodyCallbackScope& operator=(const BodyCallbackScope&) = delete;
};

bool OnGameThread() {
    return std::this_thread::get_id() == gameThread;
}

const ShipOotSkeletonsV2& Skeletons() {
    return GetOotNativeSkeletonsServiceV2();
}

bool ValidName(const char* name) {
    return ValidOotServiceOwner(name); // mesma regra: 1..128 bytes, sem controle
}

bool ValidPath(const char* path) {
    return path && *path && std::strlen(path) < LINKSPAN_OOT_SKELETONS_MAX_PATH &&
           std::strncmp(path, "__OTR__", 7) != 0;
}

bool ValidPlay(const ShipOotAnimationPlayV2* play) {
    return play && play->size >= sizeof(ShipOotAnimationPlayV2) && std::isfinite(play->speed) && play->speed > 0.0f &&
           std::isfinite(play->morph_frames) && play->morph_frames >= 0.0f && play->reverse <= 1 &&
           (play->mode == LINKSPAN_OOT_ANIM_LOOP || play->mode == LINKSPAN_OOT_ANIM_ONCE);
}

ShipNativeStatus CopySpec(const char* owner, const ShipOotBodySpecV1* spec, Body& out) {
    if (spec->size < sizeof(ShipOotBodySpecV1) || spec->skeleton.size < sizeof(ShipOotSkeletonSpecV2) ||
        spec->skeleton.format > SHIP_OOT_SKELETON_LINK || !ValidPath(spec->skeleton.skeleton_path) ||
        !ValidPath(spec->skeleton.initial_animation_path) || !ValidName(spec->default_animation) ||
        !std::isfinite(spec->ground_offset) || std::fabs(spec->ground_offset) > 10000.0f || !spec->animations ||
        spec->animation_count == 0 || spec->animation_count > kMaxAnimations || spec->segment_count > 15 ||
        (spec->segment_count && !spec->segments)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    out.owner = owner;
    out.format = spec->skeleton.format;
    out.skeletonPath = spec->skeleton.skeleton_path;
    out.initialPath = spec->skeleton.initial_animation_path;
    std::memcpy(out.base, spec->skeleton.base_translation, sizeof(out.base));
    out.defaultAnimation = spec->default_animation;
    out.groundOffset = spec->ground_offset;
    for (uint32_t i = 0; i < spec->animation_count; ++i) {
        const auto& animation = spec->animations[i];
        if (animation.size < sizeof(ShipOotBodyAnimationV1) || !ValidName(animation.name) ||
            !ValidPath(animation.path) || animation.reverse_default > 1 ||
            !out.animations.emplace(animation.name, BodyAnimation{ animation.path, animation.reverse_default != 0 })
                 .second) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
    }
    if (!out.animations.count(out.defaultAnimation)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < spec->segment_count; ++i) {
        const auto& segment = spec->segments[i];
        if (segment.size < sizeof(ShipOotBodySegmentV1) || segment.segment < 1 || segment.segment > 15 ||
            !ValidPath(segment.resource_path) || !out.segments.emplace(segment.segment, segment.resource_path).second) {
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
    }
    return SHIP_NATIVE_OK;
}

void ResetAnimation() {
    overrideRequest.reset();
    fallbackRequest.reset();
    playingName.clear();
    playingSerial = 0;
    playingOnce = false;
    rootAction = 0;
    rootMode = SHIP_OOT_ROOT_DISABLED;
    rootWasOn = false;
}

// O MOVEACTOR que o tick enfileirou lê NOMOVE na hora (z_skelanime.c:1882): sem pedido de raiz vivo, o Player para.
void StopRoot() {
    if (rootWasOn && body && body->active && bridge.rootStop) {
        bridge.rootStop(body->game);
    }
}

void CancelControl() {
    StopRoot();
    control.reset();
    ResetAnimation();
}

// O lease da ação que autorizou a raiz caiu depois do tick (release, unload, troca de cena). Não chama a DLL.
void RootActionRevoked(uint64_t token) {
    if (token == 0 || token != rootAction) {
        return;
    }
    StopRoot();
    rootAction = 0;
    rootMode = SHIP_OOT_ROOT_DISABLED;
    rootWasOn = false;
}

ShipNativeStatus SHIP_NATIVE_CALL BodyPostLimb(void*, const ShipOotSkeletonLimbV2* limb);

// Cria esqueleto e ator para `candidate` na cena atual. Falha não deixa nada vivo.
ShipNativeStatus Activate(Body& candidate) {
    if (!bridge.activate || !bridge.deactivate) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    ShipOotSkeletonSpecV2 skeletonSpec{};
    skeletonSpec.size = sizeof(skeletonSpec);
    skeletonSpec.format = candidate.format;
    skeletonSpec.skeleton_path = candidate.skeletonPath.c_str();
    skeletonSpec.initial_animation_path = candidate.initialPath.c_str();
    std::memcpy(skeletonSpec.base_translation, candidate.base, sizeof(candidate.base));
    uint64_t skeleton = 0;
    ShipNativeStatus status = Skeletons().create_ex(candidate.owner.c_str(), &skeletonSpec, &skeleton);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    for (const auto& [name, animation] : candidate.animations) {
        ShipOotAnimationInfoV2 info{};
        info.size = sizeof(info);
        if (Skeletons().animation_info(skeleton, animation.path.c_str(), &info) != SHIP_NATIVE_OK || !info.loaded) {
            Skeletons().destroy(skeleton);
            return SHIP_NATIVE_FAILURE;
        }
    }
    void* game = nullptr;
    status = bridge.activate(skeleton, candidate.groundOffset, &game);
    if (status == SHIP_NATIVE_OK && bridge.segments) {
        status = bridge.segments(game, candidate.segments);
        if (status != SHIP_NATIVE_OK) {
            // Mostra o Link de volta, a não ser que um corpo anterior ainda o esconda.
            bridge.deactivate(game, true, !body || !body->active);
        }
    }
    if (status != SHIP_NATIVE_OK) {
        Skeletons().destroy(skeleton);
        return status;
    }
    Skeletons().set_post_limb(skeleton, BodyPostLimb, nullptr);
    candidate.skeleton = skeleton;
    candidate.game = game;
    candidate.active = true;
    candidate.generation = nextGeneration++;
    return SHIP_NATIVE_OK;
}

// Tira o corpo da cena. inScene: o ator ainda existe.
void Deactivate(Body& target, bool inScene, bool showLink) {
    if (!target.active) {
        return;
    }
    bridge.deactivate(target.game, inScene, showLink);
    Skeletons().destroy(target.skeleton);
    target.game = nullptr;
    target.skeleton = 0;
    target.active = false;
}

bool ControlLive(uint64_t id) {
    return control && control->id == id && body && body->active && control->generation == body->generation &&
           !OotServiceOwnerBlocked(control->owner);
}

ShipNativeStatus ControlStatus(uint64_t id) {
    if (!OnGameThread()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const uint64_t sequence = id & kSequenceMask;
    if ((id & ~kSequenceMask) != kControlDomain || sequence == 0 || sequence >= nextControl) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    return ControlLive(id) ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}

ShipNativeStatus SHIP_NATIVE_CALL SetBody(const char* owner, const ShipOotBodySpecV1* spec) {
    if (!OnGameThread() || !ValidOotServiceOwner(owner)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (OotServiceOwnerBlocked(owner)) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (body && body->owner != owner) {
        return SHIP_NATIVE_LIMIT;
    }
    if (callbackDepth) {
        return SHIP_NATIVE_LIMIT;
    }
    if (!spec) {
        if (body) {
            CancelControl();
            Deactivate(*body, true, true);
            body.reset();
        }
        return SHIP_NATIVE_OK;
    }
    if (bridge.luaBodyActive && bridge.luaBodyActive()) {
        return SHIP_NATIVE_LIMIT;
    }
    auto candidate = std::make_unique<Body>();
    try {
        const ShipNativeStatus copied = CopySpec(owner, spec, *candidate);
        if (copied != SHIP_NATIVE_OK) {
            return copied;
        }
    } catch (...) { return SHIP_NATIVE_FAILURE; }
    const ShipNativeStatus status = Activate(*candidate);
    if (status != SHIP_NATIVE_OK) {
        return status; // o corpo anterior do mesmo dono continua
    }
    if (body) {
        CancelControl();
        Deactivate(*body, true, false); // o corpo novo segue escondendo o Link
    }
    ResetAnimation();
    body = std::move(candidate);
    return SHIP_NATIVE_OK;
}

ShipOotBodyStateV1 MakeState() {
    ShipOotBodyStateV1 state{};
    state.size = sizeof(state);
    state.scene_epoch = OotSceneEpoch();
    state.animation.size = sizeof(state.animation);
    if (body) {
        state.body_generation = body->generation;
        state.active = body->active ? 1 : 0;
        state.controlled = control && ControlLive(control->id) ? 1 : 0;
        if (body->active) {
            Skeletons().get_state(body->skeleton, &state.animation);
        }
    }
    return state;
}

ShipNativeStatus SHIP_NATIVE_CALL GetState(ShipOotBodyStateV1* state) {
    if (!OnGameThread() || !state || state->size < sizeof(ShipOotBodyStateV1)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *state = MakeState();
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL AcquireControl(const char* owner, uint64_t expectedGeneration,
                                                 const ShipOotBodyControllerV1* callbacks, uint64_t* id) {
    if (id) {
        *id = 0;
    }
    if (!OnGameThread() || !id || !ValidOotServiceOwner(owner) || !callbacks ||
        callbacks->size < sizeof(ShipOotBodyControllerV1)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (OotServiceOwnerBlocked(owner) || !body || !body->active) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (body->owner != owner || (control && ControlLive(control->id)) || callbackDepth) {
        return SHIP_NATIVE_LIMIT;
    }
    if (expectedGeneration != body->generation) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (nextControl > kSequenceMask) {
        return SHIP_NATIVE_LIMIT;
    }
    Control next;
    try {
        next.owner = owner;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
    next.id = kControlDomain | nextControl++;
    next.generation = body->generation;
    next.callbacks = *callbacks;
    ResetAnimation();
    control = std::move(next);
    *id = control->id;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL ReleaseControl(uint64_t id) {
    const ShipNativeStatus status = ControlStatus(id);
    if (status == SHIP_NATIVE_INVALID_ARGUMENT) {
        return status;
    }
    if (status == SHIP_NATIVE_OK) {
        CancelControl();
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL StartAnimation(uint64_t id, const char* name, const ShipOotAnimationPlayV2* play,
                                                 uint32_t lane) {
    if (!ValidName(name) || !ValidPlay(play) || lane > SHIP_OOT_ANIM_FALLBACK) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const ShipNativeStatus status = ControlStatus(id);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    if (!body->animations.count(name)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    Request request;
    try {
        request.name = name;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
    request.play = *play;
    request.play.size = sizeof(request.play);
    request.serial = nextRequest++;
    if (lane == SHIP_OOT_ANIM_OVERRIDE) {
        overrideRequest = std::move(request);
    } else {
        fallbackRequest = std::move(request);
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL AnimationInfo(uint64_t id, const char* name, ShipOotAnimationInfoV2* info) {
    if (!ValidName(name) || !info || info->size < sizeof(ShipOotAnimationInfoV2)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const ShipNativeStatus status = ControlStatus(id);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    const auto found = body->animations.find(name);
    if (found == body->animations.end()) {
        ShipOotAnimationInfoV2 missing{};
        missing.size = sizeof(missing);
        *info = missing;
        return SHIP_NATIVE_OK;
    }
    return Skeletons().animation_info(body->skeleton, found->second.path.c_str(), info);
}

ShipNativeStatus SHIP_NATIVE_CALL SetSegment(uint64_t id, uint32_t segment, const char* path) {
    if (segment < 1 || segment > 15 || (path && !ValidPath(path))) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const ShipNativeStatus status = ControlStatus(id);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    if (!bridge.segments) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    try {
        auto next = body->segments;
        if (path) {
            next[segment] = path;
        } else {
            next.erase(segment);
        }
        const ShipNativeStatus applied = bridge.segments(body->game, next);
        if (applied == SHIP_NATIVE_OK) {
            body->segments.swap(next);
        }
        return applied;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL SetRootMotion(uint64_t id, uint64_t actionToken, uint32_t mode) {
    if (mode > SHIP_OOT_ROOT_XZ) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const ShipNativeStatus status = ControlStatus(id);
    if (status != SHIP_NATIVE_OK) {
        return status;
    }
    if (!ticking) {
        return SHIP_NATIVE_UNSUPPORTED; // pedido de raiz vale só para o tick em curso
    }
    if (mode == SHIP_OOT_ROOT_XZ) {
        std::string_view owner;
        if (!OotActionTokenLive(actionToken, &owner) || owner != body->owner) {
            return SHIP_NATIVE_UNSUPPORTED;
        }
    }
    rootAction = actionToken;
    rootMode = mode;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL BodyPostLimb(void*, const ShipOotSkeletonLimbV2* limb) {
    if (!control || !ControlLive(control->id) || !control->callbacks.post_limb) {
        return SHIP_NATIVE_OK;
    }
    const auto callback = control->callbacks.post_limb;
    void* const user = control->callbacks.user;
    const uint64_t id = control->id;
    ShipNativeStatus status = SHIP_NATIVE_FAILURE;
    {
        BodyCallbackScope callbackScope;
        try {
            status = callback(user, limb);
        } catch (...) { status = SHIP_NATIVE_FAILURE; }
    }
    if (control && control->id == id && status != SHIP_NATIVE_OK && status != SHIP_NATIVE_UNSUPPORTED &&
        ++control->postFailures >= 3) {
        control->callbacks.post_limb = nullptr;
    }
    return SHIP_NATIVE_OK; // a falha do dono não conta como falha do callback do host
}

const ShipOotPlayerBodyV1 playerBodyV1{ sizeof(ShipOotPlayerBodyV1),
                                        LINKSPAN_OOT_LAYOUT_ID,
                                        SetBody,
                                        GetState,
                                        AcquireControl,
                                        ReleaseControl,
                                        StartAnimation,
                                        AnimationInfo,
                                        SetSegment,
                                        SetRootMotion };

} // namespace

void SetOotPlayerBodyBridge(const OotPlayerBodyBridge& next) {
    bridge = next;
}

void InitializeOotPlayerBody(std::thread::id thread) {
    gameThread = thread;
    AddOotActionRevokeListener(RootActionRevoked);
}

const ShipOotPlayerBodyV1& GetOotNativePlayerBodyService() {
    return playerBodyV1;
}

bool OotPlayerBodyInUse() {
    return body != nullptr;
}

uint64_t OotPlayerBodySkeleton() {
    return body && body->active ? body->skeleton : 0;
}

void ReleaseOotPlayerBodyOwner(std::string_view owner) {
    if (control && control->owner == owner) {
        CancelControl();
    }
    if (body && body->owner == owner) {
        Deactivate(*body, true, true);
        body.reset();
    }
}

void OotPlayerBodySceneEnded() {
    CancelControl();
    if (body) {
        Deactivate(*body, false, false); // o ator foi com a arena
    }
}

void OotPlayerBodyPlayerUpdate(void*, void*) {
    if (!OnGameThread() || !body || body->active || OotServiceOwnerBlocked(body->owner) ||
        (bridge.luaBodyActive && bridge.luaBodyActive())) {
        return;
    }
    if (Activate(*body) != SHIP_NATIVE_OK) {
        body.reset(); // a cena nova não aceitou o corpo; o dono pode pedir de novo
    }
}

void OotPlayerBodyTick(void* play, void* player) {
    if (!OnGameThread() || !body || !body->active) {
        return;
    }
    rootAction = 0;
    rootMode = SHIP_OOT_ROOT_DISABLED;
    if (control && !ControlLive(control->id)) {
        CancelControl();
    }
    if (control && control->callbacks.tick) {
        ShipOotBodyTickV1 tick{};
        tick.size = sizeof(tick);
        tick.control = control->id;
        tick.play_state = play;
        tick.player = player;
        tick.state = MakeState();
        const auto callback = control->callbacks.tick;
        void* const user = control->callbacks.user;
        const uint64_t id = control->id;
        ShipNativeStatus status = SHIP_NATIVE_FAILURE;
        {
            BodyCallbackScope callbackScope(true);
            try {
                status = callback(user, &tick);
            } catch (...) { status = SHIP_NATIVE_FAILURE; }
        }
        if (control && control->id == id && status != SHIP_NATIVE_OK && status != SHIP_NATIVE_UNSUPPORTED) {
            CancelControl();
        }
    }
    if (!body || !body->active) {
        return;
    }
    // OVERRIDE > FALLBACK deste tick > padrão.
    Request chosen;
    if (overrideRequest) {
        chosen = *overrideRequest;
    } else if (fallbackRequest) {
        chosen = *fallbackRequest;
        chosen.serial = 0; // FALLBACK com o nome em curso continua a animação, sem reiniciar
    } else {
        chosen.name = body->defaultAnimation;
        chosen.play = { sizeof(ShipOotAnimationPlayV2),
                        1.0f,
                        0.0f,
                        LINKSPAN_OOT_ANIM_LOOP,
                        uint8_t(body->animations.at(chosen.name).reverse ? 1 : 0),
                        { 0, 0 } };
    }
    fallbackRequest.reset();
    bool restarted = false;
    if (chosen.name != playingName || chosen.serial != playingSerial) {
        const auto& animation = body->animations.at(chosen.name);
        if (Skeletons().play_ex(body->skeleton, animation.path.c_str(), &chosen.play) == SHIP_NATIVE_OK) {
            playingName = chosen.name;
            playingSerial = chosen.serial;
            playingOnce = chosen.play.mode == LINKSPAN_OOT_ANIM_ONCE;
            restarted = true;
        }
    }
    uint8_t finished = 0;
    Skeletons().update(body->skeleton, &finished);
    std::string_view actionOwner;
    const bool move =
        rootMode == SHIP_OOT_ROOT_XZ && OotActionTokenLive(rootAction, &actionOwner) && actionOwner == body->owner;
    if (bridge.rootMotion) {
        bridge.rootMotion(body->game, move, move && (!rootWasOn || restarted));
    }
    rootWasOn = move;
    if (finished && playingOnce && overrideRequest && overrideRequest->serial == playingSerial) {
        overrideRequest.reset();
    }
}

bool OotPlayerBodyDraw(void* play, void* player, void* actor) {
    if (!OnGameThread() || !body || !body->active) {
        return false;
    }
    if (!control || !ControlLive(control->id) || !control->callbacks.draw) {
        return true;
    }
    ShipOotBodyDrawV1 draw{};
    draw.size = sizeof(draw);
    draw.control = control->id;
    draw.play_state = play;
    draw.player = player;
    draw.actor = actor;
    draw.state = MakeState();
    const auto callback = control->callbacks.draw;
    void* const user = control->callbacks.user;
    const uint64_t id = control->id;
    uint8_t drawDefault = 1;
    ShipNativeStatus status = SHIP_NATIVE_FAILURE;
    {
        BodyCallbackScope callbackScope;
        try {
            status = callback(user, &draw, &drawDefault);
        } catch (...) { status = SHIP_NATIVE_FAILURE; }
    }
    if (!body || !body->active) {
        return false;
    }
    if (!ControlLive(id)) {
        return true; // o callback soltou o controle: o pedido dele não vale mais
    }
    if (status != SHIP_NATIVE_OK || drawDefault > 1) {
        if (control && control->id == id && status != SHIP_NATIVE_UNSUPPORTED && ++control->drawFailures >= 3) {
            control->callbacks.draw = nullptr;
        }
        return true;
    }
    if (control && control->id == id) {
        control->drawFailures = 0;
    }
    return drawDefault != 0;
}

} // namespace ShipLuaHost
