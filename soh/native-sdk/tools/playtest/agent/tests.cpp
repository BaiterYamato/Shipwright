#include "server.h"
#include <iostream>
#include <stdexcept>

using namespace Playtest;
void Check(bool value) {
    if (!value)
        throw std::runtime_error("verificação falhou");
}
int main() {
    try {
        uint16_t buttons = 0;
        std::string error;
        Check(ParseButtons(Json::array({ "A", "START", "CUP", "DRIGHT" }), buttons, error));
        Check(buttons == 0x9108);
        Check(!ParseButtons(Json::array({ "X" }), buttons, error));
        Check(!ParseButtons(1, buttons, error));
        std::vector<Step> steps;
        Check(ParseSteps(Json::parse(R"([{"buttons":["A"],"stick":[-128,127],"frames":2},{}])"), steps, error));
        Check(steps.size() == 2 && steps[0].buttons == 0x8000);
        for (auto invalid : { R"([{"stick":[128,0]}])", R"([{"cstick":[0,-129]}])", R"([{"frames":0}])",
                              R"([{"frames":3601}])", R"([{"frames":true}])", R"([{"mode":"oops"}])", R"([])" })
            Check(!ParseSteps(Json::parse(invalid), steps, error));
        Check(!ParseSteps(Json::array({ { { "frames", 1.5 } } }), steps, error));
        Request request;
        Check(!ParseRequest("{", request, error));
        Check(!ParseRequest(R"({"id":"42","op":"wat"})", request, error));
        Check(request.id == "42");
        Check(ParseRequest(R"({"id":"42","op":"state"})", request, error));
        Check(Json::parse(Reply(request.id, { { "frame", 12 } }))["id"] == "42");
        Check(!Json::parse(ReplyError("42", "erro"))["ok"].get<bool>());
        InputQueue queue;
        bool unready = false;
        try {
            queue.Push({ Step{} });
        } catch (const std::runtime_error&) { unready = true; }
        Check(unready);
        ShipOotInputPadV1 pad{};
        queue.Apply(10, pad);
        Step a;
        a.buttons = 0x8000;
        a.frames = 2;
        Step b;
        b.buttons = 0x4000;
        b.stick[0] = 64;
        auto queued = queue.Push({ a, b });
        Check(queued.start_frame == 11 && queued.end_frame == 13);
        queue.Apply(11, pad);
        Check(pad.button == 0x8000);
        queue.Apply(12, pad);
        Check(pad.button == 0x8000);
        queue.Apply(13, pad);
        Check(pad.button == 0x4000 && pad.stick_x == 64);
        pad.button = 0x0020;
        queue.Apply(14, pad);
        Check(pad.button == 0x0020);
        b.merge = true;
        b.cstick[1] = -30;
        queue.Push({ b });
        pad.stick_y = 25;
        queue.Apply(15, pad);
        Check(pad.button == 0x4020 && pad.stick_y == 25 && pad.right_stick_y == -30);
        queue.Push({ a });
        queue.Cancel();
        Check(queue.Size() == 0);
        const auto afterCancel = queue.Push({ b });
        Check(afterCancel.start_frame == 17);
        queue.Apply(16, pad);
        Check(pad.button == 0 && pad.stick_x == 0 && pad.right_stick_y == 0);
        queue.Apply(17, pad);
        Check(pad.button == 0x4000);
        queue.Cancel();
        queue.Apply(18, pad);
        const auto full = queue.Push(std::vector<Step>(600));
        Check(full.start_frame == 19 && full.end_frame == 618);
        bool limited = false;
        try {
            queue.Push({ a });
        } catch (const std::runtime_error&) { limited = true; }
        Check(limited && queue.Size() == 600);
        Lease lease;
        Check(lease.Acquire(1, false));
        Check(lease.Acquire(1, false));
        Check(!lease.Acquire(2, false));
        Check(lease.Acquire(2, true));
        Check(!lease.Release(1) && lease.Owns(2));
        Check(lease.Release(2));
        Check(lease.Acquire(3, false));
        Check(!lease.Acquire(0, true));
        for (auto valid :
             { R"({"id":"v2","op":"scene_map","limit":128})", R"({"id":"v2","op":"actors","radius":100,"category":4})",
               R"({"id":"v2","op":"teleport","x":1,"y":2,"z":3,"yaw":90})",
               R"({"id":"v2","op":"camera_set","yaw":90,"pitch":-30,"distance":200})",
               R"({"id":"v2","op":"camera_set","eye":[0,1,0],"at":[1,0,0]})",
               R"({"id":"v2","op":"camera_set","mode":"normal"})", R"({"id":"v2","op":"camera_state"})",
               R"({"id":"v2","op":"walk_to","x":0,"z":0})" })
            Check(ParseRequest(valid, request, error));
        for (auto invalid :
             { R"({"id":"v2","op":"teleport","x":1,"z":2})", R"({"id":"v2","op":"walk_to","x":true,"z":0})",
               R"({"id":"v2","op":"walk_to","x":0,"z":0,"timeout":0})",
               R"({"id":"v2","op":"walk_to","x":0,"z":0,"_epoch":1})", R"({"id":"v2","op":"actors","radius":-1})",
               R"({"id":"v2","op":"actors","category":12})", R"({"id":"v2","op":"scene_map","limit":129})",
               R"({"id":"v2","op":"scene_map","offset":1.5})",
               R"({"id":"v2","op":"camera_set","mode":"normal","eye":[0,0,0]})",
               R"({"id":"v2","op":"camera_set","eye":[0,0,0],"at":[0,0,0]})",
               R"({"id":"v2","op":"camera_set","yaw":0,"pitch":90,"distance":10})",
               R"({"id":"v2","op":"camera_set","eye":[0,1,0],"at":[1,0,0],"yaw":1})",
               R"({"id":"v2","op":"result","ticket":0})" })
            Check(!ParseRequest(invalid, request, error));
        const auto zPlane = CrossingPlane(0x23, 4 << 6, 0, 0, 1);
        Check(zPlane["axis"] == "z" && zPlane["negative_room"] == 0 && zPlane["positive_room"] == 1);
        Check(CrossingPlane(0x23, 4 << 6, 16384, 0, 1)["axis"] == "x");
        Check(CrossingPlane(0x23, 4 << 6, 8192, 0, 1)["axis"] == "oblique_xz");
        Check(CrossingPlane(0x23, 2 << 6, 0, 0, 1)["positive_room"] == 0);
        Check(!CrossingPlane(0x23, 1 << 6, 0, 0, 1)["bidirectional"].get<bool>());
        Check(CrossingPlane(9, 0, 0, 0, 1)["semantics"] == "orientation_only");
        Check(CrossingPlane(0x2E, 0, 0, 0, 1)["positive_room"] == 1);
        Check(Steering(0, 100, 0) == std::pair<int8_t, int8_t>{ 0, 80 });
        Check(Steering(100, 0, 0) == std::pair<int8_t, int8_t>{ -80, 0 });
        Check(Steering(100, 0, 90) == std::pair<int8_t, int8_t>{ 0, 80 });
        Check(Steering(0, 100, 90) == std::pair<int8_t, int8_t>{ 80, 0 });
        Mailbox mailbox;
        Check(ParseRequest(R"({"id":"v2","op":"camera_state"})", request, error));
        auto ticket = mailbox.Submit(request);
        Check(mailbox.Result(ticket)["status"] == "pending");
        bool busy = false;
        try {
            mailbox.Submit(request);
        } catch (const std::exception&) { busy = true; }
        Check(busy);
        mailbox.Tick([](Request&) -> std::optional<Json> { return std::nullopt; });
        Check(mailbox.Busy());
        mailbox.Tick([](Request&) -> std::optional<Json> { return Json{ { "status", "applied" } }; });
        Check(mailbox.Result(ticket)["result"]["status"] == "applied");
        mailbox.Cancel();
        bool expired = false;
        try {
            mailbox.Result(ticket);
        } catch (const std::exception&) { expired = true; }
        Check(expired && mailbox.TakeCleanup() && !mailbox.TakeCleanup());
        ticket = mailbox.Submit(request);
        mailbox.Tick([](Request&) -> std::optional<Json> { throw std::runtime_error("falha controlada"); });
        Check(mailbox.Result(ticket)["result"]["status"] == "error" && mailbox.TakeCleanup());
        std::cout << "protocolo v1/v2, geometria, steering, fila, lease e mailbox: OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
