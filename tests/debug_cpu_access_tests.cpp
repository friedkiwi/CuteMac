#include <iostream>
#include <array>

#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/machines/maciicx/MacIIcxMachine.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

void selectIIcxRamWindow(cutemac::machines::maciicx::MacIIcxMachine& machine, std::uint8_t value)
{
    constexpr std::uint32_t via2DataDirectionA = 0x50002600U;
    constexpr std::uint32_t via2OutputA = 0x50002200U;
    machine.debugWrite8(via2DataDirectionA, 0xc0);
    machine.debugWrite8(via2OutputA, value);
}

bool testIIcxLargeRamTopology()
{
    constexpr std::size_t MiB = 1024U * 1024U;
    bool ok = true;

    {
        cutemac::machines::maciicx::MacIIcxMachine machine(20 * MiB);
        selectIIcxRamWindow(machine, 0xc0); // Bank B at 32 MiB: probe window is too large.
        machine.debugWrite32(2 * MiB, 0x12345678U);
        ok &= expect(machine.debugRead32(2 * MiB) == 0xffffffffU,
            "IIcx oversized GLUE window must expose only the first MiB");
    }

    struct MirrorCase {
        std::size_t totalMiB;
        std::size_t bankAMiB;
        bool mirrorsBankB;
    };
    constexpr std::array<MirrorCase, 11> mirrorCases {{
        {2, 1, false}, {5, 4, true}, {8, 4, false},
        {17, 16, true}, {20, 16, true}, {32, 32, false}, {64, 64, false},
        {65, 64, true}, {68, 64, true}, {80, 64, true}, {128, 64, false},
    }};
    for (const auto& topology : mirrorCases) {
        cutemac::machines::maciicx::MacIIcxMachine machine(topology.totalMiB * MiB);
        selectIIcxRamWindow(machine, 0x00); // Bank B at 1 MiB exposes the complete installed topology.
        const auto mirrorOffset = std::size_t {0x80000}; // Beyond the 256 KiB boot-ROM overlay.
        const auto source = (topology.mirrorsBankB ? topology.bankAMiB * MiB : 0) + mirrorOffset;
        const auto mirror = topology.totalMiB * MiB + mirrorOffset;
        machine.debugWrite32(static_cast<std::uint32_t>(source), 0x5a5aa5a5U);
        const auto mirrored = machine.debugRead32(static_cast<std::uint32_t>(mirror));
        if (mirrored != 0x5a5aa5a5U)
            std::cerr << "FAIL: IIcx " << topology.totalMiB << " MiB post-RAM mirror returned 0x"
                      << std::hex << mirrored << std::dec << '\n';
        ok &= mirrored == 0x5a5aa5a5U;
    }

    return ok;
}

bool testIIcxSingleBankIgnoresBankBSelector()
{
    constexpr std::size_t MiB = 1024U * 1024U;
    bool ok = true;
    constexpr std::array<std::uint32_t, 4> bankBLocations {
        1U * MiB, 2U * MiB, 8U * MiB, 32U * MiB,
    };
    for (const auto size : { 1U * MiB, 4U * MiB, 16U * MiB }) {
        for (const auto selector : { 0x00, 0x40, 0x80, 0xc0 }) {
            cutemac::machines::maciicx::MacIIcxMachine machine(size);
            selectIIcxRamWindow(machine, static_cast<std::uint8_t>(selector));
            machine.debugWrite32(static_cast<std::uint32_t>(size - 12), 0x4d4f4445U);
            ok &= expect(machine.debugRead32(static_cast<std::uint32_t>(size - 12)) == 0x4d4f4445U,
                "IIcx single-bank RAM must remain mapped for every bank-B selector");
            const auto bankBProbe = bankBLocations[static_cast<unsigned>(selector) >> 6];
            if (bankBProbe >= size) {
                ok &= expect(machine.debugRead32(bankBProbe) == 0xffffffffU,
                    "IIcx single-bank configuration must not expose a fictitious bank B");
            }
        }
    }
    return ok;
}

} // namespace

int main()
{
    cutemac::machines::maciicx::MacIIcxMachine machine(8 * 1024 * 1024);
    auto* debug = dynamic_cast<cutemac::core::IDebugCpuAccess*>(&machine);
    bool ok = expect(debug != nullptr, "IIcx must publish common CPU debug access");
    if (!debug) return 1;

    ok &= expect(debug->debugCpuArchitecture() == QStringLiteral("m68k:68030"), "IIcx debug architecture");
    debug->debugWrite32(0x00100000, 0x12345678);
    ok &= expect(debug->debugRead32(0x00100000) == 0x12345678, "IIcx debug memory access");
    ok &= expect(!debug->debugRegisterLines().isEmpty(), "IIcx debug register formatting");
    ok &= expect(debug->disassembleBytes(debug->programCounter()) >= 2, "IIcx debug disassembly length");
    ok &= expect(machine.triggerProgrammersInterrupt(), "IIcx must support the guest programmer's interrupt");
    ok &= testIIcxLargeRamTopology();
    ok &= testIIcxSingleBankIgnoresBankBSelector();
    return ok ? 0 : 1;
}
