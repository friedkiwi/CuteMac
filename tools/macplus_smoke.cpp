#include <QCoreApplication>
#include <QTextStream>

#include "cutemac/machines/macplus/MacPlusMachine.h"

namespace {

void printUsage(QTextStream& stream, const char* executableName)
{
    stream << "usage: " << executableName << " <MacPlus ROM path> [cycles]\n";
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (argc < 2) {
        printUsage(err, argv[0]);
        return 2;
    }

    const auto romPath = QString::fromLocal8Bit(argv[1]);
    const auto cycles = argc >= 3 ? QString::fromLocal8Bit(argv[2]).toInt() : 130560;

    cutemac::machines::macplus::MacPlusMachine machine;
    if (!machine.loadRomFile(romPath)) {
        err << "failed to load 128 KiB Mac Plus ROM: " << romPath << '\n';
        return 1;
    }

    machine.reset();
    const auto cyclesRun = machine.runCycles(cycles);
    const auto& summary = machine.accessSummary();

    out << "cycles_requested=" << cycles << '\n';
    out << "cycles_run=" << cyclesRun << '\n';
    out << "pc=0x" << QString::number(machine.programCounter(), 16) << '\n';
    out << "overlay=" << (machine.overlayEnabled() ? "on" : "off") << '\n';
    out << "ram_reads=" << summary.ramReads << " ram_writes=" << summary.ramWrites << '\n';
    out << "rom_reads=" << summary.romReads << '\n';
    out << "via_reads=" << summary.viaReads << " via_writes=" << summary.viaWrites << '\n';
    out << "scc_reads=" << summary.sccReads << " scc_writes=" << summary.sccWrites << '\n';
    out << "iwm_reads=" << summary.iwmReads << " iwm_writes=" << summary.iwmWrites << '\n';
    out << "scsi_reads=" << summary.scsiReads << " scsi_writes=" << summary.scsiWrites << '\n';
    out << "configuration_reads=" << summary.configurationReads << '\n';
    out << "unmapped_reads=" << summary.unmappedReads << " unmapped_writes=" << summary.unmappedWrites << '\n';

    for (const auto& event : machine.eventLog()) {
        out << "event: " << event << '\n';
    }

    return summary.unmappedReads == 0 && summary.unmappedWrites == 0 ? 0 : 3;
}
