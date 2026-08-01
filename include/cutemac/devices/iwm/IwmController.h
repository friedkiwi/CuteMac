#pragma once

#include <array>
#include <cstdint>

#include <QString>

#include "cutemac/devices/floppy/FloppyDiskImage.h"

namespace cutemac::devices::iwm {

class IwmController {
public:
    struct DebugState {
        std::uint8_t lines = 0;
        std::uint8_t mode = 0;
        std::uint8_t status = 0;
        std::uint8_t selectedRegister = 0;
        bool motorOn = false;
        bool internalSelected = true;
        bool diskInserted = false;
        bool doubleSided = false;
        bool writable = false;
        int track = 0;
        int side = 0;
        std::uint64_t dataReads = 0;
        std::uint64_t statusReads = 0;
        std::uint64_t handshakeReads = 0;
        std::uint64_t dataWrites = 0;
        QString imagePath;
        QString imageFormat;
    };

    void reset();

    [[nodiscard]] std::uint8_t access(std::uint8_t registerIndex);
    [[nodiscard]] std::uint8_t access(std::uint8_t registerIndex, std::uint8_t value, bool write);

    [[nodiscard]] bool loadFloppyImage(const QString& path);
    void ejectFloppyImage();
    void setSideSelect(bool sideSelect);
    [[nodiscard]] QString floppyImagePath() const;
    [[nodiscard]] bool floppyInserted() const;
    [[nodiscard]] DebugState debugState() const;

    [[nodiscard]] bool q6() const;
    [[nodiscard]] bool q7() const;

private:
    enum Line : std::uint8_t {
        Ca0 = 0,
        Ca1 = 1,
        Ca2 = 2,
        Lstrb = 3,
        Motor = 4,
        SelectDrive = 5,
        Q6 = 6,
        Q7 = 7,
    };

    [[nodiscard]] std::uint8_t readRegister();
    void writeRegister(std::uint8_t value);
    void setLine(std::uint8_t line, bool on);
    void updateSelectedDriveRegister();
    void applyDriveStrobe();
    [[nodiscard]] std::uint8_t selectedDriveRegister() const;
    [[nodiscard]] bool driveSenseHigh();
    [[nodiscard]] std::uint8_t lineMask() const;

    std::array<bool, 8> m_lines {};
    floppy::FloppyDiskImage m_internalDrive;
    std::uint8_t m_mode = 0;
    std::uint8_t m_status = 0;
    std::uint8_t m_selectedDriveRegister = 0;
    bool m_stepTowardTrackZero = false;
    bool m_sideSelect = false;
    bool m_tach = false;
    std::uint64_t m_dataReads = 0;
    std::uint64_t m_statusReads = 0;
    std::uint64_t m_handshakeReads = 0;
    std::uint64_t m_dataWrites = 0;
};

} // namespace cutemac::devices::iwm
