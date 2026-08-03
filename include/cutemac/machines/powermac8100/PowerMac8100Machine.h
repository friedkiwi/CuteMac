#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <memory>

#include <QByteArray>
#include <QStringList>
#include <QVector>

#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/core/IMachine.h"
#include "cutemac/core/MachineScheduler.h"
#include "cutemac/cpu/ppc/PowerPcBus.h"
#include "cutemac/cpu/ppc/PpcCpuCore.h"
#include "cutemac/devices/cuda/CudaController.h"
#include "cutemac/devices/nubus/NuBusBus.h"
#include "cutemac/devices/scc/Z8530Scc.h"
#include "cutemac/devices/scsi/ScsiBlockDevice.h"
#include "cutemac/devices/scsi/ScsiCdRomDevice.h"
#include "cutemac/devices/scsi/ncr53c94/Ncr53c94.h"
#include "cutemac/devices/via6522/Via6522.h"
#include "cutemac/devices/video/SonoraVideo.h"

namespace cutemac::machines::powermac8100 {

class PowerMac8100Machine final : public core::IMachine, public core::IDebugCpuAccess,
                                  public cpu::ppc::PowerPcBus {
public:
    enum class BusRegion : std::uint8_t { Ram, Rom, Hmc, Amic, NuBus, MachineId, Unmapped };
    struct BusAccess {
        std::uint64_t cycle = 0;
        std::uint32_t pc = 0;
        std::uint32_t address = 0;
        std::uint32_t value = 0;
        std::uint8_t size = 0;
        bool write = false;
        BusRegion region = BusRegion::Unmapped;
    };

    explicit PowerMac8100Machine(std::size_t ramSize);

    [[nodiscard]] QString machineId() const override;
    [[nodiscard]] bool loadRomFile(const QString& path, const QStringList& patches) override;
    [[nodiscard]] bool loadDiskImage(const QString& path) override { return loadScsiDisk(0, path, false); }
    void ejectDiskImage() override {}
    [[nodiscard]] bool loadScsiDisk(int id, const QString& path, bool readOnly) override;
    [[nodiscard]] bool loadScsiCdRom(int id, const QString& path) override;
    void ejectScsiDevice(int id) override;
    void ejectScsiCdRom(int id) override;
    [[nodiscard]] bool loadFloppyImage(const QString&, bool) override { return false; }
    [[nodiscard]] bool loadFloppyImage(int, const QString&, bool) override { return false; }
    void ejectFloppyImage() override {}
    void ejectFloppyImage(int) override {}
    void reset() override;
    void attachSerialEndpoint(int channel, std::shared_ptr<devices::serial::SerialEndpoint> endpoint) override;
    [[nodiscard]] int runCycles(int cycles) override;
    [[nodiscard]] int stepInstruction() override;
    [[nodiscard]] std::uint64_t cycleCount() const override;
    [[nodiscard]] std::uint32_t programCounter() const override;
    [[nodiscard]] bool overlayEnabled() const override { return false; }
    [[nodiscard]] QByteArray framebufferBytes() const override;
    [[nodiscard]] devices::video::VideoFrame videoFrame() const override;
    void queueInput(const core::GuestInputEvent&, std::uint64_t) override {}

    [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
    [[nodiscard]] std::uint16_t read16(std::uint32_t address) override;
    [[nodiscard]] std::uint32_t read32(std::uint32_t address) override;
    void write8(std::uint32_t address, std::uint8_t value) override;
    void write16(std::uint32_t address, std::uint16_t value) override;
    void write32(std::uint32_t address, std::uint32_t value) override;
    bool handleProtectedWrite(std::uint32_t address, std::uint32_t value, unsigned size) override;
    std::optional<std::uint32_t> compatibilityRead(
        std::uint32_t address, unsigned size, std::uint32_t instructionPc) override;
    bool compatibilityWrite(std::uint32_t address, std::uint32_t value,
        unsigned size, std::uint32_t instructionPc) override;

    [[nodiscard]] QString debugCpuArchitecture() const override { return QStringLiteral("ppc:601"); }
    [[nodiscard]] QStringList debugRegisterLines() const override;
    [[nodiscard]] QString disassemble(std::uint32_t address) const override;
    [[nodiscard]] int disassembleBytes(std::uint32_t) const override { return 4; }
    [[nodiscard]] std::uint8_t debugRead8(std::uint32_t address) const override;
    [[nodiscard]] std::uint16_t debugRead16(std::uint32_t address) const override;
    [[nodiscard]] std::uint32_t debugRead32(std::uint32_t address) const override;
    void debugWrite8(std::uint32_t address, std::uint8_t value) override { write8(address, value); }
    void debugWrite16(std::uint32_t address, std::uint16_t value) override { write16(address, value); }
    void debugWrite32(std::uint32_t address, std::uint32_t value) override { write32(address, value); }

    void setBusTraceEnabled(bool enabled);
    void setCpuTraceEnabled(bool enabled);
    void setCpuTraceFreezeOnEmulatorException(bool enabled) { m_freezeCpuTraceOnEmulatorException = enabled; }
    void setVia1VblankEnabled(bool enabled) { m_via1.setAutomaticCa1Period(enabled ? 1'330'008 : 0); }
    [[nodiscard]] const std::deque<BusAccess>& busTrace() const { return m_busTrace; }
    [[nodiscard]] const std::deque<cpu::ppc::PowerPc601Core::TraceEvent>& cpuTrace() const { return m_cpuTrace; }
    [[nodiscard]] std::uint64_t unmappedAccessCount() const { return m_unmappedAccessCount; }
    [[nodiscard]] const std::array<std::uint64_t, 8>& ascCompatibilityReadCounts() const { return m_ascCompatibilityReadCounts; }
    [[nodiscard]] const std::array<std::uint32_t, 8>& ascCompatibilityReadCallers() const { return m_ascCompatibilityReadCallers; }
    [[nodiscard]] devices::cuda::CudaController::DebugState cudaDebugState() const { return m_cuda.debugState(); }
    [[nodiscard]] devices::via6522::Via6522::DebugState via1DebugState() const { return m_via1.debugState(); }
    [[nodiscard]] std::array<std::uint8_t, 5> interruptDebugState() const
    { return { m_irqControl, m_via2Ifr, m_via2Ier, m_via2SlotIfr, m_via2SlotIer }; }
    [[nodiscard]] std::array<std::uint8_t, 6> lastInterruptAssertState() const { return m_lastInterruptAssertState; }
    [[nodiscard]] std::uint64_t hmcControl() const { return m_hmcControl; }
    [[nodiscard]] const devices::scsi::ncr53c94::Ncr53c94& scsiController(bool internal) const
    { return internal ? m_scsiB : m_scsi; }
    [[nodiscard]] std::array<std::uint32_t, 4> scsiDmaDebugState() const
    { return { m_dmaBase, m_scsiDmaAddress, m_scsiDmaOffset, m_scsiDmaControl }; }
    [[nodiscard]] cpu::ppc::PowerPc601Core::RegisterSnapshot cpuRegisters() const { return m_cpu.registers(); }

private:
    [[nodiscard]] BusRegion regionFor(std::uint32_t address) const;
    [[nodiscard]] std::optional<std::size_t> ramIndex(std::uint32_t address) const;
    [[nodiscard]] std::uint8_t readMapped8(std::uint32_t address);
    void writeMapped8(std::uint32_t address, std::uint8_t value);
    void recordBus(std::uint32_t address, std::uint32_t value, unsigned size, bool write, BusRegion region);
    [[nodiscard]] std::uint8_t readHmc(std::uint32_t address);
    void writeHmc(std::uint32_t address, std::uint8_t value);
    void updateInterrupts();
    void flushDevices();
    void serviceScsiDma();

    cpu::ppc::PowerPc601Core m_cpu;
    QVector<std::uint8_t> m_ram;
    QVector<std::uint8_t> m_videoRam;
    QByteArray m_rom;
    QByteArray m_amicRegisters;
    core::MachineScheduler m_scheduler;
    devices::scc::Z8530Scc m_scc;
    devices::scsi::ncr53c94::Ncr53c94 m_scsi;
    devices::scsi::ncr53c94::Ncr53c94 m_scsiB;
    std::array<std::shared_ptr<devices::scsi::ScsiBlockDevice>, 7> m_scsiDisks {};
    std::array<std::shared_ptr<devices::scsi::ScsiCdRomDevice>, 7> m_scsiCdRoms {};
    devices::via6522::Via6522 m_via1;
    devices::cuda::CudaController m_cuda;
    devices::video::SonoraVideo m_video;
    devices::nubus::NuBusBus m_nubus;
    std::uint64_t m_hmcControl = 0;
    std::uint64_t m_hmcShiftBuffer = 0;
    unsigned m_hmcBitPosition = 0;
    bool m_hmcCommitted = false;
    bool m_compatibilityRamActive = false;
    std::uint8_t m_via2Ier = 0;
    std::uint8_t m_via2Ifr = 0;
    std::uint8_t m_via2SlotIer = 0;
    std::uint8_t m_via2SlotIfr = 0x7f;
    std::uint8_t m_irqControl = 0;
    bool m_irqLineAsserted = false;
    std::array<std::uint8_t, 6> m_lastInterruptAssertState {};
    std::uint32_t m_dmaBase = 0;
    std::uint32_t m_scsiDmaAddress = 0;
    std::uint32_t m_scsiDmaOffset = 0;
    std::uint8_t m_scsiDmaControl = 0;
    std::uint64_t m_soundDmaStartCycle = 0;
    int m_pendingDeviceCycles = 0;
    int m_videoVblankCycles = 1'330'008;
    bool m_romLoaded = false;
    bool m_busTraceEnabled = false;
    bool m_cpuTraceEnabled = false;
    bool m_cpuTraceFrozen = false;
    bool m_freezeCpuTraceOnEmulatorException = false;
    std::uint64_t m_unmappedAccessCount = 0;
    std::array<std::uint64_t, 8> m_ascCompatibilityReadCounts {};
    std::array<std::uint32_t, 8> m_ascCompatibilityReadCallers {};
    std::deque<BusAccess> m_busTrace;
    std::deque<cpu::ppc::PowerPc601Core::TraceEvent> m_cpuTrace;
};

} // namespace cutemac::machines::powermac8100
