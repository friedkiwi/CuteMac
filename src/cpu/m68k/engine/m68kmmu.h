#include <string.h>

/*
 * MC68851/MC68030 PMMU support.
 *
 * Derived from the BSD-3-Clause Musashi PMMU implementation maintained by
 * R. Belmont, Hans Ostermeyer, Sven Schnelle, and the MAME team
 * and adapted to CuteMac's directly maintained C core.  The public memory
 * path carries an architectural access result: translation status, full
 * function code, permissions and restartable fault information.  The 68040
 * uses the same boundary but deliberately has a separate MMU kind because its
 * registers, descriptors, ATC and access-error frame are not 68030-compatible.
 */

#define M68K_MMU_SR_BUS_ERROR       0x8000
#define M68K_MMU_SR_SUPERVISOR_ONLY 0x2000
#define M68K_MMU_SR_WRITE_PROTECT   0x0800
#define M68K_MMU_SR_INVALID         0x0400
#define M68K_MMU_SR_MODIFIED        0x0200
#define M68K_MMU_SR_TRANSPARENT     0x0040

#define M68K_MMU_DF_DT              0x00000003U
#define M68K_MMU_DF_DT_INVALID      0x00000000U
#define M68K_MMU_DF_DT_PAGE         0x00000001U
#define M68K_MMU_DF_DT_TABLE_4BYTE  0x00000002U
#define M68K_MMU_DF_DT_TABLE_8BYTE  0x00000003U
#define M68K_MMU_DF_WP              0x00000004U
#define M68K_MMU_DF_USED            0x00000008U
#define M68K_MMU_DF_MODIFIED        0x00000010U
#define M68K_MMU_DF_SUPERVISOR      0x00000100U
#define M68K_MMU_DF_ADDR_MASK       0xfffffff0U
#define M68K_MMU_DF_IND_ADDR_MASK   0xfffffffcU

#define M68K_MMU_TC_SRE             0x02000000U
#define M68K_MMU_TC_FCL             0x01000000U

enum pmmu_intent
{
	PMMU_INTENT_NORMAL = 0,
	PMMU_INTENT_PLOAD,
	PMMU_INTENT_PTEST,
	PMMU_INTENT_DEBUG,
	PMMU_INTENT_PROBE
};

typedef struct
{
	uint physical_address;
	uint descriptor_address;
	uint16 status;
	uint8 level;
	uint8 fault;
	uint8 transparent;
} pmmu_translation_result;

static uint pmmu_page_shift(void)
{
	const uint ps = (m68ki_cpu.mmu_tc >> 20) & 0xf;
	if (ps >= 8) return ps;
	/* Preserve inspection of a partially programmed TC without claiming the
	 * configuration is architecturally valid. PMOVE performs strict validation. */
	{
		const uint used = ((m68ki_cpu.mmu_tc >> 16) & 0xf)
			+ ((m68ki_cpu.mmu_tc >> 12) & 0xf)
			+ ((m68ki_cpu.mmu_tc >> 8) & 0xf)
			+ ((m68ki_cpu.mmu_tc >> 4) & 0xf)
			+ (m68ki_cpu.mmu_tc & 0xf);
		return used < 32 ? 32 - used : 8;
	}
}

void pmmu_atc_flush(void)
{
	uint entry;
	for (entry = 0; entry < M68K_MMU_ATC_ENTRIES; ++entry)
		m68ki_cpu.mmu_atc[entry].valid = 0;
}

static uint pmmu_fc_from_modes(uint modes)
{
	if ((modes & 0x1f) == 0)
		return REG_DFC & 7;
	if ((modes & 0x1f) == 1)
		return REG_SFC & 7;
	if (((modes >> 3) & 3) == 1)
		return REG_D[modes & 7] & 7;
	if (((modes >> 3) & 3) == 2)
		return modes & 7;
	return 0;
}

static int pmmu_match_tt(uint addr, uint fc, uint tt, uint rw, uint16 *status)
{
	uint address_base, address_mask, fc_mask, fc_bits, rw_mask, rw_bit;
	if (!(tt & 0x00008000U))
		return 0;
	address_base = tt & 0xff000000U;
	address_mask = ((tt << 8) & 0xff000000U) ^ 0xff000000U;
	fc_mask = (~tt) & 7U;
	fc_bits = (tt >> 4) & 7U;
	rw_mask = (~tt) & 0x100U;
	rw_bit = tt & 0x200U;
	if ((addr & address_mask) != (address_base & address_mask))
		return 0;
	if ((fc & fc_mask) != (fc_bits & fc_mask))
		return 0;
	if ((((rw ? 1U : 0U) << 8) & rw_mask) != (rw_bit & rw_mask))
		return 0;
	*status |= M68K_MMU_SR_TRANSPARENT;
	return 1;
}

