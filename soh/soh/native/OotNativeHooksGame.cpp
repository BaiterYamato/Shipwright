// Liga os pontos de hook do host (OotNativeHooks.cpp) às chamadas do jogo.
#include "OotNativeHooks.h"
#include "OotNativeView.h"

#include <spdlog/spdlog.h>

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
