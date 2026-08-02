#include "cutemac/devices/video/nubus/CuteMacAcceleratedVideoCard.h"

namespace cutemac::devices::video::nubus {

CuteMacAcceleratedVideoCard::CuteMacAcceleratedVideoCard(int width, int height, int depth,
    int vramMiB, bool acceleration, bool absolutePointer)
    : m_acceleration(acceleration)
    , m_compatibleCard(width, height, depth, vramMiB, false, absolutePointer)
{
    m_compatibleCard.setIrqCallback([this](bool asserted) { setIrq(asserted); });
}

QString CuteMacAcceleratedVideoCard::id() const
{
    return QStringLiteral("nubus-video-cutemac-accelerated");
}

void CuteMacAcceleratedVideoCard::reset() { m_compatibleCard.reset(); }
void CuteMacAcceleratedVideoCard::tick(std::uint64_t cycles) { m_compatibleCard.tick(cycles); }
std::uint8_t CuteMacAcceleratedVideoCard::read8(std::uint32_t offset) { return m_compatibleCard.read8(offset); }
void CuteMacAcceleratedVideoCard::write8(std::uint32_t offset, std::uint8_t value) { m_compatibleCard.write8(offset, value); }
void CuteMacAcceleratedVideoCard::write16(std::uint32_t offset, std::uint16_t value) { m_compatibleCard.write16(offset, value); }
void CuteMacAcceleratedVideoCard::write32(std::uint32_t offset, std::uint32_t value) { m_compatibleCard.write32(offset, value); }
VideoFrame CuteMacAcceleratedVideoCard::videoFrame() const { return m_compatibleCard.videoFrame(); }
core::GuestPowerRequest CuteMacAcceleratedVideoCard::takePowerRequest() { return m_compatibleCard.takePowerRequest(); }
const QByteArray& CuteMacAcceleratedVideoCard::declarationRom() const { return m_compatibleCard.declarationRom(); }
bool CuteMacAcceleratedVideoCard::absolutePointerEnabled() const { return m_compatibleCard.absolutePointerEnabled(); }
void CuteMacAcceleratedVideoCard::setHostPointerPosition(std::int16_t x, std::int16_t y)
{
    m_compatibleCard.setHostPointerPosition(x, y);
}

} // namespace cutemac::devices::video::nubus
