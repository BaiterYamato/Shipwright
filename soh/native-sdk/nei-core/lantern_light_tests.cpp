#include "lantern_light.h"
#include <cstdlib>

namespace {
int creates = 0, destroys = 0, updates = 0;
uint64_t live = 0;
ShipOotPointLightV1 last{};
ShipNativeStatus SHIP_NATIVE_CALL Create(const char*, const ShipOotPointLightV1* light, uint64_t* handle) {
    last = *light; live = ++creates; *handle = live; return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Update(uint64_t handle, const ShipOotPointLightV1* light) {
    ++updates;
    if (handle != live) return SHIP_NATIVE_UNSUPPORTED;
    last = *light; return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Destroy(uint64_t handle) {
    ++destroys; if (handle == live) live = 0; return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL Info(uint64_t handle, const void** info) {
    *info = handle == live ? &last : nullptr;
    return *info ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}
}
int main() {
    ShipOotLightsV1 service{ sizeof(service), Create, Update, Destroy, Info };
    LinkSpanNei::LanternLight lantern;
    lantern.Bind(&service);
    ShipOotPointLightV1 light{ sizeof(light), {100.25f, 28.5f, -35.75f}, 200, {255,180,80}, 0 };
    lantern.Set(light);
    if (!lantern.Active() || creates != 1 || last.position[0] != 100.25f) return EXIT_FAILURE;
    light.position[0] = 150.5f;
    lantern.Set(light);
    if (creates != 1 || updates != 1 || last.position[0] != 150.5f) return EXIT_FAILURE;
    // The game destroys the old scene and its LightContext.
    live = 0;
    if (lantern.Active()) return EXIT_FAILURE;
    lantern.Set(light);
    if (!lantern.Active() || creates != 2) return EXIT_FAILURE;
    // Extinguishing or Put Away removes the light; captured fire color survives.
    light.radius = 0;
    lantern.Set(light);
    if (lantern.Active() || live) return EXIT_FAILURE;
    light.radius = 200; light.color[0] = 80; light.color[1] = 150; light.color[2] = 255;
    lantern.Set(light);
    if (!lantern.Active() || creates != 3 || last.color[2] != 255) return EXIT_FAILURE;
    lantern.Clear();
    lantern.Clear();
    return !lantern.Active() && destroys == 3 ? EXIT_SUCCESS : EXIT_FAILURE;
}
