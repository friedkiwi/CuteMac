/*
    m68kmmu.h - PMMU implementation for 68851/68030/68040

    By R. Belmont

    Copyright Nicola Salmoria and the MAME Team.
    Visit http://mamedev.org for licensing and usage restrictions.
*/

/*
	pmmu_translate_addr: perform 68851/68030-style PMMU address translation
*/
void pmmu_atc_flush(void)
{
	uint entry;
	for (entry = 0; entry < M68K_MMU_ATC_ENTRIES; ++entry)
		m68ki_cpu.mmu_atc[entry].valid = 0;
}

static uint pmmu_atc_page_shift(void)
{
	const uint table_bits = ((m68ki_cpu.mmu_tc >> 16) & 0xf)
		+ ((m68ki_cpu.mmu_tc >> 12) & 0xf)
		+ ((m68ki_cpu.mmu_tc >> 8) & 0xf)
		+ ((m68ki_cpu.mmu_tc >> 4) & 0xf);
	if (table_bits >= 32)
		return 8;
	if (32 - table_bits < 8)
		return 8;
	if (32 - table_bits > 31)
		return 31;
	return 32 - table_bits;
}

static int pmmu_match_tt(uint addr_in, uint fc, uint tt, uint rw)
{
	uint address_base, address_mask, fc_mask, fc_bits;

	if (!(tt & 0x00008000U))
		return 0;

	address_base = tt & 0xff000000U;
	address_mask = ((tt << 8) & 0xff000000U) ^ 0xff000000U;
	fc_mask = (~tt) & 7U;
	fc_bits = (tt >> 4) & 7U;
	return (addr_in & address_mask) == (address_base & address_mask)
		&& (fc & fc_mask) == (fc_bits & fc_mask)
		&& ((tt & 0x100U) || (rw != 0) == (((tt >> 9) & 1U) != 0));
}

