#include "OotNativeLights.h"
#include "../Enhancements/savestate_native_lights_guard.h"

#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace ShipLuaHost {
namespace {

// O handle leva a vaga nos 4 bits de baixo e um serial crescente no resto: um handle de uma luz já apagada nunca
// aponta para a luz nova da mesma vaga.
constexpr uint32_t kSlotBits = 4;
static_assert(LINKSPAN_OOT_LIGHTS_MAX <= (1u << kSlotBits), "vaga precisa caber no handle");

struct Slot {
    bool used = false;
    uint64_t serial = 0;
    std::string owner;
    void* play = nullptr;
};

struct LightsState {
    std::thread::id ownerThread;
    OotLightsBridge bridge;
    uint64_t nextSerial = 1;
    // Private savestate epoch, never rewound. Zero permanently disables loads after wrap.
    uint64_t saveStateGeneration = 1;
    std::array<Slot, LINKSPAN_OOT_LIGHTS_MAX> slots;
    // Uma operação no meio da bridge: Lights_PointSetInfo dispara oot.light.point_color, e um callback ali não pode
    // criar, mudar ou apagar luz de mod enquanto a vaga está pela metade.
    bool busy = false;
};

LightsState& State() {
    static LightsState state;
    return state;
}

// Invalidate before entering callbacks, including failed insertions that write HostLight::info.
void AdvanceSaveStateGeneration() {
    auto& generation = State().saveStateGeneration;
    if (generation != 0) ++generation;
}

// Marca a bridge ocupada durante uma operação; a reentrada responde UNSUPPORTED sem mexer em vaga.
struct BusyScope {
    bool previous;
    BusyScope() : previous(State().busy) { State().busy = true; }
    ~BusyScope() { State().busy = previous; }
    BusyScope(const BusyScope&) = delete;
    BusyScope& operator=(const BusyScope&) = delete;
};

bool OnOwnerThread() {
    return State().ownerThread == std::this_thread::get_id();
}

void* Gameplay() {
    const auto& bridge = State().bridge;
    return bridge.gameplay ? bridge.gameplay() : nullptr;
}

bool ValidOwner(const char* text) {
    if (!text) return false;
    const size_t length = strnlen(text, LINKSPAN_OOT_LIGHTS_MAX_OWNER + 1);
    if (length == 0 || length > LINKSPAN_OOT_LIGHTS_MAX_OWNER) return false;
    for (size_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(text[i]) < 0x20) return false;
    }
    return true;
}

bool ValidLight(const ShipOotPointLightV1* light) {
    return light && light->size >= sizeof(ShipOotPointLightV1) && std::isfinite(light->position[0]) &&
           std::isfinite(light->position[1]) && std::isfinite(light->position[2]) && light->radius >= 0 &&
           light->glow <= 1;
}

// Vaga viva do handle, ou nullptr. `issued` diz se o handle já saiu daqui alguma vez.
Slot* Find(uint64_t handle, bool* issued = nullptr) {
    auto& state = State();
    const uint64_t serial = handle >> kSlotBits;
    const uint32_t index = static_cast<uint32_t>(handle & ((1u << kSlotBits) - 1));
    if (issued) *issued = serial != 0 && serial < state.nextSerial && index < LINKSPAN_OOT_LIGHTS_MAX;
    if (index >= LINKSPAN_OOT_LIGHTS_MAX || serial == 0) return nullptr;
    Slot& slot = state.slots[index];
    return slot.used && slot.serial == serial ? &slot : nullptr;
}

uint32_t IndexOf(const Slot& slot) {
    return static_cast<uint32_t>(&slot - State().slots.data());
}

// Tira a luz da lista se a cena dela ainda for a atual e libera a vaga.
void Free(Slot& slot) {
    AdvanceSaveStateGeneration();
    const auto& bridge = State().bridge;
    if (bridge.remove && slot.play && slot.play == Gameplay()) {
        BusyScope busy;
        bridge.remove(slot.play, IndexOf(slot));
    }
    slot = Slot{};
}

// Vaga viva e ainda na cena atual; uma de outra cena sai aqui mesmo.
Slot* Live(uint64_t handle, bool* issued = nullptr) {
    Slot* slot = Find(handle, issued);
    if (slot && slot->play != Gameplay()) {
        AdvanceSaveStateGeneration();
        *slot = Slot{};
        return nullptr;
    }
    return slot;
}

