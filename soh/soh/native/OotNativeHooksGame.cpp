// Liga os pontos de hook do host (OotNativeHooks.cpp) às chamadas do jogo.
#include "OotNativeHooks.h"
#include "OotNativeSkeletons.h"
#include "OotNativePlayerAction.h"
#include "OotNativePlayerBody.h"
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
#include "../../src/code/linkspan_vanilla.h"

extern "C" {
#include "functions.h"
#include "variables.h"
void Play_Update(PlayState* play);
}

extern "C" void LinkSpan_ActorHealthInit(Actor* actor);
extern "C" void ShipLua_EmitActorInit(Actor* actor);
float LinkSpan_ExtendedDamage(PlayState* play, Actor* target, float damage) noexcept;
bool LinkSpan_ActorHealthInfo(Actor* actor, uint32_t* health, uint32_t* maxHealth, bool* extended);

namespace ShipLuaHost {
// Overloads internos: os headers do SDK e o layout ficam intactos.
float DispatchOotActorDamage(ShipOotActorDamageHookV1* payload, bool extended);
float FinalizeOotActorDamage(float originalDamage, float transformedDamage, uint8_t accumulatedDamage,
                             bool nativeChanged, bool extended);
} // namespace ShipLuaHost

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
    if (actor->update)
        actor->update(actor, static_cast<PlayState*>(payload->play_state));
    return SHIP_NATIVE_OK;
}

ShipNativeStatus SHIP_NATIVE_CALL ActorDrawOriginal(const ShipNativeHookCall* call) {
    const auto* payload = static_cast<ShipOotActorHookV1*>(call->payload);
    auto* actor = static_cast<Actor*>(payload->actor);
    if (actor->draw)
        actor->draw(actor, static_cast<PlayState*>(payload->play_state));
    return SHIP_NATIVE_OK;
}

void DispatchActor(uint64_t point, Actor* actor, PlayState* play, ShipNativeHookOriginalFn original) {
    ShipOotActorHookV1 payload{ sizeof(ShipOotActorHookV1), play, actor, actor->id, actor->params };
    ShipLuaHost::GetOotHookRegistry()->Dispatch(point, &payload, sizeof(payload), original, nullptr);
}

void DispatchRenderActorDraw(Actor* actor, PlayState* play) {
    auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().renderActorDraw;
    if (!registry || !registry->HasHooks(point))
        return;
    ShipOotRenderActorDrawHookV1 payload{ sizeof(ShipOotRenderActorDrawHookV1), play, actor, actor->id, actor->params };
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
}

void DispatchRenderPlay(uint64_t point, PlayState* play) {
    auto* registry = ShipLuaHost::GetOotHookRegistry();
    if (!registry || !registry->HasHooks(point))
        return;
    ShipOotRenderPlayHookV1 payload{ sizeof(ShipOotRenderPlayHookV1), play };
    ShipLuaHost::EnterOotRenderScope(play->state.gfxCtx);
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
    ShipLuaHost::LeaveOotRenderScope();
}
} // namespace

// Chamadas pelo código C do jogo, sempre na thread do jogo.
extern "C" void LinkSpan_PlayUpdate(PlayState* play) {
    // HOST-SLIM-001: esqueletos V2 destruídos no frame anterior; a fila de animação deles já foi zerada ou rodada.
    ShipLuaHost::ReleaseRetiredOotSkeletons();
    const auto* registry = ShipLuaHost::GetOotHookRegistry();
    const auto point = ShipLuaHost::GetOotHookPoints().playUpdate;
    if (!registry || !registry->HasHooks(point)) {
        Play_Update(play);
        return;
    }
    ShipOotPlayHookV1 payload{ sizeof(ShipOotPlayHookV1), play };
    ShipLuaHost::GetOotHookRegistry()->Dispatch(point, &payload, sizeof(payload), PlayUpdateOriginal, nullptr);
}

extern "C" void LinkSpan_ActorInit(Actor* actor, PlayState* play) {
    if (!LinkSpan_EngineExtended() || actor == nullptr) {
        return;
    }
    LinkSpan_ActorHealthInit(actor);
    try {
        const auto* registry = ShipLuaHost::GetOotHookRegistry();
        const auto point = ShipLuaHost::GetOotHookPoints().actorInit;
        if (registry && registry->HasHooks(point)) {
            DispatchActor(point, actor, play, nullptr);
        }
    } catch (...) {
        // O init Lua continua mesmo se o despacho nativo falhar.
    }
    ShipLua_EmitActorInit(actor);
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
    if (!registry || !registry->HasHooks(point))
        return;
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
    if (!registry || !registry->HasHooks(point))
        return;
    ShipOotRenderFileSelectSkyHookV1 payload{ sizeof(ShipOotRenderFileSelectSkyHookV1), gameState, graphicsContext,
                                              view };
    ShipLuaHost::EnterOotRenderScope(graphicsContext);
    registry->Dispatch(point, &payload, sizeof(payload), nullptr, nullptr);
    ShipLuaHost::LeaveOotRenderScope();
}

