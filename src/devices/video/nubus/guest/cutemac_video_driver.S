        | Project-owned position-independent CuteMac Video declaration-ROM driver.
        | CMake assembles this with Retro68 and generates the C++ byte array
        | consumed by CuteMacVideoCard.
        .text
        .org 0
driver_start:
        .word   0x4c00
        .word   0
        .long   0
        .word   open-driver_start
        .word   prime-driver_start
        .word   control-driver_start
        .word   status-driver_start
        .word   close-driver_start
        .byte   22
        .asciz  "Display_CuteMac_Video"
        .even

open:
        lea     driver_storage(%pc),%a0 | locked with this loaded DRVR instance
        clr.l   (%a0)
        clr.l   4(%a0)
        clr.l   8(%a0)
        clr.l   12(%a0)
        clr.w   16(%a0)
        clr.w   18(%a0)
        move.l  %a0,20(%a1)            | dCtlStorage
        move.w  #6,4(%a0)              | sIQType
        clr.w   6(%a0)                 | queue priority
        lea     slot_interrupt(%pc),%a2
        move.l  %a2,8(%a0)             | SQAddr
        move.l  %a1,12(%a0)            | SQParm: DCE pointer
        bsr.w   enable_interrupts
        beq.s   open_done
        clr.l   20(%a1)
        rts
open_done:
        moveq   #0,%d0
        rts
prime:
        moveq   #-1,%d0
        bra.w   complete
close:
        bsr.w   disable_interrupts
        clr.l   20(%a1)
        moveq   #0,%d0
        rts

control:
        move.w  26(%a0),%d0
        cmpi.w  #2,%d0
        beq.w   set_mode
        cmpi.w  #10,%d0               | SwitchMode uses VDSwitchInfo
        beq.w   switch_mode
        cmpi.w  #3,%d0
        beq.w   set_entries
        cmpi.w  #5,%d0                | GrayPage / GrayScreen
        beq.w   gray_screen
        cmpi.w  #6,%d0
        beq.w   set_gray
        cmpi.w  #7,%d0
        beq.w   set_interrupt
        cmpi.w  #26,%d0                | CuteMac: install guest helpers
        beq.w   install_guest_helpers
        moveq   #0,%d0
        bra.w   complete

set_mode:
        movea.l 28(%a0),%a3            | csParam points to VDSwitchInfo
        tst.l   %a3
        beq.w   param_error
        tst.w   6(%a3)                 | only page zero is supported
        bne.w   param_error
set_mode_common:
        move.w  (%a3),%d1
        subi.w  #0x80,%d1
        cmpi.w  #5,%d1
        bhi.w   param_error
        bsr.w   framebuffer_base
        move.l  %a2,8(%a3)             | csBaseAddr
        bsr.w   card_base
        adda.l  #0x000e0000,%a2
        move.b  %d1,(%a2)
        moveq   #0,%d0
        bra.w   complete

switch_mode:
        movea.l 28(%a0),%a3            | csParam points to VDSwitchInfo
        tst.l   %a3
        beq.w   param_error
        tst.w   6(%a3)                 | only page zero is supported
        bne.w   param_error
        moveq   #0,%d0
        move.b  41(%a1),%d0            | functional sResource ID
        cmp.l   2(%a3),%d0             | fixed-resolution card has one timing
        bne.w   param_error
        bra.w   set_mode_common

gray_screen:
        bsr.w   card_base
        adda.l  #0x000e0006,%a2
        clr.b   (%a2)
        moveq   #0,%d0
        bra.w   complete

set_entries:
        movea.l 28(%a0),%a3            | csParam points to VDSetEntryRecord
        tst.l   %a3
        beq.w   param_error
        movea.l (%a3),%a3
        movea.l 28(%a0),%a2
        move.w  4(%a2),%d3
        move.w  6(%a2),%d2
        tst.l   %a3
        beq.w   param_error
        cmpi.w  #255,%d2
        bhi.w   param_error
        | Video Manager passes csCount as the zero-based last record number.
        | A value of zero therefore still describes one ColorSpec.
        bsr.w   card_base
        adda.l  #0x000e0002,%a2
