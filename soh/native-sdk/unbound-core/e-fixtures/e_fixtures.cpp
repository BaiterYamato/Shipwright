// Fixtures da fase E do Unbound: monta assets/ (cenas JSON, registro, texto) na raiz do VFS e percorre um
// roteiro de entradas, uma por hotkey, relatando chegada, posição, chão e atores vivos.
#include <cmath>
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
constexpr uint32_t REPORT_FRAMES[] = { 60, 300, 450, 600, 900 };

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
    int32_t stableScene = -1;
    uint32_t framesInScene = 0;
    size_t reportsDone = 0;
    // UNBOUND-021: mudanças do flag de chão depois da chegada (sair do piso, cair, voltar).
    int lastGround = -1;
    uint32_t groundChanges = 0;
    // UNBOUND-031: trocas de sala sem viagem (planos En_Holl), relatadas alguns frames depois.
    int lastRoom = -1;
    int roomFrom = -1;
    uint32_t roomReportFrame = 0;
    uint32_t roomChanges = 0;
    // RFC 0027 R08/R09: campos de mapa só nas viagens dos casos (ENTR_DODONGOS_*, linkspan_e/r08_*); a pausa é
    // relatada em toda cena, a partir da segunda leitura (a primeira só guarda o estado de chegada).
    bool mapReport = false;
    int lastPauseState = -1;
    int lastPausePage = -1;
};

constexpr uint32_t MAX_GROUND_CHANGES = 24;
constexpr uint32_t MAX_ROOM_CHANGES = 24;
// A sala nova executa os comandos dela (inclusive LIGHT_LIST) depois da troca do número.
constexpr uint32_t ROOM_REPORT_DELAY = 20;

ShipNativeStatus Write(ShipNativeWriteFn write, void* writer, const std::string& text) {
    return write(writer, text.data(), static_cast<uint32_t>(text.size()));
}

bool PlayerReady(const Fixtures& fixtures, const PlayState*& play, const Player*& player) {
    play = static_cast<const PlayState*>(fixtures.engine->get_play_state());
    const auto* save = static_cast<const SaveContext*>(fixtures.engine->get_save_context());
    player = static_cast<const Player*>(fixtures.engine->get_player());
    return play && save && save->fileNum != 0xFF && player;
}

// Limites ampliados (UNBOUND-008/009): salas da cena, objetos carregados, transition actors, entradas da malha da
// sala atual, DynaPoly em uso, portas e caixas vivas, água sob o jogador e nado.
std::string Limits(const PlayState* play, const Player* player) {
    unsigned doors = 0;
    unsigned crates = 0;
    for (int category = 0; category < ACTORCAT_MAX; ++category) {
        for (const Actor* actor = play->actorCtx.actorLists[category].head; actor; actor = actor->next) {
            doors += actor->id == 0x0009 ? 1 : 0;
            crates += actor->id == 0x01A0 ? 1 : 0;
        }
    }
    const auto& dyna = play->colCtx.dyna;
    int dynaInUse = 0;
    // UNBOUND-026: soma da geometria dos registros em uso, a mesma que o DynaPoly_Setup compara com as listas.
    long dynaPolys = 0;
    long dynaVertices = 0;
    for (int32_t i = 0; dyna.bgActorFlags && i < dyna.bgActorMax; ++i) {
        if (dyna.bgActorFlags[i] & 1) {
            ++dynaInUse;
            if (const CollisionHeader* header = dyna.bgActors[i].colHeader) {
                dynaPolys += static_cast<long>(header->numPolygons);
                dynaVertices += static_cast<long>(header->numVertices);
            }
        }
    }
    unsigned meshEntries = 0;
    if (const MeshHeader* mesh = play->roomCtx.curRoom.meshHeader) {
        meshEntries = mesh->base.type == 0 ? mesh->polygon0.num : mesh->base.type == 2 ? mesh->polygon2.num : 1;
    }
    if (doors == 0 && crates == 0 && play->numRooms < 2 && meshEntries < 2 && !dynaInUse &&
        !(player->actor.bgCheckFlags & BGCHECKFLAG_WATER)) {
        return {};
    }
    char text[256];
    std::snprintf(text, sizeof(text),
                  " | salas=%u objetos=%u transicao=%u malha=%u dyna=%d/%d polys=%d vtx=%d dpolys=%ld dvtx=%ld "
                  "portas=%u caixas=%u agua=%d nado=%d",
                  static_cast<unsigned>(play->numRooms), static_cast<unsigned>(play->objectCtx.num),
                  static_cast<unsigned>(play->transiActorCtx.numActors), meshEntries, dynaInUse, dyna.bgActorMax,
                  dyna.polyListMax, dyna.vtxListMax, dynaPolys, dynaVertices, doors, crates,
                  (player->actor.bgCheckFlags & BGCHECKFLAG_WATER) ? 1 : 0,
                  (player->stateFlags1 & PLAYER_STATE1_IN_WATER) ? 1 : 0);
    return text;
}