extern "C" void LinkSpan_PointLightColor(LightInfo* info, u8* r, u8* g, u8* b, s16 radius) {
    if (!info || !r || !g || !b || !ShipLuaHost::HasOotPointLightColorHooks())
        return;
    ShipOotPointLightColorHookV2 payload{ sizeof(ShipOotPointLightColorHookV2),
                                          info->params.point.x,
                                          info->params.point.y,
                                          info->params.point.z,
                                          radius,
                                          info->type,
                                          *r,
                                          *g,
                                          *b,
                                          info };
    ShipLuaHost::DispatchOotPointLightColor(&payload);
    *r = payload.r;
    *g = payload.g;
    *b = payload.b;
}

namespace {
void CopyFairyLight(ShipOotFairyLightV1& out, const LightInfo* info) {
    const LightPoint& point = info->params.point;
    out = ShipOotFairyLightV1{ info,
                               { point.x, point.y, point.z },
                               point.radius,
                               { point.color[0], point.color[1], point.color[2] },
                               info->type };
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
    if (!registry || !registry->HasHooks(point) || !actor || !outerColor || !noGlow || !glow)
        return;
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
    // SOH [Unbound] The TRANSFORM ceiling cannot hold this resource; keep its original list.
    if (count > LINKSPAN_OOT_ROOM_ACTORS_MAX) {
        SPDLOG_WARN("[Unbound] oot.room.actors cena {} sala {} camada {}: {} atores excedem {}; "
                    "hooks ignorados, lista do recurso mantida",
                    play->sceneNum, play->roomCtx.curRoom.num, layer, count, LINKSPAN_OOT_ROOM_ACTORS_MAX);
        return;
    }
    const uint32_t capacity =
        std::min<uint32_t>(LINKSPAN_OOT_ROOM_ACTORS_MAX, std::max<uint32_t>(count * 2, count + 256));
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
    if (result != count ||
        (count && std::memcmp(buffer.data(), play->setupActorList, count * sizeof(ActorEntry)) != 0)) {
        SPDLOG_INFO("Link-Span: oot.room.actors cena {} sala {} camada {}: {} -> {} atores", play->sceneNum, room,
                    layer, count, result);
    }
    play->setupActorList = reinterpret_cast<ActorEntry*>(buffer.data());
    play->numSetupActors = static_cast<u16>(result);
}

// HOST-SLIM-001 (RFC 0029): pontes dos hooks do Link e do Play, chamadas pelo código C do jogo na thread do jogo.
extern "C" {
extern PlayState* gPlayState;
u8 ShipLua_ShouldBlockRoll(void);
}

namespace {
struct PlayerActionCall {
    Player* player;
    PlayState* play;
};

void RunPlayerAction(void* context) {
    const auto* call = static_cast<PlayerActionCall*>(context);
    call->player->actionFunc(call->player, call->play);
}

struct PlayerMaskCall {
    PlayState* play;
    Player* player;
    void (*draw)(PlayState*, Player*);
};

void RunPlayerMaskDraw(void* context) {
    const auto* call = static_cast<PlayerMaskCall*>(context);
    call->draw(call->play, call->player);
}
} // namespace

extern "C" u8 LinkSpan_PlayerVoiceSfx(Player* player, u16* sfxId) {
    return ShipLuaHost::DispatchOotPlayerVoice(gPlayState, player, sfxId);
}

// O veto antigo (set_roll_blocked e o corpo customizado, até a F4) é o valor de entrada.
extern "C" s32 LinkSpan_PlayerRollAllowed(Player* player, PlayState* play) {
    return ShipLuaHost::DispatchOotPlayerGate(ShipLuaHost::GetOotHookPoints().playerRollAllowed, play, player,
                                              !ShipLua_ShouldBlockRoll());
}

extern "C" s32 LinkSpan_PlayerLedgeGrabAllowed(Player* player, PlayState* play) {
    return ShipLuaHost::DispatchOotPlayerGate(ShipLuaHost::GetOotHookPoints().playerLedgeGrabAllowed, play, player,
                                              true);
}

extern "C" void LinkSpan_PlayerAction(Player* player, PlayState* play, Input* input, s32 vanillaAllowed) {
    PlayerActionCall call{ player, play };
    ShipLuaHost::DispatchOotPlayerAction(play, player, input, vanillaAllowed != 0, RunPlayerAction, &call);
}

extern "C" void LinkSpan_PlayerUpdateEnd(Player* player, PlayState* play) {
    ShipLuaHost::OotPlayerBodyPlayerUpdate(play, player);
    ShipLuaHost::DispatchOotPlayerTick(ShipLuaHost::GetOotHookPoints().playerUpdateEnd, play, player);
}

extern "C" void LinkSpan_PlayerMaskDraw(PlayState* play, Player* player, s32 vanillaAllowed,
                                        void (*drawVanilla)(PlayState*, Player*)) {
    PlayerMaskCall call{ play, player, drawVanilla };
    const bool hooked = ShipLuaHost::HasOotHooks(ShipLuaHost::GetOotHookPoints().playerMaskDraw);
    if (hooked)
        ShipLuaHost::EnterOotRenderScope(play->state.gfxCtx);
    ShipLuaHost::DispatchOotPlayerMaskDraw(play, player, player->currentMask, vanillaAllowed != 0, RunPlayerMaskDraw,
                                           &call);
    if (hooked)
        ShipLuaHost::LeaveOotRenderScope();
}

extern "C" void LinkSpan_PlayDestroy(PlayState* play) {
    ShipLuaHost::DispatchOotPlayDestroy(play);
}

// HOST-SLIM-001 (RFC 0029): leitura e Idle do Link para linkspan.oot.movement V3; helpers em z_player.c.
extern "C" {
u32 LinkSpan_PlayerActionFlags(Player* player);
s32 LinkSpan_PlayerActionEligible(Player* player, PlayState* play);
s32 LinkSpan_PlayerRestoreIdle(Player* player, PlayState* play, s32 setupIdle);
}

namespace {
bool SnapshotPlayerAction(ShipLuaHost::OotActionSnapshot* out) {
    PlayState* play = gPlayState;
    Player* player = play ? GET_PLAYER(play) : nullptr;
    if (!player) {
        return false;
    }
    out->play = play;
    out->player = player;
    out->action = reinterpret_cast<const void*>(player->actionFunc);
    out->tick = play->gameplayFrames;
    out->flags = LinkSpan_PlayerActionFlags(player);
    out->eligible = LinkSpan_PlayerActionEligible(player, play) != 0;
    out->neutral = out->eligible && (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
    return true;
}

ShipNativeStatus RestorePlayerAction(const ShipLuaHost::OotActionSnapshot& at, bool releaseIdle) {
    auto* play = static_cast<PlayState*>(at.play);
    auto* player = static_cast<Player*>(at.player);
    if (play != gPlayState || GET_PLAYER(play) != player) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    return LinkSpan_PlayerRestoreIdle(player, play, releaseIdle) ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}
} // namespace

void ShipLuaHost::RegisterOotPlayerActionGame() {
    SetOotActionBridge({ SnapshotPlayerAction, RestorePlayerAction });
}

extern "C" void LinkSpan_PlayerCreated(Player* player) {
    ShipLuaHost::NotifyOotPlayerCreated(player);
}

extern "C" void LinkSpan_PlayerDestroyed(Player* player) {
    ShipLuaHost::NotifyOotPlayerDestroyed(player);
}

extern "C" void LinkSpan_PlayerActionChanged(Player* player) {
    ShipLuaHost::NotifyOotPlayerActionChanged(player);
}

// HOST-SLIM-001 (RFC 0029). Definida em ShipLuaBootstrap.cpp (passo 6).
extern "C" float ShipLua_TransformActorDamage(Actor* target, Actor* attacker, uint32_t dmgFlags, uint8_t damageEffect,
                                              uint8_t attackerIsPlayer, float baseDamage, float damage);

extern "C" float LinkSpan_ActorDamage(PlayState* play, Collider* collider, ColliderInfo* info, float damage) {
    if (!LinkSpan_EngineExtended() || !collider || !collider->actor || !info || !info->acHitInfo) {
        return damage;
    }
    try {
        Actor* target = collider->actor;
        Actor* attacker = info->acHit ? info->acHit->actor : nullptr;
        const uint32_t dmgFlags = info->acHitInfo->toucher.dmgFlags;
        const uint8_t damageEffect = target->colChkInfo.damageEffect;
        const uint8_t attackerIsPlayer = info->acHit && (info->acHit->atFlags & AT_TYPE_PLAYER) ? 1 : 0;
        uint32_t health = 0;
        uint32_t maxHealth = 0;
        bool extended = false;
        LinkSpan_ActorHealthInfo(target, &health, &maxHealth, &extended);
        ShipOotActorDamageHookV1 payload{ sizeof(payload),  play, target, attacker, dmgFlags, damageEffect,
                                          attackerIsPlayer, {},   damage, damage };
        const float nativeDamage = ShipLuaHost::DispatchOotActorDamage(&payload, extended);
        const float finalDamage = ShipLua_TransformActorDamage(target, attacker, dmgFlags, damageEffect,
                                                               attackerIsPlayer, damage, nativeDamage);
        // Um callback pode ter ligado ou reescalado a vida; consulta novamente.
        LinkSpan_ActorHealthInfo(target, &health, &maxHealth, &extended);
        const float bounded = ShipLuaHost::FinalizeOotActorDamage(damage, finalDamage, target->colChkInfo.damage,
                                                                  nativeDamage != damage, extended);
        return LinkSpan_ExtendedDamage(play, target, bounded);
    } catch (...) {
        // Mesmo na falha, um alvo estendido recebe só a queda do espelho.
        return LinkSpan_ExtendedDamage(play, collider->actor, damage);
    }
}