set_entries_loop:
        cmpi.w  #-1,%d3
        bne.s   set_entries_sequential
        move.w  (%a3),%d1
        bra.s   set_entries_index_ready
set_entries_sequential:
        move.w  %d3,%d1
        addq.w  #1,%d3
set_entries_index_ready:
        andi.w  #0x00ff,%d1            | ColorSpec.value may carry flags above the CLUT index
        tst.w   %d1
        beq.s   set_entries_next        | preserve logical white
        cmpi.w  #255,%d1
        beq.s   set_entries_next        | preserve eight-bit logical black
        cmp.w   %d2,%d1
        bne.s   set_entries_program
        cmpi.w  #15,%d2
        beq.s   set_entries_next        | preserve four-bit logical black
        cmpi.w  #3,%d2
        beq.s   set_entries_next        | preserve two-bit logical black
        cmpi.w  #1,%d2
        beq.s   set_entries_next        | preserve one-bit logical black
set_entries_program:
        move.b  %d1,(%a2)
        | System 6 supplies either a colour or grayscale table. Program its
        | RGB values verbatim: SetGray may be ordered after SetEntries.
        move.b  2(%a3),1(%a2)
        move.b  4(%a3),2(%a2)
        move.b  6(%a3),3(%a2)
set_entries_next:
        adda.w  #8,%a3
        dbra    %d2,set_entries_loop
        moveq   #0,%d0
        bra.w   complete

set_gray:
        movea.l 28(%a0),%a3            | csParam points to VDGrayRecord
        tst.l   %a3
        beq.w   param_error
        movea.l 20(%a1),%a2
        tst.l   %a2
        beq.w   param_error
        tst.b   (%a3)                   | 0 = monochrome, 1 = colour on System 6
        seq     %d0
        move.b  %d0,19(%a2)            | private flag is nonzero for luminance mapping
        moveq   #0,%d0
        bra.w   complete

set_interrupt:
        movea.l 28(%a0),%a3            | csParam points to VDFlagRecord
        tst.l   %a3
        beq.w   param_error
        move.l  %a0,-(%sp)
        tst.b   (%a3)
        bne.s   set_interrupt_disable
        bsr.w   enable_interrupts
        bra.s   set_interrupt_done
set_interrupt_disable:
        bsr.w   disable_interrupts
set_interrupt_done:
        movea.l (%sp)+,%a0
        bra.w   complete

enable_interrupts:
        movea.l 20(%a1),%a0
        tst.l   %a0
        beq.s   interrupt_storage_error
        tst.b   16(%a0)
        bne.s   interrupt_enable_hardware
        moveq   #0,%d0
        move.b  40(%a1),%d0            | dCtlSlot
        move.l  %a1,-(%sp)             | Slot Manager traps do not preserve the DCE
        .word   0xa075                  | _SIntInstall
        movea.l (%sp)+,%a1
        tst.w   %d0
        bne.s   interrupt_done
        movea.l 20(%a1),%a0
        st      16(%a0)
interrupt_enable_hardware:
        bsr.w   card_base
        adda.l  #0x000e0001,%a2
        clr.b   (%a2)                   | enable and acknowledge VBL
        moveq   #0,%d0
interrupt_done:
        rts
interrupt_storage_error:
        moveq   #-50,%d0
        rts

disable_interrupts:
        bsr.w   card_base
        adda.l  #0x000e0001,%a2
        move.b  #1,(%a2)
        movea.l 20(%a1),%a0
        tst.l   %a0
        beq.s   interrupt_disabled
        tst.b   16(%a0)
        beq.s   interrupt_disabled
        moveq   #0,%d0
        move.b  40(%a1),%d0
        move.l  %a1,-(%sp)
        .word   0xa076                  | _SIntRemove
        movea.l (%sp)+,%a1
        movea.l 20(%a1),%a0
        clr.b   16(%a0)
