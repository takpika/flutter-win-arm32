#pragma once

// Keep Microsoft C++ call sites unchanged while using MinGW's COM declarations.
// This alters declaration spelling only; interface layout and C ABI stay intact.
#if defined(__MINGW32__) && defined(FLUTTER_WINDOWS_PHONE)
// DirectWrite takes pointers to this GDI type in the app API family.
typedef struct tagFONTSIGNATURE FONTSIGNATURE;
#endif

#if defined(__MINGW32__) && defined(__cplusplus) && !defined(CINTERFACE)
#pragma push_macro("GetGlyphImageFormats_")
#undef GetGlyphImageFormats_
#define GetGlyphImageFormats_ GetGlyphImageFormats
#include_next <dwrite_3.h>
#pragma pop_macro("GetGlyphImageFormats_")
#else
#include_next <dwrite_3.h>
#endif
