#include "cutemac/cpu/ppc/PpcCpuCore.h"

namespace cutemac::cpu::ppc {

QString PpcCpuCore::id() const
{
    return QStringLiteral("cpu.ppc");
}

void PpcCpuCore::reset()
{
}

} // namespace cutemac::cpu::ppc
