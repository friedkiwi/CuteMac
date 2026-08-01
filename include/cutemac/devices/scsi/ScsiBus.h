#pragma once

#include "cutemac/core/Device.h"

namespace cutemac::devices::scsi {

class ScsiBus final : public core::Device {
public:
    [[nodiscard]] QString id() const override;
    void reset() override;
};

} // namespace cutemac::devices::scsi
