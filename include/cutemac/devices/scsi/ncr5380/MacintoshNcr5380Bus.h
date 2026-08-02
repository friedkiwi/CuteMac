#pragma once

#include <cstdint>

#include "cutemac/devices/scsi/ncr5380/Ncr5380.h"

namespace cutemac::devices::scsi::ncr5380 {

// Models the Macintosh bus glue between a CPU transaction and the byte-wide
// NCR5380.  The controller sees one operation per physical DACK/register
// strobe; CPU byte-lane wiring and blind pseudo-DMA bursts belong here.
class MacintoshNcr5380Bus {
public:
    enum class RegisterLane {
        MostSignificant,
        LeastSignificant,
    };

    struct Wiring {
        RegisterLane registerLane = RegisterLane::MostSignificant;
        RegisterLane pseudoDmaLane = RegisterLane::MostSignificant;
        bool pseudoDmaBurst = false;
        bool waitForDrq = true;
    };

    MacintoshNcr5380Bus(Ncr5380& controller, Wiring wiring);

    [[nodiscard]] std::uint32_t readRegister(std::uint8_t index, unsigned accessBytes);
    void writeRegister(std::uint8_t index, unsigned accessBytes, std::uint32_t value);
    [[nodiscard]] std::uint32_t readPseudoDma(unsigned accessBytes);
    void writePseudoDma(unsigned accessBytes, std::uint32_t value);

private:
    [[nodiscard]] std::uint8_t readDack();
    void writeDack(std::uint8_t value);
    [[nodiscard]] static unsigned laneShift(RegisterLane lane, unsigned accessBytes);
    [[nodiscard]] static std::uint8_t selectedByte(std::uint32_t value, RegisterLane lane, unsigned accessBytes);

    Ncr5380& m_controller;
    Wiring m_wiring;
};

} // namespace cutemac::devices::scsi::ncr5380
