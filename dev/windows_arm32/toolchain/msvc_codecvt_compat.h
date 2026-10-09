#pragma once

// Honor the original MSVC STL opt-out for these conversion classes only.
// Include their dependencies before temporarily adjusting the declaration
// attribute, so unrelated C++17 deprecation attributes remain unchanged.
#if defined(_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
#include <__config>
#include <__locale>
#include <string>
#include <ios>
#include <streambuf>
#include <version>
#include <__algorithm/reverse.h>

#pragma push_macro("_LIBCPP_DEPRECATED_IN_CXX17")
#undef _LIBCPP_DEPRECATED_IN_CXX17
#define _LIBCPP_DEPRECATED_IN_CXX17
#include <codecvt>
#include <__locale_dir/wstring_convert.h>
#include <__locale_dir/wbuffer_convert.h>
#pragma pop_macro("_LIBCPP_DEPRECATED_IN_CXX17")
#endif
