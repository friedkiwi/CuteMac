#include "cutemac/devices/video/DafbVideo.h"

#include <algorithm>

namespace cutemac::devices::video {
namespace {

constexpr std::uint64_t defaultVblCycles = 1'330'008;

std::uint32_t grayscaleColor(std::uint32_t color)
{
    const auto red = (color >> 16) & 0xffU;
    const auto green = (color >> 8) & 0xffU;
    const auto blue = color & 0xffU;
    const auto level = (red * 299U + green * 587U + blue * 114U + 500U) / 1000U;
    return 0xff000000U | (level << 16) | (level << 8) | level;
}

std::uint8_t ext(std::uint8_t bc, std::uint8_t ac, std::uint8_t ab)
{
    return static_cast<std::uint8_t>(0x40U | (bc << 4U) | (ac << 2U) | ab);
}

std::uint16_t byteSwap16(std::uint16_t value)
{
    return static_cast<std::uint16_t>((value << 8) | (value >> 8));
}

} // namespace

DafbVideo::DafbVideo(Variant variant, Monitor monitor)
    : m_variant(variant)
    , m_monitor(monitor)
    , m_vram(variant == Variant::Memc || variant == Variant::MemcJr
          ? 1024 * 1024 : 2 * 1024 * 1024, 0)
    , m_palette(256, 0xff000000U)
{
    reset();
}

void DafbVideo::reset()
{
    std::fill(m_vram.begin(), m_vram.end(), 0);
    std::fill(m_palette.begin(), m_palette.end(), 0xff000000U);
    if (!m_palette.isEmpty()) {
        m_palette[0] = 0xffffffffU;
        m_palette[1] = 0xff000000U;
    }
    m_horizontal.fill(0);
    m_vertical.fill(0);
    m_scsiControl.fill(0);
    m_scsiDrq.fill(false);
    m_vblCycles = defaultVblCycles;
    m_base = 0;
    m_stride = 1024;
    m_config = 0;
    m_test = 0;
    m_blockControl = 0;
    m_swatchTest = 0;
    m_swatchMode = 1;
    m_monitorDrive = 0;
    m_paletteAddress = 0;
    m_paletteComponent = 0;
    m_pixelBusControl = 0;
    m_pixelBusControl1 = 0;
    m_mode = 0;
    m_interruptStatus = 0;
    m_width = 640;
    m_height = 480;
    recalcIrq();
}

void DafbVideo::tick(std::uint64_t cycles)
{
    if ((m_swatchMode & 1U) != 0) return;
    if (cycles >= m_vblCycles) {
        m_vblCycles = defaultVblCycles;
        setInterrupt(0x01, true);
    } else {
        m_vblCycles -= cycles;
    }
}

void DafbVideo::attachTurboScsi(int bus, scsi::ncr53c94::Ncr53c94* controller)
{
    if (bus < 0 || bus >= static_cast<int>(m_scsi.size())) return;
    m_scsi[static_cast<std::size_t>(bus)] = controller;
}

void DafbVideo::setTurboScsiDrq(int bus, bool asserted)
{
    if (bus < 0 || bus >= static_cast<int>(m_scsiDrq.size())) return;
    m_scsiDrq[static_cast<std::size_t>(bus)] = asserted;
}

std::uint8_t DafbVideo::readRegister8(std::uint32_t offset)
{
    const auto value = readRegister32(offset & ~3U);
    return static_cast<std::uint8_t>(value >> ((3U - (offset & 3U)) * 8U));
}

std::uint16_t DafbVideo::readRegister16(std::uint32_t offset)
{
    return static_cast<std::uint16_t>((readRegister8(offset) << 8) | readRegister8(offset + 1));
}

std::uint32_t DafbVideo::readRegister32(std::uint32_t offset)
{
    return readBlock(offset & 0x3ffU);
}

void DafbVideo::writeRegister8(std::uint32_t offset, std::uint8_t value)
{
    writeRegister32(offset & ~3U,
        (static_cast<std::uint32_t>(value) << 24) | (static_cast<std::uint32_t>(value) << 16)
            | (static_cast<std::uint32_t>(value) << 8) | value);
}

void DafbVideo::writeRegister16(std::uint32_t offset, std::uint16_t value)
{
    writeRegister32(offset & ~3U, (static_cast<std::uint32_t>(value) << 16) | value);
}

void DafbVideo::writeRegister32(std::uint32_t offset, std::uint32_t value)
{
    writeBlock(offset & 0x3ffU, value);
}

std::uint8_t DafbVideo::readVram8(std::uint32_t offset) const
{
    if (offset >= static_cast<std::uint32_t>(m_vram.size())) return 0xff;
    return static_cast<std::uint8_t>(m_vram[static_cast<qsizetype>(offset)]);
}

std::uint16_t DafbVideo::readVram16(std::uint32_t offset) const
{
    return static_cast<std::uint16_t>((readVram8(offset) << 8) | readVram8(offset + 1));
}

std::uint32_t DafbVideo::readVram32(std::uint32_t offset) const
{
    return (static_cast<std::uint32_t>(readVram16(offset)) << 16) | readVram16(offset + 2);
}

void DafbVideo::writeVram8(std::uint32_t offset, std::uint8_t value)
{
    if (offset >= static_cast<std::uint32_t>(m_vram.size())) return;
    m_vram[static_cast<qsizetype>(offset)] = static_cast<char>(value);
}

void DafbVideo::writeVram16(std::uint32_t offset, std::uint16_t value)
{
    writeVram8(offset, static_cast<std::uint8_t>(value >> 8));
    writeVram8(offset + 1, static_cast<std::uint8_t>(value));
}

void DafbVideo::writeVram32(std::uint32_t offset, std::uint32_t value)
{
    writeVram16(offset, static_cast<std::uint16_t>(value >> 16));
    writeVram16(offset + 2, static_cast<std::uint16_t>(value));
}

std::uint8_t DafbVideo::readTurboScsiRegister(int bus, std::uint32_t offset)
{
    if (bus < 0 || bus >= static_cast<int>(m_scsi.size()) || !m_scsi[static_cast<std::size_t>(bus)]) return 0xff;
    return m_scsi[static_cast<std::size_t>(bus)]->readRegister(static_cast<std::uint8_t>((offset >> 4) & 0x0fU));
}

void DafbVideo::writeTurboScsiRegister(int bus, std::uint32_t offset, std::uint8_t value)
{
    if (bus < 0 || bus >= static_cast<int>(m_scsi.size()) || !m_scsi[static_cast<std::size_t>(bus)]) return;
    m_scsi[static_cast<std::size_t>(bus)]->writeRegister(static_cast<std::uint8_t>((offset >> 4) & 0x0fU), value);
}

std::uint16_t DafbVideo::readTurboScsiDma16(int bus)
{
    if (bus < 0 || bus >= static_cast<int>(m_scsi.size()) || !m_scsi[static_cast<std::size_t>(bus)]) return 0xffffU;
    return m_scsi[static_cast<std::size_t>(bus)]->readDmaWord();
}

void DafbVideo::writeTurboScsiDma16(int bus, std::uint16_t value)
{
    if (bus < 0 || bus >= static_cast<int>(m_scsi.size()) || !m_scsi[static_cast<std::size_t>(bus)]) return;
    m_scsi[static_cast<std::size_t>(bus)]->writeDmaWord(value);
}

VideoFrame DafbVideo::videoFrame() const
{
    if (!displayEnabled() || m_monitor == Monitor::None) return {};

    const auto base = scanoutBaseBytes();
    const auto stride = scanoutStrideBytes();
    if (base >= static_cast<std::uint32_t>(m_vram.size()) || stride == 0) return {};
    const auto bytes = std::min<qsizetype>(m_vram.size() - static_cast<qsizetype>(base),
        static_cast<qsizetype>(stride * static_cast<std::uint32_t>(m_height)));
    if (bytes <= 0) return {};
    auto pixels = m_vram.mid(static_cast<qsizetype>(base), bytes);
    if (m_mode == 4) {
        return {
            m_width, m_height, static_cast<int>(stride), PixelStorage::Direct, 24,
            ByteOrder::BigEndian, BitOrder::MostSignificantFirst,
            std::move(pixels), {}, {}, { 0xff0000, 0x00ff00, 0x0000ff, 0 }, true,
        };
    }
    if (m_mode == 5) {
        return {
            m_width, m_height, static_cast<int>(stride), PixelStorage::Direct, 15,
            ByteOrder::BigEndian, BitOrder::MostSignificantFirst,
            std::move(pixels), {}, {}, { 0x7c00, 0x03e0, 0x001f, 0 }, true,
        };
    }

    const auto depth = indexedDepth();
    QVector<std::uint16_t> mapping(1 << depth);
    for (int index = 0; index < mapping.size(); ++index) mapping[index] = static_cast<std::uint16_t>(index);
    auto palette = m_palette;
    if (monitorInfo().mono) {
        for (auto& color : palette) color = grayscaleColor(color);
    }
    return {
        m_width, m_height, static_cast<int>(stride), PixelStorage::Indexed, depth,
        ByteOrder::BigEndian, BitOrder::MostSignificantFirst,
        std::move(pixels), std::move(palette), std::move(mapping), {}, true,
    };
}

std::uint32_t DafbVideo::readDafb(std::uint32_t registerOffset)
{
    switch (registerOffset) {
    case 0x00: return (m_base >> 9) & 0x0fffU;
    case 0x04: return (m_base >> 5) & 0x0fU;
    case 0x08: return m_stride >> 2;
    case 0x0c: return 0;
    case 0x10: return m_config;
    case 0x14: return m_blockControl;
    case 0x1c: return monitorSense();
    case 0x24: return m_scsiControl[0] | (m_scsiDrq[0] ? 0x200U : 0U);
    case 0x28: return m_scsiControl[1] | (m_scsiDrq[1] ? 0x200U : 0U);
    case 0x2c: {
        std::uint32_t version = 1;
        if (m_variant == Variant::Quadra950 || m_variant == Variant::Memc || m_variant == Variant::MemcJr)
            version = 3;
        return (m_test & 0x1ffU) | (version << 9U);
    }
    default: return 0;
    }
}

void DafbVideo::writeDafb(std::uint32_t registerOffset, std::uint32_t value)
{
    value &= 0x0fffU;
    switch (registerOffset) {
    case 0x00:
        m_base = (m_base & 0x1e0U) | ((value & 0x0fffU) << 9U);
        break;
    case 0x04:
        m_base = (m_base & ~0x1e0U) | ((value & 0x0fU) << 5U);
        break;
    case 0x08:
        m_stride = value << 2U;
        updateMode();
        break;
    case 0x10:
        m_config = value;
        updateMode();
        break;
    case 0x14:
        m_blockControl = value;
        break;
    case 0x1c:
        m_monitorDrive = static_cast<std::uint8_t>((value & 0x07U) ^ 0x07U);
        break;
    case 0x24: case 0x28: {
        const auto bus = static_cast<std::size_t>((registerOffset - 0x24U) / 4U);
        m_scsiControl[bus] = static_cast<std::uint16_t>(value);
        break;
    }
    case 0x2c:
        m_test = value;
        break;
    default:
        break;
    }
}

std::uint32_t DafbVideo::readSwatch(std::uint32_t registerOffset)
{
    if (registerOffset == 0x08) return m_interruptStatus;
    if (registerOffset == 0x0c) {
        setInterrupt(0x04, false);
        return 0;
    }
    if (registerOffset == 0x14) {
        setInterrupt(0x01, false);
        return 0;
    }
    if (registerOffset == 0x20) return m_swatchTest;
    if (registerOffset >= 0x24 && registerOffset <= 0x48 && (registerOffset & 3U) == 0)
        return m_horizontal[static_cast<std::size_t>((registerOffset - 0x24U) / 4U)];
    if (registerOffset >= 0x4c && registerOffset <= 0x64 && (registerOffset & 3U) == 0)
        return m_vertical[static_cast<std::size_t>((registerOffset - 0x4cU) / 4U)];
    return 0;
}

void DafbVideo::writeSwatch(std::uint32_t registerOffset, std::uint32_t value)
{
    value &= 0x0fffU;
    if (registerOffset == 0x00) {
        m_swatchMode = static_cast<std::uint8_t>(value);
        return;
    }
    if (registerOffset == 0x04) {
        if ((value & 1U) == 0) setInterrupt(0x01, false);
        return;
    }
    if (registerOffset == 0x10) {
        m_interruptStatus = 0;
        recalcIrq();
        return;
    }
    if (registerOffset == 0x20) {
        m_swatchTest = value;
        return;
    }
    if (registerOffset >= 0x24 && registerOffset <= 0x48 && (registerOffset & 3U) == 0) {
        m_horizontal[static_cast<std::size_t>((registerOffset - 0x24U) / 4U)] = value;
        updateMode();
    } else if (registerOffset >= 0x4c && registerOffset <= 0x64 && (registerOffset & 3U) == 0) {
        m_vertical[static_cast<std::size_t>((registerOffset - 0x4cU) / 4U)] = value;
        updateMode();
    }
}

std::uint32_t DafbVideo::readRamdac(std::uint32_t registerOffset)
{
    switch (registerOffset) {
    case 0x00:
        m_paletteComponent = 0;
        return m_paletteAddress;
    case 0x10: {
        const auto color = m_palette[static_cast<std::size_t>(m_paletteAddress)];
        const auto component = m_paletteComponent++;
        if (m_paletteComponent == 3) {
            m_paletteComponent = 0;
            ++m_paletteAddress;
        }
        if (component == 0) return (color >> 16) & 0xffU;
        if (component == 1) return (color >> 8) & 0xffU;
        return color & 0xffU;
    }
    case 0x20:
        if ((m_paletteAddress == 1) && ((m_pixelBusControl & 0x06U) == 0x06U)
            && m_variant != Variant::Discrete) return m_pixelBusControl1;
        return m_pixelBusControl;
    default:
        return 0;
    }
}

void DafbVideo::writeRamdac(std::uint32_t registerOffset, std::uint32_t value)
{
    value &= 0xffU;
    switch (registerOffset) {
    case 0x00:
        m_paletteAddress = static_cast<std::uint8_t>(value);
        m_paletteComponent = 0;
        break;
    case 0x10:
        m_paletteLatch[static_cast<std::size_t>(m_paletteComponent++)] = static_cast<std::uint8_t>(value);
        if (m_paletteComponent == 3) {
            auto color = 0xff000000U
                | (static_cast<std::uint32_t>(m_paletteLatch[0]) << 16U)
                | (static_cast<std::uint32_t>(m_paletteLatch[1]) << 8U)
                | m_paletteLatch[2];
            if (monitorInfo().mono) color = grayscaleColor(color);
            m_palette[static_cast<std::size_t>(m_paletteAddress)] = color;
            ++m_paletteAddress;
            m_paletteComponent = 0;
        }
        break;
    case 0x20:
        if ((m_paletteAddress == 1) && ((m_pixelBusControl & 0x06U) == 0x06U)
            && m_variant != Variant::Discrete) {
            m_pixelBusControl1 = static_cast<std::uint8_t>((value & 0xf0U) | 0x02U);
        } else {
            m_pixelBusControl = static_cast<std::uint8_t>(value);
            updateMode();
        }
        break;
    default:
        break;
    }
}

std::uint32_t DafbVideo::readClockGenerator(std::uint32_t) const
{
    return 0;
}

void DafbVideo::writeClockGenerator(std::uint32_t, std::uint32_t)
{
}

std::uint32_t DafbVideo::readBlock(std::uint32_t offset)
{
    const auto block = offset & 0x300U;
    const auto reg = offset & 0xffU;
    if (block == 0x000U) return readDafb(reg);
    if (block == 0x100U) return readSwatch(reg);
    if (block == 0x200U) return readRamdac(reg);
    return readClockGenerator(reg);
}

void DafbVideo::writeBlock(std::uint32_t offset, std::uint32_t value)
{
    const auto block = offset & 0x300U;
    const auto reg = offset & 0xffU;
    if (block == 0x000U) writeDafb(reg, value);
    else if (block == 0x100U) writeSwatch(reg, value);
    else if (block == 0x200U) writeRamdac(reg, value);
    else writeClockGenerator(reg, value);
}

std::uint8_t DafbVideo::monitorSense() const
{
    const auto raw = static_cast<std::uint8_t>(m_monitor);
    std::uint8_t result = 7;
    if (raw & 0x40U) {
        if (m_monitorDrive == 0x4U) result &= static_cast<std::uint8_t>(4U | (((raw >> 5U) & 1U) << 1U) | ((raw >> 4U) & 1U));
        if (m_monitorDrive == 0x2U) result &= static_cast<std::uint8_t>((((raw >> 3U) & 1U) << 2U) | 2U | ((raw >> 2U) & 1U));
        if (m_monitorDrive == 0x1U) result &= static_cast<std::uint8_t>((((raw >> 1U) & 1U) << 2U) | ((raw & 1U) << 1U) | 1U);
    } else {
        result = raw & 0x07U;
    }
    return result ^ 7U;
}

const DafbVideo::MonitorInfo& DafbVideo::monitorInfo() const
{
    static const std::array<MonitorInfo, 8> simple {{
        { false, { 0, 0, 0, 0 } },
        { true, { 1, 1, 1, 0 } },
        { false, { 2, 2, 0, 2 } },
        { true, { 3, 3, 1, 2 } },
        { false, { 4, 0, 4, 4 } },
        { false, { 5, 1, 5, 4 } },
        { false, { 6, 2, 4, 6 } },
        { false, { 7, 7, 7, 7 } },
    }};
    static const MonitorInfo extendedColor { false, { 6, 2, 4, 6 } };
    const auto value = static_cast<std::uint8_t>(m_monitor);
    if ((value & 0x40U) != 0) return extendedColor;
    return simple[static_cast<std::size_t>(value & 7U)];
}

void DafbVideo::updateMode()
{
    if (m_variant != Variant::Discrete && (m_pixelBusControl & 0x06U) == 0x06U
        && (m_pixelBusControl1 & 0xc0U) == 0xc0U) {
        m_mode = 5;
    } else {
        switch (m_pixelBusControl & 0x1cU) {
        case 0x00: m_mode = 0; break;
        case 0x08: m_mode = 1; break;
        case 0x10: m_mode = 2; break;
        case 0x18: m_mode = 3; break;
        case 0x1c: m_mode = 4; break;
        default: break;
        }
    }

    const auto htotal = m_horizontal[9];
    const auto vtotal = m_vertical[6] >> 1U;
    if (htotal == 0 || vtotal == 0) return;
    const auto hres = static_cast<int>(m_horizontal[8] - m_horizontal[7]);
    const auto vres = static_cast<int>((m_vertical[5] >> 1U) - (m_vertical[3] >> 1U));
    if (hres > 0 && hres <= 4096) m_width = hres;
    if (vres > 0 && vres <= 2160) m_height = vres;
    if (m_width == 512 && m_variant == Variant::Discrete) {
        m_base = 0x1000;
        m_height = 384;
    }
    const auto clockDivider = 1 << ((m_pixelBusControl & 0x60U) >> 5U);
    if (m_config & 0x08U) {
        m_width = std::max(1, (m_width / clockDivider) - 23);
        m_stride = std::max<std::uint32_t>(1, m_stride / static_cast<std::uint32_t>(clockDivider));
    } else {
        m_width *= clockDivider;
    }
}

void DafbVideo::setInterrupt(std::uint8_t mask, bool asserted)
{
    if (asserted) m_interruptStatus |= mask;
    else m_interruptStatus &= static_cast<std::uint8_t>(~mask);
    recalcIrq();
}

void DafbVideo::recalcIrq()
{
    if (m_irqCallback) m_irqCallback(m_interruptStatus != 0);
}

int DafbVideo::indexedDepth() const
{
    switch (m_mode) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 4;
    case 3: return 8;
    default: return 8;
    }
}

std::uint32_t DafbVideo::scanoutStrideBytes() const
{
    if (m_config & 0x08U) return 1024;
    return m_stride;
}

std::uint32_t DafbVideo::scanoutBaseBytes() const
{
    return m_base;
}

} // namespace cutemac::devices::video
