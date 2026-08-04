#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace cutemac::ui {

struct FileDialogRequest {
    QString title;
    QString directory;
    QString selectedFile;
    QStringList nameFilters;
    QString defaultSuffix;
};

// Use these helpers instead of QFileDialog's static get* functions so platform
// file-dialog policy stays consistent across every browse button.
[[nodiscard]] QStringList openFiles(QWidget* parent, const FileDialogRequest& request);
[[nodiscard]] QString openFile(QWidget* parent, const FileDialogRequest& request);
[[nodiscard]] QString saveFile(QWidget* parent, const FileDialogRequest& request);
[[nodiscard]] QString selectDirectory(QWidget* parent, const FileDialogRequest& request);

} // namespace cutemac::ui
