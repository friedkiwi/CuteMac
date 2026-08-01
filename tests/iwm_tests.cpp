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

    return ok ? 0 : 1;
}
