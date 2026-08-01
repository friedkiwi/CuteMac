#include <cstdint>
#include <iostream>
#include <vector>

#include "cutemac/devices/adb/AdbTransceiver.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    cutemac::devices::adb::AdbTransceiver adb;
    std::vector<std::uint8_t> received;
    int transmitCompletions = 0;
    bool interrupt = false;
    adb.setReceiveByteCallback([&received](std::uint8_t value) { received.push_back(value); });
    adb.setTransmitCompleteCallback([&transmitCompletions]() { ++transmitCompletions; });
    adb.setIrqCallback([&interrupt](bool asserted) { interrupt = asserted; });
    adb.reset();

    // The PIC receives the command from the VIA in S0. S1 and S2 clock the
    // first and second response bytes back to the VIA.
    adb.shiftRegisterWritten(0x3c); // TALK mouse register 0
    adb.setViaState(0);
    adb.tick(32);
    ok &= expect(transmitCompletions == 1, "S0 must complete the VIA command transfer");
    ok &= expect(received.empty(), "S0 must not return a response byte");

    adb.setViaState(1);
    adb.tick(32);
    ok &= expect(received.size() == 1 && received[0] == 0x80, "S1 must return the first mouse byte");
    ok &= expect(!interrupt, "a valid first response byte must leave PB3 high");

    adb.setViaState(2);
    adb.tick(32);
    ok &= expect(received.size() == 2 && received[1] == 0x80, "S2 must return the next mouse byte");
    ok &= expect(!interrupt, "a valid second response byte must leave PB3 high");

    adb.setViaState(1);
    adb.tick(32);
    ok &= expect(received.size() == 3 && received[2] == 0xff, "reading past the response must return 0xff");
    ok &= expect(interrupt, "reading past the response must assert PB3");

    interrupt = false;
    received.clear();
    adb.shiftRegisterWritten(0x2f); // TALK keyboard register 3
    adb.setViaState(0);
    adb.tick(32);
    adb.setViaState(1);
    adb.tick(32);
    adb.setViaState(2);
    adb.tick(32);
    ok &= expect(received.size() == 2 && received[0] == 0x22 && received[1] == 0x01,
        "keyboard register 3 must report the standard keyboard handler");

    received.clear();
    adb.shiftRegisterWritten(0x2b); // LISTEN keyboard register 3
    adb.setViaState(0);
    adb.tick(32);
    adb.shiftRegisterWritten(0x05);
    adb.tick(32);
    adb.setViaState(1);
    adb.tick(32);
    adb.shiftRegisterWritten(0xfe);
    adb.setViaState(0);
    adb.tick(32);
    adb.setViaState(2);
    adb.tick(32);
    ok &= expect(received.size() == 2 && received[0] == 0xff && received[1] == 0xff,
        "each LISTEN payload byte must be acknowledged");

    received.clear();
    adb.shiftRegisterWritten(0x5f); // TALK relocated keyboard register 3
    adb.setViaState(0);
    adb.tick(32);
    adb.setViaState(1);
    adb.tick(32);
    adb.setViaState(2);
    adb.tick(32);
    ok &= expect(received.size() == 2 && received[0] == 0x25 && received[1] == 0x01,
        "keyboard register 3 must follow an address-change LISTEN");

    // In S3 the PIC autonomously repeats the last TALK when an ADB endpoint
    // needs service and clocks a wake/status byte into the VIA. S1 and S2
    // then transfer the actual two-byte response.
    received.clear();
    adb.shiftRegisterWritten(0x3c);
    adb.setViaState(0);
    adb.tick(32);
    adb.setViaState(3);
    adb.moveMouse(7, -4);
    adb.tick(32);
    ok &= expect(received.size() == 1 && received[0] == 0xfc,
        "S3 auto-poll must clock the first byte's vertical delta into the VIA");
    adb.setViaState(1);
    adb.tick(32);
    adb.setViaState(2);
    adb.tick(32);
    ok &= expect(received.size() == 3 && received[1] == 0xfc && received[2] == 0x87,
        "ADB mouse packets must encode vertical then horizontal motion");
    adb.setViaState(3);

    // Host press/release events can arrive together before the ROM next polls
    // ADB. Preserve both transitions so a quick click (and both halves of a
    // double-click) cannot collapse into the final released state.
    received.clear();
    adb.setMouseButton(true);
    adb.setMouseButton(false);
    adb.tick(32);
    adb.setViaState(1);
    adb.tick(32);
    adb.setViaState(2);
    adb.tick(32);
    ok &= expect(received.size() == 3 && (received[1] & 0x80) == 0,
        "the first auto-poll after a quick click must report button down");
    adb.setViaState(3);
    adb.tick(32);
    adb.setViaState(1);
    adb.tick(32);
    adb.setViaState(2);
    adb.tick(32);
    ok &= expect(received.size() == 6 && (received[4] & 0x80) != 0,
        "the following poll after a quick click must report button up");

    return ok ? 0 : 1;
}
