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
    if (frame == steps_.front().end)
        steps_.pop_front();
}
size_t InputQueue::Size() const {
    std::lock_guard lock(mutex_);
    return steps_.size();
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
} // namespace Playtest
