#include "cutemac/ui/ProfileManagerWindow.h"

#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputMethodEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "cutemac/rom/RomCatalog.h"
#include "cutemac/session/SessionWindowManager.h"
#include "cutemac/session/WindowMenu.h"
#include "cutemac/ui/ConfigurationDialog.h"
#include "cutemac/ui/DiskImageWidgets.h"
#include "cutemac/ui/RomManagerDialog.h"

namespace {

enum Column {
    NameColumn,
    MachineColumn,
    RomStatusColumn,
    SessionColumn,
    DiskColumn,
    FloppyColumn,
    PathColumn,
    ColumnCount,
};

class NonEditableTableWidget final : public QTableWidget {
public:
    using QTableWidget::QTableWidget;

protected:
    bool edit(const QModelIndex&, EditTrigger, QEvent*) override { return false; }
    void inputMethodEvent(QInputMethodEvent* event) override { event->ignore(); }
};

} // namespace

namespace cutemac::ui {

ProfileManagerWindow::ProfileManagerWindow(session::SessionWindowManager& sessions, QWidget* parent)
    : QMainWindow(parent)
    , m_sessions(sessions)
{
    setWindowTitle(QStringLiteral("CuteMac"));
    resize(960, 520);

    (void)m_manager.ensureDirectories();
    ensureDefaultProfile();
    buildUi();
    loadProfiles();

    // A running session owns its profile file and rewrites it on media and
    // configuration changes, so refresh whenever the session set changes.
    m_sessions.setSessionsChangedCallback([this]() { loadProfiles(); });
}

ProfileManagerWindow::~ProfileManagerWindow()
{
    m_sessions.setSessionsChangedCallback({});
}

void ProfileManagerWindow::buildUi()
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

    m_table = new NonEditableTableWidget;
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Name"),
        QStringLiteral("Machine"),
        QStringLiteral("ROM status"),
        QStringLiteral("Session"),
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
    m_startButton = new QPushButton(QStringLiteral("Start"));
    auto* newButton = new QPushButton(QStringLiteral("New"));
    m_editButton = new QPushButton(QStringLiteral("Configure"));
    m_cloneButton = new QPushButton(QStringLiteral("Clone"));
    m_deleteButton = new QPushButton(QStringLiteral("Delete"));
    auto* refreshButton = new QPushButton(QStringLiteral("Refresh"));
    buttonLayout->addWidget(m_startButton);
    buttonLayout->addWidget(newButton);
    buttonLayout->addWidget(m_editButton);
    buttonLayout->addWidget(m_cloneButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(refreshButton);
    layout->addLayout(buttonLayout);

    setCentralWidget(central);

    auto* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    fileMenu->addAction(QStringLiteral("New Profile"), this, [this]() { createProfile(); });
    fileMenu->addAction(QStringLiteral("Open Profile Folder"), this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(config::ConfigurationManager::profileDirectoryPath()));
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Quit"), this, &QWidget::close);

    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
    toolsMenu->addAction(QStringLiteral("Disk Image Manager..."), this, [this]() {
        DiskImageManagerDialog dialog(this);
        dialog.exec();
    });
    toolsMenu->addAction(QStringLiteral("ROM Manager..."), this, [this]() {
        RomManagerDialog dialog(this);
        dialog.exec();
        loadProfiles();
    });
    toolsMenu->addAction(QStringLiteral("Open Disk Image Folder"), this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(config::ConfigurationManager::diskImageDirectoryPath()));
    });

    session::WindowMenu::install(this);

    connect(m_startButton, &QPushButton::clicked, this, [this]() { startSelectedProfile(); });
    connect(newButton, &QPushButton::clicked, this, [this]() { createProfile(); });
    connect(m_editButton, &QPushButton::clicked, this, [this]() { editSelectedProfile(); });
    connect(m_cloneButton, &QPushButton::clicked, this, [this]() { cloneSelectedProfile(); });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() { deleteSelectedProfile(); });
    connect(refreshButton, &QPushButton::clicked, this, [this]() { loadProfiles(); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { startSelectedProfile(); });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
        [this]() { updateActionsForSelection(); });
}

void ProfileManagerWindow::ensureDefaultProfile()
{
    if (!m_manager.profileFilePaths().isEmpty()) {
        return;
    }
    const auto configuration = config::ConfigurationManager::defaultMacPlusConfiguration();
    (void)m_manager.saveTomlFile(m_manager.profilePathForName(configuration.profileName), configuration);
}

