        | CuteMac System 6 Guest Tools.
        |
        | QuickDraw normally dispatches CopyBits through the current port's
        | QDProcs/CQDProcs bitsProc.  Patch port/procedure initialization so
        | each new table receives our resident bitsProc, while retaining the
        | system routine for complete software fallback.
        .text
        .even
        .globl  _start

        .equ    grafProcs,          0x68
        .equ    rectProc,           0x08
        .equ    bitsProc,           0x20
        .equ    acceleratorDelta,   0x000a0000

_start:
        movem.l %d0-%d7/%a0-%a6,-(%sp)

        cmpi.l  #0x01000000,0x02ae      | compact Macs have a low ROMBase
        blo.w   guest_tools_done        | never probe nonexistent NuBus space

        movea.l #0x009a0000,%a0         | slot 9 accelerator aperture
        moveq   #5,%d1                  | scan slots 9 through 14
guest_tools_find_card:
        cmpi.l  #0x43564131,(%a0)       | 'CVA1'
        beq.s   guest_tools_card_found
        adda.l  #0x00100000,%a0
        dbra    %d1,guest_tools_find_card
        bra.w   guest_tools_done

guest_tools_card_found:
        lea     guest_tools_card_base(%pc),%a1
        move.l  %a0,(%a1)

        lea     guest_tools_late_install(%pc),%a1
        move.l  %a1,0x0038(%a0)         | slot-VBL callback mailbox

        lea     guest_tools_card_base(%pc),%a0
        movea.l (%a0),%a0
        move.l  #0x43564754,0x002c(%a0) | 'CVGT'
        move.w  0x015a,%d0              | SysVersion
        move.w  %d0,0x0032(%a0)
        move.l  #3,0x0034(%a0)          | copy/fill/mono QuickDraw hook version
        move.b  #2,0x005f(%a0)          | controlGuestAttach

guest_tools_done:
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts

        | D0=trap word, A1=old-address cell, A2=replacement.
guest_tools_install_trap:
        move.l  %d0,-(%sp)
        .word   0xa746                   | _GetToolTrapAddress -> A0
        move.l  %a0,(%a1)
        movea.l %a2,%a0
        move.l  (%sp)+,%d0
        .word   0xa647                   | _SetToolTrapAddress
        rts

guest_tools_late_install:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        move.w  #0x0170,%d0             | final _GetNextEvent
        lea     guest_tools_old_get_next_event(%pc),%a1
        lea     guest_tools_get_next_event_hook(%pc),%a2
        bsr.w   guest_tools_install_trap
        move.w  #0x0060,%d0             | final _WaitNextEvent
        lea     guest_tools_old_wait_next_event(%pc),%a1
        lea     guest_tools_wait_next_event_hook(%pc),%a2
        bsr.w   guest_tools_install_trap
        move.w  #0x00ea,%d0             | final _SetStdProcs
        lea     guest_tools_old_set_std_procs(%pc),%a1
        lea     guest_tools_set_std_procs_hook(%pc),%a2
        bsr.w   guest_tools_install_trap
        move.w  #0xaa4e,%d0             | final _SetStdCProcs
        lea     guest_tools_old_set_std_cprocs(%pc),%a1
        lea     guest_tools_set_std_cprocs_hook(%pc),%a2
        bsr.w   guest_tools_install_trap
        move.w  #0x00eb,%d0             | final global _StdBits fallback path
        .word   0xa746
        lea     guest_tools_old_bits_proc(%pc),%a1
        move.l  %a0,(%a1)
        lea     guest_tools_old_color_bits_proc(%pc),%a1
        move.l  %a0,(%a1)
        lea     guest_tools_bits_proc(%pc),%a0
        move.w  #0x00eb,%d0
        .word   0xa647
        move.w  #0x0073,%d0             | capture final CQD _SetPort
        .word   0xa746                   | _GetToolTrapAddress -> A0
        lea     guest_tools_set_port_hook(%pc),%a1
        cmpa.l  %a1,%a0
        beq.s   guest_tools_late_done
        lea     guest_tools_old_set_port(%pc),%a2
        move.l  %a0,(%a2)
        movea.l %a1,%a0
        move.w  #0x0073,%d0
        .word   0xa647                   | _SetToolTrapAddress
        movea.l 0x0904,%a1              | attach current port without SetPort
        cmpa.l  #0,%a1
        beq.s   guest_tools_late_done
        movea.l (%a1),%a2
        cmpa.l  #0,%a2
        beq.s   guest_tools_late_done
        move.w  6(%a2),%d0
        andi.w  #0xc000,%d0
        cmpi.w  #0xc000,%d0
        bne.s   guest_tools_late_attach_mono
        pea     guest_tools_cqd_procs(%pc)
        .word   0xaa4e
        addq.l  #4,%sp
        lea     guest_tools_cqd_procs(%pc),%a0
        move.l  %a0,grafProcs(%a2)
        bra.s   guest_tools_late_done
