#pragma once

#include <array>
#include <cstdint>

namespace cutemac::devices::scc {

class Z8530Scc {
public:
    enum class Channel {
        A,
        B,
    };

    void reset();

    [[nodiscard]] std::uint8_t readControl(Channel channel);
    [[nodiscard]] std::uint8_t readData(Channel channel) const;

    void writeControl(Channel channel, std::uint8_t value);
    void writeData(Channel channel, std::uint8_t value);

private:
    struct ChannelState {
        std::array<std::uint8_t, 16> writeRegisters {};
        std::uint8_t selectedRegister = 0;
        bool awaitingRegisterWrite = false;
        std::uint8_t data = 0;
    };

    [[nodiscard]] ChannelState& state(Channel channel);
    [[nodiscard]] const ChannelState& state(Channel channel) const;

    ChannelState m_channelA;
    ChannelState m_channelB;
};

} // namespace cutemac::devices::scc
