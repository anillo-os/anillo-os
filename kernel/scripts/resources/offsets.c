#include <ferro/core/interrupts.h>
#include <ferro/core/threads.h>
#include <ferro/platform.h>

#if FERRO_ARCH == FERRO_ARCH_x86_64
farch_int_isr_frame_t frame;
#elif FERRO_ARCH == FERRO_ARCH_aarch64
farch_int_exception_frame_t frame;
#endif


fthread_saved_context_t thread_context;
