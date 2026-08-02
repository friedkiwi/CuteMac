#pragma once

#include <QByteArray>
#include <QImage>

#include "cutemac/devices/video/VideoFrame.h"

namespace cutemac::session {

class FramebufferRenderer {
public:
    [[nodiscard]] static QImage renderMonochrome(const QByteArray& bytes, int width, int height);
    [[nodiscard]] static QImage render(const devices::video::VideoFrame& frame);
    static bool update(QImage& image, const devices::video::VideoFrame& frame);
};

} // namespace cutemac::session
