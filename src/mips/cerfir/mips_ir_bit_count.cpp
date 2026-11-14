#include "mips_ir_fns.h"

namespace cerfengine {

void MipsIrClz(MipsIrContext& c) {
    if (c.d->rd == 0u) return;
    MipsIrWriteZext(c, c.d->rd, c.b->Op1(cerfir::Op::kClz, MipsIrReadLo(c, c.d->rs)));
}

void MipsIrClo(MipsIrContext& c) {
    if (c.d->rd == 0u) return;
    MipsIrWriteZext(c, c.d->rd,
        c.b->Op1(cerfir::Op::kClz, c.b->Op1(cerfir::Op::kNot, MipsIrReadLo(c, c.d->rs))));
}

}
