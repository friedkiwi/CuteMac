#pragma once

#include <cstdint>

namespace cutemac::core {

struct GuestInputEvent {
    enum class Type {
        MousePosition,
        MouseDelta,
        MouseButton,
        Key,
        ResetKeyboard,
    };

    Type type = Type::MouseDelta;
    std::int32_t first = 0;
    std::int32_t second = 0;
    bool pressed = false;
};

} // namespace cutemac::core
