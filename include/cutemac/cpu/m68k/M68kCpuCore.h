#pragma once

#include "cutemac/core/CpuCore.h"

namespace cutemac::cpu::m68k {

class M68kCpuCore final : public core::CpuCore {
public:
    [[nodiscard]] QString id() const override;
    void reset() override;
};

} // namespace cutemac::cpu::m68k
