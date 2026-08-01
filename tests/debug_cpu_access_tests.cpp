#include <iostream>

#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/machines/maciicx/MacIIcxMachine.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    cutemac::machines::maciicx::MacIIcxMachine machine(8 * 1024 * 1024);
    auto* debug = dynamic_cast<cutemac::core::IDebugCpuAccess*>(&machine);
    bool ok = expect(debug != nullptr, "IIcx must publish common CPU debug access");
    if (!debug) return 1;

    ok &= expect(debug->debugCpuArchitecture() == QStringLiteral("m68k:68030"), "IIcx debug architecture");
    debug->debugWrite32(0x00100000, 0x12345678);
    ok &= expect(debug->debugRead32(0x00100000) == 0x12345678, "IIcx debug memory access");
    ok &= expect(!debug->debugRegisterLines().isEmpty(), "IIcx debug register formatting");
    ok &= expect(debug->disassembleBytes(debug->programCounter()) >= 2, "IIcx debug disassembly length");
    return ok ? 0 : 1;
}