// UNBOUND-031: nós na lista de luzes do jogo (listas de cena e de sala, atores, ambiente).
unsigned CountLights(const PlayState* play) {
    unsigned count = 0;
    for (const LightNode* node = play->lightCtx.listHead; node && count < 65536; node = node->next) {
        ++count;
    }
    return count;
}

// RFC 0027 R08/R09: somente leitura pelo serviço/layout já verificado no Init.
// R_MAP_TEX_INDEX depende de gGameInfo (regs.h); engine v1 não expõe esse ponteiro.
// Não importar globals internos do exe numa DLL de SDK independente.
std::string MapState(const Fixtures& fixtures, const PlayState* play) {
    const auto* save = static_cast<const SaveContext*>(fixtures.engine->get_save_context());
    if (!save) {
        return " | mapa=sem_save";
    }
    const auto& interfaceCtx = play->interfaceCtx;
    const int index = save->mapIndex;
    const bool dungeon = play->sceneNum == SCENE_DODONGOS_CAVERN;
    const unsigned items = dungeon && index >= 0 && index < 20 ? save->inventory.dungeonItems[index] : 0;
    char text[320];
    std::snprintf(text, sizeof(text),
                  " | worldMapArea=%d worldMapAreaData=0x%08X mapIndex=%d mapRoomNum=%d mapPaletteIndex=%d "
                  "mapTex0=%d mapName0=%d dungeonItems=0x%02X pauseState=%u pausePage=%u "
                  "R_MAP_TEX_INDEX=indisponivel_SDK_v1",
                  static_cast<int>(save->worldMapArea), static_cast<unsigned>(save->worldMapAreaData), index,
                  static_cast<int>(interfaceCtx.mapRoomNum), static_cast<int>(interfaceCtx.mapPaletteIndex),
                  interfaceCtx.mapSegment && interfaceCtx.mapSegment[0] ? 1 : 0, interfaceCtx.mapSegmentName[0] ? 1 : 0,
                  items, static_cast<unsigned>(play->pauseCtx.state), static_cast<unsigned>(play->pauseCtx.pageIndex));
    return text;
}

