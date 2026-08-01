#pragma once

#include <QByteArray>
#include <QImage>

namespace cutemac::session {

class FramebufferRenderer {
public:
    [[nodiscard]] static QImage renderMonochrome(const QByteArray& bytes, int width, int height);
};

} // namespace cutemac::session
