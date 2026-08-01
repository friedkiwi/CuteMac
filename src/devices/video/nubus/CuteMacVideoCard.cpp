#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace cutemac::devices::video::nubus {

namespace {

constexpr std::uint32_t declarationRomBase = 0x00f00000;
constexpr std::uint32_t modeRegister = 0x00800000;
constexpr std::uint32_t interruptRegister = 0x00800001;
constexpr int declarationRomBytes = 4096;
constexpr std::uint64_t cyclesPerVbl = 260608;

constexpr std::array<std::uint8_t, 316> videoDriver = {
    0x4c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2a,0x00,0x2e,0x00,0x38,0x00,0x94,
    0x00,0x34,0x16,0x2e,0x44,0x69,0x73,0x70,0x6c,0x61,0x79,0x5f,0x43,0x75,0x74,0x65,
    0x4d,0x61,0x63,0x5f,0x56,0x69,0x64,0x65,0x6f,0x00,0x70,0x00,0x4e,0x75,0x70,0xff,
    0x60,0x00,0x00,0xf8,0x70,0x00,0x4e,0x75,0x30,0x28,0x00,0x1a,0x0c,0x40,0x00,0x02,
    0x67,0x00,0x00,0x10,0x0c,0x40,0x00,0x07,0x67,0x00,0x00,0x30,0x70,0x00,0x60,0x00,
    0x00,0xda,0x32,0x28,0x00,0x1c,0x04,0x41,0x00,0x80,0x0c,0x41,0x00,0x05,0x62,0x00,
    0x00,0x2e,0x24,0x69,0x00,0x2a,0xd5,0xfc,0x00,0x80,0x00,0x00,0x14,0x81,0x21,0x69,
    0x00,0x2a,0x00,0x24,0x70,0x00,0x60,0x00,0x00,0xb2,0x24,0x69,0x00,0x2a,0xd5,0xfc,
    0x00,0x80,0x00,0x01,0x14,0xa8,0x00,0x1c,0x70,0x00,0x60,0x00,0x00,0x9e,0x70,0xce,
    0x60,0x00,0x00,0x98,0x30,0x28,0x00,0x1a,0x0c,0x40,0x00,0x02,0x67,0x00,0x00,0x28,
    0x0c,0x40,0x00,0x04,0x67,0x00,0x00,0x4a,0x0c,0x40,0x00,0x05,0x67,0x00,0x00,0x4e,
    0x0c,0x40,0x00,0x06,0x67,0x00,0x00,0x5a,0x0c,0x40,0x00,0x07,0x67,0x00,0x00,0x5c,
    0x70,0xee,0x60,0x00,0x00,0x66,0x24,0x69,0x00,0x2a,0xd5,0xfc,0x00,0x80,0x00,0x00,
    0x72,0x00,0x12,0x12,0x02,0x41,0x00,0x07,0x06,0x41,0x00,0x80,0x31,0x41,0x00,0x1c,
    0x42,0x68,0x00,0x22,0x21,0x69,0x00,0x2a,0x00,0x24,0x70,0x00,0x60,0x00,0x00,0x3c,
    0x31,0x7c,0x00,0x01,0x00,0x22,0x70,0x00,0x60,0x00,0x00,0x30,0x4a,0x68,0x00,0x22,
    0x66,0x00,0xff,0x8c,0x21,0x69,0x00,0x2a,0x00,0x24,0x70,0x00,0x60,0x00,0x00,0x1c,
    0x42,0x28,0x00,0x1c,0x70,0x00,0x60,0x00,0x00,0x12,0x24,0x69,0x00,0x2a,0xd5,0xfc,
    0x00,0x80,0x00,0x01,0x11,0x52,0x00,0x1c,0x70,0x00,0x08,0x28,0x00,0x09,0x00,0x06,
    0x67,0x00,0x00,0x04,0x4e,0x75,0x24,0x78,0x08,0xfc,0x4e,0xd2
};

class SlotRomBuilder {
public:
    int position() const { return m_data.size(); }
    void byte(std::uint8_t value) { m_data.append(static_cast<char>(value)); }
    void word(std::uint16_t value) { byte(static_cast<std::uint8_t>(value >> 8)); byte(static_cast<std::uint8_t>(value)); }
    void longWord(std::uint32_t value) { word(static_cast<std::uint16_t>(value >> 16)); word(static_cast<std::uint16_t>(value)); }
    void text(const char* value) { while (*value != '\0') byte(static_cast<std::uint8_t>(*value++)); byte(0); while ((position() & 3) != 0) byte(0); }
    void offset(std::uint8_t id, int target) { longWord((static_cast<std::uint32_t>(id) << 24) | ((target - position()) & 0x00ffffff)); }
    void data(std::uint8_t id, std::uint32_t value) { longWord((static_cast<std::uint32_t>(id) << 24) | (value & 0x00ffffff)); }
    void end() { longWord(0xff000000); }
    void append(const std::uint8_t* bytes, std::size_t size) { m_data.append(reinterpret_cast<const char*>(bytes), static_cast<qsizetype>(size)); }
    QByteArray& bytes() { return m_data; }
private:
    QByteArray m_data;
};

int addVideoParameters(SlotRomBuilder& rom, int width, int height, int depth)
{
    const auto at = rom.position();
    const auto stride = ((width * depth + 31) / 32) * 4;
    rom.longWord(46);
    rom.longWord(0);
    rom.word(static_cast<std::uint16_t>(stride));
    rom.word(0); rom.word(0); rom.word(static_cast<std::uint16_t>(height)); rom.word(static_cast<std::uint16_t>(width));
    rom.word(0); rom.word(0); rom.longWord(0); rom.longWord(0x00480000); rom.longWord(0x00480000);
    const auto direct = depth >= 16;
    rom.word(direct ? 0x10 : 0);
    rom.word(static_cast<std::uint16_t>(depth));
    rom.word(direct ? 3 : 1);
    rom.word(depth == 16 ? 5 : (depth == 32 ? 8 : depth));
    rom.longWord(0);
    return at;
}

} // namespace

