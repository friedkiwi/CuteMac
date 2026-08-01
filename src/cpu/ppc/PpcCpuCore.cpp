#include "cutemac/cpu/ppc/PpcCpuCore.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
#include <utility>

extern "C" {
typedef signed char sint8;
typedef signed short sint16;
typedef signed int sint32;
typedef signed long long sint64;
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long long uint64;
#include "../m68k/engine/softfloat/milieu.h"
#include "../m68k/engine/softfloat/softfloat.h"
}

namespace cutemac::cpu::ppc {
namespace {

constexpr std::uint32_t xerSo = 0x80000000U;
constexpr std::uint32_t xerOv = 0x40000000U;
constexpr std::uint32_t xerCa = 0x20000000U;
constexpr std::uint32_t exceptionPrefixLow = 0x00000000U;
constexpr std::uint32_t exceptionPrefixHigh = 0xfff00000U;
constexpr std::uint32_t programIllegal = 0x00080000U;
constexpr std::uint32_t programPrivileged = 0x00040000U;
constexpr std::uint32_t programTrap = 0x00020000U;

constexpr unsigned rt(std::uint32_t op) { return (op >> 21) & 31; }
constexpr unsigned ra(std::uint32_t op) { return (op >> 16) & 31; }
constexpr unsigned rb(std::uint32_t op) { return (op >> 11) & 31; }
constexpr unsigned xo(std::uint32_t op) { return (op >> 1) & 1023; }
constexpr std::int32_t simm(std::uint32_t op) { return static_cast<std::int16_t>(op); }
constexpr std::uint32_t uimm(std::uint32_t op) { return op & 0xffff; }
constexpr std::uint32_t rotl32(std::uint32_t value, unsigned shift)
{
    shift &= 31;
    return shift == 0 ? value : (value << shift) | (value >> (32 - shift));
}
constexpr std::uint32_t mask32(unsigned mb, unsigned me)
{
    const auto left = 0xffffffffU >> mb;
    const auto right = 0xffffffffU << (31 - me);
    return mb <= me ? left & right : left | right;
}
constexpr std::uint32_t byteSwap32(std::uint32_t value)
{
    return ((value & 0x000000ffU) << 24) | ((value & 0x0000ff00U) << 8)
        | ((value & 0x00ff0000U) >> 8) | ((value & 0xff000000U) >> 24);
}
constexpr std::uint16_t byteSwap16(std::uint16_t value)
{
    return static_cast<std::uint16_t>((value << 8) | (value >> 8));
}
constexpr unsigned decodedSpr(std::uint32_t opcode)
{
    return ((opcode >> 16) & 0x1f) | ((opcode >> 6) & 0x3e0);
}
std::uint32_t addCarry(std::uint32_t a, std::uint32_t b, bool carry, bool& resultCarry)
{
    const auto wide = static_cast<std::uint64_t>(a) + b + (carry ? 1U : 0U);
    resultCarry = (wide >> 32) != 0;
    return static_cast<std::uint32_t>(wide);
}
bool signedOverflowAdd(std::uint32_t a, std::uint32_t b, std::uint32_t result)
{
    return ((~(a ^ b) & (a ^ result)) & 0x80000000U) != 0;
}
constexpr bool isFloat64Nan(std::uint64_t value)
{
    return (value & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL
        && (value & 0x000fffffffffffffULL) != 0;
}
constexpr bool isFloat64SignalingNan(std::uint64_t value)
{
    return isFloat64Nan(value) && (value & 0x0008000000000000ULL) == 0;
}
constexpr bool isFloat64Infinity(std::uint64_t value)
{
    return (value & 0x7fffffffffffffffULL) == 0x7ff0000000000000ULL;
}
constexpr bool isFloat64Zero(std::uint64_t value)
{
    return (value & 0x7fffffffffffffffULL) == 0;
}
constexpr std::uint64_t quietFloat64Nan(std::uint64_t value)
{
    return value | 0x0008000000000000ULL;
}
constexpr std::uint32_t floatingResultFlags(std::uint64_t value)
{
    const bool negative = (value >> 63) != 0;
    const auto exponent = (value >> 52) & 0x7ffU;
    const auto fraction = value & 0x000fffffffffffffULL;
    std::uint32_t classification = 0;
    if (exponent == 0x7ffU)
        classification = fraction ? 0x11U : negative ? 0x09U : 0x05U;
    else if (exponent == 0)
        classification = fraction ? (negative ? 0x18U : 0x14U) : (negative ? 0x12U : 0x02U);
    else
        classification = negative ? 0x08U : 0x04U;
    return classification << 12;
}
constexpr std::uint32_t floatingResultFlagsSingle(std::uint32_t value)
{
    const bool negative = (value >> 31) != 0;
    const auto exponent = (value >> 23) & 0xffU;
    const auto fraction = value & 0x007fffffU;
    std::uint32_t classification = 0;
    if (exponent == 0xffU)
        classification = fraction ? 0x11U : negative ? 0x09U : 0x05U;
    else if (exponent == 0)
        classification = fraction ? (negative ? 0x18U : 0x14U) : (negative ? 0x12U : 0x02U);
    else
        classification = negative ? 0x08U : 0x04U;
    return classification << 12;
}
QString hex32(std::uint32_t value)
{
    return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0'));
}

} // namespace

QString PowerPc601Core::id() const { return QStringLiteral("cpu.ppc601"); }

void PowerPc601Core::reset()
{
    m_state = {};
    m_state.pc = 0xfff00100U;
    m_state.msr = msrMe | msrIp;
    m_externalInterrupt = false;
    m_decrementerPending = false;
    m_rtcCycleRemainder = 0;
    m_rtcNanosecondRemainder = 0;
    m_cycleCount = 0;
}

void PowerPc601Core::setBus(PowerPcBus* bus) { m_bus = bus; }
void PowerPc601Core::setExternalInterrupt(bool asserted) { m_externalInterrupt = asserted; }
void PowerPc601Core::setTraceSink(TraceSink sink) { m_traceSink = std::move(sink); }
void PowerPc601Core::setCycleCount(std::uint64_t cycles) { m_cycleCount = cycles; }
void PowerPc601Core::setClockFrequency(std::uint32_t hz) { m_clockFrequency = std::max(1U, hz); }

void PowerPc601Core::advanceTime(std::uint32_t cycles)
{
    const auto scaledTime = m_rtcCycleRemainder + static_cast<std::uint64_t>(cycles) * 1'000'000'000ULL;
    const auto elapsedNanoseconds = scaledTime / m_clockFrequency;
    m_rtcCycleRemainder = scaledTime % m_clockFrequency;
    const auto tickTime = static_cast<std::uint64_t>(m_rtcNanosecondRemainder) + elapsedNanoseconds;
    const auto ticks = static_cast<std::uint32_t>(tickTime / 128U);
    m_rtcNanosecondRemainder = static_cast<std::uint32_t>(tickTime % 128U);
    const auto oldDec = m_state.dec;
    m_state.dec = (m_state.dec - (ticks << 7)) & 0xffffff80U;
    if ((oldDec & 0x80000000U) == 0 && (m_state.dec & 0x80000000U) != 0)
        m_decrementerPending = true;

    const auto nanoseconds = static_cast<std::uint64_t>(m_state.rtcl) + static_cast<std::uint64_t>(ticks) * 128U;
    m_state.rtcu += static_cast<std::uint32_t>(nanoseconds / 1'000'000'000ULL);
    m_state.rtcl = static_cast<std::uint32_t>(nanoseconds % 1'000'000'000ULL) & 0xffffff80U;
    m_cycleCount += cycles;
}

int PowerPc601Core::execute(int cycles)
{
    int consumed = 0;
    while (consumed < cycles) {
        const auto step = stepInstruction();
        consumed += step;
        advanceTime(static_cast<std::uint32_t>(step));
    }
    return consumed;
}

int PowerPc601Core::stepInstruction()
{
    if (!m_bus)
        return 1;
    if (m_externalInterrupt && (m_state.msr & msrEe)) {
        emitTrace({ TraceEvent::Kind::Interrupt, m_cycleCount, m_state.pc, 0, 0, 0, 0,
            m_state.msr, AccessType::Instruction, TranslationPath::Real,
            Exception::ExternalInterrupt, true });
        enterException(Exception::ExternalInterrupt, m_state.pc);
        return 1;
    }
    if (m_decrementerPending && (m_state.msr & msrEe)) {
        m_decrementerPending = false;
        enterException(Exception::Decrementer, m_state.pc);
        return 1;
    }

    const auto instructionPc = m_state.pc;
    m_instructionPc = instructionPc;
    const auto fetched = read(instructionPc, 4, AccessType::Instruction);
    if (!fetched)
        return 1;
    m_state.pc += 4;
    const auto cycles = executeOpcode(*fetched, instructionPc);
    emitTrace({ TraceEvent::Kind::Instruction, m_cycleCount, instructionPc, *fetched,
        instructionPc, 0, static_cast<std::uint32_t>(cycles), m_state.msr,
        AccessType::Instruction, TranslationPath::Real, Exception::Reset, true });
    if ((m_state.msr & msrSe) != 0)
        enterException(Exception::Trace, m_state.pc);
    return cycles;
}

std::uint32_t PowerPc601Core::programCounter() const { return m_state.pc; }
void PowerPc601Core::setProgramCounter(std::uint32_t address) { m_state.pc = address & ~3U; }
PowerPc601Core::RegisterSnapshot PowerPc601Core::registers() const { return m_state; }
void PowerPc601Core::setRegisters(const RegisterSnapshot& registers) { m_state = registers; }

void PowerPc601Core::emitTrace(TraceEvent event) const
{
    if (m_traceSink)
        m_traceSink(event);
}

std::optional<PowerPc601Core::Translation> PowerPc601Core::translate(
    std::uint32_t address, AccessType access, bool sideEffects)
{
    const bool translated = access == AccessType::Instruction ? (m_state.msr & msrIr) : (m_state.msr & msrDr);
    if (!translated) {
        emitTrace({ TraceEvent::Kind::Translation, m_cycleCount, m_state.pc, 0, address,
            address, 0, m_state.msr, access, TranslationPath::Real, Exception::Reset, true });
        return Translation { address, TranslationPath::Real, false };
    }

    const auto sr = m_state.sr[address >> 28];
    if ((sr & 0x80000000U) != 0 && ((sr >> 20) & 0x1ffU) == 0x7fU) {
        const auto physical = (address & 0x0fffffffU) | (sr << 28);
        emitTrace({ TraceEvent::Kind::Translation, m_cycleCount, m_state.pc, 0, address,
            physical, 0, m_state.msr, access, TranslationPath::Page, Exception::Reset, true });
        return Translation { physical, TranslationPath::Page, true };
    }

    if ((sr & 0x80000000U) == 0) {
        for (unsigned i = 0; i < 4; ++i) {
            const auto lower = m_state.batl[i];
            if ((lower & 0x40U) == 0)
                continue;
            const auto bsm = lower & 0x3fU;
            const auto mask = ~((bsm << 17) | 0x1ffffU);
            if ((address & mask) != (m_state.batu[i] & mask))
                continue;
            const auto key = ((((m_state.batu[i] >> 2) & 1U) != 0 && (m_state.msr & msrPr))
                || (((m_state.batu[i] >> 3) & 1U) != 0 && !(m_state.msr & msrPr))) ? 1U : 0U;
            const auto pp = m_state.batu[i] & 3U;
            const bool writeAccess = access == AccessType::Write;
            const bool denied = (key && (pp == 0 || (pp == 1 && writeAccess))) || (pp == 3 && writeAccess);
            if (denied)
                break;
            const auto physical = (lower & mask) | (address & ~mask);
            emitTrace({ TraceEvent::Kind::Translation, m_cycleCount, m_state.pc, 0, address,
                physical, 0, m_state.msr, access, TranslationPath::Bat, Exception::Reset, true });
            return Translation { physical, TranslationPath::Bat, false };
        }
    }

    if ((sr & 0x10000000U) != 0 && access == AccessType::Instruction) {
        if (sideEffects)
            enterException(Exception::InstructionStorage, m_state.pc, 0x10000000U);
        emitTrace({ TraceEvent::Kind::Translation, m_cycleCount, m_state.pc, 0, address, 0,
            0x10000000U, m_state.msr, access, TranslationPath::Fault,
            Exception::InstructionStorage, false });
        return std::nullopt;
    }

    const auto pageIndex = (address >> 12) & 0xffffU;
    const auto vsid = sr & 0x00ffffffU;
    const auto primaryHash = (sr & 0x7ffffU) ^ pageIndex;
    std::uint32_t pteAddress = 0;
    std::uint32_t pte1 = 0, pte2 = 0;
    bool found = false;
    for (unsigned secondary = 0; secondary < 2 && !found; ++secondary) {
        const auto hash = secondary ? ~primaryHash : primaryHash;
        const auto pteg = (m_state.sdr1 & 0xffff0000U)
            | (((m_state.sdr1 & 0x1ffU) << 16) & ((hash & 0x7fc00U) << 6))
            | ((hash & 0x3ffU) << 6);
        const auto wanted = 0x80000000U | (vsid << 7) | (secondary << 6) | (pageIndex >> 10);
        for (unsigned slot = 0; slot < 8; ++slot) {
            const auto candidate = pteg + slot * 8;
            if (m_bus->read32(candidate) == wanted) {
                pteAddress = candidate;
                pte1 = wanted;
                pte2 = m_bus->read32(candidate + 4);
                found = true;
                break;
            }
        }
    }
    (void)pte1;

    const bool writeAccess = access == AccessType::Write;
    std::uint32_t fault = 0;
    if (!found) {
        fault = 0x40000000U;
    } else {
        const auto key = ((((sr >> 29) & 1U) != 0 && (m_state.msr & msrPr))
            || (((sr >> 30) & 1U) != 0 && !(m_state.msr & msrPr))) ? 1U : 0U;
        const auto pp = pte2 & 3U;
        if ((key && (pp == 0 || (pp == 1 && writeAccess))) || (pp == 3 && writeAccess))
            fault = 0x08000000U;
    }
    if (fault != 0) {
        const auto exception = access == AccessType::Instruction ? Exception::InstructionStorage : Exception::DataStorage;
        if (sideEffects) {
            if (exception == Exception::DataStorage) {
                m_state.dar = address;
                m_state.dsisr = fault | (writeAccess ? 0x02000000U : 0U);
            }
            enterException(exception, access == AccessType::Instruction ? address : m_instructionPc, fault);
        }
        emitTrace({ TraceEvent::Kind::Translation, m_cycleCount, m_state.pc, 0, address, 0,
            fault, m_state.msr, access, TranslationPath::Fault, exception, false });
        return std::nullopt;
    }

    if (sideEffects) {
        pte2 |= 0x00000100U;
        if (writeAccess)
            pte2 |= 0x00000080U;
        m_bus->write32(pteAddress + 4, pte2);
    }
    const auto physical = (pte2 & 0xfffff000U) | (address & 0xfffU);
    emitTrace({ TraceEvent::Kind::Translation, m_cycleCount, m_state.pc, 0, address,
        physical, 0, m_state.msr, access, TranslationPath::Page, Exception::Reset, true });
    return Translation { physical, TranslationPath::Page, (pte2 & 0x08U) != 0 };
}

std::optional<std::uint32_t> PowerPc601Core::translateForDebug(
    std::uint32_t address, AccessType access) const
{
    auto* self = const_cast<PowerPc601Core*>(this);
    const auto result = self->translate(address, access, false);
    return result ? std::optional<std::uint32_t>(result->physicalAddress) : std::nullopt;
}

std::optional<std::uint32_t> PowerPc601Core::read(std::uint32_t address, unsigned size, AccessType access)
{
    if (access == AccessType::Instruction && (address & (size - 1U)) != 0) {
        m_state.dar = address;
        m_state.dsisr = 0;
        enterException(Exception::Alignment, access == AccessType::Instruction ? address : m_instructionPc);
        return std::nullopt;
    }
    const auto translated = translate(address, access, true);
    if (!translated)
        return std::nullopt;
    switch (size) {
    case 1: return m_bus->read8(translated->physicalAddress);
    case 2: return m_bus->read16(translated->physicalAddress);
    default: return m_bus->read32(translated->physicalAddress);
    }
}

bool PowerPc601Core::write(std::uint32_t address, std::uint32_t value, unsigned size)
{
    const auto translated = translate(address, AccessType::Write, true);
    if (!translated)
        return false;
    switch (size) {
    case 1: m_bus->write8(translated->physicalAddress, static_cast<std::uint8_t>(value)); break;
    case 2: m_bus->write16(translated->physicalAddress, static_cast<std::uint16_t>(value)); break;
    default: m_bus->write32(translated->physicalAddress, value); break;
    }
    m_state.reservationValid = false;
    return true;
}

void PowerPc601Core::enterException(Exception exception, std::uint32_t savedPc, std::uint32_t srr1Bits)
{
    const auto oldMsr = m_state.msr;
    m_state.srr0 = savedPc;
    m_state.srr1 = (oldMsr & 0x0000ffffU) | srr1Bits;
    m_state.msr = oldMsr & (msrMe | msrIp);
    m_state.pc = ((oldMsr & msrIp) ? exceptionPrefixHigh : exceptionPrefixLow)
        | static_cast<std::uint32_t>(exception);
    m_state.reservationValid = false;
    emitTrace({ TraceEvent::Kind::Exception, m_cycleCount, savedPc, 0, 0, 0, srr1Bits,
        oldMsr, AccessType::Instruction, TranslationPath::Real, exception, true });
}

void PowerPc601Core::programException(std::uint32_t savedPc, std::uint32_t cause)
{
    enterException(Exception::Program, savedPc, cause);
}

void PowerPc601Core::updateCrField(unsigned field, std::uint8_t value)
{
    const auto shift = 28U - field * 4U;
    m_state.cr = (m_state.cr & ~(0xfU << shift)) | (static_cast<std::uint32_t>(value & 0xfU) << shift);
}

void PowerPc601Core::updateCr0(std::uint32_t value)
{
    std::uint8_t field = static_cast<std::int32_t>(value) < 0 ? 8 : value > 0 ? 4 : 2;
    if (m_state.xer & xerSo)
        field |= 1;
    updateCrField(0, field);
}

bool PowerPc601Core::branchCondition(std::uint32_t opcode)
{
    const auto bo = (opcode >> 21) & 31U;
    const auto bi = (opcode >> 16) & 31U;
    if ((bo & 4U) == 0)
        --m_state.ctr;
    const bool ctrOk = (bo & 4U) != 0 || ((m_state.ctr != 0) != ((bo & 2U) != 0));
    const bool condition = (m_state.cr & (0x80000000U >> bi)) != 0;
    const bool conditionOk = (bo & 16U) != 0 || condition == ((bo & 8U) != 0);
    return ctrOk && conditionOk;
}

int PowerPc601Core::executeOpcode(std::uint32_t op, std::uint32_t instructionPc)
{
    const auto primary = op >> 26;
    auto eaD = [&] { return (ra(op) ? m_state.gpr[ra(op)] : 0U) + static_cast<std::uint32_t>(simm(op)); };
    auto loadD = [&](unsigned size, bool signExtend, bool update) {
        const auto ea = eaD();
        const auto value = read(ea, size, AccessType::Read);
        if (!value) return;
        m_state.gpr[rt(op)] = signExtend && size == 2
            ? static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(*value))) : *value;
        if (update && ra(op) != 0) m_state.gpr[ra(op)] = ea;
    };
    auto storeD = [&](unsigned size, bool update) {
        const auto ea = eaD();
        if (write(ea, m_state.gpr[rt(op)], size) && update && ra(op) != 0) m_state.gpr[ra(op)] = ea;
    };

    switch (primary) {
    case 3: { // twi
        const auto to = rt(op); const auto a = m_state.gpr[ra(op)]; const auto b = static_cast<std::uint32_t>(simm(op));
        const bool trap = ((to & 16) && static_cast<std::int32_t>(a) < static_cast<std::int32_t>(b))
            || ((to & 8) && static_cast<std::int32_t>(a) > static_cast<std::int32_t>(b))
            || ((to & 4) && a == b) || ((to & 2) && a < b) || ((to & 1) && a > b);
        if (trap) programException(instructionPc, programTrap);
        return 1;
    }
    case 7: m_state.gpr[rt(op)] = static_cast<std::uint32_t>(static_cast<std::int32_t>(m_state.gpr[ra(op)]) * simm(op)); return 3;
    case 8: { bool carry; m_state.gpr[rt(op)] = addCarry(~m_state.gpr[ra(op)], static_cast<std::uint32_t>(simm(op)), true, carry); m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa; return 1; }
    case 9: { const auto difference = simm(op) - static_cast<std::int32_t>(m_state.gpr[ra(op)]); m_state.gpr[rt(op)] = difference > 0 ? static_cast<std::uint32_t>(difference) : 0U; return 1; }
    case 10: case 11: {
        const auto field = (op >> 23) & 7U;
        const auto a = m_state.gpr[ra(op)];
        const bool logical = primary == 10;
        const auto b = logical ? uimm(op) : static_cast<std::uint32_t>(simm(op));
        const auto less = logical ? a < b : static_cast<std::int32_t>(a) < static_cast<std::int32_t>(b);
        const auto greater = logical ? a > b : static_cast<std::int32_t>(a) > static_cast<std::int32_t>(b);
        updateCrField(field, static_cast<std::uint8_t>((less ? 8 : greater ? 4 : 2) | ((m_state.xer & xerSo) ? 1 : 0)));
        return 1;
    }
    case 12: case 13: {
        bool carry; const auto result = addCarry(m_state.gpr[ra(op)], static_cast<std::uint32_t>(simm(op)), false, carry);
        m_state.gpr[rt(op)] = result;
        m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa;
        if (primary == 13) updateCr0(result);
        return 1;
    }
    case 14: case 15: {
        const auto base = ra(op) ? m_state.gpr[ra(op)] : 0U;
        const auto immediate = primary == 15 ? static_cast<std::uint32_t>(simm(op)) << 16 : static_cast<std::uint32_t>(simm(op));
        m_state.gpr[rt(op)] = base + immediate; return 1;
    }
    case 16: {
        const auto displacement = static_cast<std::uint32_t>(static_cast<std::int32_t>(op << 16) >> 16);
        const auto target = (op & 2U) ? displacement : instructionPc + displacement;
        if (op & 1U) m_state.lr = instructionPc + 4;
        if (branchCondition(op)) m_state.pc = target & ~3U;
        return 1;
    }
    case 17: enterException(Exception::SystemCall, instructionPc + 4); return 1;
    case 18: {
        const auto displacement = static_cast<std::uint32_t>(static_cast<std::int32_t>(op << 6) >> 6);
        const auto target = (op & 2U) ? displacement : instructionPc + displacement;
        if (op & 1U) m_state.lr = instructionPc + 4;
        m_state.pc = target & ~3U; return 1;
    }
    case 19: return executeOpcode19(op, instructionPc);
    case 20: case 21: case 22: case 23: {
        const auto sh = (primary == 22 || primary == 23) ? (m_state.gpr[rb(op)] & 31U) : rb(op);
        const auto mask = mask32((op >> 6) & 31U, (op >> 1) & 31U);
        const auto rotated = rotl32(m_state.gpr[rt(op)], sh);
        if (primary == 20 || primary == 22) m_state.gpr[ra(op)] = (rotated & mask) | (m_state.gpr[ra(op)] & ~mask);
        else m_state.gpr[ra(op)] = rotated & mask;
        if (op & 1U) updateCr0(m_state.gpr[ra(op)]);
        return 1;
    }
    case 24: m_state.gpr[ra(op)] = m_state.gpr[rt(op)] | uimm(op); return 1;
    case 25: m_state.gpr[ra(op)] = m_state.gpr[rt(op)] | (uimm(op) << 16); return 1;
    case 26: m_state.gpr[ra(op)] = m_state.gpr[rt(op)] ^ uimm(op); return 1;
    case 27: m_state.gpr[ra(op)] = m_state.gpr[rt(op)] ^ (uimm(op) << 16); return 1;
    case 28: m_state.gpr[ra(op)] = m_state.gpr[rt(op)] & uimm(op); updateCr0(m_state.gpr[ra(op)]); return 1;
    case 29: m_state.gpr[ra(op)] = m_state.gpr[rt(op)] & (uimm(op) << 16); updateCr0(m_state.gpr[ra(op)]); return 1;
    case 31: return executeOpcode31(op, instructionPc);
    case 32: loadD(4, false, false); return 2;
    case 33: loadD(4, false, true); return 2;
    case 34: loadD(1, false, false); return 2;
    case 35: loadD(1, false, true); return 2;
    case 36: storeD(4, false); return 2;
    case 37: storeD(4, true); return 2;
    case 38: storeD(1, false); return 2;
    case 39: storeD(1, true); return 2;
    case 40: loadD(2, false, false); return 2;
    case 41: loadD(2, false, true); return 2;
    case 42: loadD(2, true, false); return 2;
    case 43: loadD(2, true, true); return 2;
    case 44: storeD(2, false); return 2;
    case 45: storeD(2, true); return 2;
    case 46: case 47: { // lmw/stmw
        auto ea = eaD();
        for (unsigned reg = rt(op); reg < 32; ++reg, ea += 4) {
            if (primary == 46) { const auto value = read(ea, 4, AccessType::Read); if (!value) break; m_state.gpr[reg] = *value; }
            else if (!write(ea, m_state.gpr[reg], 4)) break;
        }
        return 4;
    }
    case 48: case 49: case 50: case 51: {
        const auto ea = eaD(); const auto size = primary < 50 ? 4U : 8U;
        if ((m_state.msr & msrFp) == 0) { enterException(Exception::FloatingPointUnavailable, instructionPc); return 1; }
        std::uint64_t bits = 0;
        if (size == 4) { const auto value = read(ea, 4, AccessType::Read); if (!value) return 2; bits = float32_to_float64(*value); }
        else { const auto hi = read(ea, 4, AccessType::Read); const auto lo = read(ea + 4, 4, AccessType::Read); if (!hi || !lo) return 2; bits = (std::uint64_t(*hi) << 32) | *lo; }
        m_state.fpr[rt(op)] = bits;
        if ((primary & 1U) && ra(op)) m_state.gpr[ra(op)] = ea;
        return 3;
    }
    case 52: case 53: case 54: case 55: {
        const auto ea = eaD(); const auto size = primary < 54 ? 4U : 8U;
        if ((m_state.msr & msrFp) == 0) { enterException(Exception::FloatingPointUnavailable, instructionPc); return 1; }
        bool ok = size == 4 ? write(ea, float64_to_float32(m_state.fpr[rt(op)]), 4)
            : write(ea, static_cast<std::uint32_t>(m_state.fpr[rt(op)] >> 32), 4)
                && write(ea + 4, static_cast<std::uint32_t>(m_state.fpr[rt(op)]), 4);
        if (ok && (primary & 1U) && ra(op)) m_state.gpr[ra(op)] = ea;
        return 3;
    }
    case 59: return executeFloating(op, true);
    case 63: return executeFloating(op, false);
    default: programException(instructionPc, programIllegal); return 1;
    }
}

int PowerPc601Core::executeOpcode19(std::uint32_t op, std::uint32_t instructionPc)
{
    switch (xo(op)) {
    case 0: { // mcrf
        const auto source = (op >> 18) & 7U, dest = (op >> 23) & 7U;
        updateCrField(dest, static_cast<std::uint8_t>((m_state.cr >> (28 - source * 4)) & 0xfU));
        return 1;
    }
    case 16: { // bclr
        const auto target = m_state.lr;
        if (op & 1U) m_state.lr = instructionPc + 4;
        if (branchCondition(op)) m_state.pc = target & ~3U;
        return 1;
    }
    case 33: case 129: case 193: case 225: case 257: case 289: case 417: case 449: {
        const auto bitA = (m_state.cr >> (31 - ((op >> 16) & 31U))) & 1U;
        const auto bitB = (m_state.cr >> (31 - ((op >> 11) & 31U))) & 1U;
        bool result = false;
        switch (xo(op)) {
        case 33: result = !(bitA || bitB); break;      // crnor
        case 129: result = bitA && !bitB; break;      // crandc
        case 193: result = bitA ^ bitB; break;        // crxor
        case 225: result = !(bitA && bitB); break;    // crnand
        case 257: result = bitA && bitB; break;       // crand
        case 289: result = !(bitA ^ bitB); break;     // creqv
        case 417: result = bitA || !bitB; break;      // crorc
        case 449: result = bitA || bitB; break;       // cror
        }
        const auto destMask = 0x80000000U >> ((op >> 21) & 31U);
        m_state.cr = result ? m_state.cr | destMask : m_state.cr & ~destMask;
        return 1;
    }
    case 50: // rfi
        if (m_state.msr & msrPr) { programException(instructionPc, programPrivileged); return 1; }
        m_state.msr = m_state.srr1 & 0x0000ffffU;
        m_state.pc = m_state.srr0 & ~3U;
        return 2;
    case 150: // isync
        return 1;
    case 528: { // bcctr
        const auto target = m_state.ctr;
        if (op & 1U) m_state.lr = instructionPc + 4;
        // CTR-decrement forms are invalid for bcctr.
        if (((op >> 21) & 4U) == 0) { programException(instructionPc, programIllegal); return 1; }
        if (branchCondition(op)) m_state.pc = target & ~3U;
        return 1;
    }
    default: programException(instructionPc, programIllegal); return 1;
    }
}

std::uint32_t PowerPc601Core::readSpr(unsigned spr) const
{
    switch (spr) {
    case 0: return m_state.mq;
    case 1: return m_state.xer;
    case 4: case 20: return m_state.rtcu;
    case 5: case 21: return m_state.rtcl;
    case 6: case 22: return m_state.dec;
    case 8: return m_state.lr;
    case 9: return m_state.ctr;
    case 18: return m_state.dsisr;
    case 19: return m_state.dar;
    case 25: return m_state.sdr1;
    case 26: return m_state.srr0;
    case 27: return m_state.srr1;
    case 272: case 273: case 274: case 275: return m_state.sprg[spr - 272];
    case 287: return 0x00010001U;
    case 1008: return m_state.hid0;
    case 1009: return m_state.hid1;
    default:
        if (spr >= 528 && spr <= 535)
            return (spr & 1U) ? m_state.batl[(spr - 528) / 2] : m_state.batu[(spr - 528) / 2];
        return 0;
    }
}

bool PowerPc601Core::writeSpr(unsigned spr, std::uint32_t value)
{
    switch (spr) {
    case 0: m_state.mq = value; return true;
    case 1: m_state.xer = value; return true;
    case 8: m_state.lr = value; return true;
    case 9: m_state.ctr = value; return true;
    case 18: m_state.dsisr = value; return true;
    case 19: m_state.dar = value; return true;
    case 20: m_state.rtcu = value; return true;
    case 21: m_state.rtcl = value & 0xffffff80U; return true;
    case 22: {
        const auto old = m_state.dec;
        m_state.dec = value & 0xffffff80U;
        m_decrementerPending = (old & 0x80000000U) == 0 && (m_state.dec & 0x80000000U) != 0;
        return true;
    }
    case 25: m_state.sdr1 = value; return true;
    case 26: m_state.srr0 = value; return true;
    case 27: m_state.srr1 = value; return true;
    case 272: case 273: case 274: case 275: m_state.sprg[spr - 272] = value; return true;
    case 1008: m_state.hid0 = value; return true;
    case 1009: m_state.hid1 = value; return true;
    default:
        if (spr >= 528 && spr <= 535) {
            auto& target = (spr & 1U) ? m_state.batl[(spr - 528) / 2] : m_state.batu[(spr - 528) / 2];
            target = value;
            return true;
        }
        return false;
    }
}

int PowerPc601Core::executeOpcode31(std::uint32_t op, std::uint32_t instructionPc)
{
    const auto a = m_state.gpr[ra(op)], b = m_state.gpr[rb(op)];
    const bool rc = (op & 1U) != 0, oe = (op & 0x400U) != 0;
    auto finish = [&](unsigned dest, std::uint32_t value) {
        m_state.gpr[dest] = value;
        if (rc) updateCr0(value);
    };
    auto setOverflow = [&](bool overflow) {
        if (!oe) return;
        m_state.xer = overflow ? (m_state.xer | xerOv | xerSo) : (m_state.xer & ~xerOv);
    };
    auto eaX = [&] { return (ra(op) ? a : 0U) + b; };

    switch (xo(op)) {
    case 0: case 32: { // cmp/cmpl
        const auto field = (op >> 23) & 7U;
        const bool logical = xo(op) == 32;
        const bool less = logical ? a < b : static_cast<std::int32_t>(a) < static_cast<std::int32_t>(b);
        const bool greater = logical ? a > b : static_cast<std::int32_t>(a) > static_cast<std::int32_t>(b);
        updateCrField(field, static_cast<std::uint8_t>((less ? 8 : greater ? 4 : 2) | ((m_state.xer & xerSo) ? 1 : 0)));
        return 1;
    }
    case 4: { // tw
        const auto to = rt(op);
        const bool trap = ((to & 16) && static_cast<std::int32_t>(a) < static_cast<std::int32_t>(b))
            || ((to & 8) && static_cast<std::int32_t>(a) > static_cast<std::int32_t>(b))
            || ((to & 4) && a == b) || ((to & 2) && a < b) || ((to & 1) && a > b);
        if (trap) programException(instructionPc, programTrap);
        return 1;
    }
    case 8: case 520: { // subfc/subfco
        bool carry; const auto result = addCarry(~a, b, true, carry);
        m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa;
        setOverflow(signedOverflowAdd(~a, b, result)); finish(rt(op), result); return 1;
    }
    case 10: case 522: { // addc/addco
        bool carry; const auto result = addCarry(a, b, false, carry);
        m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa;
        setOverflow(signedOverflowAdd(a, b, result)); finish(rt(op), result); return 1;
    }
    case 11: finish(rt(op), static_cast<std::uint32_t>((static_cast<std::uint64_t>(a) * b) >> 32)); return 3; // mulhwu
    case 19: finish(rt(op), m_state.cr); return 1; // mfcr
    case 20: { // lwarx
        const auto ea = eaX();
        if (ea & 3U) { m_state.dar = ea; m_state.dsisr = 0; enterException(Exception::Alignment, instructionPc); return 1; }
        const auto value = read(ea, 4, AccessType::Read);
        if (value) { m_state.gpr[rt(op)] = *value; m_state.reservationAddress = ea & ~31U; m_state.reservationValid = true; }
        return 2;
    }
    case 23: case 55: case 87: case 119: case 279: case 311: case 343: case 375: {
        const auto ea = eaX();
        const unsigned size = (xo(op) == 87 || xo(op) == 119) ? 1U : (xo(op) == 279 || xo(op) == 311 || xo(op) == 343 || xo(op) == 375) ? 2U : 4U;
        const auto value = read(ea, size, AccessType::Read); if (!value) return 2;
        auto result = *value;
        if (xo(op) == 343 || xo(op) == 375) result = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(result)));
        m_state.gpr[rt(op)] = result;
        if ((xo(op) == 55 || xo(op) == 119 || xo(op) == 311 || xo(op) == 375) && ra(op)) m_state.gpr[ra(op)] = ea;
        return 2;
    }
    case 24: finish(ra(op), (b & 0x20U) ? 0U : m_state.gpr[rt(op)] << (b & 31U)); return 1; // slw
    case 26: finish(ra(op), a == 0 ? 32U : static_cast<std::uint32_t>(__builtin_clz(a))); return 1;
    case 28: finish(ra(op), m_state.gpr[rt(op)] & b); return 1;
    case 29: finish(ra(op), mask32(m_state.gpr[rt(op)] & 31U, b & 31U)); return 1; // maskg
    case 54: case 86: case 246: case 278: case 982:
        return 1; // dcbst/dcbf/dcbtst/dcbt/icbi: coherent portable interpreter
    case 40: case 552: { const auto result = b - a; setOverflow(((a ^ b) & (b ^ result) & 0x80000000U) != 0); finish(rt(op), result); return 1; }
    case 60: finish(ra(op), m_state.gpr[rt(op)] & ~b); return 1;
    case 75: finish(rt(op), static_cast<std::uint32_t>((static_cast<std::int64_t>(static_cast<std::int32_t>(a)) * static_cast<std::int32_t>(b)) >> 32)); return 3;
    case 83: // mfmsr
        if (m_state.msr & msrPr) { programException(instructionPc, programPrivileged); return 1; }
        finish(rt(op), m_state.msr); return 1;
    case 104: case 616: { const auto result = 0U - a; setOverflow(a == 0x80000000U); finish(rt(op), result); return 1; }
    case 124: finish(ra(op), ~(m_state.gpr[rt(op)] | b)); return 1;
    case 136: case 648: { // subfe/subfeo
        bool carry; const auto result = addCarry(~a, b, (m_state.xer & xerCa) != 0, carry);
        m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa;
        setOverflow(signedOverflowAdd(~a, b, result)); finish(rt(op), result); return 1;
    }
    case 138: case 650: { bool carry; const auto result = addCarry(a, b, (m_state.xer & xerCa) != 0, carry); m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa; setOverflow(signedOverflowAdd(a, b, result)); finish(rt(op), result); return 1; }
    case 144: { // mtcrf
        const auto fxm = (op >> 12) & 0xffU;
        for (unsigned field = 0; field < 8; ++field) if (fxm & (0x80U >> field)) updateCrField(field, static_cast<std::uint8_t>((m_state.gpr[rt(op)] >> (28 - field * 4)) & 0xfU));
        return 1;
    }
    case 146: // mtmsr
        if (m_state.msr & msrPr) { programException(instructionPc, programPrivileged); return 1; }
        m_state.msr = m_state.gpr[rt(op)] & 0x0000ffffU;
        return 1;
    case 150: { // stwcx.
        const auto ea = eaX(); const bool valid = m_state.reservationValid && m_state.reservationAddress == (ea & ~31U);
        m_state.reservationValid = false;
        if (ea & 3U) { m_state.dar = ea; m_state.dsisr = 0x02000000U; enterException(Exception::Alignment, instructionPc); return 1; }
        const bool stored = valid && write(ea, m_state.gpr[rt(op)], 4);
        updateCrField(0, static_cast<std::uint8_t>((stored ? 2 : 0) | ((m_state.xer & xerSo) ? 1 : 0)));
        return 2;
    }
    case 151: case 183: case 215: case 247: case 407: case 439: {
        const auto ea = eaX();
        const unsigned size = (xo(op) == 215 || xo(op) == 247) ? 1U : (xo(op) == 407 || xo(op) == 439) ? 2U : 4U;
        if (write(ea, m_state.gpr[rt(op)], size) && (xo(op) == 183 || xo(op) == 247 || xo(op) == 439) && ra(op)) m_state.gpr[ra(op)] = ea;
        return 2;
    }
    case 200: case 712: { const bool ca = (m_state.xer & xerCa) != 0; bool carry; const auto result = addCarry(~a, 0, ca, carry); m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa; setOverflow(a == 0x80000000U && ca); finish(rt(op), result); return 1; }
    case 202: case 714: { const bool ca = (m_state.xer & xerCa) != 0; bool carry; const auto result = addCarry(a, 0, ca, carry); m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa; setOverflow(a == 0x7fffffffU && ca); finish(rt(op), result); return 1; }
    case 210: // mtsr
        if (m_state.msr & msrPr) { programException(instructionPc, programPrivileged); return 1; }
        m_state.sr[ra(op) & 15U] = m_state.gpr[rt(op)];
        return 1;
    case 232: case 744: { const bool ca = (m_state.xer & xerCa) != 0; bool carry; const auto result = addCarry(~a, 0xffffffffU, ca, carry); m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa; setOverflow(a == 0x7fffffffU && !ca); finish(rt(op), result); return 1; }
    case 234: case 746: { const bool ca = (m_state.xer & xerCa) != 0; bool carry; const auto result = addCarry(a, 0xffffffffU, ca, carry); m_state.xer = carry ? m_state.xer | xerCa : m_state.xer & ~xerCa; setOverflow(a == 0x80000000U && !ca); finish(rt(op), result); return 1; }
    case 242: // mtsrin
        if (m_state.msr & msrPr) { programException(instructionPc, programPrivileged); return 1; }
        m_state.sr[b >> 28] = m_state.gpr[rt(op)];
        return 1;
    case 235: case 747: { const auto product = static_cast<std::int64_t>(static_cast<std::int32_t>(a)) * static_cast<std::int32_t>(b); const auto result = static_cast<std::uint32_t>(product); setOverflow(product != static_cast<std::int32_t>(result)); finish(rt(op), result); return 3; }
    case 264: case 776: { const auto difference = static_cast<std::int64_t>(static_cast<std::int32_t>(b)) - static_cast<std::int32_t>(a); const auto result = difference > 0 ? static_cast<std::uint32_t>(difference) : 0U; setOverflow(difference > INT32_MAX); finish(rt(op), result); return 1; }
    case 266: case 778: { const auto result = a + b; setOverflow(signedOverflowAdd(a, b, result)); finish(rt(op), result); return 1; }
    case 284: finish(ra(op), ~(m_state.gpr[rt(op)] ^ b)); return 1;
    case 306: // 601 tlbi: interpreter has no cached TLB entries
        if (m_state.msr & msrPr) programException(instructionPc, programPrivileged);
        return 1;
    case 316: finish(ra(op), m_state.gpr[rt(op)] ^ b); return 1;
    case 339: { // mfspr
        const auto spr = decodedSpr(op);
        if ((m_state.msr & msrPr) && spr != 4 && spr != 5 && spr != 6) { programException(instructionPc, programPrivileged); return 1; }
        finish(rt(op), readSpr(spr)); return 1;
    }
    case 360: case 872: { const auto result = static_cast<std::int32_t>(a) < 0 ? 0U - a : a; setOverflow(a == 0x80000000U); finish(rt(op), result); return 1; } // abs
    case 371: { // mftb/601 RTC aliases
        finish(rt(op), readSpr(decodedSpr(op))); return 1;
    }
    case 412: finish(ra(op), m_state.gpr[rt(op)] | ~b); return 1;
    case 444: finish(ra(op), m_state.gpr[rt(op)] | b); return 1;
    case 470: // dcbi
        if (m_state.msr & msrPr) programException(instructionPc, programPrivileged);
        return 1;
    case 459: case 971: { const bool invalid = b == 0; const auto result = invalid ? 0U : a / b; setOverflow(invalid); finish(rt(op), result); return 20; }
    case 467: { // mtspr
        if (m_state.msr & msrPr) { programException(instructionPc, programPrivileged); return 1; }
        if (!writeSpr(decodedSpr(op), m_state.gpr[rt(op)])) programException(instructionPc, programIllegal);
        return 1;
    }
    case 476: finish(ra(op), ~(m_state.gpr[rt(op)] & b)); return 1;
    case 488: case 1000: { const auto result = static_cast<std::int32_t>(a) > 0 ? 0U - a : a; setOverflow(a == 0x80000000U); finish(rt(op), result); return 1; } // nabs
    case 491: case 1003: { const bool invalid = b == 0 || (a == 0x80000000U && b == 0xffffffffU); const auto result = invalid ? (static_cast<std::int32_t>(a) < 0 ? 0xffffffffU : 0U) : static_cast<std::uint32_t>(static_cast<std::int32_t>(a) / static_cast<std::int32_t>(b)); setOverflow(invalid); finish(rt(op), result); return 20; }
    case 512: { const auto field = (op >> 23) & 7U; updateCrField(field, static_cast<std::uint8_t>((m_state.fpscr >> (28 - field * 4)) & 0xfU)); return 1; }
    case 533: { // lswx (minimal architecturally ordered byte sequence)
        auto ea = eaX(); auto count = m_state.xer & 0x7fU; auto reg = rt(op); unsigned shift = 24;
        m_state.gpr[reg] = 0;
        while (count--) { const auto byte = read(ea++, 1, AccessType::Read); if (!byte) break; m_state.gpr[reg] |= *byte << shift; if (shift == 0) { reg = (reg + 1) & 31U; m_state.gpr[reg] = 0; shift = 24; } else shift -= 8; }
        return 4;
    }
    case 534: { // lwbrx
        const auto value = read(eaX(), 4, AccessType::Read); if (value) m_state.gpr[rt(op)] = byteSwap32(*value); return 2;
    }
    case 537: { const auto bit = 0x80000000U >> (b & 31U); finish(ra(op), (a & ~bit) | ((m_state.gpr[rt(op)] & 0x80000000U) ? bit : 0U)); return 1; } // rrib
    case 535: { // lfsx
        if (!(m_state.msr & msrFp)) { enterException(Exception::FloatingPointUnavailable, instructionPc); return 1; }
        const auto value = read(eaX(), 4, AccessType::Read); if (value) m_state.fpr[rt(op)] = float32_to_float64(*value); return 3;
    }
    case 536: finish(ra(op), (b & 0x20U) ? 0U : m_state.gpr[rt(op)] >> (b & 31U)); return 1;
    case 541: finish(ra(op), (m_state.gpr[rt(op)] & b) | (a & ~b)); return 1; // maskir
    case 566: return 1; // tlbsync
    case 567: { // lfsux
        if (!(m_state.msr & msrFp)) { enterException(Exception::FloatingPointUnavailable, instructionPc); return 1; }
        const auto ea = eaX(); const auto value = read(ea, 4, AccessType::Read); if (value) { m_state.fpr[rt(op)] = float32_to_float64(*value); if (ra(op)) m_state.gpr[ra(op)] = ea; } return 3;
    }
    case 595: { // mfsr
        if (m_state.msr & msrPr) { programException(instructionPc, programPrivileged); return 1; }
        finish(rt(op), m_state.sr[ra(op) & 15U]); return 1;
    }
    case 597: { // lswi
        auto ea = ra(op) ? a : 0U; auto count = rb(op) ? rb(op) : 32U; auto reg = rt(op); unsigned shift = 24; m_state.gpr[reg] = 0;
        while (count--) { const auto byte = read(ea++, 1, AccessType::Read); if (!byte) break; m_state.gpr[reg] |= *byte << shift; if (shift == 0) { reg = (reg + 1) & 31U; m_state.gpr[reg] = 0; shift = 24; } else shift -= 8; } return 4;
    }
    case 598: return 1; // sync
    case 599: { // lfdx
        if (!(m_state.msr & msrFp)) { enterException(Exception::FloatingPointUnavailable, instructionPc); return 1; }
        const auto ea = eaX(); const auto hi = read(ea, 4, AccessType::Read), lo = read(ea + 4, 4, AccessType::Read); if (hi && lo) m_state.fpr[rt(op)] = (std::uint64_t(*hi) << 32) | *lo; return 3;
    }
    case 659: { // mfsrin
        if (m_state.msr & msrPr) { programException(instructionPc, programPrivileged); return 1; }
        finish(rt(op), m_state.sr[b >> 28]); return 1;
    }
    case 662: { const auto value = read(eaX(), 2, AccessType::Read); if (value) m_state.gpr[rt(op)] = byteSwap16(static_cast<std::uint16_t>(*value)); return 2; }
    case 695: case 727: case 759: case 983: { // indexed FP loads/stores subset
        if (!(m_state.msr & msrFp)) { enterException(Exception::FloatingPointUnavailable, instructionPc); return 1; }
        const auto ea = eaX();
        if (xo(op) == 695) { const auto hi = read(ea, 4, AccessType::Read), lo = read(ea + 4, 4, AccessType::Read); if (hi && lo) { m_state.fpr[rt(op)] = (std::uint64_t(*hi) << 32) | *lo; if (ra(op)) m_state.gpr[ra(op)] = ea; } }
        else if (xo(op) == 727) (void)write(ea, float64_to_float32(m_state.fpr[rt(op)]), 4);
        else if (xo(op) == 759) { if (write(ea, float64_to_float32(m_state.fpr[rt(op)]), 4) && ra(op)) m_state.gpr[ra(op)] = ea; }
        else (void)write(ea, static_cast<std::uint32_t>(m_state.fpr[rt(op)]), 4);
        return 3;
    }
    case 792: { // sraw
        const auto shift = b & 0x3fU; std::uint32_t result;
        if (shift >= 32) { result = static_cast<std::int32_t>(m_state.gpr[rt(op)]) < 0 ? 0xffffffffU : 0; m_state.xer = (result && m_state.gpr[rt(op)]) ? m_state.xer | xerCa : m_state.xer & ~xerCa; }
        else { result = static_cast<std::uint32_t>(static_cast<std::int32_t>(m_state.gpr[rt(op)]) >> shift); const auto lost = shift ? m_state.gpr[rt(op)] & ((1U << shift) - 1U) : 0; m_state.xer = (static_cast<std::int32_t>(m_state.gpr[rt(op)]) < 0 && lost) ? m_state.xer | xerCa : m_state.xer & ~xerCa; }
        finish(ra(op), result); return 1;
    }
    case 824: { const auto shift = rb(op); const auto source = m_state.gpr[rt(op)]; const auto result = static_cast<std::uint32_t>(static_cast<std::int32_t>(source) >> shift); const auto lost = shift ? source & ((1U << shift) - 1U) : 0; m_state.xer = (static_cast<std::int32_t>(source) < 0 && lost) ? m_state.xer | xerCa : m_state.xer & ~xerCa; finish(ra(op), result); return 1; }
    case 854: return 1; // eieio
    case 918: { // sthbrx
        return write(eaX(), byteSwap16(static_cast<std::uint16_t>(m_state.gpr[rt(op)])), 2) ? 2 : 1;
    }
    case 922: finish(ra(op), static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(m_state.gpr[rt(op)])))); return 1;
    case 954: finish(ra(op), static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(m_state.gpr[rt(op)])))); return 1;
    case 978: { // tlbie: no cached TLB in interpreter
        if (m_state.msr & msrPr) programException(instructionPc, programPrivileged); return 1;
    }
    case 1014: { // dcbz, 32-byte 601 cache block
        const auto base = eaX() & ~31U; for (unsigned offset = 0; offset < 32; offset += 4) if (!write(base + offset, 0, 4)) break; return 8;
    }
    default: programException(instructionPc, programIllegal); return 1;
    }
}

