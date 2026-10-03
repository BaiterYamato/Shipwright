#include "actor_registry.h"
#include "transcode.h"
#include "room_actors.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace LinkSpanUnbound;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #expr); ++failures; } } while (0)

int main() {
    ActorDefinition type;
    std::vector<std::string> notes;
    const Json npc = Json::parse(R"({"name":"NPC","model":{"skeleton":"rig","animation":"idle","scale":0.02},
        "collision":{"radius":18,"height":63},"talk":{"message":"0xA001"},
        "look":{"limb":15,"turnAxis":[-2,0,0],"nodAxis":[0,0,"3"]}})");
    CHECK(ReadActorDefinition("example/npc", npc, type, notes));
    CHECK(type.skeleton == "__OTR__rig" && type.animation == "__OTR__idle");
    CHECK(type.talks && type.message == 0xA001 && type.talkRange == 68 && type.looks);
    CHECK(type.turnAxis.x == -1 && type.nodAxis.z == 1);
    std::vector<ShipOotActorModelSegmentV1> segments;
    const auto spec = ModelSpec(type, "example", segments);
    CHECK(spec.radius == 18 && spec.height == 63 && spec.scale == 0.02f && spec.turn_axis[0] == -1);

    Json edited = npc;
    edited["model"].erase("animation");
    CHECK(!ReadActorDefinition("example/npc", edited, type, notes));
    edited = npc; edited["model"]["scale"] = 0;
    CHECK(!ReadActorDefinition("example/npc", edited, type, notes));
    edited = npc; edited["model"]["lod"] = 1;
    CHECK(!ReadActorDefinition("example/npc", edited, type, notes));
    edited = npc; edited["base"] = "En_Toryo";
    CHECK(!ReadActorDefinition("example/npc", edited, type, notes));
    CHECK(!ReadActorDefinition("0x123", npc, type, notes));
    edited = npc; edited["look"]["turnAxis"] = {0,0,2};
    CHECK(ReadActorDefinition("example/npc", edited, type, notes) && !type.looks);
    edited = npc; edited["look"]["nodAxis"] = {0,1};
    CHECK(ReadActorDefinition("example/npc", edited, type, notes) && !type.looks);
    edited = npc; edited["talk"]["message"] = 65535;
    CHECK(ReadActorDefinition("example/npc", edited, type, notes) && type.message == 0);
    edited = npc; edited["collision"]["radius"] = 50000;
    CHECK(ReadActorDefinition("example/npc", edited, type, notes) && type.radius == INT16_MAX);
    edited = Json::parse(R"({"model":{"displayList":"pot"},"talk":{}})");
    CHECK(ReadActorDefinition("example/pot", edited, type, notes) && type.talks && type.talkRange == 50);
    edited["look"] = {{"limb",1}};
    CHECK(!ReadActorDefinition("example/pot", edited, type, notes));

    Json merged;
    CHECK(MergeActorLayers({{"a", npc.dump(), 0}, {"b", R"({"talk":{"message":42}})", 0}}, merged, notes));
    CHECK(ReadActorDefinition("example/npc", merged, type, notes) && type.message == 42 && type.radius == 18);
    CHECK(MergeActorLayers({{"a",npc.dump(),0},{"b","null",0}}, merged, notes) && merged.is_null());
    CHECK(MergeActorLayers({{"a",npc.dump(),0},{"b","null",0},{"c",R"({"model":{"displayList":"pot"}})",0}}, merged, notes));
    CHECK(ReadActorDefinition("example/pot", merged, type, notes) && type.skeleton.empty());
    CHECK(MergeActorLayers({{"a",npc.dump(),0},{"b","{bad",0}}, merged, notes) && merged == npc);
    CHECK(ActorNameFromPath("unbound/actors/example/npc.json") == "example/npc");
    CHECK(ActorNameFromPath("unbound/scenes.json").empty());

    auto resolve = [](const std::string& name) { return name == "example/npc" ? 4096 : name == "En_Kanban" ? 321 : -1; };
    int16_t id = -1;
    CHECK(ResolveRoomActorId(Json{{"id","example/npc"}}, resolve, id, notes, "test") && id == 4096);
    CHECK(ResolveRoomActorId(Json{{"id","En_Kanban"}}, resolve, id, notes, "test") && id == 321);
    CHECK(ResolveRoomActorId(Json{{"id","0x0005"}}, resolve, id, notes, "test") && id == 5);
    CHECK(!ResolveRoomActorId(Json{{"id","unknown"}}, resolve, id, notes, "test"));
    CHECK(!ResolveRoomActorId(Json{{"id",4096}}, resolve, id, notes, "test"));
    CHECK(!ResolveRoomActorId(Json{{"id",-1}}, resolve, id, notes, "test"));
    CHECK(!ResolveRoomActorId(Json{{"id","example/npc"},{"params",Json::object()}}, resolve, id, notes, "test"));

    TranscodeContext ctx; ctx.path = "test"; ctx.resolveActor = resolve;
    Json room = Json::parse(R"({"setups":{"0":{"actors":{"a":{"id":"example/npc"},"b":{"id":"unknown"},
        "c":{"id":"En_Kanban"},"d":{"id":4096}},"transitionActors":{"0":{"id":"En_Door"},"1":{"id":5}}}}})");
    const auto xml = TranscodeScene(room, true, ctx);
    CHECK(xml.find("Id=\"4096\"") != std::string::npos && xml.find("Id=\"321\"") != std::string::npos);
    CHECK(xml.find("Id=\"-1\"") != std::string::npos && ctx.notes.size() == 3);
    RoomActorsResult actors; std::string error;
    room["$schema"] = "unbound/room/1";
    CHECK(ApplyRoomActorLayers({}, 0, {{"test",room.dump(),0}}, actors, error, resolve) && actors.actors.size() == 2);

    std::printf("unbound actors: %s (%d falhas)\n", failures ? "falhou" : "ok", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
