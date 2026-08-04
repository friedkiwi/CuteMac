#include "cutemac/machines/quadra700/Quadra700Machine.h"
#include "cutemac/rom/RomPatcher.h"

#include <cstdint>
#include <iostream>

#include <QByteArray>

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

bool testRamPatternPatchIsChecksumGated()
{
    const auto definitions = cutemac::rom::RomPatcher::definitionsForMachine(QStringLiteral("quadra-700"));
    bool ok = expect(definitions.size() == 1, "Q700 must expose one RAM-test patch");
    if (!ok) return false;

    const auto& patch = definitions.front();
    ok &= expect(patch.requiredSha256 == QByteArray::fromHex("c2093476e9c9a7d76973910a91fbfba23ca71163b84eb2623adf8608a6b03ed2"),
        "Q700 RAM-test patch must be tied to the supported ROM");
    ok &= expect(patch.edits.size() == 2 && patch.edits[1].offset == 0x47280
            && patch.edits[1].expectedBytes == QByteArray::fromHex("4cfa003f")
            && patch.edits[1].replacementBytes == QByteArray::fromHex("7c004ed6"),
        "Q700 RAM-test patch must return a successful result from the destructive routine entry");
    return ok;
}

} // namespace

int main()
{
    return testVia1TimerCalibrationSequence() && testRamPatternPatchIsChecksumGated() ? 0 : 1;
}
