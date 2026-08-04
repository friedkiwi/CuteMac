#include "cutemac/devices/video/nubus/AppleDisplayCard.h"

#include <QFile>

#include <algorithm>

namespace cutemac::devices::video::nubus {

namespace {

constexpr std::uint32_t standardDeclarationRomBase = 0x000e0000;
constexpr std::uint32_t superDeclarationRomBase = 0x00fe0000;
constexpr std::uint32_t vramWindowLimit = 0x00200000;
constexpr std::uint32_t jmfbBase = 0x00200000;
constexpr std::uint32_t crtcBase = 0x00200100;
constexpr std::uint32_t ramdacBase = 0x00200200;
constexpr std::uint32_t clockGeneratorBase = 0x00200300;
constexpr std::uint64_t cyclesPerVbl = 261379;

struct MonitorInfo {
    bool mono = false;
    std::array<std::uint16_t, 4> sense {};
};

constexpr std::array<MonitorInfo, 32> monitors {{
    { false, { 0, 0, 0, 0 } },
    { true, { 1, 1, 1, 0 } },
    { false, { 2, 2, 0, 2 } },
    { true, { 3, 3, 1, 2 } },
    { false, { 4, 0, 4, 4 } },
    { false, { 5, 1, 5, 4 } },
    { false, { 6, 2, 4, 6 } },
    { false, { 6, 0, 0, 6 } },
    { false, { 6, 0, 4, 6 } },
    { false, { 6, 2, 0, 6 } },
    { false, { 7, 0, 0, 0 } },
    { false, { 7, 1, 1, 0 } },
    { false, { 7, 1, 1, 6 } },
    { false, { 7, 2, 5, 2 } },
    { false, { 7, 3, 0, 0 } },
    { false, { 7, 3, 4, 4 } },
}};

std::uint32_t smearRegisterByte(std::uint8_t value)
{
    return (static_cast<std::uint32_t>(value) << 24)
        | (static_cast<std::uint32_t>(value) << 16)
        | (static_cast<std::uint32_t>(value) << 8)
        | value;
}

int clampVramKiB(int vramKiB)
{
    return vramKiB <= 512 ? 512 : 1024;
}

std::size_t monitorIndex(AppleDisplayCard::Monitor monitor)
{
    return static_cast<std::uint8_t>(monitor) & 0x0fU;
}

std::uint32_t grayscaleColor(std::uint32_t color)
{
    const auto red = (color >> 16) & 0xffU;
    const auto green = (color >> 8) & 0xffU;
    const auto blue = color & 0xffU;
    const auto level = (red * 299U + green * 587U + blue * 114U + 500U) / 1000U;
    return (color & 0xff000000U) | (level << 16) | (level << 8) | level;
}

QVector<std::uint32_t> scanoutPalette(const QVector<std::uint32_t>& palette, AppleDisplayCard::Monitor monitor)
{
    auto result = palette;
    if (monitors[monitorIndex(monitor)].mono) {
        for (auto& color : result) color = grayscaleColor(color);
    }
    return result;
}

QVector<std::uint32_t> scanoutPalette(const QVector<std::uint32_t>& palette, AppleDisplayCard::Monitor monitor, int depth)
{
    auto result = scanoutPalette(palette, monitor);
    if (!result.isEmpty()) result[0] = 0xffffffffU;
    const auto blackIndex = (1 << depth) - 1;
    if (blackIndex < result.size()) result[blackIndex] = 0xff000000U;
    return result;
}

QByteArray scanoutDirectPixels(const QByteArray& pixels, AppleDisplayCard::Monitor monitor)
{
    if (!monitors[monitorIndex(monitor)].mono) return pixels;

    auto result = pixels;
    for (qsizetype index = 0; index + 2 < result.size(); index += 3) {
        const auto color = 0xff000000U
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(result[index])) << 16)
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(result[index + 1])) << 8)
            | static_cast<std::uint8_t>(result[index + 2]);
        const auto gray = static_cast<char>(grayscaleColor(color) & 0xffU);
        result[index] = gray;
        result[index + 1] = gray;
        result[index + 2] = gray;
    }
    return result;
}

} // namespace

