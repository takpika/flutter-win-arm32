#pragma once
#include <__config>
#if defined(__MINGW32__) && defined(_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)
#pragma push_macro("_LIBCPP_DEPRECATED_IN_CXX17")
#undef _LIBCPP_DEPRECATED_IN_CXX17
#define _LIBCPP_DEPRECATED_IN_CXX17
#include_next <__iterator/iterator.h>
#pragma pop_macro("_LIBCPP_DEPRECATED_IN_CXX17")
#else
#include_next <__iterator/iterator.h>
#endif
