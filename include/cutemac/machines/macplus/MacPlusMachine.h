#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cutemac/cpu/m68k/M68kBus.h"
#include "cutemac/core/IMachine.h"
#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/core/MachineScheduler.h"
#include "cutemac/core/BusTransaction.h"
#include "cutemac/cpu/m68k/M68kCpuCore.h"
#include "cutemac/devices/bus/ByteWideMmioAdapter.h"
#include "cutemac/devices/iwm/IwmController.h"
#include "cutemac/devices/rtc/MacRtc.h"
#include "cutemac/devices/scc/Z8530Scc.h"
#include "cutemac/devices/scsi/ScsiBlockDevice.h"
#include "cutemac/devices/scsi/ScsiCdRomDevice.h"
#include "cutemac/devices/scsi/ncr5380/MacintoshNcr5380Bus.h"
#include "cutemac/devices/scsi/ncr5380/Ncr5380.h"
#include "cutemac/devices/via6522/Via6522.h"

namespace cutemac::machines::macplus {

class MacPlusMachine final : public core::IMachine, public core::IDebugCpuAccess, public cpu::m68k::M68kBus {
public:
    struct BusAccess {
        QString operation;
        QString region;
        std::uint32_t address = 0;
        std::uint32_t value = 0;
        std::uint8_t size = 0;
    };

    struct AccessSummary {
        std::uint64_t ramReads = 0;
        std::uint64_t ramWrites = 0;
        std::uint64_t romReads = 0;
        std::uint64_t sccReads = 0;
        std::uint64_t sccWrites = 0;
        std::uint64_t iwmReads = 0;
        std::uint64_t iwmWrites = 0;
        std::uint64_t viaReads = 0;
        std::uint64_t viaWrites = 0;
        std::uint64_t scsiReads = 0;
        std::uint64_t scsiWrites = 0;
        std::uint64_t configurationReads = 0;
        std::uint64_t unmappedReads = 0;
        std::uint64_t unmappedWrites = 0;
    };

    struct RomInfo {
        QString path;
        std::uint32_t size = 0;
        std::uint32_t checksum = 0;
        std::uint32_t resetStackPointer = 0;
        std::uint32_t resetProgramCounter = 0;
        QString sha256;
        QStringList appliedPatches;
        QString patchError;
        bool loaded = false;
    };

    explicit MacPlusMachine(std::size_t ramSize = 4 * 1024 * 1024, const QString& nvramPath = {});

    [[nodiscard]] QString machineId() const override;
    [[nodiscard]] bool loadRomFile(const QString& path, const QStringList& enabledPatches = {}) override;
    [[nodiscard]] bool loadDiskImage(const QString& path) override;
    void ejectDiskImage() override;
    [[nodiscard]] bool loadScsiDisk(int id, const QString& path, bool readOnly) override;
    [[nodiscard]] bool loadScsiCdRom(int id, const QString& path) override;
    void ejectScsiCdRom(int id) override;
    void ejectScsiDevice(int id) override;
    [[nodiscard]] bool loadFloppyImage(const QString& path, bool readOnly = false) override;
    void ejectFloppyImage() override;
    void attachSerialEndpoint(int channel, std::shared_ptr<devices::serial::SerialEndpoint> endpoint) override;
    void reset() override;

    [[nodiscard]] int runCycles(int cycles) override;
    [[nodiscard]] std::uint64_t cycleCount() const override;
    void queueInput(const core::GuestInputEvent& event, std::uint64_t cycle) override;
    [[nodiscard]] QString debugCpuArchitecture() const override;
    [[nodiscard]] QStringList debugRegisterLines() const override;
    [[nodiscard]] int stepInstruction() override;
    [[nodiscard]] bool runUntilPc(std::uint32_t address, int maxCycles);

    [[nodiscard]] std::uint32_t programCounter() const override;
    [[nodiscard]] cpu::m68k::M68kCpuCore::RegisterSnapshot cpuRegisters() const;
    [[nodiscard]] QString disassemble(std::uint32_t address) const override;
    [[nodiscard]] int disassembleBytes(std::uint32_t address) const override;
    [[nodiscard]] bool overlayEnabled() const override;
    [[nodiscard]] const AccessSummary& accessSummary() const;
    [[nodiscard]] const QVector<QString>& eventLog() const;
    [[nodiscard]] QVector<BusAccess> busTrace() const;
    void clearBusTrace();
    void setBusTraceEnabled(bool enabled);

    [[nodiscard]] std::uint8_t debugRead8(std::uint32_t address) const override;
    [[nodiscard]] std::uint16_t debugRead16(std::uint32_t address) const override;
    [[nodiscard]] std::uint32_t debugRead32(std::uint32_t address) const override;
    void debugWrite8(std::uint32_t address, std::uint8_t value) override;
    void debugWrite16(std::uint32_t address, std::uint16_t value) override;
    void debugWrite32(std::uint32_t address, std::uint32_t value) override;

