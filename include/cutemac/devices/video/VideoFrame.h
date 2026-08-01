#pragma once

#include <cstdint>

#include <QByteArray>
#include <QVector>

namespace cutemac::devices::video {

enum class PixelFormat {
    Monochrome1,
    Indexed1,
    Indexed2,
    Indexed4,
    Indexed8,
    RGB555,
    RGB888,
    XRGB8888,
};

struct VideoFrame {
    int width = 0;
    int height = 0;
    int strideBytes = 0;
    PixelFormat format = PixelFormat::Monochrome1;
    QByteArray pixels;
    QVector<std::uint32_t> palette;

    [[nodiscard]] bool valid() const
    {
        return width > 0 && height > 0 && strideBytes > 0
            && pixels.size() >= static_cast<qsizetype>(strideBytes) * height;
    }
};

} // namespace cutemac::devices::video
