// Liga os pontos de hook do host (OotNativeHooks.cpp) às chamadas do jogo.
#include "OotNativeHooks.h"
#include "OotNativeView.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
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

void DispatchRenderActorDraw(Actor* actor, PlayState* play) {
    auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().renderActorDraw;
    if (!registry || !registry->HasHooks(point)) return;
    ShipOotRenderActorDrawHookV1 payload{sizeof(ShipOotRenderActorDrawHookV1), play, actor, actor->id, actor->params};
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
}

void DispatchRenderPlay(uint64_t point, PlayState* play) {
    auto* registry = ShipLuaHost::GetOotHookRegistry();
    if (!registry || !registry->HasHooks(point)) return;
    ShipOotRenderPlayHookV1 payload{sizeof(ShipOotRenderPlayHookV1), play};
    ShipLuaHost::EnterOotRenderScope(play->state.gfxCtx);
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
    ShipLuaHost::LeaveOotRenderScope();
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

extern "C" void LinkSpan_ActorInit(Actor* actor, PlayState* play) {
    const auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().actorInit;
    if (registry && registry->HasHooks(point)) DispatchActor(point, actor, play, nullptr);
}
extern "C" void LinkSpan_PlayDrawEnd(PlayState* play) {
    DispatchRenderPlay(ShipLuaHost::GetOotHookPoints().playDrawEnd, play);
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
    ShipLuaHost::EnterOotRenderScope(play->state.gfxCtx);
    DispatchActor(point, actor, play, ActorDrawOriginal);
    ShipLuaHost::LeaveOotRenderScope();
}

extern "C" void LinkSpan_RenderActorDraw(Actor* actor, PlayState* play) {
    // CEL-004: o ator anterior saiu do colchete toon (set_actor_toon_enabled); religa antes deste, como o fork.
    ShipLuaHost::RestoreOotActorToon(play->state.gfxCtx);
    const auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().renderActorDraw;
    if (!registry || !registry->HasHooks(point)) return;
    ShipLuaHost::EnterOotRenderScope(play->state.gfxCtx, ShipLuaHost::OotRenderScopeKind::ActorDraw);
    DispatchRenderActorDraw(actor, play);
    ShipLuaHost::LeaveOotRenderScope();
}

extern "C" void LinkSpan_RenderWorldLights(PlayState* play) {
    // O último receptor do pré-passe pode ter saído do colchete toon; o opt-out é do ator, não das luzes do mundo.
    ShipLuaHost::RestoreOotActorToon(play->state.gfxCtx);
    DispatchRenderPlay(ShipLuaHost::GetOotHookPoints().renderWorldLights, play);
}

extern "C" void LinkSpan_RenderSkyGradient(PlayState* play) {
    DispatchRenderPlay(ShipLuaHost::GetOotHookPoints().renderSkyGradient, play);
}

extern "C" void LinkSpan_RenderSky(PlayState* play) {
    DispatchRenderPlay(ShipLuaHost::GetOotHookPoints().renderSky, play);
}

extern "C" void LinkSpan_RenderSkyClouds(PlayState* play) {
    DispatchRenderPlay(ShipLuaHost::GetOotHookPoints().renderSkyClouds, play);
}

extern "C" void LinkSpan_RenderFileSelectSky(void* gameState, void* graphicsContext, void* view) {
    auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().renderFileSelectSky;
    if (!registry || !registry->HasHooks(point)) return;
    ShipOotRenderFileSelectSkyHookV1 payload{sizeof(ShipOotRenderFileSelectSkyHookV1), gameState, graphicsContext, view};
    ShipLuaHost::EnterOotRenderScope(graphicsContext);
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
    ShipLuaHost::LeaveOotRenderScope();
}

extern "C" void LinkSpan_PointLightColor(LightInfo* info, u8* r, u8* g, u8* b, s16 radius) {
    if (!info || !r || !g || !b || !ShipLuaHost::HasOotPointLightColorHooks()) return;
    ShipOotPointLightColorHookV2 payload{sizeof(ShipOotPointLightColorHookV2), info->params.point.x,
                                         info->params.point.y, info->params.point.z, radius, info->type,
                                         *r, *g, *b, info};
    ShipLuaHost::DispatchOotPointLightColor(&payload);
    *r = payload.r;
    *g = payload.g;
    *b = payload.b;
}

namespace {
void CopyFairyLight(ShipOotFairyLightV1& out, const LightInfo* info) {
    const LightPoint& point = info->params.point;
    out = ShipOotFairyLightV1{info, {point.x, point.y, point.z}, point.radius,
                              {point.color[0], point.color[1], point.color[2]}, info->type};
}

// Só posição, raio e cor voltam; tipo e identidade são do ator.
void ApplyFairyLight(const ShipOotFairyLightV1& in, LightInfo* info) {
    LightPoint& point = info->params.point;
    if (std::isfinite(in.position[0]) && std::isfinite(in.position[1]) && std::isfinite(in.position[2])) {
        point.x = in.position[0];
        point.y = in.position[1];
        point.z = in.position[2];
    }
    point.radius = in.radius;
    point.color[0] = in.color[0];
    point.color[1] = in.color[1];
    point.color[2] = in.color[2];
}
} // namespace

// Chamada no fim de EnElf_Update (z_en_elf.c), na thread do jogo: oot.light.fairy sobre as duas luzes da fada.
extern "C" void LinkSpan_FairyLights(PlayState* play, Actor* actor, u16 fairyFlags, const Color_RGBAf* outerColor,
                                     LightInfo* noGlow, LightInfo* glow) {
    auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().fairyLights;
    if (!registry || !registry->HasHooks(point) || !actor || !outerColor || !noGlow || !glow) return;
    ShipOotFairyLightHookV1 payload{};
    payload.size = sizeof(payload);
    payload.play_state = play;
    payload.actor = actor;
    payload.actor_id = actor->id;
    payload.params = actor->params;
    payload.fairy_flags = fairyFlags;
    payload.outer_color[0] = outerColor->r;
    payload.outer_color[1] = outerColor->g;
    payload.outer_color[2] = outerColor->b;
    CopyFairyLight(payload.no_glow, noGlow);
    CopyFairyLight(payload.glow, glow);
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
    ApplyFairyLight(payload.no_glow, noGlow);
    ApplyFairyLight(payload.glow, glow);
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
