#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <QtGlobal>

#if !defined(Q_OS_WASM)
#include <thread>
#endif

#include "cutemac/core/EmulationSession.h"

namespace cutemac::core {

class SessionRunner {
public:
    SessionRunner(EmulationSession& session, int cyclesPerFrame);
    ~SessionRunner();

    void start();
    void stop();
    void setPaused(bool paused);
    void runHostFrame();

private:
#if !defined(Q_OS_WASM)
    void workerLoop();
#endif

    EmulationSession& m_session;
    int m_cyclesPerFrame = 130560;
    std::atomic_bool m_running = false;
    std::atomic_bool m_paused = true;
#if !defined(Q_OS_WASM)
    std::thread m_worker;
    std::mutex m_waitMutex;
    std::condition_variable m_wake;
#endif
};

} // namespace cutemac::core
