#pragma once

#include <cstdint>

#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "cutemac/config/Configuration.h"
#include "cutemac/core/EmulationSession.h"
#include "cutemac/core/SessionRunner.h"
#include "cutemac/session/AudioOutput.h"
#if defined(CUTEMAC_ENABLE_SESSION_CONTROL)
#include "cutemac/session/SessionControlServer.h"
#endif
#include "cutemac/storage/DiskImageManager.h"

class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QToolBar;

namespace cutemac::session {

class DisplayWidget;

// A single emulated machine and its window. Multiple instances can coexist in
// one process; each owns its own runner thread and audio sink.
class EmulatorWindow final : public QMainWindow {
public:
    EmulatorWindow(config::Configuration configuration, QString profilePath, QWidget* profileManagerWindow = nullptr);
    ~EmulatorWindow() override;

    // Canonical path of the backing profile file, or an empty string for a
    // session started from a configuration that was never saved.
    [[nodiscard]] QString profilePath() const { return m_profilePath; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    [[nodiscard]] double preferredInitialZoom() const;
    void updateInteractiveInputState(bool preserveDoubleClickWindow = false);
    // Media insertion and ejection are host-time events injected into guest
    // time. Unlimited execution can advance the guest by many seconds across
    // the host-time gap around one, so the disk-change signal the guest has a
    // bounded window to observe goes by before its driver ever polls for it.
    void beginMediaSettlingWindow();
    // Mirrors media the guest ejected itself back into the frontend's own
    // record, so the toolbar and profile stop advertising an empty drive.
    void reconcileGuestEjectedMedia();
    void buildMenus();
    QAction* addZoomAction(QMenu* menu, QActionGroup* group, const QString& text, double factor);
    void setZoom(double factor);
    void setCustomZoom();
    void buildToolbar();
    void insertScsiFromToolbar(int id, config::ScsiDeviceType type);
    void insertScsiPath(int id, config::ScsiDeviceType type, const QString& path);
    void ejectScsiFromToolbar(int id);
    void createHardDiskFromToolbar(int id);
    void setScsiReadOnly(int id, bool enabled);
    void showScsiDetails(int id);
    void insertFloppyFromToolbar(int drive);
    void insertFloppyPath(int drive, const QString& path);
    [[nodiscard]] QStringList recentImages(storage::DiskImageType type) const;
    void rememberRecentImage(storage::DiskImageType type, const QString& path);
    void ejectFloppy(int drive);
    void createFloppyFromToolbar(int drive, qint64 sizeBytes);
    void buildStatusBar();
    void updateWindowTitle();
    void loadAndReset();
    void setRuntimeSpeed(config::RuntimeSpeed speed);
    void updateSpeedActions();
    void configureMachine();
    void saveConfiguration();
    void setPaused(bool paused);
#if CUTEMAC_ENABLE_PANIC_DUMP
    void triggerPanicDump();
#endif
    void runFrame();
    void updateStatus();
    void showProfileManager();

    config::Configuration m_configuration;
    // The profile exactly as it was loaded. Panic dumps carry both this and the
    // live configuration, which drift apart as media is inserted and speed
    // changes during a session.
    config::Configuration m_startupConfiguration;
    QString m_profilePath;
    QPointer<QWidget> m_profileManagerWindow;
    core::EmulationSession m_session;
    core::SessionRunner m_runner;
#if defined(CUTEMAC_ENABLE_SESSION_CONTROL)
    SessionControlServer m_controlServer;
#endif
    AudioOutput m_audioOutput;
    int m_audioPacingFrames = 0;
    DisplayWidget* m_display = nullptr;
    QLabel* m_profileStatus = nullptr;
    QLabel* m_runStatus = nullptr;
    QLabel* m_speedStatus = nullptr;
    QLabel* m_diskActivityStatus = nullptr;
    QAction* m_realtimeSpeedAction = nullptr;
    QAction* m_unlimitedSpeedAction = nullptr;
    QAction* m_zoom1xAction = nullptr;
    QAction* m_zoom15xAction = nullptr;
    QAction* m_zoom2xAction = nullptr;
    QAction* m_zoomCustomAction = nullptr;
    QSet<int> m_pressedInputKeys;
    bool m_mouseInputPressed = false;
    QTimer m_mousePacingReleaseTimer;
    QTimer m_mediaPacingReleaseTimer;
    QTimer m_frameTimer;
    QToolBar* m_toolbar = nullptr;
    QAction* m_pauseAction = nullptr;
    std::uint64_t m_lastDiskActivityCounter = 0;
    int m_diskActivityFlashFrames = 0;
    bool m_diskActivityLit = false;
    bool m_romLoaded = false;
    bool m_paused = true;
    bool m_inputCaptured = false;
    bool m_zoomResizePending = false;
    bool m_diskActivityCounterInitialized = false;
    quint64 m_frames = 0;
};

} // namespace cutemac::session