int PowerPc601Core::executeFloating(std::uint32_t op, bool singlePrecision)
{
    if ((m_state.msr & msrFp) == 0) {
        enterException(Exception::FloatingPointUnavailable, m_state.pc - 4);
        return 1;
    }
    const auto frt = rt(op), fra = ra(op), frb = rb(op), frc = (op >> 6) & 31U;
    const auto subop = xo(op);
    const auto a = static_cast<float64>(m_state.fpr[fra]);
    const auto b = static_cast<float64>(m_state.fpr[frb]);
    const auto c = static_cast<float64>(m_state.fpr[frc]);

    static constexpr int roundingModes[] = {
        float_round_nearest_even, float_round_to_zero, float_round_up, float_round_down
    };
    float_rounding_mode = roundingModes[m_state.fpscr & 3U];
    float_exception_flags = 0;
    m_state.fpscr &= ~0x0007f000U; // FR, FI and FPRF are non-sticky.
    float64 result = 0;
    bool writesResult = true;
    std::uint32_t invalidCause = 0;

    // A-form opcodes use only the low five XO bits.
    switch (subop & 31U) {
    case 18: result = float64_div(a, b); break;
    case 20: result = float64_sub(a, b); break;
    case 21: result = float64_add(a, b); break;
    case 25: result = float64_mul(a, c); break;
    case 28: result = float64_sub(float64_mul(a, c), b); break;
    case 29: result = float64_add(float64_mul(a, c), b); break;
    case 30: result = float64_sub(float64_mul(a, c), b) ^ 0x8000000000000000ULL; break;
    case 31: result = float64_add(float64_mul(a, c), b) ^ 0x8000000000000000ULL; break;
    default: writesResult = false; break;
    }

    if (!writesResult && !singlePrecision) {
        switch (subop) {
        case 0: case 32: { // fcmpu/fcmpo
            const bool unordered = isFloat64Nan(a) || isFloat64Nan(b);
            const auto field = unordered ? 1U : float64_lt(a, b) ? 8U : float64_eq(a, b) ? 2U : 4U;
            updateCrField((op >> 23) & 7U, static_cast<std::uint8_t>(field));
            m_state.fpscr = (m_state.fpscr & ~0x0001f000U) | (field << 12);
            if (unordered && (isFloat64SignalingNan(a) || isFloat64SignalingNan(b))) {
                m_state.fpscr |= 0xa1000000U;
            } else if (subop == 32 && unordered) {
                m_state.fpscr |= 0xa0080000U;
            }
            return 3;
        }
        case 12: result = b & 0x7fffffffffffffffULL; writesResult = true; break;
        case 14: result = static_cast<std::uint32_t>(float64_to_int32(b)); writesResult = true; break;
        case 15: result = static_cast<std::uint32_t>(float64_to_int32_round_to_zero(b)); writesResult = true; break;
        case 40: result = b ^ 0x8000000000000000ULL; writesResult = true; break;
        case 72: result = b; writesResult = true; break;
        case 136: result = b | 0x8000000000000000ULL; writesResult = true; break;
        case 264: result = (m_state.cr & (0x80000000U >> ((op >> 16) & 31U))) ? a : b; writesResult = true; break;
        case 583: m_state.fpscr = static_cast<std::uint32_t>(b); return 1; // mffs approximation
        case 711: { // mtfsf
            const auto mask = (op >> 17) & 0xffU;
            const auto source = static_cast<std::uint32_t>(b);
            for (unsigned field = 0; field < 8; ++field) if (mask & (0x80U >> field)) {
                const auto shift = 28 - field * 4; m_state.fpscr = (m_state.fpscr & ~(0xfU << shift)) | (source & (0xfU << shift));
            }
            return 1;
        }
        default: break;
        }
    }
    if (!writesResult) {
        programException(m_state.pc - 4, programIllegal);
        return 1;
    }

    const auto arithmeticKind = subop & 31U;
    const bool usesC = arithmeticKind == 25 || arithmeticKind >= 28;
    const bool anyNan = isFloat64Nan(a) || isFloat64Nan(b) || (usesC && isFloat64Nan(c));
    const bool anySignalingNan = isFloat64SignalingNan(a) || isFloat64SignalingNan(b)
        || (usesC && isFloat64SignalingNan(c));
    if (anyNan) {
        result = quietFloat64Nan(isFloat64Nan(a) ? a : isFloat64Nan(b) ? b : c);
        if (anySignalingNan) invalidCause = 0x01000000U; // VXSNAN
        else float_exception_flags &= ~float_flag_invalid;
    } else if ((arithmeticKind == 20 || arithmeticKind == 21)
        && isFloat64Infinity(a) && isFloat64Infinity(b)
        && (((a ^ b) >> 63) != (arithmeticKind == 20 ? 1U : 0U))) {
        result = 0x7ff8000000000000ULL; invalidCause = 0x00800000U; // VXISI
    } else if (arithmeticKind == 18 && isFloat64Infinity(a) && isFloat64Infinity(b)) {
        result = 0x7ff8000000000000ULL; invalidCause = 0x00400000U; // VXIDI
    } else if (arithmeticKind == 18 && isFloat64Zero(a) && isFloat64Zero(b)) {
        result = 0x7ff8000000000000ULL; invalidCause = 0x00200000U; // VXZDZ
    } else if ((arithmeticKind == 25 || arithmeticKind >= 28)
        && ((isFloat64Zero(a) && isFloat64Infinity(c)) || (isFloat64Infinity(a) && isFloat64Zero(c)))) {
        result = 0x7ff8000000000000ULL; invalidCause = 0x00100000U; // VXIMZ
    }
    if (invalidCause != 0) float_exception_flags |= float_flag_invalid;

    std::uint32_t singleResult = 0;
    if (singlePrecision) {
        singleResult = float64_to_float32(result);
        result = float32_to_float64(singleResult);
    }
    if (float_exception_flags & float_flag_invalid) m_state.fpscr |= 0x20000000U;
    m_state.fpscr |= invalidCause;
    if (float_exception_flags & float_flag_overflow) m_state.fpscr |= 0x10000000U;
    if (float_exception_flags & float_flag_underflow) m_state.fpscr |= 0x08000000U;
    if (float_exception_flags & float_flag_divbyzero) m_state.fpscr |= 0x04000000U;
    if (float_exception_flags & float_flag_inexact) m_state.fpscr |= 0x02000000U;
    if (float_exception_flags) m_state.fpscr |= 0x80000000U;
    if (float_exception_flags & float_flag_inexact) m_state.fpscr |= 0x00020000U;
    if ((float_exception_flags & float_flag_inexact)
        && (((m_state.fpscr & 3U) == 2U && (result >> 63) == 0)
            || ((m_state.fpscr & 3U) == 3U && (result >> 63) != 0)))
        m_state.fpscr |= 0x00040000U;
    const bool enabledInvalid = invalidCause != 0 && (m_state.fpscr & 0x80U) != 0;
    if (enabledInvalid) {
        m_state.fpscr |= 0x40000000U;
    } else {
        m_state.fpr[frt] = result;
        m_state.fpscr = (m_state.fpscr & ~0x0001f000U)
            | (singlePrecision ? floatingResultFlagsSingle(singleResult) : floatingResultFlags(result));
    }
    if (op & 1U) updateCrField(1, static_cast<std::uint8_t>((m_state.fpscr >> 28) & 0xfU));
    return 3;
}

