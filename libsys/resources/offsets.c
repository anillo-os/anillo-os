#include <libsys/libsys.h>
#include <calculate-offsets.h>
#include <ferro/platform.h>

OFFSETS_BEGIN;

SIZE(sys_ucs_context);

#if FERRO_ARCH == FERRO_ARCH_x86_64
OFFSET(sys_ucs_context, rip);
OFFSET(sys_ucs_context, rdi);
OFFSET(sys_ucs_context, rbx);
OFFSET(sys_ucs_context, rsp);
OFFSET(sys_ucs_context, rbp);
OFFSET(sys_ucs_context, r12);
OFFSET(sys_ucs_context, r13);
OFFSET(sys_ucs_context, r14);
OFFSET(sys_ucs_context, r15);
OFFSET(sys_ucs_context, mxcsr);
OFFSET(sys_ucs_context, x87_cw);
#elif FERRO_ARCH == FERRO_ARCH_aarch64
OFFSET(sys_ucs_context, ip);
OFFSET(sys_ucs_context, x0);
OFFSET(sys_ucs_context, x19);
OFFSET(sys_ucs_context, x20);
OFFSET(sys_ucs_context, x21);
OFFSET(sys_ucs_context, x22);
OFFSET(sys_ucs_context, x23);
OFFSET(sys_ucs_context, x24);
OFFSET(sys_ucs_context, x25);
OFFSET(sys_ucs_context, x26);
OFFSET(sys_ucs_context, x27);
OFFSET(sys_ucs_context, x28);
OFFSET(sys_ucs_context, x29);
OFFSET(sys_ucs_context, x30);
OFFSET(sys_ucs_context, sp);
OFFSET(sys_ucs_context, fpcr);
OFFSET(sys_ucs_context, fp_registers);
#endif

OFFSETS_END;