guest_tools_late_attach_mono:
        pea     guest_tools_qd_procs(%pc)
        .word   0xa8ea
        addq.l  #4,%sp
        lea     guest_tools_qd_procs(%pc),%a0
        move.l  %a0,grafProcs(%a2)
guest_tools_late_done:
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts

guest_tools_get_next_event_hook:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        bsr.s   guest_tools_attach_active_port
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        lea     guest_tools_old_get_next_event(%pc),%a0
        movea.l (%a0),%a0
        jmp     (%a0)

guest_tools_wait_next_event_hook:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        bsr.s   guest_tools_attach_active_port
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        lea     guest_tools_old_wait_next_event(%pc),%a0
        movea.l (%a0),%a0
        jmp     (%a0)

guest_tools_attach_active_port:
        lea     guest_tools_active_port_attached(%pc),%a0
        tst.b   (%a0)
        bne.s   guest_tools_attach_active_done
        movea.l 0x0904,%a1              | CurrentA5 -> qd.thePort
        cmpa.l  #0,%a1
        beq.s   guest_tools_attach_active_done
        move.l  (%a1),%d0
        beq.s   guest_tools_attach_active_done
        move.l  %d0,-(%sp)
        .word   0xa873                   | task-context port attachment
        addq.l  #4,%sp
        st      (%a0)
guest_tools_attach_active_done:
        rts

guest_tools_set_port_hook:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        movea.l 64(%sp),%a2             | GrafPtr/CGrafPtr

        move.l  grafProcs(%a2),%d0
        beq.s   guest_tools_attach_port
        lea     guest_tools_qd_procs(%pc),%a0
        cmp.l   %a0,%d0
        beq.s   guest_tools_attach_port
        lea     guest_tools_cqd_procs(%pc),%a0
        cmp.l   %a0,%d0
        bne.s   guest_tools_set_port_done | preserve application custom procs

guest_tools_attach_port:
        move.w  6(%a2),%d0
        andi.w  #0xc000,%d0
        cmpi.w  #0xc000,%d0
        beq.s   guest_tools_attach_color_port

        pea     guest_tools_qd_procs(%pc)
        .word   0xa8ea                   | final system _SetStdProcs
        addq.l  #4,%sp
        lea     guest_tools_qd_procs(%pc),%a0
        move.l  %a0,grafProcs(%a2)
        bra.s   guest_tools_set_port_done

guest_tools_attach_color_port:
        pea     guest_tools_cqd_procs(%pc)
        .word   0xaa4e                   | final system _SetStdCProcs
        addq.l  #4,%sp
        lea     guest_tools_cqd_procs(%pc),%a0
        move.l  %a0,grafProcs(%a2)

guest_tools_set_port_done:
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        lea     guest_tools_old_set_port(%pc),%a0
        movea.l (%a0),%a0
        jmp     (%a0)                   | transparent head patch

        | Wrappers call originals with copied Pascal arguments and remove the
        | copies themselves; the original caller owns its argument cleanup.
guest_tools_set_std_procs_hook:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        movea.l 64(%sp),%a2             | QDProcs *
        move.l  %a2,-(%sp)
        lea     guest_tools_old_set_std_procs(%pc),%a0
        movea.l (%a0),%a0
        jsr     (%a0)
        addq.l  #4,%sp
        move.l  bitsProc(%a2),%d0
        lea     guest_tools_bits_proc(%pc),%a0
        cmp.l   %a0,%d0
        beq.s   guest_tools_set_std_rect
        lea     guest_tools_old_bits_proc(%pc),%a0
        move.l  %d0,(%a0)
        lea     guest_tools_bits_proc(%pc),%a0
        move.l  %a0,bitsProc(%a2)
