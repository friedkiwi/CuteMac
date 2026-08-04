#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include <QByteArray>
#include <QVector>

#include "cutemac/devices/scsi/ncr53c94/Ncr53c94.h"
#include "cutemac/devices/video/VideoFrame.h"

namespace cutemac::devices::video {

class DafbVideo {
public:
    enum class Variant {
        Discrete,
        Quadra950,
        Memc,
        MemcJr,
    };

    enum class Monitor {
        Rgb21Inch = 0x00,
        PortraitMono15Inch = 0x01,
        Rgb12Inch = 0x02,
        TwoPageMono21Inch = 0x03,
        NtscEncoder = 0x04,
        PortraitRgb15Inch = 0x05,
        HiResRgb = 0x06,
        None = 0x07,
        Vga = 0x76,
        Rgb16Inch = 0x79,
        PalEncoder = 0x40,
        PalMonitor = 0x70,
        Rgb19Inch = 0x7e,
    };

    using IrqCallback = std::function<void(bool)>;

    explicit DafbVideo(Variant variant = Variant::Discrete, Monitor monitor = Monitor::HiResRgb);

    void reset();
    void tick(std::uint64_t cycles);
    void setIrqCallback(IrqCallback callback) { m_irqCallback = std::move(callback); }
    void attachTurboScsi(int bus, scsi::ncr53c94::Ncr53c94* controller);
    void setTurboScsiDrq(int bus, bool asserted);

    [[nodiscard]] std::uint8_t readRegister8(std::uint32_t offset);
    [[nodiscard]] std::uint16_t readRegister16(std::uint32_t offset);
    [[nodiscard]] std::uint32_t readRegister32(std::uint32_t offset);
    void writeRegister8(std::uint32_t offset, std::uint8_t value);
    void writeRegister16(std::uint32_t offset, std::uint16_t value);
    void writeRegister32(std::uint32_t offset, std::uint32_t value);

    [[nodiscard]] std::uint8_t readVram8(std::uint32_t offset) const;
    [[nodiscard]] std::uint16_t readVram16(std::uint32_t offset) const;
    [[nodiscard]] std::uint32_t readVram32(std::uint32_t offset) const;
    void writeVram8(std::uint32_t offset, std::uint8_t value);
    void writeVram16(std::uint32_t offset, std::uint16_t value);
    void writeVram32(std::uint32_t offset, std::uint32_t value);

    [[nodiscard]] std::uint8_t readTurboScsiRegister(int bus, std::uint32_t offset);
    void writeTurboScsiRegister(int bus, std::uint32_t offset, std::uint8_t value);
    [[nodiscard]] std::uint16_t readTurboScsiDma16(int bus);
    void writeTurboScsiDma16(int bus, std::uint16_t value);

    [[nodiscard]] VideoFrame videoFrame() const;
    [[nodiscard]] bool interruptActive() const { return m_interruptStatus != 0; }
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] std::uint8_t mode() const { return m_mode; }
    [[nodiscard]] std::uint32_t base() const { return m_base; }
    [[nodiscard]] std::uint32_t stride() const { return m_stride; }
    [[nodiscard]] std::uint32_t config() const { return m_config; }
    [[nodiscard]] std::uint32_t testRegister() const { return m_test; }
    [[nodiscard]] std::size_t vramSize() const { return static_cast<std::size_t>(m_vram.size()); }

private:
    struct MonitorInfo {
        bool mono = false;
        std::array<std::uint8_t, 4> sense {};
    };

    [[nodiscard]] std::uint32_t readDafb(std::uint32_t registerOffset);
    void writeDafb(std::uint32_t registerOffset, std::uint32_t value);
    [[nodiscard]] std::uint32_t readSwatch(std::uint32_t registerOffset);
    void writeSwatch(std::uint32_t registerOffset, std::uint32_t value);
    [[nodiscard]] std::uint32_t readRamdac(std::uint32_t registerOffset);
    void writeRamdac(std::uint32_t registerOffset, std::uint32_t value);
    [[nodiscard]] std::uint32_t readClockGenerator(std::uint32_t registerOffset) const;
    void writeClockGenerator(std::uint32_t registerOffset, std::uint32_t value);
    [[nodiscard]] std::uint32_t readBlock(std::uint32_t offset);
    void writeBlock(std::uint32_t offset, std::uint32_t value);
    [[nodiscard]] std::uint8_t monitorSense() const;
    [[nodiscard]] const MonitorInfo& monitorInfo() const;
    void updateMode();
    void setInterrupt(std::uint8_t mask, bool asserted);
    void recalcIrq();
    [[nodiscard]] bool displayEnabled() const { return (m_swatchMode & 1U) == 0; }
    [[nodiscard]] bool directMode() const { return m_mode == 4 || m_mode == 5; }
    [[nodiscard]] int indexedDepth() const;
    [[nodiscard]] std::uint32_t scanoutStrideBytes() const;
    [[nodiscard]] std::uint32_t scanoutBaseBytes() const;

    Variant m_variant = Variant::Discrete;
    Monitor m_monitor = Monitor::HiResRgb;
    QByteArray m_vram;
    QVector<std::uint32_t> m_palette;
    std::array<std::uint8_t, 3> m_paletteLatch {};
    std::array<std::uint32_t, 10> m_horizontal {};
    std::array<std::uint32_t, 7> m_vertical {};
    std::array<std::uint16_t, 2> m_scsiControl {};
    std::array<bool, 2> m_scsiDrq {};
    std::array<scsi::ncr53c94::Ncr53c94*, 2> m_scsi {};
    IrqCallback m_irqCallback;
    std::uint64_t m_vblCycles = 0;
    std::uint32_t m_base = 0;
    std::uint32_t m_stride = 1024;
    std::uint32_t m_config = 0;
    std::uint32_t m_test = 0;
    std::uint32_t m_blockControl = 0;
    std::uint32_t m_swatchTest = 0;
    std::uint8_t m_swatchMode = 1;
    std::uint8_t m_monitorDrive = 0;
    std::uint8_t m_paletteAddress = 0;
    std::uint8_t m_paletteComponent = 0;
    std::uint8_t m_pixelBusControl = 0;
    std::uint8_t m_pixelBusControl1 = 0;
    std::uint8_t m_mode = 0;
    std::uint8_t m_interruptStatus = 0;
    int m_width = 640;
    int m_height = 480;
};

} // namespace cutemac::devices::video