static int pmmu_decode_ea(uint ea, uint *address)
{
	const uint mode = (ea >> 3) & 7;
	const uint reg = ea & 7;
	switch (mode)
	{
		case 2: *address = REG_A[reg]; return 1;
		case 5: *address = EA_AY_DI_32(); return 1;
		case 6: *address = EA_AY_IX_32(); return 1;
		case 7:
			switch (reg)
			{
				case 0: *address = EA_AW_32(); return 1;
				case 1: *address = EA_AL_32(); return 1;
			}
	}
	pmmu_illegal(0, "PMMU addressing mode %d reg %d has no effective address", (REG_IR >> 3) & 7, REG_IR & 7);
	return 0;
}

static void pmmu_status_from_descriptor(uint type, uint first, int is_long,
	uint fc, uint16 *status)
{
	if (type == M68K_MMU_DF_DT_PAGE && (first & M68K_MMU_DF_MODIFIED))
		*status |= M68K_MMU_SR_MODIFIED;
	if (type != M68K_MMU_DF_DT_INVALID && (first & M68K_MMU_DF_WP))
		*status |= M68K_MMU_SR_WRITE_PROTECT;
	if (is_long && !(fc & 4) && (first & M68K_MMU_DF_SUPERVISOR))
		*status |= M68K_MMU_SR_SUPERVISOR_ONLY;
}

static void pmmu_update_descriptor(uint address, uint type, uint first,
	int is_long, uint rw)
{
	uint updated = first;
	if (type == M68K_MMU_DF_DT_INVALID)
		return;
	updated |= M68K_MMU_DF_USED;
	if (type == M68K_MMU_DF_DT_PAGE && !rw && !(first & M68K_MMU_DF_WP))
		updated |= M68K_MMU_DF_MODIFIED;
	if (updated != first)
		m68k_write_memory_32(address, updated);
	(void)is_long;
}

static int pmmu_atc_lookup(uint address, uint fc, uint rw, int ptest,
	pmmu_translation_result *result)
{
	uint i;
	const uint shift = pmmu_page_shift();
	const uint mask = 0xffffffffU >> (32 - shift);
	for (i = 0; i < M68K_MMU_ATC_ENTRIES; ++i)
	{
		m68ki_mmu_atc_entry *entry = &m68ki_cpu.mmu_atc[i];
		if (!entry->valid || entry->page_shift != shift
			|| entry->function_code != (fc & 7)
			|| entry->logical_page != (address & ~mask))
			continue;
		if (!ptest && !rw && !entry->modified && !entry->write_protect)
		{
			entry->valid = 0;
			continue;
		}
		result->physical_address = entry->physical_page | (address & mask);
		if (entry->modified) result->status |= M68K_MMU_SR_MODIFIED;
		if (entry->write_protect) result->status |= M68K_MMU_SR_WRITE_PROTECT;
		if (entry->fault) result->status |= M68K_MMU_SR_INVALID;
		result->fault = entry->fault || (!rw && entry->write_protect);
		++m68ki_cpu.mmu_atc_hits;
		return 1;
	}
	++m68ki_cpu.mmu_atc_misses;
	if (ptest)
	{
		result->status = M68K_MMU_SR_INVALID;
		result->fault = 1;
	}
	return 0;
}

