#pragma once

#include <cstdint>
#include <array>

#include <QByteArray>

#include "cutemac/devices/nubus/NuBusCard.h"

namespace cutemac::devices::video::nubus {

class CuteMacVideoCard final : public devices::nubus::NuBusCard {
public:
    static constexpr std::uint32_t guestServicesBase = 0x00090000;
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
    void setHostPointerPosition(std::int16_t x, std::int16_t y);

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
};

} // namespace cutemac::devices::video::nubus
