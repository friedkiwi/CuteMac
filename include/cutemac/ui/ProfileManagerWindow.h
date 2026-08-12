#pragma once

#include <QMainWindow>
#include <QString>
#include <QVector>

#include "cutemac/config/Configuration.h"

class QPushButton;
class QTableWidget;

namespace cutemac::session {
class SessionWindowManager;
}

namespace cutemac::ui {

// The window CuteMac opens when it is started without a profile argument.
class ProfileManagerWindow final : public QMainWindow {
public:
    explicit ProfileManagerWindow(session::SessionWindowManager& sessions, QWidget* parent = nullptr);
    ~ProfileManagerWindow() override;

private:
    struct ProfileRow {
        QString path;
        config::Configuration configuration;
    };

    void buildUi();
    void ensureDefaultProfile();
    void loadProfiles();
    void rescanCatalogs();
    void setItem(int row, int column, const QString& text);
    [[nodiscard]] QString compactPath(const QString& path) const;
    [[nodiscard]] QString compactFloppyPaths(const config::Configuration& configuration) const;
    [[nodiscard]] int selectedRow() const;
    void createProfile();
    void editSelectedProfile();
    void cloneSelectedProfile();
    void deleteSelectedProfile();
    void startSelectedProfile();
    void updateActionsForSelection();

    session::SessionWindowManager& m_sessions;
    config::ConfigurationManager m_manager;
    QTableWidget* m_table = nullptr;
    QVector<ProfileRow> m_profiles;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_cloneButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
};

} // namespace cutemac::ui
