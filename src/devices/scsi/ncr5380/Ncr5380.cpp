#include "cutemac/devices/scsi/ncr5380/Ncr5380.h"

#include <algorithm>

namespace cutemac::devices::scsi::ncr5380 {

namespace {

constexpr std::uint8_t currentData = 0;
constexpr std::uint8_t initiatorCommand = 1;
constexpr std::uint8_t mode = 2;
constexpr std::uint8_t targetCommand = 3;
constexpr std::uint8_t currentScsiBusStatus = 4;
constexpr std::uint8_t busAndStatusRegister = 5;
constexpr std::uint8_t inputData = 6;
constexpr std::uint8_t resetRegister = 7;

constexpr std::uint8_t icrAssertDataBus = 0x01;
constexpr std::uint8_t icrAssertSelect = 0x04;
constexpr std::uint8_t icrAssertBusy = 0x08;
constexpr std::uint8_t icrAssertAck = 0x10;
constexpr std::uint8_t icrAssertReset = 0x80;
constexpr std::uint8_t modeArbitrate = 0x01;

constexpr std::uint8_t csrIo = 0x04;
constexpr std::uint8_t csrCd = 0x08;
constexpr std::uint8_t csrMsg = 0x10;
constexpr std::uint8_t csrReq = 0x20;
constexpr std::uint8_t csrBsy = 0x40;

constexpr std::uint8_t bsrPhaseMatch = 0x08;
constexpr std::uint8_t bsrReqPhaseMatch = 0x20;
constexpr std::uint8_t bsrDmaRequest = 0x40;
constexpr std::uint8_t bsrAck = 0x01;

} // namespace

void Ncr5380::reset()
{
    m_registers.fill(0);
    m_command.clear();
    m_dataBuffer.clear();
    m_dataOut.clear();
    m_dataIndex = 0;
    m_expectedDataOut = 0;
    m_phase = Phase::BusFree;
    m_targetCommand = 0;
    m_outputData = 0;
    m_status = 0;
    m_message = 0;
    m_senseKey = 0;
    m_completedCommands = 0;
    m_selected = false;
    m_request = false;
    m_previousAck = false;
    m_commandReady = false;
    m_lastCommand.clear();
    m_activeTarget.reset();
    m_activeTargetId = 0xff;
}

void Ncr5380::attachTarget(std::uint8_t id, std::shared_ptr<ScsiTarget> target)
{
    if (id < m_targets.size()) {
        m_targets[id] = std::move(target);
    }
}

void Ncr5380::detachTarget(std::uint8_t id)
{
    if (id < m_targets.size()) {
        m_targets[id].reset();
    }
}

bool Ncr5380::hasTarget(std::uint8_t id) const
{
    return id < m_targets.size() && m_targets[id] != nullptr && m_targets[id]->ready();
}

std::uint8_t Ncr5380::readRegister(std::uint8_t registerIndex, bool dack)
{
    registerIndex &= 0x07;
    if (dack) {
        return readDataByte();
    }

    switch (registerIndex) {
    case currentData:
        return readDataByte();
    case initiatorCommand:
        return static_cast<std::uint8_t>(0x40 | (m_registers[mode] & modeArbitrate ? 0x40 : 0x00));
    case targetCommand:
        return m_targetCommand;
    case currentScsiBusStatus:
        return currentBusStatus();
    case busAndStatusRegister:
        return busAndStatus();
    case inputData:
        return m_outputData;
    case resetRegister:
        m_request = false;
        return 0;
    default:
        return m_registers[registerIndex];
    }
}

void Ncr5380::writeRegister(std::uint8_t registerIndex, bool dack, std::uint8_t value)
{
    registerIndex &= 0x07;
    if (dack) {
        writeDataByte(value);
        return;
    }

    switch (registerIndex) {
    case currentData:
        m_outputData = value;
        m_registers[currentData] = value;
        break;
    case initiatorCommand: {
        const auto previous = m_registers[initiatorCommand];
        m_registers[initiatorCommand] = value;
        if ((value & icrAssertReset) != 0) {
            reset();
            return;
        }

        if ((value & icrAssertSelect) != 0 && !m_selected) {
            std::uint8_t selectedId = 0xff;
            for (std::uint8_t id = 0; id < 7; ++id) {
                if ((m_outputData & (1U << id)) != 0 && hasTarget(id)) {
                    selectedId = id;
                    break;
                }
            }
            if (selectedId != 0xff) {
                m_activeTarget = m_targets[selectedId];
                m_activeTargetId = selectedId;
                m_selected = true;
            }
        }

        if ((previous & icrAssertSelect) != 0 && (value & icrAssertSelect) == 0 && m_selected) {
            setPhase(Phase::Command);
        }

        const auto ack = (value & icrAssertAck) != 0;
        if (ack && !m_previousAck) {
            if (m_phase == Phase::Command && (value & icrAssertDataBus) != 0) {
                acceptCommandByte(m_outputData);
            } else if (m_phase == Phase::DataIn || m_phase == Phase::DataOut || m_phase == Phase::Status || m_phase == Phase::MessageIn) {
                m_request = false;
            }
        } else if (!ack && m_previousAck) {
            if (m_phase == Phase::Command && m_selected && !m_command.isEmpty()) {
                if (m_commandReady) {
                    m_commandReady = false;
                    executeCommand();
                } else if (m_command.size() < expectedCommandLength(static_cast<std::uint8_t>(m_command[0]))) {
                    m_request = true;
                }
            } else if (m_phase == Phase::DataIn) {
                if (m_dataIndex >= m_dataBuffer.size()) {
                    setPhase(Phase::Status);
                } else {
                    m_request = true;
                }
            } else if (m_phase == Phase::DataOut) {
                finishDataPhaseIfDone();
                if (m_phase == Phase::DataOut) {
                    m_request = true;
                }
            } else if (m_phase == Phase::Status) {
                setPhase(Phase::MessageIn);
            } else if (m_phase == Phase::MessageIn) {
                setPhase(Phase::BusFree, false);
            }
        }
        m_previousAck = ack;
        break;
    }
    case mode:
        m_registers[mode] = value;
        break;
    case targetCommand:
        m_targetCommand = value;
        m_registers[targetCommand] = value;
        break;
    case currentScsiBusStatus:
        m_registers[currentScsiBusStatus] = value;
        break;
    case busAndStatusRegister:
        m_registers[busAndStatusRegister] = value;
        break;
    case inputData:
        m_registers[inputData] = value;
        break;
    case resetRegister:
        m_registers[resetRegister] = value;
        break;
    }
}

void Ncr5380::setPhase(Phase phase, bool request)
{
    m_phase = phase;
    m_request = request;
    if (phase == Phase::BusFree) {
        m_selected = false;
        m_activeTarget.reset();
        m_command.clear();
        m_dataBuffer.clear();
        m_dataOut.clear();
        m_dataIndex = 0;
        m_expectedDataOut = 0;
        m_commandReady = false;
        m_activeTargetId = 0xff;
    }
}

void Ncr5380::acceptCommandByte(std::uint8_t value)
{
    m_command.append(static_cast<char>(value));
    m_request = false;
    if (m_command.size() >= expectedCommandLength(static_cast<std::uint8_t>(m_command[0]))) {
        m_commandReady = true;
    }
}

void Ncr5380::executeCommand()
{
    if (!m_activeTarget) {
        m_status = 0x02;
        m_message = 0x00;
        setPhase(Phase::Status);
        return;
    }

    m_dataOut.clear();
    const auto opcode = static_cast<std::uint8_t>(m_command[0]);
    if (opcode == 0x0a && m_command.size() >= 6) {
        const auto blocks = static_cast<std::uint8_t>(m_command[4]) == 0 ? 256 : static_cast<std::uint8_t>(m_command[4]);
        m_expectedDataOut = blocks * 512;
        m_dataBuffer.clear();
        m_dataIndex = 0;
        setPhase(Phase::DataOut);
        return;
    }
    if (opcode == 0x15 && m_command.size() >= 6 && static_cast<std::uint8_t>(m_command[4]) != 0) {
        m_expectedDataOut = static_cast<std::uint8_t>(m_command[4]);
        m_dataBuffer.clear();
        m_dataIndex = 0;
        setPhase(Phase::DataOut);
        return;
    }
    if (opcode == 0x04 && m_command.size() >= 6 && (static_cast<std::uint8_t>(m_command[1]) & 0x10) != 0) {
        m_expectedDataOut = 4;
        m_dataBuffer.clear();
        m_dataIndex = 0;
        setPhase(Phase::DataOut);
        return;
    }

    const auto result = m_activeTarget->executeCommand(m_command, {});
    m_lastCommand = m_command;
    ++m_completedCommands;
    m_dataBuffer = result.data;
    m_dataIndex = 0;
    m_status = result.status;
    m_message = result.message;
    m_senseKey = result.senseKey;
    setPhase(m_dataBuffer.isEmpty() ? Phase::Status : Phase::DataIn);
}

void Ncr5380::finishDataPhaseIfDone()
{
    if (m_phase == Phase::DataIn && m_dataIndex >= m_dataBuffer.size()) {
        setPhase(Phase::Status);
    } else if (m_phase == Phase::DataOut && !m_command.isEmpty()
        && static_cast<std::uint8_t>(m_command[0]) == 0x04 && m_dataOut.size() >= m_expectedDataOut) {
        const auto defectLength = (static_cast<qsizetype>(static_cast<std::uint8_t>(m_dataOut[2])) << 8)
            | static_cast<std::uint8_t>(m_dataOut[3]);
        const bool initializationPattern = (static_cast<std::uint8_t>(m_dataOut[1]) & 0x08) != 0;
        if (initializationPattern && m_dataOut.size() < 8) {
            m_expectedDataOut = 8;
            return;
        }
        const auto patternLength = initializationPattern
            ? (static_cast<qsizetype>(static_cast<std::uint8_t>(m_dataOut[6])) << 8) | static_cast<std::uint8_t>(m_dataOut[7])
            : 0;
        const auto totalLength = 4 + (initializationPattern ? 4 : 0) + defectLength + patternLength;
        if (m_dataOut.size() < totalLength) {
            m_expectedDataOut = totalLength;
            return;
        }
        const auto result = m_activeTarget ? m_activeTarget->executeCommand(m_command, m_dataOut) : ScsiCommandResult {};
        m_lastCommand = m_command;
        ++m_completedCommands;
        m_status = result.status;
        m_message = result.message;
        m_senseKey = result.senseKey;
        setPhase(Phase::Status);
    } else if (m_phase == Phase::DataOut && m_dataOut.size() >= m_expectedDataOut) {
        const auto result = m_activeTarget ? m_activeTarget->executeCommand(m_command, m_dataOut) : ScsiCommandResult {};
        m_lastCommand = m_command;
        ++m_completedCommands;
        m_status = result.status;
        m_message = result.message;
        m_senseKey = result.senseKey;
        setPhase(Phase::Status);
    }
}

std::uint8_t Ncr5380::readDataByte()
{
    switch (m_phase) {
    case Phase::DataIn:
        if (m_dataIndex < m_dataBuffer.size()) {
            return static_cast<std::uint8_t>(m_dataBuffer[m_dataIndex++]);
        }
        return 0;
    case Phase::Status:
        return m_status;
    case Phase::MessageIn:
        return m_message;
    case Phase::Command:
    case Phase::DataOut:
    case Phase::BusFree:
        return 0;
    }
    return 0;
}

void Ncr5380::writeDataByte(std::uint8_t value)
{
    if (m_phase != Phase::DataOut) {
        return;
    }
    m_dataOut.append(static_cast<char>(value));
}

std::uint8_t Ncr5380::currentBusStatus() const
{
    std::uint8_t status = phaseBits();
    if (m_selected) {
        status |= csrBsy;
    }
    if (m_request) {
        status |= csrReq;
    }
    return status;
}

std::uint8_t Ncr5380::busAndStatus() const
{
    std::uint8_t status = phaseMatchesTargetCommand() ? bsrPhaseMatch : 0;
    if (m_request) {
        status |= bsrReqPhaseMatch;
    }
    if (m_request && (m_phase == Phase::DataIn || m_phase == Phase::DataOut)) {
        status |= bsrDmaRequest;
    }
    if (m_previousAck) {
        status |= bsrAck;
    }
    return status;
}

std::uint8_t Ncr5380::phaseBits() const
{
    switch (m_phase) {
    case Phase::DataIn:
        return csrIo;
    case Phase::DataOut:
        return 0;
    case Phase::Command:
        return csrCd;
    case Phase::Status:
        return static_cast<std::uint8_t>(csrCd | csrIo);
    case Phase::MessageIn:
        return static_cast<std::uint8_t>(csrMsg | csrCd | csrIo);
    case Phase::BusFree:
        return 0;
    }
    return 0;
}

bool Ncr5380::phaseMatchesTargetCommand() const
{
    return (m_targetCommand & 0x07) == ((phaseBits() >> 2) & 0x07);
}

Ncr5380::DebugState Ncr5380::debugState() const
{
    return {
        phaseName(),
        m_command,
        m_lastCommand,
        m_dataIndex,
        m_dataBuffer.size(),
        m_dataOut.size(),
        m_expectedDataOut,
        m_completedCommands,
        m_activeTargetId,
        m_status,
        m_message,
        m_targetCommand,
        m_outputData,
        m_selected,
        m_request,
        m_previousAck,
        m_commandReady,
    };
}

QString Ncr5380::phaseName() const
{
    switch (m_phase) {
    case Phase::BusFree:
        return QStringLiteral("bus-free");
    case Phase::Command:
        return QStringLiteral("command");
    case Phase::DataIn:
        return QStringLiteral("data-in");
    case Phase::DataOut:
        return QStringLiteral("data-out");
    case Phase::Status:
        return QStringLiteral("status");
    case Phase::MessageIn:
        return QStringLiteral("message-in");
    }
    return QStringLiteral("unknown");
}

qsizetype Ncr5380::expectedCommandLength(std::uint8_t opcode) const
{
    const auto group = opcode >> 5;
    if (group == 0) {
        return 6;
    }
    if (group == 1 || group == 2) {
        return 10;
    }
    if (group == 5) {
        return 12;
    }
    return 6;
}

} // namespace cutemac::devices::scsi::ncr5380
