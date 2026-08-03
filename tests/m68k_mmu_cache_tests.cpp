#include <array>
#include <cstdint>
#include <iostream>

#include "cutemac/cpu/m68k/M68kBus.h"
#include "cutemac/cpu/m68k/M68kCpuCore.h"

extern "C" {
#include "m68kcpu.h"
}

namespace {

class TestBus final : public cutemac::cpu::m68k::M68kBus {
public:
    std::uint8_t read8(std::uint32_t address) override
    {
        ++reads;
        return memory[address & (memory.size() - 1)];
    }

    std::uint16_t read16(std::uint32_t address) override
    {
        return static_cast<std::uint16_t>((read8(address) << 8) | read8(address + 1));
    }

    std::uint32_t read32(std::uint32_t address) override
    {
        return (static_cast<std::uint32_t>(read16(address)) << 16) | read16(address + 2);
    }

    void write8(std::uint32_t address, std::uint8_t value) override
    {
        memory[address & (memory.size() - 1)] = value;
    }

    void write16(std::uint32_t address, std::uint16_t value) override
    {
        write8(address, static_cast<std::uint8_t>(value >> 8));
        write8(address + 1, static_cast<std::uint8_t>(value));
    }

    void write32(std::uint32_t address, std::uint32_t value) override
    {
        write16(address, static_cast<std::uint16_t>(value >> 16));
        write16(address + 2, static_cast<std::uint16_t>(value));
    }

    std::array<std::uint8_t, 0x4000> memory {};
    unsigned int reads = 0;
};

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    TestBus bus;
    cutemac::cpu::m68k::M68kCpuCore core;
    core.setModel(cutemac::cpu::m68k::M68kCpuCore::Model::M68030);
    core.setBus(&bus);

    // Ignore no address bits and use four A-table bits. Early termination at
    // that table produces 256 MiB mappings, making cache behavior explicit.
    m68ki_cpu.mmu_tc = 0x82004000U;
    m68ki_cpu.mmu_crp_limit = 2;
    m68ki_cpu.mmu_crp_aptr = 0x1000;
    m68ki_cpu.mmu_srp_limit = 2;
    m68ki_cpu.mmu_srp_aptr = 0x2000;
    m68ki_cpu.pmmu_enabled = 1;
    bus.write32(0x1008, 0x10000001U);
    bus.write32(0x2008, 0x20000001U);
    m68k_pmmu_atc_flush();
    m68k_pmmu_atc_reset_statistics();
    bus.reads = 0;

    m68ki_cpu.s_flag = 0;
    const auto userFirst = pmmu_translate_addr(0x21234567U);
    const auto descriptorReads = bus.reads;
    const auto userSecond = pmmu_translate_addr(0x21234678U);
    bool ok = expect(userFirst == 0x11234567U && userSecond == 0x11234678U,
        "ATC must preserve offsets within the translated page");
    ok &= expect(descriptorReads != 0 && bus.reads == descriptorReads
            && m68k_get_pmmu_atc_hits() == 1 && m68k_get_pmmu_atc_misses() == 1,
        "a repeated user-page translation must hit without rereading descriptors");

    m68ki_cpu.s_flag = SFLAG_SET;
    const auto supervisor = pmmu_translate_addr(0x21234567U);
    ok &= expect(supervisor == 0x21234567U && m68k_get_pmmu_atc_misses() == 2,
        "supervisor and user translations must occupy distinct ATC entries");

    m68ki_cpu.s_flag = 0;
    bus.write32(0x1008, 0x30000001U);
    ok &= expect(pmmu_translate_addr(0x21234567U) == userFirst,
        "descriptor changes must remain cached until an architectural flush");
    m68k_pmmu_atc_flush();
    ok &= expect(pmmu_translate_addr(0x21234567U) == 0x31234567U,
        "an architectural flush must expose changed page-table descriptors");

    bus.write32(0x1008, 0x40000001U);
    bus.write16(0x0100, 0xf000U);
    bus.write16(0x0102, 0x2400U); // PFLUSH, flush all function codes.
    m68ki_cpu.pmmu_enabled = 0;
    core.setProgramCounter(0x0100);
    (void)core.stepInstruction();
    m68ki_cpu.pmmu_enabled = 1;
    ok &= expect(pmmu_translate_addr(0x21234567U) == 0x41234567U,
        "executing a 68030 PFLUSH instruction must invalidate cached translations");

