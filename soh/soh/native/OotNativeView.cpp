#include "OotNativeView.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ShipLuaHost {
namespace {

constexpr std::string_view kOtrPrefix = "__OTR__";

struct ViewState {
    std::thread::id ownerThread;
    OotViewBridge bridge;
    uint64_t nextToken = 1;
    uint64_t token = 0; // 0 = câmera livre
    std::string owner;
    int16_t camera = -1;
    void* play = nullptr;
    bool hasView = false;
    ShipOotCameraViewV1 view{};
    // Push feitos pelo mod em cada escopo de draw aberto.
    std::vector<uint32_t> scopes;
    std::set<std::string, std::less<>> paths;
    struct RenderState {
        uint64_t token = 0;
        std::string owner;
        uint32_t features = 0;
        std::vector<int16_t> receivers;
    };
    uint64_t nextRenderToken = 1;
    std::vector<RenderState> renderStates;
    uint32_t renderFeatures = 0;
    // Algum mod mudou a rampa ou a sombra; volta ao padrão quando o último estado sai.
    bool toonLookChanged = false;
};

ViewState& State() {
    static ViewState state;
    return state;
}

bool OnOwnerThread() {
    return State().ownerThread == std::this_thread::get_id();
}

void DropCamera(bool destroy) {
    auto& state = State();
    if (destroy && state.bridge.cameraDestroy && state.camera >= 0) {
        state.bridge.cameraDestroy(state.camera, state.play);
    }
    state.token = 0;
    state.owner.clear();
    state.camera = -1;
    state.play = nullptr;
    state.hasView = false;
}

// Dono atual com a câmera ainda ativa; senão a posse cai aqui mesmo.
bool Holds(uint64_t token) {
    auto& state = State();
    if (!token || token != state.token) {
        return false;
    }
    if (!state.bridge.cameraValid || !state.bridge.cameraValid(state.camera, state.play)) {
        // cameraDestroy só remove a subcâmera se ela ainda existir na cena atual.
        DropCamera(true);
        return false;
    }
    return true;
}

bool ValidOwner(const char* text) {
    if (!text) {
        return false;
    }
    const size_t length = strnlen(text, LINKSPAN_OOT_CAMERA_MAX_OWNER + 1);
    if (length == 0 || length > LINKSPAN_OOT_CAMERA_MAX_OWNER) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(text[i]) < 0x20) {
            return false;
        }
    }
    return true;
}

bool ValidView(const ShipOotCameraViewV1* view) {
    if (!view || view->size < sizeof(ShipOotCameraViewV1)) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(view->eye[i]) || !std::isfinite(view->at[i])) {
            return false;
        }
    }
    const float dx = view->eye[0] - view->at[0];
    const float dy = view->eye[1] - view->at[1];
    const float dz = view->eye[2] - view->at[2];
    return std::isfinite(view->fov) && view->fov >= 1.0f && view->fov <= 170.0f &&
           dx * dx + dy * dy + dz * dz > 0.0001f;
}

bool InScope() {
    return !State().scopes.empty();
}

uint32_t RenderFeatures() {
    return State().renderFeatures;
}

void RebuildRenderFeatures() {
    auto& state = State();
    state.renderFeatures = 0;
    for (const auto& renderState : state.renderStates) state.renderFeatures |= renderState.features;
    if (state.renderStates.empty() && state.toonLookChanged) {
        state.toonLookChanged = false;
        if (state.bridge.resetToonLook) state.bridge.resetToonLook();
    }
}

ViewState::RenderState* FindRenderState(uint64_t token) {
    auto& states = State().renderStates;
    const auto it = std::find_if(states.begin(), states.end(),
                                 [token](const auto& state) { return state.token == token; });
    return it == states.end() ? nullptr : &*it;
}

