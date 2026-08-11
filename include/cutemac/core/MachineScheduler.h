#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

#include <QStringList>

namespace cutemac::core {

class MachineScheduler {
public:
    using Callback = std::function<void()>;

    [[nodiscard]] std::uint64_t now() const;
    void reset();
    // The label is a static string used only by debug reporting; without it a
    // pending-event dump cannot say what the queued callbacks are.
    void schedule(std::uint64_t cycle, Callback callback, const char* label = nullptr);
    void advance(std::uint64_t cycles);
    void dispatchDue();

    // Pending events in dispatch order, rendered for panic dumps and the debug
    // session. Copies the queue, so keep it off hot paths.
    [[nodiscard]] QStringList pendingEvents() const;

private:
    struct Event {
        std::uint64_t cycle = 0;
        std::uint64_t sequence = 0;
        Callback callback;
        const char* label = nullptr;
    };

    struct Later {
        bool operator()(const Event& left, const Event& right) const
        {
            return left.cycle != right.cycle ? left.cycle > right.cycle : left.sequence > right.sequence;
        }
    };

    std::priority_queue<Event, std::vector<Event>, Later> m_events;
    std::uint64_t m_now = 0;
    std::uint64_t m_nextSequence = 0;
};

} // namespace cutemac::core
