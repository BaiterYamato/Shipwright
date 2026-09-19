// Fixtures da fase E do Unbound: monta assets/ (cenas JSON, registro, texto) na raiz do VFS e percorre um
// roteiro de entradas, uma por hotkey, relatando chegada, posição, chão e atores vivos.
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "oot_engine.h"
#include "oot_layout_id.h"
#include "oot_resources.h"
#include "oot_scenes.h"
#include "oot_text.h"
#include "package_assets.h"
#include "z64.h"

namespace {

constexpr uint32_t MAX_PLAN = 4096;
// Relatórios de posição depois da chegada: o jogador assentou no chão (ou caiu no vazio).
constexpr uint32_t REPORT_FRAMES[] = { 60, 300 };

struct Fixtures {
    const ShipOotEngineV1* engine = nullptr;
    const ShipOotResourcesV2* resources = nullptr;
    const ShipOotScenesV1* scenes = nullptr;
    const ShipOotTextV1* text = nullptr;
    uint64_t assets = 0;
    std::vector<std::string> plan;
    size_t next = 0;
    bool travelling = false;
    int32_t fromScene = -1;
    uint32_t framesInScene = 0;
    size_t reportsDone = 0;
};

ShipNativeStatus Write(ShipNativeWriteFn write, void* writer, const std::string& text) {
    return write(writer, text.data(), static_cast<uint32_t>(text.size()));
}

bool PlayerReady(const Fixtures& fixtures, const PlayState*& play, const Player*& player) {
    play = static_cast<const PlayState*>(fixtures.engine->get_play_state());
    const auto* save = static_cast<const SaveContext*>(fixtures.engine->get_save_context());
    player = static_cast<const Player*>(fixtures.engine->get_player());
    return play && save && save->fileNum != 0xFF && player;
}

std::string Describe(const PlayState* play, const Player* player) {
    // Coletáveis (En_Item00) vivos e o mais próximo do jogador: a fixture ampla põe 600 à frente do spawn.
    unsigned items = 0;
    const Actor* nearest = nullptr;
    float nearestDistance = 0.0f;
    for (int category = 0; category < ACTORCAT_MAX; ++category) {
        for (const Actor* actor = play->actorCtx.actorLists[category].head; actor; actor = actor->next) {
            if (actor->id != 0x0015) {
                continue;
            }
            ++items;
            const float dx = actor->world.pos.x - player->actor.world.pos.x;
            const float dz = actor->world.pos.z - player->actor.world.pos.z;
            const float distance = dx * dx + dz * dz;
            if (!nearest || distance < nearestDistance) {
                nearest = actor;
                nearestDistance = distance;
            }
        }
    }
    char text[320];
    int used = std::snprintf(text, sizeof(text), "scene=%d room=%d pos=%.1f,%.1f,%.1f chao=%d atores=%u",
                             play->sceneNum, static_cast<int>(play->roomCtx.curRoom.num), player->actor.world.pos.x,
                             player->actor.world.pos.y, player->actor.world.pos.z,
                             (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0,
                             static_cast<unsigned>(play->actorCtx.total));
    if (nearest && used > 0 && used < static_cast<int>(sizeof(text))) {
        std::snprintf(text + used, sizeof(text) - used, " coletaveis=%u proximo=%.1f,%.1f,%.1f draw=%d flags=0x%X",
                      items, nearest->world.pos.x, nearest->world.pos.y, nearest->world.pos.z,
                      nearest->draw != nullptr ? 1 : 0, static_cast<unsigned>(nearest->flags));
    }
    return text;
}

// "a;b;c": entradas do roteiro. Monta assets/ antes do game.ready em que o framework lê as camadas.
ShipNativeStatus SHIP_NATIVE_CALL Configure(void* user, const char* request, uint32_t length, ShipNativeWriteFn write,
                                            void* writer) {
    auto& fixtures = *static_cast<Fixtures*>(user);
    if (!request || !length || length > MAX_PLAN) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    try {
        fixtures.plan.clear();
        const std::string text(request, length);
        size_t start = 0;
        while (start <= text.size()) {
            const size_t end = std::min(text.find(';', start), text.size());
            if (end > start) {
                fixtures.plan.push_back(text.substr(start, end - start));
            }
            start = end + 1;
        }
    } catch (...) {
        return SHIP_NATIVE_FAILURE;
    }
    if (!fixtures.assets) {
        const std::string assets = ProviderAssetsDirectory();
        if (assets.empty() || fixtures.resources->mount_archive(assets.c_str(), &fixtures.assets) != SHIP_NATIVE_OK) {
            fixtures.assets = 0;
            return Write(write, writer, "fail@assets");
        }
    }
    return Write(write, writer, "configured steps=" + std::to_string(fixtures.plan.size()));
}

// Hotkey: viaja para a próxima entrada do roteiro (volta ao começo no fim).
ShipNativeStatus SHIP_NATIVE_CALL Next(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                       void* writer) {
    auto& fixtures = *static_cast<Fixtures*>(user);
    const PlayState* play = nullptr;
    const Player* player = nullptr;
    if (length || fixtures.plan.empty()) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!PlayerReady(fixtures, play, player)) {
        return Write(write, writer, "sem jogador em cena");
    }
    const std::string& target = fixtures.plan[fixtures.next % fixtures.plan.size()];
    int32_t index = -1;
    if (fixtures.scenes->find_entrance(target.c_str(), &index) != SHIP_NATIVE_OK) {
        return Write(write, writer, "entrada desconhecida: " + target);
    }
    if (fixtures.scenes->travel_to_entrance(index) != SHIP_NATIVE_OK) {
        return Write(write, writer, "viagem recusada: " + target);
    }
    ++fixtures.next;
    fixtures.travelling = true;
    fixtures.fromScene = play->sceneNum;
    fixtures.framesInScene = 0;
    fixtures.reportsDone = 0;
    return Write(write, writer, "travel " + target + " entrance=" + std::to_string(index) + " from " +
                                    Describe(play, player));
}

// Por frame: chegada numa cena nova e relatórios de posição depois dela.
ShipNativeStatus SHIP_NATIVE_CALL Update(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                         void* writer) {
    auto& fixtures = *static_cast<Fixtures*>(user);
    const PlayState* play = nullptr;
    const Player* player = nullptr;
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!PlayerReady(fixtures, play, player)) {
        return Write(write, writer, "idle");
    }
    if (fixtures.travelling) {
        if (play->sceneNum == fixtures.fromScene && fixtures.framesInScene == 0 &&
            play->transitionTrigger != TRANS_TRIGGER_OFF) {
            return Write(write, writer, "idle");
        }
        if (play->transitionTrigger == TRANS_TRIGGER_OFF && play->transitionMode == TRANS_MODE_OFF) {
            fixtures.travelling = false;
            fixtures.framesInScene = 0;
            fixtures.reportsDone = 0;
            return Write(write, writer, "arrived " + Describe(play, player));
        }
        return Write(write, writer, "idle");
    }
    ++fixtures.framesInScene;
    if (fixtures.reportsDone < std::size(REPORT_FRAMES) &&
        fixtures.framesInScene == REPORT_FRAMES[fixtures.reportsDone]) {
        ++fixtures.reportsDone;
        return Write(write, writer, "after " + std::to_string(fixtures.framesInScene) + " frames " +
                                        Describe(play, player));
    }
    return Write(write, writer, "idle");
}

