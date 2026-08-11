#include "cutemac/cpu/m68k/M68kCoprocessorDiagnostics.h"

#include <QVector>

#include <algorithm>

namespace cutemac::cpu::m68k {

namespace {

// Small enough to cost nothing, long enough that a burst of refusals during one
// failing operation does not push out the first and most informative record.
constexpr int maximumRecords = 64;

QVector<CoprocessorDiagnosticRecord>& records()
{
    // The emulation thread owns this; the debug session reads it under the same
    // session lock that guards the rest of the machine's debug state.
    static QVector<CoprocessorDiagnosticRecord> ring;
    return ring;
}

} // namespace

QVector<CoprocessorDiagnosticRecord> coprocessorDiagnosticRecords()
{
    return records();
}

QStringList coprocessorDiagnosticLines()
{
    QStringList lines;
    for (const auto& record : records()) {
        lines.append(QStringLiteral("%1 pc=0x%2 opcode=0x%3 ext=0x%4 %5%6")
                .arg(record.unit)
                .arg(record.pc, 8, 16, QLatin1Char('0'))
                .arg(record.opcode, 4, 16, QLatin1Char('0'))
                .arg(record.extension, 4, 16, QLatin1Char('0'))
                .arg(record.detail,
                    record.cutemacLimitation ? QStringLiteral(" [unimplemented in CuteMac]")
                                             : QStringLiteral(" [illegal encoding]")));
    }
    return lines;
}

void clearCoprocessorDiagnostics()
{
    records().clear();
}

} // namespace cutemac::cpu::m68k

extern "C" void cutemac_m68k_coprocessor_report(uint32_t pc, uint16_t opcode, uint16_t extension,
    const char* unit, const char* detail, int limitation)
{
    using namespace cutemac::cpu::m68k;

    auto& ring = records();
    CoprocessorDiagnosticRecord record;
    record.pc = pc;
    record.opcode = opcode;
    record.extension = extension;
    record.unit = QString::fromLatin1(unit != nullptr ? unit : "");
    record.detail = QString::fromLatin1(detail != nullptr ? detail : "");
    record.cutemacLimitation = limitation != 0;

    // Repeats are common: a loop that calls one missing routine would otherwise
    // flush every other record out of the ring.
    const auto duplicate = std::find_if(ring.begin(), ring.end(), [&](const CoprocessorDiagnosticRecord& existing) {
        return existing.pc == record.pc && existing.opcode == record.opcode
            && existing.extension == record.extension;
    });
    if (duplicate != ring.end()) return;

    if (ring.size() >= maximumRecords) ring.removeFirst();
    ring.append(std::move(record));
}