static void pmmu_atc_add(uint logical, uint physical, uint fc,
	const pmmu_translation_result *result)
{
	static uint replacement;
	uint i, selected = M68K_MMU_ATC_ENTRIES;
	const uint shift = pmmu_page_shift();
	const uint mask = 0xffffffffU >> (32 - shift);
	for (i = 0; i < M68K_MMU_ATC_ENTRIES; ++i)
	{
		if (m68ki_cpu.mmu_atc[i].valid
			&& m68ki_cpu.mmu_atc[i].function_code == (fc & 7)
			&& m68ki_cpu.mmu_atc[i].logical_page == (logical & ~mask))
		{
			selected = i;
			break;
		}
		if (!m68ki_cpu.mmu_atc[i].valid && selected == M68K_MMU_ATC_ENTRIES)
			selected = i;
	}
	if (selected == M68K_MMU_ATC_ENTRIES)
	{
		selected = replacement++ % M68K_MMU_ATC_ENTRIES;
	}
	m68ki_cpu.mmu_atc[selected].logical_page = logical & ~mask;
	m68ki_cpu.mmu_atc[selected].physical_page = physical & ~mask;
	m68ki_cpu.mmu_atc[selected].page_shift = (uint8)shift;
	m68ki_cpu.mmu_atc[selected].function_code = (uint8)(fc & 7);
	m68ki_cpu.mmu_atc[selected].write_protect =
		(result->status & M68K_MMU_SR_WRITE_PROTECT) != 0;
	m68ki_cpu.mmu_atc[selected].modified =
		(result->status & M68K_MMU_SR_MODIFIED) != 0;
	m68ki_cpu.mmu_atc[selected].fault = result->fault;
	m68ki_cpu.mmu_atc[selected].valid = 1;
}

static void pmmu_walk_tables(uint logical, uint fc, uint rw, uint limit,
	int side_effects, pmmu_translation_result *result)
{
	uint bits = m68ki_cpu.mmu_tc & 0xffffU;
	uint type, table, address_work, pageshift, bitpos, level = 0;
	uint resolved = 0;
	int shift;

	/* Fields after the first zero TI field are ignored. */
	for (shift = 12; shift >= 0; shift -= 4)
	{
		if (!((bits >> shift) & 0xf))
		{
			bits &= ~((1U << (shift + 4)) - 1U);
			break;
		}
	}

	if ((m68ki_cpu.mmu_tc & M68K_MMU_TC_SRE) && (fc & 4))
	{
		type = m68ki_cpu.mmu_srp_limit & M68K_MMU_DF_DT;
		table = m68ki_cpu.mmu_srp_aptr & M68K_MMU_DF_ADDR_MASK;
	}
	else
	{
		type = m68ki_cpu.mmu_crp_limit & M68K_MMU_DF_DT;
		table = m68ki_cpu.mmu_crp_aptr & M68K_MMU_DF_ADDR_MASK;
	}

	pageshift = (m68ki_cpu.mmu_tc >> 16) & 0xf;
	address_work = logical << pageshift;
	bitpos = (m68ki_cpu.mmu_tc & M68K_MMU_TC_FCL) ? 16 : 12;
	m68ki_cpu.mmu_tablewalk = 1;

	do
	{
		const uint indexbits = bitpos <= 16 ? ((bits >> bitpos) & 0xf) : 0;
		const uint index = bitpos == 16 ? (fc & 7)
			: (indexbits ? address_work >> (32 - indexbits) : 0);
		const int indirect = indexbits && (bitpos == 0 || !(bits & ((1U << bitpos) - 1U)));
		uint descriptor_address, first, second = 0;
		int is_long;
		bitpos = bitpos >= 4 ? bitpos - 4 : 0xffffffffU;

		if (type == M68K_MMU_DF_DT_INVALID)
		{
			result->status |= M68K_MMU_SR_INVALID;
			resolved = 1;
			break;
		}
		if (type == M68K_MMU_DF_DT_PAGE)
		{
			const uint ps = pmmu_page_shift();
			result->physical_address = (table & (0xffffffffU << ps))
				+ (address_work >> pageshift);
			resolved = 1;
			break;
		}

		is_long = type == M68K_MMU_DF_DT_TABLE_8BYTE;
		descriptor_address = table + index * (is_long ? 8U : 4U);
		first = m68k_read_memory_32(descriptor_address);
		if (is_long) second = m68k_read_memory_32(descriptor_address + 4);
		if (m68ki_cpu.mmu_tmp_sr & M68K_MMU_SR_BUS_ERROR)
		{
			result->status |= m68ki_cpu.mmu_tmp_sr;
			resolved = 1;
			break;
		}
		type = first & M68K_MMU_DF_DT;
		++level;

		if (indirect && (type == M68K_MMU_DF_DT_TABLE_4BYTE
			|| type == M68K_MMU_DF_DT_TABLE_8BYTE))
		{
			const int indirect_long = type == M68K_MMU_DF_DT_TABLE_8BYTE;
			descriptor_address = (is_long ? second : first) & M68K_MMU_DF_IND_ADDR_MASK;
			first = m68k_read_memory_32(descriptor_address);
			second = indirect_long ? m68k_read_memory_32(descriptor_address + 4) : 0;
			if (m68ki_cpu.mmu_tmp_sr & M68K_MMU_SR_BUS_ERROR)
			{
				result->status |= m68ki_cpu.mmu_tmp_sr;
				resolved = 1;
				break;
			}
			is_long = indirect_long;
			type = first & M68K_MMU_DF_DT;
			++level;
		}

		result->descriptor_address = descriptor_address;
		pmmu_status_from_descriptor(type, first, is_long, fc, &result->status);
		if (side_effects)
			pmmu_update_descriptor(descriptor_address, type, first, is_long, rw);
		table = (is_long ? second : first) & M68K_MMU_DF_ADDR_MASK;
		address_work <<= indexbits;
		pageshift += indexbits;
		if (level >= limit)
			break;
	} while (!resolved && level < 7);

	if (!resolved && limit >= 7)
		result->status |= M68K_MMU_SR_INVALID;
	result->level = (uint8)level;
	result->status = (result->status & 0xfff0U) | (level & 7U);
	result->fault = (result->status & (M68K_MMU_SR_BUS_ERROR
		| M68K_MMU_SR_INVALID | M68K_MMU_SR_SUPERVISOR_ONLY)) != 0
		|| (!rw && (result->status & M68K_MMU_SR_WRITE_PROTECT));
	m68ki_cpu.mmu_tablewalk = 0;
}

