#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <memory>

#include "cutemac/devices/scsi/ScsiBlockDevice.h"
#include "cutemac/devices/scsi/ScsiCdRomDevice.h"
#include "cutemac/devices/scsi/ncr5380/MacintoshNcr5380Bus.h"
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
        return { responseData };
    }

    QByteArray lastCdb;
    QByteArray lastDataOut;
    QByteArray responseData;
};

void sendByte(cutemac::devices::scsi::ncr5380::Ncr5380& controller, std::uint8_t value, bool dataOut)
{
    if (dataOut) (void)controller.readRegister(5, false);
    controller.writeRegister(0, dataOut, value);
    if (dataOut) return;
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
    ok &= expect(inquiry.data.mid(8, 8) == QByteArrayLiteral("CONNER  "), "20 MiB vendor identity is wrong");
    ok &= expect(inquiry.data.mid(16, 16) == QByteArrayLiteral("CP2025-20mb     "), "20 MiB product identity is wrong");
    const struct {
        std::uint64_t mib;
        const char* vendor;
        const char* product;
    } identities[] {
        { 20, "CONNER  ", "CP2025-20mb     " },
        { 40, "QUANTUM ", "GO40S           " },
        { 80, "QUANTUM ", "GO80S1          " },
        { 160, "QUANTUM ", "GO160S          " },
        { 230, "QUANTUM ", "LP240S          " },
        { 500, "QUANTUM ", "LPS540S         " },
        { 1024, "IBM     ", "DPES-31080      " },
    };
    for (const auto& expected : identities) {
        const auto actual = cutemac::devices::scsi::ScsiBlockDevice::identityForSize(expected.mib * 1024 * 1024);
        ok &= expect(actual.vendor == expected.vendor && actual.product == expected.product && actual.revision == "1.0 ",
            "capacity-specific SCSI identity is wrong");
    }
    const auto customIdentity = cutemac::devices::scsi::ScsiBlockDevice::identityForSize(123 * 1024 * 1024);
    ok &= expect(customIdentity.vendor == QByteArrayLiteral("QUANTUM ")
            && customIdentity.product == QByteArrayLiteral("FIREBALL1       "),
        "custom-size SCSI identity must use the generic Fireball personality");

    const auto modeSense = disk.executeCommand(QByteArray::fromHex("1a003000ff00"), {});
    ok &= expect(modeSense.status == 0, "Apple MODE SENSE failed");
    ok &= expect(modeSense.data.size() == 36 && static_cast<std::uint8_t>(modeSense.data[3]) == 8,
        "Apple MODE SENSE block descriptor is wrong");
    ok &= expect(static_cast<std::uint8_t>(modeSense.data[12]) == 0x30
            && static_cast<std::uint8_t>(modeSense.data[13]) == 0x16,
        "Apple mode page header is wrong");
    ok &= expect(modeSense.data.mid(14, 22) == QByteArrayLiteral("APPLE COMPUTER, INC   "), "Apple mode page vendor is wrong");
    const auto modeSenseDbd = disk.executeCommand(QByteArray::fromHex("1a083000ff00"), {});
    ok &= expect(modeSenseDbd.status == 0 && modeSenseDbd.data.size() == 28
            && modeSenseDbd.data[3] == 0
            && static_cast<std::uint8_t>(modeSenseDbd.data[4]) == 0x30,
        "Apple MODE SENSE DBD response is wrong");

    const auto modeSelect = disk.executeCommand(QByteArray::fromHex("150000000c00"), QByteArray(12, '\0'));
    ok &= expect(modeSelect.status == 0, "MODE SELECT failed");

    const auto formatPage = disk.executeCommand(QByteArray::fromHex("1a000300ff00"), {});
    ok &= expect(formatPage.status == 0 && formatPage.data.size() == 36
            && static_cast<std::uint8_t>(formatPage.data[3]) == 8
            && formatPage.data.mid(5, 3) == QByteArray::fromHex("00a000")
            && formatPage.data.mid(9, 3) == QByteArray::fromHex("000200")
            && static_cast<std::uint8_t>(formatPage.data[12]) == 0x83,
        "format-device mode page is wrong");
    const auto geometryPage = disk.executeCommand(QByteArray::fromHex("1a000400ff00"), {});
    ok &= expect(geometryPage.status == 0 && geometryPage.data.size() == 36
            && static_cast<std::uint8_t>(geometryPage.data[3]) == 8
            && static_cast<std::uint8_t>(geometryPage.data[12]) == 0x04,
        "rigid-disk geometry mode page is wrong");
    const auto geometryPageDbd = disk.executeCommand(QByteArray::fromHex("1a080400ff00"), {});
    ok &= expect(geometryPageDbd.status == 0 && geometryPageDbd.data.size() == 28
            && geometryPageDbd.data[3] == 0
            && static_cast<std::uint8_t>(geometryPageDbd.data[4]) == 0x04,
        "MODE SENSE DBD must suppress the direct-access block descriptor");

    const auto format = disk.executeCommand(QByteArray::fromHex("040000000000"), {});
    ok &= expect(format.status == 0, "FORMAT UNIT failed");
    const auto verify = disk.executeCommand(QByteArray::fromHex("2f000000000000000100"), {});
    ok &= expect(verify.status == 0, "VERIFY(10) failed");
    const auto defects = disk.executeCommand(QByteArray::fromHex("37000000000000000400"), {});
    ok &= expect(defects.status == 0 && defects.data == QByteArray::fromHex("00000000"), "READ DEFECT DATA failed");

    const QByteArray marker(512, static_cast<char>(0x5a));
    const auto write = disk.executeCommand(QByteArray::fromHex("0a0000000100"), marker);
    ok &= expect(write.status == 0, "WRITE(6) failed");
    const auto read10 = disk.executeCommand(QByteArray::fromHex("28000000000000000100"), {});
    ok &= expect(read10.status == 0 && read10.data == marker, "READ(10) failed");
    const QByteArray marker10(512, static_cast<char>(0xa5));
    const auto write10 = disk.executeCommand(QByteArray::fromHex("2a000000000100000100"), marker10);
    ok &= expect(write10.status == 0, "WRITE(10) failed");
    const auto readBack10 = disk.executeCommand(QByteArray::fromHex("28000000000100000100"), {});
    ok &= expect(readBack10.status == 0 && readBack10.data == marker10, "WRITE(10) data was not readable");
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
    const auto polledMediaChangedSense = cdRom.executeCommand(QByteArray::fromHex("030000001200"), {});
    ok &= expect(polledMediaChangedSense.status == 0
            && static_cast<std::uint8_t>(polledMediaChangedSense.data[2]) == 0x06
            && static_cast<std::uint8_t>(polledMediaChangedSense.data[12]) == 0x28,
        "directly polled CD insertion sense must report changed media");
    ok &= expect(cdRom.executeCommand(QByteArray::fromHex("000000000000"), {}).status == 0,
        "REQUEST SENSE must consume insertion unit attention");
    ok &= expect(cdRom.loadImage(isoPath), "CD-ROM reload for TEST UNIT READY polling failed");
    const auto mediaChanged = cdRom.executeCommand(QByteArray::fromHex("000000000000"), {});
    ok &= expect(mediaChanged.status == 0x02 && mediaChanged.senseKey == 0x06, "CD insertion must report unit attention");
    const auto mediaChangedSense = cdRom.executeCommand(QByteArray::fromHex("030000001200"), {});
    ok &= expect(mediaChangedSense.status == 0 && static_cast<std::uint8_t>(mediaChangedSense.data[12]) == 0x28,
        "CD insertion sense code must report changed media");
    ok &= expect(cdRom.loadImage(isoPath), "CD-ROM reload failed");
    cdRom.acknowledgeMediaChange();
    ok &= expect(cdRom.executeCommand(QByteArray::fromHex("000000000000"), {}).status == 0,
        "power-on acknowledgement must clear configured-media unit attention");
    const auto capacity = cdRom.executeCommand(QByteArray::fromHex("25000000000000000000"), {});
    ok &= expect(capacity.data == QByteArray::fromHex("0000000300000800"), "CD-ROM capacity must use 2048-byte blocks");
    const auto cdRead = cdRom.executeCommand(QByteArray::fromHex("28000000000200000100"), {});
    ok &= expect(cdRead.status == 0 && cdRead.data == QByteArray(2048, static_cast<char>(0x5a)), "READ(10) CD sector failed");
    const auto select512 = cdRom.executeCommand(QByteArray::fromHex("151000000c00"),
        QByteArray::fromHex("000000080000000000000200"));
    ok &= expect(select512.status == 0, "CD-ROM MODE SELECT(6) 512-byte negotiation failed");
    ok &= expect(cdRom.loadImage(isoPath), "runtime CD reload after block-size negotiation failed");
    cdRom.acknowledgeMediaChange();
    const auto capacity512 = cdRom.executeCommand(QByteArray::fromHex("25000000000000000000"), {});
    ok &= expect(capacity512.data == QByteArray::fromHex("0000000f00000200"),
        "negotiated CD-ROM capacity must use 512-byte logical blocks");
    const auto cdRead512 = cdRom.executeCommand(QByteArray::fromHex("28000000000800000400"), {});
    ok &= expect(cdRead512.status == 0 && cdRead512.data == QByteArray(2048, static_cast<char>(0x5a)),
        "negotiated 512-byte CD reads must map to the correct image bytes");
    const auto restore2048 = cdRom.executeCommand(QByteArray::fromHex("151000000c00"),
        QByteArray::fromHex("000000080000000000000800"));
    ok &= expect(restore2048.status == 0, "CD-ROM 2048-byte block-size restore failed");
    const auto toc = cdRom.executeCommand(QByteArray::fromHex("43000000000000001200"), {});
    ok &= expect(toc.status == 0 && toc.data.size() == 18 && static_cast<std::uint8_t>(toc.data[6]) == 1
            && static_cast<std::uint8_t>(toc.data[14]) == 0xaa,
        "single-track CD-ROM TOC is wrong");
    const auto cdMode = cdRom.executeCommand(QByteArray::fromHex("1a003f00ff00"), {});
    ok &= expect(cdMode.status == 0 && cdMode.data.contains(QByteArray::fromHex("0d06000d003c004b"))
            && cdMode.data.contains(QByteArray::fromHex("2a0e")),
        "CD-ROM mode pages are missing");
    ok &= expect((static_cast<std::uint8_t>(cdMode.data[2]) & 0x80) != 0,
        "CD-ROM MODE SENSE(6) must advertise write protection");
    const auto cdModeHeader = cdRom.executeCommand(QByteArray::fromHex("1a0000000c00"), {});
    ok &= expect(cdModeHeader.status == 0 && cdModeHeader.data.size() == 12
            && (static_cast<std::uint8_t>(cdModeHeader.data[2]) & 0x80) != 0
            && static_cast<std::uint8_t>(cdModeHeader.data[3]) == 8
            && cdModeHeader.data.mid(9, 3) == QByteArray::fromHex("000800"),
        "classic MODE SENSE(6) page-zero probe must report a protected 2048-byte medium");
    const auto cdModeHeaderDbd = cdRom.executeCommand(QByteArray::fromHex("1a0800000400"), {});
    ok &= expect(cdModeHeaderDbd.status == 0 && cdModeHeaderDbd.data.size() == 4
            && (static_cast<std::uint8_t>(cdModeHeaderDbd.data[2]) & 0x80) != 0
            && cdModeHeaderDbd.data[3] == 0,
        "MODE SENSE(6) DBD probe must omit the block descriptor");
    const auto cdMode10 = cdRom.executeCommand(QByteArray::fromHex("5a003f0000000000ff00"), {});
    ok &= expect(cdMode10.status == 0 && (static_cast<std::uint8_t>(cdMode10.data[3]) & 0x80) != 0,
        "CD-ROM MODE SENSE(10) must advertise write protection");
    const auto appleRaw = cdRom.executeCommand(QByteArray::fromHex("d80000000002000000010000"), {});
    ok &= expect(appleRaw.status == 0 && appleRaw.data.size() == 2352
            && appleRaw.data.mid(16, 2048) == QByteArray(2048, static_cast<char>(0x5a)),
        "AppleCD 0xD8 raw-sector command failed");
    cdRom.eject();
    ok &= expect(cdRom.selectable() && !cdRom.ready(), "empty CD-ROM drive must remain selectable");
    const auto polledEjectSense = cdRom.executeCommand(QByteArray::fromHex("030000001200"), {});
    ok &= expect(polledEjectSense.status == 0
            && static_cast<std::uint8_t>(polledEjectSense.data[2]) == 0x06
            && static_cast<std::uint8_t>(polledEjectSense.data[12]) == 0x28,
        "directly polled CD ejection sense must report changed media");
    ok &= expect(cdRom.executeCommand(QByteArray::fromHex("000000000000"), {}).senseKey == 0x02,
        "REQUEST SENSE must consume ejection unit attention");
    ok &= expect(cdRom.loadImage(isoPath), "CD-ROM reload before ejection TEST UNIT READY failed");
    cdRom.acknowledgeMediaChange();
    cdRom.eject();
    const auto ejected = cdRom.executeCommand(QByteArray::fromHex("000000000000"), {});
    ok &= expect(ejected.status == 0x02 && ejected.senseKey == 0x06, "CD ejection must report unit attention");
    const auto noMedia = cdRom.executeCommand(QByteArray::fromHex("000000000000"), {});
    ok &= expect(noMedia.status == 0x02 && noMedia.senseKey == 0x02, "empty CD-ROM must report not ready after unit attention");

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

    // A pseudo-DMA send has a final-byte pipeline: the host's last DACK
    // loads the byte, but the target cannot change phase until ACK is
    // released. The IIcx ROM observes the still-matching DATA OUT phase and
    // then clears DMA mode, which completes the command.
    controller.reset();
    controller.writeRegister(0, false, 0x01);
    controller.writeRegister(1, false, 0x04);
    controller.writeRegister(1, false, 0x00);
    for (const auto byte : QByteArray::fromHex("150000000400")) sendByte(controller, static_cast<std::uint8_t>(byte), false);
    controller.writeRegister(2, false, 0x02);
    for (const auto byte : QByteArray::fromHex("00000000")) sendByte(controller, static_cast<std::uint8_t>(byte), true);
    ok &= expect(controller.debugState().phase == QStringLiteral("data-out")
            && (controller.readRegister(5, false) & 0x08) != 0
            && (controller.readRegister(5, false) & 0x40) != 0,
        "pseudo-DMA DATA OUT must retain phase match and final DRQ until DMA stops");
    controller.writeRegister(2, false, 0x00);
    ok &= expect(controller.debugState().phase == QStringLiteral("status")
            && (controller.readRegister(5, false) & 0x08) == 0
            && target->lastDataOut == QByteArray::fromHex("00000000"),
        "stopping pseudo-DMA must advance to STATUS and report a phase mismatch");

    target->responseData = QByteArray(36, static_cast<char>(0x5a));
    controller.reset();
    controller.writeRegister(0, false, 0x01);
    controller.writeRegister(1, false, 0x04);
    controller.writeRegister(1, false, 0x00);
    for (const auto byte : QByteArray::fromHex("120000002400")) sendByte(controller, static_cast<std::uint8_t>(byte), false);
    QByteArray pseudoDmaInquiry;
    pseudoDmaInquiry.append(static_cast<char>(controller.readRegister(6, true)));
    ok &= expect(!controller.debugState().request && (controller.readRegister(4, false) & 0x20) == 0
            && controller.debugState().request,
        "pseudo-DMA DATA IN must expose a REQ release before the next byte");
    for (int byte = 1; byte < 36; ++byte) {
        ok &= expect((controller.readRegister(5, false) & 0x40) != 0,
            "pseudo-DMA DATA IN must reassert DRQ before each byte");
        pseudoDmaInquiry.append(static_cast<char>(controller.readRegister(6, true)));
    }
    ok &= expect(pseudoDmaInquiry == target->responseData,
        "pseudo-DMA DATA IN must transfer every INQUIRY byte");
    ok &= expect(controller.debugState().phase == QStringLiteral("status"),
        "pseudo-DMA DATA IN must complete its handshake and enter STATUS");

    using MacintoshBus = cutemac::devices::scsi::ncr5380::MacintoshNcr5380Bus;
    controller.reset();
    controller.writeRegister(0, false, 0xa5);
    MacintoshBus plusBus(controller, {
        MacintoshBus::RegisterLane::LeastSignificant,
        MacintoshBus::RegisterLane::LeastSignificant,
        false,
        true,
    });
    MacintoshBus macIIBus(controller, {
        MacintoshBus::RegisterLane::MostSignificant,
        MacintoshBus::RegisterLane::MostSignificant,
        true,
        true,
    });
    ok &= expect(plusBus.readRegister(6, 2) == 0x00a5,
        "Mac Plus word register reads must place one byte on the low lane");
    ok &= expect(macIIBus.readRegister(6, 2) == 0xa500,
        "Mac II word register reads must place one byte on the high lane");

    target->responseData = QByteArray::fromHex("11223344");
    controller.reset();
    controller.writeRegister(0, false, 0x01);
    controller.writeRegister(1, false, 0x04);
    controller.writeRegister(1, false, 0x00);
    for (const auto byte : QByteArray::fromHex("120000000400")) sendByte(controller, static_cast<std::uint8_t>(byte), false);
    ok &= expect(plusBus.readPseudoDma(2) == 0x0011 && controller.debugState().dataIndex == 1,
        "one Mac Plus word transaction must produce exactly one DACK transfer");

    controller.reset();
    controller.writeRegister(0, false, 0x01);
    controller.writeRegister(1, false, 0x04);
    controller.writeRegister(1, false, 0x00);
    for (const auto byte : QByteArray::fromHex("120000000400")) sendByte(controller, static_cast<std::uint8_t>(byte), false);
    ok &= expect(macIIBus.readPseudoDma(2) == 0x1122 && controller.debugState().dataIndex == 2,
        "one Mac II word burst must produce exactly two ordered DACK transfers");
    return ok ? 0 : 1;
}
