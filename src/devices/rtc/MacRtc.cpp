#include "cutemac/devices/rtc/MacRtc.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace cutemac::devices::rtc {
namespace {

constexpr std::uint32_t macToUnixEpochSeconds = 2082844800U;
constexpr int group1Base = 0x10;
constexpr int group2Base = 0x08;

} // namespace

MacRtc::MacRtc()
{
    m_parameterRam.fill(0);
    m_parameterRam[group1Base] = 0xa8;
    m_parameterRam[group1Base + 3] = 0x22;
    m_parameterRam[group1Base + 4] = 0xcc;
    m_parameterRam[group1Base + 5] = 0x0a;
    m_parameterRam[group1Base + 6] = 0xcc;
    m_parameterRam[group1Base + 7] = 0x0a;
    m_parameterRam[group2Base + 2] = 4;
    resetSerial();
}

void MacRtc::resetSerial()
{
    m_shift = 0;
    m_bits = 0;
    m_mode = 0;
    m_enabled = false;
    m_clockHigh = false;
    m_outputActive = false;
    m_dataLine = true;
}

void MacRtc::setPins(bool enabled, bool clockHigh, bool dataHigh)
{
    if (!enabled) {
        if (m_enabled) resetSerial();
        m_clockHigh = clockHigh;
        return;
    }
    if (!m_enabled) {
        m_enabled = true;
        m_clockHigh = clockHigh;
        m_shift = 0;
        m_bits = 0;
        m_mode = 0;
        m_outputActive = false;
    }
    if (!m_clockHigh && clockHigh) {
        if (m_outputActive) {
            m_dataLine = ((m_shift >> (7 - m_bits)) & 1U) != 0;
            if (++m_bits == 8) {
                m_bits = 0;
                m_outputActive = false;
            }
        } else {
            m_shift = static_cast<std::uint8_t>((m_shift << 1) | (dataHigh ? 1 : 0));
            if (++m_bits == 8) {
                m_bits = 0;
                receiveByte(m_shift);
            }
        }
    }
    m_clockHigh = clockHigh;
}

bool MacRtc::dataLine() const { return m_dataLine; }

bool MacRtc::setNvramImagePath(const QString& path)
{
    if (path.isEmpty()) {
        m_nvramImagePath.clear();
        return true;
    }
    QFile file(path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly) || file.size() != static_cast<qint64>(m_parameterRam.size())) return false;
        const auto bytes = file.readAll();
        for (std::size_t index = 0; index < m_parameterRam.size(); ++index)
            m_parameterRam[index] = static_cast<std::uint8_t>(bytes[static_cast<qsizetype>(index)]);
    }
    m_nvramImagePath = QFileInfo(path).absoluteFilePath();
    if (!QDir().mkpath(QFileInfo(m_nvramImagePath).absolutePath())) return false;
    return file.exists() || saveNvram();
}

QString MacRtc::nvramImagePath() const { return m_nvramImagePath; }

void MacRtc::receiveByte(std::uint8_t value)
{
    if (m_mode == 0) {
        if ((value & 0x78) == 0x38) {
            m_command = value;
            m_mode = 2;
        } else if ((value & 0x80) != 0) {
            m_shift = accessRegister(value, 0, false);
            m_outputActive = true;
        } else {
            m_command = value;
            m_mode = 1;
        }
        return;
    }
    if (m_mode == 1) {
        (void)accessRegister(m_command, value, true);
        m_mode = 0;
        return;
    }
    if (m_mode == 2) {
        m_sector = static_cast<std::uint8_t>(((m_command & 0x07) << 5) | ((value & 0x7c) >> 2));
        if ((m_command & 0x80) != 0) {
            m_shift = m_parameterRam[m_sector];
            m_outputActive = true;
            m_mode = 0;
        } else {
            m_mode = 3;
        }
        return;
    }
    if (!m_writeProtected) {
        m_parameterRam[m_sector] = value;
        (void)saveNvram();
    }
    m_mode = 0;
}

std::uint8_t MacRtc::accessRegister(std::uint8_t command, std::uint8_t value, bool write)
{
    const auto reg = static_cast<std::uint8_t>((command & 0x7c) >> 2);
    if (reg < 8) {
        const int byte = reg & 3;
        if (write) return value;
        return static_cast<std::uint8_t>(currentSeconds() >> (byte * 8));
    }
    if (reg < 12) {
        const int address = group2Base + (reg & 3);
        if (write && !m_writeProtected) {
            m_parameterRam[address] = value;
            (void)saveNvram();
        }
        return m_parameterRam[address];
    }
    if (reg < 16) {
        if (write && reg == 13) m_writeProtected = (value & 0x80) != 0;
        return 0;
    }
    const int address = group1Base + (reg & 0x0f);
    if (write && !m_writeProtected) {
        m_parameterRam[address] = value;
        (void)saveNvram();
    }
    return m_parameterRam[address];
}

std::uint32_t MacRtc::currentSeconds() const
{
    const auto now = QDateTime::currentDateTime();
    return static_cast<std::uint32_t>(now.toSecsSinceEpoch() + now.offsetFromUtc()) + macToUnixEpochSeconds;
}

bool MacRtc::saveNvram() const
{
    if (m_nvramImagePath.isEmpty()) return true;
    QSaveFile file(m_nvramImagePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto* data = reinterpret_cast<const char*>(m_parameterRam.data());
    return file.write(data, static_cast<qint64>(m_parameterRam.size())) == static_cast<qint64>(m_parameterRam.size()) && file.commit();
}

} // namespace cutemac::devices::rtc
