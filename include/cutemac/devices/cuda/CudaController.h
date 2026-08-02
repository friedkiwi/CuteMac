#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace cutemac::devices::via6522 { class Via6522; }

namespace cutemac::devices::cuda {

class CudaController {
public:
    struct DebugState {
        std::uint64_t transitions = 0;
        std::uint64_t attentions = 0;
        std::uint8_t output = 0;
        std::uint8_t direction = 0;
        std::uint8_t lastType = 0;
        std::uint8_t lastCommand = 0;
        std::uint16_t lastPacketSize = 0;
        std::uint16_t lastAddress = 0;
        std::uint64_t packets = 0;
        std::array<std::uint32_t, 256> commandCounts {};
        bool tip = true;
        bool byteAck = true;
    };

    void attach(via6522::Via6522* via);
    void reset();
    void tick(int cycles);
    void portBChanged(std::uint8_t output, std::uint8_t direction);
    void shiftByteFromHost(std::uint8_t value);
    void shiftByteToHostConsumed();
    [[nodiscard]] DebugState debugState() const { return m_debug; }

private:
    void finishPacket();
    void makeResponse(std::uint8_t type, std::uint8_t command);
    void sendNextByte();
    [[nodiscard]] std::uint32_t macintoshTime() const;

    via6522::Via6522* m_via = nullptr;
    std::array<std::uint8_t, 256> m_pram {};
    std::vector<std::uint8_t> m_input;
    std::vector<std::uint8_t> m_output;
    std::size_t m_outputPosition = 0;
    bool m_lastTip = true;
    bool m_lastByteAck = true;
    bool m_synchronousAttention = false;
    bool m_attentionFollowupPending = false;
    int m_idleAckCycles = 0;
    int m_responseTreqCycles = 0;
    int m_shiftCycles = 0;
    bool m_shiftOutCompletion = false;
    bool m_releaseTreqAfterShift = false;
    std::uint8_t m_pendingShiftValue = 0;
    std::optional<std::uint8_t> m_stagedInput;
    DebugState m_debug;
};

} // namespace cutemac::devices::cuda
