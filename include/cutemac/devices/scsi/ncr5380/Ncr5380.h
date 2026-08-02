#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <QString>
#include <QByteArray>

#include "cutemac/devices/scsi/ScsiTarget.h"

namespace cutemac::devices::scsi::ncr5380 {

class Ncr5380 {
public:
    struct DebugState {
        QString phase;
        QByteArray activeCommand;
        QByteArray lastCommand;
        qsizetype dataIndex = 0;
        qsizetype dataLength = 0;
        qsizetype dataOutLength = 0;
        qsizetype expectedDataOutLength = 0;
        std::uint64_t completedCommands = 0;
        std::uint8_t activeTargetId = 0xff;
        std::uint8_t status = 0;
        std::uint8_t message = 0;
        std::uint8_t targetCommand = 0;
        std::uint8_t outputData = 0;
        bool selected = false;
        bool request = false;
        bool ack = false;
        bool commandReady = false;
    };

    void reset();

    void attachTarget(std::uint8_t id, std::shared_ptr<ScsiTarget> target);
    void detachTarget(std::uint8_t id);
    [[nodiscard]] bool hasTarget(std::uint8_t id) const;

    [[nodiscard]] std::uint8_t readRegister(std::uint8_t registerIndex, bool dack);
    void writeRegister(std::uint8_t registerIndex, bool dack, std::uint8_t value);
    [[nodiscard]] DebugState debugState() const;

private:
    enum class Phase {
        BusFree,
        Command,
        DataIn,
        DataOut,
        Status,
        MessageIn,
    };

    void setPhase(Phase phase, bool request = true);
    void acceptCommandByte(std::uint8_t value);
    void executeCommand();
    void finishDataPhaseIfDone();
    void completeDataOutCommand();
    [[nodiscard]] std::uint8_t readDataByte();
    void writeDataByte(std::uint8_t value);
    [[nodiscard]] std::uint8_t currentBusStatus();
    [[nodiscard]] std::uint8_t busAndStatus();
    [[nodiscard]] std::uint8_t phaseBits() const;
    [[nodiscard]] bool phaseMatchesTargetCommand() const;
    [[nodiscard]] qsizetype expectedCommandLength(std::uint8_t opcode) const;
    [[nodiscard]] QString phaseName() const;

    std::array<std::uint8_t, 8> m_registers {};
    std::array<std::shared_ptr<ScsiTarget>, 8> m_targets {};
    std::shared_ptr<ScsiTarget> m_activeTarget;
    QByteArray m_command;
    QByteArray m_dataBuffer;
    QByteArray m_dataOut;
    qsizetype m_dataIndex = 0;
    qsizetype m_expectedDataOut = 0;
    Phase m_phase = Phase::BusFree;
    std::uint8_t m_targetCommand = 0;
    std::uint8_t m_outputData = 0;
    std::uint8_t m_activeTargetId = 0xff;
    std::uint8_t m_status = 0;
    std::uint8_t m_message = 0;
    std::uint8_t m_senseKey = 0;
    std::uint64_t m_completedCommands = 0;
    bool m_selected = false;
    bool m_request = false;
    bool m_requestReassertPending = false;
    bool m_dataOutCompletionPending = false;
    int m_dataOutDrainStatusReads = 0;
    bool m_previousAck = false;
    bool m_commandReady = false;
    QByteArray m_lastCommand;
};

} // namespace cutemac::devices::scsi::ncr5380
