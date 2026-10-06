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

    cutemac::devices::audio::AppleSoundChip easc(cutemac::devices::audio::AppleSoundChip::Model::Enhanced);
    easc.reset();
    ok &= expect(easc.read(0x800) == 0xb0, "Quadra EASC must expose its ROM-visible version");
    ok &= expect(easc.read(0x807) == 3, "Quadra EASC clock register must report 44.1 kHz");
    ok &= expect(easc.read(0x804) == 0x0f && easc.read(0x804) == 0x0f,
        "Quadra EASC FIFO status must start empty and remain visible after reads");
    easc.write(0x802, 1);
    ok &= expect(easc.read(0x802) == 0, "Quadra EASC control register must be read-only");
    easc.write(0x801, 3);
    ok &= expect(easc.read(0x801) == 1, "Quadra EASC mode register must expose only FIFO enable");
    for (int index = 0; index < 0x400; ++index) {
        easc.write(0x000, static_cast<std::uint8_t>(index));
        easc.write(0x400, static_cast<std::uint8_t>(index));
    }
    easc.tick((15'667'200ULL * 514) / 44'100 + 1);
    ok &= expect((easc.read(0x804) & 0x05) == 0x05,
        "Quadra EASC playback must drain and report both stereo FIFOs");
    easc.tick((15'667'200ULL * 514) / 44'100 + 1);
    ok &= expect((easc.read(0x804) & 0x0f) == 0x0f,
        "Quadra EASC must report both stereo FIFOs empty after playback drains them");

    return ok ? 0 : 1;
}
