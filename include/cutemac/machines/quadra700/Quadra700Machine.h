#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/core/IMachine.h"
#include "cutemac/core/MachineScheduler.h"
#include "cutemac/core/PhysicalMemoryMap.h"
#include "cutemac/cpu/m68k/M68kBus.h"
#include "cutemac/cpu/m68k/M68kCpuCore.h"
#include "cutemac/devices/adb/AdbTransceiver.h"
#include "cutemac/devices/audio/AppleSoundChip.h"
#include "cutemac/devices/iwm/IwmController.h"
#include "cutemac/devices/nubus/NuBusBus.h"
#include "cutemac/devices/rtc/MacRtc.h"
#include "cutemac/devices/scc/Z8530Scc.h"
#include "cutemac/devices/scsi/ScsiBlockDevice.h"
#include "cutemac/devices/scsi/ScsiCdRomDevice.h"
#include "cutemac/devices/scsi/ncr53c94/Ncr53c94.h"
#include "cutemac/devices/via6522/Via6522.h"
#include "cutemac/devices/video/DafbVideo.h"

namespace cutemac::machines::quadra700 {

class Quadra700Machine final : public core::IMachine, public core::IDebugCpuAccess, public core::IDebugDeviceAccess, public cpu::m68k::M68kBus {
public:
    explicit Quadra700Machine(std::size_t ramSize, const QString& nvramPath = {});

    [[nodiscard]] QString machineId() const override;
    [[nodiscard]] bool loadRomFile(const QString& path, const QStringList& patches) override;
    [[nodiscard]] bool loadDiskImage(const QString& path) override;
    void ejectDiskImage() override;
    [[nodiscard]] bool loadScsiDisk(int id, const QString& path, bool readOnly) override;
    [[nodiscard]] bool loadScsiCdRom(int id, const QString& path) override;
    void ejectScsiCdRom(int id) override;
    void ejectScsiDevice(int id) override;
    [[nodiscard]] bool loadFloppyImage(const QString& path, bool readOnly) override;
    [[nodiscard]] bool loadFloppyImage(int drive, const QString& path, bool readOnly) override;
    void ejectFloppyImage() override;
    void ejectFloppyImage(int drive) override;
    void attachSerialEndpoint(int channel, std::shared_ptr<devices::serial::SerialEndpoint> endpoint) override;
    void reset() override;
    [[nodiscard]] bool triggerProgrammersInterrupt() override;
    [[nodiscard]] int runCycles(int cycles) override;
    [[nodiscard]] int stepInstruction() override;
    [[nodiscard]] std::uint64_t cycleCount() const override;
    [[nodiscard]] std::uint32_t programCounter() const override;
    [[nodiscard]] std::uint64_t diskActivityCounter() const override;

    // Machine-neutral device debug surface (core::IDebugDeviceAccess), so the
    // debug console reaches the drive through one interface every machine
    // implements instead of a per-machine special case.
    [[nodiscard]] int floppyDriveCount() const override { return 2; }
    [[nodiscard]] bool loadFloppy(int drive, const QString& path, bool readOnly) override
    {
        return loadFloppyImage(drive, path, readOnly);
    }
    void ejectFloppy(int drive) override { ejectFloppyImage(drive); }
    [[nodiscard]] QString floppyPath(int drive) const override { return floppyImagePath(drive); }
    [[nodiscard]] devices::iwm::IwmController::DebugState floppyState() const override { return m_swim.debugState(); }
    [[nodiscard]] devices::iwm::IwmController::DebugState floppyState(int drive) const override
    {
        return m_swim.debugState(drive);
    }
    [[nodiscard]] QByteArray floppyTrackBytes(int track, int side) const override
    {
        return m_swim.trackBytesForDebug(track, side);
    }
    [[nodiscard]] QByteArray floppyLastWindow() const override { return m_swim.lastNibblesForDebug(); }
    void setFloppyTraceEnabled(bool enabled) override { m_swim.setTraceEnabled(enabled); }
    [[nodiscard]] QStringList floppyTraceEvents() const override { return m_swim.traceEvents(); }


    // Live media state for the frontend; see IMachine::tracksMediaState.
    [[nodiscard]] bool tracksMediaState() const override { return true; }
    [[nodiscard]] QString floppyImagePath(int drive) const override
    {
        return drive >= 0 && drive < static_cast<int>(m_floppyPaths.size()) ? m_floppyPaths[static_cast<std::size_t>(drive)] : QString();
    }
    [[nodiscard]] QString scsiImagePath(int id) const override
    {
        if (id < 0 || id >= static_cast<int>(m_scsiDisks.size())) return {};
        const auto index = static_cast<std::size_t>(id);
        if (m_scsiCdRoms[index]) return m_scsiCdRoms[index]->imagePath();
        if (m_scsiDisks[index]) return m_scsiDisks[index]->imagePath();
        return {};
    }

