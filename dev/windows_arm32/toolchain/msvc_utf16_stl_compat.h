#pragma once

// MSIX's Windows implementation uses the MSVC STL unsigned-short extension.
// This implementation adapter is included only by its private host build.
#include <string>
#include <codecvt>
#include <cstring>
#include <vector>

#if defined(_LIBCPP_VERSION)
namespace std {
template <> struct char_traits<unsigned short> {
  using char_type = unsigned short;
  using int_type = long;
  using pos_type = streampos;
  using off_type = streamoff;
  using state_type = mbstate_t;
  static void assign(char_type& target, const char_type& value) noexcept { target = value; }
  static constexpr bool eq(char_type a, char_type b) noexcept { return a == b; }
  static constexpr bool lt(char_type a, char_type b) noexcept { return a < b; }
  static int compare(const char_type* a, const char_type* b, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
      if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
  }
  static size_t length(const char_type* value) noexcept {
    size_t size = 0;
    while (value[size]) ++size;
    return size;
  }
  static const char_type* find(const char_type* value, size_t count, const char_type& c) noexcept {
    for (size_t i = 0; i < count; ++i) if (value[i] == c) return value + i;
    return nullptr;
  }
  static char_type* move(char_type* to, const char_type* from, size_t count) noexcept {
    if (count) memmove(to, from, count * sizeof(char_type));
    return to;
  }
  static char_type* copy(char_type* to, const char_type* from, size_t count) noexcept {
    if (count) memcpy(to, from, count * sizeof(char_type));
    return to;
  }
  static char_type* assign(char_type* to, size_t count, char_type value) noexcept {
    for (size_t i = 0; i < count; ++i) to[i] = value;
    return to;
  }
  static constexpr int_type to_int_type(char_type value) noexcept { return value; }
  static constexpr char_type to_char_type(int_type value) noexcept { return static_cast<char_type>(value); }
  static constexpr bool eq_int_type(int_type a, int_type b) noexcept { return a == b; }
  static constexpr int_type eof() noexcept { return -1; }
  static constexpr int_type not_eof(int_type value) noexcept { return value == eof() ? 0 : value; }
};

template <unsigned long Maxcode, codecvt_mode Mode>
class codecvt_utf8_utf16<unsigned short, Maxcode, Mode>
    : public codecvt_utf8_utf16<char16_t, Maxcode, Mode> {
  using base = codecvt_utf8_utf16<char16_t, Maxcode, Mode>;
public:
  using intern_type = unsigned short;
  using extern_type = char;
  using state_type = mbstate_t;
  using result = codecvt_base::result;
  explicit codecvt_utf8_utf16(size_t refs = 0) : base(refs) {}
  result in(state_type& state, const char* from, const char* end, const char*& next,
            intern_type* to, intern_type* to_end, intern_type*& to_next) const {
    const size_t capacity = static_cast<size_t>(to_end - to);
    vector<char16_t> buffer(capacity + 1);
    char16_t* written = buffer.data();
    result status = base::in(state, from, end, next, buffer.data(),
                             buffer.data() + capacity, written);
    const size_t count = static_cast<size_t>(written - buffer.data());
    for (size_t i = 0; i < count; ++i) to[i] = static_cast<intern_type>(buffer[i]);
    to_next = to + count;
    return status;
  }
  result out(state_type& state, const intern_type* from, const intern_type* end,
             const intern_type*& next, char* to, char* to_end, char*& to_next) const {
    const size_t count = static_cast<size_t>(end - from);
    vector<char16_t> buffer(count + 1);
    for (size_t i = 0; i < count; ++i) buffer[i] = static_cast<char16_t>(from[i]);
    const char16_t* consumed = buffer.data();
    result status = base::out(state, buffer.data(), buffer.data() + count,
                              consumed, to, to_end, to_next);
    next = from + (consumed - buffer.data());
    return status;
  }
};
}  // namespace std
#endif
