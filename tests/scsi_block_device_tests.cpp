#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <memory>

#include "cutemac/devices/scsi/ScsiBlockDevice.h"
#include "cutemac/devices/scsi/ScsiCdRomDevice.h"
#include "cutemac/devices/scsi/ncr5380/Ncr5380.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

class RecordingTarget final : public cutemac::devices::scsi::ScsiTarget {
public:
    bool ready() const override { return true; }
    cutemac::devices::scsi::ScsiCommandResult executeCommand(const QByteArray& cdb, const QByteArray& dataOut) override
    {
        lastCdb = cdb;
        lastDataOut = dataOut;
        return {};
    }

    QByteArray lastCdb;
    QByteArray lastDataOut;
};

void sendByte(cutemac::devices::scsi::ncr5380::Ncr5380& controller, std::uint8_t value, bool dataOut)
{
    controller.writeRegister(0, dataOut, value);
    controller.writeRegister(1, false, 0x11);
    controller.writeRegister(1, false, 0x01);
}

} // namespace

int main()
{
    bool ok = true;
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("disk.hda"));
    QFile image(path);
    ok &= expect(image.open(QIODevice::WriteOnly), "image fixture open failed");
    ok &= expect(image.resize(20 * 1024 * 1024), "image fixture resize failed");
    image.close();

    cutemac::devices::scsi::ScsiBlockDevice disk;
    ok &= expect(disk.loadImage(path), "disk image load failed");
    const auto inquiry = disk.executeCommand(QByteArray::fromHex("120000002400"), {});
    ok &= expect(inquiry.status == 0, "INQUIRY failed");
    ok &= expect(inquiry.data.size() == 36, "INQUIRY length is wrong");
    ok &= expect(inquiry.data.mid(8, 8) == QByteArrayLiteral(" SEAGATE"), "Apple-compatible vendor is wrong");
    ok &= expect(inquiry.data.mid(16, 16) == QByteArrayLiteral("          ST225N"), "Apple-compatible product is wrong");

    const auto modeSense = disk.executeCommand(QByteArray::fromHex("1a003000ff00"), {});
    ok &= expect(modeSense.status == 0, "Apple MODE SENSE failed");
    ok &= expect(modeSense.data.size() == 42, "Apple mode page length is wrong");
    ok &= expect(static_cast<std::uint8_t>(modeSense.data[4]) == 0x30 && static_cast<std::uint8_t>(modeSense.data[5]) == 0x24,
        "Apple mode page header is wrong");
    ok &= expect(modeSense.data.mid(14, 22) == QByteArrayLiteral("APPLE COMPUTER, INC   "), "Apple mode page vendor is wrong");

    const auto modeSelect = disk.executeCommand(QByteArray::fromHex("150000000c00"), QByteArray(12, '\0'));
    ok &= expect(modeSelect.status == 0, "MODE SELECT failed");

    const auto formatPage = disk.executeCommand(QByteArray::fromHex("1a000300ff00"), {});
    ok &= expect(formatPage.status == 0 && formatPage.data.size() == 28 && static_cast<std::uint8_t>(formatPage.data[4]) == 0x83,
        "format-device mode page is wrong");
    const auto geometryPage = disk.executeCommand(QByteArray::fromHex("1a000400ff00"), {});
    ok &= expect(geometryPage.status == 0 && geometryPage.data.size() == 28 && static_cast<std::uint8_t>(geometryPage.data[4]) == 0x04,
        "rigid-disk geometry mode page is wrong");

    const auto format = disk.executeCommand(QByteArray::fromHex("040000000000"), {});
    ok &= expect(format.status == 0, "FORMAT UNIT failed");
    const auto verify = disk.executeCommand(QByteArray::fromHex("2f000000000000000100"), {});
    ok &= expect(verify.status == 0, "VERIFY(10) failed");
    const auto defects = disk.executeCommand(QByteArray::fromHex("37000000000000000400"), {});
    ok &= expect(defects.status == 0 && defects.data == QByteArray::fromHex("00000000"), "READ DEFECT DATA failed");

    const QByteArray marker(512, static_cast<char>(0x5a));
    const auto write = disk.executeCommand(QByteArray::fromHex("0a0000000100"), marker);
    ok &= expect(write.status == 0, "WRITE(6) failed");
    QFile persisted(path);
    ok &= expect(persisted.open(QIODevice::ReadOnly) && persisted.read(512) == marker, "WRITE(6) was not persisted");

    const auto isoPath = directory.filePath(QStringLiteral("disc.iso"));
    QFile iso(isoPath);
    ok &= expect(iso.open(QIODevice::WriteOnly), "ISO fixture open failed");
    QByteArray isoBytes(4 * 2048, '\0');
    std::fill(isoBytes.begin() + 2 * 2048, isoBytes.begin() + 3 * 2048, static_cast<char>(0x5a));
    ok &= expect(iso.write(isoBytes) == isoBytes.size(), "ISO fixture write failed");
    iso.close();

    cutemac::devices::scsi::ScsiCdRomDevice cdRom;
    ok &= expect(cdRom.loadImage(isoPath), "CD-ROM image load failed");
    const auto cdInquiry = cdRom.executeCommand(QByteArray::fromHex("120000002400"), {});
    ok &= expect(cdInquiry.status == 0 && static_cast<std::uint8_t>(cdInquiry.data[0]) == 0x05
            && (static_cast<std::uint8_t>(cdInquiry.data[1]) & 0x80) != 0,
        "CD-ROM INQUIRY type is wrong");
    ok &= expect(cdInquiry.data.mid(8, 8) == QByteArrayLiteral("MATSHITA")
            && cdInquiry.data.mid(16, 16) == QByteArrayLiteral("CD-ROM CR-8004  "),
        "AppleCD-compatible identity is wrong");
    const auto capacity = cdRom.executeCommand(QByteArray::fromHex("25000000000000000000"), {});
    ok &= expect(capacity.data == QByteArray::fromHex("0000000300000800"), "CD-ROM capacity must use 2048-byte blocks");
    const auto cdRead = cdRom.executeCommand(QByteArray::fromHex("28000000000200000100"), {});
    ok &= expect(cdRead.status == 0 && cdRead.data == QByteArray(2048, static_cast<char>(0x5a)), "READ(10) CD sector failed");
    const auto toc = cdRom.executeCommand(QByteArray::fromHex("43000000000000001200"), {});
    ok &= expect(toc.status == 0 && toc.data.size() == 18 && static_cast<std::uint8_t>(toc.data[6]) == 1
            && static_cast<std::uint8_t>(toc.data[14]) == 0xaa,
        "single-track CD-ROM TOC is wrong");
    const auto cdMode = cdRom.executeCommand(QByteArray::fromHex("1a003f00ff00"), {});
    ok &= expect(cdMode.status == 0 && cdMode.data.contains(QByteArray::fromHex("0d06000d003c004b"))
            && cdMode.data.contains(QByteArray::fromHex("2a0e")),
        "CD-ROM mode pages are missing");

    auto target = std::make_shared<RecordingTarget>();
    cutemac::devices::scsi::ncr5380::Ncr5380 controller;
    controller.attachTarget(0, target);
    controller.reset();
    controller.writeRegister(0, false, 0x01);
    controller.writeRegister(1, false, 0x04);
    controller.writeRegister(1, false, 0x00);
    for (const auto byte : QByteArray::fromHex("041000000000")) sendByte(controller, static_cast<std::uint8_t>(byte), false);
    ok &= expect(controller.debugState().phase == QStringLiteral("data-out"), "FORMAT UNIT did not enter DATA OUT");
    for (const auto byte : QByteArray::fromHex("00000002")) sendByte(controller, static_cast<std::uint8_t>(byte), true);
    ok &= expect(controller.debugState().phase == QStringLiteral("data-out") && controller.debugState().expectedDataOutLength == 6,
        "FORMAT UNIT did not extend for its defect list");
    for (const auto byte : QByteArray::fromHex("1234")) sendByte(controller, static_cast<std::uint8_t>(byte), true);
    ok &= expect(controller.debugState().phase == QStringLiteral("status"), "FORMAT UNIT did not finish after its defect list");
    ok &= expect(target->lastDataOut == QByteArray::fromHex("000000021234"), "FORMAT UNIT parameter data was not delivered");
    return ok ? 0 : 1;
}
