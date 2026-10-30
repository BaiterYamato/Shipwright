#include "server.h"
#include <stdexcept>

namespace Playtest {
Queued InputQueue::Push(const std::vector<Step>& steps) {
    std::lock_guard lock(mutex_);
    if (!ready_)
        throw std::runtime_error("aguarde o primeiro frame do agente");
    if (steps.empty() || steps_.size() + steps.size() > 600)
        throw std::runtime_error("fila excede 600 passos");
    const auto start = steps_.empty() ? frame_ + 1 + (neutral_ ? 1 : 0) : steps_.back().end + 1;
    uint64_t next = start;
    for (const auto& step : steps) {
        if (!step.frames || step.frames > 3600)
            throw std::runtime_error("frames fora de 1..3600");
    }
    for (const auto& step : steps) {
        steps_.push_back({ step, next, next + step.frames - 1 });
        next += step.frames;
    }
    return { steps.size(), start, next - 1 };
}
void InputQueue::Cancel() {
    std::lock_guard lock(mutex_);
    steps_.clear();
    neutral_ = true;
}
void InputQueue::Apply(uint64_t frame, ShipOotInputPadV1& pad) {
    std::lock_guard lock(mutex_);
    frame_ = frame;
    ready_ = true;
    if (neutral_) {
        pad.button = 0;
        pad.stick_x = pad.stick_y = pad.right_stick_x = pad.right_stick_y = 0;
        neutral_ = false;
        return;
    }
    while (!steps_.empty() && steps_.front().end < frame)
        steps_.pop_front();
    if (steps_.empty() || frame < steps_.front().start)
        return;
    const auto& step = steps_.front().step;
    pad.button = step.merge ? (pad.button | step.buttons) : step.buttons;
    if (!step.merge || step.stick[0])
        pad.stick_x = step.stick[0];
    if (!step.merge || step.stick[1])
        pad.stick_y = step.stick[1];
    if (!step.merge || step.cstick[0])
        pad.right_stick_x = step.cstick[0];
    if (!step.merge || step.cstick[1])
        pad.right_stick_y = step.cstick[1];
    appliedFrame_ = frame;
    if (frame == steps_.front().end)
        steps_.pop_front();
}
size_t InputQueue::Size() const {
    std::lock_guard lock(mutex_);
    return steps_.size();
}
bool InputQueue::IdleAt(uint64_t frame) const {
    std::lock_guard lock(mutex_);
    return steps_.empty() && appliedFrame_ != frame;
}
bool Lease::Acquire(uint64_t connection, bool force) {
    if (!connection || (owner_ && owner_ != connection && !force))
        return false;
    owner_ = connection;
    return true;
}
bool Lease::Release(uint64_t connection) {
    if (!Owns(connection))
        return false;
    owner_ = 0;
    return true;
}
uint64_t Mailbox::Submit(Request request) {
    std::lock_guard lock(mutex_);
    if (pending_)
        throw std::runtime_error("busy: operação v2 em andamento");
    ticket_ = ++sequence_;
    pending_ = std::move(request);
    result_ = nullptr;
    return ticket_;
}
Json Mailbox::Result(uint64_t ticket) const {
    std::lock_guard lock(mutex_);
    if (!ticket_ || ticket != ticket_)
        throw std::runtime_error("ticket expirado ou cancelado");
    return { { "ticket", ticket }, { "status", pending_ ? "pending" : "done" }, { "result", result_ } };
}
void Mailbox::Tick(const std::function<std::optional<Json>(Request&)>& handler) {
    std::lock_guard lock(mutex_);
    RunLocked(handler);
}
void Mailbox::Tick(const std::function<void()>& cleanup, const std::function<std::optional<Json>(Request&)>& handler) {
    std::lock_guard lock(mutex_);
    if (std::exchange(cleanup_, false))
        cleanup();
    RunLocked(handler);
}
void Mailbox::RunLocked(const std::function<std::optional<Json>(Request&)>& handler) {
    if (!pending_)
        return;
    try {
        auto result = handler(*pending_);
        if (!result)
            return;
        result_ = std::move(*result);
    } catch (const std::exception& e) {
        result_ = { { "status", "error" }, { "error", e.what() } };
        cleanup_ = true;
    }
    pending_.reset();
}
void Mailbox::Cancel() {
    std::lock_guard lock(mutex_);
    pending_.reset();
    result_ = nullptr;
    ticket_ = 0;
    cleanup_ = true;
}
bool Mailbox::Busy() const {
    std::lock_guard lock(mutex_);
    return pending_.has_value();
}
bool Mailbox::TakeCleanup() {
    std::lock_guard lock(mutex_);
    return std::exchange(cleanup_, false);
}
} // namespace Playtest
