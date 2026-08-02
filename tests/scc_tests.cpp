#include <iostream>

#include "cutemac/devices/scc/Z8530Scc.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

}

int main()
{
    using cutemac::devices::scc::Z8530Scc;
    bool ok = true;
    Z8530Scc scc;
    scc.reset();

    // Enable transmit interrupts in WR1 and master interrupts in WR9.
    scc.writeControl(Z8530Scc::Channel::A, 0x01);
    scc.writeControl(Z8530Scc::Channel::A, 0x02);
    scc.writeControl(Z8530Scc::Channel::A, 0x09);
    scc.writeControl(Z8530Scc::Channel::A, 0x08);
    scc.writeData(Z8530Scc::Channel::A, 0x5a);
    scc.tick(16);
    ok &= expect(scc.interruptActive(), "enabled SCC transmit completion must assert IRQ");

    scc.writeControl(Z8530Scc::Channel::A, 0x28);
    ok &= expect(!scc.interruptActive(), "WR0 reset-transmit-interrupt command must clear IRQ");

    // The command above must not leave the SCC waiting for register data.
    scc.writeControl(Z8530Scc::Channel::A, 0x01);
    scc.writeControl(Z8530Scc::Channel::A, 0x00);
    scc.writeData(Z8530Scc::Channel::A, 0xa5);
    scc.tick(16);
    ok &= expect(!scc.interruptActive(), "WR0 command must not consume the following WR1 selector");

    // Point-high plus low register 6 selects WR14.
    scc.writeControl(Z8530Scc::Channel::A, 0x0e);
    scc.writeControl(Z8530Scc::Channel::A, 0x10);
    scc.writeData(Z8530Scc::Channel::A, 0x33);
    ok &= expect((scc.readControl(Z8530Scc::Channel::A) & 0x01) != 0,
        "WR14 local-loopback programming must remain supported");
    ok &= expect(scc.readData(Z8530Scc::Channel::A) == 0x33,
        "SCC local loopback must preserve transmitted data");

    // System 7 selects RR2 on channel B to obtain the modified interrupt
    // vector. A control read completes that selection and must not leave the
    // next WR0 command looking like register data.
    Z8530Scc localTalk;
    localTalk.reset();
    ok &= expect((localTalk.readControl(Z8530Scc::Channel::B) & 0x44) == 0x44,
        "idle SCC transmitter must report both buffer-empty and underrun/EOM");
    ok &= expect((localTalk.readControl(Z8530Scc::Channel::B) & 0x10) != 0,
        "unattached LocalTalk receiver must report sync-hunt state");
    localTalk.writeControl(Z8530Scc::Channel::B, 0x01); // WR1
    localTalk.writeControl(Z8530Scc::Channel::B, 0x01); // ext/status enable
    localTalk.writeControl(Z8530Scc::Channel::A, 0x09); // WR9, shared master IE
    localTalk.writeControl(Z8530Scc::Channel::A, 0x08);
    localTalk.writeControl(Z8530Scc::Channel::B, 0x0c); // WR12
    localTalk.writeControl(Z8530Scc::Channel::B, 0x02);
    localTalk.writeControl(Z8530Scc::Channel::B, 0x0d); // WR13
    localTalk.writeControl(Z8530Scc::Channel::B, 0x00);
    localTalk.writeControl(Z8530Scc::Channel::B, 0x0f); // WR15
    localTalk.writeControl(Z8530Scc::Channel::B, 0x82); // zero-count + break/abort IE
    localTalk.writeControl(Z8530Scc::Channel::B, 0x0e); // WR14
    localTalk.writeControl(Z8530Scc::Channel::B, 0x01); // BRG enable
    localTalk.tick(4096);
    ok &= expect(localTalk.interruptActive(), "unconnected LocalTalk timeout must assert external/status IRQ");
    localTalk.writeControl(Z8530Scc::Channel::B, 0x02); // RR2
    ok &= expect((localTalk.readControl(Z8530Scc::Channel::B) & 0x0e) == 0x02,
        "RR2 must identify channel B external/status");
    localTalk.writeControl(Z8530Scc::Channel::B, 0x0f); // RR15
    ok &= expect(localTalk.readControl(Z8530Scc::Channel::B) == 0x82,
        "RR15 must return the external/status enable mask");
    localTalk.writeControl(Z8530Scc::Channel::B, 0x10); // reset ext/status
    ok &= expect(!localTalk.interruptActive(), "WR0 reset external/status must clear zero-count IRQ");

    return ok ? 0 : 1;
}
