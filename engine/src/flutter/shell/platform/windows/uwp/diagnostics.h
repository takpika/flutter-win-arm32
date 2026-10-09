#pragma once
#include <windows.h>
namespace flutter::winrt {
const wchar_t* DiagnosticPath();
void Diagnostic(const char* expression, HRESULT result);
}
