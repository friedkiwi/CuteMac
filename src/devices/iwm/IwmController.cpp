#include "cutemac/devices/iwm/IwmController.h"

#include <algorithm>

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
    m_swimPhaseStrobe = false;
    m_swimTraceBytesRemaining = 0;
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

    // SWIM mode entry is an IWM mode-register operation.  Do not interpret
    // arbitrary encoded disk bytes containing the same bit pattern as the
    // 1,0,1,1 unlock sequence while the drive is running.
    if (write && registerIndex == 0x0f && !m_lines[Motor]) {
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
    const auto previousMode = m_swimModeRegister;
    if (m_traceEnabled && write && (reg == 5 || reg == 6 || reg == 7)) {
        appendTraceEvent(QStringLiteral("swim %1 reg=%2 value=0x%3 mode=0x%4")
                             .arg(write ? QStringLiteral("write") : QStringLiteral("read"))
                             .arg(reg)
                             .arg(value, 2, 16, QLatin1Char('0'))
                             .arg(m_swimModeRegister, 2, 16, QLatin1Char('0')));
    }
    if (write) {
        switch (reg) {
        case 0:
        case 1:
            ++m_dataWrites;
            if ((m_swimModeRegister & 0x18) == 0x10
                && !selectedDrive().writeDiskByte(value, reg == 1)) {
                m_swimError = static_cast<std::uint8_t>(m_swimError | 0x04);
            }
            break;
        case 2:
            break;
        case 3:
            m_swimParameters[m_swimParameterIndex] = value;
            m_swimParameterIndex = static_cast<std::uint8_t>((m_swimParameterIndex + 1) & 15);
            break;
        case 4:
            m_swimPhases = value;
            applySwimPhases(value);
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
        if ((previousMode & 0x18) == 0x10 && (m_swimModeRegister & 0x18) != 0x10
            && !selectedDrive().flushWrites()) {
            m_swimError = static_cast<std::uint8_t>(m_swimError | 0x04);
        }
        m_internalDrive.setMotorOn((m_swimModeRegister & 0x82) == 0x82);
        m_externalDrive.setMotorOn((m_swimModeRegister & 0x84) == 0x84);
        if ((previousMode & 0x08) == 0 && (m_swimModeRegister & 0x18) == 0x08) {
            // On a read ACTION the SWIM synchronizer consumes gap bytes and
            // presents the first illegal-clock mark byte to the FIFO.
            auto& drive = selectedDrive();
            const auto bytesOnTrack = std::max<qsizetype>(
                drive.trackBytesForDebug(drive.currentTrack(), drive.currentSide()).size(), 1);
            for (qsizetype scanned = 0; scanned < bytesOnTrack && !drive.peekDiskByte().mark; ++scanned) {
                (void)drive.nextDiskByte();
            }
            if (m_traceEnabled) {
                m_swimTraceBytesRemaining = 12;
                appendTraceEvent(QStringLiteral("swim read-action track=%1 side=%2 cursor=%3")
                                     .arg(drive.currentTrack())
                                     .arg(drive.currentSide())
                                     .arg(drive.debugState().trackCursor));
            }
        }
        return 0xff;
    }

    switch (reg) {
    case 0: {
        ++m_dataReads;
        const auto diskByte = selectedDrive().nextDiskByte();
        if (m_traceEnabled && m_swimTraceBytesRemaining > 0) {
            --m_swimTraceBytesRemaining;
            appendTraceEvent(QStringLiteral("swim data value=0x%1 mark=%2")
                                 .arg(diskByte.value, 2, 16, QLatin1Char('0'))
                                 .arg(diskByte.mark ? 1 : 0));
        }
        if (diskByte.mark) m_swimError = static_cast<std::uint8_t>(m_swimError | 0x02);
        return diskByte.value;
    }
    case 1:
        ++m_dataReads;
        {
            const auto diskByte = selectedDrive().nextDiskByte();
            if (m_traceEnabled && m_swimTraceBytesRemaining > 0) {
                --m_swimTraceBytesRemaining;
                appendTraceEvent(QStringLiteral("swim mark value=0x%1 mark=%2")
                                     .arg(diskByte.value, 2, 16, QLatin1Char('0'))
                                     .arg(diskByte.mark ? 1 : 0));
            }
            return diskByte.value;
        }
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
        ++m_handshakeReads;
        std::uint8_t handshake = 0;
        if ((m_swimModeRegister & 0x08) != 0 && selectedDrive().inserted() && selectedDrive().motorOn()) {
            const auto diskByte = selectedDrive().peekDiskByte();
            handshake |= 0xc0;
            if (diskByte.mark) handshake |= 0x01;
        }
        // In Apple drives SENSE is multiplexed by the phase lines. RDDATA is
        // tied to the same input while the drive is being interrogated, so
        // both handshake bits reflect the selected drive-status register.
        if (driveSenseHigh()) handshake |= 0x0c;
        if (selectedDrive().motorOn()) handshake |= 0x10;
        if (m_swimError) handshake |= 0x20;
        return handshake;
    }
    default: return 0xff;
    }
}