CuteMacVideoCard::CuteMacVideoCard(int width, int height, int depth, int vramMiB, bool acceleration)
    : m_width(std::clamp(width, 320, 4096))
    , m_height(std::clamp(height, 200, 2160))
    , m_depth((depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16 || depth == 32) ? depth : 8)
    , m_acceleration(acceleration)
    , m_vram(std::clamp(vramMiB, 1, 14) * 1024 * 1024, 0)
    , m_declarationRom(buildDeclarationRom(m_width, m_height, m_vram.size()))
{
    initializePalette();
    reset();
}

QString CuteMacVideoCard::id() const { return QStringLiteral("nubus-video-cutemac"); }

void CuteMacVideoCard::reset()
{
    std::fill(m_vram.begin(), m_vram.end(), 0);
    m_depth = 1;
    m_vblEnabled = false;
    m_vblCycles = cyclesPerVbl;
    setIrq(false);
}

void CuteMacVideoCard::tick(std::uint64_t cycles)
{
    if (cycles >= m_vblCycles) {
        m_vblCycles = cyclesPerVbl - ((cycles - m_vblCycles) % cyclesPerVbl);
        if (m_vblEnabled) setIrq(true);
    } else m_vblCycles -= cycles;
}

std::uint8_t CuteMacVideoCard::read8(std::uint32_t offset)
{
    if (offset >= declarationRomBase) return static_cast<std::uint8_t>(m_declarationRom[static_cast<qsizetype>(offset & (declarationRomBytes - 1))]);
    if (offset < static_cast<std::uint32_t>(m_vram.size())) return static_cast<std::uint8_t>(m_vram[static_cast<qsizetype>(offset)]);
    if (offset == modeRegister) {
        switch (m_depth) { case 1: return 0; case 2: return 1; case 4: return 2; case 8: return 3; case 16: return 4; default: return 5; }
    }
    if (offset == interruptRegister) return m_vblEnabled ? 0 : 1;
    return 0xff;
}

void CuteMacVideoCard::write8(std::uint32_t offset, std::uint8_t value)
{
    if (offset < static_cast<std::uint32_t>(m_vram.size())) m_vram[static_cast<qsizetype>(offset)] = static_cast<char>(value);
    else if (offset == modeRegister) {
        constexpr std::array<int, 6> depths { 1, 2, 4, 8, 16, 32 };
        if (value < depths.size()) m_depth = depths[value];
    } else if (offset == interruptRegister) {
        m_vblEnabled = value == 0;
        if (m_vblEnabled) setIrq(false);
    }
}

