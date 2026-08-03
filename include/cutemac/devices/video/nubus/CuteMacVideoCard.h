#pragma once

#include <cstdint>
#include <array>

#include <QByteArray>

#include "cutemac/devices/nubus/NuBusCard.h"

namespace cutemac::devices::video::nubus {

class CuteMacVideoCard final : public devices::nubus::NuBusCard {
public:
    static constexpr std::uint32_t guestServicesBase = 0x000e1000;
    static constexpr std::uint32_t guestServicesCommand = guestServicesBase + 0x10;
    static constexpr std::uint32_t guestPointerBase = guestServicesBase + 0x18;

    CuteMacVideoCard(int width, int height, int depth, int vramMiB, bool acceleration, bool absolutePointer = true);

    [[nodiscard]] QString id() const override;
    void reset() override;
    void tick(std::uint64_t cycles) override;
    [[nodiscard]] std::uint8_t read8(std::uint32_t offset) override;
    void write8(std::uint32_t offset, std::uint8_t value) override;
    [[nodiscard]] VideoFrame videoFrame() const override;
    [[nodiscard]] core::GuestPowerRequest takePowerRequest() override;
    [[nodiscard]] const QByteArray& declarationRom() const { return m_declarationRom; }
    [[nodiscard]] bool absolutePointerEnabled() const { return m_absolutePointer; }
    [[nodiscard]] bool absolutePointerActive() const { return m_absolutePointer && m_vblEnabled; }
    void setHostPointerPosition(std::int16_t x, std::int16_t y);
    void acceleratedVramCopy(std::uint32_t sourceOffset, std::uint32_t destinationOffset,
        std::uint32_t sourceStrideBytes, std::uint32_t destinationStrideBytes,
        std::uint32_t widthBytes, std::uint32_t height, bool backward);
    void acceleratedVramFill(std::uint32_t destinationOffset, std::uint32_t destinationStrideBytes,
        std::uint32_t widthBytes, std::uint32_t height, std::uint8_t value);
    void acceleratedMonochromeExpand(std::uint32_t sourceOffset, std::uint32_t destinationOffset,
        std::uint32_t sourceStrideBytes, std::uint32_t destinationStrideBytes,
        std::uint32_t sourceBitOffset, std::uint32_t widthPixels, std::uint32_t height,
        std::uint32_t destinationDepth, std::uint8_t foreground, std::uint8_t background);

private:
    [[nodiscard]] static QByteArray buildDeclarationRom(int width, int height, int vramBytes, int maximumDepth);
    [[nodiscard]] int strideBytes() const;
    [[nodiscard]] ChannelLayout channelLayout() const;
    void initializePalette();

    int m_width;
    int m_height;
    int m_depth;
    int m_maximumDepth;
    bool m_acceleration;
    bool m_absolutePointer;
    QByteArray m_vram;
    QByteArray m_declarationRom;
    QVector<std::uint32_t> m_palette;
    int m_paletteAddress = 0;
    std::array<std::uint8_t, 3> m_paletteLatch {};
    bool m_vblEnabled = false;
    std::uint64_t m_vblCycles = 0;
    std::int16_t m_hostPointerX = 0;
    std::int16_t m_hostPointerY = 0;
    std::uint8_t m_hostPointerSequence = 0;
    bool m_hostPointerValid = false;
    core::GuestPowerRequest m_powerRequest = core::GuestPowerRequest::None;
    core::GuestPowerRequest m_deferredPowerRequest = core::GuestPowerRequest::None;
    std::uint64_t m_powerRequestDelayCycles = 0;
};

} // namespace cutemac::devices::video::nubus