bool IwmController::loadFloppyImage(const QString& path, bool readOnly)
{
    return loadFloppyImage(0, path, readOnly);
}

bool IwmController::loadFloppyImage(int drive, const QString& path, bool readOnly)
{
    auto* floppy = driveByIndex(drive);
    if (floppy == nullptr) return false;
    (void)floppy->flushWrites();
    return floppy->load(path, readOnly);
}

void IwmController::ejectFloppyImage()
{
    ejectFloppyImage(0);
}

void IwmController::ejectFloppyImage(int drive)
{
    auto* floppy = driveByIndex(drive);
    if (floppy != nullptr) floppy->eject();
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
    return floppyImagePath(0);
}

QString IwmController::floppyImagePath(int drive) const
{
    const auto* floppy = driveByIndex(drive);
    return floppy == nullptr ? QString() : floppy->path();
}

bool IwmController::floppyInserted() const
{
    return floppyInserted(0);
}

bool IwmController::floppyInserted(int drive) const
{
    const auto* floppy = driveByIndex(drive);
    return floppy != nullptr && floppy->inserted();
}

std::uint64_t IwmController::activityCounter() const
{
    return m_dataReads + m_dataWrites;
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
        floppy.highDensity,
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

IwmController::DebugState IwmController::debugState(int drive) const
{
    const auto* selected = driveByIndex(drive);
    if (selected == nullptr) return {};
    const auto floppy = selected->debugState();
    return {
        lineMask(),
        m_mode,
        m_status,
        m_selectedDriveRegister,
        floppy.motorOn,
        drive == 0,
        floppy.inserted,
        floppy.doubleSided,
        floppy.highDensity,
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
    if (!selectedDrive().writeDiskByte(value) && m_traceEnabled)
        appendTraceEvent(QStringLiteral("iwm rejected write track=%1 side=%2")
                             .arg(selectedDrive().currentTrack()).arg(selectedDrive().currentSide()));
}

void IwmController::setLine(std::uint8_t line, bool on)
{
    const auto wasOn = m_lines[line];
    if (line == Q7 && wasOn && !on) (void)selectedDrive().flushWrites();
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
        (void)selectedDrive().flushWrites();
        selectedDrive().step(m_stepTowardTrackZero);
        break;
    case 0x08:
        selectedDrive().setMotorOn(true);
        break;
    case 0x09:
        (void)selectedDrive().flushWrites();
        selectedDrive().setMotorOn(false);
        break;
    case 0x0d:
        ejectSelectedDrive();
        break;
    default:
        break;
    }
}

void IwmController::applySwimPhases(std::uint8_t phases)
{
    const auto strobe = (phases & 0x08) != 0;
    m_selectedDriveRegister = static_cast<std::uint8_t>((phases & 0x07) | (m_sideSelect ? 0x08 : 0x00));
    selectedDrive().setCurrentSide(m_sideSelect ? 1 : 0);
    if (strobe && !m_swimPhaseStrobe) {
        appendTraceEvent(QStringLiteral("swim strobe phases=0x%1 ss=%2 reg=0x%3 drive=%4")
                             .arg(phases, 2, 16, QLatin1Char('0'))
                             .arg(m_sideSelect ? 1 : 0)
                             .arg(m_selectedDriveRegister, 2, 16, QLatin1Char('0'))
                             .arg(selectedDriveIndex()));
        switch (m_selectedDriveRegister) {
        case 0x00:
            m_stepTowardTrackZero = false;
            break;
        case 0x01:
            (void)selectedDrive().flushWrites();
            selectedDrive().step(m_stepTowardTrackZero);
            break;
        case 0x02:
            selectedDrive().setMotorOn(true);
            break;
        case 0x04:
            m_stepTowardTrackZero = true;
            break;
        case 0x06:
            (void)selectedDrive().flushWrites();
            selectedDrive().setMotorOn(false);
            break;
        case 0x07:
            ejectSelectedDrive();
            break;
        case 0x09: // select MFM mode
            m_swimSetup = static_cast<std::uint8_t>(m_swimSetup & ~0x04);
            break;
        case 0x0c: // clear disk-change latch
            selectedDrive().clearMediaChanged();
            break;
        case 0x0d: // select GCR mode
            m_swimSetup = static_cast<std::uint8_t>(m_swimSetup | 0x04);
            break;
        default:
            break;
        }
    }
    m_swimPhaseStrobe = strobe;
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
    if (m_swimMode) {
        switch (m_selectedDriveRegister & 0x0f) {
        case 0x00: return m_stepTowardTrackZero;
        case 0x01: return true;
        case 0x02: return !drive.motorOn();
        case 0x03: return !drive.inserted();
        case 0x04:
            return true; // index is not timing-sensitive in the block-image model
        case 0x05: return true; // the IIcx has a SuperDrive
        case 0x06: return drive.doubleSided();
        case 0x07: return false; // active-low drive-present indication
        case 0x08: return !drive.inserted();
        case 0x09: return drive.writable();
        case 0x0a: return !drive.trackZero();
        case 0x0b:
            m_tach = !m_tach;
            return m_tach;
        case 0x0c: return drive.mediaChanged();
        case 0x0d: return drive.highDensity(); // current emulated encoding is MFM
        case 0x0e: return !drive.inserted() || !drive.motorOn(); // active-high not-ready
        case 0x0f: return !drive.highDensity(); // active-low HD aperture
        default: return false;
        }
    }
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
    case 0x0f:
        // The SuperDrive's HD sense is active low: double-density media
        // reports high, while a 1.44 MB disk with an HD aperture reports low.
        return !drive.highDensity();
    default:
        return true;
    }
}

