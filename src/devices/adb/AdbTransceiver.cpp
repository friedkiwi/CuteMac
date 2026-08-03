#include "cutemac/devices/adb/AdbTransceiver.h"

#include <algorithm>

namespace cutemac::devices::adb {

namespace {

// The 342S0440-B clocks a byte a bit at a time from an independently running
// PIC1654S.  Keep the completion far enough from the initiating 68k register
// write that a following ACR write cannot retroactively change its direction.
constexpr int viaByteCycles = 1200;
// The firmware's nested stable-state countdowns periodically clock the last
// controller byte again.  At IIcx clock rates this is on the order of 10 ms.
constexpr int stableStateRetryCycles = 1082000;
// Attention, command byte, stop and the TALK response-start window take about
// two milliseconds on the physical ADB bus.
constexpr int adbTransactionCycles = 30000;
constexpr std::size_t maxTraceEvents = 4096;

} // namespace

void AdbTransceiver::reset()
{
    m_response.clear();
    m_state = 3;
    m_command = 0;
    m_transferCycles = 0;
    m_retryCycles = 0;
    m_deferredTalkCycles = 0;
    m_responseDelayCycles = 0;
    m_commandPending = false;
    m_transmittingFromVia = false;
    m_receivingListenByte = false;
    m_autoWakePending = false;
    m_autoPollRequested = false;
    m_error = false;
    m_serviceRequest = false;
    m_transferStatus = false;
    m_retryPending = false;
    m_deferredTalkPending = false;
    m_talkTimedOut = false;
    m_errorReportPending = false;
    m_latchedHostByte = 0;
    m_latchedControllerByte = 0xff;
    m_lastTransferredState = 3;
    m_transfer = Transfer::None;
    m_phase = Phase::Idle;
    m_cycle = 0;
    m_traceEvents.clear();
    m_listenAddress = 0;
    m_listenBytes.clear();
    m_keyboardAddress = 2;
    m_mouseAddress = 3;
    m_keyboardHandler = 0x22;
    m_mouseHandler = 0x23;
    resetInput();
    setStatus(false);
}

void AdbTransceiver::setReceiveByteCallback(ReceiveByteCallback callback) { m_receiveByte = std::move(callback); }
void AdbTransceiver::setIrqCallback(IrqCallback callback) { m_irq = std::move(callback); }
void AdbTransceiver::setTransmitCompleteCallback(TransmitCompleteCallback callback) { m_transmitComplete = std::move(callback); }
void AdbTransceiver::setTraceContextCallback(TraceContextCallback callback) { m_traceContext = std::move(callback); }

void AdbTransceiver::setViaState(std::uint8_t state)
{
    state &= 3;
    // The PIC polls ST1:ST0 and dispatches only when they differ from LASTA.
    // Writes to the other VIA port-B bits (notably the RTC pins) must not
    // replay the current ADB state.
    if (state == m_state) return;
    // Every ST transition makes the real controller release PB3, CB1 and CB2
    // before dispatching on command class plus the new state.
    setStatus(false);
    m_retryCycles = 0;
    m_retryPending = false;
    m_state = state;
    trace(1, state);
    if (state == 1 || state == 2) {
        m_deferredTalkPending = false;
        m_deferredTalkCycles = 0;
    } else if (state == 3 && m_deferredTalkPending && m_deferredTalkCycles == 0) {
        m_responseDelayCycles = 0;
        m_deferredTalkCycles = stableStateRetryCycles + adbTransactionCycles;
    }
    scheduleForState();
}

void AdbTransceiver::tick(int cycles)
{
    if (cycles <= 0) return;
    m_cycle += static_cast<std::uint64_t>(cycles);
    if (m_transferCycles > 0) {
        m_transferCycles -= cycles;
        if (m_transferCycles <= 0) {
            m_transferCycles = 0;
            finishTransfer();
        }
        return;
    }
    if (m_retryCycles > 0) {
        m_retryCycles -= cycles;
        if (m_retryCycles <= 0) {
            m_retryCycles = 0;
            if (m_retryPending && m_state == m_lastTransferredState) {
                startControllerTransfer(m_latchedControllerByte, m_transferStatus);
            }
        }
        return;
    }
    if (m_responseDelayCycles > 0) {
        m_responseDelayCycles -= cycles;
        if (m_responseDelayCycles <= 0) {
            m_responseDelayCycles = 0;
            scheduleForState();
        }
        return;
    }
    if (m_deferredTalkCycles > 0) {
        m_deferredTalkCycles -= cycles;
        if (m_deferredTalkCycles <= 0) {
            m_deferredTalkCycles = 0;
            if (m_deferredTalkPending && m_state == 3) {
                m_deferredTalkPending = false;
                m_phase = Phase::AutoPoll;
                m_serviceRequest = true;
                startControllerTransfer(m_command, true, Transfer::AutoPollCommand);
            }
        }
        return;
    }
    if (m_state == 3 && m_autoPollRequested) requestAutoPoll();
}

void AdbTransceiver::shiftRegisterWritten(std::uint8_t value)
{
    // The byte is only committed when the independently clocked transfer
    // finishes.  In particular, a later VIA ACR direction change must not turn
    // the last command bit into response data.
    m_latchedHostByte = value;
    m_commandPending = true;
    trace(2, value);
    if (m_transferCycles == 0 && (m_state == 0 || m_listenAddress != 0)) startHostTransfer(value);
}

void AdbTransceiver::startHostTransfer(std::uint8_t value)
{
    m_latchedHostByte = value;
    m_transfer = Transfer::HostToController;
    m_transmittingFromVia = true;
    m_transferCycles = viaByteCycles;
    m_retryCycles = 0;
    m_retryPending = false;
    trace(3, value);
}

void AdbTransceiver::startControllerTransfer(std::uint8_t value, bool status, Transfer kind)
{
    m_latchedControllerByte = value;
    m_transferStatus = status;
    m_lastTransferredState = m_state;
    m_transfer = kind;
    m_transmittingFromVia = false;
    m_transferCycles = viaByteCycles;
    m_retryCycles = 0;
    m_retryPending = false;
    trace(5, value, status);
}

void AdbTransceiver::finishTransfer()
{
    const auto completed = m_transfer;
    m_transfer = Transfer::None;
    if (completed == Transfer::HostToController) {
        trace(4, m_latchedHostByte);
        m_transmittingFromVia = false;
        m_commandPending = false;
        if (m_listenAddress != 0) {
            m_listenBytes.push_back(m_latchedHostByte);
            m_receivingListenByte = false;
            if (m_listenBytes.size() == 2) {
                completeListen();
                m_phase = Phase::Idle;
            } else {
                m_receivingListenByte = true;
                m_phase = Phase::Listen;
            }
        } else {
            m_command = m_latchedHostByte;
            prepareResponse(m_command);
            m_phase = m_listenAddress != 0 ? Phase::Listen : Phase::Reply;
            m_deferredTalkPending = ((m_command >> 2) & 3) == 3;
            if (m_listenAddress == 0) m_responseDelayCycles = adbTransactionCycles;
        }
        if (m_transmitComplete) m_transmitComplete();
        scheduleForState();
        return;
    }

    if (m_receiveByte) m_receiveByte(m_latchedControllerByte);
    trace(6, m_latchedControllerByte, m_transferStatus);
    setStatus(m_transferStatus);
    if (completed == Transfer::AutoPollCommand) {
        m_autoWakePending = false;
        m_phase = Phase::AutoPoll;
    }
    // The PIC periodically repeats the byte if the ROM misses the external SR
    // completion and leaves ST1:ST0 unchanged.
    m_retryPending = true;
    m_retryCycles = stableStateRetryCycles;
}

void AdbTransceiver::scheduleForState()
{
    if (m_transferCycles != 0) return;
    if (m_commandPending && (m_state == 0 || m_listenAddress != 0)) {
        startHostTransfer(m_latchedHostByte);
        return;
    }
    if (m_listenAddress != 0) return;
    const auto operation = static_cast<std::uint8_t>((m_command >> 2) & 3);
    // RESET/FLUSH and TALK all return through the controller-to-host status
    // paths.  LISTEN is the sole command class for which S1/S2 receive bytes
    // from the host instead.
    if ((m_state == 1 || m_state == 2) && operation != 2 && m_responseDelayCycles == 0) {
        transferByte();
        return;
    }
    if (m_state == 3 && m_autoPollRequested) requestAutoPoll();
}

void AdbTransceiver::setStatus(bool asserted)
{
    trace(7, 0, asserted);
    if (m_irq) m_irq(asserted);
}

void AdbTransceiver::setTraceEnabled(bool enabled)
{
    m_traceEnabled = enabled;
    if (!enabled) m_traceEvents.clear();
}

std::vector<AdbTransceiver::TraceEvent> AdbTransceiver::traceEvents() const
{
    return {m_traceEvents.cbegin(), m_traceEvents.cend()};
}

void AdbTransceiver::trace(std::uint8_t kind, std::uint8_t value, bool status)
{
    if (!m_traceEnabled) return;
    if (m_traceEvents.size() == maxTraceEvents) m_traceEvents.pop_front();
    const auto context = m_traceContext ? m_traceContext() : TraceContext {};
    m_traceEvents.push_back({m_cycle, kind, m_state, value, static_cast<std::uint8_t>(m_phase), status,
        context.pc, context.viaAcr, context.viaSr, context.viaIfr, context.viaIer, context.viaOrb});
}

void AdbTransceiver::prepareResponse(std::uint8_t command)
{
    m_response.clear();
    const auto address = static_cast<std::uint8_t>(command >> 4);
    const auto operation = static_cast<std::uint8_t>((command >> 2) & 3);
    const auto reg = static_cast<std::uint8_t>(command & 3);
    m_error = false;
    m_serviceRequest = false;
    m_talkTimedOut = false;
    m_errorReportPending = false;
    if ((command & 0x0f) == 0) {
        m_listenAddress = 0;
        m_listenBytes.clear();
        m_keyboardAddress = 2;
        m_mouseAddress = 3;
        m_keyboardHandler = 0x22;
        m_mouseHandler = 0x23;
        m_keyboardPolled = false;
        m_mousePolled = false;
        return;
    }
    if (operation == 1) {
        if (address == m_keyboardAddress) m_keyEvents.clear();
        if (address == m_mouseAddress) {
            m_mouseDx = 0;
            m_mouseDy = 0;
            m_mouseButtonEvents.clear();
        }
        return;
    }
    if (operation == 2 && reg == 3 && (address == m_keyboardAddress || address == m_mouseAddress)) {
        m_listenAddress = address;
        m_listenBytes.clear();
        return;
    }
    if (operation != 3) return;

    if (reg == 3 && address == m_keyboardAddress) {
        m_response = {m_keyboardHandler, 0x01};
    } else if (reg == 3 && address == m_mouseAddress) {
        m_response = {m_mouseHandler, 0x01};
    } else if (reg == 0 && address == m_keyboardAddress) {
        if (!m_keyEvents.empty()) {
            const auto first = m_keyEvents.front();
            m_keyEvents.pop_front();
            const auto second = m_keyEvents.empty() ? std::uint8_t {0xff} : m_keyEvents.front();
            if (!m_keyEvents.empty()) m_keyEvents.pop_front();
            m_response = {first, second};
            m_keyboardPolled = true;
        } else if (!m_keyboardPolled) {
            m_response = {0xff, 0xff};
            m_keyboardPolled = true;
        }
    } else if (reg == 0 && address == m_mouseAddress) {
        if (m_mouseDx != 0 || m_mouseDy != 0 || !m_mouseButtonEvents.empty() || !m_mousePolled) {
            const auto dx = std::clamp(m_mouseDx, -64, 63);
            const auto dy = std::clamp(m_mouseDy, -64, 63);
            m_mouseDx -= dx;
            m_mouseDy -= dy;
            const auto reportedButton = m_mouseButtonEvents.empty() ? m_mouseButton : m_mouseButtonEvents.front();
            if (!m_mouseButtonEvents.empty()) m_mouseButtonEvents.pop_front();
            const auto button = reportedButton ? 0x00 : 0x80;
            m_response = {static_cast<std::uint8_t>(button | (dy & 0x7f)), static_cast<std::uint8_t>(0x80 | (dx & 0x7f))};
            m_mousePolled = true;
        }
    }
    // On a TALK with no device frame, the PIC marks ERROR and enters its
    // receive-complete path with a synthetic length of two.  ClearBuffer has
    // already filled those two positions with 0xff.
    if (m_response.empty()) {
        m_response = {0xff, 0xff};
        m_talkTimedOut = true;
        m_errorReportPending = true;
    }
}

void AdbTransceiver::completeListen()
{
    const auto value = m_listenBytes[0];
    const auto mode = m_listenBytes[1];
    if (m_listenAddress == m_keyboardAddress) {
        if (mode == 0x00) m_keyboardHandler = static_cast<std::uint8_t>(value & 0x7f);
        if (mode == 0x00 || mode == 0xfe) {
            m_keyboardAddress = static_cast<std::uint8_t>(value & 0x0f);
            m_keyboardHandler = static_cast<std::uint8_t>((m_keyboardHandler & 0xf0) | m_keyboardAddress);
        }
    } else if (m_listenAddress == m_mouseAddress) {
        if (mode == 0x00) m_mouseHandler = static_cast<std::uint8_t>(value & 0x7f);
        if (mode == 0x00 || mode == 0xfe) {
            m_mouseAddress = static_cast<std::uint8_t>(value & 0x0f);
            m_mouseHandler = static_cast<std::uint8_t>((m_mouseHandler & 0xf0) | m_mouseAddress);
        }
    }
    m_listenAddress = 0;
    m_listenBytes.clear();
}

void AdbTransceiver::queueKey(std::uint8_t keyCode, bool pressed)
{
    m_keyEvents.push_back(static_cast<std::uint8_t>((keyCode & 0x7f) | (pressed ? 0 : 0x80)));
    m_keyboardPolled = false;
    requestAutoPoll();
}

void AdbTransceiver::moveMouse(std::int16_t dx, std::int16_t dy)
{
    m_mouseDx = std::clamp(m_mouseDx + static_cast<int>(dx), -4096, 4096);
    m_mouseDy = std::clamp(m_mouseDy + static_cast<int>(dy), -4096, 4096);
    if (dx != 0 || dy != 0) requestAutoPoll();
}

void AdbTransceiver::setMouseButton(bool pressed)
{
    if (m_mouseButton == pressed) return;
    m_mouseButton = pressed;
    m_mouseButtonEvents.push_back(pressed);
    m_mousePolled = false;
    requestAutoPoll();
}

void AdbTransceiver::resetInput()
{
    m_keyEvents.clear();
    m_mouseButtonEvents.clear();
    m_mouseDx = 0;
    m_mouseDy = 0;
    m_mouseButton = false;
    m_keyboardPolled = false;
    m_mousePolled = false;
}

void AdbTransceiver::requestAutoPoll()
{
    m_autoPollRequested = true;
    trace(8, m_command);
    const auto operation = static_cast<std::uint8_t>((m_command >> 2) & 3);
    if (m_state != 3 || m_transferCycles != 0 || m_commandPending || operation != 3) return;
    prepareResponse(m_command);
    if (m_talkTimedOut) {
        m_response.clear();
        m_errorReportPending = false;
        return;
    }
    // A keyboard TALK carries at most two transitions. If more host edges
    // accumulated while the PIC was busy, keep service pending so the next
    // idle S3 interval starts another TALK instead of stranding the tail of
    // a modifier combination in m_keyEvents.
    m_autoPollRequested = !m_keyEvents.empty() || !m_mouseButtonEvents.empty()
        || m_mouseDx != 0 || m_mouseDy != 0;
    m_autoWakePending = true;
    m_serviceRequest = true;
    m_phase = Phase::AutoPoll;
    // The firmware's unsolicited path first sends the saved TALK command so
    // the ADB Manager can identify the source.  Payload bytes follow in S1/S2.
    startControllerTransfer(m_command, true, Transfer::AutoPollCommand);
}

void AdbTransceiver::transferByte()
{
    const bool endOfFrame = m_response.empty();
    const auto value = endOfFrame ? std::uint8_t {0xff} : m_response.front();
    if (!endOfFrame) m_response.pop_front();
    const bool errorStatus = m_errorReportPending;
    m_errorReportPending = false;
    m_error = endOfFrame || errorStatus;
    m_serviceRequest = false;
    m_phase = Phase::Reply;
    startControllerTransfer(value, endOfFrame || errorStatus);
}

AdbTransceiver::DebugState AdbTransceiver::debugState() const
{
    return {m_state, m_command, m_response.size(), m_transferCycles, m_commandPending, m_transmittingFromVia,
        m_mouseDx, m_mouseDy, m_keyboardAddress, m_mouseAddress, static_cast<std::uint8_t>(m_phase), m_error,
        m_serviceRequest, m_retryPending};
}

} // namespace cutemac::devices::adb