AppleDisplayCard::AppleDisplayCard(Variant variant, int vramKiB, Monitor monitor)
    : m_variant(variant)
    , m_monitor(monitor)
    , m_vram(clampVramKiB(vramKiB) * 1024, 0)
    , m_palette(256, 0xff000000U)
{
    reset();
}

QString AppleDisplayCard::id() const
{
    switch (m_variant) {
    case Variant::MacintoshDisplayCard824:
    default:
        return QStringLiteral("nubus-video-apple-mdc-824");
    }
}

bool AppleDisplayCard::loadDeclarationRom(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto bytes = file.readAll();
    if (bytes.size() != declarationRomBytes) return false;
    if (static_cast<std::uint8_t>(bytes.back()) != 0x78) return false;

    m_declarationRom.fill(static_cast<char>(0xff), mappedDeclarationRomBytes);
    for (qsizetype index = 0; index < bytes.size(); ++index) {
        m_declarationRom[index * 4 + 3] = bytes[index];
    }
    return true;
}

void AppleDisplayCard::reset()
{
    std::fill(m_vram.begin(), m_vram.end(), 0);
    for (int index = 0; index < m_palette.size(); ++index) {
        const auto level = static_cast<std::uint8_t>(255 - index);
        m_palette[index] = 0xff000000U | (static_cast<std::uint32_t>(level) << 16)
            | (static_cast<std::uint32_t>(level) << 8) | level;
    }
    m_palette[1] = 0xff000000U;

    m_control = 0x0002;
    m_preload = 256 - 8;
    m_base = 0;
    m_stride = 80 / 4;
    m_clutAddress = 0;
    m_clutComponent = 0;
    m_ramdacMode = 0;
    m_ramdacConvolution = 0;
    m_hhalf = 576;
    m_hactive = 286;
    m_hbporch = 22;
    m_hsync = 30;
    m_hfporch = 18;
    m_vactive = 1740;
    m_vbporch = 78;
    m_vsync = 6;
    m_vfporch = 6;
    m_halflinePixels = 576;
    m_multiplier = 190;
    m_modulus = 19;
    m_pixelClockDivider = 1;
    m_vblDisabled = true;
    m_vblCycles = cyclesPerVbl;
    m_frameCyclePosition = 0;
    m_width = 640;
    m_height = 480;
    m_visibleTop = 0;
    m_visibleLeft = 0;
    m_unsupportedInterlaceSelections = 0;
    setIrq(false);
}

void AppleDisplayCard::tick(std::uint64_t cycles)
{
    m_frameCyclePosition = (m_frameCyclePosition + cycles) % cyclesPerVbl;
    if (cycles >= m_vblCycles) {
        const auto remainder = cycles - m_vblCycles;
        m_vblCycles = cyclesPerVbl - (remainder % cyclesPerVbl);
        if (!m_vblDisabled) setIrq(true);
    } else {
        m_vblCycles -= cycles;
    }
}

std::uint8_t AppleDisplayCard::read8(std::uint32_t offset)
{
    if (declarationRomLoaded()) {
        if (offset >= superDeclarationRomBase && offset < superDeclarationRomBase + mappedDeclarationRomBytes) {
            return static_cast<std::uint8_t>(m_declarationRom[static_cast<qsizetype>(offset - superDeclarationRomBase)]);
        }
        if (offset >= standardDeclarationRomBase && offset < standardDeclarationRomBase + mappedDeclarationRomBytes) {
            return static_cast<std::uint8_t>(m_declarationRom[static_cast<qsizetype>(offset - standardDeclarationRomBase)]);
        }
    }

    const auto local = offset & 0x00ffffffU;
    if (local < vramWindowLimit) {
        if (directRgbActive()) {
            const auto value = packedRgbRead32(local & ~3U);
            return static_cast<std::uint8_t>(value >> (24 - static_cast<int>(local & 3U) * 8));
        }
        if (local < static_cast<std::uint32_t>(m_vram.size())) {
            return static_cast<std::uint8_t>(m_vram[static_cast<qsizetype>(local)]);
        }
        return 0xff;
    }
    if (local >= jmfbBase && local < jmfbBase + 0x10) return readRegisterByte(local);
    if (local >= crtcBase && local < crtcBase + 0x100) return readRegisterByte(local);
    if (local >= ramdacBase && local < ramdacBase + 0x10) return readRegisterByte(local);
    return 0xff;
}

