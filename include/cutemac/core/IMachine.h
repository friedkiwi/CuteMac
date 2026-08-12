#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "cutemac/core/GuestInput.h"
#include "cutemac/core/IDebugDeviceAccess.h"
#include "cutemac/core/GuestPowerRequest.h"
#include "cutemac/debug/MachineSnapshot.h"
#include "cutemac/devices/audio/AudioFrame.h"
#include "cutemac/devices/video/VideoFrame.h"
#include "cutemac/devices/serial/SerialEndpoint.h"

namespace cutemac::core {

class IDebugCpuAccess;

class IMachine {
public:
    virtual ~IMachine() = default;

    [[nodiscard]] virtual QString machineId() const = 0;
    [[nodiscard]] virtual bool loadRomFile(const QString& path, const QStringList& patches) = 0;
    [[nodiscard]] virtual bool loadDiskImage(const QString& path) = 0;
    virtual void ejectDiskImage() = 0;
    [[nodiscard]] virtual bool loadScsiDisk(int id, const QString& path, bool readOnly) = 0;
    [[nodiscard]] virtual bool loadScsiCdRom(int id, const QString& path) = 0;
    virtual void ejectScsiCdRom(int id) = 0;
    virtual void ejectScsiDevice(int id) = 0;
    [[nodiscard]] virtual bool loadFloppyImage(const QString& path, bool readOnly = false) = 0;
    [[nodiscard]] virtual bool loadFloppyImage(int drive, const QString& path, bool readOnly = false) = 0;
    virtual void ejectFloppyImage() = 0;
    virtual void ejectFloppyImage(int drive) = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual bool triggerProgrammersInterrupt() { return false; }
    [[nodiscard]] virtual int runCycles(int cycles) = 0;
    [[nodiscard]] virtual std::uint64_t cycleCount() const = 0;
    [[nodiscard]] virtual std::uint32_t programCounter() const = 0;
    [[nodiscard]] virtual std::uint64_t diskActivityCounter() const { return 0; }

    // Live media state, so a frontend can notice media the guest ejected
    // itself -- dragging a disk to the Trash, or a CD eject command -- rather
    // than going on showing what was inserted from the host. Machines that do
    // not report it answer tracksMediaState() false and are left alone, since
    // a default empty path is indistinguishable from an ejected drive.
    [[nodiscard]] virtual bool tracksMediaState() const { return false; }
    [[nodiscard]] virtual QString floppyImagePath(int drive) const { Q_UNUSED(drive); return {}; }
    [[nodiscard]] virtual QString scsiImagePath(int id) const { Q_UNUSED(id); return {}; }
    [[nodiscard]] virtual bool overlayEnabled() const = 0;
    [[nodiscard]] virtual QByteArray framebufferBytes() const = 0;
    [[nodiscard]] virtual devices::video::VideoFrame videoFrame() const = 0;
    [[nodiscard]] virtual devices::audio::AudioFrame takeAudioFrame() { return {}; }
    [[nodiscard]] virtual bool audioPlaybackActive() const { return false; }
    [[nodiscard]] virtual GuestPowerRequest takePowerRequest() { return GuestPowerRequest::None; }
    virtual void queueInput(const GuestInputEvent& event, std::uint64_t cycle) = 0;
    virtual void attachSerialEndpoint(int channel, std::shared_ptr<devices::serial::SerialEndpoint> endpoint) = 0;

    // Machine-neutral state capture for panic dumps. The default returns an
    // empty snapshot so a machine that has not been migrated still compiles;
    // MachineSnapshot::valid() reports whether anything was captured.
    [[nodiscard]] virtual debug::MachineSnapshot debugSnapshot() const { return {}; }
};

class IDebugMachineAccess {
public:
    virtual ~IDebugMachineAccess() = default;
    [[nodiscard]] virtual void* debugMachine(const QString& machineId) = 0;
    [[nodiscard]] virtual IDebugCpuAccess* debugCpuAccess() = 0;
    // Devices, the counterpart to debugCpuAccess. Null for a machine that
    // exposes no device debug surface, which a caller must handle rather
    // than reaching for a concrete machine type instead.
    [[nodiscard]] virtual IDebugDeviceAccess* debugDeviceAccess() = 0;

    // Capture under a bounded wait on the session lock. A capture that cannot
    // take the lock still returns a snapshot built from whatever is reachable,
    // with the reason recorded in MachineSnapshot::notes -- a panic button that
    // deadlocks on the hang being investigated is worse than no button.
    [[nodiscard]] virtual debug::MachineSnapshot debugSnapshot(std::chrono::milliseconds lockTimeout) = 0;
};

} // namespace cutemac::core