QString PowerPc601Core::disassemble(std::uint32_t address) const
{
    if (!m_bus)
        return QStringLiteral("<no bus>");
    auto* self = const_cast<PowerPc601Core*>(this);
    const auto translated = self->translate(address, AccessType::Instruction, false);
    return translated ? disassembleOpcode(address, m_bus->read32(translated->physicalAddress))
                      : QStringLiteral("<translation fault>");
}

QStringList PowerPc601Core::debugRegisterLines() const
{
    QStringList lines;
    for (unsigned first = 0; first < 32; first += 4) {
        QString line;
        for (unsigned reg = first; reg < first + 4; ++reg) {
            if (!line.isEmpty()) line += QLatin1Char(' ');
            line += QStringLiteral("R%1=%2").arg(reg, 2, 10, QLatin1Char('0')).arg(hex32(m_state.gpr[reg]));
        }
        lines.append(line);
    }
    lines.append(QStringLiteral("PC=%1 MSR=%2 CR=%3 XER=%4").arg(
        hex32(m_state.pc), hex32(m_state.msr), hex32(m_state.cr), hex32(m_state.xer)));
    lines.append(QStringLiteral("LR=%1 CTR=%2 MQ=%3 SRR0=%4 SRR1=%5").arg(
        hex32(m_state.lr), hex32(m_state.ctr), hex32(m_state.mq), hex32(m_state.srr0), hex32(m_state.srr1)));
    lines.append(QStringLiteral("DAR=%1 DSISR=%2 SDR1=%3 DEC=%4 RTCU=%5 RTCL=%6 FPSCR=%7").arg(
        hex32(m_state.dar), hex32(m_state.dsisr), hex32(m_state.sdr1), hex32(m_state.dec),
        hex32(m_state.rtcu), hex32(m_state.rtcl), hex32(m_state.fpscr)));
    for (unsigned i = 0; i < 4; ++i)
        lines.append(QStringLiteral("BAT%1U=%2 BAT%1L=%3").arg(i).arg(hex32(m_state.batu[i]), hex32(m_state.batl[i])));
    return lines;
}

