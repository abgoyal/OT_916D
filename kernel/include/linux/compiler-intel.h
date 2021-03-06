
#ifndef __LINUX_COMPILER_H
#error "Please don't include <linux/compiler-intel.h> directly, include <linux/compiler.h> instead."
#endif

#ifdef __ECC


#include <asm/intrinsics.h>

#undef barrier
#undef RELOC_HIDE

#define barrier() __memory_barrier()

#define RELOC_HIDE(ptr, off)					\
  ({ unsigned long __ptr;					\
     __ptr = (unsigned long) (ptr);				\
    (typeof(ptr)) (__ptr + (off)); })

/* Intel ECC compiler doesn't support __builtin_types_compatible_p() */
#define __must_be_array(a) 0

#endif

#define uninitialized_var(x) x
