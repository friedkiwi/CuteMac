#include "cutemac/cpu/m68k/M68kFpuDiagnostics.h"

#include <QVector>

#include <algorithm>

namespace cutemac::cpu::m68k {

namespace {

// Small enough to cost nothing, long enough that a burst of refusals during one
// failing operation does not push out the first and most informative record.
constexpr int maximumRecords = 64;

QVector<FpuDiagnosticRecord>& records()
{
    // The emulation thread owns this; the debug session reads it under the same
    // session lock that guards the rest of the machine's debug state.
    static QVector<FpuDiagnosticRecord> ring;
    return ring;
}

} // namespace

QVector<FpuDiagnosticRecord> fpuDiagnosticRecords()
{
    return records();
}

QStringList fpuDiagnosticLines()
{
    QStringList lines;
    for (const auto& record : records()) {
        lines.append(QStringLiteral("pc=0x%1 opcode=0x%2 ext=0x%3 %4%5")
                .arg(record.pc, 8, 16, QLatin1Char('0'))
                .arg(record.opcode, 4, 16, QLatin1Char('0'))
                .arg(record.extension, 4, 16, QLatin1Char('0'))
                .arg(record.detail,
                    record.cutemacLimitation ? QStringLiteral(" [unimplemented in CuteMac]")
                                             : QStringLiteral(" [illegal encoding]")));
    }
    return lines;
}

void clearFpuDiagnostics()
{
    records().clear();
}

} // namespace cutemac::cpu::m68k

extern "C" void cutemac_m68k_fpu_report(uint32_t pc, uint16_t opcode, uint16_t extension,
    const char* detail, int limitation)
{
    using namespace cutemac::cpu::m68k;

    auto& ring = records();
    FpuDiagnosticRecord record;
    record.pc = pc;
    record.opcode = opcode;
    record.extension = extension;
    record.detail = QString::fromLatin1(detail != nullptr ? detail : "");
    record.cutemacLimitation = limitation != 0;

    // Repeats are common: a loop that calls one missing routine would otherwise
    // flush every other record out of the ring.
    const auto duplicate = std::find_if(ring.begin(), ring.end(), [&](const FpuDiagnosticRecord& existing) {
        return existing.pc == record.pc && existing.opcode == record.opcode
            && existing.extension == record.extension;
    });
    if (duplicate != ring.end()) return;

    if (ring.size() >= maximumRecords) ring.removeFirst();
    ring.append(std::move(record));
}
