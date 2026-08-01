#include <QApplication>
#include <QCommandLineParser>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
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
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

#include "cutemac/config/Configuration.h"
#include "cutemac/machines/macplus/MacPlusMachine.h"

namespace {

class DisplayWidget final : public QWidget {
public:
    explicit DisplayWidget(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_image(512, 342, QImage::Format_RGB32)
    {
        m_image.fill(Qt::white);
        setMinimumSize(512, 342);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

    void setRunning(bool running)
    {
        m_running = running;
        update();
    }

    void setFramebuffer(const QByteArray& bytes)
    {
        if (bytes.size() < (m_image.width() * m_image.height()) / 8) {
            return;
        }

        for (int y = 0; y < m_image.height(); ++y) {
            for (int x = 0; x < m_image.width(); ++x) {
                const auto byte = static_cast<std::uint8_t>(bytes[(y * m_image.width() + x) / 8]);
                const auto bit = 7 - (x & 7);
                const auto on = ((byte >> bit) & 1) != 0;
                m_image.setPixelColor(x, y, on ? Qt::black : Qt::white);
            }
        }
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

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(24, 24, 24));
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

        const auto target = displayRect();
        painter.fillRect(target.adjusted(-18, -18, 18, 18), QColor(44, 47, 49));
        painter.drawImage(target, m_image);

        painter.setPen(QColor(20, 20, 20));
        painter.drawRect(target.adjusted(0, 0, -1, -1));

        if (!m_running) {
            painter.fillRect(target, QColor(255, 255, 255, 72));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        sendMouseEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        setFocus(Qt::MouseFocusReason);
        sendMouseEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        sendMouseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        sendKeyEvent(event, true);
    }

    void keyReleaseEvent(QKeyEvent* event) override
    {
        sendKeyEvent(event, false);
    }

private:
    [[nodiscard]] QRect displayRect() const
    {
        const QSize baseSize(512, 342);
        const auto scale = std::max(1, std::min(width() / baseSize.width(), height() / baseSize.height()));
        const QSize scaled(baseSize.width() * scale, baseSize.height() * scale);
        return QRect(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
    }

    [[nodiscard]] QPoint macPointFor(const QPoint& widgetPoint) const
    {
        const auto target = displayRect();
        if (!target.contains(widgetPoint)) {
            return QPoint(std::clamp(widgetPoint.x() - target.left(), 0, target.width() - 1) * 512 / target.width(),
                std::clamp(widgetPoint.y() - target.top(), 0, target.height() - 1) * 342 / target.height());
        }

        return QPoint((widgetPoint.x() - target.left()) * 512 / target.width(),
            (widgetPoint.y() - target.top()) * 342 / target.height());
    }

    void sendMouseEvent(QMouseEvent* event)
    {
        if (!m_mouseCallback) {
            return;
        }
        const auto point = macPointFor(event->pos());
        m_mouseCallback(point.x(), point.y(), (event->buttons() & Qt::LeftButton) != 0);
    }

    void sendKeyEvent(QKeyEvent* event, bool pressed)
    {
        if (event->isAutoRepeat() || !m_keyCallback) {
            return;
        }
        const auto code = macKeyCodeFor(event);
        if (code >= 0) {
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
    QImage m_image;
    std::function<void(int, int, bool)> m_mouseCallback;
    std::function<void(int, bool)> m_keyCallback;
};

class RuntimeSettingsDialog final : public QDialog {
public:
    RuntimeSettingsDialog(cutemac::config::Configuration configuration, QWidget* parent = nullptr)
        : QDialog(parent)
        , m_configuration(std::move(configuration))
    {
        setWindowTitle(QStringLiteral("Configure Instance"));

        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;

        m_romPath = new QLineEdit(m_configuration.romPath);
        auto* romBrowse = new QPushButton(QStringLiteral("Browse..."));
        auto* romLayout = new QHBoxLayout;
        romLayout->addWidget(m_romPath, 1);
        romLayout->addWidget(romBrowse);

        m_cyclesPerFrame = new QSpinBox;
        m_cyclesPerFrame->setRange(1000, 2000000);
        m_cyclesPerFrame->setSingleStep(10000);
        m_cyclesPerFrame->setValue(m_configuration.cyclesPerFrame);

        form->addRow(QStringLiteral("ROM"), romLayout);
        form->addRow(QStringLiteral("Cycles/frame"), m_cyclesPerFrame);
        layout->addLayout(form);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);

        connect(romBrowse, &QPushButton::clicked, this, [this]() {
            const auto path = QFileDialog::getOpenFileName(this,
                QStringLiteral("Select Mac Plus ROM"),
                cutemac::config::ConfigurationManager::romDirectoryPath(),
                QStringLiteral("ROM images (*.rom *.bin);;All files (*)"));
            if (!path.isEmpty()) {
                m_romPath->setText(path);
            }
        });
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    [[nodiscard]] cutemac::config::Configuration configuration() const
    {
        auto configuration = m_configuration;
        configuration.romPath = m_romPath->text().trimmed();
        configuration.cyclesPerFrame = m_cyclesPerFrame->value();
        return configuration;
    }

private:
    cutemac::config::Configuration m_configuration;
    QLineEdit* m_romPath = nullptr;
    QSpinBox* m_cyclesPerFrame = nullptr;
};

class EmulatorWindow final : public QMainWindow {
public:
    explicit EmulatorWindow(cutemac::config::Configuration configuration)
        : m_configuration(std::move(configuration))
        , m_machine(static_cast<std::size_t>(std::max(1, m_configuration.ramSizeMiB)) * 1024 * 1024)
    {
        setWindowTitle(QStringLiteral("CuteMac - %1").arg(m_configuration.profileName));
        resize(1120, 820);

        m_display = new DisplayWidget;
        m_display->setMouseCallback([this](int x, int y, bool pressed) {
            m_machine.setMousePosition(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y));
            m_machine.setMouseButton(pressed);
        });
        m_display->setKeyCallback([this](int keyCode, bool pressed) {
            m_machine.setKeyState(static_cast<std::uint8_t>(keyCode), pressed);
        });
        setCentralWidget(m_display);

        buildMenus();
        buildStatusBar();

        m_frameTimer.setInterval(16);
        connect(&m_frameTimer, &QTimer::timeout, this, [this]() { runFrame(); });

        loadAndReset();
        setPaused(false);
    }

private:
    void buildMenus()
    {
        auto* machineMenu = menuBar()->addMenu(QStringLiteral("Machine"));
        machineMenu->addAction(QStringLiteral("Pause/Resume"), this, [this]() { setPaused(!m_paused); });
        machineMenu->addAction(QStringLiteral("Reset"), this, [this]() { loadAndReset(); });
        machineMenu->addAction(QStringLiteral("Configure Running Instance"), this, [this]() { configureRunningInstance(); });
        machineMenu->addSeparator();
        machineMenu->addAction(QStringLiteral("Close"), this, &QWidget::close);

        auto* mediaMenu = menuBar()->addMenu(QStringLiteral("Media"));
        mediaMenu->addAction(QStringLiteral("Insert Disk Image"), this, [this]() {
            const auto path = QFileDialog::getOpenFileName(this,
                QStringLiteral("Insert Disk Image"),
                cutemac::config::ConfigurationManager::diskImageDirectoryPath(),
                QStringLiteral("Disk images (*.dsk *.img *.image);;All files (*)"));
            if (!path.isEmpty()) {
                m_configuration.diskPath = path;
                if (!m_machine.loadDiskImage(path)) {
                    statusBar()->showMessage(QStringLiteral("Failed to load disk image"), 3000);
                }
                updateStatus();
            }
        });
        mediaMenu->addAction(QStringLiteral("Eject Disk Image"), this, [this]() {
            m_configuration.diskPath.clear();
            m_machine.ejectDiskImage();
            updateStatus();
        });
        mediaMenu->addSeparator();
        mediaMenu->addAction(QStringLiteral("Insert Floppy Image"), this, [this]() {
            const auto path = QFileDialog::getOpenFileName(this,
                QStringLiteral("Insert Floppy Image"),
                cutemac::config::ConfigurationManager::diskImageDirectoryPath(),
                QStringLiteral("Floppy images (*.dsk *.img *.image *.dc42);;All files (*)"));
            if (!path.isEmpty()) {
                m_configuration.floppyPath = path;
                if (!m_machine.loadFloppyImage(path)) {
                    statusBar()->showMessage(QStringLiteral("Failed to load floppy image"), 3000);
                }
                updateStatus();
            }
        });
        mediaMenu->addAction(QStringLiteral("Eject Floppy Image"), this, [this]() {
            m_configuration.floppyPath.clear();
            m_machine.ejectFloppyImage();
            updateStatus();
        });

        auto* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
        viewMenu->addAction(QStringLiteral("Actual Size"), this, [this]() { resize(620, 480); });
        viewMenu->addAction(QStringLiteral("Double Size"), this, [this]() { resize(1120, 820); });
    }

    void buildStatusBar()
    {
        m_status = new QLabel;
        statusBar()->addPermanentWidget(m_status, 1);
        updateStatus();
    }

    void loadAndReset()
    {
        setPaused(true);
        m_romLoaded = !m_configuration.romPath.isEmpty() && m_machine.loadRomFile(m_configuration.romPath);
        if (!m_configuration.diskPath.isEmpty()) {
            (void)m_machine.loadDiskImage(m_configuration.diskPath);
        }
        if (!m_configuration.floppyPath.isEmpty()) {
            (void)m_machine.loadFloppyImage(m_configuration.floppyPath);
        }
        if (m_romLoaded) {
            m_machine.reset();
            m_display->setFramebuffer(m_machine.framebufferBytes());
            statusBar()->showMessage(QStringLiteral("ROM loaded"), 2000);
        } else {
            statusBar()->showMessage(QStringLiteral("ROM not loaded"));
        }
        setPaused(!m_romLoaded);
        updateStatus();
    }

    void configureRunningInstance()
    {
        const auto wasPaused = m_paused;
        setPaused(true);

        RuntimeSettingsDialog dialog(m_configuration, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_configuration = dialog.configuration();
            loadAndReset();
        }

        if (!wasPaused && m_romLoaded) {
            setPaused(false);
        }
    }

    void setPaused(bool paused)
    {
        m_paused = paused;
        if (m_paused || !m_romLoaded) {
            m_frameTimer.stop();
        } else {
            m_frameTimer.start();
        }
        m_display->setRunning(!m_paused && m_romLoaded);
        updateStatus();
    }

    void runFrame()
    {
        if (!m_romLoaded || m_paused) {
            return;
        }

        (void)m_machine.runCycles(m_configuration.cyclesPerFrame);
        m_display->setFramebuffer(m_machine.framebufferBytes());
        ++m_frames;
        if ((m_frames % 15) == 0) {
            updateStatus();
        }
    }

    void updateStatus()
    {
        if (m_status == nullptr) {
            return;
        }

        const auto& summary = m_machine.accessSummary();
        m_status->setText(QStringLiteral("%1 | PC 0x%2 | overlay %3 | frames %4 | ROM %5 | disk %6 | floppy %7 | unmapped %8/%9")
                              .arg(m_paused ? QStringLiteral("Paused") : QStringLiteral("Running"))
                              .arg(m_machine.programCounter(), 6, 16, QLatin1Char('0'))
                              .arg(m_machine.overlayEnabled() ? QStringLiteral("on") : QStringLiteral("off"))
                              .arg(m_frames)
                              .arg(m_romLoaded ? QStringLiteral("loaded") : QStringLiteral("missing"))
                              .arg(m_configuration.diskPath.isEmpty() ? QStringLiteral("none") : QFileInfo(m_configuration.diskPath).fileName())
                              .arg(m_configuration.floppyPath.isEmpty() ? QStringLiteral("none") : QFileInfo(m_configuration.floppyPath).fileName())
                              .arg(summary.unmappedReads)
                              .arg(summary.unmappedWrites));
    }

    cutemac::config::Configuration m_configuration;
    cutemac::machines::macplus::MacPlusMachine m_machine;
    DisplayWidget* m_display = nullptr;
    QLabel* m_status = nullptr;
    QTimer m_frameTimer;
    bool m_romLoaded = false;
    bool m_paused = true;
    quint64 m_frames = 0;
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("friedkiwi"));
    QCoreApplication::setApplicationName(QStringLiteral("CuteMac"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption profileOption(QStringLiteral("profile"), QStringLiteral("Path to a CuteMac TOML profile."), QStringLiteral("path"));
    parser.addOption(profileOption);
    parser.process(app);

    cutemac::config::ConfigurationManager manager;
    cutemac::config::Configuration configuration = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
    if (parser.isSet(profileOption)) {
        const auto loaded = manager.loadTomlFile(parser.value(profileOption));
        if (loaded.has_value()) {
            configuration = *loaded;
        }
    }

    EmulatorWindow window(configuration);
    window.show();

    return app.exec();
}
