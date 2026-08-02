#include <array>
#include <cstdint>
#include <iostream>

#include "cutemac/core/PhysicalMemoryMap.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    using cutemac::core::PhysicalMemoryMap;
    alignas(PhysicalMemoryMap::pageSize)
        std::array<std::uint8_t, PhysicalMemoryMap::pageSize * 2> ram {};
    alignas(PhysicalMemoryMap::pageSize)
        std::array<std::uint8_t, PhysicalMemoryMap::pageSize> rom {};
    PhysicalMemoryMap map;
    bool ok = true;

    map.mapReadWritePage(0x00100000, ram.data());
    map.mapReadWritePage(0x00101000, ram.data() + PhysicalMemoryMap::pageSize);
    ok &= expect(map.tryWrite32(0x00100004, 0x12345678), "direct RAM must accept a 32-bit write");
    std::uint32_t longValue = 0;
    ok &= expect(map.tryRead32(0x00100004, longValue) && longValue == 0x12345678,
        "direct RAM must preserve big-endian 32-bit values");

    std::uint16_t wordValue = 0;
    ok &= expect(!map.tryRead16(0x00100fff, wordValue),
        "cross-page reads must remain on the machine slow path");
    ok &= expect(!map.tryWrite32(0x00100ffe, 0),
        "cross-page writes must remain on the machine slow path");

    rom[5] = 0xa5;
    map.mapReadOnlyMirrored(0x40000000, PhysicalMemoryMap::pageSize * 2,
        rom.data(), static_cast<std::uint32_t>(rom.size()));
    std::uint8_t byteValue = 0;
    ok &= expect(map.tryRead8(0x40001005, byteValue) && byteValue == 0xa5,
        "read-only pages must support precomputed mirrors");
    ok &= expect(!map.tryWrite8(0x40000005, 0x5a) && rom[5] == 0xa5,
        "read-only direct pages must reject writes");

    map.clear();
    ok &= expect(!map.tryRead8(0x00100004, byteValue) && !map.tryRead8(0x40000005, byteValue),
        "clearing the map must remove allocated direct mappings");

    return ok ? 0 : 1;
}
