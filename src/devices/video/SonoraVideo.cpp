#include "cutemac/devices/video/SonoraVideo.h"

#include <algorithm>

namespace cutemac::devices::video {

void SonoraVideo::reset()
{
    m_mode = 0x9f;
    m_depth = 0;
    m_monitorDrive = 8;
    m_test = 0;
    m_paletteAddress = 0;
    m_paletteComponent = 0;
    m_paletteControl = 0;
    m_colorKey = 0;
    m_vramOffset = 0;
    for (std::size_t index = 0; index < m_palette.size(); ++index) {
        const auto level = static_cast<std::uint8_t>(index);
        m_palette[index] = { level, level, level };
    }
}

const SonoraVideo::Mode* SonoraVideo::activeMode() const
{
    static constexpr std::array<Mode, 5> modes {{
        { 0x02, 512, 384, true }, { 0x06, 640, 480, true },
        { 0x01, 640, 870, false }, { 0x09, 832, 624, false },
        { 0x0b, 640, 480, false }
    }};
    const auto id = static_cast<std::uint8_t>(m_mode & 0x1fU);
    const auto found = std::find_if(modes.begin(), modes.end(), [id](const auto& mode) { return mode.id == id; });
    return found == modes.end() ? nullptr : &*found;
}

bool SonoraVideo::enabled() const
{
    const auto* mode = activeMode();
    return !(m_mode & 0x80U) && mode && m_depth <= 4U && (m_depth != 4U || mode->sixteenBit);
}

std::uint8_t SonoraVideo::readControl(unsigned offset) const
{
    switch (offset & 7U) {
    case 0: return m_mode;
    case 1: return m_depth;
    case 2: {
        auto sense = std::uint8_t { 6 }; // 13-inch RGB sense code
        if (!(m_monitorDrive & 8U)) sense &= m_monitorDrive & 7U;
        return static_cast<std::uint8_t>(m_monitorDrive | (sense << 4));
    }
    case 3: return m_test;
    default: return 0;
    }
}

void SonoraVideo::writeControl(unsigned offset, std::uint8_t value)
{
    switch (offset & 7U) {
    case 0: m_mode = value & 0x9fU; break;
    case 1: m_depth = value & 7U; break;
    case 2: m_monitorDrive = value & 0x0fU; break;
    case 3: m_test = value & 1U; break;
    default: break;
    }
}

std::uint8_t SonoraVideo::readDac(unsigned offset) const
{
    return (offset & 3U) == 2U ? m_paletteControl : 0;
}

void SonoraVideo::writeDac(unsigned offset, std::uint8_t value)
{
    switch (offset & 3U) {
    case 0: m_paletteAddress = value; m_paletteComponent = 0; break;
    case 1:
        m_palette[m_paletteAddress][m_paletteComponent++] = value;
        if (m_paletteComponent == 3U) { m_paletteComponent = 0; ++m_paletteAddress; }
        break;
    case 2: m_paletteControl = value; break;
    case 3: m_colorKey = value; break;
    }
}

VideoFrame SonoraVideo::frame(const QVector<std::uint8_t>& ram) const
{
    VideoFrame result;
    const auto* mode = activeMode();
    if (!mode || !enabled()) return result;
    result.width = mode->width;
    result.height = mode->height;
    result.bitsPerPixel = 1 << m_depth;
    result.storage = result.bitsPerPixel <= 8 ? PixelStorage::Indexed : PixelStorage::Direct;
    result.byteOrder = ByteOrder::BigEndian;
    result.bitOrder = BitOrder::MostSignificantFirst;
    result.strideBytes = (result.width * result.bitsPerPixel + 7) / 8;
    const auto byteCount = static_cast<std::size_t>(result.strideBytes) * result.height;
    if (m_vramOffset + byteCount > static_cast<std::size_t>(ram.size())) return {};
    result.pixels = QByteArray(reinterpret_cast<const char*>(ram.constData() + m_vramOffset),
        static_cast<qsizetype>(byteCount));
    if (result.storage == PixelStorage::Indexed) {
        const auto entries = 1U << result.bitsPerPixel;
        result.colorTable.reserve(static_cast<qsizetype>(entries));
        for (unsigned value = 0; value < entries; ++value) {
            const auto paletteIndex = result.bitsPerPixel == 1 ? (value ? 127U : 255U)
                : result.bitsPerPixel == 2 ? ((value << 6) | 0x3fU)
                : result.bitsPerPixel == 4 ? ((value << 4) | 0x0fU) : value;
            const auto& color = m_palette[paletteIndex];
            result.colorTable.push_back(0xff000000U | (static_cast<std::uint32_t>(color[0]) << 16)
                | (static_cast<std::uint32_t>(color[1]) << 8) | color[2]);
        }
    } else {
        result.channels = { 0x7c00U, 0x03e0U, 0x001fU, 0 };
    }
    return result;
}

} // namespace cutemac::devices::video
