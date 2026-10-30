#include "../include/oot_scene_query.h"
#include "z64.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
extern "C" {
PlayState* gPlayState = nullptr;
}
namespace ShipLuaHost {
static uint64_t testEpoch = 1;
uint64_t OotSceneEpoch() {
    return testEpoch;
}
void InitializeOotSceneQuery(std::thread::id);
void BeginOotSceneQuery(void*);
void CaptureOotSceneList(void*, uint32_t, const void*, uint32_t, uint32_t);
const ShipOotSceneQueryV1& GetOotSceneQueryService();
} // namespace ShipLuaHost
void Check(bool b) {
    if (!b)
        throw std::runtime_error("scene query: verificação falhou");
}
int main() {
    using namespace ShipLuaHost;
    try {
        auto p = std::make_unique<PlayState>();
        gPlayState = p.get();
        InitializeOotSceneQuery(std::this_thread::get_id());
        const auto& s = GetOotSceneQueryService();
        ShipOotSceneListV1 out{};
        out.size = sizeof(out);
        Check(s.get_list(0, &out) == SHIP_NATIVE_UNSUPPORTED);
        BeginOotSceneQuery(p.get());
        ActorEntry entries[2]{};
        p->linkActorEntry = &entries[1];
        CaptureOotSceneList(p.get(), 0, entries, 2, sizeof(ActorEntry));
        Check(s.get_list(0, &out) == SHIP_NATIVE_OK && out.count == 2 && out.stride == sizeof(ActorEntry));
        Check(s.get_list(1, &out) == SHIP_NATIVE_OK && !out.count);
        EntranceEntry entrances[1]{};
        p->setupEntranceList = entrances;
        CaptureOotSceneList(p.get(), 1, entrances, 1, sizeof(EntranceEntry));
        Check(s.get_list(1, &out) == SHIP_NATIVE_OK && out.data == entrances);
        p->setupEntranceList = nullptr;
        Check(s.get_list(1, &out) == SHIP_NATIVE_UNSUPPORTED);
        uint16_t exits[1]{ 266 };
        p->setupExitList = reinterpret_cast<s16*>(exits);
        CaptureOotSceneList(p.get(), 2, exits, 1, sizeof(uint16_t));
        Check(s.get_list(2, &out) == SHIP_NATIVE_OK && out.count == 1);
        ShipNativeStatus other = SHIP_NATIVE_OK;
        std::thread worker([&] {
            ShipOotSceneListV1 x{};
            x.size = sizeof(x);
            other = s.get_list(0, &x);
        });
        worker.join();
        Check(other == SHIP_NATIVE_INVALID_ARGUMENT);
        ++testEpoch;
        Check(s.get_list(0, &out) == SHIP_NATIVE_UNSUPPORTED);
        BeginOotSceneQuery(p.get());
        Check(s.get_list(0, &out) == SHIP_NATIVE_OK && out.count == 0);
        Check(s.get_list(3, &out) == SHIP_NATIVE_INVALID_ARGUMENT);
        Check(s.get_list(0, nullptr) == SHIP_NATIVE_INVALID_ARGUMENT);
        std::cout << "scene query: contagens, ponteiros, thread e epoch OK\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
