#include "cutemac/session/FramebufferRenderer.h"

#include <algorithm>

namespace cutemac::session {

namespace {

QRgb paletteColor(const devices::video::VideoFrame& frame, int index)
{
    if (index >= 0 && index < frame.palette.size()) {
        return static_cast<QRgb>(frame.palette[index]);
    }
    const auto levels = std::max(1, frame.palette.isEmpty() ? 255 : static_cast<int>(frame.palette.size()) - 1);
    const auto value = 255 - (index * 255 / levels);
    return qRgb(value, value, value);
}

} // namespace

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

QImage FramebufferRenderer::render(const devices::video::VideoFrame& frame)
{
    if (!frame.valid()) {
        return {};
    }
    QImage image(frame.width, frame.height, QImage::Format_RGB32);
    for (int y = 0; y < frame.height; ++y) {
        const auto* source = reinterpret_cast<const std::uint8_t*>(frame.pixels.constData() + y * frame.strideBytes);
        auto* destination = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < frame.width; ++x) {
            int index = 0;
            switch (frame.format) {
            case devices::video::PixelFormat::Monochrome1:
            case devices::video::PixelFormat::Indexed1:
                index = (source[x >> 3] >> (7 - (x & 7))) & 1;
                destination[x] = paletteColor(frame, index);
                break;
            case devices::video::PixelFormat::Indexed2:
                index = (source[x >> 2] >> (6 - ((x & 3) * 2))) & 3;
                destination[x] = paletteColor(frame, index);
                break;
            case devices::video::PixelFormat::Indexed4:
                index = (source[x >> 1] >> (4 - ((x & 1) * 4))) & 15;
                destination[x] = paletteColor(frame, index);
                break;
            case devices::video::PixelFormat::Indexed8:
                destination[x] = paletteColor(frame, source[x]);
                break;
            case devices::video::PixelFormat::RGB555: {
                const auto pixel = static_cast<std::uint16_t>((source[x * 2] << 8) | source[x * 2 + 1]);
                destination[x] = qRgb(((pixel >> 10) & 31) * 255 / 31, ((pixel >> 5) & 31) * 255 / 31,
                    (pixel & 31) * 255 / 31);
                break;
            }
            case devices::video::PixelFormat::RGB888:
                destination[x] = qRgb(source[x * 3], source[x * 3 + 1], source[x * 3 + 2]);
                break;
            case devices::video::PixelFormat::XRGB8888:
                destination[x] = qRgb(source[x * 4 + 1], source[x * 4 + 2], source[x * 4 + 3]);
                break;
            }
        }
    }
    return image;
}

} // namespace cutemac::session