static pmmu_translation_result pmmu_translate(uint logical, uint fc, uint rw,
	uint size, enum pmmu_intent intent, uint limit)
{
	pmmu_translation_result result;
	memset(&result, 0, sizeof(result));
	result.physical_address = logical;
	m68ki_cpu.mmu_last_logical_addr = logical;
	m68ki_cpu.mmu_tmp_sr = 0;

	if (pmmu_match_tt(logical, fc, m68ki_cpu.mmu_tt0, rw, &result.status)
		|| pmmu_match_tt(logical, fc, m68ki_cpu.mmu_tt1, rw, &result.status)
		|| fc == 7)
	{
		result.transparent = 1;
		return result;
	}

	if (intent == PMMU_INTENT_PTEST && limit == 0)
	{
		(void)pmmu_atc_lookup(logical, fc, rw, 1, &result);
		return result;
	}
	if ((intent == PMMU_INTENT_NORMAL || intent == PMMU_INTENT_PROBE)
		&& pmmu_atc_lookup(logical, fc, rw, 0, &result))
		goto finished;

	pmmu_walk_tables(logical, fc, rw, limit,
		intent == PMMU_INTENT_NORMAL || intent == PMMU_INTENT_PLOAD, &result);
	if (intent == PMMU_INTENT_NORMAL || intent == PMMU_INTENT_PLOAD
		|| intent == PMMU_INTENT_PROBE)
		pmmu_atc_add(logical, result.physical_address, fc, &result);

finished:
	m68ki_cpu.mmu_tmp_sr = result.status;
	if (result.fault && intent == PMMU_INTENT_NORMAL)
	{
		m68ki_cpu.mmu_fault_address = logical;
		m68ki_cpu.mmu_fault_fc = (uint8)(fc & 7);
		m68ki_cpu.mmu_fault_rw = (uint8)rw;
		m68ki_cpu.mmu_fault_size = (uint8)size;
		m68ki_cpu.mmu_fault_is_mmu = 1;
		m68ki_exception_bus_error();
	}
	return result;
}

uint pmmu_translate_addr_fc_size(uint address, uint fc, uint rw, uint size)
{
	return pmmu_translate(address, fc, rw, size, PMMU_INTENT_NORMAL, 7).physical_address;
}

uint pmmu_translate_addr_fc(uint address, uint fc, uint rw)
{
	return pmmu_translate(address, fc, rw, 4, PMMU_INTENT_PROBE, 7).physical_address;
}

uint pmmu_translate_addr(uint address)
{
	const uint fc = (m68ki_get_sr() & 0x2000)
		? FUNCTION_CODE_SUPERVISOR_PROGRAM : FUNCTION_CODE_USER_PROGRAM;
	return pmmu_translate_addr_fc(address, fc, 1);
}

