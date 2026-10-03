#pragma once
#include "oot_lights.h"

namespace LinkSpanNei {

// Host-owned light: scene transitions invalidate its handle instead of leaving
// a pointer into the game's recycled LightNode buffer.
class LanternLight {
public:
    void Bind(const ShipOotLightsV1* service) { mService = service; }
    void Set(const ShipOotPointLightV1& light) {
        mLast = light;
        if (!light.radius) { Clear(); return; }
        if (!mService) return;
        if (mHandle && mService->update_point_light(mHandle, &light) == SHIP_NATIVE_OK) return;
        Clear();
        mService->create_point_light("linkspan.nei", &light, &mHandle);
    }
    void Clear() {
        if (mHandle && mService) mService->destroy_point_light(mHandle);
        mHandle = 0;
    }
    bool Active() const {
        const void* info = nullptr;
        return mHandle && mService &&
               mService->get_point_light_info(mHandle, &info) == SHIP_NATIVE_OK && info;
    }
    const ShipOotPointLightV1& Last() const { return mLast; }
private:
    const ShipOotLightsV1* mService = nullptr;
    uint64_t mHandle = 0;
    ShipOotPointLightV1 mLast{};
};

} // namespace LinkSpanNei
