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
    ok &= expect(virtualCard.declarationRom().contains(QByteArray::fromHex("d5fc00800002")),
        "CuteMac declaration ROM driver must expose palette operations");
    ok &= expect(virtualCard.declarationRom().contains(QByteArray(".CuteMac\0", 9))
            && virtualCard.declarationRom().contains(QByteArray::fromHex("a895")),
        "CuteMac declaration ROM must advertise guest services and install its shutdown callback");
    ok &= expect(virtualCard.videoFrame().storage == PixelStorage::Indexed && virtualCard.videoFrame().bitsPerPixel == 1,
        "CuteMac video must reset to one-bit indexed mode");
    ok &= expect(virtualCard.videoFrame().pixelToColorIndex == QVector<std::uint16_t> { 0, 255 },
        "CuteMac one-bit mode must span the hardware color table");
    virtualCard.write8(0x00800000, 3);
    ok &= expect(virtualCard.videoFrame().storage == PixelStorage::Indexed && virtualCard.videoFrame().bitsPerPixel == 8,
        "CuteMac mode register must select eight-bit indexed mode");
    virtualCard.write8(0x00800000, 4);
    ok &= expect(virtualCard.videoFrame().bitsPerPixel == 8,
        "CuteMac mode register must reject depths above the configured maximum");
    virtualCard.write8(0x00800002, 0x2a);
    virtualCard.write8(0x00800003, 0x12);
    virtualCard.write8(0x00800004, 0x34);
    virtualCard.write8(0x00800005, 0x56);
    ok &= expect(virtualCard.videoFrame().colorTable[0x2a] == 0xff123456U,
        "CuteMac RAMDAC must publish guest-programmed indexed color");
    virtualCard.write8(0x00800002, 0x2a);
    ok &= expect(virtualCard.read8(0x00800003) == 0x12 && virtualCard.read8(0x00800004) == 0x34
            && virtualCard.read8(0x00800005) == 0x56,
        "CuteMac RAMDAC palette entries must be readable by GetEntries");
    ok &= expect(virtualCard.read8(CuteMacVideoCard::guestServicesBase) == 'C'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 1) == 'T'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 2) == 'M'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 3) == 'C'
            && virtualCard.read8(CuteMacVideoCard::guestServicesBase + 4) == 1
            && (virtualCard.read8(CuteMacVideoCard::guestServicesBase + 5) & 1) != 0,
        "CuteMac guest-services mailbox identity and clean-shutdown capability");
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
    ok &= expect(rom.write(QByteArray(4096, static_cast<char>(0xa5))) == 4096, "authentic card ROM fixture write");
    rom.close();

    MacintoshIIVideoCard authenticCard;
    ok &= expect(authenticCard.loadDeclarationRom(romPath), "authentic card must accept a 4 KiB declaration ROM");
    ok &= expect(authenticCard.read8(0x00f00000) == 0xa5, "authentic declaration ROM mapping");
    ok &= expect(authenticCard.videoFrame().pixelToColorIndex == QVector<std::uint16_t> { 0, 128 }
            && authenticCard.videoFrame().colorTable[128] == 0xff000000U,
        "authentic one-bit boot mode must use TFB's high-bit CLUT mapping");
    authenticCard.write8(0x20, 0xaa);
    ok &= expect(static_cast<unsigned char>(authenticCard.videoFrame().pixels[0]) == 0x55, "authentic card inverted VRAM mapping");
    authenticCard.write8(0x0008003c, 0xcf);
    ok &= expect(authenticCard.videoFrame().storage == PixelStorage::Indexed && authenticCard.videoFrame().bitsPerPixel == 8,
        "authentic TFB mode register");

    return ok ? 0 : 1;
}
