#include <iostream>

#include "cutemac/devices/via6522/Via6522.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    cutemac::devices::via6522::Via6522 via;
    via.reset();

    via.setPortBInputBit(3, true);
    via.writeRegister(0, 0x00);
    ok &= expect((via.readRegister(0) & 0x08) != 0, "PB3 input must survive an output-register write");

    via.setPortBInputBit(3, false);
    via.writeRegister(0, 0xff);
    ok &= expect((via.readRegister(0) & 0x08) == 0, "PB3 input must override the output latch while configured as input");

    via.writeRegister(2, 0x8f);
    ok &= expect((via.readRegister(0) & 0x08) != 0, "PB3 output latch must take effect after changing DDRB to output");

    return ok ? 0 : 1;
}
