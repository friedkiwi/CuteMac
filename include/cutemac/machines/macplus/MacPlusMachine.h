#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <QString>
#include <QVector>

#include "cutemac/cpu/m68k/M68kBus.h"
#include "cutemac/cpu/m68k/M68kCpuCore.h"
#include "cutemac/devices/iwm/IwmController.h"
#include "cutemac/devices/scc/Z8530Scc.h"
#include "cutemac/devices/scsi/ncr5380/Ncr5380.h"
#include "cutemac/devices/via6522/Via6522.h"

namespace cutemac::machines::macplus {

class MacPlusMachine final : public cpu::m68k::M68kBus {
public:
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
        std::uint64_t syntheticTickReads = 0;
        std::uint64_t unmappedReads = 0;
        std::uint64_t unmappedWrites = 0;
    };

    explicit MacPlusMachine(std::size_t ramSize = 4 * 1024 * 1024);

    [[nodiscard]] bool loadRomFile(const QString& path);
    void reset();

    [[nodiscard]] int runCycles(int cycles);

    [[nodiscard]] std::uint32_t programCounter() const;
    [[nodiscard]] bool overlayEnabled() const;
    [[nodiscard]] const AccessSummary& accessSummary() const;
    [[nodiscard]] const QVector<QString>& eventLog() const;

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

    void incrementLowMemoryTicks();
    [[nodiscard]] std::uint32_t readRam32Direct(std::uint32_t address) const;

    void setOverlayEnabled(bool enabled);
    void logEvent(const QString& message);
    void logAccess(const char* operation, std::uint32_t address, std::uint32_t value);

    cpu::m68k::M68kCpuCore m_cpu;
    QVector<std::uint8_t> m_ram;
    std::array<std::uint8_t, 128 * 1024> m_rom {};

    devices::via6522::Via6522 m_via;
    devices::scc::Z8530Scc m_scc;
    devices::iwm::IwmController m_iwm;
    devices::scsi::ncr5380::Ncr5380 m_scsi;

    bool m_romLoaded = false;
    bool m_overlayEnabled = true;
    AccessSummary m_accessSummary;
    QVector<QString> m_eventLog;
};

} // namespace cutemac::machines::macplus
