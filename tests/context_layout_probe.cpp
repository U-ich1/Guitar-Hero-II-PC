#include <cstddef>
#include <iostream>
#ifdef GH2_REX_CONTEXT
#include <rex/ppc/context.h>
#else
#include "ppc_recomp_shared.h"
#endif
int main() {
    std::cout << "{\"size\":" << sizeof(PPCContext) << ",\"alignment\":" << alignof(PPCContext);
#define FIELD(member) std::cout << ",\"" #member "\":" << offsetof(PPCContext, member)
    FIELD(r0); FIELD(r1); FIELD(r3); FIELD(r13); FIELD(r31);
    FIELD(lr); FIELD(ctr); FIELD(xer); FIELD(reserved); FIELD(cr0); FIELD(cr7);
    FIELD(fpscr); FIELD(f0); FIELD(f31); FIELD(v0); FIELD(v127);
#ifdef GH2_REX_CONTEXT
    FIELD(msr); FIELD(vscr_sat); FIELD(last_indirect_target);
#else
    FIELD(vscr);
#endif
    std::cout << "}\n";
}
