// Upstream Windows consumers may repeat the Microsoft SDK import declaration.
#include <windows.h>
extern "C" __declspec(dllimport) VOID WINAPI
ReleaseSRWLockExclusive(PSRWLOCK lock);

void native_unlock(PSRWLOCK lock) {
  ReleaseSRWLockExclusive(lock);
}
