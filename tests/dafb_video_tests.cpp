#include "cutemac/devices/video/DafbVideo.h"
#include "cutemac/devices/scsi/ScsiTarget.h"

#include <iostream>
#include <memory>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

class InquiryTarget final : public cutemac::devices::scsi::ScsiTarget {
public:
    [[nodiscard]] bool ready() const override { return true; }
    [[nodiscard]] bool selectable() const override { return true; }
    [[nodiscard]] cutemac::devices::scsi::ScsiCommandResult executeCommand(
        const QByteArray& cdb, const QByteArray&) override
    {
        cutemac::devices::scsi::ScsiCommandResult result;
        if (!cdb.isEmpty() && static_cast<std::uint8_t>(cdb[0]) == 0x12) {
            result.data = QByteArray(36, 0);
            for (int index = 0; index < result.data.size(); ++index)
                result.data[index] = static_cast<char>(index);
            result.data[0] = 0;
            result.data[4] = 31;
        }
        return result;
    }
};

bool testMonitorSenseAndVersion()
{
    using cutemac::devices::video::DafbVideo;
    bool ok = true;
    DafbVideo dafb(DafbVideo::Variant::Discrete, DafbVideo::Monitor::HiResRgb);
    ok &= expect((dafb.readRegister32(0x1c) & 7U) == 1U,
        "DAFB must return inverted basic monitor sense code");
    ok &= expect(((dafb.readRegister32(0x2c) >> 9U) & 7U) == 1U,
        "discrete DAFB must report version 1");

    DafbVideo memc(DafbVideo::Variant::Memc, DafbVideo::Monitor::Rgb19Inch);
    memc.writeRegister32(0x1c, 0x06);
    ok &= expect(((memc.readRegister32(0x2c) >> 9U) & 7U) == 3U,
        "MEMC DAFB must report version 3");
    ok &= expect((memc.readRegister32(0x1c) & 7U) != 0,
        "extended monitor sense must respond to driven sense lines");
    return ok;
}

bool testIndexedScanoutAndClut()
{
    using cutemac::devices::video::DafbVideo;
    bool ok = true;
    DafbVideo dafb;
    dafb.writeRegister32(0x100, 0); // Swatch mode: enable display
    dafb.writeRegister32(0x008, 160); // 640-byte stride
    dafb.writeRegister32(0x124, 0); // horizontal params through HPIX
    dafb.writeRegister32(0x128, 0);
    dafb.writeRegister32(0x12c, 0);
    dafb.writeRegister32(0x130, 0);
    dafb.writeRegister32(0x134, 0);
    dafb.writeRegister32(0x138, 0);
    dafb.writeRegister32(0x13c, 0);
    dafb.writeRegister32(0x140, 0);
    dafb.writeRegister32(0x144, 639);
    dafb.writeRegister32(0x148, 800);
    dafb.writeRegister32(0x14c, 0);
    dafb.writeRegister32(0x150, 0);
    dafb.writeRegister32(0x154, 0);
    dafb.writeRegister32(0x158, 10); // VBP is not the active-display start.
    dafb.writeRegister32(0x15c, 80); // VAL: active-display start, in half-lines.
    dafb.writeRegister32(0x160, 1040); // VFP: active-display end, in half-lines.
    dafb.writeRegister32(0x164, 1050);
    dafb.writeRegister32(0x200, 1);
    dafb.writeRegister32(0x210, 0xaa);
    dafb.writeRegister32(0x210, 0x55);
    dafb.writeRegister32(0x210, 0x11);
    dafb.writeRegister32(0x220, 0x18);
    dafb.writeVram8(0, 1);

    const auto frame = dafb.videoFrame();
    ok &= expect(frame.valid(), "DAFB frame must be valid after programming timing");
    ok &= expect(frame.width == 639 && frame.height == 480, "DAFB timing must drive frame dimensions");
    ok &= expect(frame.bitsPerPixel == 8, "DAFB pixel bus control must select 8 bpp indexed mode");
    ok &= expect(frame.colorTable.size() == 256 && frame.colorTable[1] == 0xffaa5511U,
        "DAFB RAMDAC CLUT writes must update indexed palette");
    ok &= expect(static_cast<std::uint8_t>(frame.pixels[0]) == 1U,
        "DAFB scanout must read from VRAM base");
    return ok;
}

