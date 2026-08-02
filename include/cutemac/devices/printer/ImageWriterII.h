#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <QByteArray>

#include "cutemac/devices/serial/SerialEndpoint.h"

namespace cutemac::devices::printer {

struct RasterPage {
    int width = 0;
    int height = 0;
    int dotsPerInch = 144;
    QByteArray pixels; // one byte per pixel: 0 white, 1 black
};

class ImageWriterII final : public serial::SerialEndpoint {
public:
    using PageSink = std::function<void(const RasterPage&)>;

    explicit ImageWriterII(PageSink sink);
    ~ImageWriterII() override;
    void reset() override;
    void receiveByte(std::uint8_t value) override;
    void ejectPage();
    [[nodiscard]] bool pageDirty() const { return m_dirty; }

private:
    enum class ParserState { Text, Escape, Parameter, GraphicsCount, GraphicsData };
    void clearPage();
    void putDot(int x, int y);
    void putCharacter(std::uint8_t value);
    void finishGraphicsCount();

    PageSink m_sink;
    RasterPage m_page;
    ParserState m_state = ParserState::Text;
    char m_graphicsCommand = 0;
    char m_parameterCommand = 0;
    int m_parameterLength = 0;
    QByteArray m_count;
    int m_graphicsRemaining = 0;
    int m_x = 0;
    int m_y = 0;
    int m_graphicsX = 0;
    int m_lineHeight = 24;
    int m_horizontalScale = 1;
    bool m_dirty = false;
};

} // namespace cutemac::devices::printer
