#pragma once

#include "cutemac/core/Device.h"

namespace cutemac::devices::video {

class VideoDevice final : public core::Device {
public:
    [[nodiscard]] QString id() const override;
    void reset() override;
};

} // namespace cutemac::devices::video
