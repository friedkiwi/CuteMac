#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

#include <QCoreApplication>
#include <QFile>

#include "cutemac/machines/maciicx/MacIIcxMachine.h"
#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: CuteMacIIcxSmoke <rom> [cycles] [floppy-image] [framebuffer.pgm] [ram-code.bin] [scsi-disk]\n";
        return 2;
    }

    const auto cycles = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 10'000'000;
    cutemac::machines::maciicx::MacIIcxMachine machine(8 * 1024 * 1024);
    (void)machine.installNuBusCard(9, std::make_shared<cutemac::devices::video::nubus::CuteMacVideoCard>(640, 480, 8, 4, true));
    if (!machine.loadRomFile(QString::fromLocal8Bit(argv[1]), {QStringLiteral("maciicx.skip_ram_pattern_test")})) {
        std::cerr << "failed to load IIcx ROM\n";
        return 1;
    }
    if (argc >= 4 && !machine.loadFloppyImage(QString::fromLocal8Bit(argv[3]), true)) {
        std::cerr << "failed to load floppy image\n";
        return 1;
    }
    if (argc >= 7 && !machine.loadScsiDisk(0, QString::fromLocal8Bit(argv[6]), false)) {
        std::cerr << "failed to load SCSI disk image\n";
        return 1;
    }
    machine.reset();
    (void)machine.runCycles(cycles);
    if (qEnvironmentVariableIsSet("CUTEMAC_IICX_OPEN_UTILITIES")) {
        const auto moveMouse = [&machine](int dx, int dy) {
            while (dx != 0 || dy != 0) {
                const int stepX = std::clamp(dx, -32, 32);
                const int stepY = std::clamp(dy, -32, 32);
                machine.queueInput({cutemac::core::GuestInputEvent::Type::MouseDelta, stepX, stepY, false}, machine.cycleCount());
                (void)machine.runCycles(1'000'000);
                dx -= stepX;
                dy -= stepY;
            }
        };
        moveMouse(580, 45);
        for (int click = 0; click < 2; ++click) {
            machine.queueInput({cutemac::core::GuestInputEvent::Type::MouseButton, 0, 0, true}, machine.cycleCount());
            (void)machine.runCycles(1'000'000);
            machine.queueInput({cutemac::core::GuestInputEvent::Type::MouseButton, 0, 0, false}, machine.cycleCount());
            (void)machine.runCycles(1'000'000);
        }
        (void)machine.runCycles(50'000'000);
    }
    std::map<std::uint32_t, int> sampledPcs;
    for (int sample = 0; sample < 256; ++sample) {
        (void)machine.runCycles(1000);
        ++sampledPcs[machine.programCounter()];
    }
    const auto registers = machine.cpuRegisters();
    std::cout << "pc=0x" << std::hex << registers.pc << " sr=0x" << registers.sr
              << " sp=0x" << registers.a[7] << std::dec
              << " cycles=" << machine.cycleCount()
              << " overlay=" << (machine.overlayEnabled() ? "on" : "off") << '\n';
    for (int index = 0; index < 8; ++index) std::cout << "d" << index << "=0x" << std::hex << registers.d[index] << ((index == 7) ? '\n' : ' ');
    for (int index = 0; index < 8; ++index) std::cout << "a" << index << "=0x" << std::hex << registers.a[index] << ((index == 7) ? '\n' : ' ');
    std::cout << std::dec;
    std::cout << machine.disassemble(registers.pc).toStdString() << '\n';
    const auto via1 = machine.via1DebugState();
    const auto via2 = machine.via2DebugState();
    const auto adb = machine.adbDebugState();
    std::cout << "via1 ifr=0x" << std::hex << static_cast<int>(via1.interruptFlags)
              << " ier=0x" << static_cast<int>(via1.interruptEnable)
              << " irq=" << via1.interruptActive
              << " acr=0x" << static_cast<int>(via1.auxiliaryControl)
              << " sr=0x" << static_cast<int>(via1.shiftRegister)
              << " pa=0x" << static_cast<int>(via1.portA)
              << " pb=0x" << static_cast<int>(via1.portB)
              << " via2 ifr=0x" << static_cast<int>(via2.interruptFlags)
              << " ier=0x" << static_cast<int>(via2.interruptEnable)
              << " irq=" << via2.interruptActive
              << " scc_irq=" << machine.sccInterruptActive() << std::dec << '\n';
    std::cout << "adb state=" << static_cast<int>(adb.state) << " command=0x" << std::hex << static_cast<int>(adb.command)
              << std::dec << " response=" << adb.responseBytes << " cycles=" << adb.transferCycles
              << " pending=" << adb.commandPending << " tx=" << adb.transmittingFromVia << '\n';
    const auto frame = machine.videoFrame();
    const auto io = machine.ioStatistics();
    const auto changedPixels = std::count_if(frame.pixels.cbegin(), frame.pixels.cend(), [](char value) { return value != 0; });
    std::cout << "video=" << frame.width << 'x' << frame.height << " stride=" << frame.strideBytes
              << " bytes=" << frame.pixels.size() << " changed=" << changedPixels << '\n';
    std::cout << "io scsi=" << io.scsiReads << '/' << io.scsiWrites
              << " swim=" << io.swimReads << '/' << io.swimWrites
              << " nubus=" << io.nubusReads << '/' << io.nubusWrites << '\n';
    std::vector<std::pair<int, std::uint32_t>> rankedPcs;
    for (const auto& [pc, count] : sampledPcs) rankedPcs.emplace_back(count, pc);
    std::sort(rankedPcs.rbegin(), rankedPcs.rend());
    std::cout << "sampled-pcs";
    for (std::size_t index = 0; index < std::min<std::size_t>(8, rankedPcs.size()); ++index) {
        std::cout << " 0x" << std::hex << rankedPcs[index].second << ':' << std::dec << rankedPcs[index].first;
    }
    std::cout << '\n';
    for (std::size_t index = 0; index < std::min<std::size_t>(8, rankedPcs.size()); ++index) {
        std::cout << "  0x" << std::hex << rankedPcs[index].second << " "
                  << machine.disassemble(rankedPcs[index].second).toStdString() << std::dec << '\n';
    }
    if (argc >= 5 && frame.valid() && frame.format == cutemac::devices::video::PixelFormat::Indexed1) {
        QByteArray pgm = QByteArray("P5\n") + QByteArray::number(frame.width) + ' ' + QByteArray::number(frame.height) + "\n255\n";
        pgm.reserve(pgm.size() + frame.width * frame.height);
        for (int y = 0; y < frame.height; ++y) {
            const auto* row = reinterpret_cast<const unsigned char*>(frame.pixels.constData() + y * frame.strideBytes);
            for (int x = 0; x < frame.width; ++x) pgm.append((row[x >> 3] & (0x80 >> (x & 7))) ? char(0) : char(255));
        }
        QFile output(QString::fromLocal8Bit(argv[4]));
        if (output.open(QIODevice::WriteOnly)) output.write(pgm);
    }
    if (argc >= 6) {
        QByteArray dump(0x10000, 0);
        for (std::uint32_t offset = 0; offset < 0x10000; ++offset) dump[static_cast<qsizetype>(offset)] = static_cast<char>(machine.read8(0x10000 + offset));
        QFile output(QString::fromLocal8Bit(argv[5]));
        if (output.open(QIODevice::WriteOnly)) output.write(dump);
    }
    return 0;
}