QString PowerPc601Core::disassembleOpcode(std::uint32_t address, std::uint32_t op) const
{
    const auto primary = op >> 26;
    const auto rD = QStringLiteral("r%1").arg(rt(op));
    const auto rA = QStringLiteral("r%1").arg(ra(op));
    const auto rB = QStringLiteral("r%1").arg(rb(op));
    auto dform = [&](const QString& mnemonic) {
        return QStringLiteral("%1 %2,%3(%4)").arg(mnemonic, rD).arg(simm(op)).arg(rA);
    };
    switch (primary) {
    case 7: return QStringLiteral("mulli %1,%2,%3").arg(rD, rA).arg(simm(op));
    case 8: return QStringLiteral("subfic %1,%2,%3").arg(rD, rA).arg(simm(op));
    case 10: return QStringLiteral("cmplwi cr%1,%2,%3").arg((op >> 23) & 7U).arg(rA).arg(uimm(op));
    case 11: return QStringLiteral("cmpwi cr%1,%2,%3").arg((op >> 23) & 7U).arg(rA).arg(simm(op));
    case 12: return QStringLiteral("addic %1,%2,%3").arg(rD, rA).arg(simm(op));
    case 13: return QStringLiteral("addic. %1,%2,%3").arg(rD, rA).arg(simm(op));
    case 14: return ra(op) == 0 ? QStringLiteral("li %1,%2").arg(rD).arg(simm(op)) : QStringLiteral("addi %1,%2,%3").arg(rD, rA).arg(simm(op));
    case 15: return ra(op) == 0 ? QStringLiteral("lis %1,%2").arg(rD).arg(simm(op)) : QStringLiteral("addis %1,%2,%3").arg(rD, rA).arg(simm(op));
    case 16: return QStringLiteral("bc %1,%2,%3").arg((op >> 21) & 31U).arg((op >> 16) & 31U).arg(hex32(address + static_cast<std::uint32_t>(static_cast<std::int32_t>(op << 16) >> 16)));
    case 17: return QStringLiteral("sc");
    case 18: { const auto disp = static_cast<std::uint32_t>(static_cast<std::int32_t>(op << 6) >> 6); return QStringLiteral("b%1 %2").arg(op & 1U ? QStringLiteral("l") : QString()).arg(hex32((op & 2U) ? disp : address + disp)); }
    case 24: return QStringLiteral("ori %1,%2,%3").arg(rA, rD).arg(uimm(op));
    case 25: return QStringLiteral("oris %1,%2,%3").arg(rA, rD).arg(uimm(op));
    case 26: return QStringLiteral("xori %1,%2,%3").arg(rA, rD).arg(uimm(op));
    case 27: return QStringLiteral("xoris %1,%2,%3").arg(rA, rD).arg(uimm(op));
    case 28: return QStringLiteral("andi. %1,%2,%3").arg(rA, rD).arg(uimm(op));
    case 29: return QStringLiteral("andis. %1,%2,%3").arg(rA, rD).arg(uimm(op));
    case 32: return dform(QStringLiteral("lwz")); case 33: return dform(QStringLiteral("lwzu"));
    case 34: return dform(QStringLiteral("lbz")); case 35: return dform(QStringLiteral("lbzu"));
    case 36: return dform(QStringLiteral("stw")); case 37: return dform(QStringLiteral("stwu"));
    case 38: return dform(QStringLiteral("stb")); case 39: return dform(QStringLiteral("stbu"));
    case 40: return dform(QStringLiteral("lhz")); case 41: return dform(QStringLiteral("lhzu"));
    case 42: return dform(QStringLiteral("lha")); case 43: return dform(QStringLiteral("lhau"));
    case 44: return dform(QStringLiteral("sth")); case 45: return dform(QStringLiteral("sthu"));
    case 46: return dform(QStringLiteral("lmw")); case 47: return dform(QStringLiteral("stmw"));
    case 48: return dform(QStringLiteral("lfs")); case 50: return dform(QStringLiteral("lfd"));
    case 52: return dform(QStringLiteral("stfs")); case 54: return dform(QStringLiteral("stfd"));
    case 19:
        if (xo(op) == 16) return QStringLiteral("bclr %1,%2").arg((op >> 21) & 31U).arg((op >> 16) & 31U);
        if (xo(op) == 50) return QStringLiteral("rfi");
        if (xo(op) == 528) return QStringLiteral("bcctr %1,%2").arg((op >> 21) & 31U).arg((op >> 16) & 31U);
        break;
    case 31: {
        QString mnemonic;
        switch (xo(op) & 0x1ffU) {
        case 8: mnemonic = QStringLiteral("subfc"); break; case 10: mnemonic = QStringLiteral("addc"); break;
        case 23: mnemonic = QStringLiteral("lwzx"); break; case 24: mnemonic = QStringLiteral("slw"); break;
        case 26: mnemonic = QStringLiteral("cntlzw"); break; case 28: mnemonic = QStringLiteral("and"); break;
        case 40: mnemonic = QStringLiteral("subf"); break; case 104: mnemonic = QStringLiteral("neg"); break;
        case 124: mnemonic = QStringLiteral("nor"); break; case 138: mnemonic = QStringLiteral("adde"); break;
        case 150: mnemonic = QStringLiteral("stwcx."); break; case 235: mnemonic = QStringLiteral("mullw"); break;
        case 266: mnemonic = QStringLiteral("add"); break; case 316: mnemonic = QStringLiteral("xor"); break;
        case 339: return QStringLiteral("mfspr %1,%2").arg(rD).arg(decodedSpr(op));
        case 444: mnemonic = QStringLiteral("or"); break; case 459: mnemonic = QStringLiteral("divwu"); break;
        case 467: return QStringLiteral("mtspr %1,%2").arg(decodedSpr(op)).arg(rD);
        case 491: mnemonic = QStringLiteral("divw"); break;
        }
        if (!mnemonic.isEmpty()) return QStringLiteral("%1%2 %3,%4,%5").arg(mnemonic, op & 1U ? QStringLiteral(".") : QString(), rD, rA, rB);
        break;
    }
    default: break;
    }
    return QStringLiteral(".long %1").arg(hex32(op));
}