VideoFrame CuteMacVideoCard::videoFrame() const
{
    const auto stride = strideBytes();
    const auto required = static_cast<qsizetype>(stride) * m_height;
    if (required > m_vram.size()) return {};
    return { m_width, m_height, stride, pixelFormat(), m_vram.left(required), m_palette };
}

int CuteMacVideoCard::strideBytes() const { return ((m_width * m_depth + 31) / 32) * 4; }

PixelFormat CuteMacVideoCard::pixelFormat() const
{
    switch (m_depth) {
    case 1: return PixelFormat::Indexed1; case 2: return PixelFormat::Indexed2; case 4: return PixelFormat::Indexed4;
    case 8: return PixelFormat::Indexed8; case 16: return PixelFormat::RGB555; default: return PixelFormat::XRGB8888;
    }
}

void CuteMacVideoCard::initializePalette()
{
    m_palette.resize(256);
    for (int index = 0; index < 256; ++index) {
        const auto level = static_cast<std::uint8_t>(255 - index);
        m_palette[index] = 0xff000000U | (static_cast<std::uint32_t>(level) << 16)
            | (static_cast<std::uint32_t>(level) << 8) | level;
    }
}

QByteArray CuteMacVideoCard::buildDeclarationRom(int width, int height, int vramBytes)
{
    SlotRomBuilder rom;
    const auto boardType = rom.position(); rom.word(1); rom.word(0); rom.word(0); rom.word(0);
    const auto boardName = rom.position(); rom.text("CuteMac Video Board");
    const auto vendorName = rom.position(); rom.text("CuteMac Project");
    const auto vendorInfo = rom.position(); rom.offset(1, vendorName); rom.end();
    const auto board = rom.position(); rom.offset(1, boardType); rom.offset(2, boardName); rom.data(0x20, 0x43564d); rom.offset(0x24, vendorInfo); rom.end();
    const auto videoType = rom.position(); rom.word(3); rom.word(1); rom.word(1); rom.word(0x4356);
    const auto videoName = rom.position(); rom.text("Display_Video_CuteMac");
    const auto driver = rom.position(); rom.longWord(static_cast<std::uint32_t>(4 + videoDriver.size())); rom.append(videoDriver.data(), videoDriver.size()); while ((rom.position() & 3) != 0) rom.byte(0);
    const auto driverDirectory = rom.position(); rom.offset(2, driver); rom.end();

    std::array<int, 6> modeLists {};
    constexpr std::array<int, 6> depths { 1, 2, 4, 8, 16, 32 };
    for (std::size_t index = 0; index < depths.size(); ++index) {
        const auto params = addVideoParameters(rom, width, height, depths[index]);
        modeLists[index] = rom.position();
        rom.offset(1, params);
        rom.data(3, 1);
        rom.data(4, depths[index] >= 16 ? 2 : 0);
        rom.end();
    }

    const auto minorBase = rom.position(); rom.longWord(0);
    const auto minorLength = rom.position(); rom.longWord(static_cast<std::uint32_t>(vramBytes));

    const auto video = rom.position();
    rom.offset(1, videoType); rom.offset(2, videoName); rom.offset(4, driverDirectory); rom.data(8, 1);
    rom.offset(0x0a, minorBase); rom.offset(0x0b, minorLength);
    for (std::size_t index = 0; index < modeLists.size(); ++index) rom.offset(static_cast<std::uint8_t>(0x80 + index), modeLists[index]);
    rom.end();
    const auto root = rom.position(); rom.offset(1, board); rom.offset(0x80, video); rom.end();
    while (rom.position() < declarationRomBytes - 20) rom.byte(0);
    rom.offset(0, root); rom.longWord(declarationRomBytes); rom.longWord(0); rom.byte(1); rom.byte(1);
    rom.longWord(0x5a932bc7); rom.byte(0); rom.byte(0x0f);

    auto& bytes = rom.bytes();
    std::uint32_t crc = 0;
    for (const auto value : bytes) crc = ((crc << 1) | (crc >> 31)) + static_cast<std::uint8_t>(value);
    const auto crcOffset = declarationRomBytes - 12;
    bytes[crcOffset] = static_cast<char>(crc >> 24); bytes[crcOffset + 1] = static_cast<char>(crc >> 16);
    bytes[crcOffset + 2] = static_cast<char>(crc >> 8); bytes[crcOffset + 3] = static_cast<char>(crc);
    return bytes;
}

} // namespace cutemac::devices::video::nubus
