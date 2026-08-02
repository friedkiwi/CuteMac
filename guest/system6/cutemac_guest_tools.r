#include "Retro68.r"

type 'INIT' {
    RETRO68_CODE_TYPE
};

resource 'INIT' (128, "CuteMac Guest Tools", locked, sysheap) {
    dontBreakAtEntry, $$read("CuteMacGuestTools.flt")
};
