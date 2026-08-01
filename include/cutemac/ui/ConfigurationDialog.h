#pragma once

#include <QDialog>
#include <memory>

#include "cutemac/config/Configuration.h"

namespace cutemac::ui {

class ConfigurationDialog final : public QDialog {
public:
    explicit ConfigurationDialog(config::Configuration configuration, QWidget* parent = nullptr);
    ~ConfigurationDialog() override;

    [[nodiscard]] config::Configuration configuration() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace cutemac::ui
