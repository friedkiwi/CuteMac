#include "cutemac/storage/PngPageSink.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>

namespace cutemac::storage {

PngPageSink::PngPageSink(QString outputDirectory) : m_outputDirectory(std::move(outputDirectory)) {}

bool PngPageSink::write(const devices::printer::RasterPage& page)
{
    QDir directory(m_outputDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) return false;
    int sequence = 1;
    QString path;
    do {
        path = directory.filePath(QStringLiteral("ImageWriter-%1.png").arg(sequence++, 6, 10, QLatin1Char('0')));
    } while (QFileInfo::exists(path));

    QImage image(page.width, page.height, QImage::Format_Grayscale8);
    image.setDotsPerMeterX(qRound(page.dotsPerInch / 0.0254));
    image.setDotsPerMeterY(qRound(page.dotsPerInch / 0.0254));
    for (int y = 0; y < page.height; ++y) {
        auto* output = image.scanLine(y);
        const auto* input = reinterpret_cast<const unsigned char*>(page.pixels.constData() + y * page.width);
        for (int x = 0; x < page.width; ++x) output[x] = input[x] ? 0 : 255;
    }
    if (!image.save(path, "PNG")) return false;
    m_lastOutputPath = path;
    return true;
}

} // namespace cutemac::storage
