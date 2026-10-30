#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include "server.h"
#include <atomic>
#include <thread>
#include <vector>

namespace Playtest {
struct Server::Impl {
    struct Client {
        SOCKET socket;
        uint64_t id;
        std::string incoming;
        std::string outgoing;
    };
    std::mutex mutex;
    Snapshot snapshot;
    InputQueue queue;
    Mailbox commands;
    Lease lease;
    std::string layout, token;
    SOCKET listener = INVALID_SOCKET;
    std::atomic<bool> stopping{ false };
    std::thread thread;
    bool wsa = false;
    std::string Handle(uint64_t connection, const std::string& line) {
        Request request;
        std::string error;
        if (!ParseRequest(line, request, error))
            return ReplyError(request.id, error);
        std::lock_guard lock(mutex);
        const auto& a = request.args;
        const auto& id = request.id;
        if (request.op == "hello") {
            if (!a.contains("token") || !a["token"].is_string() || a["token"] != token)
                return ReplyError(id, "token inválido");
            if (!a.contains("client") || !a["client"].is_string() || a["client"].get<std::string>().empty() ||
                (a.contains("force") && !a["force"].is_boolean()))
                return ReplyError(id, "hello inválido");
            if (a.contains("protocol_version") &&
                (!a["protocol_version"].is_number_integer() || a["protocol_version"] != ProtocolVersion))
                return ReplyError(id, "protocolo incompatível: agente exige protocol_version=2");
            const bool previous = lease.Owner() != 0 && !lease.Owns(connection);
            if (!lease.Acquire(connection, a.value("force", false)))
                return ReplyError(id, "busy");
            if (previous) {
                queue.Cancel();
                commands.Cancel();
            }
            return Reply(id, { { "lease", std::to_string(connection) },
                               { "agent_version", "0.2.0" },
                               { "layout_id", layout },
                               { "protocol_version", ProtocolVersion },
                               { "capabilities",
                                 { "scene_map", "actors", "teleport", "camera_state", "camera_set", "walk_to" } } });
        }
        if (!lease.Owns(connection))
            return ReplyError(id, "lease necessário");
        if (request.op == "state") {
            auto state = snapshot.fields;
            state["queue_size"] = queue.Size();
            return Reply(id, state);
        }
        if (request.op == "cancel" || request.op == "release") {
            queue.Cancel();
            commands.Cancel();
            if (request.op == "release")
                lease.Release(connection);
            return Reply(id, { { "cancelled", true } });
        }
        if (request.op != "input") {
            try {
                if (request.op == "result")
                    return Reply(id, commands.Result(a["ticket"].get<uint64_t>()));
                const auto ticket = commands.Submit(request);
                return Reply(id, { { "ticket", ticket }, { "status", "pending" } });
            } catch (const std::exception& e) { return ReplyError(id, e.what()); }
        }
        if (commands.Busy())
            return ReplyError(id, "busy: operação v2 em andamento");
        std::vector<Step> steps;
        if (!a.contains("steps") || !ParseSteps(a["steps"], steps, error))
            return ReplyError(id, error.empty() ? "steps ausente" : error);
        try {
            const auto result = queue.Push(steps);
            return Reply(id, { { "queued", result.queued },
                               { "start_frame", result.start_frame },
                               { "end_frame", result.end_frame } });
        } catch (const std::exception& e) { return ReplyError(id, e.what()); }
    }
    void Disconnected(uint64_t connection) {
        std::lock_guard lock(mutex);
        if (lease.Release(connection)) {
            queue.Cancel();
            commands.Cancel();
        }
    }
    void Run() {
        std::vector<Client> clients;
        uint64_t sequence = 0;
        while (!stopping) {
            fd_set reads, writes;
            FD_ZERO(&reads);
            FD_ZERO(&writes);
            FD_SET(listener, &reads);
            for (const auto& client : clients) {
                FD_SET(client.socket, &reads);
                if (!client.outgoing.empty())
                    FD_SET(client.socket, &writes);
            }
            timeval timeout{ 0, 100000 };
            if (select(0, &reads, &writes, nullptr, &timeout) == SOCKET_ERROR)
                break;
            if (FD_ISSET(listener, &reads)) {
                SOCKET socket = accept(listener, nullptr, nullptr);
                if (socket != INVALID_SOCKET) {
                    if (clients.size() >= 16)
                        closesocket(socket);
                    else {
                        u_long nonblocking = 1;
                        ioctlsocket(socket, FIONBIO, &nonblocking);
                        clients.push_back({ socket, ++sequence, {}, {} });
                    }
                }
            }
            for (size_t i = 0; i < clients.size();) {
                auto& client = clients[i];
                bool alive = true;
                if (FD_ISSET(client.socket, &reads)) {
                    char buffer[8192];
                    const int count = recv(client.socket, buffer, sizeof(buffer), 0);
                    if (count <= 0)
                        alive = count < 0 && WSAGetLastError() == WSAEWOULDBLOCK;
                    else {
                        client.incoming.append(buffer, count);
                        size_t pos;
                        while ((pos = client.incoming.find('\n')) != std::string::npos) {
                            client.outgoing += Handle(client.id, client.incoming.substr(0, pos));
                            client.incoming.erase(0, pos + 1);
                            if (client.outgoing.size() > MaxLine * 4) {
                                alive = false;
                                break;
                            }
                        }
                        if (client.incoming.size() > MaxLine)
                            alive = false;
                    }
                }
                if (alive && FD_ISSET(client.socket, &writes)) {
                    const int sent =
                        send(client.socket, client.outgoing.data(), static_cast<int>(client.outgoing.size()), 0);
                    if (sent > 0)
                        client.outgoing.erase(0, sent);
                    else if (sent == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
                        alive = false;
                }
                if (alive)
                    ++i;
                else {
                    Disconnected(client.id);
                    closesocket(client.socket);
                    clients.erase(clients.begin() + i);
                }
            }
        }
        for (const auto& client : clients) {
            Disconnected(client.id);
            closesocket(client.socket);
        }
    }
};
Server::Server(std::string layout) : impl_(std::make_unique<Impl>()) {
    impl_->layout = std::move(layout);
}
Server::~Server() {
    Stop();
}
bool Server::Start(uint16_t port, const std::string& token) {
    if (!port || token.empty() || impl_->thread.joinable())
        return false;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return false;
    impl_->wsa = true;
    impl_->token = token;
    impl_->stopping = false;
    impl_->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->listener == INVALID_SOCKET) {
        Stop();
        return false;
    }
    BOOL exclusive = TRUE;
    setsockopt(impl_->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
               sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(impl_->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(impl_->listener, 16)) {
        Stop();
        return false;
    }
    u_long nonblocking = 1;
    ioctlsocket(impl_->listener, FIONBIO, &nonblocking);
    try {
        impl_->thread = std::thread([this] { impl_->Run(); });
    } catch (...) {
        Stop();
        return false;
    }
    return true;
}
void Server::Stop() {
    impl_->stopping = true;
    if (impl_->thread.joinable())
        impl_->thread.join();
    if (impl_->listener != INVALID_SOCKET) {
        closesocket(impl_->listener);
        impl_->listener = INVALID_SOCKET;
    }
    if (impl_->wsa) {
        WSACleanup();
        impl_->wsa = false;
    }
    impl_->token.clear();
    impl_->queue.Cancel();
    impl_->commands.Cancel();
}
void Server::Publish(Snapshot snapshot) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshot = std::move(snapshot);
}
InputQueue& Server::Queue() {
    return impl_->queue;
}
Mailbox& Server::Commands() {
    return impl_->commands;
}
} // namespace Playtest
