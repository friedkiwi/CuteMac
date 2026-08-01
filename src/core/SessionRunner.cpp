#include "cutemac/core/SessionRunner.h"

#include <chrono>

namespace cutemac::core {

SessionRunner::SessionRunner(EmulationSession& session, int cyclesPerFrame)
    : m_session(session)
    , m_cyclesPerFrame(cyclesPerFrame)
{
}

SessionRunner::~SessionRunner() { stop(); }

void SessionRunner::start()
{
    if (m_running.exchange(true)) {
        return;
    }
#if !defined(Q_OS_WASM)
    m_worker = std::thread([this]() { workerLoop(); });
#endif
}

void SessionRunner::stop()
{
    if (!m_running.exchange(false)) {
        return;
    }
#if !defined(Q_OS_WASM)
    m_wake.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
#endif
}

void SessionRunner::setPaused(bool paused)
{
    m_paused = paused;
    m_session.setPaused(paused);
#if !defined(Q_OS_WASM)
    m_wake.notify_all();
#endif
}

void SessionRunner::runHostFrame()
{
#if defined(Q_OS_WASM)
    if (m_running && !m_paused) {
        (void)m_session.runCycles(m_cyclesPerFrame);
    }
#endif
}

#if !defined(Q_OS_WASM)
void SessionRunner::workerLoop()
{
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now();
    while (m_running) {
        if (m_paused) {
            std::unique_lock lock(m_waitMutex);
            m_wake.wait_for(lock, std::chrono::milliseconds(20), [this]() { return !m_running || !m_paused; });
            deadline = clock::now();
            continue;
        }
        (void)m_session.runCycles(m_cyclesPerFrame);
        deadline += std::chrono::microseconds(16625);
        std::this_thread::sleep_until(deadline);
        if (deadline < clock::now() - std::chrono::milliseconds(100)) {
            deadline = clock::now();
        }
    }
}
#endif

} // namespace cutemac::core
