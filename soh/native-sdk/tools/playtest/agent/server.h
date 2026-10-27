#pragma once
#include "protocol.h"
#include "oot_hooks.h"
#include <deque>
#include <memory>
#include <mutex>

namespace Playtest {
struct Queued {
    size_t queued;
    uint64_t start_frame;
    uint64_t end_frame;
};
class InputQueue {
  public:
    Queued Push(const std::vector<Step>& steps);
    void Cancel();
    void Apply(uint64_t frame, ShipOotInputPadV1& pad);
    size_t Size() const;

  private:
    struct Entry {
        Step step;
        uint64_t start;
        uint64_t end;
    };
    mutable std::mutex mutex_;
    std::deque<Entry> steps_;
    uint64_t frame_ = 0;
    bool neutral_ = false;
    bool ready_ = false;
};
// O dono é a conexão, nunca o nome escolhido pelo cliente.
class Lease {
  public:
    bool Acquire(uint64_t connection, bool force);
    bool Owns(uint64_t connection) const {
        return owner_ != 0 && owner_ == connection;
    }
    bool Release(uint64_t connection);
    uint64_t Owner() const {
        return owner_;
    }

  private:
    uint64_t owner_ = 0;
};
struct Snapshot {
    Json fields = { { "frame", 0 },          { "game_state", "other" }, { "queue_size", 0 },
                    { "sail", false },       { "scene", nullptr },      { "room", nullptr },
                    { "entrance", nullptr }, { "position", nullptr },   { "rotation", nullptr },
                    { "age", nullptr },      { "health", nullptr },     { "max_health", nullptr },
                    { "magic", nullptr },    { "rupees", nullptr },     { "pause", nullptr },
                    { "textbox", nullptr } };
};
class Server {
  public:
    explicit Server(std::string layout);
    ~Server();
    bool Start(uint16_t port, const std::string& token);
    void Stop();
    void Publish(Snapshot snapshot);
    InputQueue& Queue();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Playtest
