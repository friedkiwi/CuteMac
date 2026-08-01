#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

#include <QString>
#include <QStringList>

#include "cutemac/core/CpuCore.h"
#include "cutemac/cpu/ppc/PowerPcBus.h"

namespace cutemac::cpu::ppc {

class PowerPc601Core final : public core::CpuCore {
public:
    enum class AccessType : std::uint8_t { Instruction, Read, Write };
    enum class TranslationPath : std::uint8_t { Real, Bat, Page, Fault };
    enum class Exception : std::uint16_t {
        Reset = 0x0100, MachineCheck = 0x0200, DataStorage = 0x0300,
        InstructionStorage = 0x0400, ExternalInterrupt = 0x0500,
        Alignment = 0x0600, Program = 0x0700, FloatingPointUnavailable = 0x0800,
        Decrementer = 0x0900, SystemCall = 0x0c00, Trace = 0x0d00,
    };

    struct RegisterSnapshot {
        std::array<std::uint32_t, 32> gpr {};
        std::array<std::uint64_t, 32> fpr {};
        std::array<std::uint32_t, 16> sr {};
        std::array<std::uint32_t, 4> batu {};
        std::array<std::uint32_t, 4> batl {};
        std::uint32_t pc = 0, msr = 0, cr = 0, xer = 0;
        std::uint32_t lr = 0, ctr = 0, mq = 0;
        std::uint32_t srr0 = 0, srr1 = 0, dar = 0, dsisr = 0;
        std::uint32_t sdr1 = 0, fpscr = 0, hid0 = 0, hid1 = 0;
        std::array<std::uint32_t, 4> sprg {};
        std::uint32_t dec = 0, rtcu = 0, rtcl = 0;
        bool reservationValid = false;
        std::uint32_t reservationAddress = 0;
    };

    struct TraceEvent {
        enum class Kind : std::uint8_t { Instruction, Exception, Interrupt, Translation };
        Kind kind = Kind::Instruction;
        std::uint64_t cycle = 0;
        std::uint32_t pc = 0;
        std::uint32_t opcode = 0;
        std::uint32_t effectiveAddress = 0;
        std::uint32_t physicalAddress = 0;
        std::uint32_t detail = 0;
        std::uint32_t savedMsr = 0;
        AccessType access = AccessType::Instruction;
        TranslationPath translation = TranslationPath::Real;
        Exception exception = Exception::Reset;
        bool success = true;
    };

    using TraceSink = std::function<void(const TraceEvent&)>;

    static constexpr std::uint32_t msrLe = 0x00000001;
    static constexpr std::uint32_t msrRi = 0x00000002;
    static constexpr std::uint32_t msrDr = 0x00000010;
    static constexpr std::uint32_t msrIr = 0x00000020;
    static constexpr std::uint32_t msrIp = 0x00000040;
    static constexpr std::uint32_t msrFe1 = 0x00000100;
    static constexpr std::uint32_t msrBe = 0x00000200;
    static constexpr std::uint32_t msrSe = 0x00000400;
    static constexpr std::uint32_t msrFe0 = 0x00000800;
    static constexpr std::uint32_t msrMe = 0x00001000;
    static constexpr std::uint32_t msrFp = 0x00002000;
    static constexpr std::uint32_t msrPr = 0x00004000;
    static constexpr std::uint32_t msrEe = 0x00008000;

    [[nodiscard]] QString id() const override;
    void reset() override;

    void setBus(PowerPcBus* bus);
    void setExternalInterrupt(bool asserted);
    void setTraceSink(TraceSink sink);
    void setCycleCount(std::uint64_t cycles);
    void setClockFrequency(std::uint32_t hz);
    void advanceTime(std::uint32_t processorCycles);

    [[nodiscard]] int execute(int cycles);
    [[nodiscard]] int stepInstruction();
    [[nodiscard]] std::uint32_t programCounter() const;
    void setProgramCounter(std::uint32_t address);
    [[nodiscard]] RegisterSnapshot registers() const;
    void setRegisters(const RegisterSnapshot& registers);
    [[nodiscard]] QString disassemble(std::uint32_t address) const;
    [[nodiscard]] QString disassembleOpcode(std::uint32_t address, std::uint32_t opcode) const;
    [[nodiscard]] QStringList debugRegisterLines() const;
    [[nodiscard]] int disassembleBytes(std::uint32_t) const { return 4; }
    [[nodiscard]] static QString formatTraceEvent(const TraceEvent& event);

    [[nodiscard]] std::optional<std::uint32_t> translateForDebug(
        std::uint32_t effectiveAddress, AccessType access) const;

private:
    struct Translation {
        std::uint32_t physicalAddress = 0;
        TranslationPath path = TranslationPath::Real;
        bool guarded = false;
    };

    [[nodiscard]] std::optional<Translation> translate(std::uint32_t address, AccessType access, bool sideEffects);
    [[nodiscard]] std::optional<std::uint32_t> read(std::uint32_t address, unsigned int size, AccessType access);
    [[nodiscard]] bool write(std::uint32_t address, std::uint32_t value, unsigned int size);
    [[nodiscard]] int executeOpcode(std::uint32_t opcode, std::uint32_t instructionPc);
    [[nodiscard]] int executeOpcode19(std::uint32_t opcode, std::uint32_t instructionPc);
    [[nodiscard]] int executeOpcode31(std::uint32_t opcode, std::uint32_t instructionPc);
    [[nodiscard]] int executeFloating(std::uint32_t opcode, bool singlePrecision);
    void enterException(Exception exception, std::uint32_t savedPc, std::uint32_t srr1Bits = 0);
    void programException(std::uint32_t savedPc, std::uint32_t cause);
    void updateCr0(std::uint32_t value);
    void updateCrField(unsigned int field, std::uint8_t value);
    [[nodiscard]] bool branchCondition(std::uint32_t opcode);
    [[nodiscard]] std::uint32_t readSpr(unsigned int spr) const;
    [[nodiscard]] bool writeSpr(unsigned int spr, std::uint32_t value);
    void emitTrace(TraceEvent event) const;

    PowerPcBus* m_bus = nullptr;
    RegisterSnapshot m_state;
    TraceSink m_traceSink;
    std::uint64_t m_cycleCount = 0;
    bool m_externalInterrupt = false;
    bool m_decrementerPending = false;
    std::uint32_t m_clockFrequency = 80'000'000;
    std::uint64_t m_rtcCycleRemainder = 0;
    std::uint32_t m_rtcNanosecondRemainder = 0;
    std::uint32_t m_instructionPc = 0;
};

using PpcCpuCore = PowerPc601Core;

} // namespace cutemac::cpu::ppc
