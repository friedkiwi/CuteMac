#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <memory>
#include <thread>

#include <QByteArray>

#include "cutemac/config/Configuration.h"
#include "cutemac/devices/serial/SerialEndpoint.h"

namespace cutemac::devices::modem {

class NullModem final : public serial::SerialEndpoint {
public:
    explicit NullModem(config::SerialDeviceConfiguration configuration);
    ~NullModem() override;

    void reset() override;
    void poll() override;
    void receiveByte(std::uint8_t value) override;

private:
    void run();
    void enqueueIncoming(const QByteArray& bytes);

    config::SerialDeviceConfiguration m_configuration;
    std::atomic_bool m_stop = false;
    std::thread m_thread;
    std::mutex m_outgoingMutex;
    std::condition_variable m_outgoingCv;
    std::deque<std::uint8_t> m_outgoing;
    std::mutex m_incomingMutex;
    std::deque<std::uint8_t> m_incoming;
};

} // namespace cutemac::devices::modem
