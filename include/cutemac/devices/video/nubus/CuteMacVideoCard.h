#pragma once

#include <cstdint>

#include <QByteArray>

#include "cutemac/devices/nubus/NuBusCard.h"

namespace cutemac::devices::video::nubus {

class CuteMacVideoCard final : public devices::nubus::NuBusCard {
public:
    CuteMacVideoCard(int width, int height, int depth, int vramMiB, bool acceleration);

    [[nodiscard]] QString id() const override;
    void reset() override;
    void tick(std::uint64_t cycles) override;
    [[nodiscard]] std::uint8_t read8(std::uint32_t offset) override;
    void write8(std::uint32_t offset, std::uint8_t value) override;
    [[nodiscard]] VideoFrame videoFrame() const override;
    [[nodiscard]] const QByteArray& declarationRom() const { return m_declarationRom; }

private:
    [[nodiscard]] static QByteArray buildDeclarationRom(int width, int height, int vramBytes);
    [[nodiscard]] int strideBytes() const;
    [[nodiscard]] PixelFormat pixelFormat() const;
    void initializePalette();

    int m_width;
    int m_height;
    int m_depth;
    bool m_acceleration;
    QByteArray m_vram;
    QByteArray m_declarationRom;
    QVector<std::uint32_t> m_palette;
    bool m_vblEnabled = false;
    std::uint64_t m_vblCycles = 0;
};

} // namespace cutemac::devices::video::nubus