ShipNativeStatus SHIP_NATIVE_CALL CreatePointLight(const char* owner, const ShipOotPointLightV1* light,
                                                   uint64_t* handle) {
    if (!OnOwnerThread() || !ValidOwner(owner) || !ValidLight(light) || !handle) return SHIP_NATIVE_INVALID_ARGUMENT;
    *handle = 0;
    auto& state = State();
    if (state.busy || !state.bridge.insert) return SHIP_NATIVE_UNSUPPORTED;
    void* play = Gameplay();
    if (!play) return SHIP_NATIVE_UNSUPPORTED;
    for (auto& slot : state.slots) {
        if (slot.used) continue;
        try {
            std::string name(owner);
            BusyScope busy;
            AdvanceSaveStateGeneration();
            if (!state.bridge.insert(play, IndexOf(slot), *light)) return SHIP_NATIVE_LIMIT;
            slot.used = true;
            slot.serial = state.nextSerial++;
            slot.owner = std::move(name);
            slot.play = play;
            *handle = (slot.serial << kSlotBits) | IndexOf(slot);
            return SHIP_NATIVE_OK;
        } catch (...) { return SHIP_NATIVE_FAILURE; }
    }
    return SHIP_NATIVE_LIMIT;
}

ShipNativeStatus SHIP_NATIVE_CALL UpdatePointLight(uint64_t handle, const ShipOotPointLightV1* light) {
    if (!OnOwnerThread() || !ValidLight(light)) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (State().busy) return SHIP_NATIVE_UNSUPPORTED;
    bool issued = false;
    Slot* slot = Live(handle, &issued);
    if (!slot) return issued ? SHIP_NATIVE_UNSUPPORTED : SHIP_NATIVE_INVALID_ARGUMENT;
    const auto& bridge = State().bridge;
    if (!bridge.update) return SHIP_NATIVE_UNSUPPORTED;
    BusyScope busy;
    // Same node and LightInfo address: value updates do not invalidate snapshots.
    bridge.update(IndexOf(*slot), *light);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL DestroyPointLight(uint64_t handle) {
    if (!OnOwnerThread()) return SHIP_NATIVE_INVALID_ARGUMENT;
    if (State().busy) return SHIP_NATIVE_UNSUPPORTED;
    bool issued = false;
    Slot* slot = Find(handle, &issued);
    if (!slot) return issued ? SHIP_NATIVE_OK : SHIP_NATIVE_INVALID_ARGUMENT; // já saiu com a cena ou o unload
    Free(*slot);
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL GetPointLightInfo(uint64_t handle, const void** info) {
    if (!OnOwnerThread() || !info) return SHIP_NATIVE_INVALID_ARGUMENT;
    *info = nullptr;
    bool issued = false;
    Slot* slot = Live(handle, &issued);
    if (!slot) return issued ? SHIP_NATIVE_UNSUPPORTED : SHIP_NATIVE_INVALID_ARGUMENT;
    const auto& bridge = State().bridge;
    if (!bridge.info) return SHIP_NATIVE_UNSUPPORTED;
    *info = bridge.info(IndexOf(*slot));
    return SHIP_NATIVE_OK;
}

const ShipOotLightsV1 lightsV1{ sizeof(ShipOotLightsV1), CreatePointLight, UpdatePointLight, DestroyPointLight,
                                GetPointLightInfo };

} // namespace

void SetOotLightsBridge(const OotLightsBridge& bridge) {
    AdvanceSaveStateGeneration();
    State().bridge = bridge;
}

void InitializeOotNativeLights(std::thread::id ownerThread) {
    ResetOotNativeLights();
    AdvanceSaveStateGeneration();
    State().ownerThread = ownerThread;
}

void ResetOotNativeLights() {
    for (auto& slot : State().slots) {
        if (slot.used) Free(slot);
    }
}

// A snapshot captured or checked inside a bridge callback is never loadable.
uint64_t OotLightsSaveStateGeneration() {
    return State().busy ? 0 : State().saveStateGeneration;
}

const ShipOotLightsV1& GetOotNativeLightsService() {
    return lightsV1;
}

void ReleaseOotLightOwner(std::string_view owner) {
    for (auto& slot : State().slots) {
        if (slot.used && slot.owner == owner) Free(slot);
    }
}

void ReleaseOotSceneLights() {
    ResetOotNativeLights();
}

uint32_t OotLightCount() {
    uint32_t count = 0;
    for (const auto& slot : State().slots) count += slot.used ? 1 : 0;
    return count;
}

} // namespace ShipLuaHost
