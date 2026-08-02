#include "cutemac/devices/scsi/ncr5380/MacintoshNcr5380Bus.h"

#include <algorithm>

namespace cutemac::devices::scsi::ncr5380 {

MacintoshNcr5380Bus::MacintoshNcr5380Bus(Ncr5380& controller, Wiring wiring)
    : m_controller(controller)
    , m_wiring(wiring)
{
}

std::uint32_t MacintoshNcr5380Bus::readRegister(std::uint8_t index, unsigned accessBytes)
{
    accessBytes = std::clamp(accessBytes, 1U, 4U);
    return static_cast<std::uint32_t>(m_controller.readRegister(index, false))
        << laneShift(m_wiring.registerLane, accessBytes);
}

void MacintoshNcr5380Bus::writeRegister(std::uint8_t index, unsigned accessBytes, std::uint32_t value)
{
    accessBytes = std::clamp(accessBytes, 1U, 4U);
    m_controller.writeRegister(index, false, selectedByte(value, m_wiring.registerLane, accessBytes));
}

std::uint32_t MacintoshNcr5380Bus::readPseudoDma(unsigned accessBytes)
{
    accessBytes = std::clamp(accessBytes, 1U, 4U);
    if (!m_wiring.pseudoDmaBurst || accessBytes == 1) {
        return static_cast<std::uint32_t>(readDack()) << laneShift(m_wiring.pseudoDmaLane, accessBytes);
    }

    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < accessBytes; ++byte)
        value = (value << 8) | readDack();
    return value;
}

void MacintoshNcr5380Bus::writePseudoDma(unsigned accessBytes, std::uint32_t value)
{
    accessBytes = std::clamp(accessBytes, 1U, 4U);
    if (!m_wiring.pseudoDmaBurst || accessBytes == 1) {
        writeDack(selectedByte(value, m_wiring.pseudoDmaLane, accessBytes));
        return;
    }

    for (unsigned byte = 0; byte < accessBytes; ++byte) {
        const auto shift = (accessBytes - byte - 1U) * 8U;
        writeDack(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint8_t MacintoshNcr5380Bus::readDack()
{
    if (m_wiring.waitForDrq) (void)m_controller.readRegister(5, false);
    return m_controller.readRegister(6, true);
}

void MacintoshNcr5380Bus::writeDack(std::uint8_t value)
{
    if (m_wiring.waitForDrq) (void)m_controller.readRegister(5, false);
    m_controller.writeRegister(0, true, value);
}

unsigned MacintoshNcr5380Bus::laneShift(RegisterLane lane, unsigned accessBytes)
{
    if (accessBytes == 1 || lane == RegisterLane::LeastSignificant) return 0;
    return (accessBytes - 1U) * 8U;
}

std::uint8_t MacintoshNcr5380Bus::selectedByte(std::uint32_t value, RegisterLane lane, unsigned accessBytes)
{
    return static_cast<std::uint8_t>(value >> laneShift(lane, accessBytes));
}

} // namespace cutemac::devices::scsi::ncr5380
