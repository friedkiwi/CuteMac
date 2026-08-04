#include "cutemac/cpu/m68k/M68kCpuCore.h"

#include <algorithm>
#include <array>

extern "C" {
#include "engine/m68k.h"
}

namespace cutemac::cpu::m68k {

namespace {

M68kCpuCore* activeCpu = nullptr;
M68kBus* activeBus = nullptr;
int instructionHookBudget = -1;
unsigned int activeFunctionCode = 0;

unsigned int musashiCpuType(M68kCpuCore::Model model)
{
    switch (model) {
    case M68kCpuCore::Model::M68000:
        return M68K_CPU_TYPE_68000;
    case M68kCpuCore::Model::M68010:
        return M68K_CPU_TYPE_68010;
    case M68kCpuCore::Model::M68Ec020:
        return M68K_CPU_TYPE_68EC020;
    case M68kCpuCore::Model::M68020:
        return M68K_CPU_TYPE_68020;
    case M68kCpuCore::Model::M68Ec030:
        return M68K_CPU_TYPE_68EC030;
    case M68kCpuCore::Model::M68030:
        return M68K_CPU_TYPE_68030;
    case M68kCpuCore::Model::M68Ec040:
        return M68K_CPU_TYPE_68EC040;
    case M68kCpuCore::Model::M68Lc040:
        return M68K_CPU_TYPE_68LC040;
    case M68kCpuCore::Model::M68040:
        return M68K_CPU_TYPE_68040;
    }

    return M68K_CPU_TYPE_68000;
}

void instructionHook(unsigned int)
{
    if (instructionHookBudget < 0) {
        return;
    }
    if (instructionHookBudget == 0) {
        m68k_end_timeslice();
        return;
    }
    --instructionHookBudget;
}

void functionCodeChanged(unsigned int functionCode)
{
    activeFunctionCode = functionCode;
}

bool isProgramFunctionCode()
{
    return (activeFunctionCode & 3U) == 2U;
}

} // namespace

M68kCpuCore::M68kCpuCore()
{
    activeCpu = this;
    m68k_init();
    m68k_set_instr_hook_callback(instructionHook);
    m68k_set_fc_callback(functionCodeChanged);
    setModel(m_model);
}

M68kCpuCore::~M68kCpuCore()
{
    if (activeCpu == this) {
        activeCpu = nullptr;
        activeBus = nullptr;
    }
}

QString M68kCpuCore::id() const
{
    return QStringLiteral("cpu.m68k");
}

void M68kCpuCore::reset()
{
    activeCpu = this;
    m68k_pulse_reset();
}

void M68kCpuCore::setModel(Model model)
{
    m_model = model;
    activeCpu = this;
    m68k_set_cpu_type(musashiCpuType(model));
}

void M68kCpuCore::setExternal68851(bool enabled)
{
    activeCpu = this;
    m68k_set_external_pmmu(enabled ? 1U : 0U);
}

void M68kCpuCore::setBus(M68kBus* bus)
{
    m_bus = bus;
    activeCpu = this;
    activeBus = bus;
}

void M68kCpuCore::setIrqLevel(unsigned int level)
{
    activeCpu = this;
    m68k_set_irq(std::min(level, 7U));
}

int M68kCpuCore::execute(int cycles)
{
    activeCpu = this;
    return m68k_execute(cycles);
}

int M68kCpuCore::stepInstruction()
{
    activeCpu = this;
    instructionHookBudget = 0;
    const auto cycles = m68k_execute(512);
    instructionHookBudget = -1;
    return cycles;
}

std::uint32_t M68kCpuCore::programCounter() const
{
    return m68k_get_reg(nullptr, M68K_REG_PC);
}

M68kCpuCore::RegisterSnapshot M68kCpuCore::registers() const
{
    RegisterSnapshot snapshot;
    snapshot.d = {
        m68k_get_reg(nullptr, M68K_REG_D0),
        m68k_get_reg(nullptr, M68K_REG_D1),
        m68k_get_reg(nullptr, M68K_REG_D2),
        m68k_get_reg(nullptr, M68K_REG_D3),
        m68k_get_reg(nullptr, M68K_REG_D4),
        m68k_get_reg(nullptr, M68K_REG_D5),
        m68k_get_reg(nullptr, M68K_REG_D6),
        m68k_get_reg(nullptr, M68K_REG_D7),
    };
    snapshot.a = {
        m68k_get_reg(nullptr, M68K_REG_A0),
        m68k_get_reg(nullptr, M68K_REG_A1),
        m68k_get_reg(nullptr, M68K_REG_A2),
        m68k_get_reg(nullptr, M68K_REG_A3),
        m68k_get_reg(nullptr, M68K_REG_A4),
        m68k_get_reg(nullptr, M68K_REG_A5),
        m68k_get_reg(nullptr, M68K_REG_A6),
        m68k_get_reg(nullptr, M68K_REG_A7),
    };
    snapshot.pc = m68k_get_reg(nullptr, M68K_REG_PC);
    snapshot.sr = static_cast<std::uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
    snapshot.usp = m68k_get_reg(nullptr, M68K_REG_USP);
    snapshot.isp = m68k_get_reg(nullptr, M68K_REG_ISP);
    snapshot.msp = m68k_get_reg(nullptr, M68K_REG_MSP);
    snapshot.vbr = m68k_get_reg(nullptr, M68K_REG_VBR);
    snapshot.pmmuEnabled = m68k_get_pmmu_enabled() != 0;
    snapshot.pmmuTc = m68k_get_pmmu_tc();
    snapshot.pmmuTt0 = m68k_get_pmmu_tt0();
    snapshot.pmmuTt1 = m68k_get_pmmu_tt1();
    snapshot.pmmuCrpLimit = m68k_get_pmmu_crp_limit();
    snapshot.pmmuCrpAddress = m68k_get_pmmu_crp_address();
    snapshot.pmmuSrpLimit = m68k_get_pmmu_srp_limit();
    snapshot.pmmuSrpAddress = m68k_get_pmmu_srp_address();
    snapshot.pmmuKind = m68k_get_pmmu_kind();
    snapshot.pmmuMmusr = static_cast<std::uint16_t>(m68k_get_pmmu_mmusr());
    snapshot.pmmuFaultAddress = m68k_get_pmmu_fault_address();
    snapshot.pmmuAtcHits = m68k_get_pmmu_atc_hits();
    snapshot.pmmuAtcMisses = m68k_get_pmmu_atc_misses();
    snapshot.physicalPc = m68k_translate_address(snapshot.pc);
    return snapshot;
}

QString M68kCpuCore::disassemble(std::uint32_t address) const
{
    activeCpu = const_cast<M68kCpuCore*>(this);
    std::array<char, 128> buffer {};
    m68k_disassemble(buffer.data(), address, musashiCpuType(m_model));
    return QString::fromLatin1(buffer.data());
}

int M68kCpuCore::disassembleBytes(std::uint32_t address) const
{
    activeCpu = const_cast<M68kCpuCore*>(this);
    std::array<char, 128> buffer {};
    return static_cast<int>(m68k_disassemble(buffer.data(), address, musashiCpuType(m_model)));
}

void M68kCpuCore::setProgramCounter(std::uint32_t address)
{
    activeCpu = this;
    m68k_set_reg(M68K_REG_PC, address);
}

} // namespace cutemac::cpu::m68k

