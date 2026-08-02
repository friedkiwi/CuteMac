#include "cutemac/session/FramebufferRenderer.h"

#include <algorithm>

namespace cutemac::session {

namespace {

std::uint32_t indexedValue(const devices::video::VideoFrame& frame, const std::uint8_t* source, int x)
{
    if (frame.bitsPerPixel == 8) return source[x];
    const auto pixelsPerByte = 8 / frame.bitsPerPixel;
    const auto position = x % pixelsPerByte;
    const auto shift = frame.bitOrder == devices::video::BitOrder::MostSignificantFirst
        ? 8 - frame.bitsPerPixel * (position + 1)
        : frame.bitsPerPixel * position;
    return (source[x / pixelsPerByte] >> shift) & ((1U << frame.bitsPerPixel) - 1U);
}

QRgb indexedColor(const devices::video::VideoFrame& frame, std::uint32_t pixelValue)
{
    auto colorIndex = pixelValue;
    if (pixelValue < static_cast<std::uint32_t>(frame.pixelToColorIndex.size())) {
        colorIndex = frame.pixelToColorIndex[static_cast<qsizetype>(pixelValue)];
    }
    if (colorIndex < static_cast<std::uint32_t>(frame.colorTable.size())) {
        return static_cast<QRgb>(frame.colorTable[static_cast<qsizetype>(colorIndex)]);
    }
    const auto maximum = std::max(1U, (1U << frame.bitsPerPixel) - 1U);
    const auto value = 255 - static_cast<int>(pixelValue * 255U / maximum);
    return qRgb(value, value, value);
}

std::uint32_t directValue(const devices::video::VideoFrame& frame, const std::uint8_t* source, int x)
{
    const auto bytesPerPixel = (frame.bitsPerPixel + 7) / 8;
    const auto* pixel = source + x * bytesPerPixel;
    std::uint32_t value = 0;
    if (frame.byteOrder == devices::video::ByteOrder::BigEndian) {
        for (int byte = 0; byte < bytesPerPixel; ++byte) value = (value << 8) | pixel[byte];
    } else {
        for (int byte = bytesPerPixel - 1; byte >= 0; --byte) value = (value << 8) | pixel[byte];
    }
    return value;
}

int channel(std::uint32_t value, std::uint32_t mask)
{
    if (mask == 0) return 0;
    int shift = 0;
    while ((mask & 1U) == 0) {
        mask >>= 1;
        ++shift;
    }
    return static_cast<int>(((value >> shift) & mask) * 255U / mask);
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
            if (frame.storage == devices::video::PixelStorage::Indexed) {
                destination[x] = indexedColor(frame, indexedValue(frame, source, x));
            } else {
                const auto value = directValue(frame, source, x);
                destination[x] = qRgb(channel(value, frame.channels.redMask), channel(value, frame.channels.greenMask),
                    channel(value, frame.channels.blueMask));
            }
        }
    }
    return image;
}

} // namespace cutemac::session
