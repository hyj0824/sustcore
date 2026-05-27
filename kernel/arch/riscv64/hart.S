#include "ctxlayout.h"

// make sure tmp is not used by caller
.macro locate_hart_context hart_id_reg, tmp
    li \tmp, HART_CONTEXT_SIZE
    mul \tmp, \hart_id_reg, \tmp
    la tp, __hart_context
    add tp, tp, \tmp
.endm

.macro set_hart_id hart_id_reg
    sd \hart_id_reg, 0(tp)
.endm