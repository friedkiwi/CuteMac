#include "cutemac/cpu/m68k/M68kCpuCore.h"

#include <algorithm>

extern "C" {
#include "engine/m68k.h"
}

namespace cutemac::cpu::m68k {

namespace {

M68kCpuCore* activeCpu = nullptr;
M68kBus* activeBus = nullptr;

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

} // namespace

M68kCpuCore::M68kCpuCore()
{
    activeCpu = this;
    m68k_init();
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

std::uint32_t M68kCpuCore::programCounter() const
{
    return m68k_get_reg(nullptr, M68K_REG_PC);
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
    return bus == nullptr ? 0xffU : bus->read8(address);
}

extern "C" unsigned int m68k_read_memory_16(unsigned int address)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    return bus == nullptr ? 0xffffU : bus->read16(address);
}

extern "C" unsigned int m68k_read_memory_32(unsigned int address)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    return bus == nullptr ? 0xffffffffU : bus->read32(address);
}

extern "C" void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus != nullptr) {
        bus->write8(address, static_cast<std::uint8_t>(value));
    }
}

extern "C" void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus != nullptr) {
        bus->write16(address, static_cast<std::uint16_t>(value));
    }
}

extern "C" void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    auto* bus = cutemac::cpu::m68k::activeBus;
    if (bus != nullptr) {
        bus->write32(address, value);
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
