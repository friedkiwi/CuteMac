#include "cutemac/devices/video/VideoDevice.h"

namespace cutemac::devices::video {

QString VideoDevice::id() const
{
    return QStringLiteral("device.video");
}

void VideoDevice::reset()
{
}

} // namespace cutemac::devices::video
