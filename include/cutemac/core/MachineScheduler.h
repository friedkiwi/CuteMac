#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

namespace cutemac::core {

class MachineScheduler {
public:
    using Callback = std::function<void()>;

    [[nodiscard]] std::uint64_t now() const;
    void reset();
    void schedule(std::uint64_t cycle, Callback callback);
    void advance(std::uint64_t cycles);
    void dispatchDue();

private:
    struct Event {
        std::uint64_t cycle = 0;
        std::uint64_t sequence = 0;
        Callback callback;
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