ShipNativeStatus SHIP_NATIVE_CALL Acquire(const char* owner, uint64_t* token) {
    if (!OnOwnerThread() || !ValidOwner(owner) || !token) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    *token = 0;
    auto& state = State();
    if (state.token && Holds(state.token)) {
        return SHIP_NATIVE_LIMIT;
    }
    if (!state.bridge.cameraCreate) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    try {
        std::string name(owner);
        int16_t camera = -1;
        void* play = nullptr;
        const ShipNativeStatus created = state.bridge.cameraCreate(&camera, &play);
        if (created != SHIP_NATIVE_OK) {
            return created;
        }
        state.token = state.nextToken++;
        state.owner = std::move(name);
        state.camera = camera;
        state.play = play;
        state.hasView = false;
        *token = state.token;
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL Release(uint64_t token) {
    if (!OnOwnerThread() || !token) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    if (token == state.token) {
        DropCamera(true);
        return SHIP_NATIVE_OK;
    }
    // Token antigo que já perdeu a posse.
    return token < state.nextToken ? SHIP_NATIVE_OK : SHIP_NATIVE_INVALID_ARGUMENT;
}

ShipNativeStatus SHIP_NATIVE_CALL SetView(uint64_t token, const ShipOotCameraViewV1* view) {
    if (!OnOwnerThread() || !ValidView(view)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!Holds(token)) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto& state = State();
    state.view = *view;
    state.view.size = sizeof(ShipOotCameraViewV1);
    state.hasView = true;
    if (state.bridge.cameraApply) {
        state.bridge.cameraApply(state.camera, state.play, state.view);
    }
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL GetView(ShipOotCameraViewV1* view) {
    if (!OnOwnerThread() || !view || view->size < sizeof(ShipOotCameraViewV1)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const auto& bridge = State().bridge;
    return bridge.cameraRead ? bridge.cameraRead(view) : SHIP_NATIVE_UNSUPPORTED;
}

uint8_t SHIP_NATIVE_CALL IsOwned(uint64_t token) {
    return OnOwnerThread() && Holds(token) ? 1 : 0;
}

ShipNativeStatus SHIP_NATIVE_CALL DrawDisplayList(void* play, const char* path, uint8_t layer) {
    if (!OnOwnerThread() || !play || !path || layer > LINKSPAN_OOT_RENDER_TRANSLUCENT) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    const std::string_view text(path, strnlen(path, LINKSPAN_OOT_RENDER_MAX_PATH + 1));
    if (text.empty() || text.size() > LINKSPAN_OOT_RENDER_MAX_PATH || text.substr(0, kOtrPrefix.size()) == kOtrPrefix ||
        text.find("..") != std::string_view::npos || text.front() == '/') {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& state = State();
    if (!InScope() || !state.bridge.drawDisplayList) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    try {
        std::string full(kOtrPrefix);
        full.append(text);
        const char* interned = state.paths.emplace(std::move(full)).first->c_str();
        return state.bridge.drawDisplayList(play, interned, layer);
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL MatrixPush() {
    auto& state = State();
    if (!OnOwnerThread() || !InScope() || !state.bridge.matrixPush) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (state.scopes.back() >= LINKSPAN_OOT_RENDER_MAX_DEPTH) {
        return SHIP_NATIVE_LIMIT;
    }
    state.bridge.matrixPush();
    ++state.scopes.back();
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL MatrixPop() {
    auto& state = State();
    if (!OnOwnerThread() || !InScope() || !state.bridge.matrixPop) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (!state.scopes.back()) {
        return SHIP_NATIVE_INVALID_ARGUMENT; // pop da matriz do host
    }
    state.bridge.matrixPop();
    --state.scopes.back();
    return SHIP_NATIVE_OK;
}

bool Finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

ShipNativeStatus SHIP_NATIVE_CALL MatrixTranslate(float x, float y, float z) {
    const auto& bridge = State().bridge;
    if (!OnOwnerThread() || !InScope() || !bridge.matrixTranslate) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (!Finite3(x, y, z)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    bridge.matrixTranslate(x, y, z);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL MatrixScale(float x, float y, float z) {
    const auto& bridge = State().bridge;
    if (!OnOwnerThread() || !InScope() || !bridge.matrixScale) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (!Finite3(x, y, z)) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    bridge.matrixScale(x, y, z);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL MatrixRotateZYX(int16_t x, int16_t y, int16_t z) {
    const auto& bridge = State().bridge;
    if (!OnOwnerThread() || !InScope() || !bridge.matrixRotateZYX) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    bridge.matrixRotateZYX(x, y, z);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL AcquireRenderState(const char* owner, uint64_t* token) {
    if (!OnOwnerThread() || !owner || !*owner || !token || std::strlen(owner) > 128) return SHIP_NATIVE_INVALID_ARGUMENT;
    try {
        auto& state = State();
        *token = state.nextRenderToken++;
        state.renderStates.push_back({*token, owner, 0, {}});
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

ShipNativeStatus SHIP_NATIVE_CALL ReleaseRenderState(uint64_t token) {
    if (!OnOwnerThread() || !token) return SHIP_NATIVE_INVALID_ARGUMENT;
    auto& states = State().renderStates;
    const auto oldSize = states.size();
    std::erase_if(states, [token](const auto& state) { return state.token == token; });
    RebuildRenderFeatures();
    return states.size() == oldSize ? SHIP_NATIVE_INVALID_ARGUMENT : SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL SetRenderState(uint64_t token, uint32_t features, const int16_t* receivers,
                                                  uint32_t receiverCount) {
    if (!OnOwnerThread() || receiverCount > LINKSPAN_OOT_RENDER_MAX_SHADOW_RECEIVERS ||
        (receiverCount && !receivers) || (features & ~(LINKSPAN_OOT_RENDER_FEATURE_TOON_ACTORS |
                                                       LINKSPAN_OOT_RENDER_FEATURE_SUPPRESS_VANILLA_SHADOWS |
                                                       LINKSPAN_OOT_RENDER_FEATURE_HIDE_VANILLA_POINT_GLOW)))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    auto* state = FindRenderState(token);
    if (!state) return SHIP_NATIVE_INVALID_ARGUMENT;
    try {
        std::vector<int16_t> copied;
        if (receiverCount) copied.assign(receivers, receivers + receiverCount);
        std::sort(copied.begin(), copied.end());
        copied.erase(std::unique(copied.begin(), copied.end()), copied.end());
        state->features = features;
        state->receivers = std::move(copied);
        RebuildRenderFeatures();
        return SHIP_NATIVE_OK;
    } catch (...) { return SHIP_NATIVE_FAILURE; }
}

bool ValidRenderLayer(uint8_t layer) {
    return layer == LINKSPAN_OOT_RENDER_OPAQUE || layer == LINKSPAN_OOT_RENDER_TRANSLUCENT;
}

ShipNativeStatus SHIP_NATIVE_CALL EmitToonKey(uint8_t layer, int8_t dx, int8_t dy, int8_t dz,
                                              uint8_t r, uint8_t g, uint8_t b) {
    const auto& bridge = State().bridge;
    if (!OnOwnerThread() || !InScope() || !ValidRenderLayer(layer) || !bridge.emitToonKey) return SHIP_NATIVE_UNSUPPORTED;
    return bridge.emitToonKey(layer, dx, dy, dz, r, g, b);
}

ShipNativeStatus SHIP_NATIVE_CALL EmitStencil(uint8_t layer, uint8_t mode) {
    const auto& bridge = State().bridge;
    if (!OnOwnerThread() || !InScope() || !ValidRenderLayer(layer) || !bridge.emitStencil) return SHIP_NATIVE_UNSUPPORTED;
    return bridge.emitStencil(layer, mode);
}

ShipNativeStatus SHIP_NATIVE_CALL EmitToonShadow(uint8_t layer, int16_t feetClampY, float size) {
    const auto& bridge = State().bridge;
    if (!OnOwnerThread() || !InScope() || !ValidRenderLayer(layer) || !bridge.emitToonShadow)
        return SHIP_NATIVE_UNSUPPORTED;
    if (!std::isfinite(size)) return SHIP_NATIVE_INVALID_ARGUMENT;
    return bridge.emitToonShadow(layer, feetClampY, size);
}

ShipNativeStatus SHIP_NATIVE_CALL SetToonRamp(float center, float softness, float highlight, float shadow,
                                              uint8_t debugBands) {
    auto& state = State();
    if (!OnOwnerThread() || !state.bridge.setToonRamp) return SHIP_NATIVE_UNSUPPORTED;
    if (!std::isfinite(center) || !std::isfinite(softness) || !std::isfinite(highlight) || !std::isfinite(shadow) ||
        softness < 0.0f)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    state.bridge.setToonRamp(center, softness, highlight, shadow, debugBands != 0);
    state.toonLookChanged = true;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL SetToonShadowParams(float opacity, float minElevation, float slabDepth,
                                                      float slabRise, int32_t edgeSoftness, uint8_t showVolume) {
    auto& state = State();
    if (!OnOwnerThread() || !state.bridge.setToonShadowParams) return SHIP_NATIVE_UNSUPPORTED;
    if (!std::isfinite(opacity) || !std::isfinite(minElevation) || !std::isfinite(slabDepth) ||
        !std::isfinite(slabRise) || edgeSoftness < 0 || edgeSoftness > 2)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    state.bridge.setToonShadowParams(opacity, minElevation, slabDepth, slabRise, edgeSoftness, showVolume != 0);
    state.toonLookChanged = true;
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL FlushToonShadows(uint8_t layer) {
    const auto& bridge = State().bridge;
    if (!OnOwnerThread() || !InScope() || !ValidRenderLayer(layer) || !bridge.flushToonShadows)
        return SHIP_NATIVE_UNSUPPORTED;
    return bridge.flushToonShadows(layer);
}

const ShipOotCameraV1 cameraV1{ sizeof(ShipOotCameraV1), Acquire, Release, SetView, GetView, IsOwned };
const ShipOotRenderV1 renderV1{ sizeof(ShipOotRenderV1), DrawDisplayList, MatrixPush,     MatrixPop,
                                MatrixTranslate,         MatrixScale,     MatrixRotateZYX };
const ShipOotRenderV2 renderV2{ sizeof(ShipOotRenderV2), DrawDisplayList, MatrixPush, MatrixPop,
                                MatrixTranslate, MatrixScale, MatrixRotateZYX, AcquireRenderState,
                                ReleaseRenderState, SetRenderState, EmitToonKey, EmitStencil,
                                EmitToonShadow, FlushToonShadows, SetToonRamp, SetToonShadowParams };

} // namespace

void SetOotViewBridge(const OotViewBridge& bridge) {
    State().bridge = bridge;
}

void InitializeOotNativeView(std::thread::id ownerThread) {
    ResetOotNativeView();
    State().ownerThread = ownerThread;
}

void ResetOotNativeView() {
    auto& state = State();
    if (state.token) {
        DropCamera(true);
    }
    state.scopes.clear();
    state.renderStates.clear();
    RebuildRenderFeatures();
}

const ShipOotCameraV1& GetOotNativeCameraService() {
    return cameraV1;
}

const ShipOotRenderV1& GetOotNativeRenderService() {
    return renderV1;
}

const ShipOotRenderV2& GetOotNativeRenderServiceV2() {
    return renderV2;
}

void ReleaseOotRenderOwner(std::string_view owner) {
    auto& states = State().renderStates;
    std::erase_if(states, [owner](const auto& state) { return state.owner == owner; });
    RebuildRenderFeatures();
}

void UpdateOotCamera() {
    auto& state = State();
    if (!state.token || !Holds(state.token)) {
        return;
    }
    if (state.hasView && state.bridge.cameraApply) {
        state.bridge.cameraApply(state.camera, state.play, state.view);
    }
}

bool InOotRenderScope() {
    return InScope();
}

bool OotRenderToonActorsEnabled() {
    return (RenderFeatures() & LINKSPAN_OOT_RENDER_FEATURE_TOON_ACTORS) != 0;
}

bool OotRenderVanillaShadowsSuppressed() {
    return (RenderFeatures() & LINKSPAN_OOT_RENDER_FEATURE_SUPPRESS_VANILLA_SHADOWS) != 0;
}

bool OotRenderVanillaPointGlowHidden() {
    return (RenderFeatures() & LINKSPAN_OOT_RENDER_FEATURE_HIDE_VANILLA_POINT_GLOW) != 0;
}

bool OotRenderStateActive() {
    return !State().renderStates.empty();
}

bool OotRenderHasShadowReceivers() {
    for (const auto& state : State().renderStates) if (!state.receivers.empty()) return true;
    return false;
}

bool OotRenderIsShadowReceiver(int16_t actorId) {
    for (const auto& state : State().renderStates) {
        if (std::binary_search(state.receivers.begin(), state.receivers.end(), actorId)) return true;
    }
    return false;
}

void EnterOotRenderScope() {
    State().scopes.push_back(0);
}

void LeaveOotRenderScope() {
    auto& state = State();
    if (state.scopes.empty()) {
        return;
    }
    for (uint32_t pushed = state.scopes.back(); pushed; --pushed) {
        if (state.bridge.matrixPop) {
            state.bridge.matrixPop();
        }
    }
    state.scopes.pop_back();
}

} // namespace ShipLuaHost

extern "C" int32_t LinkSpan_RenderToonActorsEnabled(void) {
    return ShipLuaHost::OotRenderToonActorsEnabled() ? 1 : 0;
}

extern "C" int32_t LinkSpan_RenderSuppressVanillaShadows(void) {
    return ShipLuaHost::OotRenderVanillaShadowsSuppressed() ? 1 : 0;
}

extern "C" int32_t LinkSpan_RenderHideVanillaPointGlow(void) {
    return ShipLuaHost::OotRenderVanillaPointGlowHidden() ? 1 : 0;
}

extern "C" int32_t LinkSpan_RenderHasShadowReceivers(void) {
    return ShipLuaHost::OotRenderHasShadowReceivers() ? 1 : 0;
}

extern "C" int32_t LinkSpan_RenderIsShadowReceiver(int16_t actorId) {
    return ShipLuaHost::OotRenderIsShadowReceiver(actorId) ? 1 : 0;
}

extern "C" int32_t LinkSpan_RenderStateActive(void) {
    return ShipLuaHost::OotRenderStateActive() ? 1 : 0;
}
