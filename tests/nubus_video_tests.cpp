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
    using cutemac::devices::video::PixelFormat;
    using cutemac::devices::video::nubus::CuteMacVideoCard;
    using cutemac::devices::video::nubus::MacintoshIIVideoCard;
    bool ok = true;

    CuteMacVideoCard virtualCard(832, 624, 8, 4, true);
    ok &= expect(virtualCard.declarationRom().size() == 4096, "CuteMac declaration ROM size");
    ok &= expect(virtualCard.declarationRom().mid(4090, 4).toHex() == QByteArray("5a932bc7"), "CuteMac declaration ROM test pattern");
    ok &= expect(virtualCard.videoFrame().format == PixelFormat::Indexed1, "CuteMac video must reset to one-bit mode");
    virtualCard.write8(0x00800000, 3);
    ok &= expect(virtualCard.videoFrame().format == PixelFormat::Indexed8, "CuteMac mode register must select eight-bit mode");

    QTemporaryDir directory;
    const auto romPath = directory.filePath(QStringLiteral("342-0008-a.bin"));
    QFile rom(romPath);
    ok &= expect(rom.open(QIODevice::WriteOnly), "authentic card ROM fixture open");
    ok &= expect(rom.write(QByteArray(4096, static_cast<char>(0xa5))) == 4096, "authentic card ROM fixture write");
    rom.close();

    MacintoshIIVideoCard authenticCard;
    ok &= expect(authenticCard.loadDeclarationRom(romPath), "authentic card must accept a 4 KiB declaration ROM");
    ok &= expect(authenticCard.read8(0x00f00000) == 0xa5, "authentic declaration ROM mapping");
    authenticCard.write8(0x20, 0xaa);
    ok &= expect(static_cast<unsigned char>(authenticCard.videoFrame().pixels[0]) == 0x55, "authentic card inverted VRAM mapping");
    authenticCard.write8(0x0008003c, 0xcf);
    ok &= expect(authenticCard.videoFrame().format == PixelFormat::Indexed8, "authentic TFB mode register");

    return ok ? 0 : 1;
}
