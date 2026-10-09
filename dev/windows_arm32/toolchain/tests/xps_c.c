#define COBJMACROS
#include <XpsObjectModel.h>

HRESULT call(IXpsOMThumbnailGenerator* generator, IXpsOMPage* page,
             IOpcPartUri* name, IXpsOMImageResource** image) {
  return IXpsOMThumbnailGenerator_GenerateThumbnail(
      generator, page, XPS_IMAGE_TYPE_PNG, XPS_THUMBNAIL_SIZE_SMALL, name, image);
}
