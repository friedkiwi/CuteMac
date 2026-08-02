#include "cutemac/core/EmulationSession.h"

#include <algorithm>

#include "cutemac/machines/macplus/MacPlusMachine.h"
#include "cutemac/machines/maciicx/MacIIcxMachine.h"
#include "cutemac/machines/powermac8100/PowerMac8100Machine.h"
#include "cutemac/machines/MachineCatalog.h"
#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/devices/video/nubus/MacintoshIIVideoCard.h"
#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"
#include "cutemac/devices/printer/ImageWriterII.h"
#include "cutemac/storage/PngPageSink.h"
#include "cutemac/rom/RomCatalog.h"

namespace cutemac::core {

EmulationSession::EmulationSession(config::Configuration configuration)
    : m_configuration(std::move(configuration))
    , m_machine(createMachine(m_configuration))
{
}

EmulationSession::~EmulationSession() = default;

std::unique_ptr<IMachine> EmulationSession::createMachine(const config::Configuration& configuration)
{
    if (!machines::MachineCatalog::isValidRamSize(configuration.machineId, configuration.ramSizeKiB)) {
        return {};
    }
    std::unique_ptr<IMachine> result;
    if (configuration.machineId == QStringLiteral("mac-plus")) {
        result = std::make_unique<machines::macplus::MacPlusMachine>(
            static_cast<std::size_t>(configuration.ramSizeKiB) * 1024, configuration.nvramPath);
    } else if (configuration.machineId == QStringLiteral("mac-iicx")) {
        auto machine = std::make_unique<machines::maciicx::MacIIcxMachine>(
            static_cast<std::size_t>(configuration.ramSizeKiB) * 1024,
            configuration.nvramPath);
        for (const auto& device : configuration.nubusDevices) {
            if (device.type == config::NuBusDeviceType::MacintoshIIVideo) {
                auto card = std::make_shared<devices::video::nubus::MacintoshIIVideoCard>();
                const auto path = rom::RomCatalog().deviceRomPath(QStringLiteral("apple_m2_video"));
                if (path.isEmpty() || !card->loadDeclarationRom(path)) continue;
                (void)machine->installNuBusCard(device.slot, card);
            } else {
                (void)machine->installNuBusCard(device.slot, std::make_shared<devices::video::nubus::CuteMacVideoCard>(
                    device.width, device.height, device.depth, device.vramMiB, device.acceleration, device.absolutePointer));
            }
        }
        result = std::move(machine);
    } else if (configuration.machineId == QStringLiteral("powermac-8100")) {
        result = std::make_unique<machines::powermac8100::PowerMac8100Machine>(
            static_cast<std::size_t>(configuration.ramSizeKiB) * 1024U);
    }
    if (!result) return {};
    for (const auto& device : configuration.serialDevices) {
        if (device.outputDirectory.isEmpty()) continue;
        auto pngSink = std::make_shared<storage::PngPageSink>(device.outputDirectory);
        auto printer = std::make_shared<devices::printer::ImageWriterII>(
            [pngSink](const devices::printer::RasterPage& page) { (void)pngSink->write(page); });
        result->attachSerialEndpoint(device.channel, std::move(printer));
    }
    return result;
}

bool EmulationSession::initialize()
{
    std::lock_guard lock(m_mutex);
    if (!m_machine) {
        return false;
    }
    const auto romPath = rom::RomCatalog().preferredMachineRomPath(m_configuration.machineId);
    m_romLoaded = !romPath.isEmpty() && m_machine->loadRomFile(romPath, m_configuration.enabledRomPatches());
    for (const auto& device : m_configuration.scsiDevices) {
        if (device.type == config::ScsiDeviceType::CdRom) (void)m_machine->loadScsiCdRom(device.id, device.imagePath);
        else if (device.imagePath.isEmpty()) continue;
        else (void)m_machine->loadScsiDisk(device.id, device.imagePath, device.readOnly);
    }
    if (m_configuration.scsiDevices.isEmpty() && !m_configuration.diskPath.isEmpty()) {
        (void)m_machine->loadDiskImage(m_configuration.diskPath);
    }
    if (!m_configuration.floppyPath.isEmpty()) {
        const bool readOnly = !m_configuration.iwmDevices.isEmpty() && m_configuration.iwmDevices.first().readOnly;
        (void)m_machine->loadFloppyImage(m_configuration.floppyPath, readOnly);
    }
    if (m_romLoaded) {
        m_machine->reset();
    }
    m_paused = !m_romLoaded;
    m_powerRequest = GuestPowerRequest::None;
    return m_romLoaded;
}

bool EmulationSession::reconfigure(config::Configuration configuration)
{
    std::lock_guard lock(m_mutex);
    m_configuration = std::move(configuration);
    m_machine = createMachine(m_configuration);
    m_romLoaded = false;
    m_paused = true;
    m_powerRequest = GuestPowerRequest::None;
    if (!m_machine) {
        return false;
    }
    const auto romPath = rom::RomCatalog().preferredMachineRomPath(m_configuration.machineId);
    m_romLoaded = !romPath.isEmpty() && m_machine->loadRomFile(romPath, m_configuration.enabledRomPatches());
    for (const auto& device : m_configuration.scsiDevices) {
        if (device.type == config::ScsiDeviceType::CdRom) (void)m_machine->loadScsiCdRom(device.id, device.imagePath);
        else if (device.imagePath.isEmpty()) continue;
        else (void)m_machine->loadScsiDisk(device.id, device.imagePath, device.readOnly);
    }
    if (m_configuration.scsiDevices.isEmpty() && !m_configuration.diskPath.isEmpty()) {
        (void)m_machine->loadDiskImage(m_configuration.diskPath);
    }
    if (!m_configuration.floppyPath.isEmpty()) {
        const bool readOnly = !m_configuration.iwmDevices.isEmpty() && m_configuration.iwmDevices.first().readOnly;
        (void)m_machine->loadFloppyImage(m_configuration.floppyPath, readOnly);
    }
    if (m_romLoaded) {
        m_machine->reset();
        m_paused = false;
    }
    return m_romLoaded;
}

void EmulationSession::reset()
{
    std::lock_guard lock(m_mutex);
    if (m_machine && m_romLoaded) {
        m_machine->reset();
        m_powerRequest = GuestPowerRequest::None;
    }
}

int EmulationSession::runCycles(int cycles)
{
    std::lock_guard lock(m_mutex);
    if (!m_machine || m_paused) return 0;
    const auto used = m_machine->runCycles(cycles);
    const auto request = m_machine->takePowerRequest();
    if (request != GuestPowerRequest::None) {
        m_powerRequest = request;
        m_paused = true;
    }
    return used;
}

void EmulationSession::setPaused(bool paused)
{
    std::lock_guard lock(m_mutex);
    m_paused = paused || !m_romLoaded;
}

bool EmulationSession::paused() const
{
    std::lock_guard lock(m_mutex);
    return m_paused;
}

EmulationSession::Status EmulationSession::status() const
{
    std::lock_guard lock(m_mutex);
    if (!m_machine) {
        return { m_configuration.machineId, 0, 0, false, false, true };
    }
    return { m_machine->machineId(), m_machine->programCounter(), m_machine->cycleCount(),
        m_machine->overlayEnabled(), m_romLoaded, m_paused };
}

QByteArray EmulationSession::framebufferBytes() const
{
    std::lock_guard lock(m_mutex);
    return m_machine ? m_machine->framebufferBytes() : QByteArray {};
}

devices::video::VideoFrame EmulationSession::videoFrame() const
{
    std::lock_guard lock(m_mutex);
    return m_machine ? m_machine->videoFrame() : devices::video::VideoFrame {};
}

devices::audio::AudioFrame EmulationSession::takeAudioFrame()
{
    std::lock_guard lock(m_mutex);
    return m_machine ? m_machine->takeAudioFrame() : devices::audio::AudioFrame {};
}

bool EmulationSession::audioPlaybackActive() const
{
    std::lock_guard lock(m_mutex);
    return m_machine && m_machine->audioPlaybackActive();
}

GuestPowerRequest EmulationSession::takePowerRequest()
{
    std::lock_guard lock(m_mutex);
    const auto request = m_powerRequest;
    m_powerRequest = GuestPowerRequest::None;
    return request;
}

config::Configuration EmulationSession::configuration() const
{
    std::lock_guard lock(m_mutex);
    return m_configuration;
}

void EmulationSession::queueInput(GuestInputEvent event)
{
    std::lock_guard lock(m_mutex);
    if (m_machine) {
        m_machine->queueInput(event, m_machine->cycleCount());
    }
}

void EmulationSession::queueMousePosition(std::int16_t x, std::int16_t y)
{
    queueInput({ GuestInputEvent::Type::MousePosition, x, y, false });
}

void EmulationSession::queueMouseDelta(std::int16_t dx, std::int16_t dy)
{
    queueInput({ GuestInputEvent::Type::MouseDelta, dx, dy, false });
}

void EmulationSession::queueMouseButton(bool pressed)
{
    queueInput({ GuestInputEvent::Type::MouseButton, 0, 0, pressed });
}

void EmulationSession::queueKey(std::uint8_t keyCode, bool pressed)
{
    queueInput({ GuestInputEvent::Type::Key, keyCode, 0, pressed });
}

void EmulationSession::queueKeyboardReset()
{
    queueInput({ GuestInputEvent::Type::ResetKeyboard, 0, 0, false });
}

bool EmulationSession::insertDisk(const QString& path)
{
    std::lock_guard lock(m_mutex);
    if (!m_machine || !m_machine->loadDiskImage(path)) {
        return false;
    }
    m_configuration.diskPath = path;
    return true;
}

void EmulationSession::ejectDisk()
{
    std::lock_guard lock(m_mutex);
    if (m_machine) {
        m_machine->ejectDiskImage();
    }
    m_configuration.diskPath.clear();
}

bool EmulationSession::insertScsiDevice(int id, config::ScsiDeviceType type, const QString& path, bool readOnly)
{
    std::lock_guard lock(m_mutex);
    if (!m_machine || path.isEmpty()) return false;
    const bool loaded = type == config::ScsiDeviceType::CdRom
        ? m_machine->loadScsiCdRom(id, path)
        : m_machine->loadScsiDisk(id, path, readOnly);
    if (!loaded) return false;
    for (auto& device : m_configuration.scsiDevices) {
        if (device.id != id) continue;
        device.type = type;
        device.imagePath = path;
        device.readOnly = type == config::ScsiDeviceType::CdRom || readOnly;
        return true;
    }
    return false;
}

void EmulationSession::ejectScsiDevice(int id)
{
    std::lock_guard lock(m_mutex);
    if (!m_machine) return;
    for (auto& device : m_configuration.scsiDevices) {
        if (device.id != id) continue;
        if (device.type == config::ScsiDeviceType::CdRom) m_machine->ejectScsiCdRom(id);
        else m_machine->ejectScsiDevice(id);
        device.imagePath.clear();
        return;
    }
}

bool EmulationSession::insertFloppy(const QString& path, bool readOnly)
{
    std::lock_guard lock(m_mutex);
    if (!m_machine || !m_machine->loadFloppyImage(path, readOnly)) {
        return false;
    }
    m_configuration.floppyPath = path;
    if (!m_configuration.iwmDevices.isEmpty()) {
        m_configuration.iwmDevices[0] = { path, readOnly };
    }
    return true;
}

void EmulationSession::ejectFloppy()
{
    std::lock_guard lock(m_mutex);
    if (m_machine) {
        m_machine->ejectFloppyImage();
    }
    m_configuration.floppyPath.clear();
}

void* EmulationSession::debugMachine(const QString& machineId)
{
    std::lock_guard lock(m_mutex);
    return m_machine && m_machine->machineId() == machineId ? m_machine.get() : nullptr;
}

IDebugCpuAccess* EmulationSession::debugCpuAccess()
{
    std::lock_guard lock(m_mutex);
    return dynamic_cast<IDebugCpuAccess*>(m_machine.get());
}

} // namespace cutemac::core
