#pragma once

#include <array>
#include <cstdint>

#include <QByteArray>
#include <QString>

#include "cutemac/devices/nubus/NuBusCard.h"

namespace cutemac::devices::video::nubus {

class MacintoshIIVideoCard final : public devices::nubus::NuBusCard {
public:
    static constexpr int declarationRomBytes = 4096;
    static constexpr int vramBytes = 512 * 1024;

    MacintoshIIVideoCard();

    [[nodiscard]] QString id() const override;
    [[nodiscard]] bool loadDeclarationRom(const QString& path);
    [[nodiscard]] bool declarationRomLoaded() const { return m_declarationRom.size() == declarationRomBytes; }
    void reset() override;
    void tick(std::uint64_t cycles) override;
    [[nodiscard]] std::uint8_t read8(std::uint32_t offset) override;
    void write8(std::uint32_t offset, std::uint8_t value) override;
    [[nodiscard]] VideoFrame videoFrame() const override;

private:
    void writeRamdac(std::uint32_t offset, std::uint8_t value);
    [[nodiscard]] std::uint8_t readRamdac(std::uint32_t offset) const;
    void updateMode();

    QByteArray m_declarationRom;
    QByteArray m_vram;
    std::array<std::uint8_t, 16> m_tfbRegisters {};
    QVector<std::uint32_t> m_palette;
    int m_paletteAddress = 0;
    int m_paletteComponent = 0;
    std::array<std::uint8_t, 3> m_paletteLatch {};
    int m_mode = 0;
    int m_width = 640;
    int m_height = 480;
    bool m_vblEnabled = false;
    std::uint64_t m_vblCycles = 0;
};

} // namespace cutemac::devices::video::nubus
