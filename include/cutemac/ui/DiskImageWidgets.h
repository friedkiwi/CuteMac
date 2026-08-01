#pragma once

#include <QDialog>
#include <QString>
#include <memory>

#include "cutemac/storage/DiskImageManager.h"

namespace cutemac::ui {

class DiskImageManagerDialog final : public QDialog {
public:
    explicit DiskImageManagerDialog(QWidget* parent = nullptr);
    ~DiskImageManagerDialog() override;
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

class DiskImagePickerDialog final : public QDialog {
public:
    DiskImagePickerDialog(storage::DiskImageType type, const QString& title, QWidget* parent = nullptr);
    ~DiskImagePickerDialog() override;
    [[nodiscard]] QString selectedImagePath() const;
    [[nodiscard]] static QString getImage(storage::DiskImageType type, const QString& title, QWidget* parent = nullptr);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

[[nodiscard]] QString createDiskImage(storage::DiskImageType type, QWidget* parent = nullptr);

} // namespace cutemac::ui
