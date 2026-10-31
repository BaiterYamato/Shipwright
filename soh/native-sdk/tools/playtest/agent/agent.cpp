#include "server.h"
#include "gameplay.h"
#include "oot_engine.h"
#include "shiplua/native/ship_native_abi.h"
#include "z64.h"
#include "z64save.h"
#include "z64player.h"
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
std::unique_ptr<Playtest::Server> gServer;
Playtest::Gameplay gGameplay;
GameState** gState = nullptr;
SaveContext* gSave = nullptr;
// O GameState_Init zera `init` depois de rodá-lo; o `destroy` que o init gravou identifica o estado atual.
uintptr_t gPlayDestroy = 0, gFileDestroy = 0, gTitleDestroy = 0, gSetupDestroy = 0, gOpeningDestroy = 0;
uint64_t gFrame = 0;
uint32_t gPrevious = 0;
bool gFirst = true, gSail = false;
uintptr_t Resolve(const ShipNativeRuntime* runtime, const char* name) {
    uintptr_t address = 0;
    if (runtime->resolve_symbol(runtime->context, name, &address) != SHIP_NATIVE_OK)
        return 0;
    return address;
}
void Log(const char* message) {
    // A ABI não oferece logger; stderr fica no log da sessão criado pelo launcher.
    std::fprintf(stderr, "[playtest] %s\n", message);
    std::fflush(stderr);
}
ShipNativeStatus SHIP_NATIVE_CALL OnPlayDestroy(void*, const ShipNativeHookCall*) {
    gGameplay.SceneEnded();
    if (gServer)
        gServer->Queue().Cancel();
    return SHIP_NATIVE_OK;
}
ShipNativeStatus SHIP_NATIVE_CALL OnPad(void*, const ShipNativeHookCall* call) {
    if (!gServer || !call || !call->payload || call->payload_size < sizeof(ShipOotInputPadHookV1))
        return SHIP_NATIVE_INVALID_ARGUMENT;
    auto* payload = static_cast<ShipOotInputPadHookV1*>(call->payload);
    if (payload->size < sizeof(*payload) || payload->port_count > 4)
        return SHIP_NATIVE_INVALID_ARGUMENT;
    try {
        // Estende o contador de 32 bits sem perder a ordem depois do wrap.
        gFrame = gFirst ? payload->frame : gFrame + static_cast<uint32_t>(payload->frame - gPrevious);
        gPrevious = payload->frame;
        gFirst = false;
        Playtest::Snapshot snapshot;
        auto& j = snapshot.fields;
        j = { { "frame", gFrame }, { "game_state", "other" }, { "sail", gSail }, { "queue_size", 0 } };
        for (const auto* key : { "scene", "room", "entrance", "position", "rotation", "age", "health", "max_health",
                                 "magic", "rupees", "pause", "textbox" })
            j[key] = nullptr;
        if (gSave) {
            j["game_mode"] = gSave->gameMode;
            j["file_num"] = gSave->fileNum;
        }
        GameState* state = gState ? *gState : nullptr;
        const uintptr_t destroy = state ? reinterpret_cast<uintptr_t>(state->destroy) : 0;
        if (destroy && destroy == gFileDestroy)
            j["game_state"] = "file_select";
        else if (destroy && (destroy == gTitleDestroy || destroy == gSetupDestroy || destroy == gOpeningDestroy))
            j["game_state"] = "title";
        else if (destroy && destroy == gPlayDestroy) {
            const auto* play = reinterpret_cast<const PlayState*>(state);
            // A tela de título do OoT é um PlayState (demo em Hyrule Field); o gameMode separa do gameplay.
            const bool titleDemo = gSave && gSave->gameMode == GAMEMODE_TITLE_SCREEN;
            j["game_state"] = titleDemo ? "title" : "play";
            j["scene"] = play->sceneNum;
            j["room"] = play->roomCtx.curRoom.num;
            j["pause"] = play->pauseCtx.state != 0;
            j["textbox"] = play->msgCtx.msgMode != 0;
            const auto* actor = play->actorCtx.actorLists[ACTORCAT_PLAYER].head;
            if (actor) {
                j["position"] = { { "x", actor->world.pos.x },
                                  { "y", actor->world.pos.y },
                                  { "z", actor->world.pos.z } };
                j["rotation"] = { { "x", actor->shape.rot.x },
                                  { "y", actor->shape.rot.y },
                                  { "z", actor->shape.rot.z } };
            }
            if (gSave) {
                j["entrance"] = gSave->entranceIndex;
                j["age"] = gSave->linkAge;
                j["health"] = gSave->health;
                j["max_health"] = gSave->healthCapacity;
                j["magic"] = gSave->magic;
                j["rupees"] = gSave->rupees;
            }
        }
        if (payload->port_count)
            gServer->Queue().Apply(gFrame, payload->pads[0]);
        auto* livePlay = destroy == gPlayDestroy && gSave && gSave->gameMode != GAMEMODE_TITLE_SCREEN
                             ? reinterpret_cast<PlayState*>(state)
                             : nullptr;
        gGameplay.Tick(livePlay, gFrame, *gServer, payload->port_count ? &payload->pads[0] : nullptr);
        j.update(gGameplay.Observation());
        if (livePlay) {
            const auto* actor = livePlay->actorCtx.actorLists[ACTORCAT_PLAYER].head;
            if (actor) {
                j["position"] = { { "x", actor->world.pos.x },
                                  { "y", actor->world.pos.y },
                                  { "z", actor->world.pos.z } };
                j["rotation"] = { { "x", actor->shape.rot.x },
                                  { "y", actor->shape.rot.y },
                                  { "z", actor->shape.rot.z } };
            }
        }
        j["queue_size"] = gServer->Queue().Size();
        gServer->Publish(std::move(snapshot));
        return SHIP_NATIVE_OK;
    } catch (...) {
        gServer->Queue().Cancel();
        gServer->Commands().Cancel();
        Log("falha ao atualizar o retrato; entrada cancelada");
        return SHIP_NATIVE_FAILURE;
    }
}
ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** output) {
    if (output)
        *output = nullptr;
    if (!output || !runtime || runtime->size < sizeof(*runtime) || runtime->abi_major != 1 || runtime->abi_minor < 4 ||
        !runtime->resolve_symbol || !runtime->register_hook || !runtime->get_service)
        return SHIP_NATIVE_UNSUPPORTED;
    const auto* engine = static_cast<const ShipOotEngineV1*>(
        runtime->get_service(runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, 1, sizeof(ShipOotEngineV1)));
    if (!engine || engine->size < sizeof(*engine) || !engine->layout_id ||
        std::strcmp(engine->layout_id, PLAYTEST_LAYOUT_ID))
        return SHIP_NATIVE_UNSUPPORTED;
    const char* token = std::getenv("LINKSPAN_PLAYTEST_TOKEN");
    if (!token || !*token) {
        Log("socket desativado: LINKSPAN_PLAYTEST_TOKEN ausente");
        return SHIP_NATIVE_OK;
    }
    unsigned int port = 47811;
    if (const char* value = std::getenv("LINKSPAN_PLAYTEST_PORT")) {
        const auto end = value + std::strlen(value);
        const auto parsed = std::from_chars(value, end, port);
        if (parsed.ec != std::errc{} || parsed.ptr != end || !port || port > 65535) {
            Log("LINKSPAN_PLAYTEST_PORT inválida");
            return SHIP_NATIVE_INVALID_ARGUMENT;
        }
    }
    gState = reinterpret_cast<GameState**>(Resolve(runtime, "gGameState"));
    gSave = reinterpret_cast<SaveContext*>(Resolve(runtime, "gSaveContext"));
    gPlayDestroy = Resolve(runtime, "Play_Destroy");
    gFileDestroy = Resolve(runtime, "FileChoose_Destroy");
    gTitleDestroy = Resolve(runtime, "Title_Destroy");
    gSetupDestroy = Resolve(runtime, "TitleSetup_Destroy");
    gOpeningDestroy = Resolve(runtime, "Opening_Destroy");
    if (!gState || !gSave || !gPlayDestroy) {
        Log("catálogo público incompleto");
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (!gGameplay.Init(runtime)) {
        Log("SDK incompatível: tamanhos de structs ou símbolos públicos de gameplay ausentes");
        return SHIP_NATIVE_UNSUPPORTED;
    }
    gFirst = true;
    gFrame = 0;
    const char* sail = std::getenv("LINKSPAN_PLAYTEST_SAIL");
    gSail = sail && std::strcmp(sail, "1") == 0;
    try {
        gServer = std::make_unique<Playtest::Server>(PLAYTEST_LAYOUT_ID);
    } catch (...) { return SHIP_NATIVE_FAILURE; }
    Playtest::Snapshot initial;
    initial.fields["sail"] = gSail;
    gServer->Publish(std::move(initial));
    *output = gServer.get();
    const ShipNativeHookSpec hook{ sizeof(hook),
                                   LINKSPAN_OOT_HOOK_INPUT_PAD,
                                   LINKSPAN_OOT_HOOK_INPUT_PAD_VERSION,
                                   sizeof(ShipOotInputPadHookV1),
                                   SHIP_NATIVE_HOOK_TRANSFORM,
                                   0,
                                   0,
                                   OnPad,
                                   nullptr };
    uint64_t handle = 0;
    auto result = runtime->register_hook(runtime->context, &hook, &handle);
    if (result != SHIP_NATIVE_OK) {
        Log("hook oot.input.pad indisponível");
        return result;
    }
    const ShipNativeHookSpec destroyHook{ sizeof(destroyHook),
                                          LINKSPAN_OOT_HOOK_PLAY_DESTROY,
                                          LINKSPAN_OOT_HOOKS_VERSION,
                                          sizeof(ShipOotPlayHookV1),
                                          SHIP_NATIVE_HOOK_OBSERVE,
                                          0,
                                          0,
                                          OnPlayDestroy,
                                          nullptr };
    result = runtime->register_hook(runtime->context, &destroyHook, &handle);
    if (result != SHIP_NATIVE_OK) {
        Log("hook público oot.play.destroy indisponível");
        return result;
    }
    if (!gServer->Start(static_cast<uint16_t>(port), token)) {
        Log("não foi possível abrir o socket local");
        return SHIP_NATIVE_FAILURE;
    }
    Log("agente aguardando conexão em 127.0.0.1");
    return SHIP_NATIVE_OK;
}
void SHIP_NATIVE_CALL Shutdown(void*) {
    if (gServer)
        gServer->Stop();
    gServer.reset();
    gGameplay.Cleanup();
    gState = nullptr;
    gSave = nullptr;
}
} // namespace
extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{ sizeof(descriptor), 1, 4, Init, Shutdown };
    return &descriptor;
}
