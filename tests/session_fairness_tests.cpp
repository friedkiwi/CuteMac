#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "cutemac/config/Configuration.h"
#include "cutemac/core/EmulationSession.h"

int main()
{
    auto configuration = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
    cutemac::core::EmulationSession session(configuration);
    std::atomic<bool> running { true };
    std::thread worker([&]() {
        while (running.load()) {
            (void)session.runCycles(1);
            session.serviceHostRequests();
        }
    });

    auto slowest = std::chrono::steady_clock::duration::zero();
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const auto start = std::chrono::steady_clock::now();
        const auto status = session.status();
        (void)session.videoFrame();
        (void)session.takeAudioFrame();
        (void)session.mediaState();
        slowest = std::max(slowest, std::chrono::steady_clock::now() - start);
        if (status.machineId != QStringLiteral("mac-plus")) {
            running = false;
            worker.join();
            std::cerr << "session status returned the wrong machine\n";
            return 1;
        }
    }
    running = false;
    worker.join();
    if (slowest > std::chrono::seconds(1)) {
        std::cerr << "host waited over one second for the session mutex\n";
        return 1;
    }
    return 0;
}
