#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <deque>
#include <vector>

#include <QCoreApplication>
#include <QString>

#include "cutemac/machines/quadra700/Quadra700Machine.h"

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: CuteMacQuadra700Smoke <rom> [cycles] [--cd <id> <image>|--floppy <image>|--disk <id> <image>|--disasm <address> <count>]\n";
        return 2;
    }

    const auto cycleBudget = argc >= 3 ? std::max<std::int64_t>(1, std::strtoll(argv[2], nullptr, 0)) : 10'000'000;
    bool ramKiBOk = false;
    const auto ramKiB = qEnvironmentVariable("CUTEMAC_Q700_RAM_KIB").toULongLong(&ramKiBOk, 0);
    const auto configuredRamBytes = static_cast<std::uint32_t>((ramKiBOk ? ramKiB : 8U * 1024U) * 1024ULL);
    cutemac::machines::quadra700::Quadra700Machine machine(configuredRamBytes);
    const auto patches = qEnvironmentVariableIsSet("CUTEMAC_Q700_SKIP_RAM_TEST")
        ? QStringList { QStringLiteral("quadra700.skip_ram_pattern_test") }
        : QStringList {};
    if (!machine.loadRomFile(QString::fromLocal8Bit(argv[1]), patches)) {
        std::cerr << "failed to load Quadra 700 ROM\n";
        return 1;
    }
    std::vector<std::pair<std::uint32_t, unsigned>> disassemblyRequests;
    for (int index = 3; index < argc;) {
        const auto option = QString::fromLocal8Bit(argv[index++]);
        if (option == QStringLiteral("--cd") && index + 1 < argc) {
            const auto id = QString::fromLocal8Bit(argv[index++]).toInt();
            if (!machine.loadScsiCdRom(id, QString::fromLocal8Bit(argv[index++]))) {
                std::cerr << "failed to load SCSI CD-ROM\n";
                return 1;
            }
        } else if (option == QStringLiteral("--disk") && index + 1 < argc) {
            const auto id = QString::fromLocal8Bit(argv[index++]).toInt();
            if (!machine.loadScsiDisk(id, QString::fromLocal8Bit(argv[index++]), false)) {
                std::cerr << "failed to load SCSI disk\n";
                return 1;
            }
        } else if (option == QStringLiteral("--floppy") && index < argc) {
            if (!machine.loadFloppyImage(QString::fromLocal8Bit(argv[index++]), true)) {
                std::cerr << "failed to load floppy image\n";
                return 1;
            }
        } else if (option == QStringLiteral("--disasm") && index + 1 < argc) {
            bool addressOk = false;
            bool countOk = false;
            const auto address = QString::fromLocal8Bit(argv[index++]).toUInt(&addressOk, 0);
            const auto count = QString::fromLocal8Bit(argv[index++]).toUInt(&countOk, 0);
            if (!addressOk || !countOk) {
                std::cerr << "invalid disassembly request\n";
                return 2;
            }
            disassemblyRequests.emplace_back(address, count);
        } else {
            std::cerr << "unknown or incomplete option: " << option.toStdString() << '\n';
            return 2;
        }
    }

    machine.reset();
    std::map<std::uint32_t, unsigned> sampledPcs;
    std::deque<std::uint32_t> recentPcs;
    std::uint64_t instructions = 0;
    const auto stopOnVideo = qEnvironmentVariableIsSet("CUTEMAC_Q700_STOP_ON_VIDEO");
    const auto stopOnBadPc = qEnvironmentVariableIsSet("CUTEMAC_Q700_STOP_ON_BAD_PC");
    const auto stopOnRamPc = qEnvironmentVariableIsSet("CUTEMAC_Q700_STOP_ON_RAM_PC");
    const auto stopOnBadSp = qEnvironmentVariableIsSet("CUTEMAC_Q700_STOP_ON_BAD_SP");
    bool stopPcOk = false;
    const auto stopPc = qEnvironmentVariable("CUTEMAC_Q700_STOP_PC").toUInt(&stopPcOk, 0);
    for (std::int64_t used = 0; used < cycleBudget;) {
        used += machine.stepInstruction();
        ++instructions;
        const auto pc = machine.programCounter();
        recentPcs.push_back(pc);
        if (recentPcs.size() > 24) recentPcs.pop_front();
        if ((instructions & 0xffU) == 0) ++sampledPcs[pc];
        if (stopPcOk && pc == stopPc) break;
        if (stopOnBadSp && !machine.overlayEnabled()) {
            const auto regs = machine.cpuRegisters();
            if (regs.a[7] > configuredRamBytes && regs.a[7] < 0x40000000U) break;
        }
        if (stopOnRamPc && !machine.overlayEnabled() && pc < 0x40000000U) break;
        if (stopOnBadPc && pc >= 0x10000000U && (pc & 0xf0000000U) != 0x40000000U) break;
        if (stopOnVideo && machine.videoFrame().valid()) break;
    }

    const auto regs = machine.cpuRegisters();
    const auto frame = machine.videoFrame();
    std::cout << std::hex << std::setfill('0')
              << "pc=0x" << std::setw(8) << regs.pc
              << " physical-pc=0x" << std::setw(8) << regs.physicalPc
              << " sr=0x" << std::setw(4) << regs.sr
              << " vbr=0x" << std::setw(8) << regs.vbr
              << " tc=0x" << std::setw(8) << regs.pmmuTc
              << " tt0=0x" << std::setw(8) << regs.pmmuTt0
              << " tt1=0x" << std::setw(8) << regs.pmmuTt1
              << " mmu=" << regs.pmmuEnabled
              << std::dec
              << " cycles=" << machine.cycleCount()
              << " instructions=" << instructions
              << " overlay=" << (machine.overlayEnabled() ? "on" : "off")
              << " video=" << frame.width << 'x' << frame.height << 'x' << frame.bitsPerPixel
              << " valid=" << frame.valid()
              << " disk-activity=" << machine.diskActivityCounter() << '\n';
    std::cout << machine.disassemble(regs.physicalPc).toStdString() << '\n';
    for (int index = 0; index < 8; ++index)
        std::cout << "d" << index << "=0x" << std::hex << regs.d[index] << (index == 7 ? '\n' : ' ');
    for (int index = 0; index < 8; ++index)
        std::cout << "a" << index << "=0x" << std::hex << regs.a[index] << (index == 7 ? '\n' : ' ');
    for (const auto& [address, count] : disassemblyRequests) {
        std::uint32_t cursor = address;
        for (unsigned line = 0; line < count; ++line) {
            std::cout << std::hex << std::setfill('0') << "0x" << std::setw(8) << cursor << ": "
                      << machine.disassemble(cursor).toStdString() << '\n';
            const auto bytes = machine.disassembleBytes(cursor);
            cursor += static_cast<std::uint32_t>(std::max(bytes, 2));
        }
    }
    std::cout << std::dec;
    std::cout << "vectors";
    for (std::uint32_t address = 0; address < 0x40; address += 4) {
        std::cout << " 0x" << std::hex << address << "=0x" << machine.debugRead32(address);
    }
    std::cout << std::dec << '\n';
    std::cout << "sampled-pcs";
    unsigned printed = 0;
    for (const auto& [pc, count] : sampledPcs) {
        if (printed++ >= 16) break;
        std::cout << " 0x" << std::hex << pc << ':' << std::dec << count;
    }
    std::cout << '\n';
    std::cout << "recent-pcs";
    for (const auto pc : recentPcs) std::cout << " 0x" << std::hex << pc;
    std::cout << std::dec << '\n';
    return 0;
}