interrupt_disabled:
        moveq   #0,%d0
        rts

slot_interrupt:
        bsr.s   card_base
        adda.l  #0x000e0001,%a2
        clr.b   (%a2)                   | acknowledge while remaining enabled
        bsr.s   card_base
        adda.l  #0x000e1018,%a2
        btst    #0,(%a2)                | host absolute pointer is valid
        beq.s   slot_cursor_task
        move.b  1(%a2),%d1
        movea.l 20(%a1),%a0
        cmp.b   18(%a0),%d1
        beq.s   slot_cursor_task
        move.b  %d1,18(%a0)
        move.w  4(%a2),0x0828           | MTemp vertical
        move.w  2(%a2),0x082a           | MTemp horizontal
        move.w  4(%a2),0x082c           | RawMouse vertical
        move.w  2(%a2),0x082e           | RawMouse horizontal
        move.w  4(%a2),0x0830           | Mouse vertical
        move.w  2(%a2),0x0832           | Mouse horizontal
        st      0x08ce                   | CrsrNew
slot_cursor_task:
        moveq   #0,%d0
        move.b  40(%a1),%d0
        movea.l 0x0d28,%a0             | JVBLTask
        jsr     (%a0)
        moveq   #1,%d0                 | interrupt serviced
        rts

card_base:
        | Slot Manager supplies an address valid in the current addressing
        | mode. Reconstructing $s00000 from dCtlSlot works in 24-bit mode but
        | stops reaching the card after MODE32 switches to 32-bit addressing.
        movea.l 42(%a1),%a2            | AuxDCE.dCtlDevBase
        rts

framebuffer_base:
        suba.l  %a2,%a2                | page-zero offset; Slot Manager supplies the slot base
        rts

install_guest_helpers:
        lea     service_base(%pc),%a2
        move.l  %a2,-(%sp)
        bsr.w   card_base
        movea.l (%sp)+,%a0
        move.l  %a2,(%a0)
        movea.l %a0,%a2
        tst.b   4(%a2)
        bne.s   helpers_installed
        st      4(%a2)
        pea     shutdown_callback(%pc)
        move.w  #3,-(%sp)              | sdOnPowerOff | sdOnRestart
        move.w  #3,-(%sp)              | ShutDwnInstall selector
        .word   0xa895                  | _Shutdown
helpers_installed:
        moveq   #0,%d0
        bra.w   complete

param_error:
        moveq   #-50,%d0
        bra.w   complete

status:
        move.w  26(%a0),%d0
        cmpi.w  #2,%d0
        beq.w   get_mode
        cmpi.w  #10,%d0                | GetCurMode fills VDSwitchInfo
        beq.w   get_current_mode
        cmpi.w  #3,%d0
        beq.w   get_entries
        cmpi.w  #4,%d0
        beq.w   get_pages
        cmpi.w  #5,%d0
        beq.w   get_base
        cmpi.w  #6,%d0
        beq.w   get_gray
        cmpi.w  #7,%d0
        beq.w   get_interrupt
        moveq   #-18,%d0
        bra.w   complete

get_mode:
        movea.l 28(%a0),%a3            | csParam points to VDSwitchInfo
        tst.l   %a3
        beq.w   param_error
        bsr.w   framebuffer_base
        move.l  %a2,8(%a3)
        bsr.w   card_base
        adda.l  #0x000e0000,%a2
        moveq   #0,%d1
        move.b  (%a2),%d1
        andi.w  #7,%d1
        addi.w  #0x80,%d1
        move.w  %d1,(%a3)
        clr.w   6(%a3)
        moveq   #0,%d0
        bra.w   complete

