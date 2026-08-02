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
    const auto completeControllerTransfer = [](auto& controller, std::uint8_t state) {
        controller.setViaState(state);
        controller.tick(30000);
        controller.tick(1200);
    };

    // The PIC receives the command from the VIA in S0. S1 and S2 clock the
    // first and second response bytes back to the VIA.
    adb.shiftRegisterWritten(0x3c); // TALK mouse register 0
    adb.setViaState(0);
    adb.tick(1200);
    ok &= expect(transmitCompletions == 1, "S0 must complete the VIA command transfer");
    ok &= expect(received.empty(), "S0 must not return a response byte");

    completeControllerTransfer(adb, 1);
    ok &= expect(received.size() == 1 && received[0] == 0x80, "S1 must return the first mouse byte");
    ok &= expect(!interrupt, "a valid first response byte must leave PB3 high");

    completeControllerTransfer(adb, 2);
    ok &= expect(received.size() == 2 && received[1] == 0x80, "S2 must return the next mouse byte");
    ok &= expect(!interrupt, "a valid second response byte must leave PB3 high");

    completeControllerTransfer(adb, 1);
    ok &= expect(received.size() == 3 && received[2] == 0xff, "reading past the response must return 0xff");
    ok &= expect(interrupt, "reading past the response must assert PB3");

    interrupt = false;
    received.clear();
    adb.shiftRegisterWritten(0x2f); // TALK keyboard register 3
    adb.setViaState(0);
    adb.tick(1200);
    completeControllerTransfer(adb, 1);
    completeControllerTransfer(adb, 2);
    ok &= expect(received.size() == 2 && received[0] == 0x22 && received[1] == 0x01,
        "keyboard register 3 must report the standard keyboard handler");

    received.clear();
    adb.shiftRegisterWritten(0x2b); // LISTEN keyboard register 3
    adb.setViaState(0);
    adb.tick(1200);
    adb.shiftRegisterWritten(0x05);
    adb.tick(1200);
    completeControllerTransfer(adb, 1);
    adb.shiftRegisterWritten(0xfe);
    adb.setViaState(0);
    adb.tick(1200);
    completeControllerTransfer(adb, 2);
    ok &= expect(received.empty(), "LISTEN payload shifts complete without fabricating response bytes");

    received.clear();
    adb.shiftRegisterWritten(0x5f); // TALK relocated keyboard register 3
    adb.setViaState(0);
    adb.tick(1200);
    completeControllerTransfer(adb, 1);
    completeControllerTransfer(adb, 2);
    ok &= expect(received.size() == 2 && received[0] == 0x25 && received[1] == 0x01,
        "keyboard register 3 must follow an address-change LISTEN");

    // In S3 the PIC autonomously repeats the saved TALK command when an ADB
    // endpoint needs service. PB3 marks this command byte as unsolicited; S1
    // and S2 then transfer the actual two-byte response.
    received.clear();
    adb.shiftRegisterWritten(0x3c);
    adb.setViaState(0);
    adb.tick(1200);
    adb.setViaState(3);
    adb.moveMouse(7, -4);
    adb.tick(1200);
    ok &= expect(received.size() == 1 && received[0] == 0x3c,
        "S3 auto-poll must identify its source with the saved TALK command");
    ok &= expect(interrupt, "an unsolicited auto-poll command must assert PB3");
    completeControllerTransfer(adb, 1);
    completeControllerTransfer(adb, 2);
    ok &= expect(received.size() == 3 && received[1] == 0xfc && received[2] == 0x87,
        "ADB mouse packets must encode vertical then horizontal motion");
    adb.setViaState(3);

    // Host press/release events can arrive together before the ROM next polls
    // ADB. Preserve both transitions so a quick click (and both halves of a
    // double-click) cannot collapse into the final released state.
    received.clear();
    adb.setMouseButton(true);
    adb.setMouseButton(false);
    adb.tick(1200);
    completeControllerTransfer(adb, 1);
    completeControllerTransfer(adb, 2);
    ok &= expect(received.size() == 3 && (received[1] & 0x80) == 0,
        "the first auto-poll after a quick click must report button down");
    adb.setViaState(3);
    adb.tick(1200);
    completeControllerTransfer(adb, 1);
    completeControllerTransfer(adb, 2);
    ok &= expect(received.size() == 6 && (received[4] & 0x80) != 0,
        "the following poll after a quick click must report button up");

    // The command byte is sampled at transfer completion, and a controller
    // byte is retried if the ROM leaves the state lines unchanged long enough.
    cutemac::devices::adb::AdbTransceiver retryAdb;
    std::vector<std::uint8_t> retried;
    retryAdb.setReceiveByteCallback([&retried](std::uint8_t value) { retried.push_back(value); });
    retryAdb.reset();
    retryAdb.shiftRegisterWritten(0x3c);
    retryAdb.setViaState(0);
    ok &= expect(retryAdb.debugState().command == 0, "a host byte must remain latched until its shift completes");
    retryAdb.tick(1200);
    ok &= expect(retryAdb.debugState().command == 0x3c, "shift completion must commit the latched command byte");
    completeControllerTransfer(retryAdb, 1);
    retryAdb.setViaState(1); // unrelated VIA PB write with unchanged ST1:ST0
    retryAdb.tick(1200);
    ok &= expect(retried.size() == 1, "an unchanged VIA state must not redispatch the current ADB phase");
    retryAdb.tick(1082000);
    retryAdb.tick(1200);
    ok &= expect(retried.size() == 2 && retried[0] == retried[1],
        "a stable VIA state must retry the last controller byte");

    cutemac::devices::adb::AdbTransceiver resetAdb;
    std::vector<std::uint8_t> resetReply;
    bool resetStatus = false;
    resetAdb.setReceiveByteCallback([&resetReply](std::uint8_t value) { resetReply.push_back(value); });
    resetAdb.setIrqCallback([&resetStatus](bool asserted) { resetStatus = asserted; });
    resetAdb.reset();
    resetAdb.shiftRegisterWritten(0x00);
    resetAdb.setViaState(0);
    resetAdb.tick(1200);
    completeControllerTransfer(resetAdb, 1);
    ok &= expect(resetReply.size() == 1 && resetReply[0] == 0xff,
        "ADB RESET must complete through the S1 controller-to-host status path");
    ok &= expect(resetStatus, "ADB RESET completion must assert the error/end-of-frame status on PB3");

    cutemac::devices::adb::AdbTransceiver deferredAdb;
    std::vector<std::uint8_t> deferredBytes;
    bool deferredStatus = false;
    deferredAdb.setReceiveByteCallback([&deferredBytes](std::uint8_t value) { deferredBytes.push_back(value); });
    deferredAdb.setIrqCallback([&deferredStatus](bool asserted) { deferredStatus = asserted; });
    deferredAdb.reset();
    // Consume the initial mouse packet so the next TALK has no changed data.
    deferredAdb.shiftRegisterWritten(0x3c);
    deferredAdb.setViaState(0);
    deferredAdb.tick(1200);
    completeControllerTransfer(deferredAdb, 1);
    completeControllerTransfer(deferredAdb, 2);
    deferredAdb.setViaState(3);
    deferredBytes.clear();
    deferredAdb.shiftRegisterWritten(0x3c);
    deferredAdb.setViaState(0);
    deferredAdb.tick(1200);
    deferredAdb.setViaState(3);
    deferredAdb.tick(1112000);
    deferredAdb.tick(1200);
    ok &= expect(deferredBytes.size() == 1 && deferredBytes[0] == 0x3c,
        "an S0-to-S3 TALK must execute through the PIC's deferred saved-command path");
    ok &= expect(deferredStatus, "a deferred TALK completion must wake the VIA through PB3");

    return ok ? 0 : 1;
}
