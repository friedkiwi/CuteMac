#pragma once

#include <array>
#include <cstdint>

#include <QByteArray>
#include <QString>

#include "cutemac/devices/nubus/NuBusCard.h"

namespace cutemac::devices::video::nubus {

class AppleDisplayCard final : public devices::nubus::NuBusCard {
public:
    enum class Variant {
        MacintoshDisplayCard824,
    };

    enum class Monitor {
        Rgb21Inch = 0x00,
        Rgb12Inch = 0x02,
        NtscMonitor = 0x04,
        HiResRgb = 0x06,
        PalEncoder = 0x0a,
        NtscEncoder = 0x0b,
        Rgb16Inch = 0x0d,
        PalMonitor = 0x1e,
    };

    static constexpr int declarationRomBytes = 32 * 1024;
    static constexpr int mappedDeclarationRomBytes = declarationRomBytes * 4;
    static constexpr int vram512KiB = 512 * 1024;
    static constexpr int vram1MiB = 1024 * 1024;

    explicit AppleDisplayCard(Variant variant, int vramKiB = 1024, Monitor monitor = Monitor::HiResRgb);

    [[nodiscard]] QString id() const override;
    [[nodiscard]] bool loadDeclarationRom(const QString& path);
    [[nodiscard]] bool declarationRomLoaded() const { return m_declarationRom.size() == mappedDeclarationRomBytes; }
    void reset() override;
    void tick(std::uint64_t cycles) override;
    [[nodiscard]] std::uint8_t read8(std::uint32_t offset) override;
    void write8(std::uint32_t offset, std::uint8_t value) override;
    void write16(std::uint32_t offset, std::uint16_t value) override;
    void write32(std::uint32_t offset, std::uint32_t value) override;
    [[nodiscard]] VideoFrame videoFrame() const override;

    [[nodiscard]] int vramBytes() const { return m_vram.size(); }
    [[nodiscard]] std::uint16_t control() const { return m_control; }
    [[nodiscard]] std::uint8_t ramdacMode() const { return m_ramdacMode; }
    [[nodiscard]] int unsupportedInterlaceSelections() const { return m_unsupportedInterlaceSelections; }

private:
    [[nodiscard]] std::uint8_t readRegisterByte(std::uint32_t local);
    void writeRegister(std::uint32_t local, std::uint32_t data);
    void writeJmfb(std::uint32_t offset, std::uint32_t data);
    [[nodiscard]] std::uint32_t readJmfb(std::uint32_t offset) const;
    void writeCrtc(std::uint32_t offset, std::uint32_t data);
    [[nodiscard]] std::uint32_t readCrtc(std::uint32_t offset) const;
    void writeRamdac(std::uint32_t offset, std::uint32_t data);
    [[nodiscard]] std::uint32_t readRamdac(std::uint32_t offset) const;
    void writeClockGenerator(std::uint32_t offset, std::uint32_t data);
    void updateRaster();
    [[nodiscard]] std::uint32_t packedRgbRead32(std::uint32_t local) const;
    void packedRgbWrite32(std::uint32_t local, std::uint32_t value);
    [[nodiscard]] int indexedDepth() const;
    [[nodiscard]] int indexedStrideBytes() const;
    [[nodiscard]] int framebufferOffsetBytes() const;
    [[nodiscard]] bool directRgbActive() const;
    [[nodiscard]] bool unsupportedInterlacedMode() const;

    Variant m_variant;
    Monitor m_monitor;
    QByteArray m_declarationRom;
    QByteArray m_vram;
    QVector<std::uint32_t> m_palette;
    std::array<std::uint8_t, 3> m_paletteLatch {};
    std::uint16_t m_control = 0;
    std::uint16_t m_preload = 0;
    std::uint32_t m_base = 0;
    std::uint32_t m_stride = 0;
    std::uint8_t m_clutAddress = 0;
    std::uint8_t m_clutComponent = 0;
    std::uint8_t m_ramdacMode = 0;
    std::uint8_t m_ramdacConvolution = 0;
    std::uint16_t m_hhalf = 0;
    std::uint16_t m_hactive = 0;
    std::uint16_t m_hbporch = 0;
    std::uint16_t m_hsync = 0;
    std::uint16_t m_hfporch = 0;
    std::uint16_t m_vactive = 0;
    std::uint16_t m_vbporch = 0;
    std::uint16_t m_vsync = 0;
    std::uint16_t m_vfporch = 0;
    std::uint16_t m_halflinePixels = 0;
    std::uint16_t m_multiplier = 0;
    std::uint16_t m_modulus = 0;
    std::uint8_t m_pixelClockDivider = 0;
    bool m_vblDisabled = true;
    std::uint64_t m_vblCycles = 0;
    std::uint64_t m_frameCyclePosition = 0;
    int m_width = 640;
    int m_height = 480;
    int m_visibleTop = 0;
    int m_visibleLeft = 0;
    int m_unsupportedInterlaceSelections = 0;
};

} // namespace cutemac::devices::video::nubus