void ProfileManagerWindow::loadProfiles()
{
    // Reloading rebuilds every row, so remember the selection by profile path
    // rather than by row index.
    const auto previousRow = selectedRow();
    const auto previousPath = previousRow >= 0 ? m_profiles[previousRow].path : QString();

    m_profiles.clear();
    for (const auto& path : m_manager.profileFilePaths()) {
        const auto configuration = m_manager.loadTomlFile(path);
        if (configuration.has_value()) {
            m_profiles.append({ path, *configuration });
        }
    }

    m_table->setRowCount(m_profiles.size());
    int restoredRow = -1;
    for (int row = 0; row < m_profiles.size(); ++row) {
        const auto& profile = m_profiles[row];
        setItem(row, NameColumn, profile.configuration.profileName);
        setItem(row, MachineColumn, profile.configuration.machineId);
        const auto warning = rom::RomCatalog().warningForConfiguration(profile.configuration);
        setItem(row, RomStatusColumn, warning.isEmpty() ? QStringLiteral("Ready") : QStringLiteral("Needs attention"));
        m_table->item(row, RomStatusColumn)->setToolTip(warning);
        setItem(row, SessionColumn, m_sessions.isOpen(profile.path) ? QStringLiteral("Running") : QString());
        setItem(row, DiskColumn, compactPath(profile.configuration.diskPath));
        setItem(row, FloppyColumn, compactFloppyPaths(profile.configuration));
        setItem(row, PathColumn, profile.path);
        if (profile.path == previousPath) {
            restoredRow = row;
        }
    }
    m_table->resizeColumnsToContents();
    if (!m_profiles.isEmpty()) {
        m_table->selectRow(restoredRow >= 0 ? restoredRow : 0);
    }
    updateActionsForSelection();
}

void ProfileManagerWindow::setItem(int row, int column, const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setToolTip(text);
    m_table->setItem(row, column, item);
}

QString ProfileManagerWindow::compactPath(const QString& path) const
{
    return path.isEmpty() ? QStringLiteral("(not set)") : QFileInfo(path).fileName();
}

QString ProfileManagerWindow::compactFloppyPaths(const config::Configuration& configuration) const
{
    if (configuration.iwmDevices.isEmpty()) return QStringLiteral("(not set)");
    auto drivePath = [](const QVector<config::IwmDeviceConfiguration>& devices, int drive) {
        if (drive >= devices.size() || devices[drive].imagePath.isEmpty()) return QStringLiteral("-");
        return QFileInfo(devices[drive].imagePath).fileName();
    };
    return QStringLiteral("I:%1 E:%2").arg(drivePath(configuration.iwmDevices, 0), drivePath(configuration.iwmDevices, 1));
}

int ProfileManagerWindow::selectedRow() const
{
    const auto selected = m_table->selectionModel()->selectedRows();
    if (selected.isEmpty()) return -1;
    const auto row = selected.first().row();
    return row < m_profiles.size() ? row : -1;
}

void ProfileManagerWindow::updateActionsForSelection()
{
    const auto row = selectedRow();
    const bool hasSelection = row >= 0;
    // A running session owns its profile file, so editing or deleting it from
    // here would fight the session's own saves.
    const bool running = hasSelection && m_sessions.isOpen(m_profiles[row].path);

    m_startButton->setEnabled(hasSelection);
    m_startButton->setText(running ? QStringLiteral("Show") : QStringLiteral("Start"));
    m_editButton->setEnabled(hasSelection && !running);
    m_cloneButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection && !running);
}

void ProfileManagerWindow::createProfile()
{
    ConfigurationDialog dialog(config::ConfigurationManager::defaultMacPlusConfiguration(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto configuration = dialog.configuration();
    (void)m_manager.saveTomlFile(m_manager.profilePathForName(configuration.profileName), configuration);
    loadProfiles();
}

void ProfileManagerWindow::editSelectedProfile()
{
    const auto row = selectedRow();
    if (row < 0 || m_sessions.isOpen(m_profiles[row].path)) {
        return;
    }

    ConfigurationDialog dialog(m_profiles[row].configuration, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto configuration = dialog.configuration();
    const auto path = m_profiles[row].path;
    (void)m_manager.saveTomlFile(path, configuration);
    loadProfiles();
}

void ProfileManagerWindow::cloneSelectedProfile()
{
    const auto row = selectedRow();
    if (row < 0) {
        return;
    }

    auto configuration = m_profiles[row].configuration;
    configuration.profileName += QStringLiteral(" Copy");
    ConfigurationDialog dialog(configuration, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    configuration = dialog.configuration();
    (void)m_manager.saveTomlFile(m_manager.profilePathForName(configuration.profileName), configuration);
    loadProfiles();
}

void ProfileManagerWindow::deleteSelectedProfile()
{
    const auto row = selectedRow();
    if (row < 0 || m_sessions.isOpen(m_profiles[row].path)) {
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

void ProfileManagerWindow::startSelectedProfile()
{
    const auto row = selectedRow();
    if (row < 0) {
        return;
    }

    // Copy before starting: opening a session fires the sessions-changed
    // callback, which reloads m_profiles and invalidates the row.
    const auto configuration = m_profiles[row].configuration;
    const auto profilePath = m_profiles[row].path;

    // Already running: raise it instead of warning about ROMs a second time.
    if (m_sessions.isOpen(profilePath)) {
        (void)m_sessions.open(configuration, profilePath);
        return;
    }

    const auto romWarning = rom::RomCatalog().warningForConfiguration(configuration);
    if (!romWarning.isEmpty()) {
        const auto response = QMessageBox::warning(this, QStringLiteral("ROM Warning"), romWarning
                + QStringLiteral("\n\nThe machine may not start or some hardware may not work. Continue anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (response != QMessageBox::Yes) return;
    }

    QString error;
    if (!m_sessions.open(configuration, profilePath, &error)) {
        QMessageBox::warning(this, QStringLiteral("Start"),
            error.isEmpty() ? QStringLiteral("Could not start the emulator session.") : error);
    }
}

} // namespace cutemac::ui
