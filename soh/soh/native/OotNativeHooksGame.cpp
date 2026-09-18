// Liga os pontos de hook do host (OotNativeHooks.cpp) às chamadas do jogo.
#include "OotNativeHooks.h"
#include "OotNativeView.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

#include "oot_hooks.h"

#include "z64.h"
#include "macros.h"

extern "C" {
#include "functions.h"
#include "variables.h"
void Play_Update(PlayState* play);
}

namespace {
[[maybe_unused]] const bool kHookLoggerBound = [] {
    ShipLuaHost::SetOotHookLogger([](const std::string& message) { SPDLOG_WARN("Link-Span hooks: {}", message); });
    return true;
}();

ShipNativeStatus SHIP_NATIVE_CALL PlayUpdateOriginal(const ShipNativeHookCall* call) {
    const auto* payload = static_cast<ShipOotPlayHookV1*>(call->payload);
    Play_Update(static_cast<PlayState*>(payload->play_state));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL ActorUpdateOriginal(const ShipNativeHookCall* call) {
    const auto* payload = static_cast<ShipOotActorHookV1*>(call->payload);
    auto* actor = static_cast<Actor*>(payload->actor);
    if (actor->update) actor->update(actor, static_cast<PlayState*>(payload->play_state));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL ActorDrawOriginal(const ShipNativeHookCall* call) {
    const auto* payload = static_cast<ShipOotActorHookV1*>(call->payload);
    auto* actor = static_cast<Actor*>(payload->actor);
    if (actor->draw) actor->draw(actor, static_cast<PlayState*>(payload->play_state));
    return SHIP_NATIVE_OK;
}

void DispatchActor(uint64_t point, Actor* actor, PlayState* play, ShipNativeHookOriginalFn original) {
    ShipOotActorHookV1 payload{sizeof(ShipOotActorHookV1), play, actor, actor->id, actor->params};
    ShipLuaHost::GetOotHookRegistry()->Dispatch(point, &payload, sizeof(payload), original, nullptr);
}
} // namespace

// Chamadas pelo código C do jogo, sempre na thread do jogo.
extern "C" void LinkSpan_PlayUpdate(PlayState* play) {
    const auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().playUpdate;
    if (!registry || !registry->HasHooks(point)) {
        Play_Update(play);
        return;
    }
    ShipOotPlayHookV1 payload{sizeof(ShipOotPlayHookV1), play};
    ShipLuaHost::GetOotHookRegistry()->Dispatch(point, &payload, sizeof(payload), PlayUpdateOriginal, nullptr);
}

extern "C" void LinkSpan_ActorUpdate(Actor* actor, PlayState* play) {
    const auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().actorUpdate;
    if (!registry || !registry->HasHooks(point)) {
        actor->update(actor, play);
        return;
    }
    DispatchActor(point, actor, play, ActorUpdateOriginal);
}

extern "C" void LinkSpan_ActorDraw(Actor* actor, PlayState* play) {
    const auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().actorDraw;
    if (!registry || !registry->HasHooks(point)) {
        actor->draw(actor, play);
        return;
    }
    // Replace e observe desenham com linkspan.oot.render sobre a matriz do ator.
    ShipLuaHost::EnterOotRenderScope();
    DispatchActor(point, actor, play, ActorDrawOriginal);
    ShipLuaHost::LeaveOotRenderScope();
}

static_assert(sizeof(ShipOotActorEntryV2) == sizeof(ActorEntry) &&
                  offsetof(ShipOotActorEntryV2, pos) == offsetof(ActorEntry, pos) &&
                  offsetof(ShipOotActorEntryV2, rot) == offsetof(ActorEntry, rot) &&
                  offsetof(ShipOotActorEntryV2, params) == offsetof(ActorEntry, params),
              "ShipOotActorEntryV2 precisa espelhar ActorEntry");

// Comando de lista de atores de uma sala: copia a lista para o buffer do host, deixa os
// mods editarem (oot.room.actors) e aponta a sala para o resultado. O buffer vale até a
// próxima sala; a lista é consumida pelo spawn no Actor_UpdateAll seguinte.
extern "C" void LinkSpan_RoomActors(PlayState* play, s32 layer) {
    const auto* registry = ShipLuaHost::GetOotHookRegistry();
    if (!registry || !registry->HasHooks(ShipLuaHost::GetOotHookPoints().roomActors)) {
        return;
    }
    static std::vector<ShipOotActorEntryV2> buffer;
    const uint32_t count = play->numSetupActors;
    const uint32_t capacity = std::min<uint32_t>(LINKSPAN_OOT_ROOM_ACTORS_MAX, std::max<uint32_t>(count * 2, count + 256));
    buffer.assign(capacity, ShipOotActorEntryV2{});
    if (count) {
        std::memcpy(buffer.data(), play->setupActorList, count * sizeof(ActorEntry));
    }
    const s32 room = play->roomCtx.curRoom.num;
    const char* path = (room >= 0 && room < play->numRooms) ? play->roomList[room].fileName : nullptr;
    static const char kOtr[] = "__OTR__";
    if (path && std::strncmp(path, kOtr, sizeof(kOtr) - 1) == 0) {
        path += sizeof(kOtr) - 1;
    }
    const uint32_t result = ShipLuaHost::DispatchOotRoomActors(play, play->sceneNum, room, layer, path ? path : "",
                                                               buffer.data(), count, capacity);
    if (result != count || (count && std::memcmp(buffer.data(), play->setupActorList, count * sizeof(ActorEntry)) != 0)) {
        SPDLOG_INFO("Link-Span: oot.room.actors cena {} sala {} camada {}: {} -> {} atores", play->sceneNum, room,
                    layer, count, result);
    }
    play->setupActorList = reinterpret_cast<ActorEntry*>(buffer.data());
    play->numSetupActors = static_cast<u16>(result);
}
