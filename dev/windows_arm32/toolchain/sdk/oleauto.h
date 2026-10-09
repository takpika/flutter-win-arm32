#pragma once
#include_next <oleauto.h>
#if defined(__cplusplus) && defined(__MINGW32__)
extern "C++" {
// Microsoft SDK declares VariantCopy's source const; its native ABI is unchanged.
inline HRESULT WINAPI VariantCopy(VARIANTARG* destination, const VARIANTARG* source) {
  return ::VariantCopy(destination, const_cast<VARIANTARG*>(source));
}
}
#endif
