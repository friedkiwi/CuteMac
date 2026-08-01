#pragma once

#include <cstdint>
#include <deque>
#include <functional>

namespace cutemac::devices::adb {

class AdbTransceiver {
public:
    struct DebugState {
        std::uint8_t state;
        std::uint8_t command;
        std::size_t responseBytes;
        int transferCycles;
        bool commandPending;
        bool transmittingFromVia;
        int pendingMouseDx;
        int pendingMouseDy;
        std::uint8_t keyboardAddress;
        std::uint8_t mouseAddress;
    };
    using ReceiveByteCallback = std::function<void(std::uint8_t)>;
    using IrqCallback = std::function<void(bool)>;
    using TransmitCompleteCallback = std::function<void()>;

    void reset();
    void setReceiveByteCallback(ReceiveByteCallback callback);
    void setIrqCallback(IrqCallback callback);
    void setTransmitCompleteCallback(TransmitCompleteCallback callback);
    void setViaState(std::uint8_t state);
    void shiftRegisterWritten(std::uint8_t value);
    void tick(int cycles);
    void queueKey(std::uint8_t keyCode, bool pressed);
    void moveMouse(std::int16_t dx, std::int16_t dy);
    void setMouseButton(bool pressed);
    void resetInput();
    [[nodiscard]] DebugState debugState() const;

private:
    void prepareResponse(std::uint8_t command);
    void completeListen();
    void requestAutoPoll();
    void transferByte();

    ReceiveByteCallback m_receiveByte;
    IrqCallback m_irq;
    TransmitCompleteCallback m_transmitComplete;
    std::deque<std::uint8_t> m_response;
    std::uint8_t m_state = 3;
    std::uint8_t m_command = 0;
    int m_transferCycles = 0;
    bool m_commandPending = false;
    bool m_transmittingFromVia = false;
    bool m_receivingListenByte = false;
    bool m_autoWakePending = false;
    std::uint8_t m_listenAddress = 0;
    std::deque<std::uint8_t> m_listenBytes;
    std::uint8_t m_keyboardAddress = 2;
    std::uint8_t m_mouseAddress = 3;
    std::uint8_t m_keyboardHandler = 0x22;
    std::uint8_t m_mouseHandler = 0x23;
    std::deque<std::uint8_t> m_keyEvents;
    std::deque<bool> m_mouseButtonEvents;
    int m_mouseDx = 0;
    int m_mouseDy = 0;
    bool m_mouseButton = false;
    bool m_keyboardPolled = false;
    bool m_mousePolled = false;
};

} // namespace cutemac::devices::adb
