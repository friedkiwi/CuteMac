#include <QFile>
#include <QTemporaryDir>

#include <iostream>

#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"
#include "cutemac/devices/video/nubus/MacintoshIIVideoCard.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    using cutemac::devices::video::PixelStorage;
    using cutemac::devices::video::nubus::CuteMacVideoCard;
    using cutemac::devices::video::nubus::MacintoshIIVideoCard;
    bool ok = true;

    CuteMacVideoCard virtualCard(832, 624, 8, 4, true);
    ok &= expect(virtualCard.declarationRom().size() == 4096, "CuteMac declaration ROM size");
    ok &= expect(virtualCard.declarationRom().mid(4090, 4).toHex() == QByteArray("5a932bc7"), "CuteMac declaration ROM test pattern");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("d5fc00080002")),
        "CuteMac declaration ROM driver must expose palette operations");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex(
            "0000002e00000000006800000000027003400000")),
        "CuteMac indexed mode parameters must publish the classic base PixMap layout");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("00ff005500110001")),
        "CuteMac video driver must map logical low-depth colors across the physical RAMDAC");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("4a056614")),
        "CuteMac video driver must treat nonzero SetGray mode as grayscale");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("31400010247808fc4ed2")),
        "CuteMac video driver must complete queued requests before calling JIODone");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("01030fff")),
        "CuteMac video driver must preserve the indexed white and black endpoints");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray(".CuteMac\0", 9))
            && virtualCard.declarationRom().contains(QByteArray::fromHex("a895")),
        "CuteMac declaration ROM must advertise guest services and install its shutdown callback");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("a075"))
            && virtualCard.declarationRom().contains(QByteArray::fromHex("a076"))
            && virtualCard.declarationRom().contains(QByteArray::fromHex("20780d284e90")),
        "CuteMac video driver must install, remove, and dispatch its slot VBL interrupt");
    ok &= expect(virtualCard.videoFrame().storage == PixelStorage::Indexed && virtualCard.videoFrame().bitsPerPixel == 1,
        "CuteMac video must reset to one-bit indexed mode");
    ok &= expect(virtualCard.videoFrame().pixelToColorIndex == QVector<std::uint16_t> { 0, 255 },
        "CuteMac one-bit mode must span the hardware color table");
    virtualCard.write8(0x00080000, 3);
    ok &= expect(virtualCard.videoFrame().storage == PixelStorage::Indexed && virtualCard.videoFrame().bitsPerPixel == 8,
        "CuteMac mode register must select eight-bit indexed mode");
    virtualCard.write8(0x00080000, 4);
    ok &= expect(virtualCard.videoFrame().bitsPerPixel == 8,
        "CuteMac mode register must reject depths above the configured maximum");
    virtualCard.write8(0x00080002, 0x2a);
    virtualCard.write8(0x00080003, 0x12);
    virtualCard.write8(0x00080004, 0x34);
    virtualCard.write8(0x00080005, 0x56);
    ok &= expect(virtualCard.videoFrame().colorTable[0x2a] == 0xff123456U,
        "CuteMac RAMDAC must publish guest-programmed indexed color");
    virtualCard.write8(0x00080002, 0x2a);
    ok &= expect(virtualCard.read8(0x00080003) == 0x12 && virtualCard.read8(0x00080004) == 0x34
            && virtualCard.read8(0x00080005) == 0x56,
        "CuteMac RAMDAC palette entries must be readable by GetEntries");
    ok &= expect(virtualCard.read8(CuteMacVideoCard::guestServicesBase) == 'C'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 1) == 'T'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 2) == 'M'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 3) == 'C'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 4) == 2
            && (virtualCard.read8(CuteMacVideoCard::guestServicesBase + 5) & 3) == 3,
        "CuteMac guest-services mailbox identity, shutdown, and absolute-pointer capabilities");
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
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("31ea0004082831ea0002082a")),
        "CuteMac video slot VBL must copy absolute pointer coordinates into MTemp");
    CuteMacVideoCard relativeCard(640, 480, 8, 4, true, false);
    relativeCard.setHostPointerPosition(100, 120);
    ok &= expect((relativeCard.read8(CuteMacVideoCard::guestServicesBase + 5) & 2) == 0
            && relativeCard.read8(CuteMacVideoCard::guestPointerBase) == 0,
        "disabled absolute-pointer integration must not advertise or publish host coordinates");
    virtualCard.write8(CuteMacVideoCard::guestServicesCommand, 1);
    ok &= expect(virtualCard.takePowerRequest() == cutemac::core::GuestPowerRequest::PowerOff
            && virtualCard.takePowerRequest() == cutemac::core::GuestPowerRequest::None,
        "CuteMac guest-services power-off request must be consumed once");
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

    return ok ? 0 : 1;
}
