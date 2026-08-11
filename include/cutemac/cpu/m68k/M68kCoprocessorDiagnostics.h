#pragma once

// Reporting boundary for F-line coprocessor encodings the CPU cannot execute.
//
// Both F-line units report here: the FPU (coprocessor id 1) and the PMMU
// (coprocessor id 0). The FPU used to call a local fatalerror() that ran
// exit(1), so a single guest instruction could terminate CuteMac; the PMMU
// raised line-1111 silently, which is just as hard to diagnose because the
// guest bombs with system error 10 and nothing records why. Every site now
// reports through here and raises line-1111, which is what the hardware does
// for an encoding it cannot execute.
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

struct CoprocessorDiagnosticRecord {
    std::uint32_t pc = 0;       // instruction that could not be executed
    std::uint16_t opcode = 0;   // first word
    std::uint16_t extension = 0; // second word, 0 when the form has none
    QString unit;               // "fpu" or "pmmu"
    // "unimplemented opmode $10 (FETOX)", "FSAVE addressing mode 7"
    QString detail;
    // False for encodings the hardware would also refuse. True marks a CuteMac
    // limitation, so a reader can tell "the guest is wrong" from "we are".
    bool cutemacLimitation = false;
};

// Bounded ring, oldest dropped. Cleared on CPU reset.
[[nodiscard]] QVector<CoprocessorDiagnosticRecord> coprocessorDiagnosticRecords();
[[nodiscard]] QStringList coprocessorDiagnosticLines();
void clearCoprocessorDiagnostics();

} // namespace cutemac::cpu::m68k

extern "C" {
#endif

#include <stdint.h>

// `limitation` is non-zero when CuteMac is what cannot execute the encoding,
// zero when the hardware would refuse it too.
void cutemac_m68k_coprocessor_report(uint32_t pc, uint16_t opcode, uint16_t extension,
    const char* unit, const char* detail, int limitation);

#ifdef __cplusplus
}
#endif
