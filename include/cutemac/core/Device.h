#pragma once

#include <QString>

namespace cutemac::core {

class Device {
public:
    virtual ~Device() = default;

    [[nodiscard]] virtual QString id() const = 0;
    virtual void reset() = 0;
};

} // namespace cutemac::core
