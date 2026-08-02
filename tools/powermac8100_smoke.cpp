#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <array>

#include <QCoreApplication>
#include <QFile>

#include "cutemac/machines/powermac8100/PowerMac8100Machine.h"
#include "cutemac/debug/SadMacDetector.h"
#include "cutemac/devices/serial/SerialEndpoint.h"

namespace {
class CaptureSerial final : public cutemac::devices::serial::SerialEndpoint {
public:
    void receiveByte(std::uint8_t value) override
    {
        if (bytes.size() < 4096) bytes.append(static_cast<char>(value));
    }
    QByteArray bytes;
};
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: CuteMacPowerMac8100Smoke <rom> [cycles] [scsi-disk|--cd <id> <image>]\n";
        return 2;
    }
    const auto cycleBudget = argc >= 3 ? std::max<std::int64_t>(1, std::strtoll(argv[2], nullptr, 0)) : 1'000'000;
    cutemac::machines::powermac8100::PowerMac8100Machine machine(8U * 1024U * 1024U);
    auto serial = std::make_shared<CaptureSerial>();
    machine.attachSerialEndpoint(0, serial);
    machine.attachSerialEndpoint(1, serial);
    if (!machine.loadRomFile(QString::fromLocal8Bit(argv[1]), {})) {
        std::cerr << "failed to load 4 MiB Power Macintosh ROM\n";
        return 1;
    }
    if (argc >= 4) {
        if (QString::fromLocal8Bit(argv[3]) == QStringLiteral("--cd")) {
            if (argc < 6 || !machine.loadScsiCdRom(QString::fromLocal8Bit(argv[4]).toInt(),
                    QString::fromLocal8Bit(argv[5]))) {
                std::cerr << "failed to load SCSI CD-ROM\n";
                return 1;
            }
        } else if (!machine.loadScsiDisk(0, QString::fromLocal8Bit(argv[3]), false)) {
            std::cerr << "failed to load SCSI disk\n";
            return 1;
        }
    }
    machine.setBusTraceEnabled(qEnvironmentVariableIsSet("CUTEMAC_8100_BUS_TRACE"));
    machine.setCpuTraceEnabled(qEnvironmentVariableIsSet("CUTEMAC_8100_CPU_TRACE"));
    machine.setCpuTraceFreezeOnEmulatorException(qEnvironmentVariableIsSet("CUTEMAC_8100_TRACE_FREEZE_EXCEPTION"));
    if (qEnvironmentVariableIsSet("CUTEMAC_8100_DISABLE_VBLANK")) machine.setVia1VblankEnabled(false);
    machine.reset();
    std::map<std::uint32_t, unsigned> exceptions;
    std::map<std::uint32_t, unsigned> sampledPcs;
    auto lastPc = machine.programCounter();
    bool stopPcOk = false;
    const auto stopPc = qEnvironmentVariable("CUTEMAC_8100_STOP_PC").toUInt(&stopPcOk, 0);
    bool stop68kPcOk = false;
    const auto stop68kPc = qEnvironmentVariable("CUTEMAC_8100_STOP_68K_PC").toUInt(&stop68kPcOk, 0);
    bool stop68kReturnOk = false;
    const auto stop68kReturn = qEnvironmentVariable("CUTEMAC_8100_STOP_68K_RETURN").toUInt(&stop68kReturnOk, 0);
    bool armTrace68kPcOk = false;
    const auto armTrace68kPc = qEnvironmentVariable("CUTEMAC_8100_ARM_TRACE_68K_PC").toUInt(&armTrace68kPcOk, 0);
    bool stop68kStartOk = false;
    const auto stop68kStart = qEnvironmentVariable("CUTEMAC_8100_STOP_68K_START").toUInt(&stop68kStartOk, 0);
    bool stop68kEndOk = false;
    const auto stop68kEnd = qEnvironmentVariable("CUTEMAC_8100_STOP_68K_END").toUInt(&stop68kEndOk, 0);
    bool stop68kHitsOk = false;
    const auto stop68kHits = qEnvironmentVariable("CUTEMAC_8100_STOP_68K_HITS").toUInt(&stop68kHitsOk);
    unsigned stop68kHitCount = 0;
    bool wasInStop68kRange = false;
    bool stopR14Ok = false;
    const auto stopR14 = qEnvironmentVariable("CUTEMAC_8100_STOP_R14").toUInt(&stopR14Ok, 0);
    bool stopR15Ok = false;
    const auto stopR15 = qEnvironmentVariable("CUTEMAC_8100_STOP_R15").toUInt(&stopR15Ok, 0);
    bool stopR15MaskOk = false;
    const auto stopR15Mask = qEnvironmentVariable("CUTEMAC_8100_STOP_R15_MASK").toUInt(&stopR15MaskOk, 0);
    bool stopR6Ok = false;
    const auto stopR6 = qEnvironmentVariable("CUTEMAC_8100_STOP_R6").toUInt(&stopR6Ok, 0);
    bool stopR20Ok = false;
    const auto stopR20 = qEnvironmentVariable("CUTEMAC_8100_STOP_R20").toUInt(&stopR20Ok, 0);
    int stationary = 0;
    unsigned dsiCount = 0;
    bool dsiCountOk = false;
    const auto stopDsiCount = qEnvironmentVariable("CUTEMAC_8100_STOP_DSI_COUNT").toUInt(&dsiCountOk);
    unsigned programCount = 0;
    bool programCountOk = false;
    const auto stopProgramCount = qEnvironmentVariable("CUTEMAC_8100_STOP_PROGRAM_COUNT").toUInt(&programCountOk);
    bool stopDarOk = false;
    const auto stopDar = qEnvironmentVariable("CUTEMAC_8100_STOP_DAR").toUInt(&stopDarOk, 0);
    bool stopBusStartOk = false;
    const auto stopBusStart = qEnvironmentVariable("CUTEMAC_8100_STOP_BUS_START").toUInt(&stopBusStartOk, 0);
    bool stopBusEndOk = false;
    const auto stopBusEnd = qEnvironmentVariable("CUTEMAC_8100_STOP_BUS_END").toUInt(&stopBusEndOk, 0);
    bool stopBusHitsOk = false;
    const auto stopBusHits = qEnvironmentVariable("CUTEMAC_8100_STOP_BUS_HITS").toUInt(&stopBusHitsOk);
    unsigned stopBusHitCount = 0;
    bool instructionLimitOk = false;
    const auto instructionLimit = qEnvironmentVariable("CUTEMAC_8100_INSTRUCTIONS").toULongLong(&instructionLimitOk);
    bool stopSerialBytesOk = false;
    const auto stopSerialBytes = qEnvironmentVariable("CUTEMAC_8100_STOP_SERIAL_BYTES").toUInt(&stopSerialBytesOk);
    std::uint64_t instructions = 0;
    std::array<std::uint32_t, 4096> emulatedPcHistory {};
    std::size_t emulatedPcHistoryPosition = 0;
    const auto capture68kHistory = qEnvironmentVariableIsSet("CUTEMAC_8100_68K_HISTORY");
    const auto stopOnSadMac = qEnvironmentVariableIsSet("CUTEMAC_8100_STOP_ON_SAD_MAC");
    for (std::int64_t used = 0; used < cycleBudget;) {
        used += machine.stepInstruction();
        if (++instructions == instructionLimit && instructionLimitOk) break;
        if (stopSerialBytesOk && serial->bytes.size() >= static_cast<qsizetype>(stopSerialBytes)) break;
        const auto pc = machine.programCounter();
        if (stopBusStartOk && stopBusEndOk && !machine.busTrace().empty()) {
            const auto address = machine.busTrace().back().address;
            if (address >= stopBusStart && address < stopBusEnd
                && ++stopBusHitCount >= (stopBusHitsOk ? stopBusHits : 1U)) break;
        }
        if (capture68kHistory && pc >= 0x68000000U && pc < 0x68100000U) {
            const auto emulatedPc = machine.cpuRegisters().gpr[24];
            if (emulatedPcHistory[(emulatedPcHistoryPosition + emulatedPcHistory.size() - 1)
                    % emulatedPcHistory.size()] != emulatedPc)
                emulatedPcHistory[emulatedPcHistoryPosition++ % emulatedPcHistory.size()] = emulatedPc;
        }
        if (stopOnSadMac && (instructions & 0x3ffU) == 0
            && cutemac::debug::SadMacDetector::detect(machine.videoFrame())) break;
        if (pc == 0xfff00300U && ++dsiCount && dsiCountOk && dsiCount >= stopDsiCount) break;
        if (pc == 0xfff00700U && ++programCount && programCountOk && programCount >= stopProgramCount) break;
        if (pc == 0xfff00300U && stopDarOk && machine.cpuRegisters().dar == stopDar) break;
        if ((pc & 0xfffU) == 0x300U || (pc & 0xfffU) == 0x400U || (pc & 0xfffU) == 0x700U) {
            const auto existing = exceptions.find(pc);
            if (existing != exceptions.end()) ++existing->second;
            else if (exceptions.size() < 4096) exceptions.emplace(pc, 1U);
        }
        if ((used & 0x3ffU) == 0) {
            const auto existing = sampledPcs.find(pc);
            if (existing != sampledPcs.end()) ++existing->second;
            else if (sampledPcs.size() < 65'536) sampledPcs.emplace(pc, 1U);
        }
        stationary = pc == lastPc ? stationary + 1 : 0;
        lastPc = pc;
        if (stopPcOk && pc == stopPc) break;
        const auto in68kEmulator = pc >= 0x68000000U && pc < 0x68100000U;
        if (armTrace68kPcOk && in68kEmulator && machine.cpuRegisters().gpr[24] - 2U == armTrace68kPc) {
            machine.setCpuTraceFreezeOnEmulatorException(true);
            armTrace68kPcOk = false;
        }
        if (stop68kPcOk && in68kEmulator && machine.cpuRegisters().gpr[24] - 2U == stop68kPc) break;
        if (stop68kReturnOk && in68kEmulator
            && machine.debugRead32(machine.cpuRegisters().gpr[1]) == stop68kReturn) break;
        const auto inStop68kRange = in68kEmulator && stop68kStartOk && stop68kEndOk
            && machine.cpuRegisters().gpr[24] >= stop68kStart && machine.cpuRegisters().gpr[24] < stop68kEnd;
        if (inStop68kRange && !wasInStop68kRange) ++stop68kHitCount;
        wasInStop68kRange = inStop68kRange;
        if (inStop68kRange && (!stop68kHitsOk || stop68kHitCount >= stop68kHits)) break;
        if (stopR14Ok && machine.cpuRegisters().gpr[14] == stopR14) break;
        if (stopR15Ok && machine.cpuRegisters().gpr[15] == stopR15) break;
        if (stopR15MaskOk && in68kEmulator
            && (machine.cpuRegisters().gpr[15] & stopR15Mask) != 0) break;
        if (stopR6Ok && machine.cpuRegisters().gpr[6] == stopR6) break;
        if (stopR20Ok && machine.cpuRegisters().gpr[20] == stopR20) break;
        if (qEnvironmentVariableIsSet("CUTEMAC_8100_STOP_ON_EMULATOR")
            && pc >= 0x68000000U && pc < 0x68100000U) break;
        if (qEnvironmentVariableIsSet("CUTEMAC_8100_STOP_ON_UNMAPPED") && machine.unmappedAccessCount() != 0) break;
        if (qEnvironmentVariableIsSet("CUTEMAC_8100_STOP_ON_ALIGNMENT") && pc == 0xfff00600U) break;
        if (qEnvironmentVariableIsSet("CUTEMAC_8100_STOP_ON_PROGRAM") && pc == 0xfff00700U) break;
        if (qEnvironmentVariableIsSet("CUTEMAC_8100_STOP_ON_VIDEO") && machine.videoFrame().valid()) break;
        if (stationary > 10000) break;
    }
    const auto state = machine.cpuRegisters();
    const auto frame = machine.videoFrame();
    std::cout << std::hex << std::setfill('0')
              << "pc=0x" << std::setw(8) << state.pc << " msr=0x" << std::setw(8) << state.msr
              << " srr0=0x" << std::setw(8) << state.srr0 << " srr1=0x" << std::setw(8) << state.srr1
              << " lr=0x" << std::setw(8) << state.lr << " ctr=0x" << std::setw(8) << state.ctr
              << " cr=0x" << std::setw(8) << state.cr << " xer=0x" << std::setw(8) << state.xer
              << " r6=0x" << std::setw(8) << state.gpr[6] << " r8=0x" << std::setw(8) << state.gpr[8]
              << " r10=0x" << std::setw(8) << state.gpr[10]
              << " dar=0x" << std::setw(8) << state.dar << " dsisr=0x" << std::setw(8) << state.dsisr
              << " dec=0x" << std::setw(8) << state.dec
              << " sdr1=0x" << std::setw(8) << state.sdr1 << " sr4=0x" << std::setw(8) << state.sr[4]
              << " sr5=0x" << std::setw(8) << state.sr[5]
              << " sr6=0x" << std::setw(8) << state.sr[6]
              << " sre=0x" << std::setw(8) << state.sr[14]
              << " sprg3=0x" << std::setw(8) << state.sprg[3]
              << " bats=0x" << std::setw(8) << state.batu[0] << '/' << std::setw(8) << state.batl[0]
              << ',' << std::setw(8) << state.batu[1] << '/' << std::setw(8) << state.batl[1]
              << ',' << std::setw(8) << state.batu[2] << '/' << std::setw(8) << state.batl[2]
              << ',' << std::setw(8) << state.batu[3] << '/' << std::setw(8) << state.batl[3]
              << " hmc=0x" << machine.hmcControl()
              << std::dec << " cycles=" << machine.cycleCount()
              << " instructions=" << instructions
              << " range-hits=" << stop68kHitCount
              << " unmapped=" << machine.unmappedAccessCount()
              << " video=" << frame.width << 'x' << frame.height << 'x' << frame.bitsPerPixel << '\n';
    std::cout << "asc-reads";
    for (const auto count : machine.ascCompatibilityReadCounts()) std::cout << ' ' << count;
    std::cout << '\n';
    std::cout << "asc-callers";
    for (const auto caller : machine.ascCompatibilityReadCallers())
        std::cout << " 0x" << std::hex << caller;
    std::cout << std::dec << '\n';
    const auto cuda = machine.cudaDebugState();
    std::cout << "cuda transitions=" << cuda.transitions << " attentions=" << cuda.attentions
              << " packets=" << cuda.packets << " last=" << std::hex
              << static_cast<unsigned>(cuda.lastType) << ':' << static_cast<unsigned>(cuda.lastCommand)
              << " addr=0x" << cuda.lastAddress << std::dec << " size=" << cuda.lastPacketSize
              << " output=0x" << std::hex << static_cast<unsigned>(cuda.output)
              << " direction=0x" << static_cast<unsigned>(cuda.direction)
              << " tip=" << cuda.tip << " byteack=" << cuda.byteAck << std::dec << '\n';
    std::cout << "cuda-commands";
    for (unsigned command = 0; command < cuda.commandCounts.size(); ++command)
        if (cuda.commandCounts[command])
            std::cout << " 0x" << std::hex << command << ':' << std::dec << cuda.commandCounts[command];
    std::cout << '\n';
    std::cout << "serial=" << serial->bytes.toHex(' ').toStdString() << '\n';
    for (const bool internal : { false, true }) {
        const auto& scsi = machine.scsiController(internal);
        std::cout << "scsi-" << (internal ? "internal" : "external") << " ctl";
        for (std::size_t i = 0; i < scsi.controllerCommandCounts().size(); ++i)
            if (scsi.controllerCommandCounts()[i]) std::cout << " 0x" << std::hex << i << ':' << std::dec << scsi.controllerCommandCounts()[i];
        std::cout << " cdb";
        for (std::size_t i = 0; i < scsi.scsiCommandCounts().size(); ++i)
            if (scsi.scsiCommandCounts()[i]) std::cout << " 0x" << std::hex << i << ':' << std::dec << scsi.scsiCommandCounts()[i];
        const auto debug = scsi.debugState();
        std::cout << " last=" << debug.cdb.toHex().toStdString()
                  << " target=" << static_cast<unsigned>(debug.targetId)
                  << " status=0x" << std::hex << static_cast<unsigned>(debug.status)
                  << " interrupt=0x" << static_cast<unsigned>(debug.interruptStatus)
                  << " step=0x" << static_cast<unsigned>(debug.sequenceStep)
                  << " scsi-status=0x" << static_cast<unsigned>(debug.scsiStatus)
                  << " message=0x" << static_cast<unsigned>(debug.message)
                  << std::dec << " data=" << debug.dataPosition << '/' << debug.dataSize
                  << " remaining=" << debug.transferCount
                  << " phase=" << (debug.command ? "command" : debug.dataIn ? "data-in" : debug.dataOut ? "data-out" : "other")
                  << '\n';
    }
    if (capture68kHistory) {
        std::cout << "68k-history";
        const auto count = std::min(emulatedPcHistoryPosition, emulatedPcHistory.size());
        const auto start = emulatedPcHistoryPosition >= emulatedPcHistory.size()
            ? emulatedPcHistoryPosition % emulatedPcHistory.size() : 0;
        for (std::size_t index = 0; index < count; ++index)
            std::cout << " 0x" << std::hex << emulatedPcHistory[(start + index) % emulatedPcHistory.size()];
        std::cout << std::dec << '\n';
    }
    const auto via1 = machine.via1DebugState();
    std::cout << "via1 ifr=0x" << std::hex << static_cast<unsigned>(via1.interruptFlags)
              << " ier=0x" << static_cast<unsigned>(via1.interruptEnable) << std::dec << '\n';
    const auto irq = machine.interruptDebugState();
    std::cout << "amic-irq control=0x" << std::hex << static_cast<unsigned>(irq[0])
              << " via2-ifr/ier=0x" << static_cast<unsigned>(irq[1]) << '/' << static_cast<unsigned>(irq[2])
              << " slot-ifr/ier=0x" << static_cast<unsigned>(irq[3]) << '/' << static_cast<unsigned>(irq[4])
              << std::dec << '\n';
    const auto lastIrq = machine.lastInterruptAssertState();
    std::cout << "last-irq-assert control/previous-sources=0x" << std::hex << static_cast<unsigned>(lastIrq[0])
              << '/' << static_cast<unsigned>(lastIrq[1]) << " via2-ifr/ier=0x"
              << static_cast<unsigned>(lastIrq[2]) << '/' << static_cast<unsigned>(lastIrq[3])
              << " slot-ifr/ier=0x" << static_cast<unsigned>(lastIrq[4]) << '/'
              << static_cast<unsigned>(lastIrq[5]) << std::dec << '\n';
    for (unsigned first = 0; first < 32; first += 4) {
        for (unsigned reg = first; reg < first + 4; ++reg)
            std::cout << "r" << reg << "=0x" << std::hex << std::setw(8) << state.gpr[reg] << ' ';
        std::cout << '\n';
    }
    const auto framePath = qEnvironmentVariable("CUTEMAC_8100_FRAME");
    if (!framePath.isEmpty() && frame.valid()) {
        QFile output(framePath);
        if (output.open(QIODevice::WriteOnly)) output.write(frame.pixels);
    }
    const auto ramDumpPath = qEnvironmentVariable("CUTEMAC_8100_RAM_DUMP");
    if (!ramDumpPath.isEmpty()) {
        QByteArray bytes(2 * 1024 * 1024, Qt::Uninitialized);
        for (qsizetype offset = 0; offset < bytes.size(); ++offset)
            bytes[offset] = static_cast<char>(machine.debugRead8(static_cast<std::uint32_t>(offset)));
        QFile output(ramDumpPath);
        if (output.open(QIODevice::WriteOnly)) output.write(bytes);
    }
    std::cout << machine.disassemble(state.pc).toStdString() << '\n';
    const auto emulatedPc = state.gpr[24] & ~1U;
    std::cout << "68k-words@0x" << std::hex << emulatedPc;
    for (unsigned offset = 0; offset < 16; offset += 2)
        std::cout << " 0x" << machine.debugRead16(emulatedPc + offset);
    std::cout << std::dec << '\n';
    std::cout << "spmem=0x" << std::hex << machine.debugRead32(state.gpr[1])
              << ",0x" << machine.debugRead32(state.gpr[1] + 4U) << '\n';
    std::cout << "dsi-handler=0x" << std::hex << machine.debugRead32(state.sprg[3] + 12U) << '\n';
    bool debugReadOk = false;
    const auto debugRead = qEnvironmentVariable("CUTEMAC_8100_DEBUG_READ").toUInt(&debugReadOk, 0);
    if (debugReadOk) {
        bool debugSizeOk = false;
        const auto debugSize = qEnvironmentVariable("CUTEMAC_8100_DEBUG_SIZE").toUInt(&debugSizeOk, 0);
        std::cout << "debug-memory";
        for (unsigned offset = 0; offset < (debugSizeOk ? debugSize : 64U); offset += 4)
            std::cout << " 0x" << std::hex << debugRead + offset << "=0x" << machine.debugRead32(debugRead + offset);
        std::cout << std::dec << '\n';
    }
    bool disasmAddressOk = false;
    const auto disasmAddress = qEnvironmentVariable("CUTEMAC_8100_DISASM").toUInt(&disasmAddressOk, 0);
    if (disasmAddressOk) {
        auto address = disasmAddress;
        for (unsigned i = 0; i < 128; ++i, address += 4)
            std::cout << "0x" << std::hex << address << ' ' << machine.disassemble(address).toStdString() << '\n';
    }
    if (state.dar != 0 && state.sdr1 != 0) {
        const auto sr = state.sr[state.dar >> 28];
        const auto page = (state.dar >> 12) & 0xffffU;
        const auto hash = (sr & 0x7ffffU) ^ page;
        const auto pteg = (state.sdr1 & 0xffff0000U)
            | (((state.sdr1 & 0x1ffU) << 16) & ((hash & 0x7fc00U) << 6))
            | ((hash & 0x3ffU) << 6);
        std::cout << "pteg=0x" << std::hex << pteg;
        for (unsigned slot = 0; slot < 8; ++slot)
            std::cout << ' ' << machine.debugRead32(pteg + slot * 8) << '/' << machine.debugRead32(pteg + slot * 8 + 4);
        std::cout << std::dec << '\n';
    }
    std::cout << "exceptions";
    for (const auto& [pc, count] : exceptions) std::cout << " 0x" << std::hex << pc << ':' << std::dec << count;
    std::cout << "\nhot-pcs";
    std::vector<std::pair<unsigned, std::uint32_t>> hot;
    for (const auto& [pc, count] : sampledPcs) hot.emplace_back(count, pc);
    std::sort(hot.rbegin(), hot.rend());
    for (std::size_t i = 0; i < std::min<std::size_t>(hot.size(), 12); ++i)
        std::cout << " 0x" << std::hex << hot[i].second << ':' << std::dec << hot[i].first;
    std::cout << "\nlast-bus-accesses\n";
    const auto& trace = machine.busTrace();
    std::size_t devicePrinted = 0;
    for (auto it = trace.rbegin(); it != trace.rend() && devicePrinted < 256; ++it) {
        if (it->region == cutemac::machines::powermac8100::PowerMac8100Machine::BusRegion::Ram
            || it->region == cutemac::machines::powermac8100::PowerMac8100Machine::BusRegion::Rom)
            continue;
        std::cout << "device-access " << it->cycle << " pc=0x" << std::hex << it->pc
                  << (it->write ? " write" : " read") << std::dec << static_cast<unsigned>(it->size) * 8
                  << " [0x" << std::hex << it->address << "]=0x" << it->value
                  << " region=" << std::dec << static_cast<unsigned>(it->region) << '\n';
        ++devicePrinted;
    }
    for (const auto& access : trace) {
        if ((access.address >= 0x50f2c000U && access.address < 0x50f2e000U)
            || (access.address >= 0x50f04000U && access.address < 0x50f04010U))
            std::cout << "diag-access " << access.cycle << " pc=0x" << std::hex << access.pc
                      << (access.write ? " write" : " read") << std::dec << static_cast<unsigned>(access.size) * 8
                      << " [0x" << std::hex << access.address << "]=0x" << access.value << std::dec << '\n';
    }
    const auto begin = trace.size() > 160 ? trace.size() - 160 : 0;
    for (std::size_t i = begin; i < trace.size(); ++i) {
        const auto& access = trace[i];
        std::cout << access.cycle << " pc=0x" << std::hex << access.pc
                  << (access.write ? " write" : " read") << std::dec << static_cast<unsigned>(access.size) * 8
                  << " [0x" << std::hex << access.address << "]=0x" << access.value
                  << " region=" << std::dec << static_cast<unsigned>(access.region) << '\n';
    }
    std::cout << "last-cpu-events\n";
    const auto& cpuTrace = machine.cpuTrace();
    const auto cpuBegin = std::size_t { 0 };
    for (std::size_t i = cpuBegin; i < cpuTrace.size(); ++i) {
        const auto& event = cpuTrace[i];
        std::cout << event.cycle << " kind=" << static_cast<unsigned>(event.kind)
                  << " pc=0x" << std::hex << event.pc << " op=0x" << event.opcode
                  << " ea=0x" << event.effectiveAddress << " pa=0x" << event.physicalAddress
                  << " exception=0x" << static_cast<unsigned>(event.exception) << std::dec << '\n';
    }
    return 0;
}
