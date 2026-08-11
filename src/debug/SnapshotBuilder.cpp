#include "cutemac/debug/SnapshotBuilder.h"

#include <array>

namespace cutemac::debug {

namespace {

QString hex(std::uint64_t value, int digits)
{
    return QStringLiteral("0x%1").arg(value, digits, 16, QLatin1Char('0'));
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

std::uint32_t read32(const MemoryReader& read8, std::uint32_t address)
{
    return (static_cast<std::uint32_t>(read8(address)) << 24)
        | (static_cast<std::uint32_t>(read8(address + 1)) << 16)
        | (static_cast<std::uint32_t>(read8(address + 2)) << 8)
        | static_cast<std::uint32_t>(read8(address + 3));
}

QStringList renderDisassembly(const Disassembler& disassemble, std::uint32_t pc)
{
    QStringList lines;
    if (!disassemble) return lines;
    // Forward only. Disassembling backwards from an arbitrary address on a
    // variable-length instruction set is guesswork, and a wrong guess here
    // would be indistinguishable from a real instruction stream.
    auto address = pc;
    for (int index = 0; index < disassemblyWindowInstructions; ++index) {
        const auto [text, size] = disassemble(address);
        if (text.isEmpty() || size <= 0) break;
        lines.append(QStringLiteral("%1  %2").arg(hex(address, 8), text));
        address += static_cast<std::uint32_t>(size);
    }
    return lines;
}

QStringList walkFrameChain(const MemoryReader& read8, std::uint32_t framePointer)
{
    QStringList frames;
    if (!read8) return frames;
    auto current = framePointer;
    for (int depth = 0; depth < backtraceFrameLimit; ++depth) {
        if (current == 0 || (current & 1) != 0) break;
        const auto next = read32(read8, current);
        const auto returnAddress = read32(read8, current + 4);
        frames.append(QStringLiteral("#%1 frame=%2 return=%3")
                          .arg(depth)
                          .arg(hex(current, 8), hex(returnAddress, 8)));
        // A frame chain must climb; anything else is a corrupt or non-standard
        // frame and continuing would print invented history.
        if (next <= current) break;
        current = next;
    }
    return frames;
}

} // namespace

CpuSnapshot buildCpuSnapshot(const cpu::m68k::M68kCpuCore::RegisterSnapshot& registers,
    const QString& architecture, const MemoryReader& read8, const Disassembler& disassemble)
{
    CpuSnapshot cpu;
    cpu.architecture = architecture;
    cpu.pc = registers.pc;
    cpu.stackPointer = registers.a[7];
    cpu.framePointer = registers.a[6];
    cpu.interruptLevel = static_cast<int>((registers.sr >> 8) & 0x07);

    for (int index = 0; index < 8; ++index) {
        cpu.registers.insert(QStringLiteral("d%1").arg(index), registers.d[index]);
    }
    for (int index = 0; index < 8; ++index) {
        cpu.registers.insert(QStringLiteral("a%1").arg(index), registers.a[index]);
    }
    cpu.registers.insert(QStringLiteral("pc"), registers.pc);
    cpu.registers.insert(QStringLiteral("sr"), registers.sr);
    cpu.registers.insert(QStringLiteral("usp"), registers.usp);
    cpu.registers.insert(QStringLiteral("isp"), registers.isp);
    cpu.registers.insert(QStringLiteral("msp"), registers.msp);
    cpu.registers.insert(QStringLiteral("vbr"), registers.vbr);
    cpu.registers.insert(QStringLiteral("physical_pc"), registers.physicalPc);

    cpu.mmuRegisters.insert(QStringLiteral("enabled"), registers.pmmuEnabled ? 1 : 0);
    cpu.mmuRegisters.insert(QStringLiteral("kind"), registers.pmmuKind);
    cpu.mmuRegisters.insert(QStringLiteral("tc"), registers.pmmuTc);
    cpu.mmuRegisters.insert(QStringLiteral("tt0"), registers.pmmuTt0);
    cpu.mmuRegisters.insert(QStringLiteral("tt1"), registers.pmmuTt1);
    cpu.mmuRegisters.insert(QStringLiteral("crp_limit"), registers.pmmuCrpLimit);
    cpu.mmuRegisters.insert(QStringLiteral("crp_address"), registers.pmmuCrpAddress);
    cpu.mmuRegisters.insert(QStringLiteral("srp_limit"), registers.pmmuSrpLimit);
    cpu.mmuRegisters.insert(QStringLiteral("srp_address"), registers.pmmuSrpAddress);
    cpu.mmuRegisters.insert(QStringLiteral("mmusr"), registers.pmmuMmusr);
    cpu.mmuRegisters.insert(QStringLiteral("fault_address"), registers.pmmuFaultAddress);
    cpu.mmuRegisters.insert(QStringLiteral("atc_hits"), registers.pmmuAtcHits);
    cpu.mmuRegisters.insert(QStringLiteral("atc_misses"), registers.pmmuAtcMisses);

    for (int index = 0; index < 8; ++index) {
        cpu.registerLines.append(QStringLiteral("D%1=%2 A%1=%3")
                                     .arg(index)
                                     .arg(hex(registers.d[index], 8), hex(registers.a[index], 8)));
    }
    cpu.registerLines.append(QStringLiteral("PC=%1 SR=%2 VBR=%3")
                                 .arg(hex(registers.pc, 8), hex(registers.sr, 4), hex(registers.vbr, 8)));
    cpu.registerLines.append(QStringLiteral("USP=%1 ISP=%2 MSP=%3")
                                 .arg(hex(registers.usp, 8), hex(registers.isp, 8), hex(registers.msp, 8)));

    cpu.disassembly = renderDisassembly(disassemble, registers.pc);
    cpu.backtrace = walkFrameChain(read8, registers.a[6]);

    if (read8) {
        cpu.vectorTable.reserve(vectorTableEntries);
        for (int index = 0; index < vectorTableEntries; ++index) {
            cpu.vectorTable.append(read32(read8, registers.vbr + static_cast<std::uint32_t>(index) * 4));
        }
    }
    return cpu;
}

CpuSnapshot buildCpuSnapshot(const cpu::ppc::PowerPc601Core::RegisterSnapshot& registers,
    const MemoryReader& read8, const Disassembler& disassemble)
{
    CpuSnapshot cpu;
    cpu.architecture = QStringLiteral("ppc:601");
    cpu.pc = registers.pc;
    cpu.stackPointer = registers.gpr[1];
    cpu.framePointer = registers.gpr[1];

    for (int index = 0; index < 32; ++index) {
        cpu.registers.insert(QStringLiteral("r%1").arg(index), registers.gpr[index]);
    }
    for (int index = 0; index < 16; ++index) {
        cpu.registers.insert(QStringLiteral("sr%1").arg(index), registers.sr[index]);
    }
    for (int index = 0; index < 4; ++index) {
        cpu.registers.insert(QStringLiteral("batu%1").arg(index), registers.batu[index]);
        cpu.registers.insert(QStringLiteral("batl%1").arg(index), registers.batl[index]);
        cpu.registers.insert(QStringLiteral("sprg%1").arg(index), registers.sprg[index]);
    }
    const std::array<QPair<QString, std::uint32_t>, 17> named { {
        { QStringLiteral("pc"), registers.pc }, { QStringLiteral("msr"), registers.msr },
        { QStringLiteral("cr"), registers.cr }, { QStringLiteral("xer"), registers.xer },
        { QStringLiteral("lr"), registers.lr }, { QStringLiteral("ctr"), registers.ctr },
        { QStringLiteral("mq"), registers.mq }, { QStringLiteral("srr0"), registers.srr0 },
        { QStringLiteral("srr1"), registers.srr1 }, { QStringLiteral("dar"), registers.dar },
        { QStringLiteral("dsisr"), registers.dsisr }, { QStringLiteral("sdr1"), registers.sdr1 },
        { QStringLiteral("fpscr"), registers.fpscr }, { QStringLiteral("hid0"), registers.hid0 },
        { QStringLiteral("hid1"), registers.hid1 }, { QStringLiteral("dec"), registers.dec },
        { QStringLiteral("rtcl"), registers.rtcl },
    } };
    for (const auto& [name, value] : named) cpu.registers.insert(name, value);

    cpu.mmuRegisters.insert(QStringLiteral("sdr1"), registers.sdr1);
    cpu.mmuRegisters.insert(QStringLiteral("msr_ir"), (registers.msr & cpu::ppc::PowerPc601Core::msrIr) != 0 ? 1 : 0);
    cpu.mmuRegisters.insert(QStringLiteral("msr_dr"), (registers.msr & cpu::ppc::PowerPc601Core::msrDr) != 0 ? 1 : 0);
    cpu.mmuRegisters.insert(QStringLiteral("msr_pr"), (registers.msr & cpu::ppc::PowerPc601Core::msrPr) != 0 ? 1 : 0);
    cpu.mmuRegisters.insert(QStringLiteral("msr_ee"), (registers.msr & cpu::ppc::PowerPc601Core::msrEe) != 0 ? 1 : 0);

    for (int index = 0; index < 32; index += 4) {
        cpu.registerLines.append(QStringLiteral("r%1=%2 r%3=%4 r%5=%6 r%7=%8")
                                     .arg(index)
                                     .arg(hex(registers.gpr[index], 8))
                                     .arg(index + 1)
                                     .arg(hex(registers.gpr[index + 1], 8))
                                     .arg(index + 2)
                                     .arg(hex(registers.gpr[index + 2], 8))
                                     .arg(index + 3)
                                     .arg(hex(registers.gpr[index + 3], 8)));
    }
    cpu.registerLines.append(QStringLiteral("PC=%1 MSR=%2 CR=%3 XER=%4")
                                 .arg(hex(registers.pc, 8), hex(registers.msr, 8), hex(registers.cr, 8),
                                     hex(registers.xer, 8)));
    cpu.registerLines.append(QStringLiteral("LR=%1 CTR=%2 SRR0=%3 SRR1=%4")
                                 .arg(hex(registers.lr, 8), hex(registers.ctr, 8), hex(registers.srr0, 8),
                                     hex(registers.srr1, 8)));

    cpu.disassembly = renderDisassembly(disassemble, registers.pc);
    // The 601 ABI keeps the back chain at [r1]; the return address sits one
    // word further in than the 68k convention.
    if (read8) {
        auto current = registers.gpr[1];
        for (int depth = 0; depth < backtraceFrameLimit; ++depth) {
            if (current == 0 || (current & 3) != 0) break;
            const auto next = read32(read8, current);
            cpu.backtrace.append(QStringLiteral("#%1 frame=%2 return=%3")
                                     .arg(depth)
                                     .arg(hex(current, 8), hex(read32(read8, current + 8), 8)));
            if (next <= current) break;
            current = next;
        }
    }
    return cpu;
}

DeviceSnapshot viaSnapshot(const QString& id, const devices::via6522::Via6522::DebugState& state)
{
    DeviceSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = QStringLiteral("via6522");
    snapshot.fields.insert(QStringLiteral("ifr"), hex(state.interruptFlags, 2));
    snapshot.fields.insert(QStringLiteral("ier"), hex(state.interruptEnable, 2));
    snapshot.fields.insert(QStringLiteral("acr"), hex(state.auxiliaryControl, 2));
    snapshot.fields.insert(QStringLiteral("shift_register"), hex(state.shiftRegister, 2));
    snapshot.fields.insert(QStringLiteral("port_a"), hex(state.portA, 2));
    snapshot.fields.insert(QStringLiteral("port_b"), hex(state.portB, 2));
    snapshot.fields.insert(QStringLiteral("timer1"), QString::number(state.timer1Counter));
    snapshot.fields.insert(QStringLiteral("timer1_running"), yesNo(state.timer1Running));
    snapshot.fields.insert(QStringLiteral("timer2"), QString::number(state.timer2Counter));
    snapshot.fields.insert(QStringLiteral("timer2_running"), yesNo(state.timer2Running));
    snapshot.fields.insert(QStringLiteral("interrupt_active"), yesNo(state.interruptActive));
    snapshot.fields.insert(QStringLiteral("keyboard_command"), hex(state.keyboardCommand, 2));
    snapshot.fields.insert(QStringLiteral("keyboard_queue_depth"),
        QString::number(static_cast<qulonglong>(state.keyboardQueueDepth)));
    snapshot.stateLines.append(QStringLiteral("%1 ifr=%2 ier=%3 irq=%4 t1=%5 t2=%6 pa=%7 pb=%8")
                                   .arg(id, hex(state.interruptFlags, 2), hex(state.interruptEnable, 2),
                                       yesNo(state.interruptActive))
                                   .arg(state.timer1Counter)
                                   .arg(state.timer2Counter)
                                   .arg(hex(state.portA, 2), hex(state.portB, 2)));
    return snapshot;
}

DeviceSnapshot scsiSnapshot(const QString& id, const devices::scsi::ncr5380::Ncr5380::DebugState& state)
{
    DeviceSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = QStringLiteral("ncr5380");
    snapshot.fields.insert(QStringLiteral("phase"), state.phase);
    snapshot.fields.insert(QStringLiteral("target"), hex(state.activeTargetId, 2));
    snapshot.fields.insert(QStringLiteral("status"), hex(state.status, 2));
    snapshot.fields.insert(QStringLiteral("message"), hex(state.message, 2));
    snapshot.fields.insert(QStringLiteral("target_command"), hex(state.targetCommand, 2));
    snapshot.fields.insert(QStringLiteral("data_index"), QString::number(state.dataIndex));
    snapshot.fields.insert(QStringLiteral("data_length"), QString::number(state.dataLength));
    snapshot.fields.insert(QStringLiteral("completed_commands"),
        QString::number(static_cast<qulonglong>(state.completedCommands)));
    snapshot.fields.insert(QStringLiteral("selected"), yesNo(state.selected));
    snapshot.fields.insert(QStringLiteral("request"), yesNo(state.request));
    snapshot.fields.insert(QStringLiteral("ack"), yesNo(state.ack));
    snapshot.fields.insert(QStringLiteral("active_command"), QString::fromLatin1(state.activeCommand.toHex(' ')));
    snapshot.fields.insert(QStringLiteral("last_command"), QString::fromLatin1(state.lastCommand.toHex(' ')));
    snapshot.stateLines.append(QStringLiteral("%1 phase=%2 target=%3 status=%4 last=%5")
                                   .arg(id, state.phase, hex(state.activeTargetId, 2), hex(state.status, 2),
                                       QString::fromLatin1(state.lastCommand.toHex(' '))));
    return snapshot;
}

DeviceSnapshot scsiSnapshot(const QString& id, const devices::scsi::ncr53c94::Ncr53c94::DebugState& state)
{
    DeviceSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = QStringLiteral("ncr53c94");
    snapshot.fields.insert(QStringLiteral("cdb"), QString::fromLatin1(state.cdb.toHex(' ')));
    snapshot.fields.insert(QStringLiteral("transfer_count"), QString::number(state.transferCount));
    snapshot.fields.insert(QStringLiteral("data_position"), QString::number(state.dataPosition));
    snapshot.fields.insert(QStringLiteral("data_size"), QString::number(state.dataSize));
    snapshot.fields.insert(QStringLiteral("target"), hex(state.targetId, 2));
    snapshot.fields.insert(QStringLiteral("status"), hex(state.status, 2));
    snapshot.fields.insert(QStringLiteral("interrupt_status"), hex(state.interruptStatus, 2));
    snapshot.fields.insert(QStringLiteral("sequence_step"), hex(state.sequenceStep, 2));
    snapshot.fields.insert(QStringLiteral("scsi_status"), hex(state.scsiStatus, 2));
    snapshot.fields.insert(QStringLiteral("message"), hex(state.message, 2));
    snapshot.fields.insert(QStringLiteral("data_in"), yesNo(state.dataIn));
    snapshot.fields.insert(QStringLiteral("data_out"), yesNo(state.dataOut));
    snapshot.stateLines.append(QStringLiteral("%1 cdb=%2 target=%3 status=%4 intr=%5 seq=%6")
                                   .arg(id, QString::fromLatin1(state.cdb.toHex(' ')), hex(state.targetId, 2),
                                       hex(state.status, 2), hex(state.interruptStatus, 2),
                                       hex(state.sequenceStep, 2)));
    return snapshot;
}

DeviceSnapshot sccSnapshot(const QString& id, const devices::scc::Z8530Scc::DebugChannelState& state,
    bool interruptActive)
{
    DeviceSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = QStringLiteral("z8530");
    snapshot.fields.insert(QStringLiteral("selected_register"), QString::number(state.selectedRegister));
    snapshot.fields.insert(QStringLiteral("transmit_cycles"), QString::number(state.transmitCycles));
    snapshot.fields.insert(QStringLiteral("baud_rate_cycles"), QString::number(state.baudRateCycles));
    snapshot.fields.insert(QStringLiteral("transmit_pending"), yesNo(state.transmitPending));
    snapshot.fields.insert(QStringLiteral("receive_pending"), yesNo(state.receivePending));
    snapshot.fields.insert(QStringLiteral("external_pending"), yesNo(state.externalPending));
    snapshot.fields.insert(QStringLiteral("interrupt_active"), yesNo(interruptActive));
    QByteArray registers(reinterpret_cast<const char*>(state.writeRegisters.data()),
        static_cast<qsizetype>(state.writeRegisters.size()));
    snapshot.blobs.append({ QStringLiteral("write-registers"), registers });
    snapshot.stateLines.append(QStringLiteral("%1 wr=%2 tx=%3 rx=%4 irq=%5")
                                   .arg(id, QString::fromLatin1(registers.toHex(' ')),
                                       yesNo(state.transmitPending), yesNo(state.receivePending),
                                       yesNo(interruptActive)));
    return snapshot;
}

DeviceSnapshot iwmSnapshot(const QString& id, const devices::iwm::IwmController::DebugState& state)
{
    DeviceSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = QStringLiteral("iwm");
    snapshot.fields.insert(QStringLiteral("lines"), hex(state.lines, 2));
    snapshot.fields.insert(QStringLiteral("mode"), hex(state.mode, 2));
    snapshot.fields.insert(QStringLiteral("status"), hex(state.status, 2));
    snapshot.fields.insert(QStringLiteral("selected_register"), hex(state.selectedRegister, 2));
    snapshot.fields.insert(QStringLiteral("motor_on"), yesNo(state.motorOn));
    snapshot.fields.insert(QStringLiteral("internal_selected"), yesNo(state.internalSelected));
    snapshot.fields.insert(QStringLiteral("disk_inserted"), yesNo(state.diskInserted));
    snapshot.fields.insert(QStringLiteral("double_sided"), yesNo(state.doubleSided));
    snapshot.fields.insert(QStringLiteral("high_density"), yesNo(state.highDensity));
    snapshot.fields.insert(QStringLiteral("writable"), yesNo(state.writable));
    snapshot.fields.insert(QStringLiteral("track"), QString::number(state.track));
    snapshot.fields.insert(QStringLiteral("side"), QString::number(state.side));
    snapshot.fields.insert(QStringLiteral("track_bytes"), QString::number(state.trackBytes));
    snapshot.fields.insert(QStringLiteral("track_cursor"), QString::number(state.trackCursor));
    snapshot.fields.insert(QStringLiteral("data_reads"), QString::number(static_cast<qulonglong>(state.dataReads)));
    snapshot.fields.insert(QStringLiteral("data_writes"), QString::number(static_cast<qulonglong>(state.dataWrites)));
    snapshot.fields.insert(QStringLiteral("image_path"), state.imagePath);
    snapshot.fields.insert(QStringLiteral("image_format"), state.imageFormat);
    snapshot.stateLines.append(QStringLiteral("%1 track=%2 side=%3 motor=%4 disk=%5 image=%6")
                                   .arg(id)
                                   .arg(state.track)
                                   .arg(state.side)
                                   .arg(yesNo(state.motorOn), yesNo(state.diskInserted), state.imagePath));
    return snapshot;
}

DeviceSnapshot adbSnapshot(const QString& id, const devices::adb::AdbTransceiver::DebugState& state)
{
    DeviceSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = QStringLiteral("adb");
    snapshot.fields.insert(QStringLiteral("state"), hex(state.state, 2));
    snapshot.fields.insert(QStringLiteral("command"), hex(state.command, 2));
    snapshot.fields.insert(QStringLiteral("phase"), hex(state.phase, 2));
    snapshot.fields.insert(QStringLiteral("response_bytes"),
        QString::number(static_cast<qulonglong>(state.responseBytes)));
    snapshot.fields.insert(QStringLiteral("transfer_cycles"), QString::number(state.transferCycles));
    snapshot.fields.insert(QStringLiteral("command_pending"), yesNo(state.commandPending));
    snapshot.fields.insert(QStringLiteral("transmitting_from_via"), yesNo(state.transmittingFromVia));
    snapshot.fields.insert(QStringLiteral("pending_mouse_dx"), QString::number(state.pendingMouseDx));
    snapshot.fields.insert(QStringLiteral("pending_mouse_dy"), QString::number(state.pendingMouseDy));
    snapshot.fields.insert(QStringLiteral("keyboard_address"), hex(state.keyboardAddress, 2));
    snapshot.fields.insert(QStringLiteral("mouse_address"), hex(state.mouseAddress, 2));
    snapshot.fields.insert(QStringLiteral("error"), yesNo(state.error));
    snapshot.fields.insert(QStringLiteral("service_request"), yesNo(state.serviceRequest));
    snapshot.fields.insert(QStringLiteral("retry_pending"), yesNo(state.retryPending));
    snapshot.stateLines.append(QStringLiteral("%1 state=%2 command=%3 phase=%4 error=%5 srq=%6")
                                   .arg(id, hex(state.state, 2), hex(state.command, 2), hex(state.phase, 2),
                                       yesNo(state.error), yesNo(state.serviceRequest)));
    return snapshot;
}

DeviceSnapshot cudaSnapshot(const QString& id, const devices::cuda::CudaController::DebugState& state)
{
    DeviceSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = QStringLiteral("cuda");
    snapshot.fields.insert(QStringLiteral("transitions"), QString::number(static_cast<qulonglong>(state.transitions)));
    snapshot.fields.insert(QStringLiteral("attentions"), QString::number(static_cast<qulonglong>(state.attentions)));
    snapshot.fields.insert(QStringLiteral("packets"), QString::number(static_cast<qulonglong>(state.packets)));
    snapshot.fields.insert(QStringLiteral("output"), hex(state.output, 2));
    snapshot.fields.insert(QStringLiteral("direction"), hex(state.direction, 2));
    snapshot.fields.insert(QStringLiteral("last_type"), hex(state.lastType, 2));
    snapshot.fields.insert(QStringLiteral("last_command"), hex(state.lastCommand, 2));
    snapshot.fields.insert(QStringLiteral("last_packet_size"), QString::number(state.lastPacketSize));
    snapshot.fields.insert(QStringLiteral("last_address"), hex(state.lastAddress, 4));
    snapshot.fields.insert(QStringLiteral("tip"), yesNo(state.tip));
    snapshot.fields.insert(QStringLiteral("byte_ack"), yesNo(state.byteAck));
    snapshot.stateLines.append(QStringLiteral("%1 packets=%2 last_command=%3 tip=%4 byte_ack=%5")
                                   .arg(id)
                                   .arg(static_cast<qulonglong>(state.packets))
                                   .arg(hex(state.lastCommand, 2), yesNo(state.tip), yesNo(state.byteAck)));
    return snapshot;
}

DeviceSnapshot rtcSnapshot(const QString& id, const devices::rtc::MacRtc& rtc)
{
    DeviceSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = QStringLiteral("mac-rtc");
    snapshot.fields.insert(QStringLiteral("nvram_path"), rtc.nvramImagePath());
    snapshot.blobs.append({ QStringLiteral("pram"), rtc.parameterRamBytes() });
    snapshot.stateLines.append(QStringLiteral("%1 nvram=%2 pram_bytes=%3")
                                   .arg(id, rtc.nvramImagePath())
                                   .arg(rtc.parameterRamBytes().size()));
    return snapshot;
}

QVector<DeviceSnapshot> nubusSnapshots(const devices::nubus::NuBusBus& bus)
{
    QVector<DeviceSnapshot> snapshots;
    for (int slot = 0; slot < 16; ++slot) {
        const auto card = bus.card(slot);
        if (!card) continue;
        auto snapshot = card->debugSnapshot();
        snapshot.fields.insert(QStringLiteral("slot"), QStringLiteral("0x%1").arg(slot, 1, 16));
        snapshot.id = QStringLiteral("nubus-slot-%1/%2").arg(slot, 1, 16).arg(snapshot.id);
        snapshots.append(snapshot);
    }
    return snapshots;
}

DeviceSnapshot lowMemorySnapshot(const MemoryReader& read8)
{
    DeviceSnapshot snapshot;
    snapshot.id = QStringLiteral("lowmem");
    snapshot.kind = QStringLiteral("low-memory-globals");
    if (!read8) return snapshot;

    static constexpr std::array<std::pair<const char*, std::uint32_t>, 16> globals { {
        { "MemTop", 0x0108 }, { "BufPtr", 0x010c }, { "StkLowPt", 0x0110 },
        { "HeapEnd", 0x0114 }, { "TheZone", 0x0118 }, { "ApplZone", 0x02aa },
        { "SysZone", 0x02a6 }, { "CurrentA5", 0x0904 }, { "CurStackBase", 0x0908 },
        { "ROMBase", 0x02ae }, { "RAMBase", 0x02b2 }, { "ScrnBase", 0x0824 },
        { "Ticks", 0x016a }, { "Time", 0x020c }, { "SysEvtMask", 0x0144 },
        { "DskErr", 0x0142 },
    } };
    for (const auto& [name, address] : globals) {
        snapshot.fields.insert(QString::fromLatin1(name), hex(read32(read8, address), 8));
    }
    // The Sad Mac path leaves its code at the very bottom of memory.
    snapshot.fields.insert(QStringLiteral("SadMacCode"), hex(read32(read8, 0x0000), 8));
    snapshot.fields.insert(QStringLiteral("SadMacExtra"), hex(read32(read8, 0x0010), 8));

    for (auto entry = snapshot.fields.constBegin(); entry != snapshot.fields.constEnd(); ++entry) {
        snapshot.stateLines.append(QStringLiteral("%1=%2").arg(entry.key(), entry.value()));
    }
    return snapshot;
}

} // namespace cutemac::debug
