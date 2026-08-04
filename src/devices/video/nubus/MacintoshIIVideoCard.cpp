#include "cutemac/devices/video/nubus/MacintoshIIVideoCard.h"

#include <QFile>

#include <algorithm>

namespace cutemac::devices::video::nubus {

namespace {

constexpr std::uint32_t localMask = 0x000fffff;
constexpr std::uint32_t declarationRomBase = 0x00ffc000;
// IIcx CPU clock (15.6672 MHz) scaled to the card's MAME timing:
// 25.175 MHz pixel clock, 800 pixels x 525 lines per frame.
constexpr std::uint64_t cyclesPerVbl = 261379;
constexpr std::uint64_t vblankDurationCycles = (cyclesPerVbl * 45) / 525;
constexpr std::uint64_t vblankStartDelayCycles = cyclesPerVbl / 525;

int strideForMode(int mode)
{
    return 128 << (mode & 3);
}

std::uint16_t tfbColorIndex(int depth, int pixelValue)
{
    return static_cast<std::uint16_t>(pixelValue << (8 - depth));
}

QVector<std::uint32_t> scanoutPalette(const QVector<std::uint32_t>& palette, int depth)
{
    auto result = palette;
    if (!result.isEmpty()) result[tfbColorIndex(depth, 0)] = 0xffffffffU;
    const auto blackIndex = tfbColorIndex(depth, (1 << depth) - 1);
    if (blackIndex < result.size()) result[blackIndex] = 0xff000000U;
    return result;
}

} // namespace

MacintoshIIVideoCard::MacintoshIIVideoCard()
    : m_vram(vramBytes, static_cast<char>(0xff))
    , m_palette(256, 0xff000000U)
{
    reset();
}

QString MacintoshIIVideoCard::id() const
{
    return QStringLiteral("nubus-video-apple-m2-630-0153");
}

bool MacintoshIIVideoCard::loadDeclarationRom(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto bytes = file.readAll();
    if (bytes.size() != declarationRomBytes) return false;
    // The 342-0008-A dump is stored backwards, with all bits inverted, on
    // NuBus byte lane 0 (descriptor 0xe1). This matches MAME's
    // install_declaration_rom("declrom", true, true) mapping.
    QByteArray reversed = bytes;
    std::reverse(reversed.begin(), reversed.end());
    const auto inverted = static_cast<std::uint8_t>(reversed[reversed.size() - 2]) == 0xff;
    auto byteLanes = static_cast<std::uint8_t>(reversed.back());
    if (inverted) byteLanes ^= 0xff;
    if (byteLanes != 0xe1) return false;

    m_declarationRom.fill(static_cast<char>(0xff), mappedDeclarationRomBytes);
    for (qsizetype index = 0; index < reversed.size(); ++index) {
        auto value = static_cast<std::uint8_t>(reversed[index]);
        if (inverted) value ^= 0xff;
        m_declarationRom[index * 4] = static_cast<char>(value);
    }
    return true;
}

void MacintoshIIVideoCard::reset()
{
    m_tfbRegisters.fill(0);
    std::fill(m_vram.begin(), m_vram.end(), static_cast<char>(0xff));
    for (int index = 0; index < m_palette.size(); ++index) {
        const auto level = static_cast<std::uint8_t>(255 - index);
        m_palette[index] = 0xff000000U | (static_cast<std::uint32_t>(level) << 16)
            | (static_cast<std::uint32_t>(level) << 8) | level;
    }
    // The card powers on in one-bit mode, where TFB expands pixel value 1 to
    // RAMDAC index 0x80. Keep the boot display legible before Mac OS programs
    // the full CLUT.
    m_palette[0x80] = 0xff000000U;
    m_paletteAddress = 0;
    m_paletteComponent = 0;
    m_mode = 0;
    m_width = 640;
    m_height = 480;
    m_vblEnabled = false;
    m_vblCycles = cyclesPerVbl;
    m_vblankCycles = 0;
    m_vblankStartCycles = 0;
    m_vblStatusReads = 0;
    m_vblAcks = 0;
    m_vblAssertions = 0;
    m_vblWriteOffsets.fill(0);
    setIrq(false);
}

void MacintoshIIVideoCard::tick(std::uint64_t cycles)
{
    m_vblankCycles = cycles >= m_vblankCycles ? 0 : m_vblankCycles - cycles;
    if (m_vblankStartCycles != 0) {
        if (cycles >= m_vblankStartCycles) {
            const auto remainder = cycles - m_vblankStartCycles;
            m_vblankStartCycles = 0;
            m_vblankCycles = remainder >= vblankDurationCycles ? 0 : vblankDurationCycles - remainder;
        } else {
            m_vblankStartCycles -= cycles;
        }
    }
    if (cycles >= m_vblCycles) {
        const auto remainder = cycles - m_vblCycles;
        m_vblCycles = cyclesPerVbl - (remainder % cyclesPerVbl);
        m_vblankStartCycles = vblankStartDelayCycles;
        if (m_vblEnabled) {
            ++m_vblAssertions;
            setIrq(true);
        }
    } else {
        m_vblCycles -= cycles;
    }
}

std::uint8_t MacintoshIIVideoCard::read8(std::uint32_t offset)
{
    if (offset >= declarationRomBase && declarationRomLoaded()) {
        return static_cast<std::uint8_t>(m_declarationRom[static_cast<qsizetype>(offset & (mappedDeclarationRomBytes - 1))]);
    }
    const auto local = offset & localMask;
    if (local < 0x80000) return static_cast<std::uint8_t>(m_vram[static_cast<qsizetype>(local)]) ^ 0xffU;
    if (local >= 0x90000 && local < 0x90020) {
        // Bt453 is connected to NuBus byte lane 0. Undriven lanes must not
        // advance its address or RGB component state during a wide read.
        return (local & 3) == 0 ? readRamdac((local - 0x90000) >> 2) : 0xff;
    }
    if (local >= 0xd0000 && local < 0xe0000) {
        ++m_vblStatusReads;
        // MAME maps vbl_r on the low byte of the 32-bit NuBus word only.
        if ((local & 3) != 3) return 0xff;
        return m_vblankCycles != 0 ? 0x00 : 0xff;
    }
    return 0xff;
}

void MacintoshIIVideoCard::write8(std::uint32_t offset, std::uint8_t value)
{
    const auto local = offset & localMask;
    if (local < 0x80000) {
        m_vram[static_cast<qsizetype>(local)] = static_cast<char>(value ^ 0xffU);
    } else if (local >= 0x80000 && local < 0x90000) {
        const auto index = static_cast<std::size_t>(((local - 0x80000) >> 2) & 0x0f);
        // TFB is a 32-bit handler in the original card. For byte/word cycles,
        // undriven low data lanes read high before the board-wide inversion.
        // Preserve that behavior even though the machine bus decomposes wider
        // transfers into byte callbacks.
        if ((local & 3) == 1 || (local & 3) == 2) value = 0x00;
        m_tfbRegisters[index] = value ^ 0xffU;
        if (index == 15) updateMode();
    } else if (local >= 0x90000 && local < 0x90020) {
        // Bt453 is physically connected to NuBus byte lane 0 only.
        if ((local & 3) == 0) writeRamdac((local - 0x90000) >> 2, value);
    } else if (local >= 0xa0000 && local < 0xb0000) {
        ++m_vblWriteOffsets[local & 0x1f];
        if ((local & 0x04) != 0) {
            m_vblEnabled = false;
        } else {
            m_vblEnabled = true;
            ++m_vblAcks;
            setIrq(false);
        }
    }
}

void MacintoshIIVideoCard::write32(std::uint32_t offset, std::uint32_t value)
{
    const auto local = offset & localMask;
    if (local < 0x80000) {
        for (int lane = 0; lane < 4; ++lane) {
            m_vram[static_cast<qsizetype>(local + lane)] = static_cast<char>((value >> (24 - lane * 8)) ^ 0xffU);
        }
    } else if (local >= 0x80000 && local < 0x90000) {
        const auto index = static_cast<std::size_t>(((local - 0x80000) >> 2) & 0x0f);
        m_tfbRegisters[index] = static_cast<std::uint8_t>(value ^ 0xffffffffU);
        if (index == 15) updateMode();
    } else if (local >= 0x90000 && local < 0x90020) {
        writeRamdac((local - 0x90000) >> 2, static_cast<std::uint8_t>(value >> 24));
    } else if (local >= 0xa0000 && local < 0xb0000) {
        write8(local, static_cast<std::uint8_t>(value >> 24));
    }
}

VideoFrame MacintoshIIVideoCard::videoFrame() const
{
    const auto stride = strideForMode(m_mode);
    const auto depth = 1 << (m_mode & 3);
    const auto bytes = std::min(m_vram.size() - 0x20, static_cast<qsizetype>(stride * m_height));
    QVector<std::uint16_t> mapping(1 << depth);
    for (int value = 0; value < mapping.size(); ++value) mapping[value] = tfbColorIndex(depth, value);
    return {
        m_width,
        m_height,
        stride,
        PixelStorage::Indexed,
        depth,
        ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst,
        m_vram.mid(0x20, bytes),
        scanoutPalette(m_palette, depth),
        mapping,
        {},
        true,
    };
}

void MacintoshIIVideoCard::writeRamdac(std::uint32_t offset, std::uint8_t value)
{
    value ^= 0xffU;
    switch (offset & 3) {
    case 1:
    case 3:
        m_paletteAddress = value;
        m_paletteComponent = 0;
        break;
    case 2:
        m_paletteLatch[static_cast<std::size_t>(m_paletteComponent++)] = value;
        if (m_paletteComponent == 3) {
            m_palette[m_paletteAddress] = 0xff000000U
                | (static_cast<std::uint32_t>(m_paletteLatch[0]) << 16)
                | (static_cast<std::uint32_t>(m_paletteLatch[1]) << 8)
                | m_paletteLatch[2];
            m_paletteAddress = (m_paletteAddress + 1) & 0xff;
            m_paletteComponent = 0;
        }
        break;
    default:
        break;
    }
}

std::uint8_t MacintoshIIVideoCard::readRamdac(std::uint32_t offset)
{
    if ((offset & 3) == 1 || (offset & 3) == 3) return static_cast<std::uint8_t>(m_paletteAddress) ^ 0xffU;
    if ((offset & 3) == 2) {
        const auto color = m_palette[m_paletteAddress];
        const auto shift = 16 - (m_paletteComponent * 8);
        const auto value = static_cast<std::uint8_t>(color >> shift);
        if (++m_paletteComponent == 3) {
            m_paletteComponent = 0;
            m_paletteAddress = (m_paletteAddress + 1) & 0xff;
        }
        return value ^ 0xffU;
    }
    return 0xff;
}

void MacintoshIIVideoCard::updateMode()
{
    m_mode = (m_tfbRegisters[15] >> 4) & 3;
    // The 630-0153 is a fixed 640x480 card. The TFB timing registers describe
    // blanking and pixel-clock timing as well as the visible raster; treating
    // arbitrary intermediate register programming as a new framebuffer size
    // makes Finder's depth switch publish a transient, garbled 1412x808 frame.
    m_width = 640;
    m_height = 480;
}

} // namespace cutemac::devices::video::nubus
