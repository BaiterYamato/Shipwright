#include "OotNativeView.h"

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

const ShipOotCameraV1 cameraV1{ sizeof(ShipOotCameraV1), Acquire, Release, SetView, GetView, IsOwned };
const ShipOotRenderV1 renderV1{ sizeof(ShipOotRenderV1), DrawDisplayList, MatrixPush,     MatrixPop,
                                MatrixTranslate,         MatrixScale,     MatrixRotateZYX };

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
}

const ShipOotCameraV1& GetOotNativeCameraService() {
    return cameraV1;
}

const ShipOotRenderV1& GetOotNativeRenderService() {
    return renderV1;
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
