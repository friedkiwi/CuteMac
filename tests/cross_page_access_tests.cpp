// A 68k longword access is legal at any even address, so one can straddle the
// boundary of the direct page map the machines use to reach RAM without going
// through their bus decoder. The map declines those accesses by design and the
// machine is expected to complete them the slow way. Getting that wrong is
// invisible until a guest happens to place something across a boundary: MODE32
// relocates the system stack, a return address lands two bytes below one, and
// the guest jumps through whatever the read returned.

#include <cstdint>
#include <iostream>

#include "cutemac/core/PhysicalMemoryMap.h"
#include "cutemac/machines/maciicx/MacIIcxMachine.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

constexpr std::uint32_t pageSize = cutemac::core::PhysicalMemoryMap::pageSize;

template <typename Machine>
bool testStraddlingAccess(Machine& machine, std::uint32_t base, const char* name)
{
    bool ok = true;
    // Two bytes below a page boundary, so the longword takes two bytes from
    // each side of it.
    const auto straddle = base + pageSize - 2;

    machine.write8(straddle + 0, 0x00);
    machine.write8(straddle + 1, 0x00);
    machine.write8(straddle + 2, 0x8e);
    machine.write8(straddle + 3, 0x86);

    const auto readBack = machine.read32(straddle);
    ok &= expect(readBack == 0x00008e86u, name);
    if (readBack != 0x00008e86u) {
        std::cerr << "  read32 across a page boundary returned 0x" << std::hex << readBack
                  << " for 0x" << straddle << std::dec << '\n';
    }

    // The halves must agree with the whole, which is what the machine's
    // fallback is built from.
    ok &= expect(machine.read16(straddle) == 0x0000u, "leading half of a straddling read");
    ok &= expect(machine.read16(straddle + 2) == 0x8e86u, "trailing half of a straddling read");

    // And a straddling write has to survive being read back a byte at a time.
    machine.write32(straddle, 0xdeadbeefu);
    ok &= expect(machine.read8(straddle + 0) == 0xde && machine.read8(straddle + 1) == 0xad
            && machine.read8(straddle + 2) == 0xbe && machine.read8(straddle + 3) == 0xef,
        "a straddling 32-bit write must reach both pages");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;

    {
        cutemac::machines::maciicx::MacIIcxMachine machine(8U * 1024U * 1024U);
        machine.reset();
        ok &= testStraddlingAccess(machine, 0x00040000u,
            "IIcx must read a longword that straddles a direct-page boundary");
    }

    // The Mac Plus is deliberately not exercised here. It comes out of reset
    // with the ROM overlay on, so low addresses decode to ROM rather than RAM
    // and a machine with no ROM loaded reads zeroes: the test would report a
    // failure that is its own setup rather than the machine's behaviour.

    return ok ? 0 : 1;
}
