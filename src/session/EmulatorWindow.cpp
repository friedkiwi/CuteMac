#include <QApplication>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QMessageBox>
#include <QStyle>
#include <QSet>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <algorithm>
#include <utility>

#include "cutemac/session/EmulatorWindow.h"

#include "cutemac/config/Configuration.h"
#include "cutemac/core/EmulationSession.h"
#include "cutemac/core/SessionRunner.h"
#include "cutemac/session/SessionControlServer.h"
#include "cutemac/session/AudioOutput.h"
#include "cutemac/session/FramebufferRenderer.h"
#include "cutemac/session/HostInputMapper.h"
#include "cutemac/ui/ConfigurationDialog.h"
#include "cutemac/ui/DiskImageWidgets.h"

namespace {

bool sameIwmDevices(const QVector<cutemac::config::IwmDeviceConfiguration>& left,
    const QVector<cutemac::config::IwmDeviceConfiguration>& right)
{
    if (left.size() != right.size()) return false;
    for (qsizetype index = 0; index < left.size(); ++index) {
        if (left[index].imagePath != right[index].imagePath || left[index].readOnly != right[index].readOnly) return false;
    }
    return true;
}

void ensureFloppyDriveCount(cutemac::config::Configuration& configuration)
{
    if (configuration.iwmDevices.isEmpty()) return;
    while (configuration.iwmDevices.size() < 2) {
        configuration.iwmDevices.append(cutemac::config::IwmDeviceConfiguration {});
    }
    if (configuration.iwmDevices.size() > 2) {
        configuration.iwmDevices.resize(2);
    }
    configuration.floppyPath = configuration.iwmDevices.first().imagePath;
}

QString floppyDriveName(int drive)
{
    return drive == 0 ? QStringLiteral("Internal Floppy") : QStringLiteral("External Floppy");
}

bool sameScsiDevices(const QVector<cutemac::config::ScsiDeviceConfiguration>& left,
    const QVector<cutemac::config::ScsiDeviceConfiguration>& right)
{
    if (left.size() != right.size()) return false;
    for (qsizetype index = 0; index < left.size(); ++index) {
        if (left[index].id != right[index].id || left[index].type != right[index].type
            || left[index].imagePath != right[index].imagePath || left[index].readOnly != right[index].readOnly) return false;
    }
    return true;
}

bool sameNuBusDevices(const QVector<cutemac::config::NuBusDeviceConfiguration>& left,
    const QVector<cutemac::config::NuBusDeviceConfiguration>& right)
{
    if (left.size() != right.size()) return false;
    for (qsizetype index = 0; index < left.size(); ++index) {
        const auto& a = left[index];
        const auto& b = right[index];
        if (a.slot != b.slot || a.type != b.type || a.declarationRomPath != b.declarationRomPath
            || a.width != b.width || a.height != b.height || a.depth != b.depth
            || a.vramMiB != b.vramMiB || a.acceleration != b.acceleration) return false;
    }
    return true;
}

bool sameSerialDevices(const QVector<cutemac::config::SerialDeviceConfiguration>& left,
    const QVector<cutemac::config::SerialDeviceConfiguration>& right)
{
    if (left.size() != right.size()) return false;
    for (qsizetype i = 0; i < left.size(); ++i) {
        if (left[i].phonebook.size() != right[i].phonebook.size()) return false;
        if (left[i].channel != right[i].channel || left[i].type != right[i].type
            || left[i].outputDirectory != right[i].outputDirectory
            || left[i].directTcpDialing != right[i].directTcpDialing
            || left[i].slip.enabled != right[i].slip.enabled
            || left[i].slip.localIp != right[i].slip.localIp
            || left[i].slip.remoteIp != right[i].slip.remoteIp
            || left[i].slip.mtu != right[i].slip.mtu
            || left[i].tcpMode != right[i].tcpMode
            || left[i].tcpHost != right[i].tcpHost
            || left[i].tcpPort != right[i].tcpPort) return false;
        for (qsizetype entry = 0; entry < left[i].phonebook.size(); ++entry) {
            if (left[i].phonebook[entry].number != right[i].phonebook[entry].number
                || left[i].phonebook[entry].target != right[i].phonebook[entry].target
                || left[i].phonebook[entry].telnet != right[i].phonebook[entry].telnet) return false;
        }
    }
    return true;
}

bool requiresMachineReset(const cutemac::config::Configuration& current, const cutemac::config::Configuration& proposed)
{
    return current.machineId != proposed.machineId || current.romPath != proposed.romPath
        || current.nvramPath != proposed.nvramPath
        || current.ramSizeKiB != proposed.ramSizeKiB || current.skipRamPatternTest != proposed.skipRamPatternTest
        || !sameIwmDevices(current.iwmDevices, proposed.iwmDevices)
        || !sameScsiDevices(current.scsiDevices, proposed.scsiDevices)
        || !sameNuBusDevices(current.nubusDevices, proposed.nubusDevices)
        || !sameSerialDevices(current.serialDevices, proposed.serialDevices);
}

} // namespace