guest_tools_set_std_rect:
        move.l  rectProc(%a2),%d0
        lea     guest_tools_rect_proc(%pc),%a0
        cmp.l   %a0,%d0
        beq.s   guest_tools_set_std_done
        lea     guest_tools_old_rect_proc(%pc),%a0
        move.l  %d0,(%a0)
        lea     guest_tools_rect_proc(%pc),%a0
        move.l  %a0,rectProc(%a2)
guest_tools_set_std_done:
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        bra.w   guest_tools_pascal_return_4

guest_tools_set_std_cprocs_hook:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        movea.l 64(%sp),%a2             | CQDProcs *
        move.l  %a2,-(%sp)
        lea     guest_tools_old_set_std_cprocs(%pc),%a0
        movea.l (%a0),%a0
        jsr     (%a0)
        addq.l  #4,%sp
        move.l  bitsProc(%a2),%d0
        lea     guest_tools_bits_proc(%pc),%a0
        cmp.l   %a0,%d0
        beq.s   guest_tools_set_stdc_rect
        lea     guest_tools_old_color_bits_proc(%pc),%a0
        move.l  %d0,(%a0)
        lea     guest_tools_bits_proc(%pc),%a0
        move.l  %a0,bitsProc(%a2)
guest_tools_set_stdc_rect:
        move.l  rectProc(%a2),%d0
        lea     guest_tools_rect_proc(%pc),%a0
        cmp.l   %a0,%d0
        beq.s   guest_tools_set_stdc_done
        lea     guest_tools_old_color_rect_proc(%pc),%a0
        move.l  %d0,(%a0)
        lea     guest_tools_rect_proc(%pc),%a0
        move.l  %a0,rectProc(%a2)
guest_tools_set_stdc_done:
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        bra.w   guest_tools_pascal_return_4

guest_tools_open_port_hook:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        movea.l 64(%sp),%a2             | GrafPtr
        move.l  %a2,-(%sp)
        lea     guest_tools_old_open_port(%pc),%a0
        movea.l (%a0),%a0
        jsr     (%a0)
        addq.l  #4,%sp
        pea     guest_tools_qd_procs(%pc)
        .word   0xa8ea                   | our _SetStdProcs wrapper
        addq.l  #4,%sp
        lea     guest_tools_qd_procs(%pc),%a0
        move.l  %a0,grafProcs(%a2)
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        bra.w   guest_tools_pascal_return_4

guest_tools_open_cport_hook:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        movea.l 64(%sp),%a2             | CGrafPtr
        move.l  %a2,-(%sp)
        lea     guest_tools_old_open_cport(%pc),%a0
        movea.l (%a0),%a0
        jsr     (%a0)
        addq.l  #4,%sp
        pea     guest_tools_cqd_procs(%pc)
        .word   0xaa4e                   | our _SetStdCProcs wrapper
        addq.l  #4,%sp
        lea     guest_tools_cqd_procs(%pc),%a0
        move.l  %a0,grafProcs(%a2)
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        bra.w   guest_tools_pascal_return_4

guest_tools_pascal_return_4:
        rts

        | bitsProc(srcBits, srcRect, dstRect, mode, maskRgn)
guest_tools_bits_proc:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        lea     guest_tools_card_base(%pc),%a0
        movea.l (%a0),%a0               | accelerator aperture
        cmpa.l  #0,%a0
        beq.w   guest_tools_bits_fallback
        move.l  #0,0x0080(%a0)          | trace: bitsProc call

        | GrafProcs callbacks use the 68k Pascal stack layout.  QuickDraw
        | pushes the declared arguments left-to-right, so the final maskRgn
        | is nearest the return address.
        movea.l 78(%sp),%a1             | source BitMap/PixMap
        movea.l 74(%sp),%a2             | source Rect
        movea.l 70(%sp),%a3             | destination Rect
        cmpi.w  #0,68(%sp)              | srcCopy only
        beq.s   guest_tools_bits_src_copy
        move.l  #2,0x0080(%a0)          | non-srcCopy mode
        bra.w   guest_tools_bits_fallback
