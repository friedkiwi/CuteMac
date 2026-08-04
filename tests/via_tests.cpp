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

    via.writeRegister(14, 0x82); // Enable the Mac CA1 vertical-blank interrupt.
    via.tick(130560);
    ok &= expect((via.readRegister(13) & 0x02) != 0, "VBL must assert the VIA CA1 interrupt");
    via.writeRegister(13, 0x02);
    ok &= expect((via.readRegister(13) & 0x02) == 0, "writing IFR must acknowledge VBL");

    via.writeRegister(14, 0x84); // Enable shift-register interrupts.
    via.writeRegister(11, 0x1c); // Shift out under internal clock.
    via.writeRegister(10, 0x16); // Keyboard model command.
    via.tick(80);
    ok &= expect((via.readRegister(13) & 0x04) != 0, "keyboard command completion must raise the SR interrupt");
    via.writeRegister(13, 0x04);
    via.writeRegister(11, 0x0c); // Shift in under external clock.
    (void)via.readRegister(10); // Start receiving the response.
    via.tick(80);
    ok &= expect((via.readRegister(13) & 0x04) != 0, "keyboard response must raise the SR interrupt");
    ok &= expect(via.readRegister(10) == 0x03, "keyboard model command must return a model byte");

    via.queueKeyboardTransition(0x00, true);
    via.writeRegister(11, 0x1c);
    via.writeRegister(10, 0x10); // Inquiry.
    via.tick(80);
    via.writeRegister(13, 0x04);
    via.writeRegister(11, 0x0c);
    (void)via.readRegister(10);
    via.tick(80);
    ok &= expect(via.readRegister(10) == 0x00, "A key-down must use the keyboard transition encoding");

    via.queueKeyboardTransition(0x00, false);
    via.writeRegister(11, 0x1c);
    via.writeRegister(10, 0x10);
    via.tick(80);
    via.writeRegister(13, 0x04);
    via.writeRegister(11, 0x0c);
    (void)via.readRegister(10);
    via.tick(80);
    ok &= expect(via.readRegister(10) == 0x80, "A key-up must set the transition bit");

    via.writeRegister(14, 0xc0); // Enable Timer 1 interrupts.
    via.writeRegister(4, 0x02);
    via.writeRegister(5, 0x00);
    via.tick(2);
    ok &= expect((via.readRegister(13) & 0x40) != 0, "Timer 1 expiry must set IFR6");
    ok &= expect(via.interruptActive(), "an enabled Timer 1 flag must assert IRQ");
    (void)via.readRegister(4);
    ok &= expect((via.readRegister(13) & 0x40) == 0, "reading T1C-L must acknowledge Timer 1");
    ok &= expect(!via.interruptActive(), "acknowledging Timer 1 must release IRQ");

    bool pb7 = false;
    via.setPortBChangedCallback([&](std::uint8_t value, std::uint8_t) { pb7 = (value & 0x80U) != 0; });
    via.writeRegister(2, 0x80);
    via.writeRegister(11, 0xc0); // Timer 1 free run with PB7 output enabled.
    via.writeRegister(4, 0x02);
    via.writeRegister(5, 0x00);
    ok &= expect(!pb7, "loading Timer 1 must drive PB7 low");
    via.tick(2);
    ok &= expect(pb7, "Timer 1 free-run expiry must toggle PB7");
    via.tick(2);
    ok &= expect(!pb7, "each Timer 1 period must toggle PB7 again");
    (void)via.readRegister(4);
    via.writeRegister(14, 0x40); // Disable Timer 1 before checking Timer 2 IRQ release.

    via.writeRegister(14, 0xa0); // Enable Timer 2 interrupts.
    via.writeRegister(8, 0x02);
    via.writeRegister(9, 0x00);
    via.tick(2);
    ok &= expect((via.readRegister(13) & 0x20) != 0, "Timer 2 expiry must set IFR5");
    ok &= expect(via.interruptActive(), "an enabled Timer 2 flag must assert IRQ");
    (void)via.readRegister(8);
    ok &= expect((via.readRegister(13) & 0x20) == 0, "reading T2C-L must acknowledge Timer 2");
    ok &= expect(!via.interruptActive(), "acknowledging Timer 2 must release IRQ");

    return ok ? 0 : 1;
}
