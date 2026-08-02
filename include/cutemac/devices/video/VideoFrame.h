#pragma once

#include <cstdint>

#include <QByteArray>
#include <QVector>

namespace cutemac::devices::video {

enum class PixelStorage { Indexed, Direct };
enum class ByteOrder { BigEndian, LittleEndian };
enum class BitOrder { MostSignificantFirst, LeastSignificantFirst };

struct ChannelLayout {
    std::uint32_t redMask = 0;
    std::uint32_t greenMask = 0;
    std::uint32_t blueMask = 0;
    std::uint32_t alphaMask = 0;
};

struct VideoFrame {
    int width = 0;
    int height = 0;
    int strideBytes = 0;
    PixelStorage storage = PixelStorage::Indexed;
    int bitsPerPixel = 1;
    ByteOrder byteOrder = ByteOrder::BigEndian;
    BitOrder bitOrder = BitOrder::MostSignificantFirst;
    QByteArray pixels;
    QVector<std::uint32_t> colorTable;
    QVector<std::uint16_t> pixelToColorIndex;
    ChannelLayout channels;

    [[nodiscard]] bool valid() const
    {
        const bool supportedDepth = storage == PixelStorage::Indexed
            ? (bitsPerPixel == 1 || bitsPerPixel == 2 || bitsPerPixel == 4 || bitsPerPixel == 8)
            : (bitsPerPixel == 15 || bitsPerPixel == 16 || bitsPerPixel == 24 || bitsPerPixel == 32);
        return width > 0 && height > 0 && strideBytes > 0 && supportedDepth
            && pixels.size() >= static_cast<qsizetype>(strideBytes) * height;
    }
};

} // namespace cutemac::devices::video
