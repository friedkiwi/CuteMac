#pragma once

#include <cstdint>
#include <functional>

namespace cutemac::devices::serial {

class SerialEndpoint {
public:
    using TransmitHandler = std::function<void(std::uint8_t)>;
    virtual ~SerialEndpoint() = default;
    virtual void reset() {}
    virtual void setTransmitHandler(TransmitHandler handler) { m_transmitHandler = std::move(handler); }
    virtual void receiveByte(std::uint8_t value) = 0;

protected:
    void transmitByte(std::uint8_t value) const { if (m_transmitHandler) m_transmitHandler(value); }

private:
    TransmitHandler m_transmitHandler;
};

} // namespace cutemac::devices::serial
