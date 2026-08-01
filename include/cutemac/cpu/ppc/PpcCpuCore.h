#pragma once

#include "cutemac/core/CpuCore.h"

namespace cutemac::cpu::ppc {

class PpcCpuCore final : public core::CpuCore {
public:
    [[nodiscard]] QString id() const override;
    void reset() override;
};

} // namespace cutemac::cpu::ppc
