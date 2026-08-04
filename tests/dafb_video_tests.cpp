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
    dafb.writeRegister32(0x158, 0);
    dafb.writeRegister32(0x15c, 0);
    dafb.writeRegister32(0x160, 960);
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
    dafb.tick(2'000'000);
    ok &= expect(irq && dafb.interruptActive(), "DAFB VBL tick must assert IRQ");
    (void)dafb.readRegister32(0x114);
    ok &= expect(!irq && !dafb.interruptActive(), "DAFB VBL clear register must clear IRQ");
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
    dafb.writeTurboScsiRegister(0, 0x20, 0x03);
    dafb.writeTurboScsiRegister(0, 0x20, 0x12);
    dafb.writeTurboScsiRegister(0, 0x20, 0);
    dafb.writeTurboScsiRegister(0, 0x20, 0);
    dafb.writeTurboScsiRegister(0, 0x20, 0);
    dafb.writeTurboScsiRegister(0, 0x20, 36);
    dafb.writeTurboScsiRegister(0, 0x20, 0);
    dafb.writeTurboScsiRegister(0, 0x30, 0x41);
    ok &= expect(scsi.interruptActive(), "DAFB TurboSCSI register writes must reach NCR53C9x");
    (void)dafb.readTurboScsiRegister(0, 0x50);
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
    return ok ? 0 : 1;
}
