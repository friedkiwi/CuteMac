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
    // An unattached synchronous receiver is hunting for a sync character.
    // LocalTalk's collision/backoff code samples RR0 bit 4 before attempting
    // a frame; reporting an acquired sync on an idle wire strands the LAP
    // request indefinitely.
    m_channelB.status = 0x10;
    m_busA.reset();
    m_busB.reset();
}

std::uint8_t Z8530Scc::readControl(Channel channel)
{
    auto& channelState = state(channel);
    const auto selectedRegister = channelState.selectedRegister;
    channelState.selectedRegister = 0;
    channelState.awaitingRegisterWrite = false;

    if (selectedRegister == 0) return static_cast<std::uint8_t>(channelState.status | (channelState.transmitCycles == 0 ? 0x44 : 0x00)
        | (channelState.zeroCount ? 0x02 : 0x00) | (channelState.receiveDataAvailable ? 0x01 : 0x00));
    // RR1 bit 0 is All Sent.  The Power Macintosh ROM selects RR1 and polls
    // this bit while programming each SCC channel; leaving RR1 stubbed at zero
    // strands early hardware initialization before video bring-up.
    if (selectedRegister == 1)
        return channelState.transmitCycles == 0 ? 0x01U : 0x00U;
    if (selectedRegister == 2 && channel == Channel::B) return modifiedInterruptVector();
    if (selectedRegister == 15) return channelState.writeRegisters[15];
    if (selectedRegister == 3 && channel == Channel::A)
        return static_cast<std::uint8_t>((receiveInterruptEnabled(m_channelA) ? 0x20 : 0)
            | (transmitInterruptEnabled(m_channelA) ? 0x10 : 0)
            | (externalInterruptEnabled(m_channelA) ? 0x08 : 0)
            | (receiveInterruptEnabled(m_channelB) ? 0x04 : 0)
            | (transmitInterruptEnabled(m_channelB) ? 0x02 : 0)
            | (externalInterruptEnabled(m_channelB) ? 0x01 : 0));

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
        const auto reg = static_cast<std::uint8_t>(channelState.selectedRegister & 0x0f);
        channelState.writeRegisters[reg] = value;
        channelState.awaitingRegisterWrite = false;
        channelState.selectedRegister = 0;
        if (reg == 12 || reg == 13 || reg == 14) restartBaudRateTimer(channelState);
        return;
    }

    const auto command = static_cast<std::uint8_t>((value >> 3) & 7);
    const auto lowRegister = static_cast<std::uint8_t>(value & 7);
    if (command == 1) {
        channelState.selectedRegister = static_cast<std::uint8_t>(8 + lowRegister);
        channelState.awaitingRegisterWrite = true;
        return;
    }
    if (command == 0 && lowRegister != 0) {
        channelState.selectedRegister = lowRegister;
        channelState.awaitingRegisterWrite = true;
        return;
    }

    // WR0 commands do not begin a register-data pair. In particular, a
    // Reset Transmit Interrupt Pending command (0x28) must not consume the
    // following control write as bogus WR8 data.
    if (command == 2) {
        channelState.externalStatusPending = false;
        channelState.zeroCount = false;
    } else if (command == 5) {
        channelState.transmitInterruptPending = false;
    } else if (command == 7) {
        resetHighestInterrupt();
    }
    channelState.selectedRegister = 0;
    channelState.awaitingRegisterWrite = false;
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
        if (channel->transmitCycles > 0) {
            channel->transmitCycles = std::max(0, channel->transmitCycles - cycles);
            if (channel->transmitCycles == 0) {
                channel->transmitInterruptPending = true;
                // SerialEndpoint represents an asynchronous byte-stream
                // peripheral. In synchronous modes (notably LocalTalk's
                // WR4=0x20 SDLC mode), the same SCC data register feeds the
                // synchronous encoder and must not leak packet bytes into an
                // attached serial printer. A future LocalTalk attachment will
                // use a framed synchronous boundary instead.
                if ((channel->writeRegisters[4] & 0x0cU) != 0)
                    (channel == &m_channelA ? m_busA : m_busB).transmit(channel->transmitData);
            }
        }
        if (channel->baudRateCycles > 0) {
            channel->baudRateCycles -= cycles;
            if (channel->baudRateCycles <= 0) {
                if ((channel->writeRegisters[15] & 0x02) != 0) channel->zeroCount = true;
                channel->externalStatusPending = (channel->writeRegisters[15] & 0x02) != 0;
                channel->baudRateCycles = 0;
            }
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
    return masterEnabled && modifiedInterruptVector() != 0xff;
}

Z8530Scc::DebugChannelState Z8530Scc::debugState(Channel channel) const
{
    const auto& s = state(channel);
    return {s.writeRegisters, s.selectedRegister, s.transmitCycles, s.baudRateCycles,
        s.transmitInterruptPending, s.receiveDataAvailable, s.externalStatusPending, s.zeroCount};
}

bool Z8530Scc::receiveInterruptEnabled(const ChannelState& channel) const
{
    return channel.receiveDataAvailable && (channel.writeRegisters[1] & 0x18) != 0;
}

bool Z8530Scc::transmitInterruptEnabled(const ChannelState& channel) const
{
    return channel.transmitInterruptPending && (channel.writeRegisters[1] & 0x02) != 0;
}

bool Z8530Scc::externalInterruptEnabled(const ChannelState& channel) const
{
    return channel.externalStatusPending && (channel.writeRegisters[1] & 0x01) != 0;
}

std::uint8_t Z8530Scc::modifiedInterruptVector() const
{
    if (receiveInterruptEnabled(m_channelA)) return 0x0c;
    if (transmitInterruptEnabled(m_channelA)) return 0x08;
    if (externalInterruptEnabled(m_channelA)) return 0x0a;
    if (receiveInterruptEnabled(m_channelB)) return 0x04;
    if (transmitInterruptEnabled(m_channelB)) return 0x00;
    if (externalInterruptEnabled(m_channelB)) return 0x02;
    return 0xff;
}

void Z8530Scc::restartBaudRateTimer(ChannelState& channel)
{
    if ((channel.writeRegisters[14] & 0x01) == 0) {
        channel.baudRateCycles = 0;
        return;
    }
    const auto timeConstant = static_cast<int>(channel.writeRegisters[12])
        | (static_cast<int>(channel.writeRegisters[13]) << 8);
    // Scheduling stub for an unconnected LocalTalk port. Preserve ordering and
    // time-constant scaling; a future LocalTalk endpoint can supply line clocks.
    channel.baudRateCycles = std::max(1024, (timeConstant + 2) * 1024);
}

void Z8530Scc::resetHighestInterrupt()
{
    for (auto* channel : {&m_channelA, &m_channelB}) {
        if (receiveInterruptEnabled(*channel)) { channel->receiveDataAvailable = false; return; }
        if (transmitInterruptEnabled(*channel)) { channel->transmitInterruptPending = false; return; }
        if (externalInterruptEnabled(*channel)) {
            channel->externalStatusPending = false;
            channel->zeroCount = false;
            return;
        }
    }
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
