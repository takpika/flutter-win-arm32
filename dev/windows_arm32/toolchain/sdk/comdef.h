#pragma once
#include_next <comdef.h>
#if defined(__clang__) && defined(__MINGW32__)
// The SDK typedef may be repeated. A generated GUID getter function cannot.
#undef _COM_SMARTPTR_TYPEDEF
#define _COM_SMARTPTR_TYPEDEF(Interface, aIID) \
  typedef _com_ptr_t<_com_IIID<Interface, &aIID>> Interface ## Ptr
#endif
