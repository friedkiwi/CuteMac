#include "cutemac/devices/scsi/ncr5380/Ncr5380.h"

namespace cutemac::devices::scsi::ncr5380 {

namespace {

constexpr std::uint8_t currentScsiBusStatus = 4;
constexpr std::uint8_t busAndStatus = 5;

} // namespace

void Ncr5380::reset()
{
    m_registers.fill(0);
}

std::uint8_t Ncr5380::readRegister(std::uint8_t registerIndex, bool dack) const
{
    (void)dack;
    registerIndex &= 0x07;
    if (registerIndex == currentScsiBusStatus) {
        return 0;
    }
    if (registerIndex == busAndStatus) {
        return 0x40; // Phase match / no DMA request approximation for idle bus.
    }

    return m_registers[registerIndex];
}

void Ncr5380::writeRegister(std::uint8_t registerIndex, bool dack, std::uint8_t value)
{
    (void)dack;
    m_registers[registerIndex & 0x07] = value;
}

} // namespace cutemac::devices::scsi::ncr5380
