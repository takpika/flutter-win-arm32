#pragma once
#pragma push_macro("_MSC_VER")
#undef _MSC_VER
#include_next <intrin.h>
#pragma pop_macro("_MSC_VER")

#if defined(__MINGW32__) && defined(__arm__)
// Preserve the MSVC ARM instruction intrinsic absent from the MinGW header.
static __inline__ __attribute__((__always_inline__)) void __nop(void) {
  __asm__ __volatile__("nop");
}
#endif
