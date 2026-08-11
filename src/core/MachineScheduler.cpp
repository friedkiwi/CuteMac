#include "cutemac/core/MachineScheduler.h"

namespace cutemac::core {

std::uint64_t MachineScheduler::now() const { return m_now; }

void MachineScheduler::reset()
{
    m_now = 0;
    m_nextSequence = 0;
    m_events = {};
}

void MachineScheduler::schedule(std::uint64_t cycle, Callback callback, const char* label)
{
    m_events.push({ cycle, m_nextSequence++, std::move(callback), label });
}

QStringList MachineScheduler::pendingEvents() const
{
    auto events = m_events;
    QStringList lines;
    lines.reserve(static_cast<qsizetype>(events.size()));
    while (!events.empty()) {
        const auto& event = events.top();
        lines.append(QStringLiteral("cycle=%1 in=%2 seq=%3 label=%4")
                         .arg(event.cycle)
                         .arg(event.cycle >= m_now ? event.cycle - m_now : 0)
                         .arg(event.sequence)
                         .arg(event.label != nullptr ? QString::fromLatin1(event.label)
                                                     : QStringLiteral("<unlabelled>")));
        events.pop();
    }
    return lines;
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
