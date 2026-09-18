#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include "oot_engine.h"
#include "oot_layout_id.h"
#include "oot_resources.h"
#include "oot_scenes.h"
#include "package_assets.h"
#include "z64.h"

namespace {

// Frames com o jogador em cena antes da viagem: a cena do save termina de carregar.
constexpr uint32_t READY_FRAMES = 60;
constexpr uint32_t MAX_TARGET = LINKSPAN_OOT_SCENES_MAX_NAME * 2 + 1;

struct Demo {
    const ShipOotEngineV1* engine = nullptr;
    const ShipOotResourcesV2* resources = nullptr;
    const ShipOotScenesV1* scenes = nullptr;
    uint64_t assets = 0;
    std::string target;
    uint32_t readyFrames = 0;
    bool travelled = false;
    bool arrived = false;
    int32_t fromScene = -1;
};

ShipNativeStatus Write(ShipNativeWriteFn write, void* writer, const char* text) {
    return write(writer, text, static_cast<uint32_t>(std::strlen(text)));
}

// Monta assets/ na raiz do VFS, antes do game.ready em que o framework lê unbound/scenes.json,
// e guarda o nome da entrada de destino.
ShipNativeStatus SHIP_NATIVE_CALL Configure(void* user, const char* request, uint32_t length, ShipNativeWriteFn write,
                                            void* writer) {
    auto& demo = *static_cast<Demo*>(user);
    if (!request || !length || length > MAX_TARGET) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        demo.target.assign(request, length);
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
    if (!demo.assets) {
        const std::string assets = ProviderAssetsDirectory();
        if (assets.empty()) {
            return Write(write, writer, "fail@assets");
        }
        if (demo.resources->mount_archive(assets.c_str(), &demo.assets) != SHIP_NATIVE_OK || !demo.assets) {
            demo.assets = 0;
            return Write(write, writer, "fail@mount");
        }
    }
    return Write(write, writer, "configured");
}

// Com um save aberto, viaja uma vez para a entrada de destino e avisa quando a cena do mod carrega.
ShipNativeStatus SHIP_NATIVE_CALL Update(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                         void* writer) {
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    auto& demo = *static_cast<Demo*>(user);
    const auto* play = static_cast<const PlayState*>(demo.engine->get_play_state());
    const auto* save = static_cast<const SaveContext*>(demo.engine->get_save_context());
    if (!play || !save || save->fileNum == 0xFF || !demo.engine->get_player()) {
        demo.readyFrames = 0;
        return Write(write, writer, "idle");
    }
    char text[160];
    if (!demo.travelled) {
        int32_t index = 0;
        if (demo.target.empty() || demo.scenes->find_entrance(demo.target.c_str(), &index) != SHIP_NATIVE_OK ||
            ++demo.readyFrames < READY_FRAMES || demo.scenes->travel_to_entrance(index) != SHIP_NATIVE_OK) {
            return Write(write, writer, "idle");
        }
        demo.travelled = true;
        demo.fromScene = play->sceneNum;
        std::snprintf(text, sizeof(text), "travel entrance=%d from scene=%d", index, play->sceneNum);
        return Write(write, writer, text);
    }
    // Chegada: a cena mudou desde a viagem (cena de mod ou vanilla).
    if (!demo.arrived && play->sceneNum != demo.fromScene) {
        demo.arrived = true;
        const auto* player = static_cast<const Player*>(demo.engine->get_player());
        std::snprintf(text, sizeof(text), "arrived scene=%d room=%d pos=%.0f,%.0f,%.0f yaw=0x%04X", play->sceneNum,
                      static_cast<int>(play->roomCtx.curRoom.num), player->actor.world.pos.x,
                      player->actor.world.pos.y, player->actor.world.pos.z,
                      static_cast<unsigned>(static_cast<uint16_t>(player->actor.shape.rot.y)));
        return Write(write, writer, text);
    }
    return Write(write, writer, "idle");
}

ShipNativeStatus SHIP_NATIVE_CALL Init(const ShipNativeRuntime* runtime, void** instance) {
    if (!runtime || runtime->size < sizeof(ShipNativeRuntime) || !runtime->get_service ||
        !runtime->register_function || !instance) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    const auto* engine = static_cast<const ShipOotEngineV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_ENGINE_SERVICE, LINKSPAN_OOT_ENGINE_VERSION, sizeof(ShipOotEngineV1)));
    const auto* resources = static_cast<const ShipOotResourcesV2*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_RESOURCES_SERVICE, LINKSPAN_OOT_RESOURCES_VERSION_2,
        sizeof(ShipOotResourcesV2)));
    const auto* scenes = static_cast<const ShipOotScenesV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_SCENES_SERVICE, LINKSPAN_OOT_SCENES_VERSION, sizeof(ShipOotScenesV1)));
    if (!engine || engine->size < sizeof(ShipOotEngineV1) || !engine->layout_id ||
        std::strcmp(engine->layout_id, LINKSPAN_OOT_LAYOUT_ID) || engine->play_state_size != sizeof(PlayState) ||
        engine->save_context_size != sizeof(SaveContext) || !engine->get_play_state || !engine->get_save_context ||
        !engine->get_player) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    if (!resources || resources->size < sizeof(ShipOotResourcesV2) || !resources->mount_archive ||
        !resources->unmount_archive || !scenes || scenes->size < sizeof(ShipOotScenesV1) || !scenes->find_entrance ||
        !scenes->travel_to_entrance) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* demo = new (std::nothrow) Demo;
    if (!demo) {
        return SHIP_NATIVE_FAILURE;
    }
    demo->engine = engine;
    demo->resources = resources;
    demo->scenes = scenes;
    if (runtime->register_function(runtime->context, "configure", Configure, demo) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "update", Update, demo) != SHIP_NATIVE_OK) {
        delete demo;
        return SHIP_NATIVE_FAILURE;
    }
    *instance = demo;
    return SHIP_NATIVE_OK;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* demo = static_cast<Demo*>(instance);
    if (demo && demo->assets) {
        demo->resources->unmount_archive(demo->assets);
    }
    delete demo;
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 0u, Init, Shutdown,
    };
    return &descriptor;
}
