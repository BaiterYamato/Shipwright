#pragma once
#include "protocol.h"
#include "oot_hooks.h"
#include <deque>
#include <memory>
#include <mutex>
#include <functional>
#include <optional>

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
// Uma operação por vez. Socket só publica pedidos; o jogo executa e copia JSON.
// Cancelar, trocar dono ou desconectar invalida também resultados da geração anterior.
class Mailbox {
  public:
    uint64_t Submit(Request request);
    Json Result(uint64_t ticket) const;
    void Tick(const std::function<std::optional<Json>(Request&)>& handler);
    // Consome a limpeza pendente e executa o pedido sob o mesmo mutex: um Cancel+Submit entre as duas etapas não
    // pode fazer a limpeza antiga desfazer o pedido novo (por exemplo, soltar a câmera recém-aplicada).
    void Tick(const std::function<void()>& cleanup, const std::function<std::optional<Json>(Request&)>& handler);
    void Cancel();
    bool Busy() const;
    bool TakeCleanup();

  private:
    void RunLocked(const std::function<std::optional<Json>(Request&)>& handler);
    mutable std::mutex mutex_;
    uint64_t sequence_ = 0, ticket_ = 0;
    std::optional<Request> pending_;
    Json result_;
    bool cleanup_ = false;
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
    Mailbox& Commands();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Playtest
