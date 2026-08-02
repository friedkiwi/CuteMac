#include "cutemac/debug/SadMacDetector.h"

#include <algorithm>

namespace cutemac::debug {
namespace {

bool darkPixel(const devices::video::VideoFrame& frame, int x, int y)
{
    if (!frame.valid() || frame.storage != devices::video::PixelStorage::Indexed
        || x < 0 || y < 0 || x >= frame.width || y >= frame.height) return false;
    const auto bit = x * frame.bitsPerPixel;
    const auto byte = static_cast<std::uint8_t>(frame.pixels.at(y * frame.strideBytes + bit / 8));
    const auto shift = frame.bitOrder == devices::video::BitOrder::MostSignificantFirst
        ? 8 - frame.bitsPerPixel - (bit & 7) : bit & 7;
    const auto mask = (1U << frame.bitsPerPixel) - 1U;
    auto index = static_cast<unsigned>((byte >> shift) & mask);
    if (index < static_cast<unsigned>(frame.pixelToColorIndex.size())) index = frame.pixelToColorIndex[index];
    if (index >= static_cast<unsigned>(frame.colorTable.size())) return index != 0;
    const auto color = frame.colorTable[static_cast<qsizetype>(index)];
    return ((color >> 16) & 0xffU) + ((color >> 8) & 0xffU) + (color & 0xffU) < 3U * 128U;
}

} // namespace

bool SadMacDetector::detect(const devices::video::VideoFrame& frame)
{
    if (!frame.valid() || frame.storage != devices::video::PixelStorage::Indexed) return false;
    const auto center = frame.width / 2;
    const auto darkCount = [&](int y, int halfWidth) {
        int count = 0;
        for (int x = std::max(0, center - halfWidth); x < std::min(frame.width, center + halfWidth); ++x)
            if (darkPixel(frame, x, y)) ++count;
        return count;
    };
    // The ROM Sad Mac renderer uses two centered rows of eight six-pixel-high
    // hexadecimal glyphs beneath a roughly 32-pixel-high icon. Match this
    // frontend-neutral layout instead of a machine VRAM address or palette.
    for (int y = frame.height / 3; y + 18 < frame.height * 3 / 4; ++y) {
        bool glyphRows = true;
        for (int row = 0; row < 6; ++row) {
            const auto first = darkCount(y + row, 48);
            const auto second = darkCount(y + 12 + row, 48);
            if (first < 8 || first > 55 || second < 8 || second > 55) { glyphRows = false; break; }
        }
        if (!glyphRows) continue;
        int gapInk = 0;
        for (int row = 6; row < 12; ++row) gapInk += darkCount(y + row, 48);
        if (gapInk > 4) continue;
        int iconInk = 0;
        int iconRows = 0;
        for (int row = std::max(0, y - 42); row < y - 4; ++row) {
            const auto ink = darkCount(row, 28);
            iconInk += ink;
            if (ink >= 2) ++iconRows;
        }
        if (iconInk >= 100 && iconRows >= 18) return true;
    }
    return false;
}

} // namespace cutemac::debug
