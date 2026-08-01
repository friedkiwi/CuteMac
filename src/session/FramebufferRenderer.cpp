#include "cutemac/session/FramebufferRenderer.h"

namespace cutemac::session {

QImage FramebufferRenderer::renderMonochrome(const QByteArray& bytes, int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);
    if (bytes.size() < (width * height) / 8) {
        return image;
    }
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const auto byte = static_cast<std::uint8_t>(bytes[(y * width + x) / 8]);
            line[x] = (byte & (0x80U >> (x & 7))) != 0 ? qRgb(0, 0, 0) : qRgb(255, 255, 255);
        }
    }
    return image;
}

} // namespace cutemac::session