std::string Describe(const Fixtures& fixtures, const PlayState* play, const Player* player) {
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
    // UNBOUND-021: índice do polígono de chão na colisão estática da cena (-1 sem chão, -2 DynaPoly).
    long floorIndex = -1;
    const CollisionHeader* header = play->colCtx.colHeader;
    if (player->actor.floorPoly && player->actor.floorBgId == BGCHECK_SCENE && header && header->polyList) {
        floorIndex = static_cast<long>(player->actor.floorPoly - header->polyList);
    } else if (player->actor.floorPoly) {
        floorIndex = -2;
    }
    // UNBOUND-024: câmera da superfície sob o jogador (índice s32 da tabela, -1 sem chão da cena) e o índice e o
    // setting da câmera principal (cameraPtrs[0]), que o jogo troca ao pisar numa superfície de outra câmera.
    long surfaceCamera = -1;
    if (floorIndex >= 0 && header->surfaceTypeList) {
        surfaceCamera = static_cast<long>(header->surfaceTypeList[player->actor.floorPoly->type].camera);
    }
    const Camera* camera = play->cameraPtrs[0];
    char text[640];
    int used = std::snprintf(text, sizeof(text),
                             "scene=%d room=%d pos=%.1f,%.1f,%.1f chao=%d piso=%ld alturaChao=%.1f camSup=%ld cam=%d "
                             "set=%d bg=%d atores=%u luzes=%u",
                             play->sceneNum, static_cast<int>(play->roomCtx.curRoom.num), player->actor.world.pos.x,
                             player->actor.world.pos.y, player->actor.world.pos.z,
                             (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0, floorIndex,
                             player->actor.floorHeight, surfaceCamera, camera ? camera->camDataIdx : -1,
                             camera ? camera->setting : -1, static_cast<int>(player->actor.floorBgId),
                             static_cast<unsigned>(play->actorCtx.total), CountLights(play));
    if (nearest && used > 0 && used < static_cast<int>(sizeof(text))) {
        used += std::snprintf(text + used, sizeof(text) - used,
                              " coletaveis=%u proximo=%.1f,%.1f,%.1f draw=%d flags=0x%X", items, nearest->world.pos.x,
                              nearest->world.pos.y, nearest->world.pos.z, nearest->draw != nullptr ? 1 : 0,
                              static_cast<unsigned>(nearest->flags));
    }
    // UNBOUND-013: idade, cavalo vivo, montaria e a cena gravada da Epona.
    const Actor* horse = nullptr;
    for (int category = 0; category < ACTORCAT_MAX && !horse; ++category) {
        for (const Actor* actor = play->actorCtx.actorLists[category].head; actor && !horse; actor = actor->next) {
            horse = actor->id == 0x0014 ? actor : nullptr;
        }
    }
    if (used > 0 && used < static_cast<int>(sizeof(text))) {
        used += std::snprintf(text + used, sizeof(text) - used, " adulto=%d montado=%d", play->linkAgeOnLoad == 0,
                              player->rideActor != nullptr ? 1 : 0);
    }
    if (horse && used > 0 && used < static_cast<int>(sizeof(text))) {
        used += std::snprintf(text + used, sizeof(text) - used, " cavalo=%.1f,%.1f,%.1f", horse->world.pos.x,
                              horse->world.pos.y, horse->world.pos.z);
    }
    if (used > 0 && used < static_cast<int>(sizeof(text))) {
        std::snprintf(text + used, sizeof(text) - used, "%s", Limits(play, player).c_str());
    }
    return fixtures.mapReport ? std::string(text) + MapState(fixtures, play) : std::string(text);
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
    std::string granted;
    const bool dodongo = target.rfind("ENTR_DODONGOS_CAVERN", 0) == 0;
    if (dodongo) {
        // Só teste (RFC 0027 R09): mapa (0x04) e bússola (0x02) da Dodongo, para o minimapa e as marcas desenharem
        // na sala 32. O save da cópia de teste é restaurado depois da sessão.
        auto* save = const_cast<SaveContext*>(static_cast<const SaveContext*>(fixtures.engine->get_save_context()));
        if (save) {
            save->inventory.dungeonItems[SCENE_DODONGOS_CAVERN] |= 0x06;
            granted = " (mapa e bussola da Dodongo concedidos)";
        }
    }
    ++fixtures.next;
    fixtures.travelling = true;
    fixtures.fromScene = play->sceneNum;
    fixtures.framesInScene = 0;
    fixtures.reportsDone = 0;
    std::string line = "travel " + target + " entrance=" + std::to_string(index) + granted + " from " +
                       Describe(fixtures, play, player);
    fixtures.mapReport = dodongo || target.rfind("linkspan_e/r08_", 0) == 0;
    return Write(write, writer, line);
}

// Só teste (UNBOUND-013): Link adulto na próxima carga, Epona obtida, ocarina e Epona's Song, e viagem de volta para a
// cena pedida. O save da cópia de teste é restaurado depois da sessão.
ShipNativeStatus SHIP_NATIVE_CALL AdultEpona(void* user, const char* request, uint32_t length,
                                             ShipNativeWriteFn write, void* writer) {
    auto& fixtures = *static_cast<Fixtures*>(user);
    const PlayState* play = nullptr;
    const Player* player = nullptr;
    if (!request || !length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!PlayerReady(fixtures, play, player)) {
        return Write(write, writer, "sem jogador em cena");
    }
    const std::string target(request, length);
    int32_t index = -1;
    if (fixtures.scenes->find_entrance(target.c_str(), &index) != SHIP_NATIVE_OK) {
        return Write(write, writer, "entrada desconhecida: " + target);
    }
    auto* save = const_cast<SaveContext*>(static_cast<const SaveContext*>(fixtures.engine->get_save_context()));
    save->inventory.items[SLOT_OCARINA] = ITEM_OCARINA_TIME;
    save->inventory.questItems |= 1u << QUEST_SONG_EPONA;
    save->eventChkInf[EVENTCHKINF_EPONA_OBTAINED >> 4] |= 1 << (EVENTCHKINF_EPONA_OBTAINED & 0xF);
    const_cast<PlayState*>(play)->linkAgeOnLoad = 0;
    if (fixtures.scenes->travel_to_entrance(index) != SHIP_NATIVE_OK) {
        return Write(write, writer, "viagem recusada: " + target);
    }
    fixtures.travelling = true;
    fixtures.fromScene = play->sceneNum;
    fixtures.framesInScene = 0;
    fixtures.reportsDone = 0;
    return Write(write, writer, "adulto com Epona, travel " + target + " from " + Describe(fixtures, play, player));
}

// Só teste (UNBOUND-013): põe a Epona ao lado do Link. Ela anda sozinha pela cena, e uma sequência automatizada
// não tem como persegui-la até o A virar "Ride"; a montaria em si continua sendo a do jogo.
ShipNativeStatus SHIP_NATIVE_CALL HorseHere(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                            void* writer) {
    auto& fixtures = *static_cast<Fixtures*>(user);
    const PlayState* play = nullptr;
    const Player* player = nullptr;
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!PlayerReady(fixtures, play, player)) {
        return Write(write, writer, "sem jogador em cena");
    }
    Actor* horse = nullptr;
    for (int category = 0; category < ACTORCAT_MAX && !horse; ++category) {
        for (Actor* actor = play->actorCtx.actorLists[category].head; actor && !horse; actor = actor->next) {
            horse = actor->id == 0x0014 ? actor : nullptr;
        }
    }
    if (!horse) {
        return Write(write, writer, "sem cavalo na cena");
    }
    // A direção em que o Link olha vira a direção da cavalgada: a Epona fica de lado para ele, a um quarto de volta,
    // e o Link gira para encará-la, que é o jeito de o A virar "Ride". Depois de montar, a frente dela é a original.
    auto* mutablePlayer = const_cast<Player*>(player);
    const int16_t ride = player->actor.shape.rot.y;
    const int16_t facing = static_cast<int16_t>(ride - 0x4000);
    const float yaw = static_cast<float>(facing) * (3.14159265f / 32768.0f);
    horse->world.pos.x = player->actor.world.pos.x + 70.0f * std::sin(yaw);
    horse->world.pos.y = player->actor.world.pos.y;
    horse->world.pos.z = player->actor.world.pos.z + 70.0f * std::cos(yaw);
    horse->prevPos = horse->world.pos;
    horse->shape.rot.y = ride;
    horse->world.rot.y = ride;
    horse->speedXZ = 0.0f;
    mutablePlayer->actor.shape.rot.y = facing;
    mutablePlayer->actor.world.rot.y = facing;
    return Write(write, writer, "cavalo ao lado " + Describe(fixtures, play, player));
}

