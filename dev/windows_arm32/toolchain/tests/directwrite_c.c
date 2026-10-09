#define COBJMACROS
#include <dwrite_3.h>

HRESULT four(IDWriteFontFace4* face, DWRITE_GLYPH_IMAGE_FORMATS* formats) {
  return IDWriteFontFace4_GetGlyphImageFormats_(face, 1, 0, 0xffffffff, formats);
}

DWRITE_GLYPH_IMAGE_FORMATS zero(IDWriteFontFace4* face) {
  return IDWriteFontFace4_GetGlyphImageFormats(face);
}
