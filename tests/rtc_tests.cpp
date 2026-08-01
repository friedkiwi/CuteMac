#include "cutemac/devices/rtc/MacRtc.h"

#include <QDateTime>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void begin(cutemac::devices::rtc::MacRtc& rtc)
{
    rtc.setPins(false, true, true);
    rtc.setPins(true, false, false);
}

void sendByte(cutemac::devices::rtc::MacRtc& rtc, std::uint8_t value)
{
    for (int bit = 7; bit >= 0; --bit) {
        const bool data = ((value >> bit) & 1U) != 0;
        rtc.setPins(true, false, data);
        rtc.setPins(true, true, data);
    }
}

void writeRegister(cutemac::devices::rtc::MacRtc& rtc, std::uint8_t command, std::uint8_t value)
{
    begin(rtc);
    sendByte(rtc, command);
    sendByte(rtc, value);
    rtc.setPins(false, true, true);
}

std::uint8_t receiveByte(cutemac::devices::rtc::MacRtc& rtc)
{
    std::uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        rtc.setPins(true, false, false);
        rtc.setPins(true, true, false);
        value = static_cast<std::uint8_t>((value << 1) | (rtc.dataLine() ? 1 : 0));
    }
    return value;
}

std::uint8_t readRegister(cutemac::devices::rtc::MacRtc& rtc, std::uint8_t command)
{
    begin(rtc);
    sendByte(rtc, command);
    const auto value = receiveByte(rtc);
    rtc.setPins(false, true, true);
    return value;
}

} // namespace

int main()
{
    cutemac::devices::rtc::MacRtc rtc;
    constexpr std::uint32_t epochDelta = 2082844800U;
    const auto readTime = [&rtc]() {
        return static_cast<std::uint32_t>(readRegister(rtc, 0x81))
            | (static_cast<std::uint32_t>(readRegister(rtc, 0x85)) << 8)
            | (static_cast<std::uint32_t>(readRegister(rtc, 0x89)) << 16)
            | (static_cast<std::uint32_t>(readRegister(rtc, 0x8d)) << 24);
    };
    const auto now = QDateTime::currentDateTime();
    const auto expected = static_cast<std::uint32_t>(now.toSecsSinceEpoch() + now.offsetFromUtc()) + epochDelta;
    require(readTime() >= expected && readTime() <= expected + 1, "RTC should expose current host time in the Macintosh epoch");
    writeRegister(rtc, 0x01, 0);
    writeRegister(rtc, 0x05, 0);
    writeRegister(rtc, 0x09, 0);
    writeRegister(rtc, 0x0d, 0);
    require(readTime() >= expected, "writes to RTC time registers should be discarded");
    require(readRegister(rtc, 0xc1) == 0xa8, "RTC PRAM should contain the valid signature");

    QTemporaryDir directory;
    const auto nvramPath = directory.filePath(QStringLiteral("mac-plus.nvram"));
    require(rtc.setNvramImagePath(nvramPath), "a new NVRAM image should be created");
    writeRegister(rtc, 0x41, 0x5a);
    require(QFileInfo(nvramPath).size() == 256, "NVRAM image should contain all 256 PRAM bytes");
    cutemac::devices::rtc::MacRtc restored;
    require(restored.setNvramImagePath(nvramPath), "an existing NVRAM image should load");
    require(readRegister(restored, 0xc1) == 0x5a, "PRAM writes should persist in the configured NVRAM image");
    return 0;
}
