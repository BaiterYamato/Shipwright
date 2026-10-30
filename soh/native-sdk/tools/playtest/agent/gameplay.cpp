#include "gameplay.h"
#include "overview.h"
#include "z64save.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace Playtest {
namespace {
constexpr double Pi = 3.14159265358979323846;
double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
Json Position(const Vec3f& p) {
    return { { "x", p.x }, { "y", p.y }, { "z", p.z } };
}
Json Position(const float* p) {
    return { { "x", p[0] }, { "y", p[1] }, { "z", p[2] } };
}
double Distance(const Vec3f& a, const Vec3f& b) {
    return std::hypot(std::hypot(double(a.x) - b.x, double(a.z) - b.z), double(a.y) - b.y);
}
Json Name(int id) {
    switch (id) {
#define DEFINE_ACTOR(name, id, alloc) \
    case id:                          \
        return #name;
#define DEFINE_ACTOR_INTERNAL DEFINE_ACTOR
#define DEFINE_ACTOR_UNSET(id)
#include "tables/actor_table.h"
#undef DEFINE_ACTOR
#undef DEFINE_ACTOR_INTERNAL
#undef DEFINE_ACTOR_UNSET
        default:
            return nullptr; // Tipos dinâmicos não têm tabela de nomes pública.
    }
}
constexpr const char* Categories[] = { "switch", "bg",         "player", "explosive", "npc",  "enemy",
                                       "prop",   "itemaction", "misc",   "boss",      "door", "chest" };
void Neutral(ShipOotInputPadV1* pad) {
    if (!pad)
        return;
    pad->button = 0;
    pad->stick_x = pad->stick_y = pad->right_stick_x = pad->right_stick_y = 0;
}
template <class T> const T* Service(const ShipNativeRuntime* runtime, const char* name) {
    const auto* s = static_cast<const T*>(runtime->get_service(runtime->context, name, 1, sizeof(T)));
    return s && s->size >= sizeof(T) ? s : nullptr;
}
uintptr_t Symbol(const ShipNativeRuntime* r, const char* name) {
    uintptr_t value = 0;
    return r->resolve_symbol(r->context, name, &value) == SHIP_NATIVE_OK ? value : 0;
}
} // namespace
bool Gameplay::Init(const ShipNativeRuntime* runtime) {
    engine_ = Service<ShipOotEngineV1>(runtime, LINKSPAN_OOT_ENGINE_SERVICE);
    if (!engine_ || !engine_->layout_id || std::strcmp(engine_->layout_id, PLAYTEST_LAYOUT_ID) ||
        engine_->play_state_size != sizeof(PlayState) || engine_->player_size != sizeof(Player) ||
        !engine_->get_player || !engine_->get_play_state)
        return false;
    camera_ = Service<ShipOotCameraV1>(runtime, LINKSPAN_OOT_CAMERA_SERVICE);
    if (camera_ &&
        (!camera_->get_view || !camera_->set_view || !camera_->acquire || !camera_->release || !camera_->is_owned))
        camera_ = nullptr;
    movement_ = Service<ShipOotMovementV1>(runtime, LINKSPAN_OOT_MOVEMENT_SERVICE);
    world_ = Service<ShipOotWorldV1>(runtime, LINKSPAN_OOT_WORLD_SERVICE);
    lists_ = Service<ShipOotSceneQueryV1>(runtime, LINKSPAN_OOT_SCENE_QUERY_SERVICE);
    if (lists_ && (!lists_->layout_id || std::strcmp(lists_->layout_id, PLAYTEST_LAYOUT_ID) || !lists_->get_list))
        lists_ = nullptr;
    inCs_ = reinterpret_cast<decltype(inCs_)>(Symbol(runtime, "Player_InCsMode"));
    inputYaw_ = reinterpret_cast<decltype(inputYaw_)>(Symbol(runtime, "Camera_GetInputDirYaw"));
    return inCs_ && inputYaw_;
}
std::string Gameplay::Blocked() const {
    if (!play_ || !player_ || !player_->actor.update)
        return "gameplay indisponível";
    if (play_->pauseCtx.state || play_->msgCtx.msgMode || play_->gameOverCtx.state)
        return "pausa, texto ou game over";
    if (play_->transitionTrigger || play_->transitionMode || play_->roomCtx.status || play_->haltAllActors)
        return "transição/carregamento em andamento";
    constexpr uint32_t flags1 = PLAYER_STATE1_LOADING | PLAYER_STATE1_INPUT_DISABLED | PLAYER_STATE1_DEAD |
                                PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_TALKING |
                                PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_CLIMBING_LEDGE |
                                PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_DAMAGED |
                                PLAYER_STATE1_IN_WATER | PLAYER_STATE1_FLOOR_DISABLED | PLAYER_STATE1_JUMPING |
                                PLAYER_STATE1_FREEFALL;
    constexpr uint32_t flags2 = PLAYER_STATE2_CRAWLING | PLAYER_STATE2_GRABBED_BY_ENEMY | PLAYER_STATE2_FROZEN |
                                PLAYER_STATE2_OCARINA_PLAYING | PLAYER_STATE2_FORCED_VOID_OUT;
    if (play_->csCtx.state || player_->csAction || inCs_(play_) || (player_->stateFlags1 & flags1) ||
        (player_->stateFlags2 & flags2) || player_->doorTimer || player_->actor.parent)
        return "Player bloqueado: cutscene, caminhada automática, porta ou ação incompatível";
    if (movement_ && movement_->is_player_rolling && movement_->is_player_rolling())
        return "Player rolando";
    return {};
}
Json Gameplay::CameraState() const {
    if (!play_ || !camera_ || !camera_->get_view || !inputYaw_)
        throw std::runtime_error("serviço público de câmera indisponível");
    ShipOotCameraViewV1 v{};
    v.size = sizeof(v);
    if (camera_->get_view(&v) != SHIP_NATIVE_OK)
        throw std::runtime_error("câmera ativa indisponível");
    const int i = play_->activeCamera;
    if (i < 0 || i >= NUM_CAMS || !play_->cameraPtrs[i])
        throw std::runtime_error("câmera ativa inválida");
    const double dx = double(v.at[0]) - v.eye[0], dy = double(v.at[1]) - v.eye[1], dz = double(v.at[2]) - v.eye[2];
    const int16_t input = inputYaw_(play_->cameraPtrs[i]);
    return { { "eye", Position(v.eye) },
             { "at", Position(v.at) },
             { "yaw", std::atan2(dx, dz) * 180 / Pi },
             { "pitch", std::atan2(dy, std::hypot(dx, dz)) * 180 / Pi },
             { "distance", std::hypot(std::hypot(dx, dz), dy) },
             { "input_yaw", input * (180.0 / 32768.0) },
             { "input_yaw_units", input },
             { "fov", v.fov },
             { "mode",
               cameraToken_ && camera_->is_owned(cameraToken_) ? "free" : (i == CAM_ID_MAIN ? "normal" : "external") },
             { "active_camera", i },
             { "frame", frame_ },
             { "scene_epoch", epoch_ } };
}
Json Gameplay::Actors(const Json& a) const {
    if (!play_ || !player_)
        throw std::runtime_error("atores exigem gameplay com Player");
    std::vector<Json> matches;
    std::unordered_set<const Actor*> seen;
    bool truncated = false;
    for (int cat = 0; cat < 12; ++cat) {
        if (a.contains("category") && a["category"] != cat)
            continue;
        for (auto* actor = play_->actorCtx.actorLists[cat].head; actor; actor = actor->next) {
            if (seen.size() >= 4096 || !seen.insert(actor).second) {
                truncated = true;
                break;
            }
            if (!actor->update)
                continue; // morto/aguardando destruição
            const double distance = Distance(actor->world.pos, player_->actor.world.pos);
            if (!std::isfinite(distance) || (a.contains("radius") && distance > a["radius"].get<double>()))
                continue;
            matches.push_back({ { "id", actor->id },
                                { "name", Name(actor->id) },
                                { "category", cat },
                                { "category_name", Categories[cat] },
                                { "position", Position(actor->world.pos) },
                                { "room", actor->room },
                                { "distance", distance },
                                { "params", actor->params } });
        }
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const Json& a, const Json& b) { return a["distance"] < b["distance"]; });
    const size_t offset = a.value("offset", 0u), limit = a.value("limit", 128u);
    Json result = Json::array();
    for (size_t i = offset; i < matches.size() && i < offset + limit; ++i)
        result.push_back(matches[i]);
    return { { "actors", result },         { "total", matches.size() },
             { "offset", offset },         { "has_more", offset + limit < matches.size() },
             { "truncated", truncated },   { "frame", frame_ },
             { "scene", play_->sceneNum }, { "room", play_->roomCtx.curRoom.num },
             { "scene_epoch", epoch_ },    { "name_source", "SDK actor_table.h; tipos dinâmicos: null" } };
}
Json Gameplay::SceneMap(const Json& a) const {
    if (!play_)
        throw std::runtime_error("scene_map exige gameplay");
    const size_t offset = a.value("offset", 0u), limit = a.value("limit", 128u);
    Json j = { { "source", "live_play_state" },
               { "frame", frame_ },
               { "scene", play_->sceneNum },
               { "scene_epoch", epoch_ },
               { "room", play_->roomCtx.curRoom.num },
               { "previous_room", play_->roomCtx.prevRoom.num },
               { "room_loading", play_->roomCtx.status != 0 },
               { "current_entrance", play_->curSpawn },
               { "spawns", nullptr },
               { "entrances", nullptr },
               { "exits", nullptr },
               { "rooms", Json::array() },
               { "transitions", Json::array() },
               { "counts", Json::object() },
               { "offset", offset },
               { "has_more", false },
               { "complete", lists_ != nullptr },
               { "warnings", Json::array() } };
    const auto* save = engine_->get_save_context && engine_->save_context_size == sizeof(SaveContext)
                           ? static_cast<const SaveContext*>(engine_->get_save_context())
                           : nullptr;
    j["entrance_index"] = save ? Json(save->entranceIndex) : Json(nullptr);
    auto page = [&](size_t count, const char* key, auto emit) {
        j["counts"][key] = count;
        j[key] = Json::array();
        for (size_t i = offset; i < count && i < offset + limit; ++i)
            j[key].push_back(emit(i));
        if (offset + limit < count)
            j["has_more"] = true;
    };
    for (uint32_t kind = 0; kind < 3 && lists_; ++kind) {
        ShipOotSceneListV1 list{};
        list.size = sizeof(list);
        const char* key = kind == 0 ? "spawns" : (kind == 1 ? "entrances" : "exits");
        const uint32_t stride = kind == 0 ? sizeof(ActorEntry) : (kind == 1 ? sizeof(EntranceEntry) : sizeof(uint16_t));
        if (lists_->get_list(kind, &list) != SHIP_NATIVE_OK || list.stride != stride || list.count > 65535 ||
            (list.count && !list.data)) {
            j["complete"] = false;
            j["warnings"].push_back(std::string("lista indisponível/incompatível: ") + key);
            continue;
        }
        page(list.count, key, [&](size_t i) -> Json {
            if (kind == 0) {
                const auto& e = static_cast<const ActorEntry*>(list.data)[i];
                return { { "index", i },
                         { "id", e.id },
                         { "position", Position(e.pos) },
                         { "rotation", { e.rot.x, e.rot.y, e.rot.z } },
                         { "params", e.params } };
            }
            if (kind == 1) {
                const auto& e = static_cast<const EntranceEntry*>(list.data)[i];
                return { { "index", i }, { "spawn", e.spawn }, { "room", e.room } };
            }
            return { { "index", i }, { "entrance", static_cast<const uint16_t*>(list.data)[i] } };
        });
    }
    if (!lists_)
        j["warnings"].push_back("host sem linkspan.oot.scene_query v1: spawns, entradas e exits ficam null");
    // Ponteiro individual conhecido, nunca usado como início de um array inferido.
    if (play_->linkActorEntry)
        j["selected_spawn"] = { { "position", Position(play_->linkActorEntry->pos) },
                                { "params", play_->linkActorEntry->params } };
    page(play_->numRooms, "rooms", [&](size_t i) -> Json {
        return { { "id", i },
                 { "current", i == static_cast<size_t>(play_->roomCtx.curRoom.num) },
                 { "previous", i == static_cast<size_t>(play_->roomCtx.prevRoom.num) } };
    });
    if (play_->transiActorCtx.numActors && !play_->transiActorCtx.list)
        throw std::runtime_error("lista de transições ainda não carregada");
    page(play_->transiActorCtx.numActors, "transitions", [&](size_t i) -> Json {
        const auto& t = play_->transiActorCtx.list[i];
        const int id = std::abs(int(t.id)) & 0x1FFF; // sinal de instância e bits reservados
        return { { "index", i },
                 { "id", id },
                 { "raw_id", t.id },
                 { "name", Name(id) },
                 { "params", t.params },
                 { "position", Position(t.pos) },
                 { "rotY", t.rotY },
                 { "front_room", t.sides[0].room },
                 { "back_room", t.sides[1].room },
                 { "effects", { t.sides[0].effects, t.sides[1].effects } },
                 { "crossing", CrossingPlane(id, t.params, t.rotY, t.sides[0].room, t.sides[1].room) } };
    });
    return j;
}
Json Gameplay::SceneOverview(const Json& args) const {
    if (!play_ || !player_ || !player_->actor.update)
        throw std::runtime_error("scene_overview exige gameplay com Player");
    if (play_->transitionTrigger || play_->transitionMode || play_->roomCtx.status)
        throw std::runtime_error("scene_overview: transição/carregamento em andamento; tente novamente");
    if (args.contains("scene_epoch") && args["scene_epoch"] != epoch_)
        throw std::runtime_error("scene_overview: scene_epoch mudou; descarte as páginas e reinicie");
    const auto* header = play_->colCtx.colHeader; // Local ao callback; nunca publicado/retido.
    if (!header)
        throw std::runtime_error("scene_overview: colisão estática indisponível");
    Json a = args;
    a["y_min"] = args.value("y_min", double(player_->actor.world.pos.y) - 200);
    a["y_max"] = args.value("y_max", double(player_->actor.world.pos.y) + 300);
    if (a["y_min"] > a["y_max"])
        throw std::runtime_error("scene_overview: y_min maior que y_max");
    auto j = OverviewCollision(*header, a);
    j["scene"] = play_->sceneNum;
    j["scene_epoch"] = epoch_;
    j["frame"] = frame_;
    j["room"] = play_->roomCtx.curRoom.num;
    j["previous_room"] = play_->roomCtx.prevRoom.num;
    j["y_min"] = a["y_min"];
    j["y_max"] = a["y_max"];
    j["stride"] = a.value("stride", 1u);
    j["link"] = { { "position", Position(player_->actor.world.pos) },
                  { "yaw", player_->actor.shape.rot.y * (180.0 / 32768.0) },
                  { "room", j["room"] } };
    const auto limit = a.value("limit", 128u), to = a.value("transition_offset", 0u);
    // Reuse the existing transition semantics, only copying values inside this callback.
    const auto map = SceneMap({ { "offset", to }, { "limit", limit } });
    j["transitions"] = map["transitions"];
    j["rooms"] = Json::array();
    const uint64_t ro = a.value("room_offset", uint64_t(0));
    for (uint64_t i = ro; i < uint64_t(play_->numRooms) && i < ro + limit; ++i)
        j["rooms"].push_back({ { "id", i }, { "current", i == uint64_t(play_->roomCtx.curRoom.num) } });
    const uint64_t nr = ro + j["rooms"].size();
    j["next_offsets"]["room_offset"] = nr < uint64_t(play_->numRooms) ? Json(nr) : Json(nullptr);
    // Room ids have no geometry association in CollisionPoly. List all bounded room ids separately.
    j["room_count"] = play_->numRooms;
    j["counts"]["transitions"] = map["counts"]["transitions"];
    const uint64_t nt = uint64_t(to) + j["transitions"].size();
    j["next_offsets"]["transition_offset"] = nt < play_->transiActorCtx.numActors ? Json(nt) : Json(nullptr);
    j["actors"] = Json::array();
    j["counts"]["actors"] = 0;
    j["next_offsets"]["actor_offset"] = nullptr;
    j["actors_truncated"] = false;
    if (a.value("include_actors", false)) {
        const auto ao = a.value("actor_offset", 0u);
        const auto actors = Actors({ { "offset", ao }, { "limit", limit } });
        j["actors"] = actors["actors"];
        j["counts"]["actors"] = actors["total"];
        j["actors_truncated"] = actors["truncated"];
        j["next_offsets"]["actor_offset"] = actors["has_more"].get<bool>() ? Json(uint64_t(ao) + limit) : Json(nullptr);
    }
    j["dynamic_collision"] = "omitted";
    j["warnings"] = { "Colisão estática sem sala por polígono; salas localizadas somente nas transições/atores/água.",
                      "DynaPoly omitido: buffers baked sem contagem pública de geometria válida neste callback." };
    j["has_more"] = false;
    for (const auto& next : j["next_offsets"])
        if (!next.is_null())
            j["has_more"] = true;
    // Leave space for the ticket/result envelope and id (MaxLine is shared with the Python client).
    if (j.dump().size() > MaxLine - 4096)
        throw std::runtime_error("scene_overview: página grande demais; reduza limit");
    return j;
}
void Gameplay::Cleanup() {
    if (cameraToken_ && camera_ && camera_->release)
        camera_->release(cameraToken_);
    cameraToken_ = 0;
}
void Gameplay::SceneEnded() {
    Cleanup();
    ++epoch_;
    play_ = nullptr;
    player_ = nullptr;
    segment_ = nullptr;
    previousTick_ = 0;
}
Json Gameplay::Observation() const {
    Json j{ { "scene_epoch", epoch_ },
            { "grounded", player_ ? Json(bool(player_->actor.bgCheckFlags & 1)) : Json(nullptr) },
            { "controllable", Blocked().empty() },
            { "blocked_reason", Blocked() } };
    try {
        j["camera"] = CameraState();
    } catch (...) { j["camera"] = nullptr; }
    return j;
}
void Gameplay::Tick(PlayState* play, uint64_t frame, Server& server, ShipOotInputPadV1* pad) {
    auto* player = play ? static_cast<Player*>(engine_->get_player()) : nullptr;
    if (play && engine_->get_play_state() != play) {
        play = nullptr;
        player = nullptr;
    }
    if (play != play_ || player != player_ ||
        (play && (play->sceneSegment != segment_ || play->gameplayFrames < previousTick_))) {
        ++epoch_;
        Cleanup();
    }
    play_ = play;
    player_ = player;
    segment_ = play ? play->sceneSegment : nullptr;
    previousTick_ = play ? play->gameplayFrames : 0;
    frame_ = frame;
    if (cameraToken_ && !camera_->is_owned(cameraToken_))
        cameraToken_ = 0;
    server.Commands().Tick([&] { Cleanup(); }, [&](Request& request) { return Execute(request, server.Queue(), pad); });
}
std::optional<Json> Gameplay::Execute(Request& r, InputQueue& queue, ShipOotInputPadV1* pad) {
    auto& a = r.args;
    if (r.op == "scene_overview")
        return SceneOverview(a);
    if (r.op == "scene_map")
        return SceneMap(a);
    if (r.op == "actors")
        return Actors(a);
    if (r.op == "camera_state")
        return CameraState();
    if (r.op == "camera_set") {
        if (!camera_ || !camera_->acquire || !camera_->set_view || !camera_->release || !camera_->is_owned)
            throw std::runtime_error("linkspan.oot.camera v1 indisponível");
        if (a.value("mode", std::string("free")) == "normal") {
            Cleanup();
            return Json{ { "status", "released" } };
        }
        const auto blocked = Blocked();
        if (!blocked.empty())
            return Json{ { "status", "refused" }, { "reason", blocked } };
        ShipOotCameraViewV1 v{};
        v.size = sizeof(v);
        if (camera_->get_view(&v) != SHIP_NATIVE_OK)
            throw std::runtime_error("câmera não disponível");
        if (a.contains("eye")) {
            for (int i = 0; i < 3; ++i) {
                v.eye[i] = a["eye"][i].get<float>();
                v.at[i] = a["at"][i].get<float>();
            }
        } else {
            if (a.contains("at")) {
                for (int i = 0; i < 3; ++i)
                    v.at[i] = a["at"][i].get<float>();
            } else {
                v.at[0] = player_->actor.world.pos.x;
                v.at[1] = player_->actor.world.pos.y + 40;
                v.at[2] = player_->actor.world.pos.z;
            }
            const double yaw = a["yaw"].get<double>() * Pi / 180, pitch = a["pitch"].get<double>() * Pi / 180,
                         d = a["distance"].get<double>();
            v.eye[0] = float(v.at[0] - std::sin(yaw) * std::cos(pitch) * d);
            v.eye[1] = float(v.at[1] - std::sin(pitch) * d);
            v.eye[2] = float(v.at[2] - std::cos(yaw) * std::cos(pitch) * d);
        }
        bool acquired = false;
        if (!cameraToken_) {
            if (camera_->acquire("linkspan.tool.playtest", &cameraToken_) != SHIP_NATIVE_OK)
                return Json{ { "status", "refused" }, { "reason", "câmera ocupada ou cutscene" } };
            acquired = true;
        }
        const auto status = camera_->set_view(cameraToken_, &v);
        if (status != SHIP_NATIVE_OK) {
            if (acquired)
                Cleanup();
            throw std::runtime_error("set_view recusado pelo host");
        }
        return Json{ { "status", "applied" },  { "eye", Position(v.eye) },
                     { "at", Position(v.at) }, { "mode", "free" },
                     { "frame", frame_ },      { "note", "vista reaplicada pelo host no update da câmera" } };
    }
    // Operações longas não retêm ponteiros do jogo; apenas epoch e valores JSON.
    // O servidor carimba _epoch no aceite com a cena que o cliente viu: um pedido aceito antes de Play_Destroy não
    // vale para a cena seguinte. Sem carimbo (chamada direta), vale a cena desta primeira execução.
    const bool first = !a.contains("_deadline");
    if (first) {
        if (!a.contains("_epoch"))
            a["_epoch"] = epoch_;
        a["_deadline"] = Now() + a.value("timeout", r.op == "teleport" ? 3.0 : 15.0);
        a["_start_frame"] = frame_;
        if (player_)
            a["_before"] = Position(player_->actor.world.pos);
    }
    auto finish = [&](const char* status, const std::string& reason) -> std::optional<Json> {
        // Recusa antes de assumir o pad preserva a fila e o input que outro pedido já pôs neste frame.
        if (a.value("_owns", false)) {
            Neutral(pad);
            queue.Cancel();
        }
        Json result{ { "status", status },
                     { "reason", reason },
                     { "frame", frame_ },
                     { "scene_epoch", epoch_ },
                     { "start_frame", a["_start_frame"] },
                     { "before", a.value("_before", Json(nullptr)) },
                     { "position", player_ ? Position(player_->actor.world.pos) : Json(nullptr) },
                     { "mutated", a.value("_applied", false) } };
        if (r.op == "walk_to" && player_) {
            const double dx = a["x"].get<double>() - player_->actor.world.pos.x,
                         dz = a["z"].get<double>() - player_->actor.world.pos.z;
            result["remaining"] = std::hypot(dx, dz);
            result["initial_distance"] = a.value("_initial", result["remaining"]);
            result["progress"] = result["initial_distance"].get<double>() - result["remaining"].get<double>();
            result["room"] = play_->roomCtx.curRoom.num;
        }
        return result;
    };
    if (a["_epoch"] != epoch_)
        return finish("scene_changed", "cena ou Player mudou; operação interrompida");
    if (Now() >= a["_deadline"].get<double>())
        return finish("timeout", "limite de tempo atingido");
    if (!pad)
        return finish("refused", "porta de input indisponível");
    if (r.op == "walk_to" && play_ && play_->roomCtx.status) {
        if (a.value("_owns", false))
            Neutral(pad);
        a["_waited"] = true;
        return std::nullopt;
    }
    const auto blocked = Blocked();
    if (!blocked.empty())
        return finish("refused", blocked);
    if (!world_ || !world_->raycast_floor || !world_->wall_check)
        return finish("unsupported", "linkspan.oot.world v1 indisponível");
    if (!a.value("_owns", false) && queue.Size())
        return finish("refused", "fila de input ocupada; aguarde ou cancele antes");
    // O último passo de um input já saiu da fila, mas foi escrito no pad deste frame: assumir agora o apagaria.
    if (!a.value("_owns", false) && !queue.IdleAt(frame_))
        return std::nullopt;
    a["_owns"] = true;
    Neutral(pad);
    auto& actor = player_->actor;
    if (r.op == "teleport") {
        if (!a.contains("_target")) {
            if (!(actor.bgCheckFlags & 1))
                return finish("refused", "teleporte exige Player apoiado no chão");
            const float x = a["x"].get<float>(), y = a["y"].get<float>(), z = a["z"].get<float>();
            ShipOotWorldHitV1 floor{};
            floor.size = sizeof(floor);
            if (world_->raycast_floor(x, y + 50, z, &floor) != SHIP_NATIVE_OK || !floor.hit ||
                std::abs(floor.pos[1] - y) > 200 || floor.normal[1] < 0.5f)
                return finish("refused", "sem chão seguro a até 200 unidades de y (sonda inicia em y+50)");
            float target[3]{ x, floor.pos[1] + 1, z };
            ShipOotWorldHitV1 wall{};
            wall.size = sizeof(wall);
            if (world_->wall_check(target, target, 12, 30, &wall) != SHIP_NATIVE_OK || wall.hit)
                return finish("refused", "destino colide com parede");
            actor.world.pos = actor.prevPos = actor.home.pos = Vec3f{ target[0], target[1], target[2] };
            actor.velocity = {};
            actor.speedXZ = 0;
            player_->linearVelocity = 0;
            player_->pushedSpeed = 0;
            player_->fallStartHeight = static_cast<s16>(target[1]);
            player_->fallDistance = 0;
            if (a.contains("yaw")) {
                const auto units = static_cast<int16_t>(
                    static_cast<uint16_t>(static_cast<int32_t>(std::lround(a["yaw"].get<double>() * 32768 / 180))));
                actor.world.rot.y = actor.shape.rot.y = actor.home.rot.y = player_->yaw = player_->parallelYaw = units;
            }
            actor.bgCheckFlags = 0;
            actor.wallPoly = nullptr;
            actor.floorPoly = nullptr;
            a["_applied"] = true;
            a["_target"] = Position(actor.world.pos);
            a["_tick"] = play_->gameplayFrames;
            return std::nullopt;
        }
        const auto& t = a["_target"];
        const double dx = actor.world.pos.x - t["x"].get<double>(), dz = actor.world.pos.z - t["z"].get<double>();
        if (play_->gameplayFrames > a["_tick"].get<uint32_t>() + 1 && (actor.bgCheckFlags & 1) && actor.floorPoly &&
            std::hypot(dx, dz) <= 5 && std::abs(actor.world.pos.y - t["y"].get<double>()) <= 5)
            return finish("grounded", "posição confirmada após bgcheck em dois updates do jogo");
        return std::nullopt;
    }
    const double dx = a["x"].get<double>() - actor.world.pos.x, dz = a["z"].get<double>() - actor.world.pos.z;
    const double distance = std::hypot(dx, dz), tolerance = a.value("tolerance", 20.0);
    // A caminhada começa no primeiro frame sem carga de sala; esperar a carga não conta como falta de progresso.
    if (!a.contains("_best")) {
        a["_initial"] = distance;
        a["_best"] = distance;
        a["_progress_at"] = Now();
    }
    if (a.value("_waited", false)) {
        a.erase("_waited");
        a["_progress_at"] = Now();
    }
    if (distance <= tolerance)
        return finish("arrived", "alvo dentro da tolerância");
    if (!(actor.bgCheckFlags & 1))
        return finish("blocked", "Player deixou o chão; caminho exige revisão");
    // Progresso é aproximar-se do alvo: esteira ou empurrão lateral desloca o Player sem renovar o prazo.
    if (distance < a["_best"].get<double>() - 3) {
        a["_best"] = distance;
        a["_progress_at"] = Now();
    } else if (Now() - a["_progress_at"].get<double>() >= 1.5)
        return finish("blocked", "sem aproximação do alvo por 1,5 s");
    float from[3]{ actor.world.pos.x, actor.world.pos.y, actor.world.pos.z };
    const double step = std::min(distance, 12.0);
    float to[3]{ float(from[0] + dx / distance * step), from[1], float(from[2] + dz / distance * step) };
    ShipOotWorldHitV1 wall{};
    wall.size = sizeof(wall);
    if (world_->wall_check(from, to, 10, 25, &wall) != SHIP_NATIVE_OK)
        return finish("unsupported", "consulta de parede falhou");
    if (wall.hit)
        return finish("blocked", "parede no próximo passo");
    ShipOotWorldHitV1 floor{};
    floor.size = sizeof(floor);
    if (world_->raycast_floor(to[0], to[1] + 30, to[2], &floor) != SHIP_NATIVE_OK || !floor.hit ||
        floor.pos[1] < from[1] - 40 || floor.pos[1] > from[1] + 25)
        return finish("blocked", "degrau/queda sem chão seguro");
    const auto cam = CameraState();
    const auto stick = Steering(dx, dz, cam["input_yaw"].get<double>(), distance < 50 ? 40 : 80);
    const bool mirror =
        movement_ && movement_->get_setting_int && movement_->get_setting_int("gEnhancements.MirroredWorld", 0);
    pad->stick_x = mirror ? -stick.first : stick.first;
    pad->stick_y = stick.second;
    a["_applied"] = true;
    return std::nullopt;
}
} // namespace Playtest