uint pmmu_debug_translate_addr(uint address, uint fc)
{
	/* Debugger address inspection must not perturb architecturally visible
	 * PMMU state or change how the next guest access completes. */
	const uint saved_last = m68ki_cpu.mmu_last_logical_addr;
	const uint16 saved_tmp_sr = m68ki_cpu.mmu_tmp_sr;
	const uint saved_fault_address = m68ki_cpu.mmu_fault_address;
	const uint8 saved_fault_fc = m68ki_cpu.mmu_fault_fc;
	const uint8 saved_fault_rw = m68ki_cpu.mmu_fault_rw;
	const uint8 saved_fault_size = m68ki_cpu.mmu_fault_size;
	const uint8 saved_fault_is_mmu = m68ki_cpu.mmu_fault_is_mmu;
	const uint8 saved_tablewalk = m68ki_cpu.mmu_tablewalk;
	const uint physical = pmmu_translate(address, fc, 1, 1,
		PMMU_INTENT_DEBUG, 7).physical_address;
	m68ki_cpu.mmu_last_logical_addr = saved_last;
	m68ki_cpu.mmu_tmp_sr = saved_tmp_sr;
	m68ki_cpu.mmu_fault_address = saved_fault_address;
	m68ki_cpu.mmu_fault_fc = saved_fault_fc;
	m68ki_cpu.mmu_fault_rw = saved_fault_rw;
	m68ki_cpu.mmu_fault_size = saved_fault_size;
	m68ki_cpu.mmu_fault_is_mmu = saved_fault_is_mmu;
	m68ki_cpu.mmu_tablewalk = saved_tablewalk;
	return physical;
}

static void pmmu_atc_flush_modes(uint modes, uint ea)
{
	const uint mode = (modes >> 10) & 7;
	const uint fc_mask = (modes >> 5) & 7;
	const uint fc = pmmu_fc_from_modes(modes) & fc_mask;
	const uint shift = pmmu_page_shift();
	const uint page_mask = 0xffffffffU >> (32 - shift);
	uint i, address = 0;
	if (mode == 1)
	{
		pmmu_atc_flush();
		return;
	}
	if ((mode == 6 || mode == 7) && !pmmu_decode_ea(ea, &address))
		return;
	for (i = 0; i < M68K_MMU_ATC_ENTRIES; ++i)
	{
		m68ki_mmu_atc_entry *entry = &m68ki_cpu.mmu_atc[i];
		if (!entry->valid || ((entry->function_code & fc_mask) != fc))
			continue;
		if ((mode == 6 || mode == 7) && entry->logical_page != (address & ~page_mask))
			continue;
		entry->valid = 0;
	}
}

static int pmmu_validate_tc(uint tc)
{
	uint bits, shift;
	if (!(tc & 0x80000000U)) return 1;
	bits = ((tc >> 20) & 0xf) + ((tc >> 16) & 0xf);
	if (((tc >> 20) & 0xf) < 8) return 0;
	for (shift = 12; ; shift -= 4)
	{
		const uint ti = (tc >> shift) & 0xf;
		if (!ti) break;
		bits += ti;
		if (shift == 0) break;
	}
	return bits == 32;
}

