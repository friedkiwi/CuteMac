#include "cutemac/devices/video/nubus/CuteMacAcceleratedVideoCard.h"

#include <algorithm>
#include <array>
#include <limits>

namespace cutemac::devices::video::nubus {

namespace {

constexpr int declarationRomBytes = 4096;
#include "cutemac_accelerated_video_driver.generated.h"

class AcceleratedSlotRomBuilder {
public:
    int position() const { return m_data.size(); }
    void byte(std::uint8_t value) { m_data.append(static_cast<char>(value)); }
    void word(std::uint16_t value) { byte(value >> 8); byte(value); }
    void longWord(std::uint32_t value) { word(value >> 16); word(value); }
    void text(const char* value) { while (*value) byte(*value++); byte(0); while (position() & 3) byte(0); }
    void offset(std::uint8_t id, int target) { longWord((std::uint32_t(id) << 24) | ((target - position()) & 0x00ffffff)); }
    void data(std::uint8_t id, std::uint32_t value) { longWord((std::uint32_t(id) << 24) | (value & 0x00ffffff)); }
    void end() { longWord(0xff000000); }
    void append(const std::uint8_t* bytes, std::size_t size) { m_data.append(reinterpret_cast<const char*>(bytes), size); }
    QByteArray& bytes() { return m_data; }
private:
    QByteArray m_data;
};

int addAcceleratedVideoParameters(AcceleratedSlotRomBuilder& rom, int width, int height, int depth)
{
    const auto at = rom.position();
    const auto stride = ((width * depth + 31) / 32) * 4;
    rom.longWord(46); rom.longWord(0); rom.word(stride);
    rom.word(0); rom.word(0); rom.word(height); rom.word(width);
    rom.word(0); rom.word(0); rom.longWord(0); rom.longWord(0x00480000); rom.longWord(0x00480000);
    const bool direct = depth >= 16;
    rom.word(direct ? 0x10 : 0); rom.word(depth); rom.word(direct ? 3 : 1);
    rom.word(depth == 16 ? 5 : (depth == 32 ? 8 : depth)); rom.longWord(0);
    return at;
}

} // namespace

CuteMacAcceleratedVideoCard::CuteMacAcceleratedVideoCard(int width, int height, int depth,
    int vramMiB, bool acceleration, bool absolutePointer)
    : m_acceleration(acceleration)
    , m_vramBytes(static_cast<std::uint32_t>(std::clamp(vramMiB, 1, 14)) * 1024U * 1024U)
    , m_compatibleCard(width, height, depth, vramMiB, false, absolutePointer)
    , m_declarationRom(buildDeclarationRom(width, height, static_cast<int>(m_vramBytes), depth))
{
    m_compatibleCard.setIrqCallback([this](bool asserted) { setIrq(asserted); });
    resetStatistics();
}

QString CuteMacAcceleratedVideoCard::id() const
{
    return QStringLiteral("nubus-video-cutemac-accelerated");
}

void CuteMacAcceleratedVideoCard::reset()
{
    m_compatibleCard.reset();
    m_status = m_acceleration ? statusEnabled : 0;
    m_guestAdapter = 0;
    m_guestSystemVersion = 0;
    m_guestAdapterVersion = 0;
    resetStatistics();
}
void CuteMacAcceleratedVideoCard::tick(std::uint64_t cycles) { m_compatibleCard.tick(cycles); }
std::uint8_t CuteMacAcceleratedVideoCard::read8(std::uint32_t offset)
{
    if (offset >= 0x00f00000U) {
        return static_cast<std::uint8_t>(m_declarationRom[static_cast<qsizetype>(offset & (declarationRomBytes - 1))]);
    }
    offset &= 0x000fffffU;
    if (offset >= acceleratorBase && offset < acceleratorBase + acceleratorBytes) {
        const auto relative = offset - acceleratorBase;
        const auto value = readAcceleratorRegister(relative & ~3U);
        return static_cast<std::uint8_t>(value >> ((3U - (relative & 3U)) * 8U));
    }
    return m_compatibleCard.read8(offset);
}

void CuteMacAcceleratedVideoCard::write8(std::uint32_t offset, std::uint8_t value)
{
    if (offset >= 0x00f00000U) return;
    offset &= 0x000fffffU;
    if (offset >= acceleratorBase && offset < acceleratorBase + acceleratorBytes) {
        const auto relative = offset - acceleratorBase;
        const auto aligned = relative & ~3U;
        if (aligned == static_cast<std::uint32_t>(AcceleratorRegister::Command)
            || aligned == static_cast<std::uint32_t>(AcceleratorRegister::Control)) {
            if ((relative & 3U) == 3U) writeAcceleratorRegister(aligned, value);
            return;
        }
        const auto shift = (3U - (relative & 3U)) * 8U;
        const auto mask = 0xffU << shift;
        writeAcceleratorRegister(aligned,
            (readAcceleratorRegister(aligned) & ~mask) | (static_cast<std::uint32_t>(value) << shift));
        return;
    }
    m_compatibleCard.write8(offset, value);
}

void CuteMacAcceleratedVideoCard::write16(std::uint32_t offset, std::uint16_t value)
{
    write8(offset, static_cast<std::uint8_t>(value >> 8));
    write8(offset + 1, static_cast<std::uint8_t>(value));
}

void CuteMacAcceleratedVideoCard::write32(std::uint32_t offset, std::uint32_t value)
{
    write16(offset, static_cast<std::uint16_t>(value >> 16));
    write16(offset + 2, static_cast<std::uint16_t>(value));
}
VideoFrame CuteMacAcceleratedVideoCard::videoFrame() const { return m_compatibleCard.videoFrame(); }
core::GuestPowerRequest CuteMacAcceleratedVideoCard::takePowerRequest() { return m_compatibleCard.takePowerRequest(); }
const QByteArray& CuteMacAcceleratedVideoCard::declarationRom() const { return m_declarationRom; }
bool CuteMacAcceleratedVideoCard::absolutePointerEnabled() const { return m_compatibleCard.absolutePointerEnabled(); }
void CuteMacAcceleratedVideoCard::setHostPointerPosition(std::int16_t x, std::int16_t y)
{
    m_compatibleCard.setHostPointerPosition(x, y);
}

std::uint32_t CuteMacAcceleratedVideoCard::readAcceleratorRegister(std::uint32_t offset) const
{
    switch (static_cast<AcceleratorRegister>(offset)) {
    case AcceleratorRegister::Signature: return 0x43564131U; // CVA1
    case AcceleratorRegister::Version: return 1;
    case AcceleratorRegister::Capabilities: return m_acceleration ? capabilityVramCopy : 0;
    case AcceleratorRegister::Status: return m_status;
    case AcceleratorRegister::CommandsSubmitted: return m_commandsSubmitted;
    case AcceleratorRegister::CommandsCompleted: return m_commandsCompleted;
    case AcceleratorRegister::CommandsRejected: return m_commandsRejected;
    case AcceleratorRegister::FallbackOperations: return m_fallbackOperations;
    case AcceleratorRegister::BytesCopied: return m_bytesCopied;
    case AcceleratorRegister::LastCommand: return m_lastCommand;
    case AcceleratorRegister::LastError: return m_lastError;
    case AcceleratorRegister::GuestAdapter: return m_guestAdapter;
    case AcceleratorRegister::GuestSystemVersion: return m_guestSystemVersion;
    case AcceleratorRegister::GuestAdapterVersion: return m_guestAdapterVersion;
    case AcceleratorRegister::SourceOffset: return m_sourceOffset;
    case AcceleratorRegister::DestinationOffset: return m_destinationOffset;
    case AcceleratorRegister::StrideBytes: return m_strideBytes;
    case AcceleratorRegister::WidthBytes: return m_widthBytes;
    case AcceleratorRegister::Height: return m_height;
    case AcceleratorRegister::Flags: return m_flags;
    default: return 0;
    }
}

void CuteMacAcceleratedVideoCard::writeAcceleratorRegister(std::uint32_t offset, std::uint32_t value)
{
    switch (static_cast<AcceleratorRegister>(offset)) {
    case AcceleratorRegister::GuestAdapter: m_guestAdapter = value; break;
    case AcceleratorRegister::GuestSystemVersion: m_guestSystemVersion = value; break;
    case AcceleratorRegister::GuestAdapterVersion: m_guestAdapterVersion = value; break;
    case AcceleratorRegister::SourceOffset: m_sourceOffset = value; break;
    case AcceleratorRegister::DestinationOffset: m_destinationOffset = value; break;
    case AcceleratorRegister::StrideBytes: m_strideBytes = value; break;
    case AcceleratorRegister::WidthBytes: m_widthBytes = value; break;
    case AcceleratorRegister::Height: m_height = value; break;
    case AcceleratorRegister::Flags: m_flags = value; break;
    case AcceleratorRegister::Command: executeCommand(value); break;
    case AcceleratorRegister::Control:
        if (value == controlResetStatistics) resetStatistics();
        else if (value == controlGuestAttach) m_status |= statusGuestAttached;
        else if (value == controlGuestDetach) m_status &= ~statusGuestAttached;
        else if (value == controlRecordFallback) incrementSaturating(m_fallbackOperations);
        break;
    default: break;
    }
}

void CuteMacAcceleratedVideoCard::executeCommand(std::uint32_t command)
{
    incrementSaturating(m_commandsSubmitted);
    m_lastCommand = command;
    m_lastError = 0;
    m_status &= ~statusError;
    if (!m_acceleration) return reject(AcceleratorError::Disabled);
    if (command != commandVramCopy) return reject(AcceleratorError::UnknownCommand);
    executeVramCopy();
}

void CuteMacAcceleratedVideoCard::executeVramCopy()
{
    if (m_widthBytes == 0 || m_height == 0) return reject(AcceleratorError::InvalidDimensions);
    if (m_strideBytes < m_widthBytes) return reject(AcceleratorError::InvalidStride);
    if ((m_flags & ~copyBackward) != 0) return reject(AcceleratorError::InvalidFlags);

    const auto lastRowOffset = static_cast<std::uint64_t>(m_height - 1) * m_strideBytes;
    const auto sourceEnd = static_cast<std::uint64_t>(m_sourceOffset) + lastRowOffset + m_widthBytes;
    const auto destinationEnd = static_cast<std::uint64_t>(m_destinationOffset) + lastRowOffset + m_widthBytes;
    if (sourceEnd > m_vramBytes || destinationEnd > m_vramBytes) return reject(AcceleratorError::OutOfRange);

    m_status |= statusBusy;
    const bool backward = (m_flags & copyBackward) != 0;
    for (std::uint32_t rowIndex = 0; rowIndex < m_height; ++rowIndex) {
        const auto row = backward ? m_height - 1 - rowIndex : rowIndex;
        const auto source = m_sourceOffset + row * m_strideBytes;
        const auto destination = m_destinationOffset + row * m_strideBytes;
        for (std::uint32_t byteIndex = 0; byteIndex < m_widthBytes; ++byteIndex) {
            const auto byte = backward ? m_widthBytes - 1 - byteIndex : byteIndex;
            m_compatibleCard.write8(destination + byte, m_compatibleCard.read8(source + byte));
        }
    }
    m_status &= ~statusBusy;
    incrementSaturating(m_commandsCompleted);
    const auto copied = static_cast<std::uint64_t>(m_widthBytes) * m_height;
    incrementSaturating(m_bytesCopied, static_cast<std::uint32_t>(std::min<std::uint64_t>(
        copied, std::numeric_limits<std::uint32_t>::max())));
}

void CuteMacAcceleratedVideoCard::reject(AcceleratorError error)
{
    m_status &= ~statusBusy;
    m_status |= statusError;
    m_lastError = static_cast<std::uint32_t>(error);
    incrementSaturating(m_commandsRejected);
}

void CuteMacAcceleratedVideoCard::resetStatistics()
{
    m_commandsSubmitted = 0;
    m_commandsCompleted = 0;
    m_commandsRejected = 0;
    m_fallbackOperations = 0;
    m_bytesCopied = 0;
    m_lastCommand = 0;
    m_lastError = 0;
    m_status &= ~statusError;
}

void CuteMacAcceleratedVideoCard::incrementSaturating(std::uint32_t& value, std::uint32_t amount)
{
    value = amount > std::numeric_limits<std::uint32_t>::max() - value
        ? std::numeric_limits<std::uint32_t>::max() : value + amount;
}

QByteArray CuteMacAcceleratedVideoCard::buildDeclarationRom(int width, int height, int vramBytes, int maximumDepth)
{
    width = std::clamp(width, 320, 4096); height = std::clamp(height, 200, 2160);
    AcceleratedSlotRomBuilder rom;
    const auto boardType = rom.position(); rom.word(1); rom.word(0); rom.word(0); rom.word(0);
    const auto boardName = rom.position(); rom.text("CuteMac Video Accelerated");
    const auto vendorName = rom.position(); rom.text("CuteMac Project");
    const auto vendorInfo = rom.position(); rom.offset(1, vendorName); rom.end();
    const auto board = rom.position(); rom.offset(1, boardType); rom.offset(2, boardName); rom.data(0x20, 0x435641); rom.offset(0x24, vendorInfo); rom.end();
    const auto videoType = rom.position(); rom.word(3); rom.word(1); rom.word(1); rom.word(0x4356);
    const auto videoName = rom.position(); rom.text("Display_Video_CuteMac_Accelerated");
    const auto driver = rom.position(); rom.longWord(4 + acceleratedVideoDriver.size());
    rom.append(acceleratedVideoDriver.data(), acceleratedVideoDriver.size()); while (rom.position() & 3) rom.byte(0);
    const auto driverDirectory = rom.position(); rom.offset(2, driver); rom.end();
    std::array<int, 6> modeLists {};
    constexpr std::array<int, 6> depths { 1, 2, 4, 8, 16, 32 };
    for (std::size_t index = 0; index < depths.size(); ++index) {
        if (depths[index] > maximumDepth) continue;
        const auto params = addAcceleratedVideoParameters(rom, width, height, depths[index]);
        modeLists[index] = rom.position(); rom.offset(1, params); rom.data(3, 1);
        rom.data(4, depths[index] >= 16 ? 2 : 0); rom.end();
    }
    const auto minorBase = rom.position(); rom.longWord(0);
    const auto minorLength = rom.position(); rom.longWord(vramBytes);
    const auto video = rom.position(); rom.offset(1, videoType); rom.offset(2, videoName);
    rom.offset(4, driverDirectory); rom.data(8, 1); rom.offset(0x0a, minorBase); rom.offset(0x0b, minorLength);
    for (std::size_t index = 0; index < modeLists.size(); ++index) if (modeLists[index]) rom.offset(0x80 + index, modeLists[index]);
    rom.end();
    const auto serviceType = rom.position(); rom.word(0x4354); rom.word(1); rom.word(1); rom.word(1);
    const auto serviceName = rom.position(); rom.text(".CuteMac");
    const auto services = rom.position(); rom.offset(1, serviceType); rom.offset(2, serviceName); rom.data(0x20, 1); rom.end();
    const auto root = rom.position(); rom.offset(1, board); rom.offset(0x80, video); rom.offset(0x81, services); rom.end();
    if (rom.position() > declarationRomBytes - 20) return {};
    while (rom.position() < declarationRomBytes - 20) rom.byte(0);
    rom.offset(0, root); rom.longWord(declarationRomBytes); rom.longWord(0); rom.byte(1); rom.byte(1);
    rom.longWord(0x5a932bc7); rom.byte(0); rom.byte(0x0f);
    auto& bytes = rom.bytes(); std::uint32_t crc = 0;
    for (const auto value : bytes) crc = ((crc << 1) | (crc >> 31)) + static_cast<std::uint8_t>(value);
    const auto crcOffset = declarationRomBytes - 12;
    bytes[crcOffset] = crc >> 24; bytes[crcOffset + 1] = crc >> 16; bytes[crcOffset + 2] = crc >> 8; bytes[crcOffset + 3] = crc;
    return bytes;
}

} // namespace cutemac::devices::video::nubus
