#include <QFile>
#include <QTemporaryDir>

#include <cstdint>
#include <iostream>

#include "cutemac/devices/iwm/IwmController.h"

namespace {

using cutemac::devices::iwm::IwmController;

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

void selectDriveRegister(IwmController& iwm, std::uint8_t reg)
{
    (void)iwm.access((reg & 0x04) != 0 ? 1 : 0); // CA0
    (void)iwm.access((reg & 0x08) != 0 ? 3 : 2); // CA1
    (void)iwm.access((reg & 0x01) != 0 ? 5 : 4); // CA2
    iwm.setSideSelect((reg & 0x02) != 0);        // SEL
}

std::uint8_t readStatus(IwmController& iwm)
{
    (void)iwm.access(13); // Q6 on
    return iwm.access(14); // Read while taking Q7 low.
}

void writeMode(IwmController& iwm, std::uint8_t mode)
{
    (void)iwm.access(8); // IWM enable off
    (void)iwm.access(13); // Q6 on
    (void)iwm.access(15, mode, true); // Q7 on and write
    (void)iwm.access(14); // Latch through Q7 off, as the ROM does.
}

bool create800KImage(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(QByteArray(800 * 1024, '\0')) == 800 * 1024;
}

bool create1440KImage(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QByteArray bytes(1440 * 1024, '\0');
    for (int block = 0; block < bytes.size() / 512; ++block) {
        bytes[block * 512] = static_cast<char>(block >> 8);
        bytes[block * 512 + 1] = static_cast<char>(block);
    }
    return file.write(bytes) == bytes.size();
}

} // namespace

int main()
{
    bool ok = true;
    IwmController iwm;
    iwm.reset();

    writeMode(iwm, 0x1f);
    ok &= expect((readStatus(iwm) & 0x3f) == 0x1f, "mode bits must be readable while IWM is disabled");

    (void)iwm.access(9); // IWM enable on
    ok &= expect((readStatus(iwm) & 0x3f) == 0x3f, "status bit 5 must report IWM enable");

    QTemporaryDir directory;
    const auto imagePath = directory.filePath(QStringLiteral("test.dsk"));
    ok &= expect(directory.isValid() && create800KImage(imagePath), "test image creation failed");
    ok &= expect(iwm.loadFloppyImage(imagePath), "800K image must load");

    selectDriveRegister(iwm, 0x06); // WRTPRT
    ok &= expect((readStatus(iwm) & 0x80) != 0, "writable media must deassert write protection");

    selectDriveRegister(iwm, 0x09); // SIDES
    ok &= expect((readStatus(iwm) & 0x80) != 0, "800K media must report a double-sided drive");

    selectDriveRegister(iwm, 0x08); // MOTORON
    ok &= expect((readStatus(iwm) & 0x80) != 0, "stopped drive must report MOTORON false");
    (void)iwm.access(7); // LSTRB on
    (void)iwm.access(6); // LSTRB off: execute MOTORON command
    ok &= expect(iwm.debugState().motorOn, "MOTORON strobe must start the selected drive");

    (void)iwm.access(8); // IWM enable off must not undo the drive command.
    ok &= expect(iwm.debugState().motorOn, "IWM disable must not stop the latched drive motor");
    ok &= expect((readStatus(iwm) & 0x80) == 0, "running drive must report MOTORON true");

    IwmController swim;
    swim.reset();
    const auto hdImagePath = directory.filePath(QStringLiteral("test-1440.dsk"));
    ok &= expect(create1440KImage(hdImagePath) && swim.loadFloppyImage(hdImagePath, true), "1.44 MB image must load");
    ok &= expect(swim.debugState().highDensity, "1.44 MB media must report high density");
    ok &= expect(swim.debugState().imageFormat == QStringLiteral("raw-1440k"), "1.44 MB format name");
    const auto mfmTrack = swim.trackBytesForDebug(0, 0);
    ok &= expect(mfmTrack.size() == 12500, "1.44 MB MFM track must span one 300 RPM revolution");
    ok &= expect(mfmTrack.mid(92, 4) == QByteArray::fromHex("c2c2c2fc"),
        "MFM index address mark must use illegal-clock C2 bytes");
    ok &= expect(mfmTrack.mid(160, 8) == QByteArray::fromHex("a1a1a1fe00000102"),
        "first MFM ID field must describe cylinder 0, side 0, sector 1");
    ok &= expect(mfmTrack.mid(204, 4) == QByteArray::fromHex("a1a1a1fb"),
        "first MFM data field must follow the documented gap");
    ok &= expect(mfmTrack.mid(823 + 160 - 148, 8) == QByteArray::fromHex("a1a1a1fe00000202"),
        "1.44 MB sectors must use the documented 101-byte inter-sector gap");
    for (int track = 0; track < 80; ++track) {
        for (int side = 0; side < 2; ++side) {
            const auto encodedTrack = swim.trackBytesForDebug(track, side);
            for (int sector = 0; sector < 18; ++sector) {
                const auto field = 148 + sector * 675;
                const auto block = (track * 2 + side) * 18 + sector;
                ok &= expect(static_cast<std::uint8_t>(encodedTrack[field + 16]) == track
                        && static_cast<std::uint8_t>(encodedTrack[field + 17]) == side
                        && static_cast<std::uint8_t>(encodedTrack[field + 18]) == sector + 1,
                    "every MFM ID field must identify its source sector");
                ok &= expect(static_cast<std::uint8_t>(encodedTrack[field + 60]) == (block >> 8)
                        && static_cast<std::uint8_t>(encodedTrack[field + 61]) == (block & 0xff),
                    "every MFM data field must contain the corresponding raw-image block");
            }
        }
    }

    // Enter ISM mode with the unmodified ROM's 1,0,1,1 sequence.
    for (const auto value : { 0x40, 0x00, 0x40, 0x40 }) (void)swim.access(15, static_cast<std::uint8_t>(value), true);
    (void)swim.access(7, 0xc2, true); // ISM + motor + internal drive
    swim.setSideSelect(true);
    (void)swim.access(4, 0xf6, true); // Select the active-low NoReady drive sense.
    (void)swim.access(7, 0x08, true); // read ACTION
    const auto handshake = swim.access(15);
    ok &= expect((handshake & 0x80) != 0, "MFM read action must make a byte available");
    ok &= expect((handshake & 0x01) != 0, "first synchronized MFM byte must be a mark");
    ok &= expect((handshake & 0x0c) == 0, "inserted spinning media must deassert NoReady");
    ok &= expect(swim.access(9) == 0xc2, "first synchronized MFM field must be the C2 index mark");

    return ok ? 0 : 1;
}
