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

bool testRtcChipEnableIsActiveLow()
{
    using cutemac::machines::quadra700::Quadra700Machine;

    Quadra700Machine machine(4U * 1024U * 1024U);
    machine.reset();

    constexpr std::uint32_t via1 = 0x50000000U;
    constexpr std::uint32_t orb = via1;
    constexpr std::uint32_t ddrb = via1 + 0x0400U;
    const auto writePins = [&machine](std::uint8_t value) { machine.debugWrite8(orb, value); };
    const auto sendByte = [&writePins](std::uint8_t value) {
        for (int bit = 7; bit >= 0; --bit) {
            const auto data = static_cast<std::uint8_t>((value >> bit) & 1U);
            writePins(data); // PB2 low selects the RTC; PB1 low is the clock's idle phase.
            writePins(static_cast<std::uint8_t>(data | 0x02U));
        }
    };

    machine.debugWrite8(ddrb, 0x07U);
    writePins(0x07U); // PB2 high deselects the RTC.
    writePins(0x00U); // Begin a transaction with PB2 low.
    sendByte(0xc1U); // Read blank PRAM byte 0x10.
    std::uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        writePins(0x00U);
        writePins(0x02U);
        value = static_cast<std::uint8_t>((value << 1U) | (machine.debugRead8(orb) & 1U));
    }
    writePins(0x07U);

    return expect(value == 0x00U,
        "Q700 VIA1 PB2 low must select the RTC and expose serial PRAM data");
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
    ok &= expect(machine.debugRead32(ramSize) == 0xffffffffU,
        "Q700 reads beyond configured RAM must see high open-bus data");
    machine.debugWrite32(ramSize, 0xa5a5a5a5U);
    ok &= expect(machine.debugRead32(ramSize) == 0xffffffffU,
        "Q700 writes beyond configured RAM must not create an alias");
    ok &= expect(machine.readPhysical32(ramSize).busError,
        "Q700 unpopulated-bank probes must assert transfer error before MCU configuration");
    machine.debugWrite8(0x5000e000U, 0x12);
    machine.debugWrite8(0x5000e002U, 0x08);
    machine.debugWrite8(0x5000e004U, 0x09);
    machine.debugWrite8(0x5000e006U, 0x84);
    ok &= expect(machine.debugRead8(0x5000e000U) == 0x12
            && machine.debugRead8(0x5000e002U) == 0x08
            && machine.debugRead32(0x5000e000U) == 0x12080984U,
        "Q700 Orwell controls must retain and pack byte-wide values on two-byte spacing");
    machine.debugWrite8(0x5000e000U, 0x00);
    ok &= expect(!machine.readPhysical32(ramSize).busError
            && !machine.readPhysical32(0x04000000U).busError,
        "Q700 installed temporary map must expose absent banks as open bus");
    machine.debugWrite32(0x20U, 0x13579bdfU);
    machine.debugWrite32(ramSize - 0x2cU, 0x2468ace0U);
    machine.debugWrite8(0x5000e000U, 0x00);
    ok &= expect(machine.readPhysical32(ramSize).busError,
        "Q700 clearing the temporary map must restore faults for destructive sizing");
    ok &= expect(machine.debugRead32(0x37ff0020U) == 0x13579bdfU,
        "Q700 relocated RAM aperture must begin at 0x37ff0000");
    ok &= expect(machine.debugRead32(0x383effd4U) == 0x2468ace0U,
        "Q700 relocated aperture must preserve the ROM's top-of-bank descriptor offset");
    machine.debugWrite32(0x37ff0020U, 0x89abcdefU);
    ok &= expect(machine.debugRead32(0x383f0020U) == 0x89abcdefU
            && machine.debugRead32(0x20U) == 0x13579bdfU,
        "Q700 relocated working bank must mirror by RAM size without changing low RAM");
    machine.debugWrite8(0x5000e000U, 0x00);
    ok &= expect(!machine.readPhysical32(ramSize).busError,
        "Q700 repeated odd mapping pass must reinstall the temporary open-bus map");
    machine.debugWrite8(0x5000e000U, 0x00);
    ok &= expect(machine.readPhysical32(ramSize).busError,
        "Q700 following clear must restore absent-bank transfer errors");
    return ok;
}

bool testTwentyFourBitRomAliasYieldsToInstalledRam()
{
    using cutemac::machines::quadra700::Quadra700Machine;

    constexpr std::uint32_t romAlias = 0x00800000U;
    Quadra700Machine fourMiB(4U * 1024U * 1024U);
    fourMiB.reset();
    (void)fourMiB.debugRead8(0x40000000U); // Release the reset overlay.

    bool ok = true;
    ok &= expect(!fourMiB.readPhysical32(romAlias).busError,
        "Q700 24-bit ROM alias must answer above installed 4 MiB RAM");
    ok &= expect(fourMiB.debugRead32(romAlias) == fourMiB.debugRead32(0x40800000U),
        "Q700 24-bit ROM alias must expose the same ROM contents as the high mapping");

    Quadra700Machine sixteenMiB(16U * 1024U * 1024U);
    sixteenMiB.reset();
    (void)sixteenMiB.debugRead8(0x40000000U);
    sixteenMiB.debugWrite32(romAlias, 0x12345678U);
    ok &= expect(sixteenMiB.debugRead32(romAlias) == 0x12345678U,
        "Q700 installed RAM must take precedence over the 24-bit ROM alias");
    return ok;
}

} // namespace

int main()
{
    return testVia1TimerCalibrationSequence() && testRtcChipEnableIsActiveLow()
            && testRamDoesNotAliasOutsideConfiguredRange()
            && testTwentyFourBitRomAliasYieldsToInstalledRam()
        ? 0
        : 1;
}