    [[nodiscard]] QByteArray framebufferBytes() const override;
    [[nodiscard]] devices::video::VideoFrame videoFrame() const override;
    [[nodiscard]] devices::audio::AudioFrame takeAudioFrame() override;
    [[nodiscard]] bool audioPlaybackActive() const override;
    [[nodiscard]] std::uint32_t framebufferHash() const;
    [[nodiscard]] QByteArray soundBufferBytes() const;
    [[nodiscard]] std::uint32_t soundBufferHash() const;
    [[nodiscard]] QByteArray soundCaptureBytes() const;
    [[nodiscard]] std::uint32_t soundCaptureHash() const;
    void clearSoundCapture();
    void setSoundCaptureEnabled(bool enabled);
    [[nodiscard]] RomInfo romInfo() const;
    [[nodiscard]] QString diskImagePath() const;
    [[nodiscard]] QString floppyImagePath() const;
    [[nodiscard]] devices::scsi::ncr5380::Ncr5380::DebugState scsiDebugState() const;
    [[nodiscard]] devices::iwm::IwmController::DebugState iwmDebugState() const;
    [[nodiscard]] QByteArray floppyTrackBytesForDebug(int track, int side) const;
    void setIwmTraceEnabled(bool enabled);
    void clearIwmTrace();
    [[nodiscard]] QStringList iwmTraceEvents() const;
    [[nodiscard]] QByteArray iwmLastNibblesForDebug() const;
    [[nodiscard]] devices::via6522::Via6522::DebugState viaDebugState() const;

    void setMousePosition(std::int16_t x, std::int16_t y);
    void moveMouse(std::int16_t dx, std::int16_t dy);
    void setMouseButton(bool pressed);
    void setKeyState(std::uint8_t macKeyCode, bool pressed);
    void resetKeyboard();
    [[nodiscard]] std::int16_t mouseX() const;
    [[nodiscard]] std::int16_t mouseY() const;
    [[nodiscard]] bool mouseButtonPressed() const;
    [[nodiscard]] QByteArray keyMapBytes() const;

    [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
    [[nodiscard]] std::uint16_t read16(std::uint32_t address) override;
    [[nodiscard]] std::uint32_t read32(std::uint32_t address) override;

    void write8(std::uint32_t address, std::uint8_t value) override;
    void write16(std::uint32_t address, std::uint16_t value) override;
    void write32(std::uint32_t address, std::uint32_t value) override;

private:
    enum class Region {
        Ram,
        Rom,
        Scc,
        Iwm,
        Via,
        Scsi,
        Configuration,
        Unmapped,
    };

    [[nodiscard]] Region regionFor(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t ramOffset(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t romOffset(std::uint32_t address) const;

    [[nodiscard]] std::uint8_t readDevice8(std::uint32_t address, Region region);
    void writeDevice8(std::uint32_t address, Region region, std::uint8_t value);
    [[nodiscard]] core::BusResponse accessDevice(const core::BusTransaction& transaction, Region region);

    void updateInterrupts();
    [[nodiscard]] std::uint32_t readRam32Direct(std::uint32_t address) const;
    [[nodiscard]] std::uint8_t readRam8Direct(std::uint32_t address) const;
    [[nodiscard]] std::uint16_t readRam16Direct(std::uint32_t address) const;
    void writeRam8Direct(std::uint32_t address, std::uint8_t value);
    void writeRam16Direct(std::uint32_t address, std::uint16_t value);
    void writeRam32Direct(std::uint32_t address, std::uint32_t value);
    void synchronizeMouseLowMemory();
    void applyInput(const core::GuestInputEvent& event);

    void setOverlayEnabled(bool enabled);
    void logEvent(const QString& message);
    void logAccess(const char* operation, std::uint32_t address, std::uint32_t value);
    void recordBusAccess(const char* operation, Region region, std::uint32_t address, std::uint32_t value, std::uint8_t size);
    void recordSoundBufferWrite(std::uint32_t address, std::uint8_t value);
    void advanceAudio(int cycles);
    [[nodiscard]] std::uint32_t soundBufferBase(bool mainPage) const;
    [[nodiscard]] QString regionName(Region region) const;
    [[nodiscard]] std::uint32_t readRom32Direct(std::uint32_t offset) const;

    cpu::m68k::M68kCpuCore m_cpu;
    QVector<std::uint8_t> m_ram;
    std::array<std::uint8_t, 128 * 1024> m_rom {};

    devices::via6522::Via6522 m_via;
    devices::rtc::MacRtc m_rtc;
    devices::scc::Z8530Scc m_scc;
    devices::iwm::IwmController m_iwm;
    devices::scsi::ncr5380::Ncr5380 m_scsi;
    devices::scsi::ncr5380::MacintoshNcr5380Bus m_scsiBus;
    devices::bus::ByteWideMmioAdapter m_sccBus;
    devices::bus::ByteWideMmioAdapter m_iwmBus;
    devices::bus::ByteWideMmioAdapter m_viaBus;
    std::array<std::shared_ptr<devices::scsi::ScsiBlockDevice>, 7> m_scsiDisks;
    std::array<std::shared_ptr<devices::scsi::ScsiCdRomDevice>, 7> m_scsiCdRoms;

    bool m_romLoaded = false;
    QString m_romPath;
    QString m_romSha256;
    QStringList m_appliedRomPatches;
    QString m_romPatchError;
    QString m_diskImagePath;
    std::int16_t m_mouseX = 15;
    std::int16_t m_mouseY = 15;
    bool m_mouseButtonPressed = false;
    bool m_overlayEnabled = true;
    AccessSummary m_accessSummary;
    QVector<QString> m_eventLog;
    QVector<BusAccess> m_busTrace;
    QByteArray m_soundCapture;
    QByteArray m_pendingAudio;
    std::uint64_t m_audioCyclePhase = 0;
    std::uint32_t m_audioBufferIndex = 0;
    std::uint8_t m_viaPortA = 0x7b;
    std::uint8_t m_soundVolume = 3;
    bool m_soundEnabled = false;
    bool m_audioPlaybackActive = false;
    bool m_busTraceEnabled = false;
    bool m_soundCaptureEnabled = false;
    core::MachineScheduler m_scheduler;
    std::uint64_t m_lastMouseButtonCycle = 0;
    bool m_queuedMouseButtonPressed = false;
};

} // namespace cutemac::machines::macplus