guest_tools_bits_src_copy:
        move.l  #1,0x0080(%a0)
        tst.l   64(%sp)                 | no mask region
        beq.s   guest_tools_bits_unmasked
        move.l  #3,0x0080(%a0)
        bra.w   guest_tools_bits_fallback
guest_tools_bits_unmasked:

        move.w  4(%a1),%d1
        andi.l  #0x00003fff,%d1          | source rowBytes
        beq.w   guest_tools_bits_fallback
        moveq   #1,%d2                  | default BitMap depth
        btst    #7,4(%a1)               | PixMap rowBytes flag
        beq.s   guest_tools_source_depth
        move.w  32(%a1),%d2
guest_tools_source_depth:
        cmpi.w  #1,%d2
        bne.s   guest_tools_not_mono_source
        move.l  #5,0x0080(%a0)
        bra.w   guest_tools_mono_expand
guest_tools_not_mono_source:
        cmpi.w  #1,%d2
        beq.s   guest_tools_depth_ok
        cmpi.w  #2,%d2
        bne.s   guest_tools_not_depth2
        move.l  #6,0x0080(%a0)
        bra.s   guest_tools_depth_ok
guest_tools_not_depth2:
        cmpi.w  #4,%d2
        bne.s   guest_tools_not_depth4
        move.l  #7,0x0080(%a0)
        bra.s   guest_tools_depth_ok
guest_tools_not_depth4:
        cmpi.w  #8,%d2
        bne.w   guest_tools_bits_fallback
        move.l  #8,0x0080(%a0)
guest_tools_depth_ok:
        move.w  4(%a2),%d3              | src width
        sub.w   (%a2),%d3
        move.w  4(%a3),%d4              | dst width
        sub.w   (%a3),%d4
        cmp.w   %d3,%d4
        beq.s   guest_tools_width_same
        move.l  #4,0x0080(%a0)
        bra.w   guest_tools_bits_fallback
guest_tools_width_same:
        move.w  6(%a2),%d5              | src height
        sub.w   2(%a2),%d5
        move.w  6(%a3),%d6              | dst height
        sub.w   2(%a3),%d6
        cmp.w   %d5,%d6
        beq.s   guest_tools_height_same
        move.l  #4,0x0080(%a0)
        bra.w   guest_tools_bits_fallback
guest_tools_height_same:
        tst.w   %d3
        ble.w   guest_tools_bits_fallback
        tst.w   %d5
        ble.w   guest_tools_bits_fallback

        | Require byte-aligned horizontal bounds and width.
        move.w  (%a2),%d7
        sub.w   6(%a1),%d7
        mulu.w  %d2,%d7
        move.w  %d7,%d0
        andi.w  #7,%d0
        beq.s   guest_tools_source_aligned
        move.l  #12,0x0080(%a0)
        bra.w   guest_tools_bits_fallback
guest_tools_source_aligned:
        lsr.l   #3,%d7                  | source X byte
        mulu.w  %d2,%d3
        move.w  %d3,%d0
        andi.w  #7,%d0
        beq.s   guest_tools_width_aligned
        move.l  #12,0x0080(%a0)
        bra.w   guest_tools_bits_fallback
guest_tools_width_aligned:
        lsr.l   #3,%d3                  | width bytes

        | Resolve the current port's destination BitMap/PixMap through A5.
        movea.l (%a5),%a4
        cmpa.l  #0,%a4
        beq.w   guest_tools_bits_fallback
        movea.l %a4,%a6
        move.w  6(%a4),%d0
        andi.w  #0xc000,%d0
        cmpi.w  #0xc000,%d0
        bne.s   guest_tools_have_destination
        movea.l 2(%a4),%a6              | PixMapHandle
        cmpa.l  #0,%a6
        beq.w   guest_tools_bits_fallback
        movea.l (%a6),%a6
guest_tools_have_destination:
        move.w  4(%a6),%d4
        andi.l  #0x00003fff,%d4
        cmp.l   %d1,%d4
        beq.s   guest_tools_stride_same
        move.l  #11,0x0080(%a0)
guest_tools_stride_same:

        move.w  (%a3),%d6
        sub.w   6(%a6),%d6
        mulu.w  %d2,%d6
        move.w  %d6,%d0
        andi.w  #7,%d0
        beq.s   guest_tools_destination_aligned
        move.l  #13,0x0080(%a0)
        bra.w   guest_tools_bits_fallback