QString PowerPc601Core::formatTraceEvent(const TraceEvent& event)
{
    const auto kind = event.kind == TraceEvent::Kind::Instruction ? QStringLiteral("insn")
        : event.kind == TraceEvent::Kind::Exception ? QStringLiteral("exception")
        : event.kind == TraceEvent::Kind::Interrupt ? QStringLiteral("interrupt")
                                                   : QStringLiteral("translation");
    const auto access = event.access == AccessType::Instruction ? QStringLiteral("execute")
        : event.access == AccessType::Read ? QStringLiteral("read") : QStringLiteral("write");
    const auto path = event.translation == TranslationPath::Real ? QStringLiteral("real")
        : event.translation == TranslationPath::Bat ? QStringLiteral("bat")
        : event.translation == TranslationPath::Page ? QStringLiteral("page") : QStringLiteral("fault");
    return QStringLiteral("v=1 arch=ppc601 kind=%1 cycle=%2 pc=%3 opcode=%4 ea=%5 pa=%6 detail=%7 msr=%8 access=%9 path=%10 exception=%11 success=%12")
        .arg(kind).arg(event.cycle).arg(hex32(event.pc), hex32(event.opcode),
            hex32(event.effectiveAddress), hex32(event.physicalAddress), hex32(event.detail),
            hex32(event.savedMsr), access, path)
        .arg(hex32(static_cast<std::uint32_t>(event.exception)))
        .arg(event.success ? QStringLiteral("1") : QStringLiteral("0"));
}

} // namespace cutemac::cpu::ppc
