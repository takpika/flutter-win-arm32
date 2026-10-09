#pragma once
#include_next <guiddef.h>
#if defined(__cplusplus) && defined(__clang__) && (USE___UUIDOF == 0)
// MinGW keeps explicit SDK UUID specializations. For attributed application
// classes, use Clang's native __declspec(uuid) metadata as the primary template.
#pragma push_macro("__uuidof")
#undef __uuidof
extern "C++" {
template<typename T>
#if __cpp_constexpr >= 200704l && __cpp_inline_variables >= 201606L
constexpr
#else
inline
#endif
const GUID& __mingw_uuidof() { return __uuidof(T); }
}
#pragma pop_macro("__uuidof")
#endif
