#include <windows.applicationmodel.core.h>
#if defined(__cplusplus)
#include <type_traits>
using BooleanBox = ABI::Windows::Foundation::IReference<bool>;
using ByteBox = ABI::Windows::Foundation::IReference<BYTE>;
static_assert(!std::is_same<BooleanBox, ByteBox>::value);
static_assert(sizeof(bool) == 1 && sizeof(BYTE) == 1);
static_assert(__uuidof(BooleanBox).Data1 == 0x3c00fd60);
static_assert(__uuidof(ByteBox).Data1 == 0xe5198cc8);
HRESULT ReadBoolean(BooleanBox* box, bool* value) { return box->get_Value(value); }
HRESULT ReadByte(ByteBox* box, BYTE* value) { return box->get_Value(value); }
#else
_Static_assert(sizeof(boolean) == 1 && sizeof(BYTE) == 1, "WinRT scalar ABI");
_Static_assert(sizeof(__FIReference_1_booleanVtbl) == sizeof(__FIReference_1_BYTEVtbl),
               "WinRT box vtable ABI");
HRESULT ReadBoolean(__FIReference_1_boolean* box, boolean* value) {
  return box->lpVtbl->get_Value(box, value);
}
HRESULT ReadByte(__FIReference_1_BYTE* box, BYTE* value) {
  return box->lpVtbl->get_Value(box, value);
}
#endif
