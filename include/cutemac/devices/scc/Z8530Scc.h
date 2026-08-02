#pragma once

#include <array>
#include <cstdint>
#include "cutemac/devices/serial/SerialBus.h"

namespace cutemac::devices::scc {

class Z8530Scc {
public:
    enum class Channel {
        A,
        B,
    };

    Z8530Scc();

    void reset();

    [[nodiscard]] std::uint8_t readControl(Channel channel);
    [[nodiscard]] std::uint8_t readData(Channel channel);

    void writeControl(Channel channel, std::uint8_t value);
    void writeData(Channel channel, std::uint8_t value);
    void tick(int cycles);
    [[nodiscard]] bool interruptActive() const;
    void attachEndpoint(Channel channel, std::shared_ptr<serial::SerialEndpoint> endpoint);
    void receiveByte(Channel channel, std::uint8_t value);

private:
    struct ChannelState {
        std::array<std::uint8_t, 16> writeRegisters {};
        std::uint8_t selectedRegister = 0;
        bool awaitingRegisterWrite = false;
        std::uint8_t data = 0;
        int transmitCycles = 0;
        bool transmitInterruptPending = false;
        bool receiveDataAvailable = false;
        std::uint8_t transmitData = 0;
    };

    [[nodiscard]] ChannelState& state(Channel channel);
    [[nodiscard]] const ChannelState& state(Channel channel) const;

    ChannelState m_channelA;
    ChannelState m_channelB;
    serial::SerialBus m_busA;
    serial::SerialBus m_busB;
};

} // namespace cutemac::devices::scc
