#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace cutemac::devices::adb {

class AdbTransceiver {
public:
    struct TraceEvent {
        std::uint64_t cycle;
        std::uint8_t kind;
        std::uint8_t state;
        std::uint8_t value;
        std::uint8_t phase;
        bool status;
        std::uint32_t pc;
        std::uint8_t viaAcr;
        std::uint8_t viaSr;
        std::uint8_t viaIfr;
        std::uint8_t viaIer;
        std::uint8_t viaOrb;
    };
    struct TraceContext {
        std::uint32_t pc = 0;
        std::uint8_t viaAcr = 0;
        std::uint8_t viaSr = 0;
        std::uint8_t viaIfr = 0;
        std::uint8_t viaIer = 0;
        std::uint8_t viaOrb = 0;
    };
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
        std::uint8_t phase;
        bool error;
        bool serviceRequest;
        bool retryPending;
    };
    using ReceiveByteCallback = std::function<void(std::uint8_t)>;
    using IrqCallback = std::function<void(bool)>;
    using TransmitCompleteCallback = std::function<void()>;
    using TraceContextCallback = std::function<TraceContext()>;

    void reset();
    void setReceiveByteCallback(ReceiveByteCallback callback);
    void setIrqCallback(IrqCallback callback);
    void setTransmitCompleteCallback(TransmitCompleteCallback callback);
    void setTraceContextCallback(TraceContextCallback callback);
    void setViaState(std::uint8_t state);
    void shiftRegisterWritten(std::uint8_t value);
    void tick(int cycles);
    void queueKey(std::uint8_t keyCode, bool pressed);
    void moveMouse(std::int16_t dx, std::int16_t dy);
    void setMouseButton(bool pressed);
    void resetInput();
    void setTraceEnabled(bool enabled);
    [[nodiscard]] std::vector<TraceEvent> traceEvents() const;
    [[nodiscard]] DebugState debugState() const;

private:
    enum class Transfer : std::uint8_t { None, HostToController, ControllerToHost, AutoPollCommand };
    enum class Phase : std::uint8_t { Idle, Command, Listen, Reply, AutoPoll };

    void prepareResponse(std::uint8_t command);
    void completeListen();
    void requestAutoPoll();
    void transferByte();
    void startHostTransfer(std::uint8_t value);
    void startControllerTransfer(std::uint8_t value, bool status, Transfer kind = Transfer::ControllerToHost);
    void finishTransfer();
    void scheduleForState();
    void setStatus(bool asserted);
    void trace(std::uint8_t kind, std::uint8_t value = 0, bool status = false);

    ReceiveByteCallback m_receiveByte;
    IrqCallback m_irq;
    TransmitCompleteCallback m_transmitComplete;
    TraceContextCallback m_traceContext;
    std::deque<std::uint8_t> m_response;
    std::uint8_t m_state = 3;
    std::uint8_t m_command = 0;
    int m_transferCycles = 0;
    int m_retryCycles = 0;
    int m_deferredTalkCycles = 0;
    int m_responseDelayCycles = 0;
    bool m_commandPending = false;
    bool m_transmittingFromVia = false;
    bool m_receivingListenByte = false;
    bool m_autoWakePending = false;
    bool m_autoPollRequested = false;
    bool m_error = false;
    bool m_serviceRequest = false;
    bool m_transferStatus = false;
    bool m_retryPending = false;
    bool m_deferredTalkPending = false;
    bool m_talkTimedOut = false;
    bool m_errorReportPending = false;
    std::uint8_t m_latchedHostByte = 0;
    std::uint8_t m_latchedControllerByte = 0xff;
    std::uint8_t m_lastTransferredState = 3;
    Transfer m_transfer = Transfer::None;
    Phase m_phase = Phase::Idle;
    std::uint64_t m_cycle = 0;
    bool m_traceEnabled = false;
    std::deque<TraceEvent> m_traceEvents;
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
