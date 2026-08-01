#pragma once

#include <QDialog>

class QTableWidget;

namespace cutemac::ui {

class RomManagerDialog final : public QDialog {
public:
    explicit RomManagerDialog(QWidget* parent = nullptr);

private:
    void refresh();
    QTableWidget* m_table = nullptr;
};

} // namespace cutemac::ui