    // System 7.5.3 on the IIcx programs TC=0x80f84500: IS=8, A=4,
    // B=5, C=0. That makes each terminal B-table entry a 32 KiB page.
    // Exercise the exact layout so the cache cannot accidentally assume the
    // much larger early-termination page used by the checks above.
    constexpr std::uint32_t s7Address = 0x40826cc0U;
    m68ki_cpu.mmu_tc = 0x80f84500U;
    m68ki_cpu.mmu_crp_limit = 2;
    m68ki_cpu.mmu_crp_aptr = 0x1000;
    m68ki_cpu.s_flag = 0;
    const auto aIndex = (s7Address << 8) >> 28;
    const auto bIndex = (s7Address << 12) >> 27;
    bus.write32(0x1000 + aIndex * 4, 0x00003002U);
    bus.write32(0x3000 + bIndex * 4, 0x50000001U);
    bus.write32(0x3000 + (bIndex + 1) * 4, 0x60000001U);
    m68k_pmmu_atc_flush();
    m68k_pmmu_atc_reset_statistics();
    const auto s7First = pmmu_translate_addr(s7Address);
    const auto s7SamePage = pmmu_translate_addr(s7Address + 0x123U);
    const auto s7NextPage = pmmu_translate_addr((s7Address & ~0x7fffU) + 0x8000U);
    ok &= expect(s7First == 0x50006cc0U && s7SamePage == 0x50006de3U,
        "the System 7 TC layout must cache translations at 32 KiB granularity");
    ok &= expect(s7NextPage == 0x60000000U
            && m68k_get_pmmu_atc_hits() == 1 && m68k_get_pmmu_atc_misses() == 2,
        "adjacent System 7 pages must not alias in the ATC");

    // A/UX uses PTEST to obtain protection and modified state without making
    // normal-translation U/M side effects.
    const auto s7DescriptorAddress = 0x3000U + bIndex * 4U;
    bus.write32(s7DescriptorAddress, 0x50000015U); // page, WP and M
    bus.write16(0x0100, 0xf010U); // PTESTR #5,(A0),7
    bus.write16(0x0102, 0x9e15U);
    m68ki_cpu.dar[8] = s7Address;
    m68ki_cpu.pmmu_enabled = 0; // keep the instruction fixture physically addressed
    core.setProgramCounter(0x0100);
    (void)core.stepInstruction();
    ok &= expect((m68ki_cpu.mmu_sr & 0x0e00U) == 0x0a00U
            && (m68ki_cpu.mmu_sr & 7U) == 2U,
        "68030 PTEST must report write protection, modified state, and the reached level");
    ok &= expect(bus.read32(s7DescriptorAddress) == 0x50000015U,
        "PTEST must not update descriptor used/modified bits");

    // MMUSR is a word-sized PMOVE register. A long transfer corrupts the word
    // following the destination and was present in the old implementation.
    bus.write16(0x0100, 0xf010U); // PMOVE MMUSR,(A0)
    bus.write16(0x0102, 0x6200U);
    bus.write32(0x0200, 0xdeadbeefU);
    m68ki_cpu.mmu_sr = 0x0a02U;
    m68ki_cpu.dar[8] = 0x0200;
    core.setProgramCounter(0x0100);
    (void)core.stepInstruction();
    ok &= expect(bus.read32(0x0200) == 0x0a02beefU,
        "PMOVE from MMUSR must transfer exactly one word");

    // A normal write sets both U and M in the terminating page descriptor.
    bus.write32(s7DescriptorAddress, 0x50000001U);
    m68ki_cpu.mmu_tc = 0x80f84500U;
    m68ki_cpu.pmmu_enabled = 1;
    m68k_pmmu_atc_flush();
    (void)pmmu_translate_addr_fc_size(s7Address, 1, 0, 4);
    ok &= expect((bus.read32(s7DescriptorAddress) & 0x18U) == 0x18U,
        "a writable PMMU store must set descriptor used and modified bits");

    core.reset();
    ok &= expect(m68k_get_pmmu_atc_hits() == 0 && m68k_get_pmmu_atc_misses() == 0,
        "CPU reset must flush the ATC and reset its statistics");

    // A/UX saves a freshly reset 68882 frame through FSAVE (A7). This valid
    // no-update addressing form must write the null frame without changing A7.
    bus.write16(0x0100, 0xf317U); // FSAVE (A7)
    bus.write32(0x0200, 0xffffffffU);
    m68ki_cpu.dar[15] = 0x0200;
    m68ki_cpu.fpu_just_reset = 1;
    core.setProgramCounter(0x0100);
    (void)core.stepInstruction();
    ok &= expect(bus.read32(0x0200) == 0 && m68ki_cpu.dar[15] == 0x0200,
        "FSAVE (An) must write a null frame without updating the address register");

