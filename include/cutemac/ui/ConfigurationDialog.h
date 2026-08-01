#pragma once

#include <QDialog>

#include "cutemac/config/Configuration.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace cutemac::ui {

class ConfigurationDialog final : public QDialog {
public:
    explicit ConfigurationDialog(config::Configuration configuration, QWidget* parent = nullptr);

    [[nodiscard]] config::Configuration configuration() const;

private:
    config::Configuration m_configuration;
    QLineEdit* m_name = nullptr;
    QComboBox* m_machine = nullptr;
    QLineEdit* m_romPath = nullptr;
    QLineEdit* m_diskPath = nullptr;
    QLineEdit* m_floppyPath = nullptr;
    QSpinBox* m_ramSize = nullptr;
    QSpinBox* m_cyclesPerFrame = nullptr;
    QComboBox* m_runtimeSpeed = nullptr;
    QCheckBox* m_skipRamPatternTest = nullptr;
};

} // namespace cutemac::ui
