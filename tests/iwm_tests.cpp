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

bool create400KImage(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(QByteArray(400 * 1024, '\0')) == 400 * 1024;
}

bool createPatternImage(const QString& path, qsizetype size, std::uint8_t seed)
{
    QByteArray bytes(size, '\0');
    for (qsizetype offset = 0; offset < 512; ++offset)
        bytes[offset] = static_cast<char>(seed + offset * 37);
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray {};
}

void writeBe32(QByteArray& bytes, qsizetype offset, std::uint32_t value)
{
    bytes[offset] = static_cast<char>(value >> 24);
    bytes[offset + 1] = static_cast<char>(value >> 16);
    bytes[offset + 2] = static_cast<char>(value >> 8);
    bytes[offset + 3] = static_cast<char>(value);
}

std::uint32_t readBe32(const QByteArray& bytes, qsizetype offset)
{
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) << 24)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 8)
        | static_cast<std::uint8_t>(bytes[offset + 3]);
}

std::uint32_t diskCopyChecksum(const QByteArray& bytes)
{
    std::uint32_t checksum = 0;
    for (qsizetype offset = 0; offset + 1 < bytes.size(); offset += 2) {
        checksum += (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) << 8)
            | static_cast<std::uint8_t>(bytes[offset + 1]);
        checksum = (checksum >> 1) | (checksum << 31);
    }
    return checksum;
}

bool createDiskCopyImage(const QString& path, qsizetype dataSize)
{
    QByteArray image(84 + dataSize, '\0');
    writeBe32(image, 64, static_cast<std::uint32_t>(dataSize));
    writeBe32(image, 68, 0);
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(image) == image.size();
}

bool startIwmMotor(IwmController& iwm)
{
    (void)iwm.access(9); // Enable IWM writes.
    selectDriveRegister(iwm, 0x08); // MOTORON
    (void)iwm.access(7);
    (void)iwm.access(6);
    return iwm.debugState().motorOn;
}

bool exerciseGcrWrite(const QString& sourcePath, const QString& targetPath, qsizetype imageSize)
{
    if (!createPatternImage(sourcePath, imageSize, 0x31)
        || !createPatternImage(targetPath, imageSize, 0x00)) return false;

    IwmController source;
    if (!source.loadFloppyImage(sourcePath, true)) return false;
    const auto track = source.trackBytesForDebug(0, 0);
    const auto prologue = track.indexOf(QByteArray::fromHex("d5aaad"));
    if (prologue < 0 || prologue + 710 > track.size()) return false;
    const auto record = track.mid(prologue, 710);

    IwmController target;
    if (!target.loadFloppyImage(targetPath) || !startIwmMotor(target)) return false;
    (void)target.access(13); // Q6 on: data-register write mode.
    for (const auto byte : record)
        (void)target.access(15, static_cast<std::uint8_t>(byte), true);
    (void)target.access(14); // Q7 off flushes the completed write transaction.
    return readFile(targetPath).left(512) == readFile(sourcePath).left(512);
}

bool exerciseMfmWrite(const QString& sourcePath, const QString& targetPath)
{
    if (!createPatternImage(sourcePath, 1440 * 1024, 0x57)
        || !createPatternImage(targetPath, 1440 * 1024, 0x00)) return false;

    IwmController source;
    if (!source.loadFloppyImage(sourcePath, true)) return false;
    const auto track = source.trackBytesForDebug(0, 0);
    const auto id = track.indexOf(QByteArray::fromHex("a1a1a1fe"));
    const auto data = track.indexOf(QByteArray::fromHex("a1a1a1fb"), id + 10);
    if (id < 0 || data < 0 || data + 518 > track.size()) return false;

    IwmController target;
    if (!target.loadFloppyImage(targetPath)) return false;
    for (const auto value : { 0x40, 0x00, 0x40, 0x40 })
        (void)target.access(15, static_cast<std::uint8_t>(value), true);
    (void)target.access(7, 0xd2, true); // ISM, internal motor, write ACTION.
    for (const auto byte : track.mid(id, 10))
        (void)target.access(8, static_cast<std::uint8_t>(byte), true);
    for (const auto byte : track.mid(data, 518))
        (void)target.access(8, static_cast<std::uint8_t>(byte), true);
    (void)target.access(6, 0x10, true); // End ACTION and flush.
    return readFile(targetPath).left(512) == readFile(sourcePath).left(512);
}