void AppleDisplayCard::write8(std::uint32_t offset, std::uint8_t value)
{
    const auto local = offset & 0x00ffffffU;
    if (local < vramWindowLimit) {
        if (directRgbActive()) {
            auto current = packedRgbRead32(local & ~3U);
            const auto shift = 24 - static_cast<int>(local & 3U) * 8;
            current = (current & ~(0xffU << shift)) | (static_cast<std::uint32_t>(value) << shift);
            packedRgbWrite32(local & ~3U, current);
        } else if (local < static_cast<std::uint32_t>(m_vram.size())) {
            m_vram[static_cast<qsizetype>(local)] = static_cast<char>(value);
        }
        return;
    }
    if ((local >= jmfbBase && local < jmfbBase + 0x10)
        || (local >= crtcBase && local < crtcBase + 0x100)
        || (local >= ramdacBase && local < ramdacBase + 0x10)
        || (local >= clockGeneratorBase && local < clockGeneratorBase + 0x40)) {
        writeRegister(local, smearRegisterByte(value));
    }
}

void AppleDisplayCard::write16(std::uint32_t offset, std::uint16_t value)
{
    const auto local = offset & 0x00ffffffU;
    if ((local >= jmfbBase && local < jmfbBase + 0x10)
        || (local >= crtcBase && local < crtcBase + 0x100)
        || (local >= ramdacBase && local < ramdacBase + 0x10)
        || (local >= clockGeneratorBase && local < clockGeneratorBase + 0x40)) {
        writeRegister(local, value);
        return;
    }
    write8(offset, static_cast<std::uint8_t>(value >> 8));
    write8(offset + 1, static_cast<std::uint8_t>(value));
}

void AppleDisplayCard::write32(std::uint32_t offset, std::uint32_t value)
{
    const auto local = offset & 0x00ffffffU;
    if (local < vramWindowLimit) {
        if (directRgbActive()) {
            packedRgbWrite32(local, value);
        } else {
            for (int lane = 0; lane < 4; ++lane) {
                const auto address = local + static_cast<std::uint32_t>(lane);
                if (address < static_cast<std::uint32_t>(m_vram.size())) {
                    m_vram[static_cast<qsizetype>(address)] = static_cast<char>(value >> (24 - lane * 8));
                }
            }
        }
        return;
    }
    if ((local >= jmfbBase && local < jmfbBase + 0x10)
        || (local >= crtcBase && local < crtcBase + 0x100)
        || (local >= ramdacBase && local < ramdacBase + 0x10)
        || (local >= clockGeneratorBase && local < clockGeneratorBase + 0x40)) {
        writeRegister(local, value);
    }
}

VideoFrame AppleDisplayCard::videoFrame() const
{
    if (unsupportedInterlacedMode()) return {};

    const auto offset = framebufferOffsetBytes();
    if (directRgbActive() && m_ramdacMode == 0x0d) {
        const auto stride = static_cast<int>(m_stride << 3);
        const auto bytes = std::min<qsizetype>(m_vram.size() - offset, static_cast<qsizetype>(stride * m_height));
        const auto pixels = bytes > 0 ? m_vram.mid(offset, bytes) : QByteArray {};
        return {
            m_width,
            m_height,
            stride,
            PixelStorage::Direct,
            24,
            ByteOrder::BigEndian,
            BitOrder::MostSignificantFirst,
            scanoutDirectPixels(pixels, m_monitor),
            {},
            {},
            { 0xff0000, 0x00ff00, 0x0000ff, 0 },
            true,
        };
    }

    const auto depth = indexedDepth();
    const auto stride = indexedStrideBytes();
    const auto bytes = std::min<qsizetype>(m_vram.size() - offset, static_cast<qsizetype>(stride * m_height));
    QVector<std::uint16_t> mapping(1 << depth);
    for (int value = 0; value < mapping.size(); ++value) mapping[value] = static_cast<std::uint16_t>(value);
    return {
        m_width,
        m_height,
        stride,
        PixelStorage::Indexed,
        depth,
        ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst,
        bytes > 0 ? m_vram.mid(offset, bytes) : QByteArray {},
        scanoutPalette(m_palette, m_monitor, depth),
        mapping,
        {},
        true,
    };
}

