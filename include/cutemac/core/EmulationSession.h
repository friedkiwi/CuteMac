#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

#include <QByteArray>
#include <QString>

#include "cutemac/config/Configuration.h"
#include "cutemac/core/GuestInput.h"
#include "cutemac/core/IMachine.h"

namespace cutemac::core {

class EmulationSession final : public IDebugMachineAccess {
public:
    struct Status {
        QString machineId;
        std::uint32_t programCounter = 0;
        std::uint64_t cycles = 0;
        std::uint64_t diskActivityCounter = 0;
        bool overlayEnabled = false;
        bool romLoaded = false;
        bool paused = true;
    };

    explicit EmulationSession(config::Configuration configuration);
    ~EmulationSession() override;

    EmulationSession(const EmulationSession&) = delete;
    EmulationSession& operator=(const EmulationSession&) = delete;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool reconfigure(config::Configuration configuration);
    void reset();
    [[nodiscard]] bool triggerProgrammersInterrupt();
    [[nodiscard]] int runCycles(int cycles);
    void setPaused(bool paused);
    [[nodiscard]] bool paused() const;
    [[nodiscard]] Status status() const;
    [[nodiscard]] QByteArray framebufferBytes() const;
    [[nodiscard]] devices::video::VideoFrame videoFrame() const;
    [[nodiscard]] devices::audio::AudioFrame takeAudioFrame();
    [[nodiscard]] bool audioPlaybackActive() const;
    [[nodiscard]] GuestPowerRequest takePowerRequest();
    [[nodiscard]] config::Configuration configuration() const;

    void queueMousePosition(std::int16_t x, std::int16_t y);
    void queueMouseDelta(std::int16_t dx, std::int16_t dy);
    void queueMouseButton(bool pressed);
    void queueKey(std::uint8_t keyCode, bool pressed);
    void queueKeyboardReset();

    [[nodiscard]] bool insertDisk(const QString& path);
    void ejectDisk();
    [[nodiscard]] bool insertScsiDevice(int id, config::ScsiDeviceType type, const QString& path, bool readOnly = false);
    void ejectScsiDevice(int id);
    [[nodiscard]] bool insertFloppy(const QString& path, bool readOnly = false);
    // Media the machine actually has right now, which is not the same as what
    // the host inserted: the guest can eject a disk itself. Reported in one
    // locked call so a frontend polling it does not take the session lock once
    // per drive. tracked is false for machines that do not report media state,
    // and the frontend must then leave its own record alone.
    struct MediaState {
        bool tracked = false;
        std::array<QString, 2> floppyPaths;
        std::array<QString, 7> scsiPaths;
    };
    [[nodiscard]] MediaState mediaState() const;

    [[nodiscard]] bool insertFloppy(int drive, const QString& path, bool readOnly = false);
    void ejectFloppy();
    void ejectFloppy(int drive);

    [[nodiscard]] void* debugMachine(const QString& machineId) override;
    [[nodiscard]] IDebugCpuAccess* debugCpuAccess() override;
    [[nodiscard]] IDebugDeviceAccess* debugDeviceAccess() override;
    [[nodiscard]] debug::MachineSnapshot debugSnapshot(std::chrono::milliseconds lockTimeout) override;

private:
    [[nodiscard]] static std::unique_ptr<IMachine> createMachine(const config::Configuration& configuration);
    void queueInput(GuestInputEvent event);

    // Timed so panic capture can bound its wait instead of blocking forever on
    // a wedged emulation thread.
    mutable std::timed_mutex m_mutex;
    config::Configuration m_configuration;
    std::unique_ptr<IMachine> m_machine;
    bool m_romLoaded = false;
    bool m_paused = true;
    GuestPowerRequest m_powerRequest = GuestPowerRequest::None;
};

} // namespace cutemac::core
