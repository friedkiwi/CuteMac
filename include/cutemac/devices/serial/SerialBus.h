#pragma once

#include <cstdint>
#include <memory>
#include <functional>

#include "cutemac/devices/serial/SerialEndpoint.h"

namespace cutemac::devices::serial {

class SerialBus {
public:
    using ReceiveHandler = std::function<void(std::uint8_t)>;
    void setReceiveHandler(ReceiveHandler handler) { m_receiveHandler = std::move(handler); }
    void attach(std::shared_ptr<SerialEndpoint> endpoint) {
        m_endpoint = std::move(endpoint);
        if (m_endpoint) m_endpoint->setTransmitHandler(m_receiveHandler);
    }
    void detach() { if (m_endpoint) m_endpoint->setTransmitHandler({}); m_endpoint.reset(); }
    void reset() { if (m_endpoint) m_endpoint->reset(); }
    void transmit(std::uint8_t value) { if (m_endpoint) m_endpoint->receiveByte(value); }
    [[nodiscard]] bool attached() const { return static_cast<bool>(m_endpoint); }

private:
    std::shared_ptr<SerialEndpoint> m_endpoint;
    ReceiveHandler m_receiveHandler;
};

} // namespace cutemac::devices::serial