    // The 68030 PMOVE encoding group zero addresses TT0/TT1. Older CuteMac
    // code incorrectly treated these as the 64-bit SRP/CRP registers.
    bus.write16(0x0100, 0xf010U); // PMOVE (A0),TT0
    bus.write16(0x0102, 0x0800U);
    bus.write32(0x0200, 0x50008107U); // $50xxxxxx, all FCs/RW, enabled
    m68ki_cpu.dar[8] = 0x0200;
    core.setProgramCounter(0x0100);
    (void)core.stepInstruction();
    ok &= expect(m68k_get_pmmu_tt0() == 0x50008107U,
        "68030 PMOVE must load TT0 as a 32-bit register");

    m68ki_cpu.mmu_tc = 0x82004000U;
    m68ki_cpu.pmmu_enabled = 1;
    m68k_pmmu_atc_flush();
    bus.reads = 0;
    ok &= expect(pmmu_translate_addr_fc(0x50f04000U, 5, 1) == 0x50f04000U
            && bus.reads == 0,
        "a matching 68030 TT must bypass the ATC and page-table walk");
    bus.write32(0x1010, 0x30000001U);
    ok &= expect(pmmu_translate_addr_fc(0x40f04000U, 1, 1) != 0x40f04000U
            && bus.reads != 0,
        "a nonmatching address must continue through normal translation");

    // FC and R/W qualification are architectural parts of a 68030 TT.
    m68ki_cpu.mmu_tt0 = 0x50008250U; // $50xxxxxx, supervisor-data reads only
    ok &= expect(pmmu_translate_addr_fc(0x50f04000U, 5, 1) == 0x50f04000U,
        "TT function-code and read qualification must accept a matching access");
    bus.reads = 0;
    bus.write32(0x1014, 0x30000001U);
    (void)pmmu_translate_addr_fc(0x50f04000U, 1, 1);
    ok &= expect(bus.reads != 0,
        "TT function-code qualification must reject a nonmatching access");

    // Debugger translation performs a table walk for visibility but must not
    // fill the ATC, update descriptor U/M bits, or replace guest fault state.
    m68ki_cpu.mmu_tt0 = 0;
    m68ki_cpu.mmu_tc = 0x80f84500U;
    m68ki_cpu.mmu_crp_limit = 2;
    m68ki_cpu.mmu_crp_aptr = 0x1000;
    bus.write32(s7DescriptorAddress, 0x50000001U);
    m68k_pmmu_atc_flush();
    m68k_pmmu_atc_reset_statistics();
    m68ki_cpu.mmu_tmp_sr = 0x1234;
    m68ki_cpu.mmu_fault_address = 0xdeadbeefU;
    const auto debugPhysical = m68k_translate_address(s7Address);
    ok &= expect(debugPhysical == 0x50006cc0U
            && bus.read32(s7DescriptorAddress) == 0x50000001U,
        "debug translation must resolve an address without descriptor side effects");
    ok &= expect(m68k_get_pmmu_atc_hits() == 0 && m68k_get_pmmu_atc_misses() == 0
            && m68ki_cpu.mmu_tmp_sr == 0x1234
            && m68ki_cpu.mmu_fault_address == 0xdeadbeefU,
        "debug translation must preserve ATC statistics and pending PMMU state");

    // CPU selection gates integrated PMMU registers. A plain 68020 has no
    // integrated PMMU; a future external 68851 remains a separate capability.
    core.setModel(cutemac::cpu::m68k::M68kCpuCore::Model::M68020);
    m68ki_cpu.mmu_tt0 = 0x12345678U;
    m68ki_cpu.dar[8] = 0x0200;
    core.setProgramCounter(0x0100);
    (void)core.stepInstruction();
    ok &= expect(m68ki_cpu.mmu_tt0 == 0x12345678U,
        "68020 selection must not expose integrated 68030 TT registers");
    core.setExternal68851(true);
    ok &= expect(m68ki_cpu.has_pmmu && m68ki_cpu.mmu_kind == M68K_MMU_KIND_68851,
        "a full 68020 must permit an explicitly attached external 68851");
    core.setExternal68851(false);
    ok &= expect(!m68ki_cpu.has_pmmu && m68ki_cpu.mmu_kind == M68K_MMU_KIND_NONE,
        "detaching the 68851 must restore plain 68020 behavior");
    return ok ? 0 : 1;
}
