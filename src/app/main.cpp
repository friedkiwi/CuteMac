#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#if !defined(Q_OS_WASM)
#include <QProcess>
#endif

#include "cutemac/config/Configuration.h"

namespace {

class ProfileDialog final : public QDialog {
public:
    explicit ProfileDialog(cutemac::config::Configuration configuration, QWidget* parent = nullptr)
        : QDialog(parent)
        , m_configuration(std::move(configuration))
    {
        setWindowTitle(QStringLiteral("Profile"));

        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;

        m_name = new QLineEdit(m_configuration.profileName);
        m_machine = new QComboBox;
        m_machine->addItem(QStringLiteral("Macintosh Plus"), QStringLiteral("mac-plus"));
        m_machine->setCurrentIndex(0);

        m_romPath = new QLineEdit(m_configuration.romPath);
        auto* romBrowse = new QPushButton(QStringLiteral("Browse..."));
        auto* romLayout = new QHBoxLayout;
        romLayout->addWidget(m_romPath, 1);
        romLayout->addWidget(romBrowse);

        m_diskPath = new QLineEdit(m_configuration.diskPath);
        auto* diskBrowse = new QPushButton(QStringLiteral("Browse..."));
        auto* diskLayout = new QHBoxLayout;
        diskLayout->addWidget(m_diskPath, 1);
        diskLayout->addWidget(diskBrowse);

        m_floppyPath = new QLineEdit(m_configuration.floppyPath);
        auto* floppyBrowse = new QPushButton(QStringLiteral("Browse..."));
        auto* floppyLayout = new QHBoxLayout;
        floppyLayout->addWidget(m_floppyPath, 1);
        floppyLayout->addWidget(floppyBrowse);

        m_ramSize = new QSpinBox;
        m_ramSize->setRange(1, 4);
        m_ramSize->setSuffix(QStringLiteral(" MiB"));
        m_ramSize->setValue(m_configuration.ramSizeMiB);

        m_cyclesPerFrame = new QSpinBox;
        m_cyclesPerFrame->setRange(1000, 2000000);
        m_cyclesPerFrame->setSingleStep(10000);
        m_cyclesPerFrame->setValue(m_configuration.cyclesPerFrame);

        m_skipRamPatternTest = new QCheckBox;
        m_skipRamPatternTest->setChecked(m_configuration.skipRamPatternTest);

        form->addRow(QStringLiteral("Name"), m_name);
        form->addRow(QStringLiteral("Machine"), m_machine);
        form->addRow(QStringLiteral("ROM"), romLayout);
        form->addRow(QStringLiteral("Disk image"), diskLayout);
        form->addRow(QStringLiteral("Floppy image"), floppyLayout);
        form->addRow(QStringLiteral("RAM"), m_ramSize);
        form->addRow(QStringLiteral("Cycles/frame"), m_cyclesPerFrame);
        form->addRow(QStringLiteral("Skip RAM pattern test"), m_skipRamPatternTest);
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
        connect(diskBrowse, &QPushButton::clicked, this, [this]() {
            const auto path = QFileDialog::getOpenFileName(this,
                QStringLiteral("Select disk image"),
                cutemac::config::ConfigurationManager::diskImageDirectoryPath(),
                QStringLiteral("Disk images (*.dsk *.img *.image);;All files (*)"));
            if (!path.isEmpty()) {
                m_diskPath->setText(path);
            }
        });
        connect(floppyBrowse, &QPushButton::clicked, this, [this]() {
            const auto path = QFileDialog::getOpenFileName(this,
                QStringLiteral("Select floppy image"),
                cutemac::config::ConfigurationManager::diskImageDirectoryPath(),
                QStringLiteral("Floppy images (*.dsk *.img *.image *.dc42);;All files (*)"));
            if (!path.isEmpty()) {
                m_floppyPath->setText(path);
            }
        });
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (m_name->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Profile"), QStringLiteral("Profile name is required."));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    [[nodiscard]] cutemac::config::Configuration configuration() const
    {
        auto configuration = m_configuration;
        configuration.profileName = m_name->text().trimmed();
        configuration.machineId = m_machine->currentData().toString();
        configuration.romPath = m_romPath->text().trimmed();
        configuration.diskPath = m_diskPath->text().trimmed();
        configuration.floppyPath = m_floppyPath->text().trimmed();
        configuration.ramSizeMiB = m_ramSize->value();
        configuration.cyclesPerFrame = m_cyclesPerFrame->value();
        configuration.skipRamPatternTest = m_skipRamPatternTest->isChecked();
        return configuration;
    }

private:
    cutemac::config::Configuration m_configuration;
    QLineEdit* m_name = nullptr;
    QComboBox* m_machine = nullptr;
    QLineEdit* m_romPath = nullptr;
    QLineEdit* m_diskPath = nullptr;
    QLineEdit* m_floppyPath = nullptr;
    QSpinBox* m_ramSize = nullptr;
    QSpinBox* m_cyclesPerFrame = nullptr;
    QCheckBox* m_skipRamPatternTest = nullptr;
};

struct ProfileRow {
    QString path;
    cutemac::config::Configuration configuration;
};

class ProfileManagerWindow final : public QMainWindow {
public:
    ProfileManagerWindow()
    {
        setWindowTitle(QStringLiteral("CuteMac"));
        resize(900, 520);

        (void)m_manager.ensureDirectories();
        ensureDefaultProfile();
        buildUi();
        loadProfiles();
    }

private:
    void buildUi()
    {
        auto* central = new QWidget;
        auto* layout = new QVBoxLayout(central);

        auto* headerLayout = new QHBoxLayout;
        auto* title = new QLabel(QStringLiteral("Profiles"));
        QFont titleFont = title->font();
        titleFont.setPointSize(titleFont.pointSize() + 6);
        titleFont.setBold(true);
        title->setFont(titleFont);
        headerLayout->addWidget(title);
        headerLayout->addStretch();
        layout->addLayout(headerLayout);

        m_table = new QTableWidget;
        m_table->setColumnCount(6);
        m_table->setHorizontalHeaderLabels({
            QStringLiteral("Name"),
            QStringLiteral("Machine"),
            QStringLiteral("ROM"),
            QStringLiteral("Disk"),
            QStringLiteral("Floppy"),
            QStringLiteral("Profile file"),
        });
        m_table->horizontalHeader()->setStretchLastSection(true);
        m_table->verticalHeader()->hide();
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(m_table, 1);

        auto* buttonLayout = new QHBoxLayout;
        auto* startButton = new QPushButton(QStringLiteral("Start"));
        auto* newButton = new QPushButton(QStringLiteral("New"));
        auto* editButton = new QPushButton(QStringLiteral("Configure"));
        auto* cloneButton = new QPushButton(QStringLiteral("Clone"));
        auto* deleteButton = new QPushButton(QStringLiteral("Delete"));
        auto* refreshButton = new QPushButton(QStringLiteral("Refresh"));
        buttonLayout->addWidget(startButton);
        buttonLayout->addWidget(newButton);
        buttonLayout->addWidget(editButton);
        buttonLayout->addWidget(cloneButton);
        buttonLayout->addWidget(deleteButton);
        buttonLayout->addStretch();
        buttonLayout->addWidget(refreshButton);
        layout->addLayout(buttonLayout);

        setCentralWidget(central);

        auto* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
        fileMenu->addAction(QStringLiteral("New Profile"), this, [this]() { createProfile(); });
        fileMenu->addAction(QStringLiteral("Open Profile Folder"), this, [this]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(cutemac::config::ConfigurationManager::profileDirectoryPath()));
        });
        fileMenu->addSeparator();
        fileMenu->addAction(QStringLiteral("Quit"), this, &QWidget::close);

        auto* toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
        toolsMenu->addAction(QStringLiteral("Open ROM Folder"), this, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(cutemac::config::ConfigurationManager::romDirectoryPath()));
        });
        toolsMenu->addAction(QStringLiteral("Open Disk Image Folder"), this, []() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(cutemac::config::ConfigurationManager::diskImageDirectoryPath()));
        });

        connect(startButton, &QPushButton::clicked, this, [this]() { startSelectedProfile(); });
        connect(newButton, &QPushButton::clicked, this, [this]() { createProfile(); });
        connect(editButton, &QPushButton::clicked, this, [this]() { editSelectedProfile(); });
        connect(cloneButton, &QPushButton::clicked, this, [this]() { cloneSelectedProfile(); });
        connect(deleteButton, &QPushButton::clicked, this, [this]() { deleteSelectedProfile(); });
        connect(refreshButton, &QPushButton::clicked, this, [this]() { loadProfiles(); });
        connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { startSelectedProfile(); });
    }

    void ensureDefaultProfile()
    {
        if (!m_manager.profileFilePaths().isEmpty()) {
            return;
        }
        const auto configuration = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
        (void)m_manager.saveTomlFile(m_manager.profilePathForName(configuration.profileName), configuration);
    }

    void loadProfiles()
    {
        m_profiles.clear();
        for (const auto& path : m_manager.profileFilePaths()) {
            const auto configuration = m_manager.loadTomlFile(path);
            if (configuration.has_value()) {
                m_profiles.append({ path, *configuration });
            }
        }

        m_table->setRowCount(m_profiles.size());
        for (int row = 0; row < m_profiles.size(); ++row) {
            const auto& profile = m_profiles[row];
            setItem(row, 0, profile.configuration.profileName);
            setItem(row, 1, profile.configuration.machineId);
            setItem(row, 2, compactPath(profile.configuration.romPath));
            setItem(row, 3, compactPath(profile.configuration.diskPath));
            setItem(row, 4, compactPath(profile.configuration.floppyPath));
            setItem(row, 5, profile.path);
        }
        m_table->resizeColumnsToContents();
        if (!m_profiles.isEmpty()) {
            m_table->selectRow(0);
        }
    }

    void setItem(int row, int column, const QString& text)
    {
        auto* item = new QTableWidgetItem(text);
        item->setToolTip(text);
        m_table->setItem(row, column, item);
    }

    [[nodiscard]] QString compactPath(const QString& path) const
    {
        return path.isEmpty() ? QStringLiteral("(not set)") : QFileInfo(path).fileName();
    }

    [[nodiscard]] int selectedRow() const
    {
        const auto selected = m_table->selectionModel()->selectedRows();
        return selected.isEmpty() ? -1 : selected.first().row();
    }

    void createProfile()
    {
        ProfileDialog dialog(cutemac::config::ConfigurationManager::defaultMacPlusConfiguration(), this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        const auto configuration = dialog.configuration();
        (void)m_manager.saveTomlFile(m_manager.profilePathForName(configuration.profileName), configuration);
        loadProfiles();
    }

    void editSelectedProfile()
    {
        const auto row = selectedRow();
        if (row < 0) {
            return;
        }

        ProfileDialog dialog(m_profiles[row].configuration, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        const auto configuration = dialog.configuration();
        const auto path = m_profiles[row].path;
        (void)m_manager.saveTomlFile(path, configuration);
        loadProfiles();
    }

    void cloneSelectedProfile()
    {
        const auto row = selectedRow();
        if (row < 0) {
            return;
        }

        auto configuration = m_profiles[row].configuration;
        configuration.profileName += QStringLiteral(" Copy");
        ProfileDialog dialog(configuration, this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        configuration = dialog.configuration();
        (void)m_manager.saveTomlFile(m_manager.profilePathForName(configuration.profileName), configuration);
        loadProfiles();
    }

    void deleteSelectedProfile()
    {
        const auto row = selectedRow();
        if (row < 0) {
            return;
        }

        const auto response = QMessageBox::question(this,
            QStringLiteral("Delete Profile"),
            QStringLiteral("Delete profile \"%1\"?").arg(m_profiles[row].configuration.profileName));
        if (response != QMessageBox::Yes) {
            return;
        }

        QFile::remove(m_profiles[row].path);
        loadProfiles();
    }

    void startSelectedProfile()
    {
        const auto row = selectedRow();
        if (row < 0) {
            return;
        }

#if defined(Q_OS_WASM)
        QMessageBox::information(this, QStringLiteral("Start"), QStringLiteral("Separate emulator sessions are not available in WebAssembly builds."));
#else
        const auto executable = sessionExecutablePath();
        if (!QFileInfo::exists(executable)) {
            QMessageBox::warning(this, QStringLiteral("Start"), QStringLiteral("Could not find emulator session executable:\n%1").arg(executable));
            return;
        }

        if (!QProcess::startDetached(executable, { QStringLiteral("--profile"), m_profiles[row].path })) {
            QMessageBox::warning(this, QStringLiteral("Start"), QStringLiteral("Could not start emulator session."));
        }
#endif
    }

    [[nodiscard]] QString sessionExecutablePath() const
    {
        const auto suffix =
#if defined(Q_OS_WIN)
            QStringLiteral(".exe");
#else
            QString();
#endif
        const auto appDir = QCoreApplication::applicationDirPath();
        const auto directPath = appDir + QLatin1Char('/') + QStringLiteral("CuteMacSession") + suffix;
        if (QFileInfo::exists(directPath)) {
            return directPath;
        }

#if defined(Q_OS_MACOS)
        const auto bundledPath = QDir(appDir).absoluteFilePath(QStringLiteral("../../../CuteMacSession.app/Contents/MacOS/CuteMacSession"));
        if (QFileInfo::exists(bundledPath)) {
            return bundledPath;
        }
#endif

        return directPath;
    }

    cutemac::config::ConfigurationManager m_manager;
    QTableWidget* m_table = nullptr;
    QVector<ProfileRow> m_profiles;
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("friedkiwi"));
    QCoreApplication::setApplicationName(QStringLiteral("CuteMac"));

    ProfileManagerWindow window;
    window.show();

    return app.exec();
}
