#include "cutemac/devices/iwm/IwmController.h"

namespace cutemac::devices::iwm {

namespace {

constexpr qsizetype maxTraceEvents = 4096;

} // namespace

void IwmController::reset()
{
    m_lines.fill(false);
    m_mode = 0;
    m_status = 0;
    m_selectedDriveRegister = 0;
    m_stepTowardTrackZero = false;
    m_sideSelect = false;
    m_tach = false;
    m_dataReads = 0;
    m_statusReads = 0;
    m_handshakeReads = 0;
    m_dataWrites = 0;
    m_swimMode = false;
    m_swimTransition = 0;
    m_swimModeRegister = 0;
    m_swimSetup = 0;
    m_swimPhases = 0;
    m_swimParameters.fill(0);
    m_swimParameterIndex = 0;
    m_swimError = 0;
    m_internalDrive.setMotorOn(false);
    m_externalDrive.setMotorOn(false);
}

std::uint8_t IwmController::access(std::uint8_t registerIndex)
{
    return access(registerIndex, 0, false);
}

std::uint8_t IwmController::access(std::uint8_t registerIndex, std::uint8_t value, bool write)
{
    registerIndex &= 0x0f;
    if (m_swimMode) return swimAccess(registerIndex, value, write);

    if (write && registerIndex == 0x0f) {
        const bool expected = m_swimTransition == 1 ? (value & 0x40) == 0 : (value & 0x40) != 0;
        if (expected) {
            ++m_swimTransition;
            if (m_swimTransition == 4) {
                m_swimMode = true;
                m_swimModeRegister = 0x40;
                m_swimTransition = 0;
                appendTraceEvent(QStringLiteral("swim entered ISM mode"));
                return 0xff;
            }
        } else {
            m_swimTransition = 0;
        }
    } else if (m_swimTransition != 0) {
        m_swimTransition = 0;
    }
    const auto line = static_cast<std::uint8_t>(registerIndex >> 1);
    setLine(line, (registerIndex & 1) != 0);

    if (write && q6() && q7()) {
        writeRegister(value);
    }

    return readRegister();
}

std::uint8_t IwmController::swimAccess(std::uint8_t registerIndex, std::uint8_t value, bool write)
{
    const auto reg = static_cast<std::uint8_t>(registerIndex & 7);
    if (write) {
        switch (reg) {
        case 0:
        case 1:
            ++m_dataWrites;
            break;
        case 2:
            break;
        case 3:
            m_swimParameters[m_swimParameterIndex] = value;
            m_swimParameterIndex = static_cast<std::uint8_t>((m_swimParameterIndex + 1) & 15);
            break;
        case 4:
            m_swimPhases = value;
            for (std::uint8_t line = 0; line < 4; ++line) setLine(line, (value & (1U << line)) != 0);
            break;
        case 5:
            m_swimSetup = value;
            break;
        case 6:
            m_swimModeRegister = static_cast<std::uint8_t>(m_swimModeRegister & ~value);
            m_swimParameterIndex = 0;
            if ((m_swimModeRegister & 0x40) == 0) m_swimMode = false;
            break;
        case 7:
            m_swimModeRegister = static_cast<std::uint8_t>(m_swimModeRegister | value);
            break;
        }
        if (m_swimModeRegister & 0x01) m_swimError = 0;
        m_internalDrive.setMotorOn((m_swimModeRegister & 0x80) != 0);
        return 0xff;
    }

    switch (reg) {
    case 0:
    case 1:
        ++m_dataReads;
        return selectedDrive().nextNibble();
    case 2: {
        const auto error = m_swimError;
        m_swimError = 0;
        return error;
    }
    case 3: {
        const auto result = m_swimParameters[m_swimParameterIndex];
        m_swimParameterIndex = static_cast<std::uint8_t>((m_swimParameterIndex + 1) & 15);
        return result;
    }
    case 4: return m_swimPhases;
    case 5: return m_swimSetup;
    case 6: return m_swimModeRegister;
    case 7: {
        std::uint8_t handshake = 0xc0;
        if (!selectedDrive().inserted() || !selectedDrive().writable()) handshake |= 0x0c;
        if (m_swimError) handshake |= 0x20;
        return handshake;
    }
    default: return 0xff;
    }
}

bool IwmController::loadFloppyImage(const QString& path, bool readOnly)
{
    return m_internalDrive.load(path, readOnly);
}

void IwmController::ejectFloppyImage()
{
    m_internalDrive.eject();
}

void IwmController::setSideSelect(bool sideSelect)
{
    if (m_sideSelect == sideSelect) {
        return;
    }
    m_sideSelect = sideSelect;
    updateSelectedDriveRegister();
}

QString IwmController::floppyImagePath() const
{
    return m_internalDrive.path();
}

bool IwmController::floppyInserted() const
{
    return m_internalDrive.inserted();
}

IwmController::DebugState IwmController::debugState() const
{
    const auto floppy = selectedDrive().debugState();
    return {
        lineMask(),
        m_mode,
        m_status,
        m_selectedDriveRegister,
        floppy.motorOn,
        !m_lines[SelectDrive],
        floppy.inserted,
        floppy.doubleSided,
        floppy.writable,
        floppy.track,
        floppy.side,
        m_dataReads,
        m_statusReads,
        m_handshakeReads,
        m_dataWrites,
        floppy.imagePath,
        floppy.imageFormat,
        floppy.trackBytes,
        floppy.trackCursor,
    };
}

QByteArray IwmController::currentTrackBytesForDebug() const
{
    return selectedDrive().trackBytesForDebug(selectedDrive().currentTrack(), selectedDrive().currentSide());
}

QByteArray IwmController::trackBytesForDebug(int track, int side) const
{
    return selectedDrive().trackBytesForDebug(track, side);
}

void IwmController::setTraceEnabled(bool enabled)
{
    m_traceEnabled = enabled;
    m_internalDrive.setTraceEnabled(enabled);
    m_externalDrive.setTraceEnabled(enabled);
}

void IwmController::clearTrace()
{
    m_traceEvents.clear();
    m_internalDrive.clearTrace();
    m_externalDrive.clearTrace();
}

QStringList IwmController::traceEvents() const
{
    auto events = m_traceEvents;
    events.append(m_internalDrive.traceEvents());
    events.append(m_externalDrive.traceEvents());
    return events;
}

QByteArray IwmController::lastNibblesForDebug() const
{
    return selectedDrive().lastNibblesForDebug();
}

bool IwmController::q6() const
{
    return m_lines[Q6];
}

bool IwmController::q7() const
{
    return m_lines[Q7];
}

std::uint8_t IwmController::readRegister()
{
    const auto selector = static_cast<std::uint8_t>((q6() ? 0x02 : 0x00) | (q7() ? 0x01 : 0x00));
    switch (selector) {
    case 0x00:
        ++m_dataReads;
        if (m_traceEnabled && (m_dataReads <= 32 || (m_dataReads % 256) == 0)) {
            appendTraceEvent(QStringLiteral("iwm data-read count=%1 track=%2 side=%3 drive=%4")
                                 .arg(m_dataReads)
                                 .arg(selectedDrive().currentTrack())
                                 .arg(selectedDrive().currentSide())
                                 .arg(m_lines[SelectDrive] ? QStringLiteral("external") : QStringLiteral("internal")));
        }
        return selectedDrive().nextNibble();
    case 0x02: {
        ++m_statusReads;
        const auto modeBits = static_cast<std::uint8_t>((m_mode & 0x1f) | (m_status & 0x20));
        const auto sense = driveSenseHigh() ? 0x80 : 0x00;
        m_status = static_cast<std::uint8_t>(sense | modeBits);
        if (m_traceEnabled) {
            appendTraceEvent(QStringLiteral("iwm status-read reg=0x%1 value=0x%2 drive=%3")
                                 .arg(m_selectedDriveRegister, 2, 16, QLatin1Char('0'))
                                 .arg(m_status, 2, 16, QLatin1Char('0'))
                                 .arg(m_lines[SelectDrive] ? QStringLiteral("external") : QStringLiteral("internal")));
        }
        return m_status;
    }
    case 0x01:
        ++m_handshakeReads;
        return 0xc0;
    case 0x03:
        return m_mode;
    }
    return 0;
}

void IwmController::writeRegister(std::uint8_t value)
{
    if (!m_lines[Motor]) {
        m_mode = value;
        m_status = static_cast<std::uint8_t>((m_status & 0xc0) | (m_mode & 0x3f));
        return;
    }

    ++m_dataWrites;
    (void)value;
}

void IwmController::setLine(std::uint8_t line, bool on)
{
    const auto wasOn = m_lines[line];
    m_lines[line] = on;

    if (line == Motor) {
        if (on) {
            m_status = static_cast<std::uint8_t>(m_status | 0x20);
        } else {
            m_status = static_cast<std::uint8_t>(m_status & ~0x20);
        }
        appendTraceEvent(QStringLiteral("iwm enable=%1 drive=%2 motor=%3")
                             .arg(on ? QStringLiteral("on") : QStringLiteral("off"),
                                 m_lines[SelectDrive] ? QStringLiteral("external") : QStringLiteral("internal"),
                                 selectedDrive().motorOn() ? QStringLiteral("on") : QStringLiteral("off")));
    } else if (line == Ca0 || line == Ca1 || line == Ca2 || line == SelectDrive) {
        updateSelectedDriveRegister();
    } else if (line == Lstrb && wasOn && !on) {
        applyDriveStrobe();
    }
}

void IwmController::updateSelectedDriveRegister()
{
    m_selectedDriveRegister = selectedDriveRegister();
    selectedDrive().setCurrentSide(m_sideSelect ? 1 : 0);
    appendTraceEvent(QStringLiteral("iwm select reg=0x%1 drive=%2 side=%3")
                         .arg(m_selectedDriveRegister, 2, 16, QLatin1Char('0'))
                         .arg(m_lines[SelectDrive] ? QStringLiteral("external") : QStringLiteral("internal"))
                         .arg(m_sideSelect ? 1 : 0));
}

void IwmController::applyDriveStrobe()
{
    appendTraceEvent(QStringLiteral("iwm strobe reg=0x%1 drive=%2")
                         .arg(m_selectedDriveRegister, 2, 16, QLatin1Char('0'))
                         .arg(m_lines[SelectDrive] ? QStringLiteral("external") : QStringLiteral("internal")));
    switch (m_selectedDriveRegister) {
    case 0x00:
        m_stepTowardTrackZero = false;
        break;
    case 0x01:
        m_stepTowardTrackZero = true;
        break;
    case 0x04:
        selectedDrive().step(m_stepTowardTrackZero);
        break;
    case 0x08:
        selectedDrive().setMotorOn(true);
        break;
    case 0x09:
        selectedDrive().setMotorOn(false);
        break;
    case 0x0d:
        selectedDrive().eject();
        break;
    default:
        break;
    }
}

std::uint8_t IwmController::selectedDriveRegister() const
{
    return static_cast<std::uint8_t>((m_lines[Ca0] ? 0x04 : 0x00)
        | (m_lines[Ca1] ? 0x08 : 0x00)
        | (m_lines[Ca2] ? 0x01 : 0x00)
        | (m_sideSelect ? 0x02 : 0x00));
}

bool IwmController::driveSenseHigh()
{
    const auto& drive = selectedDrive();
    switch (m_selectedDriveRegister & 0x0f) {
    case 0x02:
        return !drive.inserted();
    case 0x04:
        return true;
    case 0x06:
        return drive.writable();
    case 0x08:
        return !drive.motorOn();
    case 0x09:
        return drive.doubleSided();
    case 0x0a:
        return !drive.trackZero();
    case 0x0b:
        return false;
    case 0x0c:
        return !drive.doubleSided();
    case 0x0d:
        return m_lines[SelectDrive];
    case 0x0e:
        m_tach = !m_tach;
        return m_tach;
    default:
        return true;
    }
}

floppy::FloppyDiskImage& IwmController::selectedDrive()
{
    return m_lines[SelectDrive] ? m_externalDrive : m_internalDrive;
}

const floppy::FloppyDiskImage& IwmController::selectedDrive() const
{
    return m_lines[SelectDrive] ? m_externalDrive : m_internalDrive;
}

void IwmController::appendTraceEvent(const QString& event)
{
    if (!m_traceEnabled) {
        return;
    }
    if (m_traceEvents.size() == maxTraceEvents) {
        m_traceEvents.removeFirst();
    }
    m_traceEvents.append(event);
}

std::uint8_t IwmController::lineMask() const
{
    std::uint8_t mask = 0;
    for (std::uint8_t i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i]) {
            mask = static_cast<std::uint8_t>(mask | (1U << i));
        }
    }
    return mask;
}

} // namespace cutemac::devices::iwm
