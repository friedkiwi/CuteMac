        | System 6 desk accessory for CuteMac Guest Tools.
        .text
        .org    0
guest_tools_da:
        .word   0x4400                  | dNeedLock | dCtlEnable
        .word   0                       | delay
        .word   0x0140                  | updateEvt | activateEvt
        .word   0                       | menu ID
        .word   guest_tools_da_open-guest_tools_da
        .word   guest_tools_da_prime-guest_tools_da
        .word   guest_tools_da_control-guest_tools_da
        .word   guest_tools_da_status-guest_tools_da
        .word   guest_tools_da_close-guest_tools_da
        .byte   20
        .ascii  ".CuteMac Guest Tools"
        .even

guest_tools_da_open:
        movem.l %d1-%d7/%a0/%a2-%a6,-(%sp)
        movea.l %a1,%a4                 | preserve DCE across Toolbox calls
        tst.l   30(%a4)
        bne.s   guest_tools_da_open_done
        clr.l   -(%sp)                  | WindowPtr result
        clr.l   -(%sp)                  | allocate WindowRecord
        pea     guest_tools_da_bounds(%pc)
        pea     guest_tools_da_title(%pc)
        move.w  #1,-(%sp)               | visible
        clr.w   -(%sp)                  | documentProc
        move.l  #-1,-(%sp)              | in front of all windows
        move.w  #1,-(%sp)               | close box
        clr.l   -(%sp)                  | refCon
        .word   0xa913                  | _NewWindow
        movea.l (%sp)+,%a0
        move.l  %a0,30(%a4)             | dCtlWindow
        cmpa.l  #0,%a0
        beq.s   guest_tools_da_open_error
        move.w  24(%a4),108(%a0)        | windowKind = negative DA refNum
guest_tools_da_open_done:
        moveq   #0,%d0
        bra.s   guest_tools_da_open_exit
guest_tools_da_open_error:
        moveq   #-108,%d0               | memFullErr-style failure
guest_tools_da_open_exit:
        movem.l (%sp)+,%d1-%d7/%a0/%a2-%a6
        rts

guest_tools_da_prime:
        moveq   #-1,%d0
        rts

guest_tools_da_control:
        cmpi.w  #64,26(%a0)             | accEvent
        bne.s   guest_tools_da_control_done
        movea.l 28(%a0),%a2             | EventRecord pointer
        cmpa.l  #0,%a2
        beq.s   guest_tools_da_control_done
        cmpi.w  #6,(%a2)                | updateEvt
        bne.s   guest_tools_da_control_done
        movem.l %d1-%d7/%a0/%a2-%a6,-(%sp)
        movea.l 30(%a1),%a4
        cmpa.l  #0,%a4
        beq.s   guest_tools_da_draw_done
        move.l  %a4,-(%sp)
        .word   0xa873                  | _SetPort
        move.l  %a4,-(%sp)
        .word   0xa922                  | _BeginUpdate
        bsr.w   guest_tools_da_draw
        move.l  %a4,-(%sp)
        .word   0xa923                  | _EndUpdate
guest_tools_da_draw_done:
        movem.l (%sp)+,%d1-%d7/%a0/%a2-%a6
guest_tools_da_control_done:
        moveq   #0,%d0
        rts

guest_tools_da_draw:
        lea     guest_tools_da_heading(%pc),%a0
        moveq   #24,%d0
        bsr.s   guest_tools_da_draw_line

        cmpi.l  #0x01000000,0x02ae      | avoid NuBus probes on compact Macs
        blo.s   guest_tools_da_no_card
        movea.l #0x009a0000,%a2
        moveq   #5,%d1
guest_tools_da_find_card:
        cmpi.l  #0x43564131,(%a2)
        beq.s   guest_tools_da_have_card
        adda.l  #0x00100000,%a2
        dbra    %d1,guest_tools_da_find_card
guest_tools_da_no_card:
        lea     guest_tools_da_absent(%pc),%a0
        moveq   #48,%d0
        bsr.s   guest_tools_da_draw_line
        lea     guest_tools_da_inactive(%pc),%a0
        moveq   #66,%d0
        bra.s   guest_tools_da_draw_line

guest_tools_da_have_card:
        lea     guest_tools_da_present(%pc),%a0
        moveq   #48,%d0
        bsr.s   guest_tools_da_draw_line
        move.l  0x002c(%a2),%d1
        cmpi.l  #0x43564754,%d1         | 'CVGT'
        bne.s   guest_tools_da_card_inactive
        lea     guest_tools_da_loaded(%pc),%a0
        moveq   #66,%d0
        bsr.s   guest_tools_da_draw_line
        tst.l   0x001c(%a2)             | fallback count records hook calls
        beq.s   guest_tools_da_no_calls
        lea     guest_tools_da_calls(%pc),%a0
        moveq   #84,%d0
        bra.s   guest_tools_da_draw_line
guest_tools_da_no_calls:
        lea     guest_tools_da_no_calls_text(%pc),%a0
        moveq   #84,%d0
        bra.s   guest_tools_da_draw_line
guest_tools_da_card_inactive:
        lea     guest_tools_da_inactive(%pc),%a0
        moveq   #66,%d0

guest_tools_da_draw_line:
        move.w  #16,-(%sp)              | h
        move.w  %d0,-(%sp)              | v
        .word   0xa893                  | _MoveTo
        move.l  %a0,-(%sp)
        .word   0xa884                  | _DrawString
        rts

guest_tools_da_status:
        moveq   #-18,%d0
        rts

guest_tools_da_close:
        movea.l 30(%a1),%a0
        cmpa.l  #0,%a0
        beq.s   guest_tools_da_close_done
        clr.l   30(%a1)
        move.l  %a0,-(%sp)
        .word   0xa914                  | _DisposeWindow
guest_tools_da_close_done:
        moveq   #0,%d0
        rts

        .even
guest_tools_da_bounds:
        .word   50,50,160,390
guest_tools_da_title:
        .byte   19
        .ascii  "CuteMac Guest Tools"
guest_tools_da_heading:
        .byte   26
        .ascii  "CuteMac Guest Tools Status"
guest_tools_da_present:
        .byte   31
        .ascii  "Accelerated video card: present"
guest_tools_da_absent:
        .byte   33
        .ascii  "Accelerated video card: not found"
guest_tools_da_loaded:
        .byte   23
        .ascii  "Guest INIT: loaded (v1)"
guest_tools_da_inactive:
        .byte   20
        .ascii  "Guest INIT: inactive"
guest_tools_da_calls:
        .byte   25
        .ascii  "QuickDraw hook calls: yes"
guest_tools_da_no_calls_text:
        .byte   24
        .ascii  "QuickDraw hook calls: no"
        .even
