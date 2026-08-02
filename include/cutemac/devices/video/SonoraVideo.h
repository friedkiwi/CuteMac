#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <QVector>

#include "cutemac/devices/video/VideoFrame.h"

namespace cutemac::devices::video {

class SonoraVideo {
public:
    void reset();
    [[nodiscard]] std::uint8_t readControl(unsigned offset) const;
    void writeControl(unsigned offset, std::uint8_t value);
    [[nodiscard]] std::uint8_t readDac(unsigned offset) const;
    void writeDac(unsigned offset, std::uint8_t value);
    void setVramOffset(std::size_t offset) { m_vramOffset = offset; }
    [[nodiscard]] VideoFrame frame(const QVector<std::uint8_t>& ram) const;
    [[nodiscard]] bool enabled() const;

private:
    struct Mode { std::uint8_t id; int width; int height; bool sixteenBit; };
    [[nodiscard]] const Mode* activeMode() const;

    std::uint8_t m_mode = 0x9f;
    std::uint8_t m_depth = 0;
    std::uint8_t m_monitorDrive = 8;
    std::uint8_t m_test = 0;
    std::uint8_t m_paletteAddress = 0;
    std::uint8_t m_paletteComponent = 0;
    std::uint8_t m_paletteControl = 0;
    std::uint8_t m_colorKey = 0;
    std::size_t m_vramOffset = 0;
    std::array<std::array<std::uint8_t, 3>, 256> m_palette {};
};

} // namespace cutemac::devices::video
