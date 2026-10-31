// Executa o adaptador real contra serviços fake; não é prova de bgcheck/render em jogo.
#include <winsock2.h>
#include "gameplay.h"
#include <cmath>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <thread>

using namespace Playtest;
namespace {
std::unique_ptr<PlayState> play;
std::unique_ptr<Player> player;
ShipOotEngineV1 engine{};
ShipOotCameraV1 camera{};
ShipOotWorldV1 world{};
ShipOotMovementV1 movement{};
ShipOotSceneQueryV1 lists{};
ShipOotCameraViewV1 view{};
uint64_t owned = 0;
// Injeta pedidos da thread TCP dentro do callback, entre as etapas do Tick.
std::function<void()> onIsOwned;
bool floorHit = true, wallHit = false;
bool mirror = false;
ActorEntry spawns[1]{};
EntranceEntry entrances[1]{};
uint16_t exits[1]{ 266 };
void Check(bool b, std::source_location at = std::source_location::current()) {
    if (!b)
        throw std::runtime_error("verificação gameplay falhou na linha " + std::to_string(at.line()));
}
void Update(Actor*, PlayState*) {
}
void* SHIP_NATIVE_CALL GetPlay() {
    return play.get();
}
void* SHIP_NATIVE_CALL GetPlayer() {
    return player.get();
}
int32_t InCs(PlayState*) {
    return 0;
}
int16_t InputYaw(Camera*) {
    return 16384;
}
ShipNativeStatus SHIP_NATIVE_CALL Resolve(void*, const char* name, uintptr_t* p) {
    *p = std::strcmp(name, "Player_InCsMode") == 0 ? reinterpret_cast<uintptr_t>(InCs)
                                                   : reinterpret_cast<uintptr_t>(InputYaw);
    return SHIP_NATIVE_OK;
}
const void* SHIP_NATIVE_CALL Service(void*, const char* name, uint32_t, uint32_t) {
    if (!std::strcmp(name, LINKSPAN_OOT_ENGINE_SERVICE))
        return &engine;
    if (!std::strcmp(name, LINKSPAN_OOT_CAMERA_SERVICE))
        return &camera;
    if (!std::strcmp(name, LINKSPAN_OOT_WORLD_SERVICE))
        return &world;
    if (!std::strcmp(name, LINKSPAN_OOT_MOVEMENT_SERVICE))
        return &movement;
    if (!std::strcmp(name, LINKSPAN_OOT_SCENE_QUERY_SERVICE))
        return &lists;
    return nullptr;
}
void Setup() {
    play = std::make_unique<PlayState>();
    player = std::make_unique<Player>();
    play->cameraPtrs[0] = &play->mainCamera;
    play->roomCtx.prevRoom.num = -1;
    player->actor.update = Update;
    player->actor.bgCheckFlags = 1;
    player->actor.id = 0;
    player->actor.category = 2;
    play->actorCtx.actorLists[2].head = &player->actor;
    engine.size = sizeof(engine);
    engine.layout_id = PLAYTEST_LAYOUT_ID;
    engine.play_state_size = sizeof(PlayState);
    engine.player_size = sizeof(Player);
    engine.get_play_state = GetPlay;
    engine.get_player = GetPlayer;
    view.size = sizeof(view);
    view.eye[2] = -100;
    view.fov = 60;
    camera.size = sizeof(camera);
    camera.acquire = [](const char*, uint64_t* t) -> ShipNativeStatus {
        *t = owned = 1;
        return SHIP_NATIVE_OK;
    };
    camera.release = [](uint64_t) -> ShipNativeStatus {
        owned = 0;
        return SHIP_NATIVE_OK;
    };
    camera.is_owned = [](uint64_t t) -> uint8_t {
        if (onIsOwned) {
            auto inject = std::move(onIsOwned);
            onIsOwned = nullptr;
            inject();
        }
        return t && owned == t;
    };
    camera.get_view = [](ShipOotCameraViewV1* v) -> ShipNativeStatus {
        *v = view;
        return SHIP_NATIVE_OK;
    };
    camera.set_view = [](uint64_t, const ShipOotCameraViewV1* v) -> ShipNativeStatus {
        view = *v;
        return SHIP_NATIVE_OK;
    };
    world.size = sizeof(world);
    movement.size = sizeof(movement);
    movement.layout_id = PLAYTEST_LAYOUT_ID;
    movement.get_setting_int = [](const char*, int32_t) -> int32_t { return mirror; };
    world.raycast_floor = [](float x, float, float z, ShipOotWorldHitV1* h) -> ShipNativeStatus {
        h->hit = floorHit;
        h->pos[0] = x;
        h->pos[1] = 0;
        h->pos[2] = z;
        h->normal[1] = 1;
        return SHIP_NATIVE_OK;
    };
    world.wall_check = [](const float*, const float*, float, float, ShipOotWorldHitV1* h) -> ShipNativeStatus {
        h->hit = wallHit;
        return SHIP_NATIVE_OK;
    };
    lists.size = sizeof(lists);
    lists.layout_id = PLAYTEST_LAYOUT_ID;
    lists.get_list = [](uint32_t kind, ShipOotSceneListV1* out) -> ShipNativeStatus {
        out->count = 1;
        out->data = kind == 0 ? static_cast<void*>(spawns)
                              : (kind == 1 ? static_cast<void*>(entrances) : static_cast<void*>(exits));
        out->stride = kind == 0 ? sizeof(ActorEntry) : (kind == 1 ? sizeof(EntranceEntry) : sizeof(uint16_t));
        return SHIP_NATIVE_OK;
    };
}
// Exercita o servidor TCP real, além dos fakes Python do cliente.
void WireTest() {
    WSADATA data{};
    Check(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(bind(probe, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    int len = sizeof(addr);
    Check(getsockname(probe, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    const auto port = ntohs(addr.sin_port);
    closesocket(probe);
    Server server("wire-test");
    Check(server.Start(port, "test-only"));
    auto connectTo = [&] {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        DWORD timeout = 2000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        Check(connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        return s;
    };
    auto exchange = [&](SOCKET s, Json request) {
        const auto bytes = request.dump() + '\n';
        Check(send(s, bytes.data(), static_cast<int>(bytes.size()), 0) == static_cast<int>(bytes.size()));
        std::string line;
        char c = 0;
        while (recv(s, &c, 1, 0) == 1 && c != '\n')
            line += c;
        Check(c == '\n');
        return Json::parse(line);
    };
    const SOCKET a = connectTo(), b = connectTo();
    auto hello = Json{ { "id", "1" }, { "op", "hello" }, { "client", "test" }, { "token", "test-only" } };
    Check(exchange(a, hello)["protocol_version"] == 2); // cliente v1
    hello["protocol_version"] = 1;
    Check(exchange(b, hello)["ok"] == false);
    hello["protocol_version"] = 2;
    Check(exchange(b, hello)["error"] == "busy");
    auto accepted = exchange(a, { { "id", "2" }, { "op", "camera_state" } });
    const auto ticket = accepted["ticket"];
    Check(exchange(a, { { "id", "3" }, { "op", "result" }, { "ticket", ticket } })["status"] == "pending");
    server.Commands().Tick([](Request&) -> std::optional<Json> { return Json{ { "input_yaw", 90 } }; });
    Check(exchange(a, { { "id", "4" }, { "op", "result" }, { "ticket", ticket } })["result"]["input_yaw"] == 90);
    // Mailbox VFS independente do PlayState: também pode ser atendido no título.
    auto vfs = exchange(a, { { "id", "v1" }, { "op", "vfs_layers" }, { "paths", { "test/missing" } } });
    Check(vfs["ok"] == true && vfs["ticket"].get<uint64_t>() >= VfsTicketBase);
    Check(exchange(a, { { "id", "v2" }, { "op", "camera_state" } })["ok"] == false);
    Check(exchange(a, { { "id", "v3" }, { "op", "result" }, { "ticket", vfs["ticket"] } })["status"] == "pending");
    server.Vfs().Tick(17);
    auto vfsResult = exchange(a, { { "id", "v4" }, { "op", "result" }, { "ticket", vfs["ticket"] } });
    Check(vfsResult["result"]["frame"] == 17 && vfsResult["result"]["complete"] == false);
    Check(vfsResult["result"]["observations"][0]["hostStatus"] == SHIP_NATIVE_UNSUPPORTED);
    exchange(a, { { "id", "v5" }, { "op", "cancel" } });
    Check(exchange(a, { { "id", "v6" }, { "op", "result" }, { "ticket", vfs["ticket"] } })["ok"] == false);
    Check(exchange(a, { { "id", "5" }, { "op", "scene_map" }, { "limit", 129 } })["ok"] == false);
    // A2: o aceite carimba a mutação com o epoch do snapshot publicado; o cliente não manda o seu.
    Snapshot seen;
    seen.fields["scene_epoch"] = 7;
    server.Publish(seen);
    Check(exchange(
              a, { { "id", "5b" }, { "op", "teleport" }, { "x", 1 }, { "y", 0 }, { "z", 2 }, { "_epoch", 9 } })["ok"] ==
          false);
    exchange(a, { { "id", "5c" }, { "op", "teleport" }, { "x", 1 }, { "y", 0 }, { "z", 2 } });
    Json stamped;
    server.Commands().Tick([&](Request& r) -> std::optional<Json> {
        stamped = r.args.value("_epoch", Json(nullptr));
        return Json::object();
    });
    Check(stamped == 7);
    accepted = exchange(a, { { "id", "6" }, { "op", "walk_to" }, { "x", 10 }, { "z", 20 } });
    hello["force"] = true;
    Check(exchange(b, hello)["ok"] == true);
    Check(!server.Commands().Busy() && server.Commands().TakeCleanup());
    Check(exchange(a, { { "id", "7" }, { "op", "state" } })["ok"] == false);
    Check(exchange(b, { { "id", "8" }, { "op", "result" }, { "ticket", accepted["ticket"] } })["ok"] == false);
    vfs = exchange(b, { { "id", "v7" }, { "op", "vfs_layers" }, { "paths", { "test/missing" } } });
    hello["force"] = true;
    Check(exchange(a, hello)["ok"] == true && !server.Vfs().Busy());
    Check(exchange(a, { { "id", "v8" }, { "op", "result" }, { "ticket", vfs["ticket"] } })["ok"] == false);
    Check(exchange(b, hello)["ok"] == true);
    exchange(b, { { "id", "9" }, { "op", "camera_set" }, { "mode", "normal" } });
    closesocket(b);
    closesocket(a);
    for (int i = 0; i < 100 && server.Commands().Busy(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Check(!server.Commands().Busy());
    server.Stop();
    WSACleanup();
}
} // namespace
int main() {
    try {
        Setup();
        ShipNativeRuntime runtime{};
        runtime.get_service = Service;
        runtime.resolve_symbol = Resolve;
        Gameplay game;
        Check(game.Init(&runtime));
        Server server("test");
        ShipOotInputPadV1 pad{};
        uint64_t frame = 0;
        auto tick = [&] {
            ++play->gameplayFrames;
            server.Queue().Apply(++frame, pad);
            game.Tick(play.get(), frame, server, &pad);
        };
        auto submit = [&](const char* line) {
            Request r;
            std::string error;
            Check(ParseRequest(line, r, error));
            return server.Commands().Submit(r);
        };
        tick();
        auto t = submit(R"({"id":"a","op":"teleport","x":50,"y":0,"z":10,"yaw":90})");
        player->csAction = 1;
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "refused" && player->actor.world.pos.x == 0);
        player->csAction = 0;
        player->stateFlags1 = PLAYER_STATE1_INPUT_DISABLED;
        t = submit(R"({"id":"a","op":"teleport","x":50,"y":0,"z":10})");
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "refused");
        player->stateFlags1 = 0;
        floorHit = false;
        t = submit(R"({"id":"a","op":"teleport","x":50,"y":0,"z":10})");
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "refused" && player->actor.world.pos.x == 0);
        floorHit = true;
        player->linearVelocity = 7;
        player->actor.velocity.x = 8;
        t = submit(R"({"id":"a","op":"teleport","x":50,"y":0,"z":10,"yaw":90})");
        tick();
        Check(server.Commands().Result(t)["status"] == "pending");
        Check(player->actor.world.pos.x == 50 && player->actor.prevPos.x == 50 && player->actor.home.pos.x == 50);
        Check(player->linearVelocity == 0 && player->actor.velocity.x == 0 && player->yaw == 16384);
        player->actor.world.pos.x = 99;
        tick();
        Check(server.Commands().Result(t)["status"] == "pending" && player->actor.world.pos.x == 99);
        player->actor.world.pos.x = 50; // prova que a DLL não reaplica o teleporte a cada callback
        CollisionPoly fakePoly{};
        player->actor.bgCheckFlags = 1;
        player->actor.floorPoly = &fakePoly;
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "grounded");
        t = submit(R"({"id":"a","op":"teleport","x":75,"y":0,"z":10,"timeout":0.1})");
        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "timeout");
        Check(server.Commands().Result(t)["result"]["mutated"] == true && player->actor.world.pos.x == 75);
        player->actor.bgCheckFlags = 1;
        player->actor.floorPoly = &fakePoly;
        t = submit(R"({"id":"a","op":"walk_to","x":150,"z":10})");
        tick();
        Check(pad.stick_x == 0 && pad.stick_y == 80); // alvo +X, câmera input yaw +90
        wallHit = true;
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "blocked" && pad.stick_y == 0);
        wallHit = false;
        t = submit(R"({"id":"a","op":"walk_to","x":150,"z":10})");
        tick();
        play->roomCtx.curRoom.num = 1;
        player->actor.world.pos.x = 140;
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "arrived");
        mirror = true;
        t = submit(R"({"id":"a","op":"walk_to","x":140,"z":-100})");
        tick();
        Check(pad.stick_x == 80 && pad.stick_y == 0);
        server.Commands().Cancel();
        tick();
        mirror = false;
        t = submit(R"({"id":"a","op":"walk_to","x":400,"z":10})");
        tick();
        play->gameplayFrames = 0;
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "scene_changed" && pad.stick_y == 0);
        t = submit(R"({"id":"a","op":"camera_set","yaw":0,"pitch":-30,"distance":100})");
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "applied" && owned);
        Check(view.eye[1] > view.at[1]);
        t = submit(R"({"id":"a","op":"camera_state"})");
        tick();
        Check(server.Commands().Result(t)["result"]["input_yaw"] == 90);
        server.Commands().Cancel();
        tick();
        Check(!owned);
        TransitionActorEntry trans{};
        trans.id = -ACTOR_EN_HOLL;
        trans.params = 4 << 6;
        trans.sides[1].room = 1;
        play->transiActorCtx.list = &trans;
        play->transiActorCtx.numActors = 1;
        play->numRooms = 2;
        t = submit(R"({"id":"a","op":"scene_map"})");
        tick();
        auto map = server.Commands().Result(t)["result"];
        Check(map["complete"] == true && map["spawns"].size() == 1 && map["exits"][0]["entrance"] == 266);
        Check(map["transitions"][0]["crossing"]["positive_room"] == 1);
        Actor npc{};
        npc.id = 0x15;
        npc.category = 4;
        npc.update = Update;
        npc.world.pos = player->actor.world.pos;
        play->actorCtx.actorLists[4].head = &npc;
        t = submit(R"({"id":"a","op":"actors","category":4,"radius":1})");
        tick();
        Check(server.Commands().Result(t)["result"]["actors"].size() == 1);
        t = submit(R"({"id":"a","op":"actors","category":4,"radius":0})");
        npc.world.pos.x += 5;
        tick();
        Check(server.Commands().Result(t)["result"]["actors"].empty());
        t = submit(R"({"id":"a","op":"scene_map","limit":1})");
        tick();
        Check(server.Commands().Result(t)["result"]["has_more"] == true);
        t = submit(R"({"id":"a","op":"walk_to","x":400,"z":10})");
        tick();
        play->roomCtx.status = 1;
        tick();
        Check(server.Commands().Result(t)["status"] == "pending" && pad.stick_y == 0);
        play->roomCtx.status = 0;
        player->actor.world.pos.x = 390;
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "arrived");
        // Regressões da revisão adversarial (R1-R5).
        // R3: o impulso de esteira e a altura de queda não sobrevivem ao teleporte.
        player->actor.bgCheckFlags = 1;
        player->pushedSpeed = 3;
        player->fallStartHeight = 500;
        player->fallDistance = 40;
        t = submit(R"({"id":"a","op":"teleport","x":60,"y":0,"z":10})");
        tick();
        Check(player->pushedSpeed == 0 && player->fallStartHeight == 1 && player->fallDistance == 0);
        player->actor.bgCheckFlags = 1;
        player->actor.floorPoly = &fakePoly;
        tick();
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "grounded");
        // R4: recusa antes de assumir o pad preserva a fila de input já aceita.
        Step hold;
        hold.frames = 100;
        hold.stick[1] = 50;
        server.Queue().Push({ hold });
        tick();
        t = submit(R"({"id":"a","op":"teleport","x":70,"y":0,"z":10})");
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "refused" && server.Queue().Size() == 1 &&
              pad.stick_y == 50);
        server.Queue().Cancel();
        tick();
        // R1: walk_to pedido durante a carga de sala começa quando a carga acaba, e a espera não conta como bloqueio.
        play->roomCtx.status = 1;
        t = submit(R"({"id":"a","op":"walk_to","x":600,"z":10})");
        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
        tick();
        Check(server.Commands().Result(t)["status"] == "pending");
        play->roomCtx.status = 0;
        tick();
        Check(server.Commands().Result(t)["status"] == "pending" && pad.stick_x == 0 && pad.stick_y == 80);
        // R5: deslocamento lateral sem aproximação do alvo não renova o prazo de progresso.
        for (int i = 0; i < 4; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(450));
            player->actor.world.pos.z += (i % 2) ? -6.0f : 6.0f;
            tick();
        }
        Check(server.Commands().Result(t)["result"]["status"] == "blocked");
        // R2: Cancel+Submit entre as etapas do callback não deixa a limpeza antiga soltar a câmera nova.
        t = submit(R"({"id":"a","op":"camera_set","yaw":0,"pitch":-30,"distance":100})");
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "applied" && owned);
        uint64_t fresh = 0;
        onIsOwned = [&] {
            server.Commands().Cancel();
            fresh = submit(R"({"id":"a","op":"camera_set","yaw":90,"pitch":-30,"distance":100})");
        };
        tick();
        Check(fresh && server.Commands().Result(fresh)["result"]["status"] == "applied" && owned);
        tick();
        Check(owned != 0);
        server.Commands().Cancel();
        tick();
        Check(!owned);
        // Revisão final (A1): teleporte pedido junto de um input de um frame espera o frame seguinte em vez de
        // apagar o input já escrito no pad.
        Step press;
        press.frames = 1;
        press.buttons = 0x8000;
        server.Queue().Push({ press });
        t = submit(R"({"id":"a","op":"teleport","x":100,"y":0,"z":200})");
        pad = {};
        tick();
        Check(pad.button == 0x8000 && server.Commands().Result(t)["status"] == "pending");
        tick();
        Check(pad.button == 0 && player->actor.world.pos.x == 100);
        player->actor.bgCheckFlags = 1;
        player->actor.floorPoly = &fakePoly;
        tick();
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "grounded");
        // A2: pedido carimbado no aceite com a cena que o cliente viu não teleporta na cena seguinte.
        Request stale;
        std::string staleError;
        Check(ParseRequest(R"({"id":"a","op":"teleport","x":120,"y":0,"z":30})", stale, staleError));
        stale.args["_epoch"] = game.Observation()["scene_epoch"];
        t = server.Commands().Submit(stale);
        game.SceneEnded();
        server.Queue().Cancel();
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "scene_changed" && player->actor.world.pos.x == 100);
        lists.layout_id = "host sem a proposta";
        Gameplay partial;
        Check(partial.Init(&runtime));
        t = submit(R"({"id":"a","op":"scene_map"})");
        partial.Tick(play.get(), ++frame, server, &pad);
        Check(server.Commands().Result(t)["result"]["complete"] == false);
        Check(server.Commands().Result(t)["result"]["spawns"].is_null());
        CollisionHeader collision{};
        Vec3i verts[] = { { -100, 0, -100 }, { 100, 0, -100 }, { 0, 0, 100 } };
        CollisionPoly triangle{};
        triangle.flags_vIB = 1;
        triangle.vIC = 2;
        triangle.normal.y = 32767;
        collision.numVertices = 3;
        collision.vtxList = verts;
        collision.numPolygons = 1;
        collision.polyList = &triangle;
        collision.minBounds = { -100, 0, -100 };
        collision.maxBounds = { 100, 0, 100 };
        play->colCtx.colHeader = &collision;
        t = submit(R"({"id":"a","op":"scene_overview","include_actors":true})");
        tick();
        auto overview = server.Commands().Result(t)["result"];
        Check(overview["polygons"].size() == 1 && overview["dynamic_collision"] == "omitted");
        Check(overview["actors"].size() == 2 && overview["room_count"] == 2);
        Check(overview["transitions"][0]["crossing"]["positive_room"] == 1);
        const auto oldEpoch = overview["scene_epoch"];
        play->sceneSegment = &collision;
        Request continued;
        continued.id = "a";
        continued.op = "scene_overview";
        continued.args = { { "scene_epoch", oldEpoch } };
        t = server.Commands().Submit(continued);
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "error");
        play->colCtx.colHeader = nullptr;
        t = submit(R"({"id":"a","op":"scene_overview"})");
        tick();
        Check(server.Commands().Result(t)["result"]["status"] == "error");
        game.Cleanup();
        WireTest();
        std::cout << "adaptador gameplay com serviços simulados: OK\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
