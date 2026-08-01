#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include <QByteArray>
#include <QStringList>
#include <QVector>

#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/core/IMachine.h"
#include "cutemac/core/MachineScheduler.h"
#include "cutemac/cpu/ppc/PowerPcBus.h"
#include "cutemac/cpu/ppc/PpcCpuCore.h"
#include "cutemac/devices/scc/Z8530Scc.h"

namespace cutemac::machines::powermac8100 {

class PowerMac8100Machine final : public core::IMachine, public core::IDebugCpuAccess,
                                  public cpu::ppc::PowerPcBus {
public:
    enum class BusRegion : std::uint8_t { Ram, Rom, Hmc, Amic, MachineId, Unmapped };
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
    [[nodiscard]] bool loadDiskImage(const QString&) override { return false; }
    void ejectDiskImage() override {}
    [[nodiscard]] bool loadScsiDisk(int, const QString&, bool) override { return false; }
    [[nodiscard]] bool loadScsiCdRom(int, const QString&) override { return false; }
    void ejectScsiDevice(int) override {}
    void ejectScsiCdRom(int) override {}
    [[nodiscard]] bool loadFloppyImage(const QString&, bool) override { return false; }
    void ejectFloppyImage() override {}
    void reset() override;
    [[nodiscard]] int runCycles(int cycles) override;
    [[nodiscard]] int stepInstruction() override;
    [[nodiscard]] std::uint64_t cycleCount() const override;
    [[nodiscard]] std::uint32_t programCounter() const override;
    [[nodiscard]] bool overlayEnabled() const override { return false; }
    [[nodiscard]] QByteArray framebufferBytes() const override { return {}; }
    [[nodiscard]] devices::video::VideoFrame videoFrame() const override { return {}; }
    void queueInput(const core::GuestInputEvent&, std::uint64_t) override {}

    [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
    [[nodiscard]] std::uint16_t read16(std::uint32_t address) override;
    [[nodiscard]] std::uint32_t read32(std::uint32_t address) override;
    void write8(std::uint32_t address, std::uint8_t value) override;
    void write16(std::uint32_t address, std::uint16_t value) override;
    void write32(std::uint32_t address, std::uint32_t value) override;

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
    [[nodiscard]] const std::deque<BusAccess>& busTrace() const { return m_busTrace; }
    [[nodiscard]] std::uint64_t unmappedAccessCount() const { return m_unmappedAccessCount; }
    [[nodiscard]] cpu::ppc::PowerPc601Core::RegisterSnapshot cpuRegisters() const { return m_cpu.registers(); }

private:
    [[nodiscard]] BusRegion regionFor(std::uint32_t address) const;
    [[nodiscard]] std::uint8_t readMapped8(std::uint32_t address);
    void writeMapped8(std::uint32_t address, std::uint8_t value);
    void recordBus(std::uint32_t address, std::uint32_t value, unsigned size, bool write, BusRegion region);
    [[nodiscard]] std::uint8_t readHmc(std::uint32_t address);
    void writeHmc(std::uint32_t address, std::uint8_t value);

    cpu::ppc::PowerPc601Core m_cpu;
    QVector<std::uint8_t> m_ram;
    QByteArray m_rom;
    QByteArray m_amicRegisters;
    core::MachineScheduler m_scheduler;
    devices::scc::Z8530Scc m_scc;
    std::uint64_t m_hmcControl = 0;
    unsigned m_hmcBitPosition = 0;
    std::uint64_t m_soundDmaStartCycle = 0;
    bool m_romLoaded = false;
    bool m_busTraceEnabled = false;
    std::uint64_t m_unmappedAccessCount = 0;
    std::deque<BusAccess> m_busTrace;
};

} // namespace cutemac::machines::powermac8100