guest_tools_destination_aligned:
        lsr.l   #3,%d6                  | destination X byte

        moveq   #0,%d0
        move.w  2(%a2),%d0
        sub.w   6(%a1),%d0
        muls.w  %d1,%d0
        add.l   %d7,%d0
        add.l   (%a1),%d0               | absolute source address
        move.l  %a0,%d7
        subi.l  #acceleratorDelta,%d7   | logical slot VRAM base
        sub.l   %d7,%d0                 | source VRAM offset
        bcs.w   guest_tools_bits_fallback
        move.l  #9,0x0080(%a0)

        moveq   #0,%d7
        move.w  2(%a3),%d7
        sub.w   6(%a6),%d7
        muls.w  %d4,%d7
        add.l   %d6,%d7
        add.l   (%a6),%d7               | absolute destination address
        move.l  %a0,%d6
        subi.l  #acceleratorDelta,%d6
        sub.l   %d6,%d7                 | destination VRAM offset
        bcs.w   guest_tools_bits_fallback
        move.l  #10,0x0080(%a0)

        move.l  %d0,0x0040(%a0)
        move.l  %d7,0x0044(%a0)
        move.l  %d1,0x0048(%a0)
        move.l  %d1,0x0060(%a0)         | source stride
        move.l  %d4,0x0064(%a0)         | destination stride
        move.l  %d3,0x004c(%a0)
        moveq   #0,%d6
        cmp.l   %d0,%d7
        bls.s   guest_tools_copy_forward
        moveq   #1,%d6                  | memmove-style backwards copy
guest_tools_copy_forward:
        moveq   #0,%d0
        move.w  %d5,%d0
        move.l  %d0,0x0050(%a0)
        move.l  %d6,0x0054(%a0)
        move.l  #1,0x0058(%a0)          | commandVramCopy
        btst    #2,0x000f(%a0)           | rejected commands use software path
        bne.w   guest_tools_bits_fallback
        move.l  #14,0x0080(%a0)
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts

        | Conservative 1-bit VRAM source to indexed color VRAM expansion.
guest_tools_mono_expand:
        move.l  #15,0x0080(%a0)
        move.w  4(%a2),%d3
        sub.w   (%a2),%d3               | width pixels
        move.w  4(%a3),%d4
        sub.w   (%a3),%d4
        cmp.w   %d3,%d4
        bne.w   guest_tools_bits_fallback
        move.w  6(%a2),%d5
        sub.w   2(%a2),%d5              | height
        move.w  6(%a3),%d6
        sub.w   2(%a3),%d6
        cmp.w   %d5,%d6
        bne.w   guest_tools_bits_fallback
        tst.w   %d3
        ble.w   guest_tools_bits_fallback
        tst.w   %d5
        ble.w   guest_tools_bits_fallback

        movea.l (%a5),%a4               | color destination port only
        cmpa.l  #0,%a4
        beq.w   guest_tools_bits_fallback
        move.w  6(%a4),%d0
        andi.w  #0xc000,%d0
        cmpi.w  #0xc000,%d0
        bne.w   guest_tools_bits_fallback
        movea.l 2(%a4),%a6              | PixMapHandle
        cmpa.l  #0,%a6
        beq.w   guest_tools_bits_fallback
        movea.l (%a6),%a6
        moveq   #0,%d2
        move.w  32(%a6),%d2             | destination depth
        cmpi.w  #1,%d2
        beq.s   guest_tools_mono_depth_ok
        cmpi.w  #2,%d2
        beq.s   guest_tools_mono_depth_ok
        cmpi.w  #4,%d2
        beq.s   guest_tools_mono_depth_ok
        cmpi.w  #8,%d2
        bne.w   guest_tools_bits_fallback