int IwmController::selectedDriveIndex() const
{
    return m_lines[SelectDrive] ? 1 : 0;
}

// The single guest-initiated eject path. Machines mirror what is in each drive,
// so an eject that only clears the media leaves that mirror describing a disk
// the guest has already thrown out.
void IwmController::ejectSelectedDrive()
{
    const auto drive = selectedDriveIndex();
    (void)selectedDrive().flushWrites();
    selectedDrive().eject();
    appendTraceEvent(QStringLiteral("drive %1 ejected by guest").arg(drive));
    if (m_mediaEjectedCallback) m_mediaEjectedCallback(drive);
}

void IwmController::setMediaEjectedCallback(std::function<void(int drive)> callback)
{
    m_mediaEjectedCallback = std::move(callback);
}

floppy::FloppyDiskImage& IwmController::selectedDrive()
{
    return m_lines[SelectDrive] ? m_externalDrive : m_internalDrive;
}

const floppy::FloppyDiskImage& IwmController::selectedDrive() const
{
    return m_lines[SelectDrive] ? m_externalDrive : m_internalDrive;
}

floppy::FloppyDiskImage* IwmController::driveByIndex(int drive)
{
    if (drive == 0) return &m_internalDrive;
    if (drive == 1) return &m_externalDrive;
    return nullptr;
}

const floppy::FloppyDiskImage* IwmController::driveByIndex(int drive) const
{
    if (drive == 0) return &m_internalDrive;
    if (drive == 1) return &m_externalDrive;
    return nullptr;
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
