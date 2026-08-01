#include <QFile>
#include <QTemporaryDir>

#include <iostream>

#include "cutemac/devices/scsi/ScsiBlockDevice.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
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

    const auto format = disk.executeCommand(QByteArray::fromHex("040000000000"), {});
    ok &= expect(format.status == 0, "FORMAT UNIT failed");
    return ok ? 0 : 1;
}