struct Probe {
    uint32_t ids[3];
    std::string found[3];
};

ShipNativeStatus SHIP_NATIVE_CALL VisitMessage(void* user, uint32_t id, uint8_t box, uint8_t pos,
                                               const uint8_t* bytes, uint32_t length) {
    auto& probe = *static_cast<Probe*>(user);
    for (size_t i = 0; i < 3; ++i) {
        if (probe.ids[i] == id) {
            std::string text;
            for (uint32_t j = 0; j < length && text.size() < 48; ++j) {
                text.push_back(bytes[j] >= 0x20 && bytes[j] < 0x7F ? static_cast<char>(bytes[j]) : '.');
            }
            char head[32];
            std::snprintf(head, sizeof(head), "box=%u pos=%u len=%u '", box, pos, length);
            probe.found[i] = head + text + "'";
        }
    }
    return SHIP_NATIVE_OK;
}

// UNBOUND-010: estado das mensagens da fixture (acrescentada, substituída e removida) na tabela em inglês.
ShipNativeStatus SHIP_NATIVE_CALL TextProbe(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                            void* writer) {
    auto& fixtures = *static_cast<Fixtures*>(user);
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!fixtures.text) {
        return Write(write, writer, "sem linkspan.oot.text");
    }
    Probe probe{ { 0x03FE, 0x0301, 0x0345 } };
    const auto status = fixtures.text->list_messages(LINKSPAN_OOT_TEXT_ENGLISH, VisitMessage, &probe);
    if (status != SHIP_NATIVE_OK) {
        return Write(write, writer, "list_messages falhou");
    }
    std::string text;
    for (size_t i = 0; i < 3; ++i) {
        char id[16];
        std::snprintf(id, sizeof(id), "0x%04X=", probe.ids[i]);
        text += std::string(i ? "; " : "") + id + (probe.found[i].empty() ? "ausente" : probe.found[i]);
    }
    return Write(write, writer, text);
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
        !engine->get_player || !resources || !resources->mount_archive || !resources->unmount_archive || !scenes) {
        return SHIP_NATIVE_UNSUPPORTED;
    }
    auto* fixtures = new (std::nothrow) Fixtures;
    if (!fixtures) {
        return SHIP_NATIVE_FAILURE;
    }
    fixtures->engine = engine;
    fixtures->resources = resources;
    fixtures->scenes = scenes;
    fixtures->text = static_cast<const ShipOotTextV1*>(runtime->get_service(
        runtime->context, LINKSPAN_OOT_TEXT_SERVICE, LINKSPAN_OOT_TEXT_VERSION, sizeof(ShipOotTextV1)));
    if (runtime->register_function(runtime->context, "configure", Configure, fixtures) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "next", Next, fixtures) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "update", Update, fixtures) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "text_probe", TextProbe, fixtures) != SHIP_NATIVE_OK) {
        delete fixtures;
        return SHIP_NATIVE_FAILURE;
    }
    *instance = fixtures;
    return SHIP_NATIVE_OK;
}

void SHIP_NATIVE_CALL Shutdown(void* instance) {
    auto* fixtures = static_cast<Fixtures*>(instance);
    if (fixtures && fixtures->assets) {
        fixtures->resources->unmount_archive(fixtures->assets);
    }
    delete fixtures;
}

} // namespace

extern "C" SHIP_NATIVE_EXPORT const ShipNativeDescriptor* SHIP_NATIVE_CALL ShipNative_Query() {
    static const ShipNativeDescriptor descriptor{
        sizeof(ShipNativeDescriptor), SHIP_NATIVE_ABI_MAJOR, 0u, Init, Shutdown,
    };
    return &descriptor;
}
