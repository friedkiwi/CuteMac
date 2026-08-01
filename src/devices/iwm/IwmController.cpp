#include "cutemac/devices/iwm/IwmController.h"

namespace cutemac::devices::iwm {

void IwmController::reset()
{
    m_lines.fill(false);
}

std::uint8_t IwmController::access(std::uint8_t registerIndex)
{
    registerIndex &= 0x0f;
    const auto line = static_cast<std::uint8_t>(registerIndex >> 1);
    m_lines[line] = (registerIndex & 1) != 0;

    if (!q7() && q6()) {
        const auto caLines = static_cast<std::uint8_t>((m_lines[2] ? 0x04 : 0) | (m_lines[1] ? 0x02 : 0) | (m_lines[0] ? 0x01 : 0));
        const auto senseHigh = m_diskSenseProvider ? m_diskSenseProvider(caLines) : true;
        return static_cast<std::uint8_t>((senseHigh ? 0x80 : 0x00) | 0x1f);
    }

    if (!q7()) {
        return 0x80;
    }

    return 0;
}

void IwmController::setDiskSenseProvider(DiskSenseProvider provider)
{
    m_diskSenseProvider = std::move(provider);
}

bool IwmController::q6() const
{
    return m_lines[6];
}

bool IwmController::q7() const
{
    return m_lines[7];
}

} // namespace cutemac::devices::iwm
