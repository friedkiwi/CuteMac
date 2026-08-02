#pragma once

#include <QString>

#include "cutemac/devices/printer/ImageWriterII.h"

namespace cutemac::storage {

class PngPageSink {
public:
    explicit PngPageSink(QString outputDirectory);
    [[nodiscard]] bool write(const devices::printer::RasterPage& page);
    [[nodiscard]] QString lastOutputPath() const { return m_lastOutputPath; }

private:
    QString m_outputDirectory;
    QString m_lastOutputPath;
};

} // namespace cutemac::storage