guest_tools_mono_depth_ok:
        move.l  #21,0x0080(%a0)         | mono destination resolved
        moveq   #0,%d4
        move.w  4(%a6),%d4
        andi.l  #0x00003fff,%d4          | destination stride

        moveq   #0,%d6
        move.w  (%a2),%d6
        sub.w   6(%a1),%d6              | source bit position
        move.l  %d6,%d7
        andi.l  #7,%d7                  | source bit offset
        lsr.l   #3,%d6                  | source byte X
        moveq   #0,%d0
        move.w  2(%a2),%d0
        sub.w   6(%a1),%d0
        muls.w  %d1,%d0
        add.l   %d6,%d0
        add.l   (%a1),%d0
        move.l  %a0,%d6
        subi.l  #acceleratorDelta,%d6
        sub.l   %d6,%d0
        bcs.w   guest_tools_bits_fallback
        move.l  #22,0x0080(%a0)         | mono source is card VRAM

        moveq   #0,%d6
        move.w  (%a3),%d6
        sub.w   6(%a6),%d6
        mulu.w  %d2,%d6
        move.w  %d6,%d7
        andi.w  #7,%d7                  | destination must start on byte
        bne.w   guest_tools_bits_fallback
        lsr.l   #3,%d6
        moveq   #0,%d7
        move.w  2(%a3),%d7
        sub.w   6(%a6),%d7
        muls.w  %d4,%d7
        add.l   %d6,%d7
        add.l   (%a6),%d7
        move.l  %a0,%d6
        subi.l  #acceleratorDelta,%d6
        sub.l   %d6,%d7
        bcs.w   guest_tools_bits_fallback
        move.l  #10,0x0080(%a0)         | destination is card VRAM
        move.l  %d0,0x0040(%a0)
        move.l  %d7,0x0044(%a0)

        | Only exact black/white RGB endpoint mappings are normalized.
        moveq   #0,%d6                  | foreground white by default
        tst.l   36(%a4)
        bne.s   guest_tools_mono_fg_white
        tst.w   40(%a4)
        bne.s   guest_tools_mono_fg_white
        moveq   #1,%d6
        lsl.l   %d2,%d6
        subq.l  #1,%d6                  | foreground black = max index
        bra.s   guest_tools_mono_fg_done
guest_tools_mono_fg_white:
        cmpi.l  #-1,36(%a4)
        bne.w   guest_tools_bits_fallback
        cmpi.w  #-1,40(%a4)
        bne.w   guest_tools_bits_fallback
guest_tools_mono_fg_done:
        moveq   #0,%d2                  | background white by default
        tst.l   42(%a4)
        bne.s   guest_tools_mono_bg_white
        tst.w   46(%a4)
        bne.s   guest_tools_mono_bg_white
        moveq   #1,%d2
        move.w  32(%a6),%d7
        lsl.l   %d7,%d2
        subq.l  #1,%d2
        bra.s   guest_tools_mono_colors_done
guest_tools_mono_bg_white:
        cmpi.l  #-1,42(%a4)
        bne.w   guest_tools_bits_fallback
        cmpi.w  #-1,46(%a4)
        bne.w   guest_tools_bits_fallback
guest_tools_mono_colors_done:
        move.l  %d1,0x0060(%a0)
        move.l  %d4,0x0064(%a0)
        | Recompute the source bit offset after D7 was reused.
        moveq   #0,%d7
        move.w  (%a2),%d7
        sub.w   6(%a1),%d7
        andi.l  #7,%d7
        move.l  %d7,0x006c(%a0)
        move.l  %d3,0x007c(%a0)
        moveq   #0,%d7
        move.w  %d5,%d7
        move.l  %d7,0x0050(%a0)
        moveq   #0,%d7
        move.w  32(%a6),%d7
        move.l  %d7,0x0078(%a0)
        move.l  %d6,0x0070(%a0)
        move.l  %d2,0x0074(%a0)
        clr.l   0x0054(%a0)
        move.l  #3,0x0058(%a0)
        btst    #2,0x000f(%a0)
        bne.w   guest_tools_bits_fallback
        move.l  #16,0x0080(%a0)
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts

        | rectProc(verb, rect): accelerate only unclipped solid black/white
        | paint and erase operations on indexed color ports.
