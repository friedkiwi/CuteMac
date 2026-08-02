#include <QColor>
#include <QImage>

#include <iostream>

#include "cutemac/devices/video/VideoFrame.h"
#include "cutemac/session/FramebufferRenderer.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool isColor(const QImage& image, int x, QRgb color)
{
    return image.valid(x, 0) && image.pixel(x, 0) == color;
}

} // namespace

int main()
{
    using namespace cutemac::devices::video;
    bool ok = true;

    QVector<std::uint32_t> clut(256, qRgb(255, 0, 255));
    clut[0] = qRgb(255, 255, 255);
    clut[64] = qRgb(255, 0, 0);
    clut[128] = qRgb(0, 0, 0);
    clut[192] = qRgb(0, 255, 0);
    clut[255] = qRgb(0, 0, 255);

    VideoFrame indexed1 { 2, 1, 1, PixelStorage::Indexed, 1, ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst, QByteArray(1, char(0x40)), clut, { 0, 128 }, {} };
    auto image = cutemac::session::FramebufferRenderer::render(indexed1);
    ok &= expect(isColor(image, 0, qRgb(255, 255, 255)) && isColor(image, 1, qRgb(0, 0, 0)),
        "one-bit pixels must use the device's CLUT mapping");

    VideoFrame indexed2 { 4, 1, 1, PixelStorage::Indexed, 2, ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst, QByteArray(1, char(0x1b)), clut, { 0, 64, 128, 192 }, {} };
    image = cutemac::session::FramebufferRenderer::render(indexed2);
    ok &= expect(isColor(image, 0, qRgb(255, 255, 255)) && isColor(image, 1, qRgb(255, 0, 0))
            && isColor(image, 2, qRgb(0, 0, 0)) && isColor(image, 3, qRgb(0, 255, 0)),
        "two-bit packed pixels must map through four CLUT entries");

    QVector<std::uint16_t> indexed4Mapping(16);
    for (int value = 0; value < indexed4Mapping.size(); ++value) indexed4Mapping[value] = static_cast<std::uint16_t>(value * 16);
    VideoFrame indexed4 { 2, 1, 1, PixelStorage::Indexed, 4, ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst, QByteArray(1, char(0x4c)), clut, indexed4Mapping, {} };
    image = cutemac::session::FramebufferRenderer::render(indexed4);
    ok &= expect(isColor(image, 0, qRgb(255, 0, 0)) && isColor(image, 1, qRgb(0, 255, 0)),
        "four-bit packed pixels must map through sixteen CLUT entries");

    VideoFrame indexed8 { 1, 1, 1, PixelStorage::Indexed, 8, ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst, QByteArray(1, char(0xff)), clut, {}, {} };
    image = cutemac::session::FramebufferRenderer::render(indexed8);
    ok &= expect(isColor(image, 0, qRgb(0, 0, 255)), "eight-bit pixels must address the full CLUT");

    VideoFrame rgb555 { 1, 1, 2, PixelStorage::Direct, 16, ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst, QByteArray::fromHex("7c00"), {}, {}, { 0x7c00, 0x03e0, 0x001f, 0 } };
    image = cutemac::session::FramebufferRenderer::render(rgb555);
    ok &= expect(isColor(image, 0, qRgb(255, 0, 0)), "big-endian RGB555 must render red");

    VideoFrame rgb565Little { 1, 1, 2, PixelStorage::Direct, 16, ByteOrder::LittleEndian,
        BitOrder::MostSignificantFirst, QByteArray::fromHex("e007"), {}, {}, { 0xf800, 0x07e0, 0x001f, 0 } };
    image = cutemac::session::FramebufferRenderer::render(rgb565Little);
    ok &= expect(isColor(image, 0, qRgb(0, 255, 0)), "little-endian RGB565 must honor byte order and masks");

    VideoFrame rgb888 { 1, 1, 3, PixelStorage::Direct, 24, ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst, QByteArray::fromHex("123456"), {}, {}, { 0xff0000, 0x00ff00, 0x0000ff, 0 } };
    image = cutemac::session::FramebufferRenderer::render(rgb888);
    ok &= expect(isColor(image, 0, qRgb(0x12, 0x34, 0x56)), "24-bit direct color must render channel masks");

    VideoFrame xrgb8888 { 1, 1, 4, PixelStorage::Direct, 32, ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst, QByteArray::fromHex("00789abc"), {}, {}, { 0x00ff0000, 0x0000ff00, 0x000000ff, 0 } };
    image = cutemac::session::FramebufferRenderer::render(xrgb8888);
    ok &= expect(isColor(image, 0, qRgb(0x78, 0x9a, 0xbc)), "32-bit direct color must render channel masks");

    VideoFrame incremental { 2, 2, 2, PixelStorage::Indexed, 8, ByteOrder::BigEndian,
        BitOrder::MostSignificantFirst, QByteArray::fromHex("004000c0"), clut, {}, {}, false,
        { { 0, 1, 2, 1 } } };
    QImage cached(2, 2, QImage::Format_RGB32);
    cached.fill(qRgb(1, 2, 3));
    ok &= expect(cutemac::session::FramebufferRenderer::update(cached, incremental),
        "incremental framebuffer update must accept a valid snapshot");
    ok &= expect(cached.pixel(0, 0) == qRgb(1, 2, 3) && cached.pixel(0, 1) == qRgb(255, 255, 255)
            && cached.pixel(1, 1) == qRgb(0, 255, 0),
        "incremental framebuffer update must preserve clean rows and convert dirty rows");

    return ok ? 0 : 1;
}