void m68881_mmu_ops(void)
{
	uint16 modes;
	uint ea = REG_IR & 0x3f;
	uint64 temp64;
	/* 68040 MMU instructions and descriptors are deliberately kept out of the
	 * 68851/68030 coprocessor decoder. The CPU kind is the dispatch boundary
	 * for the future native 68040 implementation. */
	if (m68ki_cpu.mmu_kind == M68K_MMU_KIND_68040)
	{
		pmmu_unimplemented(0, "68040 native MMU instruction; only the 68851/68030 decoder is implemented");
		return;
	}
	if ((REG_IR & 0xffc0) == 0xf0c0 || (REG_IR & 0xffc0) == 0xf080)
	{
		pmmu_unimplemented(0, "PBcc/PDBcc/PScc/PTRAPcc conditional form");
		return;
	}
	if (((REG_IR >> 9) & 7) != 0)
	{
		pmmu_illegal(0, "coprocessor id %d is not the PMMU", (REG_IR >> 9) & 7);
		return;
	}
	modes = OPER_I_16();
	if ((modes & 0xfde0) == 0x2000) /* PLOAD */
	{
		uint address;
		if (pmmu_decode_ea(ea, &address))
			(void)pmmu_translate(address, pmmu_fc_from_modes(modes),
				(modes & 0x200) != 0, 4, PMMU_INTENT_PLOAD, 7);
		return;
	}
	if ((modes & 0xe200) == 0x2000) /* PFLUSH */
	{
		pmmu_atc_flush_modes(modes, ea);
		return;
	}
	if (modes == 0xa000) /* PFLUSHR */
	{
		pmmu_atc_flush();
		return;
	}
	if ((modes & 0xe000) == 0x8000) /* PTEST */
	{
		uint address;
		const uint level = (modes >> 10) & 7;
		const uint rw = (modes & 0x200) != 0;
		pmmu_translation_result result;
		if (m68ki_cpu.mmu_kind == M68K_MMU_KIND_68030 && level == 0 && (modes & 0x100))
		{
			pmmu_illegal((uint16_t)modes, "PTEST level 0 with an A register is invalid on the 68030");
			return;
		}
		if (!pmmu_decode_ea(ea, &address)) return;
		result = pmmu_translate(address, pmmu_fc_from_modes(modes), rw,
			4, PMMU_INTENT_PTEST, level);
		m68ki_cpu.mmu_sr = result.status;
		if (modes & 0x100)
			REG_A[(modes >> 5) & 7] = result.descriptor_address;
		return;
	}
	if (modes == 0x2800 || (modes & 0xfff8) == 0x2c00) /* PVALID */
	{
		pmmu_unimplemented((uint16_t)modes, "PVALID is a 68851 instruction");
		return;
	}

	switch ((modes >> 13) & 7)
	{
		case 0: /* TT0/TT1 */
		{
			const uint reg = (modes >> 10) & 7;
			if (reg != 2 && reg != 3) {
				pmmu_unimplemented((uint16_t)modes, "PMOVE transparent translation register %d (only TT0 and TT1 exist)", reg);
				return;
			}
			if (modes & 0x200)
				WRITE_EA_32(ea, reg == 2 ? m68ki_cpu.mmu_tt0 : m68ki_cpu.mmu_tt1);
			else
			{
				const uint value = READ_EA_32(ea);
				if (reg == 2) m68ki_cpu.mmu_tt0 = value; else m68ki_cpu.mmu_tt1 = value;
				if (!(modes & 0x100)) pmmu_atc_flush();
			}
			return;
		}
		case 2: /* TC/SRP/CRP */
		{
			const uint reg = (modes >> 10) & 7;
			if (modes & 0x200)
			{
				if (reg == 0) WRITE_EA_32(ea, m68ki_cpu.mmu_tc);
				else if (reg == 2) WRITE_EA_64(ea, (uint64)m68ki_cpu.mmu_srp_limit << 32 | m68ki_cpu.mmu_srp_aptr);
				else if (reg == 3) WRITE_EA_64(ea, (uint64)m68ki_cpu.mmu_crp_limit << 32 | m68ki_cpu.mmu_crp_aptr);
				else {
					pmmu_unimplemented((uint16_t)modes, "PMOVE from MMU register %d (TC, SRP and CRP are implemented)", reg);
				}
				return;
			}
			if (reg == 0)
			{
				const uint tc = READ_EA_32(ea);
				if (!pmmu_validate_tc(tc))
				{
					PMMU_ENABLED = 0;
					m68ki_exception_trap(56);
					return;
				}
				m68ki_cpu.mmu_tc = tc;
				PMMU_ENABLED = (tc >> 31) & 1;
			}
			else if (reg == 2)
			{
				temp64 = READ_EA_64(ea);
				m68ki_cpu.mmu_srp_limit = (uint)(temp64 >> 32);
				m68ki_cpu.mmu_srp_aptr = (uint)temp64;
			}
			else if (reg == 3)
			{
				temp64 = READ_EA_64(ea);
				m68ki_cpu.mmu_crp_limit = (uint)(temp64 >> 32);
				m68ki_cpu.mmu_crp_aptr = (uint)temp64;
			}
			else {
				pmmu_unimplemented((uint16_t)modes, "PMOVE to MMU register %d (TC, SRP and CRP are implemented)", reg);
				return;
			}
			if (!(modes & 0x100)) pmmu_atc_flush();
			return;
		}
		case 3: /* MMUSR/68851 PSR */
			if (modes & 0x200) WRITE_EA_16(ea, m68ki_cpu.mmu_sr);
			else m68ki_cpu.mmu_sr = (uint16)READ_EA_16(ea);
			return;
	}
	pmmu_unimplemented((uint16_t)modes, "unhandled PMMU encoding");
}
