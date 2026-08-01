#include "cutemac/devices/video/nubus/MacintoshIIVideoCard.h"

#include <QFile>

#include <algorithm>

namespace cutemac::devices::video::nubus {

namespace {

constexpr std::uint32_t localMask = 0x000fffff;
constexpr std::uint32_t declarationRomBase = 0x00f00000;
constexpr std::uint64_t cyclesPerVbl = 260608;

int strideForMode(int mode)
{
    return 128 << (mode & 3);
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
    m_declarationRom = bytes;
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
    setIrq(false);
}

void MacintoshIIVideoCard::tick(std::uint64_t cycles)
{
    if (cycles >= m_vblCycles) {
        const auto remainder = cycles - m_vblCycles;
        m_vblCycles = cyclesPerVbl - (remainder % cyclesPerVbl);
        if (m_vblEnabled) setIrq(true);
    } else {
        m_vblCycles -= cycles;
    }
}

std::uint8_t MacintoshIIVideoCard::read8(std::uint32_t offset)
{
    if (offset >= declarationRomBase && declarationRomLoaded()) {
        return static_cast<std::uint8_t>(m_declarationRom[static_cast<qsizetype>(offset & (declarationRomBytes - 1))]);
    }
    const auto local = offset & localMask;
    if (local < 0x80000) return static_cast<std::uint8_t>(m_vram[static_cast<qsizetype>(local)]) ^ 0xffU;
    if (local >= 0x90000 && local < 0x90020) return readRamdac((local - 0x90000) >> 2);
    if (local >= 0xd0000 && local < 0xe0000) return m_vblCycles < (cyclesPerVbl / 12) ? 0x00 : 0xff;
    return 0xff;
}

void MacintoshIIVideoCard::write8(std::uint32_t offset, std::uint8_t value)
{
    const auto local = offset & localMask;
    if (local < 0x80000) {
        m_vram[static_cast<qsizetype>(local)] = static_cast<char>(value ^ 0xffU);
    } else if (local >= 0x80000 && local < 0x90000) {
        const auto index = static_cast<std::size_t>(((local - 0x80000) >> 2) & 0x0f);
        m_tfbRegisters[index] = value ^ 0xffU;
        if (index == 15) updateMode();
    } else if (local >= 0x90000 && local < 0x90020) {
        writeRamdac((local - 0x90000) >> 2, value);
    } else if (local >= 0xa0000 && local < 0xb0000) {
        if ((local & 0x10) != 0) {
            m_vblEnabled = false;
        } else {
            m_vblEnabled = true;
            setIrq(false);
        }
    }
}

VideoFrame MacintoshIIVideoCard::videoFrame() const
{
    const auto stride = strideForMode(m_mode);
    const auto depth = 1 << (m_mode & 3);
    const auto bytes = std::min(m_vram.size() - 0x20, static_cast<qsizetype>(stride * m_height));
    QVector<std::uint16_t> mapping(1 << depth);
    for (int value = 0; value < mapping.size(); ++value) mapping[value] = static_cast<std::uint16_t>(value << (8 - depth));
    return {
        m_width,
        m_height,
        stride,
        PixelStorage::Indexed,
        depth,
        ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst,
        m_vram.mid(0x20, bytes),
        m_palette,
        mapping,
        {},
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

std::uint8_t MacintoshIIVideoCard::readRamdac(std::uint32_t offset) const
{
    if ((offset & 3) == 1 || (offset & 3) == 3) return static_cast<std::uint8_t>(m_paletteAddress) ^ 0xffU;
    return 0xff;
}

void MacintoshIIVideoCard::updateMode()
{
    m_mode = (m_tfbRegisters[15] >> 4) & 3;
    const auto halfline = (m_tfbRegisters[12] | ((m_tfbRegisters[11] >> 7) << 8)) + 2;
    const auto hpixels = ((m_tfbRegisters[14] << 2) | ((m_tfbRegisters[13] >> 6) & 3)) + 2;
    const auto vlines = ((m_tfbRegisters[6] << 3) | ((m_tfbRegisters[5] >> 5) & 7)) + 1;
    const auto width = (halfline + hpixels) * (16 >> m_mode);
    const auto height = vlines / 2;
    if (width >= 320 && width <= 2048) m_width = width;
    if (height >= 200 && height <= 1200) m_height = height;
}

} // namespace cutemac::devices::video::nubus