    [[nodiscard]] bool overlayEnabled() const override;
    [[nodiscard]] QByteArray framebufferBytes() const override;
    [[nodiscard]] devices::video::VideoFrame videoFrame() const override;
    void queueInput(const core::GuestInputEvent& event, std::uint64_t cycle) override;

    [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
    [[nodiscard]] std::uint16_t read16(std::uint32_t address) override;
    [[nodiscard]] std::uint32_t read32(std::uint32_t address) override;
    void write8(std::uint32_t address, std::uint8_t value) override;
    void write16(std::uint32_t address, std::uint16_t value) override;
    void write32(std::uint32_t address, std::uint32_t value) override;

    [[nodiscard]] bool installNuBusCard(int slot, std::shared_ptr<devices::nubus::NuBusCard> card);
    [[nodiscard]] QString debugCpuArchitecture() const override;
    [[nodiscard]] QStringList debugRegisterLines() const override;
    [[nodiscard]] QString disassemble(std::uint32_t address) const override;
    [[nodiscard]] int disassembleBytes(std::uint32_t address) const override;
    [[nodiscard]] cpu::m68k::M68kCpuCore::RegisterSnapshot cpuRegisters() const { return m_cpu.registers(); }
    [[nodiscard]] std::uint8_t debugRead8(std::uint32_t address) const override;
    [[nodiscard]] std::uint16_t debugRead16(std::uint32_t address) const override;
    [[nodiscard]] std::uint32_t debugRead32(std::uint32_t address) const override;
    void debugWrite8(std::uint32_t address, std::uint8_t value) override;
    void debugWrite16(std::uint32_t address, std::uint16_t value) override;
    void debugWrite32(std::uint32_t address, std::uint32_t value) override;
    [[nodiscard]] std::uint32_t debugTranslate(std::uint32_t logical) const override
    {
        return m_cpu.translateForDebug(logical);
    }
    void debugFlushTranslationCache() override { m_cpu.flushTranslationCache(); }

    [[nodiscard]] debug::MachineSnapshot debugSnapshot() const override;

private:
    [[nodiscard]] bool isIo(std::uint32_t address) const;
    [[nodiscard]] bool isDafbRegister(std::uint32_t address) const;
    [[nodiscard]] bool isDafbVram(std::uint32_t address) const;
    [[nodiscard]] std::uint8_t readIo8(std::uint32_t address);
    void writeIo8(std::uint32_t address, std::uint8_t value);
    void applyInput(const core::GuestInputEvent& event);
    void updateInterrupts();
    void updateViaInputs();
    void advanceDevices(int cpuCycles);
    void observeVia1TimerCalibrationWrite(std::uint8_t reg, std::uint8_t value);
    [[nodiscard]] std::optional<std::size_t> ramIndex(std::uint32_t address) const;
    [[nodiscard]] bool isAliasedNuBus(std::uint32_t address) const;
    void rebuildPhysicalMemoryMap();

    cpu::m68k::M68kCpuCore m_cpu;
    QVector<std::uint8_t> m_ram;
    QByteArray m_rom;
    devices::via6522::Via6522 m_via1;
    devices::via6522::Via6522 m_via2;
    devices::rtc::MacRtc m_rtc;
    devices::scc::Z8530Scc m_scc;
    devices::iwm::IwmController m_swim;
    devices::audio::AppleSoundChip m_asc;
    devices::adb::AdbTransceiver m_adbTransceiver;
    devices::scsi::ncr53c94::Ncr53c94 m_scsi;
    devices::video::DafbVideo m_dafb;
    devices::nubus::NuBusBus m_nubus;
    std::array<std::shared_ptr<devices::scsi::ScsiBlockDevice>, 7> m_scsiDisks;
    std::array<std::shared_ptr<devices::scsi::ScsiCdRomDevice>, 7> m_scsiCdRoms;
    core::MachineScheduler m_scheduler;
    core::PhysicalMemoryMap m_physicalMemoryMap;
    std::array<QString, 2> m_floppyPaths;
    bool m_romLoaded = false;
    bool m_overlay = true;
    bool m_adbIrqPending = false;
    bool m_hostMousePositionValid = false;
    std::int16_t m_hostMouseX = 0;
    std::int16_t m_hostMouseY = 0;
    std::uint8_t m_nubusIrqState = 0xff;
    int m_viaCycleRemainder = 0;
    std::uint8_t m_via1TimerCalibrationState = 0;
};

} // namespace cutemac::machines::quadra700
