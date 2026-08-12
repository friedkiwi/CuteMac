#pragma once

#include <functional>
#include <memory>

class QWidget;

namespace cutemac::session {

// A platform's native way of reading pointer motion without moving a cursor
// around: raw input on Windows, event-tap deltas on macOS. The session
// frontend prefers one of these over its portable warp fallback, which has to
// keep recentering the host cursor and therefore inherits pointer
// acceleration, fractional-scaling rounding, and a periodic timer.
class HostRelativeMouseCapture {
public:
    struct Callbacks {
        // Pointer motion in host pixels, already accumulated to whole steps.
        std::function<void(int dx, int dy)> delta;
        // The platform revoked the capture on its own: a focus or foreground
        // switch, a compositor declining to keep the pointer locked, a browser
        // leaving pointer lock. Backends must deliver this asynchronously,
        // because the frontend responds by calling stop() and doing that from
        // inside a native event handler would tear down the handler mid-call.
        std::function<void()> lost;
    };

    virtual ~HostRelativeMouseCapture();

    [[nodiscard]] virtual bool start(QWidget& target, Callbacks callbacks) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool active() const = 0;
};

// Returns nullptr on platforms with no native backend yet, which leaves the
// frontend on its warp fallback (or on absolute pointer mapping where even
// that is unavailable). X11, Wayland and wasm have viable mechanisms —
// XI2 raw motion, zwp_relative_pointer_v1 plus a locked pointer, and the
// Pointer Lock API — but no implementation here yet.
[[nodiscard]] std::unique_ptr<HostRelativeMouseCapture> createHostRelativeMouseCapture();

} // namespace cutemac::session
