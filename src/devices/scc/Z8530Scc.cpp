#include "cutemac/devices/scc/Z8530Scc.h"

#include <algorithm>

namespace cutemac::devices::scc {

Z8530Scc::Z8530Scc()
{
    m_busA.setReceiveHandler([this](std::uint8_t value) { receiveByte(Channel::A, value); });
    m_busB.setReceiveHandler([this](std::uint8_t value) { receiveByte(Channel::B, value); });
}

void Z8530Scc::reset()
{
    m_channelA = {};
    m_channelB = {};
    m_busA.reset();
    m_busB.reset();
}

std::uint8_t Z8530Scc::readControl(Channel channel)
{
    auto& channelState = state(channel);
    const auto selectedRegister = channelState.selectedRegister;
    channelState.selectedRegister = 0;

    if (selectedRegister == 0) return static_cast<std::uint8_t>((channelState.transmitCycles == 0 ? 0x04 : 0x00)
        | (channelState.receiveDataAvailable ? 0x01 : 0x00));
    // RR1 bit 0 is All Sent.  The Power Macintosh ROM selects RR1 and polls
    // this bit while programming each SCC channel; leaving RR1 stubbed at zero
    // strands early hardware initialization before video bring-up.
    if (selectedRegister == 1)
        return channelState.transmitCycles == 0 ? 0x01U : 0x00U;
    if (selectedRegister == 3 && channel == Channel::A) {
        return static_cast<std::uint8_t>((m_channelA.transmitInterruptPending ? 0x10 : 0)
            | (m_channelB.transmitInterruptPending ? 0x02 : 0));
    }

    return 0;
}

std::uint8_t Z8530Scc::readData(Channel channel)
{
    auto& channelState = state(channel);
    channelState.receiveDataAvailable = false;
    return channelState.data;
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
    channelState.transmitData = value;
    channelState.transmitCycles = 16;
    channelState.transmitInterruptPending = false;
    if (channelState.writeRegisters[14] & 0x10U) channelState.receiveDataAvailable = true;
}

void Z8530Scc::tick(int cycles)
{
    for (auto* channel : {&m_channelA, &m_channelB}) {
        if (channel->transmitCycles <= 0) continue;
        channel->transmitCycles = std::max(0, channel->transmitCycles - cycles);
        if (channel->transmitCycles == 0) {
            channel->transmitInterruptPending = true;
            (channel == &m_channelA ? m_busA : m_busB).transmit(channel->transmitData);
        }
    }
}

void Z8530Scc::attachEndpoint(Channel channel, std::shared_ptr<serial::SerialEndpoint> endpoint)
{
    (channel == Channel::A ? m_busA : m_busB).attach(std::move(endpoint));
}

void Z8530Scc::receiveByte(Channel channel, std::uint8_t value)
{
    auto& channelState = state(channel);
    channelState.data = value;
    channelState.receiveDataAvailable = true;
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
