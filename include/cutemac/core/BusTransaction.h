#pragma once

#include <cstdint>

namespace cutemac::core {

enum class BusAccessKind {
    DataRead,
    DataWrite,
    InstructionFetch,
    DebugRead,
    DebugWrite,
};

enum class BusAccessStatus {
    Complete,
    Wait,
    BusError,
};

struct BusTransaction {
    std::uint32_t address = 0;
    std::uint32_t value = 0;
    std::uint8_t size = 1;
    std::uint8_t byteEnable = 1;
    BusAccessKind kind = BusAccessKind::DataRead;
    bool sideEffects = true;
};

struct BusResponse {
    std::uint32_t value = 0;
    unsigned extraCycles = 0;
    BusAccessStatus status = BusAccessStatus::Complete;
};

class IMmioTarget {
public:
    virtual ~IMmioTarget() = default;
    [[nodiscard]] virtual BusResponse access(std::uint32_t offset, const BusTransaction& transaction) = 0;
};

} // namespace cutemac::core
