#include "cutemac/core/MachineScheduler.h"

namespace cutemac::core {

std::uint64_t MachineScheduler::now() const { return m_now; }

void MachineScheduler::reset()
{
    m_now = 0;
    m_nextSequence = 0;
    m_events = {};
}

void MachineScheduler::schedule(std::uint64_t cycle, Callback callback)
{
    m_events.push({ cycle, m_nextSequence++, std::move(callback) });
}

void MachineScheduler::advance(std::uint64_t cycles)
{
    m_now += cycles;
    dispatchDue();
}

void MachineScheduler::dispatchDue()
{
    while (!m_events.empty() && m_events.top().cycle <= m_now) {
        auto event = m_events.top();
        m_events.pop();
        event.callback();
    }
}

} // namespace cutemac::core