std::uint8_t AppleDisplayCard::readRegisterByte(std::uint32_t local)
{
    std::uint32_t value = 0;
    if (local >= jmfbBase && local < jmfbBase + 0x10) value = readJmfb((local - jmfbBase) & ~3U);
    else if (local >= crtcBase && local < crtcBase + 0x100) value = readCrtc((local - crtcBase) & ~3U);
    else if (local >= ramdacBase && local < ramdacBase + 0x10) value = readRamdac((local - ramdacBase) & ~3U);
    return static_cast<std::uint8_t>(value >> (24 - static_cast<int>(local & 3U) * 8));
}

void AppleDisplayCard::writeRegister(std::uint32_t local, std::uint32_t data)
{
    if (local >= jmfbBase && local < jmfbBase + 0x10) writeJmfb((local - jmfbBase) & ~3U, data);
    else if (local >= crtcBase && local < crtcBase + 0x100) writeCrtc((local - crtcBase) & ~3U, data);
    else if (local >= ramdacBase && local < ramdacBase + 0x10) writeRamdac((local - ramdacBase) & ~3U, data);
    else if (local >= clockGeneratorBase && local < clockGeneratorBase + 0x40) writeClockGenerator((local - clockGeneratorBase) & ~3U, data);
}

void AppleDisplayCard::writeJmfb(std::uint32_t offset, std::uint32_t data)
{
    data &= 0xffffU;
    switch (offset) {
    case 0x00:
        m_control = static_cast<std::uint16_t>(data & 0x7fffU);
        if (unsupportedInterlacedMode()) ++m_unsupportedInterlaceSelections;
        break;
    case 0x04:
        m_preload = static_cast<std::uint16_t>(data & 0xffU);
        updateRaster();
        break;
    case 0x08:
        m_base = data;
        break;
    case 0x0c:
        m_stride = data;
        break;
    default:
        break;
    }
}

std::uint32_t AppleDisplayCard::readJmfb(std::uint32_t offset) const
{
    switch (offset) {
    case 0x00: {
        const auto& monitor = monitors[monitorIndex(m_monitor)];
        auto sense = monitor.sense[0];
        if ((m_control & 0x0800U) != 0) sense &= monitor.sense[1];
        if ((m_control & 0x0400U) != 0) sense &= monitor.sense[2];
        if ((m_control & 0x0200U) != 0) sense &= monitor.sense[3];
        return (m_control & 0xf1ffU) | (sense << 9);
    }
    case 0x04:
        return m_preload;
    case 0x08:
        return m_base;
    case 0x0c:
        return m_stride;
    default:
        return 0;
    }
}

void AppleDisplayCard::writeCrtc(std::uint32_t offset, std::uint32_t data)
{
    data &= 0xffffU;
    switch (offset) {
    case 0x08: m_hhalf = data & 0x0fffU; updateRaster(); break;
    case 0x0c: m_hactive = data & 0x0fffU; updateRaster(); break;
    case 0x10: m_hbporch = data & 0x0fffU; updateRaster(); break;
    case 0x14: m_hsync = data & 0x0fffU; updateRaster(); break;
    case 0x18: m_hfporch = data & 0x0fffU; updateRaster(); break;
    case 0x24: m_vactive = data & 0x0fffU; updateRaster(); break;
    case 0x28: m_vbporch = data & 0x0fffU; updateRaster(); break;
    case 0x2c: m_vsync = data & 0x0fffU; updateRaster(); break;
    case 0x30: m_vfporch = data & 0x0fffU; updateRaster(); break;
    case 0x3c: m_vblDisabled = (data & 2U) != 0; break;
    case 0x48: setIrq(false); break;
    default: break;
    }
}

