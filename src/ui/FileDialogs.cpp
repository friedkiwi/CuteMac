#include "cutemac/ui/FileDialogs.h"

#include <QDir>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>

namespace cutemac::ui {
namespace {

void applyPlatformOptions(QFileDialog& dialog)
{
#if defined(Q_OS_MACOS)
    dialog.setOption(QFileDialog::DontUseNativeDialog);
#else
    Q_UNUSED(dialog);
#endif
}

void applyRequest(QFileDialog& dialog, const FileDialogRequest& request)
{
    if (!request.nameFilters.isEmpty()) dialog.setNameFilters(request.nameFilters);
    if (!request.defaultSuffix.isEmpty()) dialog.setDefaultSuffix(request.defaultSuffix);
    if (!request.selectedFile.isEmpty()) dialog.selectFile(request.selectedFile);
}

QString dialogDirectory(const FileDialogRequest& request)
{
    return request.directory.isEmpty() ? QDir::homePath() : request.directory;
}

QString absoluteSelectedPath(const QFileDialog& dialog)
{
    const auto files = dialog.selectedFiles();
    return files.isEmpty() ? QString() : QFileInfo(files.constFirst()).absoluteFilePath();
}

} // namespace

QStringList openFiles(QWidget* parent, const FileDialogRequest& request)
{
    QFileDialog dialog(parent, request.title, dialogDirectory(request));
    applyPlatformOptions(dialog);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    applyRequest(dialog, request);
    if (dialog.exec() != QDialog::Accepted) return {};

    QStringList result;
    for (const auto& path : dialog.selectedFiles()) result.append(QFileInfo(path).absoluteFilePath());
    return result;
}

QString openFile(QWidget* parent, const FileDialogRequest& request)
{
    QFileDialog dialog(parent, request.title, dialogDirectory(request));
    applyPlatformOptions(dialog);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    applyRequest(dialog, request);
    return dialog.exec() == QDialog::Accepted ? absoluteSelectedPath(dialog) : QString();
}

QString saveFile(QWidget* parent, const FileDialogRequest& request)
{
    QFileDialog dialog(parent, request.title, dialogDirectory(request));
    applyPlatformOptions(dialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    applyRequest(dialog, request);
    return dialog.exec() == QDialog::Accepted ? absoluteSelectedPath(dialog) : QString();
}

QString selectDirectory(QWidget* parent, const FileDialogRequest& request)
{
    QFileDialog dialog(parent, request.title, dialogDirectory(request));
    applyPlatformOptions(dialog);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly);
    applyRequest(dialog, request);
    return dialog.exec() == QDialog::Accepted ? absoluteSelectedPath(dialog) : QString();
}

} // namespace cutemac::ui