bool testVblankInterrupt()
{
    cutemac::devices::video::DafbVideo dafb;
    bool irq = false;
    bool ok = true;
    dafb.setIrqCallback([&](bool asserted) { irq = asserted; });
    dafb.writeRegister32(0x100, 0);
    dafb.writeRegister32(0x104, 0x05);
    dafb.tick(2'000'000);
    ok &= expect(irq && dafb.interruptActive() && dafb.readRegister32(0x108) == 0x05,
        "DAFB frame tick must assert enabled VBL and cursor scanline IRQs");
    (void)dafb.readRegister32(0x10c);
    ok &= expect(irq && dafb.readRegister32(0x108) == 0x01,
        "DAFB cursor clear register must preserve a pending VBL IRQ");
    (void)dafb.readRegister32(0x114);
    ok &= expect(!irq && !dafb.interruptActive(), "DAFB VBL clear register must clear IRQ");
    dafb.writeRegister32(0x104, 0x04);
    dafb.tick(2'000'000);
    ok &= expect(dafb.readRegister32(0x108) == 0x04,
        "DAFB interrupt enable must suppress disabled VBL events");
    dafb.writeRegister32(0x10c, 0);
    ok &= expect(!irq && dafb.readRegister32(0x108) == 0,
        "DAFB cursor clear register must acknowledge writes as well as reads");

    dafb.writeRegister32(0x104, 0x01);
    dafb.tick(2'000'000);
    dafb.writeRegister32(0x114, 0);
    ok &= expect(!irq && dafb.readRegister32(0x108) == 0,
        "DAFB VBL clear register must acknowledge writes as well as reads");
    return ok;
}

bool testTurboScsiRegisterRouting()
{
    using cutemac::devices::scsi::ncr53c94::Ncr53c94;
    using cutemac::devices::video::DafbVideo;
    bool ok = true;
    DafbVideo dafb;
    Ncr53c94 scsi;
    scsi.reset();
    scsi.attachTarget(3, std::make_shared<InquiryTarget>());
    dafb.attachTurboScsi(0, &scsi);
    dafb.writeTurboScsiRegister(0, 0x40, 3);
    dafb.writeTurboScsiRegister(0, 0x20, 0x80);
    dafb.writeTurboScsiRegister(0, 0x20, 0x12);
    dafb.writeTurboScsiRegister(0, 0x20, 0);
    dafb.writeTurboScsiRegister(0, 0x20, 0);
    dafb.writeTurboScsiRegister(0, 0x20, 0);
    dafb.writeTurboScsiRegister(0, 0x20, 36);
    dafb.writeTurboScsiRegister(0, 0x20, 0);
    dafb.writeTurboScsiRegister(0, 0x30, 0x41);
    ok &= expect(scsi.interruptActive(), "DAFB TurboSCSI register writes must reach NCR53C9x");
    (void)dafb.readTurboScsiRegister(0, 0x50);
    dafb.writeTurboScsiRegister(0, 0x00, 16);
    dafb.writeTurboScsiRegister(0, 0x10, 0);
    dafb.writeTurboScsiRegister(0, 0x30, 0x90);
    ok &= expect((dafb.readTurboScsiRegister(0, 0x40) & 0x10U) != 0 && scsi.dmaRequest(),
        "asynchronous data-in must reach terminal count with a DMA block ready");
    ok &= expect(dafb.readTurboScsiDma16(0) == 0x0001U,
        "DAFB 16-bit DMA reads must preserve SCSI byte order on the big-endian CPU bus");
    for (int word = 1; word < 8; ++word) (void)dafb.readTurboScsiDma16(0);
    ok &= expect(scsi.debugState().dataPosition == 16 && scsi.debugState().dataIn
            && !scsi.dmaRequest() && scsi.interruptActive(),
        "draining one DMA block must preserve data-in and raise the service interrupt");
    (void)dafb.readTurboScsiRegister(0, 0x50);
    dafb.writeTurboScsiRegister(0, 0x30, 0x90);
    ok &= expect(scsi.dmaRequest(), "a following Transfer Information command must expose the next block");
    return ok;
}

bool testTurboScsiBusResetTiming()
{
    using cutemac::devices::scsi::ncr53c94::Ncr53c94;
    bool ok = true;
    Ncr53c94 scsi;
    scsi.reset();
    scsi.writeRegister(3, 0x03);
    ok &= expect(!scsi.interruptActive(), "NCR53C9x bus reset interrupt must not be immediate");
    scsi.tick(129);
    ok &= expect(!scsi.interruptActive(), "NCR53C9x bus reset must last 130 controller clocks");
    scsi.tick(1);
    ok &= expect(scsi.interruptActive(), "NCR53C9x bus reset completion must raise an interrupt");
    ok &= expect(scsi.readRegister(5) == 0x80, "NCR53C9x reset interrupt cause must be reported");

    scsi.writeRegister(8, 0x40);
    scsi.writeRegister(3, 0x03);
    scsi.tick(130);
    ok &= expect(!scsi.interruptActive(), "NCR53C9x configuration must be able to suppress reset interrupts");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= testMonitorSenseAndVersion();
    ok &= testIndexedScanoutAndClut();
    ok &= testVblankInterrupt();
    ok &= testTurboScsiRegisterRouting();
    ok &= testTurboScsiBusResetTiming();
    return ok ? 0 : 1;
}