// Só teste (RFC 0027, patch 14): lê o nível das water boxes da colisão da cena e, com "up", sobe todas 200 unidades.
// Um load_state na mesma colisão precisa trazer o nível de volta (as caixas ficam no recurso, fora dos heaps).
ShipNativeStatus SHIP_NATIVE_CALL Water(void* user, const char* request, uint32_t length, ShipNativeWriteFn write,
                                        void* writer) {
    auto& fixtures = *static_cast<Fixtures*>(user);
    const PlayState* play = nullptr;
    const Player* player = nullptr;
    const std::string mode(request ? request : "", length);
    if (!mode.empty() && mode != "up") {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!PlayerReady(fixtures, play, player)) {
        return Write(write, writer, "sem jogador em cena");
    }
    CollisionHeader* header = play->colCtx.colHeader;
    if (!header || !header->waterBoxes || header->numWaterBoxes == 0) {
        return Write(write, writer, "agua: cena sem water box");
    }
    long long before = 0;
    long long after = 0;
    for (unsigned i = 0; i < header->numWaterBoxes; ++i) {
        before += header->waterBoxes[i].ySurface;
        if (mode == "up") {
            header->waterBoxes[i].ySurface += 200;
        }
        after += header->waterBoxes[i].ySurface;
    }
    char text[192];
    std::snprintf(text, sizeof(text), "agua %s caixas=%u y0=%d soma=%lld->%lld", mode.empty() ? "leitura" : "sobe",
                  static_cast<unsigned>(header->numWaterBoxes), static_cast<int>(header->waterBoxes[0].ySurface),
                  before, after);
    return Write(write, writer, text);
}

// Só teste (RFC 0027, patch 15): conta os nós da lista de luzes do jogo. Uma luz da bridge removida depois do save
// não pode voltar ao lightCtx num load (luz fantasma).
ShipNativeStatus SHIP_NATIVE_CALL LightsProbe(void* user, const char*, uint32_t length, ShipNativeWriteFn write,
                                              void* writer) {
    auto& fixtures = *static_cast<Fixtures*>(user);
    const PlayState* play = nullptr;
    const Player* player = nullptr;
    if (length) {
        return SHIP_NATIVE_INVALID_ARGUMENT;
    }
    if (!PlayerReady(fixtures, play, player)) {
        return Write(write, writer, "sem jogador em cena");
    }
    char text[96];
    std::snprintf(text, sizeof(text), "luzes lista=%u", CountLights(play));
    return Write(write, writer, text);
}

