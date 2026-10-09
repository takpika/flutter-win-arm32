// SDK-owned Python tool dispatch. No shell or system DLL is distributed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CAPACITY 32768

static int append(wchar_t *output, const wchar_t *input) {
  size_t used = wcslen(output), count = wcslen(input);
  if (used + count >= CAPACITY) return 0;
  memcpy(output + used, input, (count + 1) * sizeof(wchar_t));
  return 1;
}

// Quote one argument using the Windows CRT command-line parsing rules.
static int argument(wchar_t *output, const wchar_t *value) {
  if (!append(output, L" \"")) return 0;
  size_t slashes = 0;
  for (;; ++value) {
    if (*value == L'\\') { ++slashes; continue; }
    size_t count = slashes * ((*value == L'\"' || !*value) ? 2 : 1);
    while (count--) if (!append(output, L"\\")) return 0;
    slashes = 0;
    if (!*value) break;
    if (*value == L'\"' && !append(output, L"\\")) return 0;
    wchar_t character[2] = {*value, 0};
    if (!append(output, character)) return 0;
  }
  return append(output, L"\"");
}

int wmain(int argc, wchar_t **argv) {
  wchar_t module[CAPACITY], directory[CAPACITY], python[CAPACITY] = L"";
  wchar_t script[CAPACITY], config[CAPACITY], command[CAPACITY] = L"";
  DWORD length = GetModuleFileNameW(NULL, module, CAPACITY);
  if (!length || length >= CAPACITY) return 126;
  wcscpy(directory, module);
  wchar_t *separator = wcsrchr(directory, L'\\');
  if (!separator) return 126;
  separator[1] = 0;
  wcscpy(config, directory);
  if (!append(config, L"python-path.txt")) return 126;
  FILE *file = _wfopen(config, L"rb");
  if (!file) return 126;
  char path[CAPACITY];
  size_t size = fread(path, 1, sizeof(path) - 1, file);
  int overflow = !feof(file);
  fclose(file);
  if (overflow || !size || memchr(path, 0, size)) return 126;
  while (size && (path[size - 1] == '\n' || path[size - 1] == '\r')) --size;
  path[size] = 0;
  wchar_t relative[CAPACITY];
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                          relative, CAPACITY)) return 126;
  // Distribution preparation supplies a relative path inside the SDK.
  if (!size || relative[0] == L'/' || relative[0] == L'\\' ||
      wcschr(relative, L':')) return 126;
  wcscpy(python, directory);
  if (!append(python, relative)) return 126;
  wcscpy(script, module);
  size_t module_length = wcslen(script);
  if (module_length >= 4 && _wcsicmp(script + module_length - 4, L".exe") == 0)
    script[module_length - 4] = 0;
  if (!append(script, L".launcher.py") || !argument(command, python) ||
      !argument(command, L"-B") || !argument(command, script)) return 126;
  for (int i = 1; i < argc; ++i)
    if (!argument(command, argv[i])) return 126;
  STARTUPINFOW startup = {0};
  PROCESS_INFORMATION child = {0};
  startup.cb = sizeof(startup);
  // Preserve inherited console, standard streams, working directory and env.
  if (!CreateProcessW(python, command + 1, NULL, NULL, TRUE, 0, NULL, NULL,
                      &startup, &child)) {
    fwprintf(stderr, L"SDK Python launcher: CreateProcess failed (%lu)\n",
             GetLastError());
    return 126;
  }
  CloseHandle(child.hThread);
  DWORD result = 126;
  if (WaitForSingleObject(child.hProcess, INFINITE) == WAIT_OBJECT_0)
    GetExitCodeProcess(child.hProcess, &result);
  CloseHandle(child.hProcess);
  return (int)result;
}
