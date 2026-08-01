#include "cutemac/devices/scc/Z8530Scc.h"

namespace cutemac::devices::scc {

void Z8530Scc::reset()
{
    m_channelA = {};
    m_channelB = {};
}

std::uint8_t Z8530Scc::readControl(Channel channel)
{
    auto& channelState = state(channel);
    const auto selectedRegister = channelState.selectedRegister;
    channelState.selectedRegister = 0;

    if (selectedRegister == 0) {
        return 0x04; // RR0: transmit buffer empty, no receive data pending.
    }

    return 0;
}

std::uint8_t Z8530Scc::readData(Channel channel) const
{
    return state(channel).data;
}

void Z8530Scc::writeControl(Channel channel, std::uint8_t value)
{
    auto& channelState = state(channel);
    if (channelState.awaitingRegisterWrite) {
        channelState.writeRegisters[channelState.selectedRegister & 0x0f] = value;
        channelState.awaitingRegisterWrite = false;
        channelState.selectedRegister = 0;
        return;
    }

    channelState.selectedRegister = static_cast<std::uint8_t>(value & 0x0f);
    channelState.awaitingRegisterWrite = channelState.selectedRegister != 0;
}

void Z8530Scc::writeData(Channel channel, std::uint8_t value)
{
    state(channel).data = value;
}

Z8530Scc::ChannelState& Z8530Scc::state(Channel channel)
{
    return channel == Channel::A ? m_channelA : m_channelB;
}

const Z8530Scc::ChannelState& Z8530Scc::state(Channel channel) const
{
    return channel == Channel::A ? m_channelA : m_channelB;
}

} // namespace cutemac::devices::scc