guest_tools_rect_proc:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        lea     guest_tools_card_base(%pc),%a0
        movea.l (%a0),%a0
        movea.l (%a5),%a4
        cmpa.l  #0,%a0
        beq.w   guest_tools_rect_fallback
        move.l  #17,0x0080(%a0)         | trace: rectProc call
        cmpa.l  #0,%a4
        beq.w   guest_tools_rect_fallback
        move.w  6(%a4),%d0
        andi.w  #0xc000,%d0
        cmpi.w  #0xc000,%d0
        bne.w   guest_tools_rect_fallback
        move.w  68(%sp),%d0             | GrafVerb
        cmpi.w  #1,%d0                  | paint
        beq.s   guest_tools_rect_paint
        cmpi.w  #2,%d0                  | erase
        beq.s   guest_tools_rect_erase
        move.l  #20,0x0080(%a0)
        bra.w   guest_tools_rect_fallback
guest_tools_rect_erase:
        move.l  #19,0x0080(%a0)
        movea.l 32(%a4),%a1             | bkPixPat
        lea     42(%a4),%a3             | rgbBkColor
        bra.s   guest_tools_rect_pattern
guest_tools_rect_paint:
        move.l  #18,0x0080(%a0)
        movea.l 58(%a4),%a1             | pnPixPat
        lea     36(%a4),%a3             | rgbFgColor
guest_tools_rect_pattern:
        cmpa.l  #0,%a1
        beq.w   guest_tools_rect_fallback
        movea.l (%a1),%a1
        cmpa.l  #0,%a1
        beq.w   guest_tools_rect_fallback
        cmpi.l  #-1,22(%a1)             | solid old-style PixPat
        bne.w   guest_tools_rect_fallback
        cmpi.l  #-1,26(%a1)
        bne.w   guest_tools_rect_fallback
        movea.l 64(%sp),%a2             | requested Rect

        | Both visibility and clipping regions must be simple rectangles that
        | completely contain the operation.
        movea.l 24(%a4),%a1
        bsr.w   guest_tools_rect_region_contains
        bne.w   guest_tools_rect_fallback
        movea.l 28(%a4),%a1
        bsr.w   guest_tools_rect_region_contains
        bne.w   guest_tools_rect_fallback

        moveq   #0,%d7                  | white endpoint byte
        tst.l   (%a3)
        bne.s   guest_tools_rect_white
        tst.w   4(%a3)
        bne.s   guest_tools_rect_white
        moveq   #-1,%d7                 | black endpoint byte
        bra.s   guest_tools_rect_color_ok
guest_tools_rect_white:
        cmpi.l  #-1,(%a3)
        bne.w   guest_tools_rect_fallback
        cmpi.w  #-1,4(%a3)
        bne.w   guest_tools_rect_fallback
guest_tools_rect_color_ok:
        movea.l 2(%a4),%a6              | destination PixMapHandle
        cmpa.l  #0,%a6
        beq.w   guest_tools_rect_fallback
        movea.l (%a6),%a6
        moveq   #0,%d1
        move.w  4(%a6),%d1
        andi.l  #0x00003fff,%d1          | destination stride
        moveq   #0,%d2
        move.w  32(%a6),%d2
        cmpi.w  #1,%d2
        beq.s   guest_tools_rect_depth_ok
        cmpi.w  #2,%d2
        beq.s   guest_tools_rect_depth_ok
        cmpi.w  #4,%d2
        beq.s   guest_tools_rect_depth_ok
        cmpi.w  #8,%d2
        bne.w   guest_tools_rect_fallback
guest_tools_rect_depth_ok:
        moveq   #0,%d3
        move.w  4(%a2),%d3
        sub.w   (%a2),%d3               | width pixels
        mulu.w  %d2,%d3
        move.w  %d3,%d0
        andi.w  #7,%d0
        bne.w   guest_tools_rect_fallback
        lsr.l   #3,%d3                  | width bytes
        moveq   #0,%d4
        move.w  6(%a2),%d4
        sub.w   2(%a2),%d4              | height
        tst.l   %d3
        beq.w   guest_tools_rect_fallback
        tst.w   %d4
        ble.w   guest_tools_rect_fallback
        moveq   #0,%d5
        move.w  (%a2),%d5
        sub.w   6(%a6),%d5
        mulu.w  %d2,%d5
        move.w  %d5,%d0
        andi.w  #7,%d0
        bne.w   guest_tools_rect_fallback
        lsr.l   #3,%d5
        moveq   #0,%d6
        move.w  2(%a2),%d6
        sub.w   6(%a6),%d6
        muls.w  %d1,%d6
        add.l   %d5,%d6
        add.l   (%a6),%d6
        move.l  %a0,%d5
        subi.l  #acceleratorDelta,%d5
        sub.l   %d5,%d6
        bcs.w   guest_tools_rect_fallback
        move.l  %d6,0x0044(%a0)
        move.l  %d1,0x0064(%a0)
        move.l  %d3,0x004c(%a0)
        move.l  %d4,0x0050(%a0)
        andi.l  #0xff,%d7
        move.l  %d7,0x0068(%a0)
        clr.l   0x0054(%a0)
        move.l  #2,0x0058(%a0)
        btst    #2,0x000f(%a0)
        bne.w   guest_tools_rect_fallback
        move.l  #23,0x0080(%a0)
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts

