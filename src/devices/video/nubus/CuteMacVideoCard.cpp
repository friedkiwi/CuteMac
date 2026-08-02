#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace cutemac::devices::video::nubus {

namespace {

constexpr std::uint32_t declarationRomBase = 0x00f00000;
constexpr std::uint32_t modeRegister = 0x00080000;
constexpr std::uint32_t interruptRegister = 0x00080001;
constexpr std::uint32_t paletteAddressRegister = 0x00080002;
constexpr std::uint32_t paletteRedRegister = 0x00080003;
constexpr std::uint32_t paletteGreenRegister = 0x00080004;
constexpr std::uint32_t paletteBlueRegister = 0x00080005;
constexpr std::array<std::uint8_t, 4> guestServicesSignature { 'C', 'T', 'M', 'C' };
constexpr std::uint8_t guestServicesVersion = 2;
constexpr std::uint8_t guestServicesCleanShutdown = 1U << 0;
constexpr std::uint8_t guestServicesAbsolutePointer = 1U << 1;
constexpr int declarationRomBytes = 4096;
constexpr std::uint64_t cyclesPerVbl = 260608;
constexpr std::uint64_t cleanShutdownGraceCycles = cyclesPerVbl * 90;

#include "cutemac_video_driver.generated.h"










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

CuteMacVideoCard::CuteMacVideoCard(int width, int height, int depth, int vramMiB, bool acceleration, bool absolutePointer)
    : m_width(std::clamp(width, 320, 4096))
    , m_height(std::clamp(height, 200, 2160))
    , m_depth((depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16 || depth == 32) ? depth : 8)
    , m_maximumDepth(m_depth)
    , m_acceleration(acceleration)
    , m_absolutePointer(absolutePointer)
    , m_vram(std::clamp(vramMiB, 1, 14) * 1024 * 1024, 0)
    , m_declarationRom(buildDeclarationRom(m_width, m_height, m_vram.size(), m_maximumDepth))
{
    initializePalette();
    reset();
}

QString CuteMacVideoCard::id() const { return QStringLiteral("nubus-video-cutemac"); }

void CuteMacVideoCard::reset()
{
    std::fill(m_vram.begin(), m_vram.end(), 0);
    m_palette[0] = 0xffffffffU;
    m_palette[255] = 0xff000000U;
    m_depth = 1;
    m_vblEnabled = false;
    m_vblCycles = cyclesPerVbl;
    m_hostPointerSequence = 0;
    m_hostPointerValid = false;
    m_powerRequest = core::GuestPowerRequest::None;
    m_deferredPowerRequest = core::GuestPowerRequest::None;
    m_powerRequestDelayCycles = 0;
    setIrq(false);
}

void CuteMacVideoCard::tick(std::uint64_t cycles)
{
    if (m_deferredPowerRequest != core::GuestPowerRequest::None) {
        if (cycles >= m_powerRequestDelayCycles) {
            m_powerRequest = m_deferredPowerRequest;
            m_deferredPowerRequest = core::GuestPowerRequest::None;
            m_powerRequestDelayCycles = 0;
        } else {
            m_powerRequestDelayCycles -= cycles;
        }
    }
    if (cycles >= m_vblCycles) {
        m_vblCycles = cyclesPerVbl - ((cycles - m_vblCycles) % cyclesPerVbl);
        if (m_vblEnabled) setIrq(true);
    } else m_vblCycles -= cycles;
}

std::uint8_t CuteMacVideoCard::read8(std::uint32_t offset)
{
    if (offset >= declarationRomBase) return static_cast<std::uint8_t>(m_declarationRom[static_cast<qsizetype>(offset & (declarationRomBytes - 1))]);
    if (offset == modeRegister) {
        switch (m_depth) { case 1: return 0; case 2: return 1; case 4: return 2; case 8: return 3; case 16: return 4; default: return 5; }
    }
    if (offset == interruptRegister) return m_vblEnabled ? 0 : 1;
    if (offset == paletteAddressRegister) return static_cast<std::uint8_t>(m_paletteAddress);
    if (offset >= paletteRedRegister && offset <= paletteBlueRegister) {
        const auto color = m_palette[m_paletteAddress];
        if (offset == paletteRedRegister) return static_cast<std::uint8_t>(color >> 16);
        if (offset == paletteGreenRegister) return static_cast<std::uint8_t>(color >> 8);
        return static_cast<std::uint8_t>(color);
    }
    if (offset >= guestServicesBase && offset < guestServicesBase + guestServicesSignature.size()) {
        return guestServicesSignature[offset - guestServicesBase];
    }
    if (offset == guestServicesBase + 4) return guestServicesVersion;
    if (offset == guestServicesBase + 5) {
        return guestServicesCleanShutdown | (m_absolutePointer ? guestServicesAbsolutePointer : 0);
    }
    if (offset == guestServicesCommand) return 0;
    if (offset == guestPointerBase) return m_hostPointerValid ? 1 : 0;
    if (offset == guestPointerBase + 1) return m_hostPointerSequence;
    if (offset == guestPointerBase + 2) return static_cast<std::uint8_t>(static_cast<std::uint16_t>(m_hostPointerX) >> 8);
    if (offset == guestPointerBase + 3) return static_cast<std::uint8_t>(m_hostPointerX);
    if (offset == guestPointerBase + 4) return static_cast<std::uint8_t>(static_cast<std::uint16_t>(m_hostPointerY) >> 8);
    if (offset == guestPointerBase + 5) return static_cast<std::uint8_t>(m_hostPointerY);
    if (offset < static_cast<std::uint32_t>(m_vram.size())) return static_cast<std::uint8_t>(m_vram[static_cast<qsizetype>(offset)]);
    return 0xff;
}

void CuteMacVideoCard::write8(std::uint32_t offset, std::uint8_t value)
{
    if (offset == modeRegister) {
        constexpr std::array<int, 6> depths { 1, 2, 4, 8, 16, 32 };
        if (value < depths.size() && depths[value] <= m_maximumDepth) m_depth = depths[value];
    } else if (offset == interruptRegister) {
        m_vblEnabled = value == 0;
        if (m_vblEnabled) setIrq(false);
    } else if (offset == paletteAddressRegister) {
        m_paletteAddress = value;
    } else if (offset == paletteRedRegister) {
        m_paletteLatch[0] = value;
    } else if (offset == paletteGreenRegister) {
        m_paletteLatch[1] = value;
    } else if (offset == paletteBlueRegister) {
        m_paletteLatch[2] = value;
        if (m_paletteAddress != 0 && m_paletteAddress != 255) {
            m_palette[m_paletteAddress] = 0xff000000U | (static_cast<std::uint32_t>(m_paletteLatch[0]) << 16)
                | (static_cast<std::uint32_t>(m_paletteLatch[1]) << 8) | m_paletteLatch[2];
        }
        m_paletteAddress = (m_paletteAddress + 1) & 0xff;
    } else if (offset == guestServicesCommand) {
        if (value == 1) {
            // The Shutdown Manager callback runs before System 6 draws its
            // final safe-to-power-off screen. Leave enough emulated time for
            // the normal shutdown path to finish before closing the host.
            m_deferredPowerRequest = core::GuestPowerRequest::PowerOff;
            m_powerRequestDelayCycles = cleanShutdownGraceCycles;
        }
        else if (value == 2) m_powerRequest = core::GuestPowerRequest::Restart;
    } else if (offset < static_cast<std::uint32_t>(m_vram.size())) m_vram[static_cast<qsizetype>(offset)] = static_cast<char>(value);
}

void CuteMacVideoCard::setHostPointerPosition(std::int16_t x, std::int16_t y)
{
    if (!m_absolutePointer) return;
    const auto clampedX = static_cast<std::int16_t>(std::clamp<int>(x, 0, m_width - 1));
    const auto clampedY = static_cast<std::int16_t>(std::clamp<int>(y, 0, m_height - 1));
    if (m_hostPointerValid && clampedX == m_hostPointerX && clampedY == m_hostPointerY) return;
    m_hostPointerX = clampedX;
    m_hostPointerY = clampedY;
    ++m_hostPointerSequence;
    m_hostPointerValid = true;
}

core::GuestPowerRequest CuteMacVideoCard::takePowerRequest()
{
    const auto request = m_powerRequest;
    m_powerRequest = core::GuestPowerRequest::None;
    return request;
}

VideoFrame CuteMacVideoCard::videoFrame() const
{
    const auto stride = strideBytes();
    const auto required = static_cast<qsizetype>(stride) * m_height;
    if (required > m_vram.size()) return {};
    if (m_depth <= 8) {
        QVector<std::uint16_t> mapping(1 << m_depth);
        const auto maximum = mapping.size() - 1;
        for (int value = 0; value < mapping.size(); ++value) {
            mapping[value] = static_cast<std::uint16_t>(value * 255 / maximum);
        }
        return { m_width, m_height, stride, PixelStorage::Indexed, m_depth, ByteOrder::BigEndian,
            BitOrder::MostSignificantFirst, m_vram.left(required), m_palette, mapping, {} };
    }
    return { m_width, m_height, stride, PixelStorage::Direct, m_depth, ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst, m_vram.left(required), {}, {}, channelLayout() };
}

int CuteMacVideoCard::strideBytes() const { return ((m_width * m_depth + 31) / 32) * 4; }

ChannelLayout CuteMacVideoCard::channelLayout() const
{
    if (m_depth == 16) return { 0x7c00, 0x03e0, 0x001f, 0 };
    return { 0x00ff0000, 0x0000ff00, 0x000000ff, 0 };
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

QByteArray CuteMacVideoCard::buildDeclarationRom(int width, int height, int vramBytes, int maximumDepth)
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
        if (depths[index] > maximumDepth) continue;
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
    for (std::size_t index = 0; index < modeLists.size(); ++index) {
        if (modeLists[index] != 0) rom.offset(static_cast<std::uint8_t>(0x80 + index), modeLists[index]);
    }
    rom.end();
    const auto serviceType = rom.position(); rom.word(0x4354); rom.word(1); rom.word(1); rom.word(1);
    const auto serviceName = rom.position(); rom.text(".CuteMac");
    const auto services = rom.position();
    rom.offset(1, serviceType); rom.offset(2, serviceName); rom.data(0x20, 1); rom.end();
    const auto root = rom.position(); rom.offset(1, board); rom.offset(0x80, video); rom.offset(0x81, services); rom.end();
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
