#include "cutemac/devices/scsi/ncr53c94/Ncr53c94.h"

#include <algorithm>

namespace cutemac::devices::scsi::ncr53c94 {
namespace {
constexpr std::uint8_t terminalCount = 0x10;
constexpr std::uint8_t interruptPending = 0x80;
constexpr std::uint8_t interruptReset = 0x80;
constexpr std::uint8_t interruptDisconnected = 0x20;
constexpr std::uint8_t interruptService = 0x10;
constexpr std::uint8_t interruptSuccess = 0x08;
}

void Ncr53c94::reset()
{
    m_registers.fill(0);
    m_fifo.clear(); m_cdb.clear(); m_data.clear(); m_dataOut.clear();
    m_dataPosition = 0; m_startTransferCount = m_transferCount = 0;
    m_targetId = 0; m_status = m_interruptStatus = m_sequenceStep = 0;
    m_scsiStatus = m_message = 0;
    m_dataIn = m_dataOutPhase = m_commandPhase = m_dmaActive = false;
    m_controllerCommandCounts.fill(0);
    m_scsiCommandCounts.fill(0);
}

void Ncr53c94::attachTarget(std::uint8_t id, std::shared_ptr<ScsiTarget> target)
{ if (id < m_targets.size()) m_targets[id] = std::move(target); }
void Ncr53c94::detachTarget(std::uint8_t id) { if (id < m_targets.size()) m_targets[id].reset(); }

Ncr53c94::DebugState Ncr53c94::debugState() const
{
    return { m_cdb, m_transferCount, m_dataPosition, m_data.size(), m_targetId,
        m_status, m_interruptStatus, m_sequenceStep, m_scsiStatus, m_message,
        m_dataIn, m_dataOutPhase, m_commandPhase };
}

std::uint8_t Ncr53c94::readRegister(std::uint8_t index)
{
    index &= 15U;
    switch (index) {
    case 0: return static_cast<std::uint8_t>(m_transferCount);
    case 1: return static_cast<std::uint8_t>(m_transferCount >> 8);
    case 2:
        if (m_fifo.isEmpty()) return 0;
        {
            const auto value = static_cast<std::uint8_t>(m_fifo.front());
            m_fifo.remove(0, 1);
            if (m_dmaActive && m_transferCount) --m_transferCount;
            if (m_dataIn && m_fifo.isEmpty()) {
                if (m_dataPosition >= m_data.size()) completeTransfer();
            }
            return value;
        }
    case 3: return m_registers[3];
    case 4: return m_status;
    case 5: {
        const auto value = m_interruptStatus;
        m_interruptStatus = 0;
        m_sequenceStep = 0;
        m_status &= 0x1fU;
        return value;
    }
    case 6: return m_sequenceStep;
    case 7: return static_cast<std::uint8_t>(m_fifo.size() & 0x1f);
    case 8: case 0xb: case 0xc: case 0xd: return m_registers[index];
    case 0xe: return static_cast<std::uint8_t>(m_transferCount >> 16);
    default: return 0;
    }
}

void Ncr53c94::writeRegister(std::uint8_t index, std::uint8_t value)
{
    index &= 15U;
    switch (index) {
    case 0: m_startTransferCount = (m_startTransferCount & 0xffff00U) | value; break;
    case 1: m_startTransferCount = (m_startTransferCount & 0xff00ffU) | (static_cast<std::uint32_t>(value) << 8); break;
    case 2:
        if (m_fifo.size() < 16) m_fifo.append(static_cast<char>(value));
        break;
    case 3: m_registers[3] = value; executeCommand(value); break;
    case 4: m_targetId = value & 7U; break;
    case 8: case 9: case 0xa: case 0xb: case 0xc: case 0xd: m_registers[index] = value; break;
    case 0xe: m_startTransferCount = (m_startTransferCount & 0x00ffffU) | (static_cast<std::uint32_t>(value) << 16); break;
    default: m_registers[index] = value; break;
    }
}

void Ncr53c94::raiseInterrupt(std::uint8_t cause)
{
    m_interruptStatus = cause;
    m_status |= interruptPending;
}

int Ncr53c94::commandLength(std::uint8_t opcode) const
{
    switch (opcode >> 5) { case 0: return 6; case 1: case 2: return 10; case 5: return 12; default: return 6; }
}

void Ncr53c94::selectTarget()
{
    auto target = m_targets[m_targetId];
    if (!target || !target->selectable()) { m_fifo.clear(); raiseInterrupt(interruptDisconnected); return; }
    if (!m_fifo.isEmpty() && (static_cast<std::uint8_t>(m_fifo.front()) & 0x80U)) m_fifo.remove(0, 1);
    // A DMA SELECT may perform arbitration/selection before the command bytes
    // are supplied by the DMA channel.  Successful target selection leaves
    // the controller in COMMAND phase; an empty FIFO is not a disconnect.
    if (m_fifo.isEmpty()) {
        m_commandPhase = true;
        m_sequenceStep = 2;
        m_status = static_cast<std::uint8_t>((m_status & 0xf8U) | 2U);
        return;
    }
    executeCdb();
}

void Ncr53c94::executeCdb()
{
    if (m_fifo.isEmpty()) return;
    const auto target = m_targets[m_targetId];
    if (!target || !target->selectable()) { m_fifo.clear(); raiseInterrupt(interruptDisconnected); return; }
    const auto length = std::min(commandLength(static_cast<std::uint8_t>(m_fifo.front())), static_cast<int>(m_fifo.size()));
    m_cdb = m_fifo.left(length); m_fifo.remove(0, length);
    const auto opcode = static_cast<std::uint8_t>(m_cdb.front());
    ++m_scsiCommandCounts[opcode];
    m_transferCount = 0;
    m_dmaActive = false;
    m_dataOutPhase = opcode == 0x0a || opcode == 0x2a || opcode == 0x15;
    if (m_dataOutPhase) {
        m_dataOut.clear(); m_data.clear(); m_dataPosition = 0; m_dataIn = false;
    } else {
        const auto result = target->executeCommand(m_cdb, {});
        m_data = result.data; m_dataPosition = 0; m_scsiStatus = result.status; m_message = result.message;
        m_dataIn = !m_data.isEmpty();
    }
    m_sequenceStep = 4;
    m_status = static_cast<std::uint8_t>((m_status & 0xe8U)
        | (m_dataIn ? 1U : m_dataOutPhase ? 0U : 3U));
    raiseInterrupt(interruptService | interruptSuccess);
}

void Ncr53c94::completeTransfer()
{
    m_transferCount = 0;
    m_dmaActive = false;
    m_status |= terminalCount;
    if (m_commandPhase) {
        if (!m_fifo.isEmpty() && m_fifo.size() >= commandLength(static_cast<std::uint8_t>(m_fifo.front()))) {
            m_commandPhase = false;
            selectTarget();
        } else {
            m_status = static_cast<std::uint8_t>((m_status & 0xf8U) | 2U);
            raiseInterrupt(interruptService);
        }
        return;
    }
    if (m_dataOutPhase) {
        const auto target = m_targets[m_targetId];
        const auto result = target ? target->executeCommand(m_cdb, m_dataOut) : ScsiCommandResult {};
        m_scsiStatus = result.status; m_message = result.message; m_dataOutPhase = false;
    }
    m_dataIn = false;
    m_status = static_cast<std::uint8_t>((m_status & 0xf8U) | 3U);
    raiseInterrupt(interruptService);
}

void Ncr53c94::executeCommand(std::uint8_t command)
{
    ++m_controllerCommandCounts[command & 0x7fU];
    const bool dma = (command & 0x80U) != 0;
    switch (command & 0x7fU) {
    case 0x01: m_fifo.clear(); break;
    case 0x02: { const auto targets = m_targets; reset(); m_targets = targets; break; }
    case 0x03: raiseInterrupt(interruptReset); break;
    case 0x10:
        if (m_commandPhase && !dma && !m_fifo.isEmpty()
            && m_fifo.size() >= commandLength(static_cast<std::uint8_t>(m_fifo.front()))) {
            m_commandPhase = false;
            executeCdb();
        } else if (m_dataIn && !dma) {
            m_fifo.clear();
            // Programmed I/O performs one SCSI REQ/ACK handshake per command.
            const auto count = std::min<qsizetype>(1, m_data.size() - m_dataPosition);
            m_fifo.append(m_data.constData() + m_dataPosition, count);
            m_dataPosition += count;
            m_dmaActive = false;
            m_status = static_cast<std::uint8_t>((m_status & 0xe8U) | 1U);
            raiseInterrupt(interruptService);
        } else {
            m_transferCount = dma ? (m_startTransferCount ? m_startTransferCount : 0x10000U) : 0;
            m_dmaActive = dma;
        }
        break;
    case 0x11:
        m_fifo.clear(); m_fifo.append(static_cast<char>(m_scsiStatus)); m_fifo.append(static_cast<char>(m_message));
        m_status = static_cast<std::uint8_t>((m_status & 0xf8U) | 7U); raiseInterrupt(interruptSuccess); break;
    case 0x12: m_fifo.clear(); m_data.clear(); m_dataOut.clear(); m_status &= 0xf8U; raiseInterrupt(interruptDisconnected); break;
    case 0x41: case 0x42: case 0x43:
        selectTarget();
        if (dma && m_commandPhase) {
            m_transferCount = m_startTransferCount ? m_startTransferCount : 0x10000U;
            m_dmaActive = true;
        }
        break;
    default: break;
    }
}

std::uint16_t Ncr53c94::readDmaWord()
{
    std::uint16_t result = 0;
    for (unsigned byte = 0; byte < 2; ++byte) {
        result = static_cast<std::uint16_t>((result << 8) | (m_dataPosition < m_data.size()
            ? static_cast<std::uint8_t>(m_data[m_dataPosition++]) : 0));
        if (m_transferCount) --m_transferCount;
    }
    if (m_transferCount == 0 || m_dataPosition >= m_data.size()) completeTransfer();
    return static_cast<std::uint16_t>((result << 8) | (result >> 8));
}

void Ncr53c94::writeDmaWord(std::uint16_t value)
{
    value = static_cast<std::uint16_t>((value << 8) | (value >> 8));
    auto& destination = m_commandPhase ? m_fifo : m_dataOut;
    destination.append(static_cast<char>(value >> 8)); destination.append(static_cast<char>(value));
    if (m_transferCount > 1) m_transferCount -= 2; else m_transferCount = 0;
    if (m_transferCount == 0) completeTransfer();
}

} // namespace cutemac::devices::scsi::ncr53c94
