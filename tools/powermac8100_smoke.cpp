#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>

#include <QCoreApplication>

#include "cutemac/machines/powermac8100/PowerMac8100Machine.h"

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: CuteMacPowerMac8100Smoke <rom> [cycles]\n";
        return 2;
    }
    const auto cycleBudget = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 1'000'000;
    cutemac::machines::powermac8100::PowerMac8100Machine machine(8U * 1024U * 1024U);
    if (!machine.loadRomFile(QString::fromLocal8Bit(argv[1]), {})) {
        std::cerr << "failed to load 4 MiB Power Macintosh ROM\n";
        return 1;
    }
    machine.setBusTraceEnabled(true);
    machine.reset();
    std::map<std::uint32_t, unsigned> exceptions;
    std::map<std::uint32_t, unsigned> sampledPcs;
    auto lastPc = machine.programCounter();
    bool stopPcOk = false;
    const auto stopPc = qEnvironmentVariable("CUTEMAC_8100_STOP_PC").toUInt(&stopPcOk, 0);
    int stationary = 0;
    for (int used = 0; used < cycleBudget;) {
        used += machine.stepInstruction();
        const auto pc = machine.programCounter();
        if ((pc & 0xfffU) == 0x300U || (pc & 0xfffU) == 0x400U || (pc & 0xfffU) == 0x700U)
            ++exceptions[pc];
        if ((used & 0x3ffU) == 0) ++sampledPcs[pc];
        stationary = pc == lastPc ? stationary + 1 : 0;
        lastPc = pc;
        if (stopPcOk && pc == stopPc) break;
        if (qEnvironmentVariableIsSet("CUTEMAC_8100_STOP_ON_UNMAPPED") && machine.unmappedAccessCount() != 0) break;
        if (qEnvironmentVariableIsSet("CUTEMAC_8100_STOP_ON_ALIGNMENT") && pc == 0xfff00600U) break;
        if (qEnvironmentVariableIsSet("CUTEMAC_8100_STOP_ON_PROGRAM") && pc == 0xfff00700U) break;
        if (stationary > 10000) break;
    }
    const auto state = machine.cpuRegisters();
    std::cout << std::hex << std::setfill('0')
              << "pc=0x" << std::setw(8) << state.pc << " msr=0x" << std::setw(8) << state.msr
              << " srr0=0x" << std::setw(8) << state.srr0 << " srr1=0x" << std::setw(8) << state.srr1
              << " lr=0x" << std::setw(8) << state.lr << " ctr=0x" << std::setw(8) << state.ctr
              << " r6=0x" << std::setw(8) << state.gpr[6] << " r8=0x" << std::setw(8) << state.gpr[8]
              << " r10=0x" << std::setw(8) << state.gpr[10]
              << " dar=0x" << std::setw(8) << state.dar << " dsisr=0x" << std::setw(8) << state.dsisr
              << " sdr1=0x" << std::setw(8) << state.sdr1 << " sr6=0x" << std::setw(8) << state.sr[6]
              << " bats=0x" << std::setw(8) << state.batu[0] << '/' << std::setw(8) << state.batl[0]
              << ',' << std::setw(8) << state.batu[1] << '/' << std::setw(8) << state.batl[1]
              << ',' << std::setw(8) << state.batu[2] << '/' << std::setw(8) << state.batl[2]
              << ',' << std::setw(8) << state.batu[3] << '/' << std::setw(8) << state.batl[3]
              << std::dec << " cycles=" << machine.cycleCount()
              << " unmapped=" << machine.unmappedAccessCount() << '\n';
    std::cout << machine.disassemble(state.pc).toStdString() << '\n';
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
    const auto begin = trace.size() > 160 ? trace.size() - 160 : 0;
    for (std::size_t i = begin; i < trace.size(); ++i) {
        const auto& access = trace[i];
        std::cout << access.cycle << " pc=0x" << std::hex << access.pc
                  << (access.write ? " write" : " read") << std::dec << static_cast<unsigned>(access.size) * 8
                  << " [0x" << std::hex << access.address << "]=0x" << access.value
                  << " region=" << std::dec << static_cast<unsigned>(access.region) << '\n';
    }
    return 0;
}
