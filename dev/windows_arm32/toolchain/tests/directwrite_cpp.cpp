#include <dwrite_3.h>

HRESULT four(IDWriteFontFace4* face, DWRITE_GLYPH_IMAGE_FORMATS* formats) {
#if defined(WINDOWS_ARM32_SDK_ADAPTER_TEST)
  return face->GetGlyphImageFormats(1, 0, 0xffffffff, formats);
#else
  return face->GetGlyphImageFormats_(1, 0, 0xffffffff, formats);
#endif
}

DWRITE_GLYPH_IMAGE_FORMATS zero(IDWriteFontFace4* face) {
  return face->GetGlyphImageFormats();
}

#ifdef GetGlyphImageFormats_
#error SDK adapter leaked its declaration macro
#endif
