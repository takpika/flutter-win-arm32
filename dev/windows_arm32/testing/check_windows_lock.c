/* CI-only probe of real Win32 sharing and release, not part of the SDK. */
#include <windows.h>
#include <wchar.h>

int __cdecl wmain(int argc, WCHAR **argv)
{
    HANDLE lock, proof;
    DWORD error, bytes;
    BOOL held;
    const char *result;
    DWORD size;
    if (argc != 4) return 2;
    if (!wcscmp(argv[2], L"held")) held = TRUE;
    else if (!wcscmp(argv[2], L"released")) held = FALSE;
    else return 3;
    lock = CreateFileW(argv[1], GENERIC_READ | GENERIC_WRITE, 0, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    error = GetLastError();
    if (held)
    {
        if (lock != INVALID_HANDLE_VALUE) { CloseHandle(lock); return 4; }
        if (error != ERROR_SHARING_VIOLATION) return 5;
    }
    else
    {
        if (lock == INVALID_HANDLE_VALUE) return 6;
        CloseHandle(lock);
    }
    result = held ? "held" : "released";
    size = held ? 4 : 8;
    proof = CreateFileW(argv[3], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (proof == INVALID_HANDLE_VALUE) return 7;
    if (!WriteFile(proof, result, size, &bytes, NULL) || bytes != size)
    { CloseHandle(proof); return 8; }
    CloseHandle(proof);
    return 0;
}
