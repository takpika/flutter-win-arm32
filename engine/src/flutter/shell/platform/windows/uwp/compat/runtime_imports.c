// LLVM-MinGW provides ARM setjmp as a static UCRT app helper. Its headers
// declare the legacy entry point imported, so provide the import-address slot
// backed by that same implementation. No desktop CRT DLL is loaded here.
extern int _setjmp(void* buffer, void* frame);
int (*__imp__setjmp)(void* buffer, void* frame) = _setjmp;

// The Phone SDK owns legacy Windows API facade resolution. The VM's generic
// library loader and applications keep their normal API calls unchanged.
#include <windows.h>
__declspec(dllimport) HMODULE WINAPI FlutterWinRTLoadPackagedLibrary(LPCWSTR, DWORD);
HMODULE (WINAPI *__imp_LoadPackagedLibrary)(LPCWSTR, DWORD) = FlutterWinRTLoadPackagedLibrary;

// Keep Dart's CRT file deletion unchanged; brokered picker grants belong to
// the Phone SDK. The SDK DLL itself retains the operating system's CRT import.
__declspec(dllimport) int __cdecl FlutterWinRTWremove(const wchar_t*);
int (__cdecl *__imp__wremove)(const wchar_t*) = FlutterWinRTWremove;