bool exerciseDiskCopyWrite(const QString& sourcePath, const QString& targetPath)
{
    if (!createPatternImage(sourcePath, 800 * 1024, 0x6b)
        || !createDiskCopyImage(targetPath, 800 * 1024)) return false;
    IwmController source;
    if (!source.loadFloppyImage(sourcePath, true)) return false;
    const auto track = source.trackBytesForDebug(0, 0);
    const auto prologue = track.indexOf(QByteArray::fromHex("d5aaad"));
    if (prologue < 0) return false;

    IwmController target;
    if (!target.loadFloppyImage(targetPath) || !startIwmMotor(target)) return false;
    (void)target.access(13);
    for (const auto byte : track.mid(prologue, 710))
        (void)target.access(15, static_cast<std::uint8_t>(byte), true);
    (void)target.access(14);
    const auto diskCopy = readFile(targetPath);
    return diskCopy.mid(84, 512) == readFile(sourcePath).left(512)
        && readBe32(diskCopy, 72) == diskCopyChecksum(diskCopy.mid(84, 800 * 1024));
}

bool exerciseReadOnlyWriteRejection(const QString& path)
{
    if (!createPatternImage(path, 400 * 1024, 0x22)) return false;
    const auto before = readFile(path);
    IwmController drive;
    if (!drive.loadFloppyImage(path, true) || !startIwmMotor(drive)) return false;
    (void)drive.access(13);
    for (int i = 0; i < 710; ++i) (void)drive.access(15, 0xff, true);
    (void)drive.access(14);
    return !drive.debugState().writable && readFile(path) == before;
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

bool createTruncatedRaw1440KHfsImage(const QString& path)
{
    QByteArray bytes(1143296, '\0');
    bytes[1024] = 0x42;
    bytes[1025] = 0x44;
    bytes[1042] = 0x0b;
    bytes[1043] = 0x3a;
    bytes[1046] = 0x02;
    bytes[1047] = 0x00;
    bytes[1052] = 0x00;
    bytes[1053] = 0x08;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
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

    QByteArray truncatedRaw800K(424895, '\0');
    truncatedRaw800K[1024] = 0x42;
    truncatedRaw800K[1025] = 0x44;
    truncatedRaw800K[1042] = 0x06;
    truncatedRaw800K[1043] = 0x3a;
    truncatedRaw800K[1046] = 0x02;
    truncatedRaw800K[1047] = 0x00;
    truncatedRaw800K[1052] = 0x00;
    truncatedRaw800K[1053] = 0x04;
    const auto truncatedPath = directory.filePath(QStringLiteral("truncated-800k.img"));
    QFile truncatedFile(truncatedPath);
    ok &= expect(truncatedFile.open(QIODevice::WriteOnly)
            && truncatedFile.write(truncatedRaw800K) == truncatedRaw800K.size(),
        "truncated raw 800K fixture creation failed");
    truncatedFile.close();
    IwmController truncatedIwm;
    truncatedIwm.reset();
    ok &= expect(truncatedIwm.loadFloppyImage(truncatedPath, true)
            && truncatedIwm.debugState().imageFormat == QStringLiteral("raw-800k")
            && truncatedIwm.debugState().doubleSided,
        "trailing-zero-truncated raw HFS floppy must load as 800K media");

    selectDriveRegister(iwm, 0x06); // WRTPRT
    ok &= expect(iwm.debugState().selectedRegister == 0x06,
        "external SEL must remain part of the Sony drive-register encoding");
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

    IwmController singleSided;
    singleSided.reset();
    const auto singleSidedPath = directory.filePath(QStringLiteral("test-400.dsk"));
    ok &= expect(create400KImage(singleSidedPath) && singleSided.loadFloppyImage(singleSidedPath, true),
        "400K image must load");
    ok &= expect(singleSided.debugState().imageFormat == QStringLiteral("raw-400k"), "400K format name");
    ok &= expect(!singleSided.debugState().doubleSided, "400K media must be single-sided");
    selectDriveRegister(singleSided, 0x09); // SIDES
    ok &= expect((readStatus(singleSided) & 0x80) == 0, "400K media must not report a double-sided drive");
    ok &= expect(startIwmMotor(singleSided), "400K internal drive motor must start with default drive-select polarity");
    (void)singleSided.access(12); // Q6 off
    (void)singleSided.access(14); // Q7 off: data-register read mode.
    QByteArray nibbles;
    for (int i = 0; i < 2048; ++i) {
        nibbles.append(static_cast<char>(singleSided.access(12)));
    }
    ok &= expect(nibbles.contains(QByteArray::fromHex("d5aa96")),
        "400K read stream must expose GCR address prologues from the inserted internal drive");
    ok &= expect(nibbles.contains(QByteArray::fromHex("d5aaad")),
        "400K read stream must expose GCR data prologues from the inserted internal drive");

    IwmController swim;
    swim.reset();
    const auto hdImagePath = directory.filePath(QStringLiteral("test-1440.dsk"));
    ok &= expect(create1440KImage(hdImagePath) && swim.loadFloppyImage(hdImagePath, true), "1.44 MB image must load");
    ok &= expect(swim.debugState().highDensity, "1.44 MB media must report high density");
    ok &= expect(swim.debugState().imageFormat == QStringLiteral("raw-1440k"), "1.44 MB format name");
    IwmController truncatedHd;
    truncatedHd.reset();
    const auto truncatedHdImagePath = directory.filePath(QStringLiteral("truncated-1440.img"));
    ok &= expect(createTruncatedRaw1440KHfsImage(truncatedHdImagePath)
            && truncatedHd.loadFloppyImage(truncatedHdImagePath, true)
            && truncatedHd.debugState().highDensity
            && truncatedHd.debugState().imageFormat == QStringLiteral("raw-1440k"),
        "trailing-zero-truncated raw HFS 1.44 MB floppy must load as high-density media");
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

    IwmController replacement;
    replacement.reset();
    ok &= expect(replacement.loadFloppyImage(imagePath, true), "800K image must load before replacement");
    for (const auto value : { 0x40, 0x00, 0x40, 0x40 }) {
        (void)replacement.access(15, static_cast<std::uint8_t>(value), true);
    }
    (void)replacement.access(7, 0xc2, true); // ISM + motor + internal drive.
    replacement.setSideSelect(true);
    (void)replacement.access(4, 0xfd, true); // Select GCR mode, matching a previous 800K access path.
    ok &= expect((replacement.access(13) & 0x04) != 0, "GCR phase command must select GCR setup");

    ok &= expect(replacement.loadFloppyImage(hdImagePath, true), "1.44 MB replacement image must load");
    ok &= expect(replacement.debugState().highDensity, "replacement media must report high density immediately");
    const auto activityBeforeRead = replacement.activityCounter();
    (void)replacement.access(4, 0xfc, true); // Select disk-change latch.
    ok &= expect((replacement.access(15) & 0x0c) == 0x0c, "media replacement must assert the SWIM disk-change latch");
    (void)replacement.access(4, 0xf4, true); // Drop the phase strobe before issuing the clear command.
    (void)replacement.access(4, 0xfc, true); // Strobe clear disk-change latch.
    ok &= expect((replacement.access(15) & 0x0c) == 0, "clear disk-change phase must clear the SWIM disk-change latch");
    (void)replacement.access(4, 0xf1, true); // Drop the phase strobe before selecting MFM mode.
    (void)replacement.access(4, 0xf9, true); // Select MFM mode.
    ok &= expect((replacement.access(13) & 0x04) == 0, "MFM phase command must clear stale GCR setup");
    (void)replacement.access(4, 0xff, true); // Select active-low HD aperture sense.
    ok &= expect((replacement.access(15) & 0x0c) == 0, "1.44 MB replacement must assert the active-low HD aperture sense");
    (void)replacement.access(7, 0x08, true); // read ACTION
    ok &= expect((replacement.access(15) & 0x80) != 0, "replacement MFM disk must make data available");
    ok &= expect(replacement.access(9) == 0xc2, "replacement disk must expose MFM index bytes, not stale 800K GCR data");
    ok &= expect(replacement.activityCounter() > activityBeforeRead, "floppy data reads must contribute to disk activity");

    ok &= expect(exerciseGcrWrite(directory.filePath(QStringLiteral("source-400.dsk")),
                     directory.filePath(QStringLiteral("written-400.dsk")), 400 * 1024),
        "IWM must decode and persist a valid 400K GCR sector write");
    ok &= expect(exerciseGcrWrite(directory.filePath(QStringLiteral("source-800.dsk")),
                     directory.filePath(QStringLiteral("written-800.dsk")), 800 * 1024),
        "IWM must decode and persist a valid 800K GCR sector write");
    ok &= expect(exerciseMfmWrite(directory.filePath(QStringLiteral("source-1440.dsk")),
                     directory.filePath(QStringLiteral("written-1440.dsk"))),
        "SWIM must validate and persist a valid 1.44 MB MFM sector write");
    ok &= expect(exerciseDiskCopyWrite(directory.filePath(QStringLiteral("source-dc42.dsk")),
                     directory.filePath(QStringLiteral("written.dc42"))),
        "GCR writes must preserve the Disk Copy 4.2 container and update its checksum");
    ok &= expect(exerciseReadOnlyWriteRejection(directory.filePath(QStringLiteral("readonly-400.dsk"))),
        "read-only media must reject writes without changing its backing file");

    return ok ? 0 : 1;
}
