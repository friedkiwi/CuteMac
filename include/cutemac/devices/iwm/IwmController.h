#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace cutemac::devices::iwm {

class IwmController {
public:
    using DiskSenseProvider = std::function<bool(std::uint8_t caLines)>;

    void reset();

    [[nodiscard]] std::uint8_t access(std::uint8_t registerIndex);

    void setDiskSenseProvider(DiskSenseProvider provider);

    [[nodiscard]] bool q6() const;
    [[nodiscard]] bool q7() const;

private:
    std::array<bool, 8> m_lines {};
    DiskSenseProvider m_diskSenseProvider;
};

} // namespace cutemac::devices::iwm
