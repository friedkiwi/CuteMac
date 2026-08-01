#pragma once

#include <array>
#include <cstdint>
#include <QString>

namespace cutemac::devices::rtc {

class MacRtc {
public:
    MacRtc();

    void resetSerial();
    void setPins(bool enabled, bool clockHigh, bool dataHigh);
    [[nodiscard]] bool dataLine() const;
    [[nodiscard]] bool setNvramImagePath(const QString& path);
    [[nodiscard]] QString nvramImagePath() const;

private:
    void receiveByte(std::uint8_t value);
    [[nodiscard]] std::uint8_t accessRegister(std::uint8_t command, std::uint8_t value, bool write);
    [[nodiscard]] std::uint32_t currentSeconds() const;
    [[nodiscard]] bool saveNvram() const;

    std::array<std::uint8_t, 256> m_parameterRam {};
    std::uint8_t m_shift = 0;
    std::uint8_t m_command = 0;
    std::uint8_t m_sector = 0;
    int m_bits = 0;
    int m_mode = 0;
    bool m_writeProtected = false;
    bool m_enabled = false;
    bool m_clockHigh = false;
    bool m_outputActive = false;
    bool m_dataLine = true;
    QString m_nvramImagePath;
};

} // namespace cutemac::devices::rtc
