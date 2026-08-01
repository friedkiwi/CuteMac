/* Project-owned classic Mac OS NuBus video driver for CuteMac Video.
 * This source is retained as the maintainable form of the bytecode embedded
 * by CuteMacVideoCard. It uses only the documented Device Manager parameter
 * block and the card's versioned MMIO registers.
 */

        .cpu 68020
        .text
driver:
        .word   0x4c00, 0, 0, 0
        .word   open-driver
        .word   prime-driver
        .word   control-driver
        .word   status-driver
        .word   close-driver
        .byte   22
        .ascii  ".Display_CuteMac_Video"
        .even

open:
        moveq   #0,%d0
        rts
prime:
        moveq   #-1,%d0
        bra     finish
close:
        moveq   #0,%d0
        rts

control:
        move.w  26(%a0),%d0
        cmpi.w  #2,%d0                  /* cscSetMode */
        beq     set_mode
        cmpi.w  #7,%d0                  /* cscSetInterrupt */
        beq     set_interrupt
        moveq   #0,%d0                  /* palette/gamma calls are optional */
        bra     finish
set_mode:
        move.w  28(%a0),%d1
        subi.w  #0x80,%d1
        cmpi.w  #5,%d1
        bhi     parameter_error
        movea.l 42(%a1),%a2
        adda.l  #0x00800000,%a2
        move.b  %d1,(%a2)
        move.l  42(%a1),36(%a0)
        moveq   #0,%d0
        bra     finish
set_interrupt:
        movea.l 42(%a1),%a2
        adda.l  #0x00800001,%a2
        move.b  28(%a0),(%a2)
        moveq   #0,%d0
        bra     finish
parameter_error:
        moveq   #-50,%d0
        bra     finish

status:
        move.w  26(%a0),%d0
        cmpi.w  #2,%d0                  /* cscGetMode */
        beq     get_mode
        cmpi.w  #4,%d0                  /* cscGetPages */
        beq     get_pages
        cmpi.w  #5,%d0                  /* cscGetBaseAddress */
        beq     get_base
        cmpi.w  #6,%d0                  /* cscGetGray */
        beq     get_false
        cmpi.w  #7,%d0                  /* cscGetInterrupt */
        beq     get_interrupt
        moveq   #-18,%d0
        bra     finish
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
        bra     finish
get_pages:
        move.w  #1,34(%a0)
        moveq   #0,%d0
        bra     finish
get_base:
        tst.w   34(%a0)
        bne     parameter_error
        move.l  42(%a1),36(%a0)
        moveq   #0,%d0
        bra     finish
get_false:
        clr.b   28(%a0)
        moveq   #0,%d0
        bra     finish
get_interrupt:
        movea.l 42(%a1),%a2
        adda.l  #0x00800001,%a2
        move.b  (%a2),28(%a0)
        moveq   #0,%d0

finish:
        btst    #9,6(%a0)               /* noQueueBit: immediate call */
        beq     queued
        rts
queued:
        movea.l 0x08fc.w,%a2            /* JIODone */
        jmp     (%a2)
