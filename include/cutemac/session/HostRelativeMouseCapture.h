#pragma once

#include <functional>
#include <memory>

class QWidget;

namespace cutemac::session {

class HostRelativeMouseCapture {
public:
    using DeltaCallback = std::function<void(int dx, int dy)>;

    virtual ~HostRelativeMouseCapture();

    [[nodiscard]] virtual bool start(QWidget& target, DeltaCallback callback) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool active() const = 0;
};

[[nodiscard]] std::unique_ptr<HostRelativeMouseCapture> createHostRelativeMouseCapture();

} // namespace cutemac::session