guest_tools_rect_region_contains:
        cmpa.l  #0,%a1
        bne.s   guest_tools_rect_region_handle
        moveq   #1,%d0
        rts
guest_tools_rect_region_handle:
        movea.l (%a1),%a1
        cmpa.l  #0,%a1
        beq.s   guest_tools_rect_region_bad
        cmpi.w  #10,(%a1)
        bne.s   guest_tools_rect_region_bad
        move.w  (%a2),%d0
        cmp.w   4(%a1),%d0              | left >= bbox.left
        blt.s   guest_tools_rect_region_bad
        move.w  2(%a2),%d0
        cmp.w   2(%a1),%d0              | top >= bbox.top
        blt.s   guest_tools_rect_region_bad
        move.w  4(%a2),%d0
        cmp.w   8(%a1),%d0              | right <= bbox.right
        bgt.s   guest_tools_rect_region_bad
        move.w  6(%a2),%d0
        cmp.w   6(%a1),%d0              | bottom <= bbox.bottom
        bgt.s   guest_tools_rect_region_bad
        moveq   #0,%d0
        rts
guest_tools_rect_region_bad:
        moveq   #1,%d0
        rts

guest_tools_rect_fallback:
        cmpa.l  #0,%a0
        beq.s   guest_tools_rect_choose_original
        move.b  #4,0x005f(%a0)
guest_tools_rect_choose_original:
        movea.l (%a5),%a1
        move.w  6(%a1),%d0
        andi.w  #0xc000,%d0
        cmpi.w  #0xc000,%d0
        bne.s   guest_tools_rect_use_mono
        lea     guest_tools_old_color_rect_proc(%pc),%a1
        movea.l (%a1),%a1
        bra.s   guest_tools_rect_chain
guest_tools_rect_use_mono:
        lea     guest_tools_old_rect_proc(%pc),%a1
        movea.l (%a1),%a1
guest_tools_rect_chain:
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        jmp     (%a1)

guest_tools_bits_fallback:
        move.b  #4,0x005f(%a0)          | controlRecordFallback
        | Prefer the color routine when the current port is a CGrafPort.
        movea.l (%a5),%a1
        moveq   #0,%d0
        cmpa.l  #0,%a1
        beq.s   guest_tools_use_mono_bits
        move.w  6(%a1),%d0
        andi.w  #0xc000,%d0
        cmpi.w  #0xc000,%d0
        bne.s   guest_tools_use_mono_bits
        lea     guest_tools_old_color_bits_proc(%pc),%a1
        movea.l (%a1),%a1
        cmpa.l  #0,%a1
        bne.s   guest_tools_chain_bits
guest_tools_use_mono_bits:
        lea     guest_tools_old_bits_proc(%pc),%a1
        movea.l (%a1),%a1
guest_tools_chain_bits:
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        jmp     (%a1)

guest_tools_old_open_port:       .long 0
guest_tools_old_open_cport:      .long 0
guest_tools_old_set_std_procs:   .long 0
guest_tools_old_set_std_cprocs:  .long 0
guest_tools_old_set_port:        .long 0
guest_tools_old_get_next_event:  .long 0
guest_tools_old_wait_next_event: .long 0
guest_tools_old_bits_proc:       .long 0
guest_tools_old_color_bits_proc: .long 0
guest_tools_old_rect_proc:       .long 0
guest_tools_old_color_rect_proc: .long 0
guest_tools_card_base:           .long 0
guest_tools_active_port_attached:.byte 0

        .even
guest_tools_qd_procs:            .space 13*4
guest_tools_cqd_procs:           .space 19*4
