#include "OotNativeSkeletons.h"

#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "OotNativeView.h"

namespace ShipLuaHost {
namespace {

struct SkeletonsState {
    std::thread::id ownerThread;
    OotSkeletonsBridge bridge;
    uint64_t nextHandle = 1;
    std::map<uint64_t, void*> skeletons;
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
    if (!path || !*path || std::strlen(path) >= LINKSPAN_OOT_SKELETONS_MAX_PATH || std::strncmp(path, "__OTR__", 7) == 0) {
        return nullptr;
    }
    static std::set<std::string> paths;
    return paths.insert(std::string("__OTR__") + path).first->c_str();
}

void* Find(uint64_t handle) {
    const auto found = State().skeletons.find(handle);
    return found == State().skeletons.end() ? nullptr : found->second;
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
    const uint64_t handle = state.nextHandle++;
    state.skeletons.emplace(handle, instance);
    *skeleton = handle;
    return SHIP_NATIVE_OK;
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
    if (state.bridge.destroy) {
        state.bridge.destroy(found->second);
    }
    state.skeletons.erase(found);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL PlayAnimation(uint64_t skeleton, const char* animationPath, float speed, uint8_t mode,
                                               float morphFrames) {
    if (!OnOwnerThread() || !std::isfinite(speed) || !std::isfinite(morphFrames) || morphFrames < 0.0f ||
        (mode != LINKSPAN_OOT_ANIM_LOOP && mode != LINKSPAN_OOT_ANIM_ONCE)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    void* instance = Find(skeleton);
    const char* anim = InternPath(animationPath);
    if (!instance || !anim) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    return bridge.play ? bridge.play(instance, anim, speed, mode, morphFrames) : SHIP_NATIVE_UNSUPPORTED;
}

ShipNativeStatus SHIP_NATIVE_CALL Update(uint64_t skeleton, uint8_t* finished) {
    void* instance = OnOwnerThread() ? Find(skeleton) : nullptr;
    if (!instance) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!bridge.update) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const uint8_t done = bridge.update(instance);
    if (finished) {
        *finished = done ? 1 : 0;
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL GetFrame(uint64_t skeleton, float* frame, float* lastFrame) {
    void* instance = OnOwnerThread() ? Find(skeleton) : nullptr;
    if (!instance || !frame || !lastFrame) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!bridge.frame) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    bridge.frame(instance, frame, lastFrame);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL Draw(void* play, uint64_t skeleton) {
    void* instance = OnOwnerThread() ? Find(skeleton) : nullptr;
    if (!play || !instance) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    if (!InOotRenderScope() || !bridge.draw) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    bridge.draw(play, instance);
    return SHIP_NATIVE_OK;
}

const ShipOotSkeletonsV1 skeletonsV1{ sizeof(ShipOotSkeletonsV1), Create, Destroy, PlayAnimation, Update, GetFrame,
                                      Draw };

} // namespace

void SetOotSkeletonsBridge(const OotSkeletonsBridge& bridge) {
    State().bridge = bridge;
}

void InitializeOotNativeSkeletons(std::thread::id ownerThread) {
    State().ownerThread = ownerThread;
}

void ResetOotNativeSkeletons() {
    auto& state = State();
    for (const auto& [handle, instance] : state.skeletons) {
        if (state.bridge.destroy) {
            state.bridge.destroy(instance);
        }
    }
    state.skeletons.clear();
}

const ShipOotSkeletonsV1& GetOotNativeSkeletonsService() {
    return skeletonsV1;
}

uint32_t OotSkeletonCount() {
    return static_cast<uint32_t>(State().skeletons.size());
}

} // namespace ShipLuaHost
