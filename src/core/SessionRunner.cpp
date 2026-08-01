#include "cutemac/core/SessionRunner.h"

#include <chrono>

namespace cutemac::core {

SessionRunner::SessionRunner(EmulationSession& session, int cyclesPerFrame, config::RuntimeSpeed speed)
    : m_session(session)
    , m_cyclesPerFrame(cyclesPerFrame)
    , m_speed(speed)
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

void SessionRunner::setCyclesPerFrame(int cyclesPerFrame)
{
    m_cyclesPerFrame = cyclesPerFrame;
}

void SessionRunner::setSpeed(config::RuntimeSpeed speed)
{
    m_speed = speed;
#if !defined(Q_OS_WASM)
    m_wake.notify_all();
#endif
}

config::RuntimeSpeed SessionRunner::speed() const
{
    return m_speed.load();
}

void SessionRunner::runHostFrame()
{
#if defined(Q_OS_WASM)
    if (m_running && !m_paused) {
        const auto cycles = m_cyclesPerFrame.load();
        (void)m_session.runCycles(cycles);
        if (m_speed == config::RuntimeSpeed::Unlimited) {
            // Stay cooperative with the browser while using the remainder of this host frame.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(12);
            while (m_running && !m_paused && m_speed == config::RuntimeSpeed::Unlimited
                && std::chrono::steady_clock::now() < deadline) {
                (void)m_session.runCycles(cycles);
            }
        }
    }
#endif
}

#if !defined(Q_OS_WASM)
void SessionRunner::workerLoop()
{
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now();
    auto previousSpeed = m_speed.load();
    while (m_running) {
        if (m_paused) {
            std::unique_lock lock(m_waitMutex);
            m_wake.wait_for(lock, std::chrono::milliseconds(20), [this]() { return !m_running || !m_paused; });
            deadline = clock::now();
            continue;
        }
        const auto currentSpeed = m_speed.load();
        if (currentSpeed != previousSpeed) {
            deadline = clock::now();
            previousSpeed = currentSpeed;
        }
        (void)m_session.runCycles(m_cyclesPerFrame.load());
        if (currentSpeed == config::RuntimeSpeed::Unlimited) {
            deadline = clock::now();
            // Give the frontend a deterministic lock-acquisition window for input and display work.
            std::unique_lock lock(m_waitMutex);
            m_wake.wait_for(lock, std::chrono::microseconds(50), [this, currentSpeed]() {
                return !m_running || m_paused || m_speed.load() != currentSpeed;
            });
            continue;
        }
        deadline += std::chrono::microseconds(16625);
        std::unique_lock lock(m_waitMutex);
        m_wake.wait_until(lock, deadline, [this, currentSpeed]() {
            return !m_running || m_paused || m_speed.load() != currentSpeed;
        });
        if (deadline < clock::now() - std::chrono::milliseconds(100)) {
            deadline = clock::now();
        }
    }
}
#endif

} // namespace cutemac::core
