#pragma once

#include <cstdint>

// Private host contract: outside ExportSdk.cmake layout_headers.
namespace ShipLuaHost {
uint64_t OotLightsSaveStateGeneration();
}

struct SaveStateNativeLightsGuard {
    uint64_t generation = 0;

    void Capture() {
        generation = ShipLuaHost::OotLightsSaveStateGeneration();
    }

    bool Matches() const {
        return generation != 0 && generation == ShipLuaHost::OotLightsSaveStateGeneration();
    }
};
