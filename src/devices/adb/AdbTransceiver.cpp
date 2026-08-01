#include "cutemac/devices/adb/AdbTransceiver.h"

#include <algorithm>

namespace cutemac::devices::adb {

void AdbTransceiver::reset()
{
    m_response.clear();
    m_state = 3;
    m_command = 0;
    m_transferCycles = 0;
    m_commandPending = false;
    m_transmittingFromVia = false;
    m_receivingListenByte = false;
    m_autoWakePending = false;
    m_listenAddress = 0;
    m_listenBytes.clear();
    m_keyboardAddress = 2;
    m_mouseAddress = 3;
    m_keyboardHandler = 0x22;
    m_mouseHandler = 0x23;
    resetInput();
    if (m_irq) m_irq(false);
}

void AdbTransceiver::setReceiveByteCallback(ReceiveByteCallback callback) { m_receiveByte = std::move(callback); }
void AdbTransceiver::setIrqCallback(IrqCallback callback) { m_irq = std::move(callback); }
void AdbTransceiver::setTransmitCompleteCallback(TransmitCompleteCallback callback) { m_transmitComplete = std::move(callback); }

void AdbTransceiver::setViaState(std::uint8_t state)
{
    state &= 3;
    if (state == 0) {
        if (m_irq) m_irq(false);
        if (m_commandPending && m_transferCycles == 0) {
            prepareResponse(m_command);
            m_transmittingFromVia = true;
            m_transferCycles = 32;
        }
    }
    m_state = state;
    if ((state == 1 || state == 2) && m_transferCycles == 0) m_transferCycles = 32;
    if (state == 3 && (m_mouseDx != 0 || m_mouseDy != 0 || !m_mouseButtonEvents.empty() || !m_keyEvents.empty())) requestAutoPoll();
}

void AdbTransceiver::tick(int cycles)
{
    if (m_transferCycles <= 0) return;
    m_transferCycles -= cycles;
    if (m_transferCycles <= 0) {
        m_transferCycles = 0;
        if (m_transmittingFromVia) {
            m_transmittingFromVia = false;
            m_commandPending = false;
            if (m_receivingListenByte) {
                m_receivingListenByte = false;
                if (m_listenBytes.size() == 2) completeListen();
                m_response = {0xff};
            }
            if (m_transmitComplete) m_transmitComplete();
        } else if (m_autoWakePending) {
            m_autoWakePending = false;
            const bool serviceRequest = m_response.empty();
            if (m_receiveByte) m_receiveByte(serviceRequest ? std::uint8_t {0xff} : m_response.front());
            if (m_irq) m_irq(serviceRequest);
        } else {
            transferByte();
        }
    }
}

void AdbTransceiver::shiftRegisterWritten(std::uint8_t value)
{
    if (m_listenAddress != 0 && m_listenBytes.size() < 2) {
        m_listenBytes.push_back(value);
        m_receivingListenByte = true;
        m_commandPending = true;
        m_transmittingFromVia = true;
        if (m_transferCycles == 0) m_transferCycles = 32;
        return;
    }
    m_command = value;
    m_commandPending = true;
    if (m_transferCycles == 0) {
        if (m_state == 0) {
            prepareResponse(value);
            m_transmittingFromVia = true;
            m_transferCycles = 32;
        }
    }
}

void AdbTransceiver::prepareResponse(std::uint8_t command)
{
    m_response.clear();
    const auto address = static_cast<std::uint8_t>(command >> 4);
    const auto operation = static_cast<std::uint8_t>((command >> 2) & 3);
    const auto reg = static_cast<std::uint8_t>(command & 3);
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
    const auto operation = static_cast<std::uint8_t>((m_command >> 2) & 3);
    if (m_state != 3 || m_transferCycles != 0 || m_commandPending || operation != 3) return;
    prepareResponse(m_command);
    m_autoWakePending = true;
    m_transferCycles = 32;
}

void AdbTransceiver::transferByte()
{
    const bool endOfFrame = m_response.empty();
    const auto value = endOfFrame ? std::uint8_t {0xff} : m_response.front();
    if (!endOfFrame) m_response.pop_front();
    if (m_receiveByte) m_receiveByte(value);
    if (m_irq) m_irq(endOfFrame);
}

AdbTransceiver::DebugState AdbTransceiver::debugState() const
{
    return {m_state, m_command, m_response.size(), m_transferCycles, m_commandPending, m_transmittingFromVia,
        m_mouseDx, m_mouseDy, m_keyboardAddress, m_mouseAddress};
}

} // namespace cutemac::devices::adb