void ResetRooms(Fixtures& fixtures) {
    fixtures.lastRoom = -1;
    fixtures.roomFrom = -1;
    fixtures.roomReportFrame = 0;
    fixtures.roomChanges = 0;
    fixtures.lastPauseState = -1;
    fixtures.lastPausePage = -1;
}

// Por frame: chegada numa cena nova (inclusive por saída física) e relatórios de posição depois dela.
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
            fixtures.stableScene = play->sceneNum;
            fixtures.framesInScene = 0;
            fixtures.reportsDone = 0;
            fixtures.lastGround = -1;
            fixtures.groundChanges = 0;
            ResetRooms(fixtures);
            return Write(write, writer, "arrived " + Describe(fixtures, play, player));
        }
        return Write(write, writer, "idle");
    }
    // J/K marcam travelling; uma saída de colisão é iniciada pelo host. Registre a chegada dela sem interferir no input.
    if (fixtures.stableScene == -1) {
        fixtures.stableScene = play->sceneNum;
    } else if (fixtures.stableScene != play->sceneNum) {
        if (play->transitionTrigger != TRANS_TRIGGER_OFF || play->transitionMode != TRANS_MODE_OFF) {
            return Write(write, writer, "idle");
        }
        fixtures.stableScene = play->sceneNum;
        fixtures.framesInScene = 0;
        fixtures.reportsDone = 0;
        fixtures.lastGround = -1;
        fixtures.groundChanges = 0;
        ResetRooms(fixtures);
        return Write(write, writer, "arrived por saida " + Describe(fixtures, play, player));
    }
    ++fixtures.framesInScene;
    // Registra abertura, mudança de página e fechamento mesmo depois do último REPORT_FRAMES.
    // mapSegment é reutilizado pela pausa: só comparar a ausência do minimapa com pauseState=0.
    const int pauseState = play->pauseCtx.state;
    const int pausePage = play->pauseCtx.pageIndex;
    if (fixtures.lastPauseState < 0) {
        fixtures.lastPauseState = pauseState;
        fixtures.lastPausePage = pausePage;
    } else if (pauseState != fixtures.lastPauseState || pausePage != fixtures.lastPausePage) {
        fixtures.lastPauseState = pauseState;
        fixtures.lastPausePage = pausePage;
        return Write(write, writer, "pausa frame " + std::to_string(fixtures.framesInScene) + " " +
                                        Describe(fixtures, play, player));
    }
    // UNBOUND-031: a troca de sala só é registrada aqui; o relatório sai ROOM_REPORT_DELAY frames depois da primeira
    // troca pendente, com a sala de origem dela e a sala atual.
    const int room = play->roomCtx.curRoom.num;
    if (fixtures.lastRoom != -1 && room != fixtures.lastRoom && fixtures.roomChanges < MAX_ROOM_CHANGES) {
        ++fixtures.roomChanges;
        if (!fixtures.roomReportFrame) {
            fixtures.roomFrom = fixtures.lastRoom;
            fixtures.roomReportFrame = fixtures.framesInScene + ROOM_REPORT_DELAY;
        }
    }
    fixtures.lastRoom = room;
    const int ground = (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ? 1 : 0;
    if (fixtures.lastGround != -1 && ground != fixtures.lastGround && fixtures.groundChanges < MAX_GROUND_CHANGES) {
        fixtures.lastGround = ground;
        ++fixtures.groundChanges;
        return Write(write, writer, std::string("chao ") + (ground ? "0->1 " : "1->0 ") + "frame " +
                                        std::to_string(fixtures.framesInScene) + " " + Describe(fixtures, play, player));
    }
    fixtures.lastGround = ground;
    if (fixtures.roomReportFrame && fixtures.framesInScene >= fixtures.roomReportFrame) {
        fixtures.roomReportFrame = 0;
        return Write(write, writer, "sala " + std::to_string(fixtures.roomFrom) + "->" + std::to_string(room) +
                                        " frame " + std::to_string(fixtures.framesInScene) + " " +
                                        Describe(fixtures, play, player));
    }
    if (fixtures.reportsDone < std::size(REPORT_FRAMES) &&
        fixtures.framesInScene >= REPORT_FRAMES[fixtures.reportsDone]) {
        ++fixtures.reportsDone;
        return Write(write, writer, "after " + std::to_string(fixtures.framesInScene) + " frames " +
                                        Describe(fixtures, play, player));
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
        runtime->register_function(runtime->context, "text_probe", TextProbe, fixtures) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "adult_epona", AdultEpona, fixtures) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "horse_here", HorseHere, fixtures) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "water", Water, fixtures) != SHIP_NATIVE_OK ||
        runtime->register_function(runtime->context, "lights_probe", LightsProbe, fixtures) != SHIP_NATIVE_OK) {
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
