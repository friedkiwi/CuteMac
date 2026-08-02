#pragma once

#include "cutemac/devices/video/VideoFrame.h"

namespace cutemac::debug {

class SadMacDetector {
public:
    [[nodiscard]] static bool detect(const devices::video::VideoFrame& frame);
};

} // namespace cutemac::debug