uint pmmu_translate_addr_fc(uint addr_in, uint fc, uint rw)
{
	uint32 addr_out, tbl_entry = 0, tbl_entry2, tamode = 0, tbmode = 0, tcmode = 0;
	uint root_aptr, root_limit, tofs, is, abits, bbits, cbits;
	uint resolved, tptr, shift, page_shift, page_mask, supervisor, atc_index;
	m68ki_mmu_atc_entry *atc_entry;

	/* The 68030 transparent translation registers are checked before the
	 * ATC and page tables.  A match maps the logical address unchanged. */
	if (pmmu_match_tt(addr_in, fc, m68ki_cpu.mmu_tt0, rw)
		|| pmmu_match_tt(addr_in, fc, m68ki_cpu.mmu_tt1, rw))
		return addr_in;

	page_shift = pmmu_atc_page_shift();
	page_mask = 0xffffffffU >> (32 - page_shift);
	supervisor = (fc & 4U) != 0;
	atc_index = ((addr_in >> page_shift) ^ (supervisor ? 0x80U : 0U)) & (M68K_MMU_ATC_ENTRIES - 1);
	atc_entry = &m68ki_cpu.mmu_atc[atc_index];
	if (atc_entry->valid && atc_entry->page_shift == page_shift
		&& atc_entry->supervisor == supervisor
		&& atc_entry->logical_page == (addr_in & ~page_mask))
	{
		++m68ki_cpu.mmu_atc_hits;
		return atc_entry->physical_page | (addr_in & page_mask);
	}
	++m68ki_cpu.mmu_atc_misses;

	resolved = 0;
	addr_out = addr_in;

	// if SRP is enabled and we're in supervisor mode, use it
	if ((m68ki_cpu.mmu_tc & 0x02000000) && supervisor)
	{
		root_aptr = m68ki_cpu.mmu_srp_aptr;
		root_limit = m68ki_cpu.mmu_srp_limit;
	}
	else	// else use the CRP
	{
		root_aptr = m68ki_cpu.mmu_crp_aptr;
		root_limit = m68ki_cpu.mmu_crp_limit;
	}

	// get initial shift (# of top bits to ignore)
	is = (m68ki_cpu.mmu_tc>>16) & 0xf;
	abits = (m68ki_cpu.mmu_tc>>12)&0xf;
	bbits = (m68ki_cpu.mmu_tc>>8)&0xf;
	cbits = (m68ki_cpu.mmu_tc>>4)&0xf;

//	fprintf(stderr,"PMMU: tcr %08x limit %08x aptr %08x is %x abits %d bbits %d cbits %d\n", m68ki_cpu.mmu_tc, root_limit, root_aptr, is, abits, bbits, cbits);

	// get table A offset
	tofs = (addr_in<<is)>>(32-abits);

	// find out what format table A is
	switch (root_limit & 3)
	{
		case 0:	// invalid, should cause MMU exception
		case 1:	// page descriptor, should cause direct mapping
			fatalerror("680x0 PMMU: Unhandled root mode\n");
			break;

		case 2:	// valid 4 byte descriptors
			tofs *= 4;
//			fprintf(stderr,"PMMU: reading table A entry at %08x\n", tofs + (root_aptr & 0xfffffffc));
			tbl_entry = m68k_read_memory_32( tofs + (root_aptr & 0xfffffffc));
			tamode = tbl_entry & 3;
//			fprintf(stderr,"PMMU: addr %08x entry %08x mode %x tofs %x\n", addr_in, tbl_entry, tamode, tofs);
			break;

		case 3: // valid 8 byte descriptors
			tofs *= 8;
//			fprintf(stderr,"PMMU: reading table A entries at %08x\n", tofs + (root_aptr & 0xfffffffc));
			tbl_entry2 = m68k_read_memory_32( tofs + (root_aptr & 0xfffffffc));
			tbl_entry = m68k_read_memory_32( tofs + (root_aptr & 0xfffffffc)+4);
			tamode = tbl_entry2 & 3;
//			fprintf(stderr,"PMMU: addr %08x entry %08x entry2 %08x mode %x tofs %x\n", addr_in, tbl_entry, tbl_entry2, tamode, tofs);
			break;
	}

	// get table B offset and pointer
	tofs = (addr_in<<(is+abits))>>(32-bbits);
	tptr = tbl_entry & 0xfffffff0;

	// find out what format table B is, if any
	switch (tamode)
	{
		case 0: // invalid, should cause MMU exception
			fatalerror("680x0 PMMU: Unhandled Table A mode %d (addr_in %08x)\n", tamode, addr_in);
			break;

		case 2: // 4-byte table B descriptor
			tofs *= 4;
//			fprintf(stderr,"PMMU: reading table B entry at %08x\n", tofs + tptr);
			tbl_entry = m68k_read_memory_32( tofs + tptr);
			tbmode = tbl_entry & 3;
//			fprintf(stderr,"PMMU: addr %08x entry %08x mode %x tofs %x\n", addr_in, tbl_entry, tbmode, tofs);
			break;

		case 3: // 8-byte table B descriptor
			tofs *= 8;
//			fprintf(stderr,"PMMU: reading table B entries at %08x\n", tofs + tptr);
			tbl_entry2 = m68k_read_memory_32( tofs + tptr);
			tbl_entry = m68k_read_memory_32( tofs + tptr + 4);
			tbmode = tbl_entry2 & 3;
//			fprintf(stderr,"PMMU: addr %08x entry %08x entry2 %08x mode %x tofs %x\n", addr_in, tbl_entry, tbl_entry2, tbmode, tofs);
			break;

		case 1:	// early termination descriptor
			tbl_entry &= 0xffffff00;

			shift = is+abits;
			addr_out = ((addr_in<<shift)>>shift) + tbl_entry;
			resolved = 1;
			break;
	}

	// if table A wasn't early-out, continue to process table B
	if (!resolved)
	{
		// get table C offset and pointer
		tofs = (addr_in<<(is+abits+bbits))>>(32-cbits);
		tptr = tbl_entry & 0xfffffff0;

		switch (tbmode)
		{
			case 0:	// invalid, should cause MMU exception
				fatalerror("680x0 PMMU: Unhandled Table B mode %d (addr_in %08x PC %x)\n", tbmode, addr_in, REG_PC);
				break;

			case 2: // 4-byte table C descriptor
				tofs *= 4;
//				fprintf(stderr,"PMMU: reading table C entry at %08x\n", tofs + tptr);
				tbl_entry = m68k_read_memory_32(tofs + tptr);
				tcmode = tbl_entry & 3;
//				fprintf(stderr,"PMMU: addr %08x entry %08x mode %x tofs %x\n", addr_in, tbl_entry, tbmode, tofs);
				break;

			case 3: // 8-byte table C descriptor
				tofs *= 8;
//				fprintf(stderr,"PMMU: reading table C entries at %08x\n", tofs + tptr);
				tbl_entry2 = m68k_read_memory_32(tofs + tptr);
				tbl_entry = m68k_read_memory_32(tofs + tptr + 4);
				tcmode = tbl_entry2 & 3;
//				fprintf(stderr,"PMMU: addr %08x entry %08x entry2 %08x mode %x tofs %x\n", addr_in, tbl_entry, tbl_entry2, tbmode, tofs);
				break;

			case 1: // termination descriptor
				tbl_entry &= 0xffffff00;

				shift = is+abits+bbits;
				addr_out = ((addr_in<<shift)>>shift) + tbl_entry;
				resolved = 1;
				break;
		}
	}

	if (!resolved)
	{
		switch (tcmode)
		{
			case 0:	// invalid, should cause MMU exception
			case 2: // 4-byte ??? descriptor
			case 3: // 8-byte ??? descriptor
				fatalerror("680x0 PMMU: Unhandled Table B mode %d (addr_in %08x PC %x)\n", tbmode, addr_in, REG_PC);
				break;

			case 1: // termination descriptor
				tbl_entry &= 0xffffff00;

				shift = is+abits+bbits+cbits;
				addr_out = ((addr_in<<shift)>>shift) + tbl_entry;
				resolved = 1;
				break;
		}
	}


	atc_entry->logical_page = addr_in & ~page_mask;
	atc_entry->physical_page = addr_out & ~page_mask;
	atc_entry->page_shift = (uint8)page_shift;
	atc_entry->supervisor = (uint8)supervisor;
	atc_entry->valid = 1;

	return addr_out;
}

