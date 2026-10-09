#include <windows.h>
#include <stddef.h>
#include <type_traits>

static_assert(std::is_same_v<decltype(&LoadLibraryExW),
                            HMODULE (WINAPI*)(LPCWSTR, HANDLE, DWORD)>);

static_assert(std::is_same_v<decltype(&SetHandleInformation),
                            WINBOOL (WINAPI*)(HANDLE, DWORD, DWORD)>);
static_assert(std::is_same_v<decltype(&CreateFileW),
                            HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD,
                                            LPSECURITY_ATTRIBUTES, DWORD,
                                            DWORD, HANDLE)>);
static_assert(std::is_same_v<decltype(&RegisterWaitForSingleObject),
                            WINBOOL (WINAPI*)(PHANDLE, HANDLE,
                                             WAITORTIMERCALLBACK, PVOID,
                                             ULONG, ULONG)>);
static_assert(std::is_same_v<decltype(&UpdateProcThreadAttribute),
                            WINBOOL (WINAPI*)(LPPROC_THREAD_ATTRIBUTE_LIST,
                                             DWORD, DWORD_PTR, PVOID, SIZE_T,
                                             PVOID, PSIZE_T)>);

extern "C" unsigned startup_size() { return sizeof(STARTUPINFOEXW); }
extern "C" unsigned attribute_offset() {
  return offsetof(STARTUPINFOEXW, lpAttributeList);
}
extern "C" unsigned handle_list_flag() { return PROC_THREAD_ATTRIBUTE_HANDLE_LIST; }
extern "C" unsigned standard_handles_flag() { return STARTF_USESTDHANDLES; }
extern "C" unsigned system_library_search_flag() { return LOAD_LIBRARY_SEARCH_SYSTEM32; }
