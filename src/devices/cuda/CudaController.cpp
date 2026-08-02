#include "cutemac/devices/cuda/CudaController.h"

#include <chrono>

#include "cutemac/devices/via6522/Via6522.h"

namespace cutemac::devices::cuda {
namespace {
constexpr std::uint8_t tip = 0x20;
constexpr std::uint8_t byteAck = 0x10;
constexpr std::uint8_t treqBit = 3;
constexpr std::uint8_t pseudoPacket = 1;
constexpr std::uint32_t unixToMacintoshEpoch = 2'082'844'800U;
constexpr std::uint16_t pramMcuBase = 0x0100;
}

void CudaController::attach(via6522::Via6522* via) { m_via = via; }

void CudaController::reset()
{
    m_input.clear();
    m_output.clear();
    m_outputPosition = 0;
    m_lastTip = true;
    m_lastByteAck = true;
    m_synchronousAttention = false;
    m_attentionFollowupPending = false;
    m_idleAckCycles = 0;
    m_responseTreqCycles = 0;
    m_shiftCycles = 0;
    m_shiftOutCompletion = false;
    m_releaseTreqAfterShift = false;
    m_stagedInput.reset();
    m_debug = {};
    if (m_via) m_via->setPortBInputBit(treqBit, true);
}

void CudaController::tick(int cycles)
{
    if (!m_via) return;
    if (m_responseTreqCycles > 0) {
        m_responseTreqCycles -= cycles;
        if (m_responseTreqCycles <= 0) m_via->setPortBInputBit(treqBit, false);
    }
    if (m_idleAckCycles > 0) {
        m_idleAckCycles -= cycles;
        if (m_idleAckCycles <= 0) m_via->externalShiftIn(0);
    }
    if (m_shiftCycles > 0) {
        m_shiftCycles -= cycles;
        if (m_shiftCycles <= 0) {
            if (m_shiftOutCompletion) m_via->externalShiftOutComplete();
            else {
                m_via->externalShiftIn(m_pendingShiftValue);
                if (m_releaseTreqAfterShift) {
                    m_releaseTreqAfterShift = false;
                    m_via->setPortBInputBit(treqBit, true);
                }
            }
        }
    }
}

void CudaController::shiftByteFromHost(std::uint8_t value)
{
    // VIA SR writes stage a byte. Cuda latches it on the next BYTEACK/TIP
    // transition while TIP is low.
    m_stagedInput = value;
}

void CudaController::portBChanged(std::uint8_t output, std::uint8_t direction)
{
    const auto pins = static_cast<std::uint8_t>(output & direction);
    const bool newTip = (pins & tip) != 0;
    const bool newByteAck = (pins & byteAck) != 0;
    m_debug.output = output;
    m_debug.direction = direction;
    m_debug.tip = newTip;
    m_debug.byteAck = newByteAck;
    if (newTip == m_lastTip && newByteAck == m_lastByteAck) return;
    ++m_debug.transitions;
    m_lastTip = newTip;
    m_lastByteAck = newByteAck;

    if (newTip && !newByteAck) {
        ++m_debug.attentions;
        // Cuda synchronous-attention state.  Firmware uses this before the
        // first packet to put both sides on a known byte boundary.  TREQ is
        // asserted and the shift-register interrupt supplies the idle ack.
        m_input.clear();
        m_output.clear();
        m_outputPosition = 0;
        m_stagedInput.reset();
        m_synchronousAttention = true;
        if (m_via) {
            m_via->setPortBInputBit(treqBit, false);
            // The Cuda clocks the attention byte after the same 61 us delay
            // as an idle acknowledgement.  Completing it synchronously lets
            // the ROM consume the SR flag before it has entered its wait path.
            m_idleAckCycles = 4'880;
        }
        return;
    }

    if (newTip && newByteAck) {
        const bool completedRequest = !m_input.empty();
        const bool completedResponse = !m_output.empty() && m_outputPosition >= m_output.size();
        if (m_via) {
            m_via->setPortBInputBit(treqBit, true);
            // The transaction-end clock follows the final response byte
            // immediately.  If it is delayed like the attention/request idle
            // acknowledgement, the ROM returns from its synchronous Cuda
            // call and receives the SR interrupt as an unexpected level-1
            // 68k interrupt during later startup code.
            m_idleAckCycles = 4'880; // about 61 us at the 80 MHz machine clock
        }
        m_synchronousAttention = false;
        if (completedRequest) finishPacket();
        m_input.clear();
        if (completedRequest && !m_output.empty() && m_via) {
            m_responseTreqCycles = 1'040; // about 13 us
        } else if (!completedRequest) {
            m_output.clear();
            m_outputPosition = 0;
        }
        return;
    }

    if (!newTip && m_via) {
        if ((m_via->debugState().auxiliaryControl & 0x10U) != 0) {
            if (m_stagedInput) {
                m_input.push_back(*m_stagedInput);
                m_stagedInput.reset();
            }
            m_shiftOutCompletion = true;
            m_shiftCycles = 5'680; // about 71 us
        } else {
            sendNextByte();
        }
    }
}

void CudaController::shiftByteToHostConsumed()
{
    if (!m_attentionFollowupPending || !m_via) return;
    m_attentionFollowupPending = false;
}

void CudaController::makeResponse(std::uint8_t type, std::uint8_t command)
{
    m_output = { type, 0, command };
    m_outputPosition = 0;
}

std::uint32_t CudaController::macintoshTime() const
{
    const auto unixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return static_cast<std::uint32_t>(unixSeconds + unixToMacintoshEpoch);
}

void CudaController::finishPacket()
{
    if (m_input.size() < 2) { m_output = { 2, 3, 0, 0 }; m_outputPosition = 0; return; }
    const auto type = m_input[0];
    const auto command = m_input[1];
    ++m_debug.packets;
    ++m_debug.commandCounts[command];
    m_debug.lastType = type;
    m_debug.lastCommand = command;
    m_debug.lastPacketSize = static_cast<std::uint16_t>(m_input.size());
    if (m_input.size() >= 4)
        m_debug.lastAddress = static_cast<std::uint16_t>((m_input[2] << 8) | m_input[3]);
    if (type == 0) { makeResponse(0, command); return; } // empty ADB response
    if (type != pseudoPacket) { m_output = { 2, 1, type, command }; m_outputPosition = 0; return; }

    makeResponse(pseudoPacket, command);
    switch (command) {
    case 0x02: { // read Cuda MCU memory; PRAM occupies 0x100-0x1ff
        if (m_input.size() >= 4) {
            const auto address = static_cast<std::uint16_t>((m_input[2] << 8) | m_input[3]);
            if (address >= pramMcuBase && address < pramMcuBase + m_pram.size()) {
                const auto start = static_cast<std::uint8_t>(address - pramMcuBase);
                for (unsigned index = 0; index < m_pram.size(); ++index)
                    m_output.push_back(m_pram[static_cast<std::uint8_t>(start + index)]);
            }
            else if (address >= 0x0f00U) {
                // Minimal CUDA firmware descriptor used by the ROM's version
                // probe: empty copyright string, descriptor length, version.
                m_output.insert(m_output.end(), { 0, 0, 0x19, 0, 2, 0, 0x29 });
            }
        }
        break;
    }
    case 0x03: { // get real time
        const auto value = macintoshTime();
        m_output.insert(m_output.end(), { static_cast<std::uint8_t>(value >> 24), static_cast<std::uint8_t>(value >> 16),
            static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value) });
        break;
    }
    case 0x07: { // open-ended PRAM read; TIP high terminates the transfer
        if (m_input.size() >= 4) {
            const auto address = static_cast<std::uint16_t>((m_input[2] << 8) | m_input[3]);
            if (address < m_pram.size()) {
                const auto start = static_cast<std::uint8_t>(address);
                for (unsigned index = 0; index < m_pram.size(); ++index)
                    m_output.push_back(m_pram[static_cast<std::uint8_t>(start + index)]);
            }
        }
        break;
    }
    case 0x08: { // write Cuda MCU memory
        if (m_input.size() >= 4) {
            auto address = static_cast<std::uint16_t>((m_input[2] << 8) | m_input[3]);
            for (std::size_t index = 4; index < m_input.size(); ++index, ++address) {
                if (address >= pramMcuBase && address < pramMcuBase + m_pram.size())
                    m_pram[address - pramMcuBase] = m_input[index];
            }
        }
        break;
    }
    case 0x0c: { // write PRAM
        if (m_input.size() >= 4) {
            auto address = static_cast<std::uint16_t>((m_input[2] << 8) | m_input[3]);
            for (std::size_t index = 4; index < m_input.size() && address < m_pram.size(); ++index, ++address)
                m_pram[address] = m_input[index];
        }
        break;
    }
    default: break; // commands without result acknowledge with the common header
    }
}

void CudaController::sendNextByte()
{
    const auto value = m_outputPosition < m_output.size() ? m_output[m_outputPosition++] : 0;
    m_pendingShiftValue = value;
    m_shiftOutCompletion = false;
    m_shiftCycles = 7'040; // about 88 us
    // TREQ describes whether another byte follows.  On real hardware the
    // Cuda changes it after clocking the current byte into the VIA.  Releasing
    // it here lets the host end the transaction before the last SR completion
    // and turns the subsequent completion into a spurious interrupt.
    m_releaseTreqAfterShift = m_outputPosition >= m_output.size();
}

} // namespace cutemac::devices::cuda