std::uint32_t AppleDisplayCard::readCrtc(std::uint32_t offset) const
{
    switch (offset) {
    case 0x08: return m_hhalf;
    case 0x0c: return m_hactive;
    case 0x10: return m_hbporch;
    case 0x14: return m_hsync;
    case 0x18: return m_hfporch;
    case 0x24: return m_vactive;
    case 0x28: return m_vbporch;
    case 0x2c: return m_vsync;
    case 0x30: return m_vfporch;
    case 0xc0: {
        const int vtotal = m_vactive + m_vbporch + m_vsync + m_vfporch;
        if (vtotal == 0 || m_vactive == 0) return 0x0f;
        const bool interlace = (vtotal % 2) != 0;
        const bool convolution = (m_control & 0x0020U) != 0;
        const int divider = 256 - m_preload;
        if (divider <= 0) return 0x0f;

        int scale = 2;
        switch (m_ramdacMode) {
        case 0x00: scale = 5; break;
        case 0x04: scale = 4; break;
        case 0x08: scale = 3; break;
        case 0x0c: scale = 2; break;
        case 0x0d: scale = 0; break;
        default: break;
        }

        const int htotal = m_hactive + m_hbporch + m_hsync + m_hfporch + 8;
        int hpixels = ((htotal << scale) >> (convolution ? 2 : 0)) / divider;
        if (hpixels <= 0) hpixels = 1;
        const int vlines = std::max(1, vtotal >> (interlace ? 0 : 1));
        const auto framePixels = static_cast<std::uint64_t>(hpixels) * static_cast<std::uint64_t>(vlines);
        const auto beamPixel = (m_frameCyclePosition * framePixels) / cyclesPerVbl;
        const int vpos = static_cast<int>(beamPixel / static_cast<std::uint64_t>(hpixels));
        const int hpos = static_cast<int>(beamPixel % static_cast<std::uint64_t>(hpixels));
        const int hsplit = m_visibleLeft + m_halflinePixels;
        const int halfline = interlace ? vpos : ((vpos << 1) | (hpos >= hsplit ? 1 : 0));
        std::uint32_t result = 0x0f;

        if (hpos < m_visibleLeft || hpos >= m_visibleLeft + m_width) result |= 0x20;
        if (halfline < m_vsync) result &= ~0x04U;
        else if (halfline < m_vsync + m_vbporch) result &= ~0x02U;
        else if (halfline < m_vsync + m_vbporch + m_vactive) result &= ~0x01U;
        else result &= ~0x08U;
        return result;
    }
    case 0xcc: return 0;
    default: return 0;
    }
}

void AppleDisplayCard::writeRamdac(std::uint32_t offset, std::uint32_t data)
{
    data &= 0xffU;
    switch (offset) {
    case 0x00:
        m_clutAddress = static_cast<std::uint8_t>(data);
        m_clutComponent = 0;
        break;
    case 0x04:
        m_paletteLatch[static_cast<std::size_t>(m_clutComponent++)] = static_cast<std::uint8_t>(data);
        if (m_clutComponent == 3) {
            m_palette[m_clutAddress] = 0xff000000U
                | (static_cast<std::uint32_t>(m_paletteLatch[0]) << 16)
                | (static_cast<std::uint32_t>(m_paletteLatch[1]) << 8)
                | m_paletteLatch[2];
            ++m_clutAddress;
            m_clutComponent = 0;
        }
        break;
    case 0x08:
        m_ramdacMode = static_cast<std::uint8_t>((data >> 1) & 0x0fU);
        m_ramdacConvolution = static_cast<std::uint8_t>(data & 1U);
        updateRaster();
        break;
    default:
        break;
    }
}

std::uint32_t AppleDisplayCard::readRamdac(std::uint32_t offset) const
{
    return offset == 0x00 ? m_clutAddress : 0;
}

