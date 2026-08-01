#pragma once

#include <cstdint>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "cutemac/core/GuestInput.h"
#include "cutemac/core/GuestPowerRequest.h"
#include "cutemac/devices/audio/AudioFrame.h"
#include "cutemac/devices/video/VideoFrame.h"

namespace cutemac::core {

class IMachine {
public:
    virtual ~IMachine() = default;

    [[nodiscard]] virtual QString machineId() const = 0;
    [[nodiscard]] virtual bool loadRomFile(const QString& path, const QStringList& patches) = 0;
    [[nodiscard]] virtual bool loadDiskImage(const QString& path) = 0;
    virtual void ejectDiskImage() = 0;
    [[nodiscard]] virtual bool loadScsiDisk(int id, const QString& path, bool readOnly) = 0;
    virtual void ejectScsiDevice(int id) = 0;
    [[nodiscard]] virtual bool loadFloppyImage(const QString& path, bool readOnly = false) = 0;
    virtual void ejectFloppyImage() = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual int runCycles(int cycles) = 0;
    [[nodiscard]] virtual std::uint64_t cycleCount() const = 0;
    [[nodiscard]] virtual std::uint32_t programCounter() const = 0;
    [[nodiscard]] virtual bool overlayEnabled() const = 0;
    [[nodiscard]] virtual QByteArray framebufferBytes() const = 0;
    [[nodiscard]] virtual devices::video::VideoFrame videoFrame() const = 0;
    [[nodiscard]] virtual devices::audio::AudioFrame takeAudioFrame() { return {}; }
    [[nodiscard]] virtual GuestPowerRequest takePowerRequest() { return GuestPowerRequest::None; }
    virtual void queueInput(const GuestInputEvent& event, std::uint64_t cycle) = 0;
};

class IDebugMachineAccess {
public:
    virtual ~IDebugMachineAccess() = default;
    [[nodiscard]] virtual void* debugMachine(const QString& machineId) = 0;
};

} // namespace cutemac::core
