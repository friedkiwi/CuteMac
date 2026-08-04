#pragma once

#include <array>
#include <cstdint>

#include <QByteArray>
#include <QString>
#include <QStringList>

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
        bool highDensity = false;
        bool writable = false;
        int track = 0;
        int side = 0;
        std::uint64_t dataReads = 0;
        std::uint64_t statusReads = 0;
        std::uint64_t handshakeReads = 0;
        std::uint64_t dataWrites = 0;
        QString imagePath;
        QString imageFormat;
        qsizetype trackBytes = 0;
        qsizetype trackCursor = 0;
    };

    void reset();

    [[nodiscard]] std::uint8_t access(std::uint8_t registerIndex);
    [[nodiscard]] std::uint8_t access(std::uint8_t registerIndex, std::uint8_t value, bool write);

    [[nodiscard]] bool loadFloppyImage(const QString& path, bool readOnly = false);
    [[nodiscard]] bool loadFloppyImage(int drive, const QString& path, bool readOnly = false);
    void ejectFloppyImage();
    void ejectFloppyImage(int drive);
    void setSideSelect(bool sideSelect);
    [[nodiscard]] QString floppyImagePath() const;
    [[nodiscard]] QString floppyImagePath(int drive) const;
    [[nodiscard]] bool floppyInserted() const;
    [[nodiscard]] bool floppyInserted(int drive) const;
    [[nodiscard]] std::uint64_t activityCounter() const;
    [[nodiscard]] DebugState debugState() const;
    [[nodiscard]] DebugState debugState(int drive) const;
    [[nodiscard]] QByteArray currentTrackBytesForDebug() const;
    [[nodiscard]] QByteArray trackBytesForDebug(int track, int side) const;
    void setTraceEnabled(bool enabled);
    void clearTrace();
    [[nodiscard]] QStringList traceEvents() const;
    [[nodiscard]] QByteArray lastNibblesForDebug() const;

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
    void applySwimPhases(std::uint8_t phases);
    [[nodiscard]] std::uint8_t selectedDriveRegister() const;
    [[nodiscard]] bool driveSenseHigh();
    [[nodiscard]] std::uint8_t lineMask() const;
    [[nodiscard]] std::uint8_t swimAccess(std::uint8_t registerIndex, std::uint8_t value, bool write);
    [[nodiscard]] floppy::FloppyDiskImage& selectedDrive();
    [[nodiscard]] const floppy::FloppyDiskImage& selectedDrive() const;
    [[nodiscard]] floppy::FloppyDiskImage* driveByIndex(int drive);
    [[nodiscard]] const floppy::FloppyDiskImage* driveByIndex(int drive) const;
    void appendTraceEvent(const QString& event);

    std::array<bool, 8> m_lines {};
    floppy::FloppyDiskImage m_internalDrive;
    floppy::FloppyDiskImage m_externalDrive;
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
    bool m_traceEnabled = false;
    QStringList m_traceEvents;
    bool m_swimMode = false;
    std::uint8_t m_swimTransition = 0;
    std::uint8_t m_swimModeRegister = 0;
    std::uint8_t m_swimSetup = 0;
    std::uint8_t m_swimPhases = 0;
    std::array<std::uint8_t, 16> m_swimParameters {};
    std::uint8_t m_swimParameterIndex = 0;
    std::uint8_t m_swimError = 0;
    bool m_swimPhaseStrobe = false;
    int m_swimTraceBytesRemaining = 0;
};

} // namespace cutemac::devices::iwm
