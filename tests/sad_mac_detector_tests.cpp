#include <cstdlib>
#include <iostream>

#include "cutemac/debug/SadMacDetector.h"

namespace {

void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

cutemac::devices::video::VideoFrame frame(int width, int height)
{
    cutemac::devices::video::VideoFrame result;
    result.width = width;
    result.height = height;
    result.strideBytes = (width + 7) / 8;
    result.bitsPerPixel = 1;
    result.pixels = QByteArray(result.strideBytes * height, 0);
    result.colorTable = { 0xffffffffU, 0xff000000U };
    return result;
}

void setDark(cutemac::devices::video::VideoFrame& frame, int x, int y)
{
    auto value = static_cast<std::uint8_t>(frame.pixels[y * frame.strideBytes + x / 8]);
    value |= static_cast<std::uint8_t>(0x80U >> (x & 7));
    frame.pixels[y * frame.strideBytes + x / 8] = static_cast<char>(value);
}

void drawSadLayout(cutemac::devices::video::VideoFrame& frame)
{
    const auto center = frame.width / 2;
    const auto codeY = frame.height / 2 + 24;
    for (int y = codeY - 38; y < codeY - 6; ++y)
        for (int x = center - 16; x < center + 16; ++x)
            if (y == codeY - 38 || y == codeY - 7 || x == center - 16 || x == center + 15) setDark(frame, x, y);
    for (const auto baseY : { codeY, codeY + 12 })
        for (int row = 0; row < 6; ++row)
            for (int digit = 0; digit < 8; ++digit) {
                setDark(frame, center - 34 + digit * 9, baseY + row);
                setDark(frame, center - 32 + digit * 9, baseY + row);
            }
}

} // namespace

int main()
{
    auto plus = frame(512, 342);
    require(!cutemac::debug::SadMacDetector::detect(plus), "blank Mac Plus frame");
    drawSadLayout(plus);
    require(cutemac::debug::SadMacDetector::detect(plus), "Mac Plus Sad Mac layout");

    auto colorMachine = frame(640, 480);
    drawSadLayout(colorMachine);
    require(cutemac::debug::SadMacDetector::detect(colorMachine), "IIcx/PM8100 Sad Mac layout");
    colorMachine.colorTable = { 0xff000000U, 0xffffffffU };
    require(!cutemac::debug::SadMacDetector::detect(colorMachine), "palette interpretation");
    std::cout << "Sad Mac detector tests passed\n";
    return 0;
}