get_current_mode:
        movea.l 28(%a0),%a3
        tst.l   %a3
        beq.w   param_error
        bsr.w   framebuffer_base
        move.l  %a2,8(%a3)             | csBaseAddr
        moveq   #0,%d1
        move.b  41(%a1),%d1
        move.l  %d1,2(%a3)             | csData: functional sResource ID
        clr.w   6(%a3)                 | csPage
        clr.l   12(%a3)                | csReserved
        bsr.w   card_base
        adda.l  #0x000e0000,%a2
        moveq   #0,%d1
        move.b  (%a2),%d1
        andi.w  #7,%d1
        addi.w  #0x80,%d1
        move.w  %d1,(%a3)
        moveq   #0,%d0
        bra.w   complete

get_entries:
        movea.l 28(%a0),%a3            | csParam points to VDSetEntryRecord
        tst.l   %a3
        beq.w   param_error
        movea.l (%a3),%a3
        movea.l 28(%a0),%a2
        move.w  4(%a2),%d3
        move.w  6(%a2),%d2
        tst.l   %a3
        beq.w   param_error
        cmpi.w  #255,%d2
        bhi.w   param_error
        | As in SetEntries, csCount is zero-based and can be used directly
        | as DBRA's terminal counter.
        bsr.w   card_base
        adda.l  #0x000e0002,%a2
get_entries_loop:
        cmpi.w  #-1,%d3
        bne.s   get_entries_sequential
        move.w  (%a3),%d1
        bra.s   get_entries_index_ready
get_entries_sequential:
        move.w  %d3,%d1
        move.w  %d1,(%a3)
        addq.w  #1,%d3
get_entries_index_ready:
        andi.w  #0x00ff,%d1
        move.b  %d1,(%a2)
        move.b  1(%a2),%d0
        move.b  %d0,2(%a3)
        move.b  %d0,3(%a3)
        move.b  2(%a2),%d0
        move.b  %d0,4(%a3)
        move.b  %d0,5(%a3)
        move.b  3(%a2),%d0
        move.b  %d0,6(%a3)
        move.b  %d0,7(%a3)
        adda.w  #8,%a3
        dbra    %d2,get_entries_loop
        moveq   #0,%d0
        bra.w   complete

get_pages:
        movea.l 28(%a0),%a3            | csParam points to VDPageInfo
        tst.l   %a3
        beq.w   param_error
        move.w  #1,6(%a3)
        moveq   #0,%d0
        bra.w   complete
get_base:
        movea.l 28(%a0),%a3            | csParam points to VDPageInfo
        tst.l   %a3
        beq.w   param_error
        tst.w   6(%a3)
        bne.w   param_error
        bsr.w   framebuffer_base
        move.l  %a2,8(%a3)
        moveq   #0,%d0
        bra.w   complete
get_gray:
        movea.l 28(%a0),%a3            | csParam points to VDGrayRecord
        tst.l   %a3
        beq.w   param_error
        movea.l 20(%a1),%a2
        tst.l   %a2
        beq.w   param_error
        moveq   #0,%d0
        tst.b   19(%a2)
        seq     %d0
        andi.b  #1,%d0
        move.b  %d0,(%a3)
        moveq   #0,%d0
        bra.w   complete
get_interrupt:
        movea.l 28(%a0),%a3            | csParam points to VDFlagRecord
        tst.l   %a3
        beq.w   param_error
        bsr.w   card_base
        adda.l  #0x000e0001,%a2
        move.b  (%a2),(%a3)
        moveq   #0,%d0

complete:
        btst    #9,6(%a0)
        beq.s   queued
        rts
queued:
        move.w  %d0,16(%a0)            | JIODone requires ioResult to leave ioInProgress
        movea.l 0x08fc,%a2
        jmp     (%a2)

shutdown_callback:
        movem.l %d0-%d1/%a0-%a1,-(%sp)
        cmpi.l  #1,%d0                 | 0 = power off, 1 = restart
        bhi.s   shutdown_done
        addq.b  #1,%d0                 | mailbox commands are 1 and 2
        movea.l service_base(%pc),%a0
        adda.l  #0x000e1010,%a0
        move.b  %d0,(%a0)
shutdown_done:
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        rts

service_base:
        .long   0
shutdown_installed:
        .byte   0
        .even
driver_storage:
        .space  20
