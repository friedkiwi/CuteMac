#include "cutemac/devices/printer/ImageWriterII.h"

#include <algorithm>
#include <array>
#include <QString>

namespace cutemac::devices::printer {
namespace {
// Compact deterministic glyphs are a fallback for text-mode printer output.
std::array<std::uint8_t, 5> glyph(std::uint8_t c)
{
    if (c == ' ') return {};
    // A legible diagnostic glyph derived from the character bits. Macintosh
    // printing normally reaches the graphics path, where pixels are exact.
    return { static_cast<std::uint8_t>(0x41 | ((c & 1) << 2)),
        static_cast<std::uint8_t>(0x22 | ((c & 2) << 3)),
        static_cast<std::uint8_t>(0x14 | ((c & 4) << 2)),
        static_cast<std::uint8_t>(0x22 | ((c & 8) << 1)), 0x41 };
}
}

ImageWriterII::ImageWriterII(PageSink sink) : m_sink(std::move(sink)) { clearPage(); }
ImageWriterII::~ImageWriterII() { ejectPage(); }

void ImageWriterII::clearPage()
{
    m_page = {1224, 1584, 144, QByteArray(1224 * 1584, 0)};
    m_x = 0; m_y = 0; m_graphicsX = 0; m_dirty = false;
}

void ImageWriterII::reset()
{
    ejectPage();
    m_state = ParserState::Text; m_count.clear(); m_graphicsRemaining = 0;
    m_lineHeight = 24; m_horizontalScale = 1; m_x = 0;
}

void ImageWriterII::putDot(int x, int y)
{
    if (x < 0 || y < 0 || x >= m_page.width || y >= m_page.height) return;
    m_page.pixels[y * m_page.width + x] = 1;
    m_dirty = true;
}

void ImageWriterII::putCharacter(std::uint8_t value)
{
    const auto bits = glyph(value);
    for (int column = 0; column < 5; ++column)
        for (int row = 0; row < 7; ++row)
            if (bits[column] & (1U << row)) putDot(m_x + column * 2, m_y + row * 2);
    m_x += 12;
}

void ImageWriterII::finishGraphicsCount()
{
    bool ok = false;
    const int count = QString::fromLatin1(m_count).trimmed().toInt(&ok);
    m_graphicsRemaining = ok ? count * (m_graphicsCommand == 'g' ? 8 : 1) : 0;
    m_graphicsX = m_x;
    m_state = m_graphicsRemaining ? ParserState::GraphicsData : ParserState::Text;
}

void ImageWriterII::receiveByte(std::uint8_t value)
{
    if (m_state == ParserState::GraphicsData) {
        for (int bit = 0; bit < 8; ++bit)
            if (value & (1U << bit)) putDot(m_graphicsX, m_y + bit * 2);
        m_graphicsX += m_horizontalScale;
        if (--m_graphicsRemaining == 0) { m_x = m_graphicsX; m_state = ParserState::Text; }
        return;
    }
    if (m_state == ParserState::GraphicsCount) {
        m_count.append(static_cast<char>(value));
        const int digits = m_graphicsCommand == 'g' ? 3 : 4;
        if (m_count.size() == digits) finishGraphicsCount();
        return;
    }
    if (m_state == ParserState::Parameter) {
        m_count.append(static_cast<char>(value));
        if (m_count.size() == m_parameterLength) {
            bool ok = false;
            const int parameter = QString::fromLatin1(m_count).trimmed().toInt(&ok);
            if (ok && m_parameterCommand == 'T') m_lineHeight = std::max(1, parameter);
            else if (ok && m_parameterCommand == 'F') m_x = std::max(0, parameter);
            // Page length and margins are accepted; fixed Letter-sized PNGs
            // deliberately retain their physical canvas.
            m_state = ParserState::Text;
        }
        return;
    }
    if (m_state == ParserState::Escape) {
        m_state = ParserState::Text;
        if (value == 'G' || value == 'S' || value == 'g') {
            m_graphicsCommand = static_cast<char>(value); m_count.clear(); m_state = ParserState::GraphicsCount;
        } else if (value == 'T' || value == 'F' || value == 'H' || value == 'L') {
            m_parameterCommand = static_cast<char>(value);
            m_parameterLength = value == 'T' ? 2 : (value == 'L' ? 3 : 4);
            m_count.clear(); m_state = ParserState::Parameter;
        } else if (value == 'p') m_horizontalScale = 1; // 144 dpi pica
        else if (value == 'P') m_horizontalScale = 1;  // 160 dpi, approximated at page DPI
        else if (value == 'N') m_horizontalScale = 2;  // 80 dpi
        return;
    }
    switch (value) {
    case 0x1b: m_state = ParserState::Escape; break;
    case 0x0c: ejectPage(); break;
    case '\r': m_x = 0; break;
    case '\n': m_y += m_lineHeight; if (m_y + 16 >= m_page.height) ejectPage(); break;
    case '\t': m_x = ((m_x / 96) + 1) * 96; break;
    default: if (value >= 0x20 && value != 0x7f) putCharacter(value); break;
    }
}

void ImageWriterII::ejectPage()
{
    if (m_dirty && m_sink) m_sink(m_page);
    clearPage();
}

} // namespace cutemac::devices::printer
