        | Appended to a private copy of the compatible driver at build time.
        | Control selector 26 runs in task context after Toolbox startup.
        |
        | Install a deliberately transparent _StdBits patch first.  The hook
        | records each call through the accelerator mailbox and then chains to
        | the Toolbox routine that was installed before us.  Drawing is not
        | changed until this bootstrap has been proved under the guest OS.
accelerated_install_guest_helpers:
        movem.l %d1-%d7/%a0-%a6,-(%sp)
        bsr.w   accelerated_install_std_bits_hook
        movem.l (%sp)+,%d1-%d7/%a0-%a6
        bra.w   install_guest_helpers

accelerated_install_std_bits_hook:
        lea     accelerated_old_std_bits(%pc),%a0
        tst.l   (%a0)
        bne.s   accelerated_std_bits_install_done

        bsr.w   card_base
        lea     accelerated_card_base(%pc),%a0
        move.l  %a2,(%a0)

        move.w  #0xa8eb,%d0
        .word   0xa746                   | _GetToolTrapAddress -> A0
        lea     accelerated_old_std_bits(%pc),%a1
        move.l  %a0,(%a1)
        tst.l   %a0
        beq.s   accelerated_std_bits_install_done

        lea     accelerated_std_bits_hook(%pc),%a0
        move.w  #0xa8eb,%d0
        .word   0xa647                   | _SetToolTrapAddress

        lea     accelerated_card_base(%pc),%a2
        movea.l (%a2),%a2
        move.l  #0x43564131,0x000a002c(%a2) | guest adapter = CVA1
        move.w  0x015a,%d0               | SysVersion
        move.w  %d0,0x000a0032(%a2)
        move.l  #1,0x000a0034(%a2)       | ROM _StdBits hook version
        move.b  #2,0x000a005f(%a2)       | controlGuestAttach
accelerated_std_bits_install_done:
        rts

accelerated_std_bits_hook:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        lea     accelerated_card_base(%pc),%a0
        movea.l (%a0),%a0
        tst.l   %a0
        beq.s   accelerated_std_bits_chain
        move.b  #4,0x000a005f(%a0)       | controlRecordFallback
accelerated_std_bits_chain:
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        lea     accelerated_old_std_bits(%pc),%a0
        movea.l (%a0),%a0
        jmp     (%a0)

accelerated_old_std_bits:
        .long   0
accelerated_card_base:
        .long   0
