#pragma once

#include <cstdint>

namespace WWStyle {

// Uma transição de cena pode criar outro PlayState no mesmo endereço.
struct PlayStamp {
    const void* play = nullptr;
    int16_t scene = -1;
    uint32_t frames = 0;
};

inline bool ObservePlay(PlayStamp& stamp, const void* play, int16_t scene, uint32_t frames) noexcept {
    const bool fresh = play != stamp.play || scene != stamp.scene || frames < stamp.frames;
    stamp = { play, scene, frames };
    return fresh;
}

} // namespace WWStyle