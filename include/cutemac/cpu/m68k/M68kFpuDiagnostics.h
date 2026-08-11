#pragma once

// Reporting boundary for floating-point encodings the FPU cannot execute.
//
// The Musashi-derived FPU used to call a local fatalerror() that ran exit(1),
// so a single guest instruction could terminate CuteMac with one line on
// stderr: no dialog, no panic dump, nothing to diagnose from. Every one of
// those sites now reports through here and raises a line-1111 exception, which
// is what the hardware does for an encoding it cannot execute. The guest sees
// system error 10 and the emulator stays up.
//
// The C engine calls the extern "C" entry point; the ring behind it is bounded
// and lives on the emulation thread, and is published through the debug
// boundary so a panic dump can name the opcode instead of leaving a reader to
// guess.

#ifdef __cplusplus

#include <cstdint>

#include <QString>
#include <QStringList>
#include <QVector>

namespace cutemac::cpu::m68k {

struct FpuDiagnosticRecord {
    std::uint32_t pc = 0;       // instruction that could not be executed
    std::uint16_t opcode = 0;   // first word
    std::uint16_t extension = 0; // second word, 0 when the form has none
    // "unimplemented opmode $10 (FETOX)", "FSAVE addressing mode 7"
    QString detail;
    // False for encodings the hardware would also refuse. True marks a CuteMac
    // limitation, so a reader can tell "the guest is wrong" from "we are".
    bool cutemacLimitation = false;
};

// Bounded ring, oldest dropped. Cleared on CPU reset.
[[nodiscard]] QVector<FpuDiagnosticRecord> fpuDiagnosticRecords();
[[nodiscard]] QStringList fpuDiagnosticLines();
void clearFpuDiagnostics();

} // namespace cutemac::cpu::m68k

extern "C" {
#endif

#include <stdint.h>

// `limitation` is non-zero when CuteMac is what cannot execute the encoding,
// zero when the hardware would refuse it too.
void cutemac_m68k_fpu_report(uint32_t pc, uint16_t opcode, uint16_t extension,
    const char* detail, int limitation);

#ifdef __cplusplus
}
#endif
