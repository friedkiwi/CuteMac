#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

#include <QCoreApplication>
#include <QFile>

#include "cutemac/machines/maciicx/MacIIcxMachine.h"
#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"
#include "cutemac/devices/video/nubus/MacintoshIIVideoCard.h"

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: CuteMacIIcxSmoke <rom> [cycles] [floppy-image] [framebuffer.pgm] [ram-code.bin] [scsi-disk]\n";
        return 2;
    }

    const auto cycles = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 10'000'000;
    cutemac::machines::maciicx::MacIIcxMachine machine(8 * 1024 * 1024);
    std::shared_ptr<cutemac::devices::video::nubus::MacintoshIIVideoCard> authenticVideo;
    if (qEnvironmentVariableIsSet("CUTEMAC_IICX_AUTHENTIC_VIDEO")) {
        authenticVideo = std::make_shared<cutemac::devices::video::nubus::MacintoshIIVideoCard>();
        const auto path = qEnvironmentVariable("CUTEMAC_IICX_VIDEO_ROM", QStringLiteral("work/roms/342-0008-a.bin"));
        if (!authenticVideo->loadDeclarationRom(path) || !machine.installNuBusCard(9, authenticVideo)) {
            std::cerr << "failed to load authentic Macintosh II video card ROM\n";
            return 1;
        }
    } else {
        (void)machine.installNuBusCard(9,
            std::make_shared<cutemac::devices::video::nubus::CuteMacVideoCard>(640, 480, 8, 4, true));
    }
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
        bool tracedCursorTask = false;
        int hostMouseX = 15;
        int hostMouseY = 15;
        machine.queueInput({cutemac::core::GuestInputEvent::Type::MousePosition, hostMouseX, hostMouseY, false}, machine.cycleCount());
        const auto moveMouse = [&machine, &tracedCursorTask, &hostMouseX, &hostMouseY](int dx, int dy) {
            while (dx != 0 || dy != 0) {
                const int stepX = std::clamp(dx, -32, 32);
                const int stepY = std::clamp(dy, -32, 32);
                hostMouseX += stepX;
                hostMouseY += stepY;
                machine.queueInput({cutemac::core::GuestInputEvent::Type::MousePosition, hostMouseX, hostMouseY, false}, machine.cycleCount());
                int cursorTaskHits = 0;
                if (!tracedCursorTask) {
                    const auto cursorTask = machine.read32(0x08ee);
                    int elapsed = 0;
                    while (elapsed < 1'000'000) {
                        if (machine.programCounter() == cursorTask) ++cursorTaskHits;
                        elapsed += machine.runCycles(1);
                    }
                    tracedCursorTask = true;
                } else {
                    (void)machine.runCycles(1'000'000);
                }
                const auto adb = machine.adbDebugState();
                std::cerr << "mouse-step " << stepX << ',' << stepY << " pending="
                          << adb.pendingMouseDx << ',' << adb.pendingMouseDy << " address="
                          << static_cast<int>(adb.mouseAddress) << " mtemp="
                          << machine.read16(0x0828) << ',' << machine.read16(0x082a)
                          << " ticks=" << machine.read32(0x016a)
                          << " cursor-hits=" << cursorTaskHits
                          << " controller=" << static_cast<int>(adb.state) << ',' << static_cast<int>(adb.command)
                          << ',' << adb.responseBytes << " data="
                          << static_cast<int>(machine.read8(machine.read32(0x0cf8) + 0x0164)) << ','
                          << static_cast<int>(machine.read8(machine.read32(0x0cf8) + 0x0165)) << '\n';
                dx -= stepX;
                dy -= stepY;
            }
        };
        const auto doubleClick = [&machine]() {
            for (int click = 0; click < 2; ++click) {
                machine.queueInput({cutemac::core::GuestInputEvent::Type::MouseButton, 0, 0, true}, machine.cycleCount());
                (void)machine.runCycles(1'000'000);
                machine.queueInput({cutemac::core::GuestInputEvent::Type::MouseButton, 0, 0, false}, machine.cycleCount());
                (void)machine.runCycles(1'000'000);
            }
        };
        const auto click = [&machine]() {
            machine.queueInput({cutemac::core::GuestInputEvent::Type::MouseButton, 0, 0, true}, machine.cycleCount());
            (void)machine.runCycles(1'000'000);
            machine.queueInput({cutemac::core::GuestInputEvent::Type::MouseButton, 0, 0, false}, machine.cycleCount());
            (void)machine.runCycles(1'000'000);
        };
        moveMouse(580, 30);
        doubleClick();
        (void)machine.runCycles(50'000'000);
        if (qEnvironmentVariableIsSet("CUTEMAC_IICX_OPEN_HD_SC")) {
            moveMouse(-395, 60);
            doubleClick();
            (void)machine.runCycles(150'000'000);
            if (qEnvironmentVariableIsSet("CUTEMAC_IICX_HD_SC_INIT")) {
                moveMouse(46, 16);
                click();
                (void)machine.runCycles(50'000'000);
                if (qEnvironmentVariableIsSet("CUTEMAC_IICX_HD_SC_CONFIRM")) {
                    moveMouse(184, 74);
                    click();
                    auto completed = machine.scsiDebugState().completedCommands;
                    for (int elapsed = 0; elapsed < 1'000'000'000; elapsed += 100'000) {
                        (void)machine.runCycles(100'000);
                        const auto state = machine.scsiDebugState();
                        if (state.completedCommands != completed) {
                            std::cerr << "scsi-command count=" << state.completedCommands
                                      << " cdb=" << state.lastCommand.toHex().toStdString()
                                      << " phase=" << state.phase.toStdString()
                                      << " status=" << static_cast<int>(state.status) << '\n';
                            completed = state.completedCommands;
                        }
                    }
                    if (qEnvironmentVariableIsSet("CUTEMAC_IICX_HD_SC_FINISH")) {
                        const auto keyStroke = [&machine](std::uint8_t key, bool shift = false) {
                            if (shift) machine.queueInput({cutemac::core::GuestInputEvent::Type::Key, 0x38, 0, true}, machine.cycleCount());
                            machine.queueInput({cutemac::core::GuestInputEvent::Type::Key, key, 0, true}, machine.cycleCount());
                            (void)machine.runCycles(500'000);
                            machine.queueInput({cutemac::core::GuestInputEvent::Type::Key, key, 0, false}, machine.cycleCount());
                            if (shift) machine.queueInput({cutemac::core::GuestInputEvent::Type::Key, 0x38, 0, false}, machine.cycleCount());
                            (void)machine.runCycles(500'000);
                        };
                        keyStroke(0x2e, true); // M
                        keyStroke(0x00);       // a
                        keyStroke(0x08);       // c
                        keyStroke(0x22);       // i
                        keyStroke(0x2d);       // n
                        keyStroke(0x11);       // t
                        keyStroke(0x1f);       // o
                        keyStroke(0x01);       // s
                        keyStroke(0x04);       // h
                        keyStroke(0x31);       // space
                        keyStroke(0x04, true);  // H
                        keyStroke(0x02, true);  // D
                        keyStroke(0x24);       // Return
                        (void)machine.runCycles(300'000'000);
                    }
                }
            }
            const auto scsi = machine.scsiDebugState();
            std::cerr << "scsi-final phase=" << scsi.phase.toStdString()
                      << " command=" << scsi.activeCommand.toHex().toStdString()
                      << " last=" << scsi.lastCommand.toHex().toStdString()
                      << " selected=" << scsi.selected << " request=" << scsi.request
                      << " data=" << scsi.dataIndex << '/' << scsi.dataLength
                      << " completed=" << scsi.completedCommands << '\n';
        }
    }
    std::map<std::uint32_t, int> sampledPcs;
    for (int sample = 0; sample < 256; ++sample) {
        (void)machine.runCycles(1000);
        ++sampledPcs[machine.programCounter()];
    }
    const auto registers = machine.cpuRegisters();
    std::cout << "pc=0x" << std::hex << registers.pc << " physical-pc=0x" << registers.physicalPc
              << " pmmu=" << registers.pmmuEnabled << " tc=0x" << registers.pmmuTc << " sr=0x" << registers.sr
              << " sp=0x" << registers.a[7] << std::dec
              << " cycles=" << machine.cycleCount()
              << " overlay=" << (machine.overlayEnabled() ? "on" : "off") << '\n';
    if (registers.physicalPc != registers.pc) {
        std::cout << "physical-instruction " << machine.disassemble(registers.physicalPc).toStdString() << '\n';
    }
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
              << " pending=" << adb.commandPending << " tx=" << adb.transmittingFromVia
              << " mouse-delta=" << adb.pendingMouseDx << ',' << adb.pendingMouseDy
              << " addresses=" << static_cast<int>(adb.keyboardAddress) << ',' << static_cast<int>(adb.mouseAddress) << '\n';
    const auto frame = machine.videoFrame();
    const auto io = machine.ioStatistics();
    const auto changedPixels = std::count_if(frame.pixels.cbegin(), frame.pixels.cend(), [](char value) { return value != 0; });
    std::cout << "video=" << frame.width << 'x' << frame.height << " stride=" << frame.strideBytes
              << " bytes=" << frame.pixels.size() << " changed=" << changedPixels << '\n';
    if (!frame.colorTable.isEmpty()) {
        std::cout << "video-clut";
        for (const auto index : { 0, 1, 0x7f, 0x80, 0xfe, 0xff }) {
            if (index < frame.colorTable.size()) std::cout << " [" << index << "]=0x" << std::hex << frame.colorTable[index];
        }
        std::cout << std::dec << '\n';
    }
    if (authenticVideo) {
        std::cout << "video-vbl enabled=" << authenticVideo->vblEnabled()
                  << " assertions=" << authenticVideo->vblAssertions()
                  << " acks=" << authenticVideo->vblAcks()
                  << " status-reads=" << authenticVideo->vblStatusReads() << '\n';
    }
    std::cout << "io scsi=" << io.scsiReads << '/' << io.scsiWrites
              << " swim=" << io.swimReads << '/' << io.swimWrites
              << " nubus=" << io.nubusReads << '/' << io.nubusWrites << '\n';
    std::cout << "mouse-lowmem";
    for (std::uint32_t address = 0x0828; address <= 0x0836; address += 2) {
        std::cout << " 0x" << std::hex << address << "=0x" << machine.read16(address);
    }
    std::cout << std::dec << '\n';
    std::cout << "cursor-lowmem";
    for (std::uint32_t address = 0x08cc; address <= 0x08d3; ++address) {
        std::cout << " 0x" << std::hex << address << "=0x" << static_cast<int>(machine.read8(address));
    }
    std::cout << " ticks=0x" << machine.read32(0x016a)
              << " vbl-queue=0x" << machine.read32(0x0160)
              << "/0x" << machine.read32(0x0162)
              << " cursor-task=0x" << machine.read32(0x08ee)
              << " video-irq=0x" << static_cast<int>(machine.read8(0xf9800001)) << std::dec << '\n';
    auto vblNode = machine.read32(0x0162);
    for (int index = 0; index < 8 && vblNode != 0; ++index) {
        std::cout << "vbl-node address=0x" << std::hex << vblNode
                  << " next=0x" << machine.read32(vblNode)
                  << " type=0x" << machine.read16(vblNode + 4)
                  << " routine=0x" << machine.read32(vblNode + 6)
                  << " count=0x" << machine.read16(vblNode + 10)
                  << " phase=0x" << machine.read16(vblNode + 12) << std::dec << '\n';
        vblNode = machine.read32(vblNode);
    }
    QByteArray cursorRoutine(256, 0);
    const auto cursorTask = machine.read32(0x08ee);
    for (qsizetype index = 0; index < cursorRoutine.size(); ++index) {
        cursorRoutine[index] = static_cast<char>(machine.read8(cursorTask + static_cast<std::uint32_t>(index)));
    }
    QFile cursorDump(QStringLiteral("work/iicx-cursor-task.bin"));
    if (cursorDump.open(QIODevice::WriteOnly)) cursorDump.write(cursorRoutine);
    const auto unitTable = machine.read32(0x011c);
    const auto unitCount = machine.read16(0x01d2);
    std::cout << "unit-table=0x" << std::hex << unitTable << " count=0x" << unitCount << '\n';
    for (std::uint16_t unit = 0; unit < unitCount; ++unit) {
        const auto dceHandle = machine.read32(unitTable + static_cast<std::uint32_t>(unit) * 4);
        if (dceHandle == 0) continue;
        const auto dce = machine.read32(dceHandle) & 0x00ffffffU;
        if (dce == 0 || machine.read8(dce + 40) != 9) continue;
        const auto storage = machine.read32(dce + 20);
        std::cout << "slot9-dce unit=0x" << unit << " handle=0x" << dceHandle
                  << " dce=0x" << dce << " storage=0x" << storage
                  << " devid=0x" << static_cast<int>(machine.read8(dce + 41))
                  << " base=0x" << machine.read32(dce + 42);
        if (storage != 0) {
            std::cout << " sq-next=0x" << machine.read32(storage)
                      << " type=0x" << machine.read16(storage + 4)
                      << " priority=0x" << machine.read16(storage + 6)
                      << " routine=0x" << machine.read32(storage + 8)
                      << " parameter=0x" << machine.read32(storage + 12)
                      << " installed=0x" << static_cast<int>(machine.read8(storage + 16));
        }
        std::cout << '\n';
    }
    std::cout << std::dec;
    const auto adbBase = machine.read32(0x0cf8);
    std::cout << "adb-lowmem base=0x" << std::hex << adbBase;
    if (adbBase != 0 && adbBase != 0xffffffffU) {
        for (std::uint32_t offset = 0x015c; offset <= 0x016f; ++offset) {
            std::cout << " +" << offset << '=' << static_cast<int>(machine.read8(adbBase + offset));
        }
    }
    std::cout << std::dec << '\n';
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
    if (argc >= 5 && frame.valid() && frame.storage == cutemac::devices::video::PixelStorage::Indexed
        && (frame.bitsPerPixel == 1 || frame.bitsPerPixel == 2
            || frame.bitsPerPixel == 4 || frame.bitsPerPixel == 8)) {
        QByteArray pgm = QByteArray("P5\n") + QByteArray::number(frame.width) + ' ' + QByteArray::number(frame.height) + "\n255\n";
        pgm.reserve(pgm.size() + frame.width * frame.height);
        for (int y = 0; y < frame.height; ++y) {
            const auto* row = reinterpret_cast<const unsigned char*>(frame.pixels.constData() + y * frame.strideBytes);
            for (int x = 0; x < frame.width; ++x) {
                const auto bitOffset = x * frame.bitsPerPixel;
                const auto mask = (1U << frame.bitsPerPixel) - 1U;
                const auto pixel = (row[bitOffset >> 3] >> (8 - frame.bitsPerPixel - (bitOffset & 7))) & mask;
                const auto colorIndex = pixel < static_cast<unsigned>(frame.pixelToColorIndex.size())
                    ? frame.pixelToColorIndex[static_cast<int>(pixel)] : pixel;
                const auto color = colorIndex < static_cast<unsigned>(frame.colorTable.size())
                    ? frame.colorTable[static_cast<int>(colorIndex)] : 0xff000000U;
                const auto red = (color >> 16) & 0xff;
                const auto green = (color >> 8) & 0xff;
                const auto blue = color & 0xff;
                pgm.append(static_cast<char>((red * 77 + green * 150 + blue * 29) >> 8));
            }
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