extern "C" unsigned int m68k_read_memory_8(unsigned int address)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus == nullptr) return 0xffU;
    if (cutemac::cpu::m68k::isProgramFunctionCode()) {
        const auto programResult = bus->readProgram8(address);
        if (programResult.busError) m68k_report_physical_bus_error(address, 0, 1);
        return programResult.value;
    }
    const auto result = bus->readPhysical8(address);
    if (result.busError) m68k_report_physical_bus_error(address, 0, 1);
    return result.value;
}

extern "C" unsigned int m68k_read_memory_16(unsigned int address)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus == nullptr) return 0xffffU;
    if (cutemac::cpu::m68k::isProgramFunctionCode()) {
        const auto programResult = bus->readProgram16(address);
        if (programResult.busError) m68k_report_physical_bus_error(address, 0, 2);
        return programResult.value;
    }
    const auto result = bus->readPhysical16(address);
    if (result.busError) m68k_report_physical_bus_error(address, 0, 2);
    return result.value;
}

extern "C" unsigned int m68k_read_memory_32(unsigned int address)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus == nullptr) return 0xffffffffU;
    if (cutemac::cpu::m68k::isProgramFunctionCode()) {
        const auto programResult = bus->readProgram32(address);
        if (programResult.busError) m68k_report_physical_bus_error(address, 0, 4);
        return programResult.value;
    }
    const auto result = bus->readPhysical32(address);
    if (result.busError) m68k_report_physical_bus_error(address, 0, 4);
    return result.value;
}

extern "C" void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus != nullptr) {
        if (!bus->writePhysical8(address, static_cast<std::uint8_t>(value)))
            m68k_report_physical_bus_error(address, 1, 1);
    }
}

extern "C" void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus != nullptr) {
        if (!bus->writePhysical16(address, static_cast<std::uint16_t>(value)))
            m68k_report_physical_bus_error(address, 1, 2);
    }
}

extern "C" void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus != nullptr) {
        if (!bus->writePhysical32(address, value))
            m68k_report_physical_bus_error(address, 1, 4);
    }
}

extern "C" void m68k_write_memory_32_pd(unsigned int address, unsigned int value)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus != nullptr) {
        bus->write16(address + 2, static_cast<std::uint16_t>(value >> 16));
        bus->write16(address, static_cast<std::uint16_t>(value));
    }
}

extern "C" unsigned int m68k_read_immediate_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

extern "C" unsigned int m68k_read_immediate_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

extern "C" unsigned int m68k_read_pcrelative_8(unsigned int address)
{
    return m68k_read_memory_8(address);
}

extern "C" unsigned int m68k_read_pcrelative_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

extern "C" unsigned int m68k_read_pcrelative_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

extern "C" unsigned int m68k_read_disassembler_8(unsigned int address)
{
    return m68k_read_memory_8(address);
}

extern "C" unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

extern "C" unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}
