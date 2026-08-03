#include <iostream>

#include "cutemac/devices/audio/AppleSoundChip.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    cutemac::devices::audio::AppleSoundChip asc;
    asc.reset();
    bool ok = true;

    asc.write(0x803, 0x80);
    ok &= expect(asc.read(0x804) == 0x0a, "FIFO clear must report both channels empty");
    asc.write(0x801, 1);
    for (int index = 0; index < 0x400; ++index) asc.write(0x000, static_cast<std::uint8_t>(index));
    asc.tick((15'667'200ULL * 514) / 22'257 + 1);
    ok &= expect(asc.interruptActive(), "FIFO playback must raise the half-empty interrupt");
    ok &= expect((asc.read(0x804) & 0x01) != 0, "FIFO status must report channel A half empty");
    ok &= expect(!asc.interruptActive(), "reading FIFO status must clear the ASC interrupt");

    return ok ? 0 : 1;
}