void AppleDisplayCard::writeClockGenerator(std::uint32_t offset, std::uint32_t data)
{
    data &= 0x0fU;
    switch (offset) {
    case 0x00:
    case 0x04:
    case 0x08:
    case 0x0c:
        m_multiplier &= ~(0x0fU << ((offset & 3U) * 4U));
        m_multiplier |= static_cast<std::uint16_t>(data << ((offset & 3U) * 4U));
        updateRaster();
        break;
    case 0x10:
    case 0x14:
    case 0x18:
        m_modulus &= ~(0x0fU << ((offset & 3U) * 4U));
        m_modulus |= static_cast<std::uint16_t>(data << ((offset & 3U) * 4U));
        updateRaster();
        break;
    case 0x24:
        m_pixelClockDivider = static_cast<std::uint8_t>(data);
        updateRaster();
        break;
    default:
        break;
    }
}

void AppleDisplayCard::updateRaster()
{
    const int vtotal = m_vactive + m_vbporch + m_vsync + m_vfporch;
    if (vtotal == 0 || m_vactive == 0 || m_preload == 256) return;

    const bool interlace = (vtotal % 2) != 0;
    const bool convolution = (m_control & 0x0020U) != 0;
    const int divider = 256 - m_preload;
    int scale = 2;
    switch (m_ramdacMode) {
    case 0x00: scale = 5; break;
    case 0x04: scale = 4; break;
    case 0x08: scale = 3; break;
    case 0x0c: scale = 2; break;
    case 0x0d: scale = 0; break;
    default: break;
    }

    m_visibleLeft = ((m_hbporch + m_hsync + 4) << scale) >> (convolution ? 2 : 0);
    m_visibleLeft /= divider;
    m_width = ((m_hactive + 2) << scale) >> (convolution ? 2 : 0);
    m_width /= divider;
    m_visibleTop = (m_vsync + m_vbporch) >> (interlace ? 0 : 1);
    m_height = m_vactive >> (interlace ? 0 : 1);
    if (m_width <= 0 || m_width > 4096) m_width = 640;
    if (m_height <= 0 || m_height > 2160) m_height = 480;
}

std::uint32_t AppleDisplayCard::packedRgbRead32(std::uint32_t local) const
{
    const auto colorOffset = (local / 4U) * 3U;
    if (colorOffset + 2U >= static_cast<std::uint32_t>(m_vram.size())) return 0;
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(m_vram[static_cast<qsizetype>(colorOffset)])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(m_vram[static_cast<qsizetype>(colorOffset + 1)])) << 8)
        | static_cast<std::uint8_t>(m_vram[static_cast<qsizetype>(colorOffset + 2)]);
}

void AppleDisplayCard::packedRgbWrite32(std::uint32_t local, std::uint32_t value)
{
    const auto colorOffset = (local / 4U) * 3U;
    if (colorOffset + 2U >= static_cast<std::uint32_t>(m_vram.size())) return;
    m_vram[static_cast<qsizetype>(colorOffset)] = static_cast<char>(value >> 16);
    m_vram[static_cast<qsizetype>(colorOffset + 1)] = static_cast<char>(value >> 8);
    m_vram[static_cast<qsizetype>(colorOffset + 2)] = static_cast<char>(value);
}

int AppleDisplayCard::indexedDepth() const
{
    switch (m_ramdacMode) {
    case 0x00: return 1;
    case 0x04: return 2;
    case 0x08: return 4;
    case 0x0c: return 8;
    default: return 8;
    }
}

int AppleDisplayCard::indexedStrideBytes() const
{
    return static_cast<int>(m_stride << 2);
}

int AppleDisplayCard::framebufferOffsetBytes() const
{
    return static_cast<int>(m_base << (directRgbActive() ? 6 : 5));
}

bool AppleDisplayCard::directRgbActive() const
{
    return (m_control & 0x0004U) != 0;
}

bool AppleDisplayCard::unsupportedInterlacedMode() const
{
    const int vtotal = m_vactive + m_vbporch + m_vsync + m_vfporch;
    return (vtotal % 2) != 0 || (m_control & 0x0030U) != 0;
}

} // namespace cutemac::devices::video::nubus
