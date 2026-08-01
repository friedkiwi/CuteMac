        | Source for the position-independent declaration-ROM video driver.
        | Regenerate the byte array in CuteMacVideoCard.cpp with GNU binutils
        | for m68k after changing this file.
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
        moveq   #0,%d0
        rts
prime:
        moveq   #-1,%d0
        bra.w   complete
close:
        moveq   #0,%d0
        rts

control:
        move.w  26(%a0),%d0
        cmpi.w  #2,%d0
        beq.w   set_mode
        cmpi.w  #3,%d0
        beq.w   set_entries
        cmpi.w  #7,%d0
        beq.w   set_interrupt
        moveq   #0,%d0
        bra.w   complete

set_mode:
        move.w  28(%a0),%d1
        subi.w  #0x80,%d1
        cmpi.w  #5,%d1
        bhi.w   param_error
        movea.l 42(%a1),%a2
        adda.l  #0x00800000,%a2
        move.b  %d1,(%a2)
        move.l  42(%a1),36(%a0)
        moveq   #0,%d0
        bra.w   complete

set_entries:
        movea.l 28(%a0),%a3
        move.w  32(%a0),%d3
        move.w  34(%a0),%d2
        tst.l   %a3
        beq.w   param_error
        cmpi.w  #255,%d2
        bhi.w   param_error
        movea.l 42(%a1),%a2
        adda.l  #0x00800002,%a2
set_entries_loop:
        cmpi.w  #-1,%d3
        bne.s   set_entries_sequential
        move.w  (%a3),%d1
        bra.s   set_entries_index_ready
set_entries_sequential:
        move.w  %d3,%d1
        addq.w  #1,%d3
set_entries_index_ready:
        move.b  %d1,(%a2)
        move.b  2(%a3),1(%a2)
        move.b  4(%a3),2(%a2)
        move.b  6(%a3),3(%a2)
        adda.w  #8,%a3
        dbra    %d2,set_entries_loop
        moveq   #0,%d0
        bra.w   complete

set_interrupt:
        movea.l 42(%a1),%a2
        adda.l  #0x00800001,%a2
        move.b  28(%a0),(%a2)
        moveq   #0,%d0
        bra.w   complete

param_error:
        moveq   #-50,%d0
        bra.w   complete

status:
        move.w  26(%a0),%d0
        cmpi.w  #2,%d0
        beq.w   get_mode
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
        movea.l 42(%a1),%a2
        adda.l  #0x00800000,%a2
        moveq   #0,%d1
        move.b  (%a2),%d1
        andi.w  #7,%d1
        addi.w  #0x80,%d1
        move.w  %d1,28(%a0)
        clr.w   34(%a0)
        move.l  42(%a1),36(%a0)
        moveq   #0,%d0
        bra.w   complete

get_entries:
        movea.l 28(%a0),%a3
        move.w  32(%a0),%d3
        move.w  34(%a0),%d2
        tst.l   %a3
        beq.w   param_error
        cmpi.w  #255,%d2
        bhi.w   param_error
        movea.l 42(%a1),%a2
        adda.l  #0x00800002,%a2
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
        move.w  #1,34(%a0)
        moveq   #0,%d0
        bra.w   complete
get_base:
        tst.w   34(%a0)
        bne.w   param_error
        move.l  42(%a1),36(%a0)
        moveq   #0,%d0
        bra.w   complete
get_gray:
        clr.b   28(%a0)
        moveq   #0,%d0
        bra.w   complete
get_interrupt:
        movea.l 42(%a1),%a2
        adda.l  #0x00800001,%a2
        move.b  (%a2),28(%a0)
        moveq   #0,%d0

complete:
        btst    #9,6(%a0)
        beq.s   queued
        rts
queued:
        movea.l 0x08fc,%a2
        jmp     (%a2)