uint pmmu_translate_addr(uint addr_in)
{
	const uint fc = (m68ki_get_sr() & 0x2000)
		? FUNCTION_CODE_SUPERVISOR_PROGRAM : FUNCTION_CODE_USER_PROGRAM;
	return pmmu_translate_addr_fc(addr_in, fc, 1);
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

	m68ki_exception_1111();
	return 0;
}

static uint pmmu_fc_from_modes(uint modes)
{
	if ((modes & 0x1f) == 0)
		return REG_SFC;
	if ((modes & 0x1f) == 1)
		return REG_DFC;
	if (((modes >> 3) & 3) == 1)
		return REG_D[modes & 7] & 7;
	if (((modes >> 3) & 3) == 2)
		return modes & 7;
	return 0;
}

/*

	m68881_mmu_ops: COP 0 MMU opcode handling

*/

void m68881_mmu_ops(void)
{
	uint16 modes;
	uint32 ea = m68ki_cpu.ir & 0x3f;
	uint64 temp64;

	// catch the 2 "weird" encodings up front (PBcc)
	if ((m68ki_cpu.ir & 0xffc0) == 0xf0c0)
	{
		fprintf(stderr,"680x0: unhandled PBcc\n");
		return;
	}
	else if ((m68ki_cpu.ir & 0xffc0) == 0xf080)
	{
		fprintf(stderr,"680x0: unhandled PBcc\n");
		return;
	}
	else	// the rest are 1111000xxxXXXXXX where xxx is the instruction family
	{
		switch ((m68ki_cpu.ir>>9) & 0x7)
		{
			case 0:
				modes = OPER_I_16();

				if ((modes & 0xfde0) == 0x2000)	// PLOAD
				{
					uint address;
					if (!(m68ki_cpu.cpu_type & CPU_TYPE_030))
					{
						m68ki_exception_1111();
						return;
					}
					if (pmmu_decode_ea(ea, &address))
						(void)pmmu_translate_addr_fc(address,
							pmmu_fc_from_modes(modes), (modes & 0x200) != 0);
					return;
				}
				else if ((modes & 0xe200) == 0x2000)	// PFLUSH
				{
					pmmu_atc_flush();
					return;
				}
				else if (modes == 0xa000)	// PFLUSHR
				{
					pmmu_atc_flush();
					return;
				}
				else if (modes == 0x2800)	// PVALID (FORMAT 1)
				{
					fprintf(stderr,"680x0: unhandled PVALID1\n");
					return;
				}
				else if ((modes & 0xfff8) == 0x2c00)	// PVALID (FORMAT 2)
				{
					fprintf(stderr,"680x0: unhandled PVALID2\n");
					return;
				}
				else if ((modes & 0xe000) == 0x8000)	// PTEST
				{
					fprintf(stderr,"680x0: unhandled PTEST\n");
					return;
				}
				else
				{
					switch ((modes>>13) & 0x7)
					{
						case 0:	// MC68030 transparent translation registers
							if (!(m68ki_cpu.cpu_type & CPU_TYPE_030))
							{
								m68ki_exception_1111();
								return;
							}
							if (((modes >> 10) & 7) != 2 && ((modes >> 10) & 7) != 3)
							{
								m68ki_exception_1111();
								return;
							}
							if (modes & 0x200)
								WRITE_EA_32(ea, ((modes >> 10) & 7) == 2
									? m68ki_cpu.mmu_tt0 : m68ki_cpu.mmu_tt1);
							else
							{
								const uint value = READ_EA_32(ea);
								if (((modes >> 10) & 7) == 2)
									m68ki_cpu.mmu_tt0 = value;
								else
									m68ki_cpu.mmu_tt1 = value;
								if (!(modes & 0x100))
									pmmu_atc_flush();
							}
							break;

						case 2:	// TC/SRP/CRP (68030 or external 68851)
							if (modes & 0x200)
							{
							 	switch ((modes>>10) & 7)
								{
									case 0:	// translation control register
										WRITE_EA_32(ea, m68ki_cpu.mmu_tc);
										break;

									case 2: // supervisor root pointer
										WRITE_EA_64(ea, (uint64)m68ki_cpu.mmu_srp_limit<<32 | (uint64)m68ki_cpu.mmu_srp_aptr);
										break;

									case 3: // CPU root pointer
										WRITE_EA_64(ea, (uint64)m68ki_cpu.mmu_crp_limit<<32 | (uint64)m68ki_cpu.mmu_crp_aptr);
										break;

									default:
										fprintf(stderr,"680x0: PMOVE from unknown MMU register %x, PC %x\n", (modes>>10) & 7, REG_PC);
										break;
								}
							}
							else
							{
							 	switch ((modes>>10) & 7)
								{
					case 0:	// translation control register
						m68ki_cpu.mmu_tc = READ_EA_32(ea);
						if (!(modes & 0x100))
							pmmu_atc_flush();

										if (m68ki_cpu.mmu_tc & 0x80000000)
										{
											m68ki_cpu.pmmu_enabled = 1;
										}
										else
										{
											m68ki_cpu.pmmu_enabled = 0;
										}
										break;

									case 2:	// supervisor root pointer
										temp64 = READ_EA_64(ea);
						m68ki_cpu.mmu_srp_limit = (temp64>>32) & 0xffffffff;
						m68ki_cpu.mmu_srp_aptr = temp64 & 0xffffffff;
						if (!(modes & 0x100))
							pmmu_atc_flush();
										break;

									case 3:	// CPU root pointer
										temp64 = READ_EA_64(ea);
						m68ki_cpu.mmu_crp_limit = (temp64>>32) & 0xffffffff;
						m68ki_cpu.mmu_crp_aptr = temp64 & 0xffffffff;
						if (!(modes & 0x100))
							pmmu_atc_flush();
										break;

									default:
										fprintf(stderr,"680x0: PMOVE to unknown MMU register %x, PC %x\n", (modes>>10) & 7, REG_PC);
										break;
								}
							}
							break;

						case 3:	// MC68030 to/from status reg
							if (modes & 0x200)
							{
								WRITE_EA_32(ea, m68ki_cpu.mmu_sr);
							}
							else
							{
								m68ki_cpu.mmu_sr = READ_EA_32(ea);
							}
							break;

						default:
							fprintf(stderr,"680x0: unknown PMOVE mode %x (modes %04x) (PC %x)\n", (modes>>13) & 0x7, modes, REG_PC);
							break;
					}
				}
				break;

			default:
				fprintf(stderr,"680x0: unknown PMMU instruction group %d\n", (m68ki_cpu.ir>>9) & 0x7);
				break;
		}
	}
}
