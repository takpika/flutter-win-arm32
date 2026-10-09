#pragma once
#include_next <sal.h>

#if defined(__clang__) && defined(__MINGW32__)
// SAL contracts required by the original ATL headers but absent in MinGW SAL.
// Retain them as Clang annotations; keep GNU CRT macros and existing SAL intact.
#ifndef _Deref_pre_z_
#define _Deref_pre_z_ __attribute__((annotate("SAL:pre:dereference:null-terminated")))
#endif
#ifndef _Deref_pre_valid_
#define _Deref_pre_valid_ __attribute__((annotate("SAL:pre:dereference:valid")))
#endif
#ifndef _Deref_post_valid_
#define _Deref_post_valid_ __attribute__((annotate("SAL:post:dereference:valid")))
#endif
#ifndef _Ret_opt_
#define _Ret_opt_ __attribute__((annotate("SAL:return:optional-valid")))
#endif
#ifndef _Deref_pre_maybenull_
#define _Deref_pre_maybenull_ __attribute__((annotate("SAL:pre:dereference:maybenull")))
#endif
#ifndef _Deref_post_maybenull_
#define _Deref_post_maybenull_ __attribute__((annotate("SAL:post:dereference:maybenull")))
#endif
#endif
