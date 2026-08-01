#include "cutemac/cpu/m68k/M68kCpuCore.h"

namespace cutemac::cpu::m68k {

QString M68kCpuCore::id() const
{
    return QStringLiteral("cpu.m68k");
}

void M68kCpuCore::reset()
{
}

} // namespace cutemac::cpu::m68k
