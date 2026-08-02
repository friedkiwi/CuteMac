#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cutemac/core/IMachine.h"
#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/core/MachineScheduler.h"
#include "cutemac/cpu/m68k/M68kBus.h"
#include "cutemac/cpu/m68k/M68kCpuCore.h"
#include "cutemac/devices/audio/AppleSoundChip.h"
#include "cutemac/devices/adb/AdbTransceiver.h"
#include "cutemac/devices/iwm/IwmController.h"
#include "cutemac/devices/nubus/NuBusBus.h"
#include "cutemac/devices/rtc/MacRtc.h"
#include "cutemac/devices/scc/Z8530Scc.h"
#include "cutemac/devices/scsi/ScsiBlockDevice.h"
#include "cutemac/devices/scsi/ScsiCdRomDevice.h"
#include "cutemac/devices/scsi/ncr5380/MacintoshNcr5380Bus.h"
#include "cutemac/devices/scsi/ncr5380/Ncr5380.h"
#include "cutemac/devices/via6522/Via6522.h"

namespace cutemac::machines::maciicx {

class MacIIcxMachine final : public core::IMachine, public core::IDebugCpuAccess, public cpu::m68k::M68kBus {
public:
    explicit MacIIcxMachine(std::size_t ramSize, const QString& nvramPath = {});

    [[nodiscard]] QString machineId() const override;
    [[nodiscard]] bool loadRomFile(const QString& path, const QStringList& patches) override;
    [[nodiscard]] bool loadDiskImage(const QString& path) override;
    void ejectDiskImage() override;
    [[nodiscard]] bool loadScsiDisk(int id, const QString& path, bool readOnly) override;
    [[nodiscard]] bool loadScsiCdRom(int id, const QString& path) override;
    void ejectScsiCdRom(int id) override;
    void ejectScsiDevice(int id) override;
    [[nodiscard]] bool loadFloppyImage(const QString& path, bool readOnly) override;
    void ejectFloppyImage() override;
    void attachSerialEndpoint(int channel, std::shared_ptr<devices::serial::SerialEndpoint> endpoint) override;
    void reset() override;
    [[nodiscard]] int runCycles(int cycles) override;
    [[nodiscard]] int stepInstruction() override;
    [[nodiscard]] std::uint64_t cycleCount() const override;
    [[nodiscard]] std::uint32_t programCounter() const override;
    [[nodiscard]] bool overlayEnabled() const override;
    [[nodiscard]] QByteArray framebufferBytes() const override;
    [[nodiscard]] devices::video::VideoFrame videoFrame() const override;
    [[nodiscard]] core::GuestPowerRequest takePowerRequest() override;
    void queueInput(const core::GuestInputEvent& event, std::uint64_t cycle) override;

    [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
    [[nodiscard]] std::uint16_t read16(std::uint32_t address) override;
    [[nodiscard]] std::uint32_t read32(std::uint32_t address) override;
    void write8(std::uint32_t address, std::uint8_t value) override;
    void write16(std::uint32_t address, std::uint16_t value) override;
    void write32(std::uint32_t address, std::uint32_t value) override;

    [[nodiscard]] bool installNuBusCard(int slot, std::shared_ptr<devices::nubus::NuBusCard> card);
    [[nodiscard]] std::shared_ptr<devices::nubus::NuBusCard> nubusCard(int slot) const { return m_nubus.card(slot); }
    [[nodiscard]] cpu::m68k::M68kCpuCore::RegisterSnapshot cpuRegisters() const;
    [[nodiscard]] QString disassemble(std::uint32_t address) const;
    [[nodiscard]] int disassembleBytes(std::uint32_t address) const;
    [[nodiscard]] QString debugCpuArchitecture() const override;
    [[nodiscard]] QStringList debugRegisterLines() const override;
    [[nodiscard]] std::uint8_t debugRead8(std::uint32_t address) const override;
    [[nodiscard]] std::uint16_t debugRead16(std::uint32_t address) const override;
    [[nodiscard]] std::uint32_t debugRead32(std::uint32_t address) const override;
    void debugWrite8(std::uint32_t address, std::uint8_t value) override;
    void debugWrite16(std::uint32_t address, std::uint16_t value) override;
    void debugWrite32(std::uint32_t address, std::uint32_t value) override;
    [[nodiscard]] devices::via6522::Via6522::DebugState via1DebugState() const { return m_via1.debugState(); }
    [[nodiscard]] devices::via6522::Via6522::DebugState via2DebugState() const { return m_via2.debugState(); }
    [[nodiscard]] devices::scsi::ncr5380::Ncr5380::DebugState scsiDebugState() const { return m_scsi.debugState(); }
    [[nodiscard]] devices::iwm::IwmController::DebugState swimDebugState() const { return m_swim.debugState(); }
    void setSwimTraceEnabled(bool enabled) { m_swim.setTraceEnabled(enabled); }
    [[nodiscard]] QStringList swimTraceEvents() const { return m_swim.traceEvents(); }
    [[nodiscard]] bool sccInterruptActive() const { return m_scc.interruptActive(); }
    [[nodiscard]] devices::adb::AdbTransceiver::DebugState adbDebugState() const { return m_adbTransceiver.debugState(); }
    struct IoStatistics {
        std::uint64_t scsiReads = 0;
        std::uint64_t scsiWrites = 0;
        std::uint64_t swimReads = 0;
        std::uint64_t swimWrites = 0;
        std::uint64_t nubusReads = 0;
        std::uint64_t nubusWrites = 0;
    };
    [[nodiscard]] IoStatistics ioStatistics() const { return m_ioStatistics; }

private:
    [[nodiscard]] bool isIo(std::uint32_t address) const;
    [[nodiscard]] std::uint8_t readIo8(std::uint32_t address);
    void writeIo8(std::uint32_t address, std::uint8_t value);
    void applyInput(const core::GuestInputEvent& event);
    void updateInterrupts();
    void updateViaInputs();
    void advanceDevices(int cpuCycles);
    [[nodiscard]] std::optional<std::size_t> ramIndex(std::uint32_t address) const;

    cpu::m68k::M68kCpuCore m_cpu;
    QVector<std::uint8_t> m_ram;
    std::array<std::uint8_t, 256 * 1024> m_rom {};
    devices::via6522::Via6522 m_via1;
    devices::via6522::Via6522 m_via2;
    devices::rtc::MacRtc m_rtc;
    devices::scc::Z8530Scc m_scc;
    devices::iwm::IwmController m_swim;
    devices::audio::AppleSoundChip m_asc;
    devices::adb::AdbTransceiver m_adbTransceiver;
    devices::scsi::ncr5380::Ncr5380 m_scsi;
    devices::scsi::ncr5380::MacintoshNcr5380Bus m_scsiBus;
    devices::nubus::NuBusBus m_nubus;
    std::array<std::shared_ptr<devices::scsi::ScsiBlockDevice>, 7> m_scsiDisks;
    std::array<std::shared_ptr<devices::scsi::ScsiCdRomDevice>, 7> m_scsiCdRoms;
    core::MachineScheduler m_scheduler;
    QString m_romPath;
    QString m_floppyPath;
    bool m_romLoaded = false;
    bool m_overlay = true;
    bool m_adbIrqPending = false;
    bool m_hostMousePositionValid = false;
    std::int16_t m_hostMouseX = 0;
    std::int16_t m_hostMouseY = 0;
    std::uint8_t m_nubusIrqState = 0x3f;
    std::uint8_t m_glueRamSize = 0;
    int m_viaCycleRemainder = 0;
    IoStatistics m_ioStatistics;
};

} // namespace cutemac::machines::maciicx
