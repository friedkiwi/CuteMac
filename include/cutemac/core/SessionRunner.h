#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <QtGlobal>

#if !defined(Q_OS_WASM)
#include <thread>
#endif

#include "cutemac/core/EmulationSession.h"
#include "cutemac/config/Configuration.h"

namespace cutemac::core {

class SessionRunner {
public:
    SessionRunner(EmulationSession& session, int cyclesPerFrame, config::RuntimeSpeed speed);
    ~SessionRunner();

    void start();
    void stop();
    void setPaused(bool paused);
    void setCyclesPerFrame(int cyclesPerFrame);
    void setSpeed(config::RuntimeSpeed speed);
    void setInteractiveInputActive(bool active);
    void setAudioPlaybackActive(bool active);
    [[nodiscard]] config::RuntimeSpeed speed() const;
    void runHostFrame();

private:
#if !defined(Q_OS_WASM)
    void workerLoop();
#endif

    EmulationSession& m_session;
    std::atomic_int m_cyclesPerFrame = 130560;
    std::atomic<config::RuntimeSpeed> m_speed = config::RuntimeSpeed::Realtime;
    std::atomic_bool m_interactiveInputActive = false;
    std::atomic_bool m_audioPlaybackActive = false;
    std::atomic_bool m_machineAudioPlaybackActive = false;
    std::atomic_bool m_running = false;
    std::atomic_bool m_paused = true;
#if !defined(Q_OS_WASM)
    std::thread m_worker;
    std::mutex m_waitMutex;
    std::condition_variable m_wake;
#endif
};

} // namespace cutemac::core
