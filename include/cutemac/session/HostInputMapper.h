#pragma once

class QKeyEvent;

namespace cutemac::session {

class HostInputMapper {
public:
    [[nodiscard]] static int macKeyCode(const QKeyEvent& event);
};

} // namespace cutemac::session
