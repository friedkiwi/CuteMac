#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "cutemac/devices/iwm/IwmController.h"

namespace cutemac::core {

// Machine-neutral debug surface for emulated devices, the device-side
// counterpart to IDebugCpuAccess.
//
// The debug console used to reach devices through concrete machine pointers,
// so every command was written against whichever machine was brought up first
// and was silently unavailable everywhere else: floppy commands worked only on
// the Mac Plus, and a IIcx -- a SuperDrive machine -- could not be asked about
// its own drive at all. Growing that by machine does not scale, and the
// failure is quiet, which is worse: the command exists, answers nothing, and
// nobody learns why.
//
// A machine implements what it actually has. Everything defaults to absent, so
// a machine without a device family neither has to implement it nor can be
// mistaken for one that answers.
class IDebugDeviceAccess {
public:
    virtual ~IDebugDeviceAccess() = default;

    // Floppy. driveCount() == 0 means the machine has no drives, which a
    // console should report rather than presenting empty state as real.
    [[nodiscard]] virtual int floppyDriveCount() const { return 0; }
    [[nodiscard]] virtual bool loadFloppy(int drive, const QString& path, bool readOnly)
    {
        Q_UNUSED(drive); Q_UNUSED(path); Q_UNUSED(readOnly);
        return false;
    }
    virtual void ejectFloppy(int drive) { Q_UNUSED(drive); }
    [[nodiscard]] virtual QString floppyPath(int drive) const { Q_UNUSED(drive); return {}; }
    [[nodiscard]] virtual devices::iwm::IwmController::DebugState floppyState() const { return {}; }
    [[nodiscard]] virtual devices::iwm::IwmController::DebugState floppyState(int drive) const
    {
        Q_UNUSED(drive);
        return {};
    }
    // Decoded track bytes as the controller would present them, for a console
    // to count address and data marks against.
    [[nodiscard]] virtual QByteArray floppyTrackBytes(int track, int side) const
    {
        Q_UNUSED(track); Q_UNUSED(side);
        return {};
    }
    [[nodiscard]] virtual QByteArray floppyLastWindow() const { return {}; }

    // Controller-level tracing, which belongs to the device rather than to the
    // console's own stepping rings.
    virtual void setFloppyTraceEnabled(bool enabled) { Q_UNUSED(enabled); }
    [[nodiscard]] virtual QStringList floppyTraceEvents() const { return {}; }
};

} // namespace cutemac::core
