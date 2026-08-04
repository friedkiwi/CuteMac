#include <QFile>
#include <QTemporaryDir>

#include <iostream>

#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"
#include "cutemac/devices/video/nubus/CuteMacAcceleratedVideoCard.h"
#include "cutemac/devices/video/nubus/AppleDisplayCard.h"
#include "cutemac/devices/video/nubus/MacintoshIIVideoCard.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

std::uint32_t readCard32(cutemac::devices::nubus::NuBusCard& card, std::uint32_t address)
{
    return (static_cast<std::uint32_t>(card.read8(address)) << 24)
        | (static_cast<std::uint32_t>(card.read8(address + 1)) << 16)
        | (static_cast<std::uint32_t>(card.read8(address + 2)) << 8)
        | card.read8(address + 3);
}

} // namespace

int main()
{
    using cutemac::devices::video::PixelStorage;
    using cutemac::devices::video::nubus::AppleDisplayCard;
    using cutemac::devices::video::nubus::CuteMacAcceleratedVideoCard;
    using cutemac::devices::video::nubus::CuteMacVideoCard;
    using cutemac::devices::video::nubus::MacintoshIIVideoCard;
    bool ok = true;

    CuteMacVideoCard virtualCard(832, 624, 8, 4096, true);
    ok &= expect(virtualCard.declarationRom().size() == 4096, "CuteMac declaration ROM size");
    ok &= expect(virtualCard.declarationRom().mid(4090, 4).toHex() == QByteArray("5a932bc7"), "CuteMac declaration ROM test pattern");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("d5fc000e0002")),
        "CuteMac declaration ROM driver must expose palette operations");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex(
            "0000002e00000000006800000000027003400001")),
        "CuteMac indexed mode parameters must publish the version-1 PixMap layout");
    ok &= expect(!virtualCard.declarationRom().contains(QByteArray::fromHex("00ff005500110001")),
        "CuteMac video driver must not remap a new CLUT using the previous hardware mode");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("2668001c4a8b")),
        "CuteMac video driver must dereference selector-specific Control and Status parameters");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("0c40000a6700")),
        "CuteMac video driver must implement the System 7 SwitchMode selector");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("10290029b0ab0002")),
        "CuteMac SwitchMode must validate the functional sResource ID");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("26532468001c362a0004342a0006")),
        "CuteMac video driver must read SetEntries through VDSetEntryRecord");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("4a1357c0")),
        "CuteMac video driver must translate the System 6 colour Boolean to its luminance flag");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("31400010247808fc4ed2")),
        "CuteMac video driver must complete queued requests before calling JIODone");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("2469002a4e75")),
        "CuteMac video driver must use the addressing-mode-aware AuxDCE device base");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex(
            "4a4167300c4100ff672ab24266120c42000f67200c420003671a0c4200016714")),
        "CuteMac video driver must silently discard indexed palette endpoint writes");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("0c4200ff6200"))
            && !virtualCard.declarationRom().contains(QByteArray::fromHex("4a426700")),
        "CuteMac video driver must treat csCount as a zero-based DBRA terminal value");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("024100ff")),
        "CuteMac video driver must mask ColorSpec flags before addressing endpoint entries");
    ok &= expect(!virtualCard.declarationRom().contains(QByteArray::fromHex("c0fc004d"))
            && !virtualCard.declarationRom().contains(QByteArray::fromHex("c8fc0096")),
        "CuteMac video driver must program caller-provided RGB instead of stale grayscale conversion");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray(".CuteMac\0", 9))
            && virtualCard.declarationRom().contains(QByteArray::fromHex("a895")),
        "CuteMac declaration ROM must advertise guest services and install its shutdown callback");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("a075"))
            && virtualCard.declarationRom().contains(QByteArray::fromHex("a076"))
            && virtualCard.declarationRom().contains(QByteArray::fromHex("20780d284e90")),
        "CuteMac video driver must install, remove, and dispatch its slot VBL interrupt");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("95ca4e75")),
        "CuteMac video driver must return a page-zero offset for its non-f32BitMode declaration");
    ok &= expect(virtualCard.videoFrame().storage == PixelStorage::Indexed && virtualCard.videoFrame().bitsPerPixel == 1,
        "CuteMac video must reset to one-bit indexed mode");
    ok &= expect(!virtualCard.videoFrame().grabbable,
        "CuteMac video with integrated absolute pointer must not request host mouse grabbing");
    ok &= expect(virtualCard.videoFrame().pixelToColorIndex == QVector<std::uint16_t> { 0, 1 }
            && virtualCard.videoFrame().colorTable[1] == 0xff000000U,
        "CuteMac one-bit mode must use logical CLUT entries");
    virtualCard.write8(0x00900000, 0x5a);
    ok &= expect(virtualCard.read8(0x00000000) == 0x5a
            && static_cast<std::uint8_t>(virtualCard.videoFrame().pixels[0]) == 0x5a,
        "CuteMac video must mirror 24-bit slot-9 framebuffer accesses onto local VRAM");
    virtualCard.write8(0x00080000, 0xc6);
    ok &= expect(virtualCard.read8(0x00080000) == 0xc6,
        "CuteMac visible eight-bit framebuffer must not overlap the control aperture");
    virtualCard.write8(0x009e0000, 3);
    ok &= expect(virtualCard.read8(0x000e0000) == 3,
        "CuteMac video must mirror control registers through the 24-bit slot-9 alias");
    ok &= expect(virtualCard.read8(0x00f00020) == static_cast<std::uint8_t>(virtualCard.declarationRom()[0x20]),
        "CuteMac declaration ROM must take precedence over the mirrored hardware aperture");
    virtualCard.write8(0x00f00000, 0xc3);
    ok &= expect(virtualCard.read8(0x00000000) == 0x5a,
        "writes to the CuteMac declaration ROM aperture must not alias onto VRAM");
    virtualCard.reset();
    virtualCard.write8(0x000e0000, 3);
    ok &= expect(virtualCard.videoFrame().storage == PixelStorage::Indexed && virtualCard.videoFrame().bitsPerPixel == 8,
        "CuteMac mode register must select eight-bit indexed mode");
    virtualCard.write8(0x000e0000, 4);
    ok &= expect(virtualCard.videoFrame().bitsPerPixel == 8,
        "CuteMac mode register must reject depths above the configured maximum");
    virtualCard.reset();
    virtualCard.write8(0x000e0006, 0);
    ok &= expect(static_cast<std::uint8_t>(virtualCard.videoFrame().pixels[0]) == 0xaa
            && static_cast<std::uint8_t>(virtualCard.videoFrame().pixels[virtualCard.videoFrame().strideBytes]) == 0x55,
        "CuteMac GrayPage must replace one-bit VRAM with the classic alternating gray pattern");
    virtualCard.write8(0x000e0000, 3);
    virtualCard.write8(0x000e0006, 0);
    ok &= expect(static_cast<std::uint8_t>(virtualCard.videoFrame().pixels[0]) == 0xff
            && static_cast<std::uint8_t>(virtualCard.videoFrame().pixels[1]) == 0x00
            && static_cast<std::uint8_t>(virtualCard.videoFrame().pixels[virtualCard.videoFrame().strideBytes]) == 0x00,
        "CuteMac GrayPage must use endpoint pixels rather than packed-pattern bytes in eight-bit mode");
    virtualCard.write8(0x000e0000, 3);
    virtualCard.write8(0x000e0002, 0x2a);
    virtualCard.write8(0x000e0003, 0x12);
    virtualCard.write8(0x000e0004, 0x34);
    virtualCard.write8(0x000e0005, 0x56);
    ok &= expect(virtualCard.videoFrame().colorTable[0x2a] == 0xff123456U,
        "CuteMac RAMDAC must publish guest-programmed indexed color");
    virtualCard.write8(0x000e0002, 0x2a);
    ok &= expect(virtualCard.read8(0x000e0003) == 0x12 && virtualCard.read8(0x000e0004) == 0x34
            && virtualCard.read8(0x000e0005) == 0x56,
        "CuteMac RAMDAC palette entries must be readable by GetEntries");
    virtualCard.write8(0x000e0002, 0xff);
    virtualCard.write8(0x000e0003, 0x00);
    virtualCard.write8(0x000e0004, 0xff);
    virtualCard.write8(0x000e0005, 0x00);
    ok &= expect(virtualCard.videoFrame().colorTable[0xff] == 0xff000000U,
        "CuteMac RAMDAC must keep the final palette entry black");
    virtualCard.write8(0x000e0000, 2);
    ok &= expect(virtualCard.videoFrame().pixelToColorIndex[15] == 15
            && virtualCard.videoFrame().colorTable[15] == 0xff000000U,
        "CuteMac four-bit mode must use logical CLUT entries with black at entry fifteen");
    virtualCard.write8(0x000e0002, 0x0f);
    virtualCard.write8(0x000e0003, 0xff);
    virtualCard.write8(0x000e0004, 0x00);
    virtualCard.write8(0x000e0005, 0x00);
    ok &= expect(virtualCard.videoFrame().colorTable[0x0f] == 0xff000000U,
        "CuteMac RAMDAC must keep the four-bit logical black entry fixed");
    virtualCard.write8(0x000e0000, 3);
    virtualCard.write8(0x000e0002, 0x0f);
    virtualCard.write8(0x000e0003, 0xff);
    virtualCard.write8(0x000e0004, 0x00);
    virtualCard.write8(0x000e0005, 0x00);
    ok &= expect(virtualCard.videoFrame().colorTable[0x0f] == 0xffff0000U,
        "CuteMac RAMDAC must allow non-endpoint entries after returning to eight-bit mode");
    virtualCard.write8(0x000e0002, 0x00);
    virtualCard.write8(0x000e0003, 0x12);
    virtualCard.write8(0x000e0004, 0x34);
    virtualCard.write8(0x000e0005, 0x56);
    ok &= expect(virtualCard.videoFrame().colorTable[0x00] == 0xffffffffU,
        "CuteMac RAMDAC must keep the first palette entry white");
    ok &= expect(virtualCard.read8(CuteMacVideoCard::guestServicesBase) == 'C'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 1) == 'T'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 2) == 'M'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 3) == 'C'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 4) == 2
            && (virtualCard.read8(CuteMacVideoCard::guestServicesBase + 5) & 3) == 2,
        "CuteMac guest-services mailbox identity and absolute-pointer capability");
    virtualCard.setHostPointerPosition(900, -20);
    ok &= expect(virtualCard.read8(CuteMacVideoCard::guestPointerBase) == 1
            && virtualCard.read8(CuteMacVideoCard::guestPointerBase + 1) == 1
            && virtualCard.read8(CuteMacVideoCard::guestPointerBase + 2) == 0x03
            && virtualCard.read8(CuteMacVideoCard::guestPointerBase + 3) == 0x3f
            && virtualCard.read8(CuteMacVideoCard::guestPointerBase + 4) == 0x00
            && virtualCard.read8(CuteMacVideoCard::guestPointerBase + 5) == 0x00,
        "CuteMac absolute pointer mailbox must clamp and expose big-endian coordinates");
    virtualCard.setHostPointerPosition(900, -20);
    ok &= expect(virtualCard.read8(CuteMacVideoCard::guestPointerBase + 1) == 1,
        "stationary absolute pointer updates must not retrigger the guest cursor task");
    ok &= expect(!virtualCard.absolutePointerActive(),
        "CuteMac absolute pointer must be inactive until the guest enables the slot VBL helper");
    virtualCard.write8(0x000e0001, 0);
    ok &= expect(virtualCard.absolutePointerActive(),
        "CuteMac absolute pointer must become active with the guest slot VBL helper");
    virtualCard.write8(0x000e0001, 1);
    ok &= expect(!virtualCard.absolutePointerActive(),
        "CuteMac absolute pointer must become inactive when the guest disables the slot VBL helper");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("31ea0004082831ea0002082a")),
        "CuteMac video slot VBL must copy absolute pointer coordinates into MTemp");
    CuteMacVideoCard relativeCard(640, 480, 8, 4096, true, false);
    ok &= expect(relativeCard.videoFrame().grabbable,
        "CuteMac video without integrated absolute pointer must allow host mouse grabbing");
    relativeCard.setHostPointerPosition(100, 120);
    ok &= expect((relativeCard.read8(CuteMacVideoCard::guestServicesBase + 5) & 2) == 0
            && relativeCard.read8(CuteMacVideoCard::guestPointerBase) == 0,
        "disabled absolute-pointer integration must not advertise or publish host coordinates");
    CuteMacAcceleratedVideoCard acceleratedCard(832, 624, 8, 4096, true, true);
    ok &= expect(acceleratedCard.id() == QStringLiteral("nubus-video-cutemac-accelerated")
            && acceleratedCard.accelerationEnabled()
            && acceleratedCard.declarationRom().size() == 4096
            && acceleratedCard.declarationRom() != virtualCard.declarationRom()
            && acceleratedCard.declarationRom().contains(QByteArray::fromHex("a746"))
            && acceleratedCard.declarationRom().contains(QByteArray::fromHex("a647"))
            && acceleratedCard.read8(0x00f00000 + 0x20) == static_cast<std::uint8_t>(acceleratedCard.declarationRom()[0x20])
            && acceleratedCard.videoFrame().bitsPerPixel == 1,
        "accelerated adapter must carry a separate ROM-resident Toolbox hook");
    ok &= expect(!acceleratedCard.videoFrame().grabbable,
        "accelerated video with integrated absolute pointer must not request host mouse grabbing");
    acceleratedCard.write8(0x000e0000, 3);
    acceleratedCard.setHostPointerPosition(123, 234);
    ok &= expect(acceleratedCard.videoFrame().bitsPerPixel == 8
            && acceleratedCard.read8(CuteMacAcceleratedVideoCard::guestPointerBase + 2) == 0x00
            && acceleratedCard.read8(CuteMacAcceleratedVideoCard::guestPointerBase + 3) == 123,
        "accelerated adapter must preserve mode and absolute-pointer behavior");
    acceleratedCard.write8(0x00900000, 0xa5);
    ok &= expect(acceleratedCard.read8(0x00000000) == 0xa5
            && static_cast<std::uint8_t>(acceleratedCard.videoFrame().pixels[0]) == 0xa5,
        "accelerated adapter must preserve the 24-bit slot-9 framebuffer mirror");
    const auto accel = CuteMacAcceleratedVideoCard::acceleratorBase;
    using AccelRegister = CuteMacAcceleratedVideoCard::AcceleratorRegister;
    const auto accelRegister = [accel](AccelRegister reg) { return accel + static_cast<std::uint32_t>(reg); };
    ok &= expect(readCard32(acceleratedCard, accelRegister(AccelRegister::Signature)) == 0x43564131U
            && readCard32(acceleratedCard, accelRegister(AccelRegister::Capabilities))
                == (CuteMacAcceleratedVideoCard::capabilityVramCopy
                    | CuteMacAcceleratedVideoCard::capabilitySolidFill
                    | CuteMacAcceleratedVideoCard::capabilityMonochromeExpand),
        "accelerated adapter must expose the versioned CVA1 copy protocol");
    acceleratedCard.write32(accelRegister(AccelRegister::GuestCallback), 0x00123456U);
    ok &= expect(readCard32(acceleratedCard, accelRegister(AccelRegister::GuestCallback)) == 0x00123456U,
        "accelerated adapter must retain the guest-tools slot-VBL callback pointer");
    acceleratedCard.reset();
    ok &= expect(readCard32(acceleratedCard, accelRegister(AccelRegister::GuestCallback)) == 0,
        "accelerated adapter reset must clear stale guest callback pointers");
    for (std::uint32_t index = 0; index < 16; ++index) acceleratedCard.write8(0x100 + index, static_cast<std::uint8_t>(index));
    acceleratedCard.write32(accelRegister(AccelRegister::SourceOffset), 0x100);
    acceleratedCard.write32(accelRegister(AccelRegister::DestinationOffset), 0x200);
    acceleratedCard.write32(accelRegister(AccelRegister::StrideBytes), 8);
    acceleratedCard.write32(accelRegister(AccelRegister::SourceStrideBytes), 8);
    acceleratedCard.write32(accelRegister(AccelRegister::DestinationStrideBytes), 12);
    acceleratedCard.write32(accelRegister(AccelRegister::WidthBytes), 4);
    acceleratedCard.write32(accelRegister(AccelRegister::Height), 2);
    acceleratedCard.write32(accelRegister(AccelRegister::Flags), 0);
    acceleratedCard.write32(accelRegister(AccelRegister::Command), CuteMacAcceleratedVideoCard::commandVramCopy);
    ok &= expect(acceleratedCard.read8(0x200) == 0 && acceleratedCard.read8(0x203) == 3
            && acceleratedCard.read8(0x20c) == 8 && acceleratedCard.read8(0x20f) == 11
            && readCard32(acceleratedCard, accelRegister(AccelRegister::CommandsCompleted)) == 1
            && readCard32(acceleratedCard, accelRegister(AccelRegister::BytesCopied)) == 8,
        "accelerated adapter must copy validated VRAM rectangles");
    acceleratedCard.write32(accelRegister(AccelRegister::DestinationOffset), 0x300);
    acceleratedCard.write32(accelRegister(AccelRegister::DestinationStrideBytes), 8);
    acceleratedCard.write32(accelRegister(AccelRegister::WidthBytes), 5);
    acceleratedCard.write32(accelRegister(AccelRegister::Height), 2);
    acceleratedCard.write32(accelRegister(AccelRegister::FillValue), 0x5a);
    acceleratedCard.write32(accelRegister(AccelRegister::Flags), 0);
    acceleratedCard.write32(accelRegister(AccelRegister::Command), CuteMacAcceleratedVideoCard::commandSolidFill);
    ok &= expect(acceleratedCard.read8(0x300) == 0x5a && acceleratedCard.read8(0x304) == 0x5a
            && acceleratedCard.read8(0x308) == 0x5a && acceleratedCard.read8(0x30c) == 0x5a
            && acceleratedCard.read8(0x30d) == 0,
        "accelerated adapter must fill validated strided VRAM rectangles");
    acceleratedCard.write8(0x400, 0xa0);
    acceleratedCard.write32(accelRegister(AccelRegister::SourceOffset), 0x400);
    acceleratedCard.write32(accelRegister(AccelRegister::DestinationOffset), 0x500);
    acceleratedCard.write32(accelRegister(AccelRegister::SourceStrideBytes), 1);
    acceleratedCard.write32(accelRegister(AccelRegister::DestinationStrideBytes), 8);
    acceleratedCard.write32(accelRegister(AccelRegister::SourceBitOffset), 0);
    acceleratedCard.write32(accelRegister(AccelRegister::WidthPixels), 3);
    acceleratedCard.write32(accelRegister(AccelRegister::Height), 1);
    acceleratedCard.write32(accelRegister(AccelRegister::DestinationDepth), 8);
    acceleratedCard.write32(accelRegister(AccelRegister::ForegroundValue), 0xee);
    acceleratedCard.write32(accelRegister(AccelRegister::BackgroundValue), 0x11);
    acceleratedCard.write32(accelRegister(AccelRegister::Command),
        CuteMacAcceleratedVideoCard::commandMonochromeExpand);
    ok &= expect(acceleratedCard.read8(0x500) == 0xee && acceleratedCard.read8(0x501) == 0x11
            && acceleratedCard.read8(0x502) == 0xee,
        "accelerated adapter must expand validated monochrome sources into indexed VRAM");
    acceleratedCard.write32(accelRegister(AccelRegister::DestinationOffset), 4U * 1024U * 1024U - 2U);
    acceleratedCard.write32(accelRegister(AccelRegister::Command), CuteMacAcceleratedVideoCard::commandVramCopy);
    ok &= expect(readCard32(acceleratedCard, accelRegister(AccelRegister::CommandsRejected)) == 1
            && (readCard32(acceleratedCard, accelRegister(AccelRegister::Status))
                & CuteMacAcceleratedVideoCard::statusError) != 0,
        "accelerated adapter must reject out-of-range copies before modifying VRAM");
    virtualCard.write8(CuteMacVideoCard::guestServicesCommand, 1);
    ok &= expect(virtualCard.takePowerRequest() == cutemac::core::GuestPowerRequest::None,
        "CuteMac guest-services power-off must leave the guest shutdown screen interactive");
    virtualCard.tick(260608U * 480U - 1U);
    ok &= expect(virtualCard.takePowerRequest() == cutemac::core::GuestPowerRequest::None,
        "CuteMac guest-services power-off must not become a delayed host halt");
    virtualCard.write8(CuteMacVideoCard::guestServicesCommand, 2);
    ok &= expect(virtualCard.takePowerRequest() == cutemac::core::GuestPowerRequest::Restart,
        "CuteMac guest-services restart request");

    QTemporaryDir directory;
    const auto romPath = directory.filePath(QStringLiteral("342-0008-a.bin"));
    QFile rom(romPath);
    ok &= expect(rom.open(QIODevice::WriteOnly), "authentic card ROM fixture open");
    QByteArray authenticRom(4096, static_cast<char>(0xff));
    authenticRom[0] = 0x1e; // Reversed and inverted this becomes the 0xe1 lane descriptor.
    authenticRom[1] = static_cast<char>(0xff);
    authenticRom[4095] = static_cast<char>(0xfe);
    ok &= expect(rom.write(authenticRom) == 4096, "authentic card ROM fixture write");
    rom.close();

    MacintoshIIVideoCard authenticCard;
    ok &= expect(authenticCard.loadDeclarationRom(romPath), "authentic card must accept a 4 KiB declaration ROM");
    ok &= expect(authenticCard.read8(0x00ffc000) == 0x01, "authentic declaration ROM reversal and inversion");
    ok &= expect(authenticCard.read8(0x00fffffc) == 0xe1, "authentic declaration ROM byte-lane descriptor");
    ok &= expect(authenticCard.read8(0x00fffffd) == 0xff, "authentic declaration ROM unused byte lane");
    ok &= expect(authenticCard.videoFrame().pixelToColorIndex == QVector<std::uint16_t> { 0, 128 }
            && authenticCard.videoFrame().colorTable[128] == 0xff000000U,
        "authentic one-bit boot mode must use TFB's high-bit CLUT mapping");
    ok &= expect(authenticCard.videoFrame().grabbable,
        "authentic video cards must allow host mouse grabbing for relative ADB movement");
    authenticCard.write8(0x20, 0xaa);
    ok &= expect(static_cast<unsigned char>(authenticCard.videoFrame().pixels[0]) == 0x55, "authentic card inverted VRAM mapping");
    authenticCard.write8(0x0008003c, 0xcf);
    ok &= expect(authenticCard.videoFrame().storage == PixelStorage::Indexed && authenticCard.videoFrame().bitsPerPixel == 8,
        "authentic TFB mode register");
    authenticCard.write8(0x00080030, 0x00);
    authenticCard.write8(0x00080038, 0x00);
    authenticCard.write8(0x00080018, 0x00);
    authenticCard.write8(0x0008003c, 0xdf);
    ok &= expect(authenticCard.videoFrame().width == 640 && authenticCard.videoFrame().height == 480
            && authenticCard.videoFrame().bitsPerPixel == 4,
        "authentic card depth changes must preserve its fixed 640x480 raster");
    authenticCard.write8(0x00090004, 0xf5);
    authenticCard.write8(0x00090008, 0xed);
    authenticCard.write8(0x00090008, 0xcb);
    authenticCard.write8(0x00090008, 0xa9);
    authenticCard.write8(0x00090004, 0xf5);
    ok &= expect(authenticCard.read8(0x00090008) == 0xed
            && authenticCard.read8(0x00090008) == 0xcb
            && authenticCard.read8(0x00090008) == 0xa9,
        "authentic Bt453 palette entries must be readable by GetEntries");
    authenticCard.write8(0x00090004, 0xf5);
    ok &= expect(authenticCard.read8(0x00090008) == 0xed
            && authenticCard.read8(0x00090009) == 0xff
            && authenticCard.read8(0x0009000a) == 0xff
            && authenticCard.read8(0x0009000b) == 0xff
            && authenticCard.read8(0x00090008) == 0xcb,
        "undriven Bt453 lanes must not advance palette read state");
    authenticCard.tick(261379);
    ok &= expect(authenticCard.read8(0x000d0000) == 0xff
            && authenticCard.read8(0x000d0003) == 0xff,
        "authentic VBL IRQ must lead the active-low status by one scanline");
    authenticCard.tick(261379 / 525);
    ok &= expect(authenticCard.read8(0x000d0003) == 0x00,
        "authentic VBL status must become active after the scanline lead");
    authenticCard.tick((261379 * 45) / 525);
    ok &= expect(authenticCard.read8(0x000d0003) == 0xff,
        "authentic VBL status must clear after the blanking interval");
    authenticCard.write8(0x000a0000, 0x00);
    ok &= expect(authenticCard.vblEnabled(), "authentic VBL acknowledge register must enable interrupts");
    authenticCard.write8(0x000a0004, 0x00);
    const auto assertionsBeforeDisabledFrame = authenticCard.vblAssertions();
    authenticCard.tick(261379);
    ok &= expect(!authenticCard.vblEnabled()
            && authenticCard.vblAssertions() == assertionsBeforeDisabledFrame,
        "authentic VBL +4 register must disable slot interrupts");

    const auto displayRomPath = directory.filePath(QStringLiteral("3410868.bin"));
    QFile displayRom(displayRomPath);
    ok &= expect(displayRom.open(QIODevice::WriteOnly), "8-24 card ROM fixture open");
    QByteArray displayRomBytes(AppleDisplayCard::declarationRomBytes, static_cast<char>(0xff));
    displayRomBytes[0] = 0x12;
    displayRomBytes[AppleDisplayCard::declarationRomBytes - 1] = 0x78;
    ok &= expect(displayRom.write(displayRomBytes) == AppleDisplayCard::declarationRomBytes, "8-24 card ROM fixture write");
    displayRom.close();

    AppleDisplayCard displayCard(AppleDisplayCard::Variant::MacintoshDisplayCard824, 1024);
    ok &= expect(displayCard.id() == QStringLiteral("nubus-video-apple-mdc-824"), "8-24 card identity");
    ok &= expect(displayCard.loadDeclarationRom(displayRomPath), "8-24 card must accept a 32 KiB lane-3 ROM");
    ok &= expect(displayCard.read8(0x00fe0003) == 0x12
            && displayCard.read8(0x00fe0000) == 0xff
            && displayCard.read8(0x00ffffff) == 0x78,
        "8-24 declaration ROM must expand onto NuBus byte lane 3");
    ok &= expect(displayCard.read8(0x000e0003) == 0x12
            && displayCard.read8(0x000e0000) == 0xff
            && displayCard.read8(0x000fffff) == 0x78,
        "8-24 declaration ROM must also be visible in the IIcx 24-bit standard-slot window");
    ok &= expect(displayCard.vramBytes() == AppleDisplayCard::vram1MiB, "8-24 card must default to 1 MiB VRAM");
    ok &= expect(displayCard.videoFrame().grabbable,
        "8-24 card must allow host mouse grabbing for relative ADB movement");
    ok &= expect(displayCard.videoFrame().storage == PixelStorage::Indexed
            && displayCard.videoFrame().bitsPerPixel == 1
            && displayCard.videoFrame().width == 640
            && displayCard.videoFrame().height == 480,
        "8-24 card must reset to a usable one-bit 640x480 frame");
    ok &= expect((readCard32(displayCard, 0x002001c0) & 0x04U) == 0,
        "8-24 CRTC status must expose active-low sync instead of a stuck-high poll bit");
    displayCard.write32(0x00200000, 0x00000c40);
    ok &= expect(displayCard.control() == 0x0c40
            && readCard32(displayCard, 0x00200000) == 0x00000040,
        "8-24 JMFB control register must preserve monitor sense and transfer bits");
    displayCard.write16(0x00200000, 0x0040);
    ok &= expect(displayCard.control() == 0x0040,
        "8-24 16-bit register writes must preserve the guest value");
    displayCard.write32(0x00200200, 0x0000002a);
    displayCard.write32(0x00200204, 0x00000012);
    displayCard.write32(0x00200204, 0x00000034);
    displayCard.write32(0x00200204, 0x00000056);
    displayCard.write32(0x00200208, 0x00000018);
    displayCard.write8(0x00000000, 0x2a);
    ok &= expect(displayCard.ramdacMode() == 0x0c
            && displayCard.videoFrame().bitsPerPixel == 8
            && displayCard.videoFrame().colorTable[0x2a] == 0xff123456U
            && static_cast<std::uint8_t>(displayCard.videoFrame().pixels[0]) == 0x2a,
        "8-24 RAMDAC must select eight-bit indexed scanout with guest-programmed CLUT");
    displayCard.write32(0x0020000c, 0x00000130);
    displayCard.write32(0x0020010c, 0x0000067e);
    displayCard.write32(0x00200124, 0x000004e0);
    ok &= expect(displayCard.videoFrame().width == 832 && displayCard.videoFrame().height == 624,
        "8-24 CRTC writes must publish 832x624 RGB mode geometry");
    displayCard.write32(0x00200208, 0x0000001a);
    displayCard.write32(0x00200000, 0x00000044);
    displayCard.write32(0x00000000, 0x00123456);
    ok &= expect(displayCard.videoFrame().storage == PixelStorage::Direct
            && displayCard.videoFrame().bitsPerPixel == 24
            && displayCard.videoFrame().strideBytes == 0x130 * 8
            && static_cast<std::uint8_t>(displayCard.videoFrame().pixels[0]) == 0x12
            && static_cast<std::uint8_t>(displayCard.videoFrame().pixels[1]) == 0x34
            && static_cast<std::uint8_t>(displayCard.videoFrame().pixels[2]) == 0x56,
        "8-24 direct RGB mode must pack 24-bit pixels into VRAM");
    displayCard.write32(0x00200000, 0x00000074);
    ok &= expect(!displayCard.videoFrame().valid() && displayCard.unsupportedInterlaceSelections() == 1,
        "8-24 NTSC/PAL interlace/convolution paths must be explicitly blanked until implemented");
    AppleDisplayCard displayCard512(AppleDisplayCard::Variant::MacintoshDisplayCard824, 512);
    ok &= expect(displayCard512.vramBytes() == AppleDisplayCard::vram512KiB,
        "8-24/4-8 shared card model must retain 512 KiB VRAM configurations");

    return ok ? 0 : 1;
}
