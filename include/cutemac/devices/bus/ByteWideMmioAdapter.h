#pragma once

#include <cstdint>
#include <functional>

#include "cutemac/core/BusTransaction.h"

namespace cutemac::devices::bus {

class ByteWideMmioAdapter final : public core::IMmioTarget {
public:
    enum class ReadWiring {
        MostSignificantLane,
        LeastSignificantLane,
        Replicate,
    };

    enum class WriteWiring {
        MostSignificantLane,
        LeastSignificantLane,
    };

    struct Wiring {
        ReadWiring read = ReadWiring::MostSignificantLane;
        WriteWiring write = WriteWiring::MostSignificantLane;
        std::uint8_t openBus = 0xff;
    };

    using ReadCallback = std::function<std::uint8_t(std::uint32_t offset, bool sideEffects)>;
    using WriteCallback = std::function<void(std::uint32_t offset, std::uint8_t value)>;

    ByteWideMmioAdapter(Wiring wiring, ReadCallback read, WriteCallback write);
    [[nodiscard]] core::BusResponse access(std::uint32_t offset, const core::BusTransaction& transaction) override;

private:
    [[nodiscard]] static unsigned laneShift(unsigned accessBytes, bool mostSignificant);
    [[nodiscard]] std::uint32_t placeRead(std::uint8_t value, unsigned accessBytes) const;
    [[nodiscard]] std::uint8_t selectWrite(std::uint32_t value, unsigned accessBytes) const;

    Wiring m_wiring;
    ReadCallback m_read;
    WriteCallback m_write;
};

} // namespace cutemac::devices::bus
