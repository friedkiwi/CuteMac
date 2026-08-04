#include "cutemac/machines/quadra700/Quadra700Machine.h"
#include <cstdint>
#include <iostream>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool testVia1TimerCalibrationSequence()
{
    using cutemac::machines::quadra700::Quadra700Machine;

    Quadra700Machine machine(4U * 1024U * 1024U);
    machine.reset();

    constexpr std::uint32_t via1 = 0x50000000U;
    machine.debugWrite8(via1 + 0x1800, 0x22); // PCR
    machine.debugWrite8(via1 + 0x0000, 0x80); // An unrelated VIA write is legal between calibration writes.
    machine.debugWrite8(via1 + 0x1000, 0x0c); // T2CL
    machine.debugWrite8(via1 + 0x1200, 0x03); // T2CH
    machine.debugWrite8(via1 + 0x1c00, 0x20); // IER

    bool ok = true;
    ok &= expect(machine.debugRead16(0x0d00) == 0x7e00,
        "Q700 VIA1 calibration must initialize TimeDBRA");
    ok &= expect(machine.debugRead16(0x0d02) == 0x16d7,
        "Q700 VIA1 calibration must initialize TimeSCCDB");
    return ok;
}

bool testRamDoesNotAliasOutsideConfiguredRange()
{
    using cutemac::machines::quadra700::Quadra700Machine;

    constexpr std::uint32_t ramSize = 4U * 1024U * 1024U;
    Quadra700Machine machine(ramSize);
    machine.reset();
    machine.debugWrite32(0, 0x12345678U);
    machine.debugWrite32(0x01000000U, 0xa5a5a5a5U);

    bool ok = true;
    ok &= expect(machine.debugRead32(0) == 0x12345678U,
        "Q700 RAM must not alias at the 16 MiB boundary");
    ok &= expect(machine.debugRead32(ramSize) == 0,
        "Q700 reads beyond configured RAM must be unmapped");
    return ok;
}

} // namespace

int main()
{
    return testVia1TimerCalibrationSequence() && testRamDoesNotAliasOutsideConfiguredRange() ? 0 : 1;
}
