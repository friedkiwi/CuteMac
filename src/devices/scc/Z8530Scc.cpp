#include "cutemac/devices/scc/Z8530Scc.h"

#include <algorithm>

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

    if (selectedRegister == 0) return channelState.transmitCycles == 0 ? 0x04 : 0x00;
    if (selectedRegister == 3 && channel == Channel::A) {
        return static_cast<std::uint8_t>((m_channelA.transmitInterruptPending ? 0x10 : 0)
            | (m_channelB.transmitInterruptPending ? 0x02 : 0));
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

    const auto command = static_cast<std::uint8_t>((value >> 3) & 7);
    if (command == 5) channelState.transmitInterruptPending = false;
    channelState.selectedRegister = static_cast<std::uint8_t>(value & 0x0f);
    channelState.awaitingRegisterWrite = channelState.selectedRegister != 0;
}

void Z8530Scc::writeData(Channel channel, std::uint8_t value)
{
    auto& channelState = state(channel);
    channelState.data = value;
    channelState.transmitCycles = 16;
    channelState.transmitInterruptPending = false;
}

void Z8530Scc::tick(int cycles)
{
    for (auto* channel : {&m_channelA, &m_channelB}) {
        if (channel->transmitCycles <= 0) continue;
        channel->transmitCycles = std::max(0, channel->transmitCycles - cycles);
        if (channel->transmitCycles == 0) channel->transmitInterruptPending = true;
    }
}

bool Z8530Scc::interruptActive() const
{
    const bool masterEnabled = ((m_channelA.writeRegisters[9] | m_channelB.writeRegisters[9]) & 0x08) != 0;
    const auto enabled = [](const ChannelState& channel) {
        return channel.transmitInterruptPending && (channel.writeRegisters[1] & 0x02) != 0;
    };
    return masterEnabled && (enabled(m_channelA) || enabled(m_channelB));
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