namespace cutemac::session {

class DisplayWidget final : public QWidget {
public:
    explicit DisplayWidget(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_image(512, 342, QImage::Format_RGB32)
    {
        m_image.fill(Qt::white);
        setMinimumSize(160, 120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    void setRunning(bool running)
    {
        m_running = running;
        update();
    }

    void setFramebuffer(const cutemac::devices::video::VideoFrame& frame)
    {
        const auto image = cutemac::session::FramebufferRenderer::render(frame);
        if (!image.isNull()) m_image = image;
        update();
    }

    [[nodiscard]] QSize framebufferSize() const { return m_image.size(); }

    void setZoomFactor(double factor)
    {
        m_zoomFactor = factor;
        update();
    }

    void setMouseCallback(std::function<void(int, int, bool)> callback)
    {
        m_mouseCallback = std::move(callback);
    }

    void setKeyCallback(std::function<void(int, bool)> callback)
    {
        m_keyCallback = std::move(callback);
    }

    void releasePointerInput()
    {
        if (m_leftButtonPressed) {
            m_leftButtonPressed = false;
            if (m_mouseCallback) {
                m_mouseCallback(m_lastGuestPoint.x(), m_lastGuestPoint.y(), false);
            }
        }
        m_pointerInsideDisplay = false;
        unsetCursor();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(24, 24, 24));
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

        const auto target = displayRect();
        painter.drawImage(target, m_image);

        if (!m_running) {
            painter.fillRect(target, QColor(255, 255, 255, 72));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        updatePointerPresence(event->pos());
        if (!m_pointerInsideDisplay) return;
        sendMouseEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        setFocus(Qt::MouseFocusReason);
        updatePointerPresence(event->pos());
        if (!m_pointerInsideDisplay) {
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            m_leftButtonPressed = true;
        }
        sendMouseEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        updatePointerPresence(event->pos());
        if (event->button() == Qt::LeftButton) {
            m_leftButtonPressed = false;
        }
        if (m_pointerInsideDisplay) sendMouseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        // Qt sends the second press of a double-click as MouseButtonDblClick
        // instead of calling mousePressEvent(). Forward it so the guest sees
        // both complete clicks and can perform Finder's double-click action.
        updatePointerPresence(event->pos());
        if (m_pointerInsideDisplay && event->button() == Qt::LeftButton) {
            m_leftButtonPressed = true;
        }
        if (m_pointerInsideDisplay) sendMouseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        sendKeyEvent(event, true);
    }

    void keyReleaseEvent(QKeyEvent* event) override
    {
        sendKeyEvent(event, false);
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        releasePointerInput();
        if (m_keyCallback) {
            for (const auto keyCode : std::as_const(m_pressedMacKeys)) m_keyCallback(keyCode, false);
        }
        m_pressedMacKeys.clear();
        QWidget::focusOutEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        releasePointerInput();
        QWidget::leaveEvent(event);
    }

private:
    void updatePointerPresence(const QPoint& widgetPoint)
    {
        const bool inside = displayRect().contains(widgetPoint);
        if (inside == m_pointerInsideDisplay) return;
        if (!inside) {
            releasePointerInput();
            return;
        }
        m_pointerInsideDisplay = true;
        setCursor(Qt::BlankCursor);
    }

    [[nodiscard]] QRect displayRect() const
    {
        const QSize baseSize = m_image.size().isEmpty() ? QSize(512, 342) : m_image.size();
        const auto availableScale = std::min(static_cast<double>(width()) / baseSize.width(),
            static_cast<double>(height()) / baseSize.height());
        const auto scale = m_zoomFactor > 0.0 ? std::min(m_zoomFactor, availableScale) : availableScale;
        const QSize scaled(std::max(1, static_cast<int>(baseSize.width() * scale)),
            std::max(1, static_cast<int>(baseSize.height() * scale)));
        return QRect(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
    }

    [[nodiscard]] QPoint macPointFor(const QPoint& widgetPoint) const
    {
        const QSize baseSize = m_image.size().isEmpty() ? QSize(512, 342) : m_image.size();
        const auto target = displayRect();
        if (!target.contains(widgetPoint)) {
            return QPoint(std::clamp(widgetPoint.x() - target.left(), 0, target.width() - 1) * baseSize.width() / target.width(),
                std::clamp(widgetPoint.y() - target.top(), 0, target.height() - 1) * baseSize.height() / target.height());
        }

        return QPoint((widgetPoint.x() - target.left()) * baseSize.width() / target.width(),
            (widgetPoint.y() - target.top()) * baseSize.height() / target.height());
    }

    void sendMouseEvent(QMouseEvent* event)
    {
        if (!m_mouseCallback) {
            return;
        }
        m_lastGuestPoint = macPointFor(event->pos());
        m_mouseCallback(m_lastGuestPoint.x(), m_lastGuestPoint.y(), m_leftButtonPressed);
    }

    void sendKeyEvent(QKeyEvent* event, bool pressed)
    {
        if (event->isAutoRepeat() || !m_keyCallback) {
            return;
        }
        const auto code = cutemac::session::HostInputMapper::macKeyCode(*event);
        if (code >= 0) {
            if (pressed == m_pressedMacKeys.contains(code)) {
                event->accept();
                return;
            }
            if (pressed) m_pressedMacKeys.insert(code); else m_pressedMacKeys.remove(code);
            m_keyCallback(code, pressed);
            event->accept();
        }
    }

    [[nodiscard]] int macKeyCodeFor(QKeyEvent* event) const
    {
        switch (event->key()) {
        case Qt::Key_A: return 0x00;
        case Qt::Key_S: return 0x01;
        case Qt::Key_D: return 0x02;
        case Qt::Key_F: return 0x03;
        case Qt::Key_H: return 0x04;
        case Qt::Key_G: return 0x05;
        case Qt::Key_Z: return 0x06;
        case Qt::Key_X: return 0x07;
        case Qt::Key_C: return 0x08;
        case Qt::Key_V: return 0x09;
        case Qt::Key_B: return 0x0b;
        case Qt::Key_Q: return 0x0c;
        case Qt::Key_W: return 0x0d;
        case Qt::Key_E: return 0x0e;
        case Qt::Key_R: return 0x0f;
        case Qt::Key_Y: return 0x10;
        case Qt::Key_T: return 0x11;
        case Qt::Key_1: return 0x12;
        case Qt::Key_2: return 0x13;
        case Qt::Key_3: return 0x14;
        case Qt::Key_4: return 0x15;
        case Qt::Key_6: return 0x16;
        case Qt::Key_5: return 0x17;
        case Qt::Key_Equal: return 0x18;
        case Qt::Key_9: return 0x19;
        case Qt::Key_7: return 0x1a;
        case Qt::Key_Minus: return 0x1b;
        case Qt::Key_8: return 0x1c;
        case Qt::Key_0: return 0x1d;
        case Qt::Key_BracketRight: return 0x1e;
        case Qt::Key_O: return 0x1f;
        case Qt::Key_U: return 0x20;
        case Qt::Key_BracketLeft: return 0x21;
        case Qt::Key_I: return 0x22;
        case Qt::Key_P: return 0x23;
        case Qt::Key_Return: return 0x24;
        case Qt::Key_L: return 0x25;
        case Qt::Key_J: return 0x26;
        case Qt::Key_Apostrophe: return 0x27;
        case Qt::Key_K: return 0x28;
        case Qt::Key_Semicolon: return 0x29;
        case Qt::Key_Backslash: return 0x2a;
        case Qt::Key_Comma: return 0x2b;
        case Qt::Key_Slash: return 0x2c;
        case Qt::Key_N: return 0x2d;
        case Qt::Key_M: return 0x2e;
        case Qt::Key_Period: return 0x2f;
        case Qt::Key_Tab: return 0x30;
        case Qt::Key_Space: return 0x31;
        case Qt::Key_QuoteLeft: return 0x32;
        case Qt::Key_Backspace: return 0x33;
        case Qt::Key_Escape: return 0x35;
        case Qt::Key_Control: return 0x36;
        case Qt::Key_Shift: return 0x38;
        case Qt::Key_CapsLock: return 0x39;
        case Qt::Key_Alt: return 0x3a;
        case Qt::Key_Meta: return 0x37;
        case Qt::Key_Left: return 0x3b;
        case Qt::Key_Right: return 0x3c;
        case Qt::Key_Down: return 0x3d;
        case Qt::Key_Up: return 0x3e;
        default:
            return -1;
        }
    }

    bool m_running = false;
    double m_zoomFactor = 0.0;
    bool m_pointerInsideDisplay = false;
    bool m_leftButtonPressed = false;
    QPoint m_lastGuestPoint;
    QImage m_image;
    std::function<void(int, int, bool)> m_mouseCallback;
    std::function<void(int, bool)> m_keyCallback;
    QSet<int> m_pressedMacKeys;
};

EmulatorWindow::EmulatorWindow(cutemac::config::Configuration configuration, QString profilePath, QWidget* profileManagerWindow)
    : m_configuration(std::move(configuration))
    , m_profilePath(std::move(profilePath))
    , m_profileManagerWindow(profileManagerWindow)
    , m_session(m_configuration)
    , m_runner(m_session, m_configuration.cyclesPerFrame, m_configuration.runtimeSpeed)
    , m_controlServer(m_session, m_runner, this)
    , m_audioOutput(this)
{
    ensureFloppyDriveCount(m_configuration);
    setWindowTitle(QStringLiteral("CuteMac - %1").arg(m_configuration.profileName));
    m_display = new DisplayWidget;
    m_display->setMouseCallback([this](int x, int y, bool pressed) {
        m_session.queueMousePosition(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y));
        if (pressed != m_mouseInputPressed) {
            m_mouseInputPressed = pressed;
            m_session.queueMouseButton(pressed);
            updateInteractiveInputState(!pressed);
        }
    });
    m_display->setKeyCallback([this](int keyCode, bool pressed) {
        if (pressed) {
            m_pressedInputKeys.insert(keyCode);
            updateInteractiveInputState();
        }
        m_session.queueKey(static_cast<std::uint8_t>(keyCode), pressed);
        if (!pressed) {
            m_pressedInputKeys.remove(keyCode);
            updateInteractiveInputState();
        }
    });
    setCentralWidget(m_display);

    m_mousePacingReleaseTimer.setSingleShot(true);
    m_mousePacingReleaseTimer.setInterval(QApplication::doubleClickInterval());
    connect(&m_mousePacingReleaseTimer, &QTimer::timeout, this, [this]() { updateInteractiveInputState(); });

    buildMenus();
    buildToolbar();
    buildStatusBar();

    m_frameTimer.setTimerType(Qt::PreciseTimer);
    m_frameTimer.setInterval(16);
    connect(&m_frameTimer, &QTimer::timeout, this, [this]() { runFrame(); });

    loadAndReset();
    setZoom(1.0);
    QTimer::singleShot(0, this, [this]() { setZoom(preferredInitialZoom()); });
    m_runner.start();
    if (m_controlServer.listen()) {
        // Several sessions can share one process, so name the profile the port
        // belongs to instead of logging a bare port number.
        qInfo("CuteMac session control for \"%s\" listening on 127.0.0.1:%u",
            qUtf8Printable(m_configuration.profileName), m_controlServer.port());
    }
    setPaused(false);
}

EmulatorWindow::~EmulatorWindow() { m_runner.stop(); }

void EmulatorWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (m_zoomResizePending) {
        m_zoomResizePending = false;
        return;
    }
    setCustomZoom();
}

double EmulatorWindow::preferredInitialZoom() const
{
    const auto framebuffer = m_display->framebufferSize();
    const auto* targetScreen = screen() != nullptr ? screen() : QApplication::primaryScreen();
    if (framebuffer.isEmpty() || targetScreen == nullptr) return 1.0;

    const QSize chrome(qMax(0, width() - m_display->width()),
        qMax(0, height() - m_display->height()));
    const auto available = targetScreen->availableGeometry().size();
    // Leave breathing room for the desktop shell, window borders, and
    // positioning differences between window managers.
    const QSize comfortable(qFloor(available.width() * 0.9), qFloor(available.height() * 0.9));
    for (const double factor : { 2.0, 1.5 }) {
        const QSize requested(qRound(framebuffer.width() * factor) + chrome.width(),
            qRound(framebuffer.height() * factor) + chrome.height());
        if (requested.width() <= comfortable.width() && requested.height() <= comfortable.height()) return factor;
    }
    return 1.0;
}

void EmulatorWindow::updateInteractiveInputState(bool preserveDoubleClickWindow)
{
    const bool inputHeld = m_mouseInputPressed || !m_pressedInputKeys.isEmpty();
    if (inputHeld) {
        m_mousePacingReleaseTimer.stop();
        m_runner.setInteractiveInputActive(true);
    } else if (preserveDoubleClickWindow) {
        // Unlimited execution can advance the guest by many seconds in the
        // short host-time gap between clicks. Keep realtime pacing through
        // Qt's double-click interval so the guest sees both clicks within
        // its own DoubleTime window.
        m_runner.setInteractiveInputActive(true);
        m_mousePacingReleaseTimer.start();
    } else if (!m_mousePacingReleaseTimer.isActive()) {
        m_runner.setInteractiveInputActive(false);
    }
}

void EmulatorWindow::buildMenus()
{
    auto* machineMenu = menuBar()->addMenu(QStringLiteral("Machine"));
    machineMenu->addAction(QStringLiteral("Pause/Resume"), this, [this]() { setPaused(!m_paused); });
    machineMenu->addAction(QStringLiteral("Reset"), this, [this]() { loadAndReset(); });
    machineMenu->addAction(QStringLiteral("Configure"), this, [this]() { configureMachine(); });
    auto* speedMenu = machineMenu->addMenu(QStringLiteral("Speed"));
    auto* speedGroup = new QActionGroup(this);
    speedGroup->setExclusive(true);
    m_realtimeSpeedAction = speedMenu->addAction(QStringLiteral("Realtime"));
    m_unlimitedSpeedAction = speedMenu->addAction(QStringLiteral("Unlimited"));
    for (auto* action : { m_realtimeSpeedAction, m_unlimitedSpeedAction }) {
        action->setActionGroup(speedGroup);
        action->setCheckable(true);
    }
    connect(m_realtimeSpeedAction, &QAction::triggered, this, [this]() { setRuntimeSpeed(cutemac::config::RuntimeSpeed::Realtime); });
    connect(m_unlimitedSpeedAction, &QAction::triggered, this, [this]() { setRuntimeSpeed(cutemac::config::RuntimeSpeed::Unlimited); });
    updateSpeedActions();
    machineMenu->addSeparator();
    if (!m_profileManagerWindow.isNull()) {
        machineMenu->addAction(QStringLiteral("Session Manager"), this, [this]() { showProfileManager(); });
    }
    machineMenu->addAction(QStringLiteral("Close"), this, &QWidget::close);

    auto* displayMenu = menuBar()->addMenu(QStringLiteral("Display"));
    auto* zoomMenu = displayMenu->addMenu(QStringLiteral("Zoom"));
    auto* zoomGroup = new QActionGroup(this);
    zoomGroup->setExclusive(true);
    m_zoom1xAction = addZoomAction(zoomMenu, zoomGroup, QStringLiteral("1x"), 1.0);
    m_zoom15xAction = addZoomAction(zoomMenu, zoomGroup, QStringLiteral("1.5x"), 1.5);
    m_zoom2xAction = addZoomAction(zoomMenu, zoomGroup, QStringLiteral("2x"), 2.0);
    m_zoomCustomAction = zoomMenu->addAction(QStringLiteral("Custom"));
    m_zoomCustomAction->setActionGroup(zoomGroup);
    m_zoomCustomAction->setCheckable(true);
    connect(m_zoomCustomAction, &QAction::triggered, this, [this]() { setCustomZoom(); });

    auto* mediaMenu = menuBar()->addMenu(QStringLiteral("Media"));
    if (!m_configuration.iwmDevices.isEmpty()) {
        ensureFloppyDriveCount(m_configuration);
        for (int drive = 0; drive < 2; ++drive) {
            auto* floppyMenu = mediaMenu->addMenu(floppyDriveName(drive));
            floppyMenu->addAction(QStringLiteral("Insert Image..."), this, [this, drive]() { insertFloppyFromToolbar(drive); });
            floppyMenu->addAction(QStringLiteral("Eject Image"), this, [this, drive]() { ejectFloppy(drive); });
        }
    }

}

QAction* EmulatorWindow::addZoomAction(QMenu* menu, QActionGroup* group, const QString& text, double factor)
{
    auto* action = menu->addAction(text);
    action->setActionGroup(group);
    action->setCheckable(true);
    connect(action, &QAction::triggered, this, [this, factor]() { setZoom(factor); });
    return action;
}

void EmulatorWindow::setZoom(double factor)
{
    m_display->setZoomFactor(factor);
    if (factor == 1.0) m_zoom1xAction->setChecked(true);
    else if (factor == 1.5) m_zoom15xAction->setChecked(true);
    else if (factor == 2.0) m_zoom2xAction->setChecked(true);

    const auto framebuffer = m_display->framebufferSize();
    const QSize desiredDisplaySize(qRound(framebuffer.width() * factor), qRound(framebuffer.height() * factor));
    m_zoomResizePending = true;
    resize(size() + desiredDisplaySize - m_display->size());
    QTimer::singleShot(0, this, [this]() { m_zoomResizePending = false; });
}

void EmulatorWindow::setCustomZoom()
{
    m_display->setZoomFactor(0.0);
    if (m_zoomCustomAction != nullptr) m_zoomCustomAction->setChecked(true);
}

void EmulatorWindow::buildToolbar()
{
    if (m_toolbar == nullptr) {
        m_toolbar = addToolBar(QStringLiteral("Machine"));
        m_toolbar->setMovable(false);
    }
    m_toolbar->clear();

    m_pauseAction = m_toolbar->addAction(style()->standardIcon(m_paused ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause),
        m_paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    m_pauseAction->setToolTip(m_paused ? QStringLiteral("Resume emulation") : QStringLiteral("Pause emulation"));
    connect(m_pauseAction, &QAction::triggered, this, [this]() { setPaused(!m_paused); });
    auto* reset = m_toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Reset"));
    reset->setToolTip(QStringLiteral("Reset the emulated machine"));
    connect(reset, &QAction::triggered, this, [this]() { loadAndReset(); });
    auto* interrupt = m_toolbar->addAction(style()->standardIcon(QStyle::SP_MessageBoxInformation), QStringLiteral("Interrupt"));
    interrupt->setToolTip(QStringLiteral("Trigger the classic programmer's interrupt for guest debuggers such as MacsBug"));
    connect(interrupt, &QAction::triggered, this, [this]() {
        if (m_session.triggerProgrammersInterrupt()) {
            statusBar()->showMessage(QStringLiteral("Programmer interrupt triggered"), 2000);
        } else {
            statusBar()->showMessage(QStringLiteral("Programmer interrupt is unsupported for this machine"), 4000);
        }
    });
    m_toolbar->addSeparator();

    if (!m_configuration.iwmDevices.isEmpty()) {
        ensureFloppyDriveCount(m_configuration);
        for (int drive = 0; drive < 2; ++drive) {
            if (!m_configuration.iwmDevices[drive].imagePath.isEmpty())
                rememberRecentImage(cutemac::storage::DiskImageType::Floppy, m_configuration.iwmDevices[drive].imagePath);
            auto* floppy = new QToolButton(m_toolbar);
            floppy->setIcon(style()->standardIcon(QStyle::SP_DriveFDIcon));
            const auto imageName = m_configuration.iwmDevices[drive].imagePath.isEmpty()
                ? QStringLiteral("empty")
                : QFileInfo(m_configuration.iwmDevices[drive].imagePath).fileName();
            floppy->setToolTip(QStringLiteral("%1: %2").arg(floppyDriveName(drive), imageName));
            floppy->setText(drive == 0 ? QStringLiteral("FD1") : QStringLiteral("FD2"));
            floppy->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            floppy->setPopupMode(QToolButton::InstantPopup);
            auto* menu = new QMenu(floppy);
            menu->addAction(QStringLiteral("Insert..."), this, [this, drive]() { insertFloppyFromToolbar(drive); });
            const auto recentFloppies = recentImages(cutemac::storage::DiskImageType::Floppy);
            if (!recentFloppies.isEmpty()) {
                auto* recent = menu->addMenu(QStringLiteral("Recent Images"));
                for (const auto& path : recentFloppies)
                    recent->addAction(QFileInfo(path).fileName(), this, [this, drive, path]() { insertFloppyPath(drive, path); });
            }
            menu->addAction(QStringLiteral("Eject"), this, [this, drive]() { ejectFloppy(drive); });
            menu->addAction(QStringLiteral("New 800K Image..."), this, [this, drive]() { createFloppyFromToolbar(drive, 800LL * 1024); });
            menu->addAction(QStringLiteral("New 1.44 MB Image..."), this, [this, drive]() { createFloppyFromToolbar(drive, 1440LL * 1024); });
            menu->addSeparator();
            auto* readOnly = menu->addAction(QStringLiteral("Read-only"));
            readOnly->setCheckable(true);
            readOnly->setChecked(m_configuration.iwmDevices[drive].readOnly);
            connect(readOnly, &QAction::toggled, this, [this, drive](bool enabled) {
                m_configuration.iwmDevices[drive].readOnly = enabled;
                saveConfiguration();
                statusBar()->showMessage(QStringLiteral("Floppy access mode will apply when the media is next inserted"), 3000);
            });
            floppy->setMenu(menu);
            m_toolbar->addWidget(floppy);
        }
    }

    for (const auto& device : m_configuration.scsiDevices) {
        auto* disk = new QToolButton(m_toolbar);
        const bool cdRom = device.type == cutemac::config::ScsiDeviceType::CdRom;
        if (cdRom && !device.imagePath.isEmpty())
            rememberRecentImage(cutemac::storage::DiskImageType::CdRom, device.imagePath);
        disk->setIcon(style()->standardIcon(cdRom ? QStyle::SP_DriveCDIcon : QStyle::SP_DriveHDIcon));
        disk->setToolTip(QStringLiteral("SCSI ID %1 %2").arg(device.id).arg(cdRom ? QStringLiteral("CD-ROM") : QStringLiteral("hard disk")));
        disk->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(disk);
        menu->addAction(device.imagePath.isEmpty() ? QStringLiteral("Insert...") : QStringLiteral("Change Image..."), this,
            [this, id = device.id, type = device.type]() { insertScsiFromToolbar(id, type); });
        if (cdRom) {
            const auto recentCds = recentImages(cutemac::storage::DiskImageType::CdRom);
            if (!recentCds.isEmpty()) {
                auto* recent = menu->addMenu(QStringLiteral("Recent Images"));
                for (const auto& path : recentCds)
                    recent->addAction(QFileInfo(path).fileName(), this,
                        [this, id = device.id, type = device.type, path]() { insertScsiPath(id, type, path); });
            }
        }
        if (cdRom) {
            auto* eject = menu->addAction(QStringLiteral("Eject"), this, [this, id = device.id]() { ejectScsiFromToolbar(id); });
            eject->setEnabled(!device.imagePath.isEmpty());
        }
        if (!cdRom) {
            menu->addAction(QStringLiteral("New Hard Disk Image..."), this,
                [this, id = device.id]() { createHardDiskFromToolbar(id); });
            menu->addSeparator();
            auto* readOnly = menu->addAction(QStringLiteral("Read-only"));
            readOnly->setCheckable(true);
            readOnly->setChecked(device.readOnly);
            connect(readOnly, &QAction::toggled, this, [this, id = device.id](bool enabled) { setScsiReadOnly(id, enabled); });
        }
        menu->addSeparator();
        menu->addAction(QStringLiteral("Details..."), this, [this, id = device.id]() { showScsiDetails(id); });
        disk->setMenu(menu);
        m_toolbar->addWidget(disk);
    }
}

void EmulatorWindow::insertScsiFromToolbar(int id, cutemac::config::ScsiDeviceType type)
{
    const bool cdRom = type == cutemac::config::ScsiDeviceType::CdRom;
    const auto imageType = cdRom ? cutemac::storage::DiskImageType::CdRom : cutemac::storage::DiskImageType::HardDisk;
    const auto path = cutemac::ui::DiskImagePickerDialog::getImage(imageType,
        cdRom ? QStringLiteral("Insert CD-ROM Image") : QStringLiteral("Insert Hard Disk Image"), this);
    if (path.isEmpty()) return;
    insertScsiPath(id, type, path);
}

void EmulatorWindow::insertScsiPath(int id, cutemac::config::ScsiDeviceType type, const QString& path)
{
    const bool cdRom = type == cutemac::config::ScsiDeviceType::CdRom;
    auto iterator = std::find_if(m_configuration.scsiDevices.begin(), m_configuration.scsiDevices.end(),
        [id](const auto& device) { return device.id == id; });
    if (iterator == m_configuration.scsiDevices.end()) return;
    if (!m_session.insertScsiDevice(id, type, path, iterator->readOnly)) {
        statusBar()->showMessage(QStringLiteral("Failed to insert SCSI image"), 3000);
        return;
    }
    iterator->imagePath = path;
    iterator->readOnly = cdRom || iterator->readOnly;
    if (cdRom) rememberRecentImage(cutemac::storage::DiskImageType::CdRom, path);
    saveConfiguration();
    QTimer::singleShot(0, this, [this]() { buildToolbar(); });
    updateStatus();
}

void EmulatorWindow::ejectScsiFromToolbar(int id)
{
    const auto iterator = std::find_if(m_configuration.scsiDevices.begin(), m_configuration.scsiDevices.end(),
        [id](const auto& device) { return device.id == id; });
    if (iterator == m_configuration.scsiDevices.end()
        || iterator->type != cutemac::config::ScsiDeviceType::CdRom) return;
    m_session.ejectScsiDevice(id);
    iterator->imagePath.clear();
    saveConfiguration();
    QTimer::singleShot(0, this, [this]() { buildToolbar(); });
    updateStatus();
}

void EmulatorWindow::createHardDiskFromToolbar(int id)
{
    const auto path = cutemac::ui::createDiskImage(cutemac::storage::DiskImageType::HardDisk, this);
    if (path.isEmpty()) return;
    auto iterator = std::find_if(m_configuration.scsiDevices.begin(), m_configuration.scsiDevices.end(),
        [id](const auto& device) { return device.id == id; });
    if (iterator == m_configuration.scsiDevices.end()) return;
    if (!m_session.insertScsiDevice(id, iterator->type, path, iterator->readOnly)) {
        statusBar()->showMessage(QStringLiteral("Failed to insert new hard disk image"), 3000);
        return;
    }
    iterator->imagePath = path;
    saveConfiguration();
    QTimer::singleShot(0, this, [this]() { buildToolbar(); });
    updateStatus();
}

void EmulatorWindow::setScsiReadOnly(int id, bool enabled)
{
    const auto iterator = std::find_if(m_configuration.scsiDevices.begin(), m_configuration.scsiDevices.end(),
        [id](const auto& device) { return device.id == id; });
    if (iterator == m_configuration.scsiDevices.end()) return;
    iterator->readOnly = enabled;
    if (!iterator->imagePath.isEmpty()) (void)m_session.insertScsiDevice(id, iterator->type, iterator->imagePath, enabled);
    saveConfiguration();
}

void EmulatorWindow::showScsiDetails(int id)
{
    const auto iterator = std::find_if(m_configuration.scsiDevices.cbegin(), m_configuration.scsiDevices.cend(),
        [id](const auto& device) { return device.id == id; });
    if (iterator == m_configuration.scsiDevices.cend()) return;
    const bool cdRom = iterator->type == cutemac::config::ScsiDeviceType::CdRom;
    QMessageBox::information(this, cdRom ? QStringLiteral("SCSI CD-ROM") : QStringLiteral("SCSI Hard Disk"),
        QStringLiteral("SCSI ID: %1\nType: %2\nImage: %3\nAccess: %4")
            .arg(iterator->id)
            .arg(cdRom ? QStringLiteral("CD-ROM") : QStringLiteral("Hard disk"))
            .arg(iterator->imagePath.isEmpty() ? QStringLiteral("No image") : iterator->imagePath)
            .arg(cdRom || iterator->readOnly ? QStringLiteral("Read-only") : QStringLiteral("Read/write")));
}

void EmulatorWindow::insertFloppyFromToolbar(int drive)
{
    const auto path = cutemac::ui::DiskImagePickerDialog::getImage(cutemac::storage::DiskImageType::Floppy,
        QStringLiteral("Insert %1 Image").arg(floppyDriveName(drive)), this);
    if (path.isEmpty()) return;
    insertFloppyPath(drive, path);
}

void EmulatorWindow::insertFloppyPath(int drive, const QString& path)
{
    if (drive < 0 || drive >= 2 || m_configuration.iwmDevices.isEmpty()) return;
    ensureFloppyDriveCount(m_configuration);
    const auto previousPath = m_configuration.iwmDevices[drive].imagePath;
    m_configuration.iwmDevices[drive].imagePath = path;
    if (drive == 0) m_configuration.floppyPath = path;
    if (!m_session.insertFloppy(drive, path, m_configuration.iwmDevices[drive].readOnly)) {
        m_configuration.iwmDevices[drive].imagePath = previousPath;
        if (drive == 0) m_configuration.floppyPath = previousPath;
        statusBar()->showMessage(QStringLiteral("Failed to load floppy image"), 3000);
        return;
    }
    rememberRecentImage(cutemac::storage::DiskImageType::Floppy, path);
    saveConfiguration();
    QTimer::singleShot(0, this, [this]() { buildToolbar(); });
    updateStatus();
}

QStringList EmulatorWindow::recentImages(cutemac::storage::DiskImageType type) const
{
    QSettings settings;
    const auto key = type == cutemac::storage::DiskImageType::Floppy
        ? QStringLiteral("recentMedia/floppy") : QStringLiteral("recentMedia/cdRom");
    auto paths = settings.value(key).toStringList();
    paths.erase(std::remove_if(paths.begin(), paths.end(), [](const auto& path) { return !QFileInfo::exists(path); }), paths.end());
    if (paths.size() > 10) paths = paths.mid(0, 10);
    return paths;
}

void EmulatorWindow::rememberRecentImage(cutemac::storage::DiskImageType type, const QString& path)
{
    auto paths = recentImages(type);
    const auto absolutePath = QFileInfo(path).absoluteFilePath();
    paths.removeAll(absolutePath);
    paths.prepend(absolutePath);
    if (paths.size() > 10) paths = paths.mid(0, 10);
    QSettings settings;
    settings.setValue(type == cutemac::storage::DiskImageType::Floppy
            ? QStringLiteral("recentMedia/floppy") : QStringLiteral("recentMedia/cdRom"), paths);
}

void EmulatorWindow::ejectFloppy(int drive)
{
    if (drive < 0 || drive >= 2 || m_configuration.iwmDevices.isEmpty()) return;
    ensureFloppyDriveCount(m_configuration);
    m_configuration.iwmDevices[drive].imagePath.clear();
    if (drive == 0) m_configuration.floppyPath.clear();
    m_session.ejectFloppy(drive);
    saveConfiguration();
    QTimer::singleShot(0, this, [this]() { buildToolbar(); });
    updateStatus();
}

void EmulatorWindow::createFloppyFromToolbar(int drive, qint64 sizeBytes)
{
    const auto path = cutemac::ui::createDiskImage(cutemac::storage::DiskImageType::Floppy, this, sizeBytes);
    if (path.isEmpty()) return;
    insertFloppyPath(drive, path);
}

void EmulatorWindow::buildStatusBar()
{
    m_status = new QLabel;
    statusBar()->addPermanentWidget(m_status, 1);
    updateStatus();
}

void EmulatorWindow::loadAndReset()
{
    setPaused(true);
    m_runner.setCyclesPerFrame(m_configuration.cyclesPerFrame);
    m_runner.setSpeed(m_configuration.runtimeSpeed);
    updateSpeedActions();
    m_romLoaded = m_session.reconfigure(m_configuration);
    buildToolbar();
    if (m_romLoaded) {
        m_display->setFramebuffer(m_session.videoFrame());
        statusBar()->showMessage(QStringLiteral("ROM loaded"), 2000);
    } else {
        statusBar()->showMessage(QStringLiteral("ROM not loaded"));
    }
    setPaused(!m_romLoaded);
    updateStatus();
}

void EmulatorWindow::setRuntimeSpeed(cutemac::config::RuntimeSpeed speed)
{
    m_configuration.runtimeSpeed = speed;
    m_runner.setSpeed(speed);
    saveConfiguration();
    updateSpeedActions();
    updateStatus();
}

void EmulatorWindow::updateSpeedActions()
{
    if (m_realtimeSpeedAction != nullptr) {
        m_realtimeSpeedAction->setChecked(m_runner.speed() == cutemac::config::RuntimeSpeed::Realtime);
        m_unlimitedSpeedAction->setChecked(m_runner.speed() == cutemac::config::RuntimeSpeed::Unlimited);
    }
}

void EmulatorWindow::configureMachine()
{
    const auto wasPaused = m_paused;
    m_display->releasePointerInput();
    setPaused(true);

    cutemac::ui::ConfigurationDialog dialog(m_configuration, this);
    if (dialog.exec() == QDialog::Accepted) {
        const auto proposed = dialog.configuration();
        const bool resetRequired = dialog.nvramZapped() || requiresMachineReset(m_configuration, proposed);
        if (!resetRequired || QMessageBox::question(this, QStringLiteral("Reset Required"),
                                  QStringLiteral("Applying these hardware or media changes will reset the emulated Macintosh. Continue?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                == QMessageBox::Yes) {
            m_configuration = proposed;
            setWindowTitle(QStringLiteral("CuteMac - %1").arg(m_configuration.profileName));
            saveConfiguration();
            if (resetRequired) {
                loadAndReset();
            } else {
                m_runner.setSpeed(m_configuration.runtimeSpeed);
                updateSpeedActions();
                updateStatus();
            }
        }
    }

    if (m_romLoaded) setPaused(wasPaused);
}

void EmulatorWindow::saveConfiguration()
{
    if (m_profilePath.isEmpty()) {
        m_profilePath = cutemac::config::ConfigurationManager().profilePathForName(m_configuration.profileName);
    }
    if (!cutemac::config::ConfigurationManager().saveTomlFile(m_profilePath, m_configuration)) {
        statusBar()->showMessage(QStringLiteral("Failed to save profile"), 4000);
    }
}

void EmulatorWindow::setPaused(bool paused)
{
    m_paused = paused;
    m_runner.setPaused(m_paused || !m_romLoaded);
    if (m_paused || !m_romLoaded) m_frameTimer.stop(); else m_frameTimer.start();
    m_display->setRunning(!m_paused && m_romLoaded);
    m_audioOutput.setPaused(m_paused || !m_romLoaded);
    if (m_paused || !m_romLoaded) {
        m_audioPacingFrames = 0;
        m_runner.setAudioPlaybackActive(false);
    }
    if (m_pauseAction != nullptr) {
        m_pauseAction->setIcon(style()->standardIcon(m_paused ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
        m_pauseAction->setText(m_paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
        m_pauseAction->setToolTip(m_paused ? QStringLiteral("Resume emulation") : QStringLiteral("Pause emulation"));
    }
    updateStatus();
}

void EmulatorWindow::runFrame()
{
    if (!m_romLoaded || m_paused) {
        return;
    }

    m_runner.runHostFrame();
    m_display->setFramebuffer(m_session.videoFrame());
    if (m_audioOutput.enqueue(m_session.takeAudioFrame())) {
        // Keep the emulated sample clock at wall-clock rate while sound is
        // audible. The short tail prevents zero crossings and queued sink
        // audio from repeatedly switching unlimited execution back on.
        m_audioPacingFrames = 8;
    } else if (m_audioPacingFrames > 0) {
        --m_audioPacingFrames;
    }
    m_runner.setAudioPlaybackActive(m_audioPacingFrames > 0);
    switch (m_session.takePowerRequest()) {
    case cutemac::core::GuestPowerRequest::PowerOff:
        setPaused(true);
        // The card defers this request until the guest has rendered its
        // final shutdown screen. Keep that last framebuffer visible long
        // enough to be observed even when emulation was unrestricted.
        QTimer::singleShot(1000, this, &QWidget::close);
        return;
    case cutemac::core::GuestPowerRequest::Restart:
        loadAndReset();
        return;
    case cutemac::core::GuestPowerRequest::None:
        break;
    }
    ++m_frames;
    if ((m_frames % 15) == 0) {
        updateStatus();
    }
}

void EmulatorWindow::updateStatus()
{
    if (m_status == nullptr) {
        return;
    }

    const auto state = m_session.status();
    updateSpeedActions();
    const auto floppyStatus = [this]() {
        if (m_configuration.iwmDevices.isEmpty()) return QStringLiteral("none");
        auto driveText = [](const QVector<cutemac::config::IwmDeviceConfiguration>& devices, int drive) {
            if (drive >= devices.size() || devices[drive].imagePath.isEmpty()) return QStringLiteral("empty");
            return QFileInfo(devices[drive].imagePath).fileName();
        };
        return QStringLiteral("I:%1 E:%2").arg(driveText(m_configuration.iwmDevices, 0), driveText(m_configuration.iwmDevices, 1));
    }();
    m_status->setText(QStringLiteral("%1 | %2 | %3 | PC 0x%4 | cycles %5 | overlay %6 | frames %7 | ROM %8 | disk %9 | floppy %10")
                          .arg(m_paused ? QStringLiteral("Paused") : QStringLiteral("Running"))
                          .arg(state.machineId)
                          .arg(cutemac::config::runtimeSpeedName(m_runner.speed()))
                          .arg(state.programCounter, 6, 16, QLatin1Char('0'))
                          .arg(state.cycles)
                          .arg(state.overlayEnabled ? QStringLiteral("on") : QStringLiteral("off"))
                          .arg(m_frames)
                          .arg(m_romLoaded ? QStringLiteral("loaded") : QStringLiteral("missing"))
                          .arg(m_configuration.diskPath.isEmpty() ? QStringLiteral("none") : QFileInfo(m_configuration.diskPath).fileName())
                          .arg(floppyStatus));
}

void EmulatorWindow::showProfileManager()
{
    if (m_profileManagerWindow.isNull()) return;
    m_profileManagerWindow->show();
    m_profileManagerWindow->raise();
    m_profileManagerWindow->activateWindow();
}

} // namespace cutemac::session
