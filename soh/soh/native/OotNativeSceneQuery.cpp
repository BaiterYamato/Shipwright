// Listas contadas da cena efetivamente carregada. Nenhuma política de conteúdo.
#include "../../native-sdk/include/oot_scene_query.h"
#include "oot_layout_id.h"
#include "z64.h"
#include <array>
#include <cstdint>
#include <thread>

extern "C" { extern PlayState* gPlayState; }
namespace ShipLuaHost {
uint64_t OotSceneEpoch();
namespace {
std::thread::id ownerThread;
void* capturedPlay = nullptr;
void* capturedSegment = nullptr;
uint64_t epoch = 0;
std::array<ShipOotSceneListV1, 3> lists{};
ShipNativeStatus SHIP_NATIVE_CALL GetList(uint32_t kind, ShipOotSceneListV1* out) {
    if (std::this_thread::get_id() != ownerThread || !out || out->size < sizeof(*out) || kind >= lists.size())
        return SHIP_NATIVE_INVALID_ARGUMENT;
    *out = {}; out->size = sizeof(*out);
    if (!gPlayState || capturedPlay != gPlayState || capturedSegment != gPlayState->sceneSegment || epoch != OotSceneEpoch())
        return SHIP_NATIVE_UNSUPPORTED;
    const auto& list = lists[kind];
    if ((list.count && !list.data) || list.count > 65535) return SHIP_NATIVE_LIMIT;
    // Listas substituídas por outro carregador não herdam uma contagem antiga.
    if (kind == SHIP_OOT_SCENE_ENTRANCES && list.data != gPlayState->setupEntranceList)
        return SHIP_NATIVE_UNSUPPORTED;
    if (kind == SHIP_OOT_SCENE_EXITS && list.data != gPlayState->setupExitList)
        return SHIP_NATIVE_UNSUPPORTED;
    if (kind == SHIP_OOT_SCENE_SPAWNS && list.count) {
        const uintptr_t first = reinterpret_cast<uintptr_t>(list.data);
        const uintptr_t selected = reinterpret_cast<uintptr_t>(gPlayState->linkActorEntry);
        if (selected < first || (selected - first) / list.stride >= list.count || (selected - first) % list.stride)
            return SHIP_NATIVE_UNSUPPORTED;
    }
    *out = list;
    return SHIP_NATIVE_OK;
}
const ShipOotSceneQueryV1 service{ sizeof(service), LINKSPAN_OOT_LAYOUT_ID, GetList };
}
void InitializeOotSceneQuery(std::thread::id thread) {
    ownerThread = thread; capturedPlay = nullptr; capturedSegment = nullptr; epoch = 0; lists = {};
}
void BeginOotSceneQuery(void* p) {
    if (std::this_thread::get_id() != ownerThread || !p) return;
    auto* play = static_cast<PlayState*>(p);
    capturedPlay = p; capturedSegment = play->sceneSegment; epoch = OotSceneEpoch(); lists = {};
    for (uint32_t i = 0; i < lists.size(); ++i) {
        lists[i].size = sizeof(ShipOotSceneListV1);
        lists[i].stride = i == 0 ? sizeof(ActorEntry) : (i == 1 ? sizeof(EntranceEntry) : sizeof(uint16_t));
    }
}
void CaptureOotSceneList(void* p, uint32_t kind, const void* data, uint32_t count, uint32_t stride) {
    if (std::this_thread::get_id() != ownerThread || !p || p != capturedPlay || epoch != OotSceneEpoch() || kind >= lists.size()) return;
    lists[kind] = { sizeof(ShipOotSceneListV1), count, stride, 0, data };
}
const ShipOotSceneQueryV1& GetOotSceneQueryService() { return service; }
} // namespace ShipLuaHost
