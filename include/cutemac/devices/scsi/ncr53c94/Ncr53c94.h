#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <QByteArray>

#include "cutemac/devices/scsi/ScsiTarget.h"

namespace cutemac::devices::scsi::ncr53c94 {

class Ncr53c94 {
public:
    struct DebugState {
        QByteArray cdb;
        std::uint32_t transferCount = 0;
        qsizetype dataPosition = 0;
        qsizetype dataSize = 0;
        std::uint8_t targetId = 0;
        std::uint8_t status = 0;
        std::uint8_t interruptStatus = 0;
        std::uint8_t sequenceStep = 0;
        std::uint8_t scsiStatus = 0;
        std::uint8_t message = 0;
        bool dataIn = false;
        bool dataOut = false;
        bool command = false;
    };

    void reset();
    void attachTarget(std::uint8_t id, std::shared_ptr<ScsiTarget> target);
    void detachTarget(std::uint8_t id);
    [[nodiscard]] std::uint8_t readRegister(std::uint8_t index);
    void writeRegister(std::uint8_t index, std::uint8_t value);
    [[nodiscard]] std::uint16_t readDmaWord();
    void writeDmaWord(std::uint16_t value);
    [[nodiscard]] bool interruptActive() const { return (m_status & 0x80U) != 0; }
    [[nodiscard]] bool dmaRequest() const { return m_dmaActive && m_transferCount != 0
        && (m_dataIn || m_dataOutPhase || m_commandPhase); }
    [[nodiscard]] bool dmaToHost() const { return m_dataIn; }
    [[nodiscard]] const std::array<std::uint64_t, 128>& controllerCommandCounts() const { return m_controllerCommandCounts; }
    [[nodiscard]] const std::array<std::uint64_t, 256>& scsiCommandCounts() const { return m_scsiCommandCounts; }
    [[nodiscard]] DebugState debugState() const;

private:
    void executeCommand(std::uint8_t command);
    void selectTarget();
    void executeCdb();
    void completeTransfer();
    void raiseInterrupt(std::uint8_t cause);
    [[nodiscard]] int commandLength(std::uint8_t opcode) const;

    std::array<std::shared_ptr<ScsiTarget>, 8> m_targets {};
    std::array<std::uint8_t, 16> m_registers {};
    QByteArray m_fifo;
    QByteArray m_cdb;
    QByteArray m_data;
    QByteArray m_dataOut;
    qsizetype m_dataPosition = 0;
    std::uint32_t m_startTransferCount = 0;
    std::uint32_t m_transferCount = 0;
    std::uint8_t m_targetId = 0;
    std::uint8_t m_status = 0;
    std::uint8_t m_interruptStatus = 0;
    std::uint8_t m_sequenceStep = 0;
    std::uint8_t m_scsiStatus = 0;
    std::uint8_t m_message = 0;
    bool m_dataIn = false;
    bool m_dataOutPhase = false;
    bool m_commandPhase = false;
    bool m_dmaActive = false;
    std::array<std::uint64_t, 128> m_controllerCommandCounts {};
    std::array<std::uint64_t, 256> m_scsiCommandCounts {};
};

} // namespace cutemac::devices::scsi::ncr53c94
