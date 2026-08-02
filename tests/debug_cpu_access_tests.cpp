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
    constexpr std::array<MirrorCase, 8> mirrorCases {{
        {17, 16, true}, {20, 16, true}, {32, 32, false}, {64, 64, false},
        {65, 64, true}, {68, 64, true}, {80, 64, true}, {128, 64, false},
    }};
    for (const auto& topology : mirrorCases) {
        cutemac::machines::maciicx::MacIIcxMachine machine(topology.totalMiB * MiB);
        selectIIcxRamWindow(machine, topology.totalMiB < 32 ? 0x80 : 0xc0);
        const auto mirrorOffset = topology.mirrorsBankB ? std::size_t {0x1000} : MiB + 0x1000;
        const auto source = (topology.mirrorsBankB ? topology.bankAMiB * MiB : 0) + mirrorOffset;
        const auto mirror = topology.totalMiB * MiB + mirrorOffset;
        machine.debugWrite32(static_cast<std::uint32_t>(source), 0x5a5aa5a5U);
        ok &= expect(machine.debugRead32(static_cast<std::uint32_t>(mirror)) == 0x5a5aa5a5U,
            "IIcx large-RAM topology must expose the ROM-required post-RAM bank mirror");
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
    ok &= testIIcxLargeRamTopology();
    return ok ? 0 : 1;
}
