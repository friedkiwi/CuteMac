#pragma once

#include <array>
#include <cstdint>

namespace cutemac::devices::scsi::ncr5380 {

class Ncr5380 {
public:
    void reset();

    [[nodiscard]] std::uint8_t readRegister(std::uint8_t registerIndex, bool dack) const;
    void writeRegister(std::uint8_t registerIndex, bool dack, std::uint8_t value);

private:
    std::array<std::uint8_t, 8> m_registers {};
};

} // namespace cutemac::devices::scsi::ncr5380
