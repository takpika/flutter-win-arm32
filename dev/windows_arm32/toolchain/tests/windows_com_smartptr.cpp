#include <comdef.h>
#include <comip.h>
#include <shobjidl.h>

// The Microsoft SDK permits identical typedefs in a header and its consumer.
_COM_SMARTPTR_TYPEDEF(IFileDialog, IID_IFileDialog);
_COM_SMARTPTR_TYPEDEF(IFileDialog, IID_IFileDialog);

IFileDialog* native_com_pointer(IFileDialog* value) {
  IFileDialogPtr pointer(value);
  return pointer.GetInterfacePtr();
}
