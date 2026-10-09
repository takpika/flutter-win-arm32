#pragma once
#if defined(__MINGW32__)
#include <cxxabi.h>
#ifndef _VCRTIMP
#define _VCRTIMP
#endif
// ATL asks the C++ runtime whether an exception is currently unwinding.
// This SDK uses the GNU C++ ABI runtime rather than the Microsoft C++ runtime.
extern "C" inline bool __uncaught_exception() {
  return __cxxabiv1::__cxa_uncaught_exception();
}
#endif
