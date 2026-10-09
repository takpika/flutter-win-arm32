#pragma once
#include_next <synchapi.h>
#if defined(__MINGW32__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdll-attribute-on-redeclaration"
#ifdef __cplusplus
extern "C" {
#endif
// Complete MinGW's declaration with the Microsoft SDK's import attribute.
// This adaptation is confined to this native SDK declaration.
__declspec(dllimport) VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK);
#ifdef __cplusplus
}
#endif
#pragma clang diagnostic pop
#endif
