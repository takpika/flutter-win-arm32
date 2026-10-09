template <class F> void visit(F& visitor) { visitor(); }

struct Record {
  int value;
  void operator()() { ++value; }
};
struct Other {};

void take_record(Record&);
void take_volatile(volatile Record&);
void take_scalar(int&);

void permitted_only_in_ms_compatibility() {
  visit([]() mutable {}); // standard-error {{no matching function}}
  take_record(Record{1}); // standard-error {{no matching function}}
}

void preserve_invalid_bindings() {
  take_volatile(Record{1}); // standard-error {{no matching function}} ms-error {{no matching function}}
  take_record(Other{});    // standard-error {{no matching function}} ms-error {{no matching function}}
  take_scalar(1);          // standard-error {{no matching function}} ms-error {{no matching function}}
  const Record value{1};
  take_record(value);      // standard-error {{no matching function}} ms-error {{no matching function}}
}

// MSVC's temporary-reference extension must not displace standard bindings.
char prefer_const(Record&);
int prefer_const(const Record&);
char prefer_rvalue(Record&);
int prefer_rvalue(Record&&);
char prefer_value(Record&);
int prefer_value(Record);
static_assert(sizeof(prefer_const(Record{1})) == sizeof(int), "const reference");
static_assert(sizeof(prefer_rvalue(Record{1})) == sizeof(int), "rvalue reference");
static_assert(sizeof(prefer_value(Record{1})) == sizeof(int), "value parameter");

struct Base {};
struct Derived : Base {};
char prefer_base(Derived&);
int prefer_base(const Base&);
static_assert(sizeof(prefer_base(Derived{})) == sizeof(int), "base reference");

struct Member {
  char method();
  int method() const;
};
static_assert(sizeof(Member{}.method()) == sizeof(char), "implicit object");

// An array view's overloaded address operator may expose const storage even
// when the view itself is mutable. Its temporary must select const X&.
template <class T> const T& at_offset(const void* p) {
  return *static_cast<const T*>(p);
}
template <class T> T& at_offset(void* p) { return *static_cast<T*>(p); }
template <class T, class X> const T& after_view(const X& x) {
  return at_offset<T>(&x);
}
template <class T, class X> T& after_view(X& x) { return at_offset<T>(&x); }
struct ConstStorageView { const void* operator&() const; };
const Record& read_const_storage() { return after_view<Record>(ConstStorageView{}); }

// Extension use is ranked for the entire candidate, before ordinary argument
// conversions. An exact match on another argument cannot favor the extension.
char prefer_mixed(Record&, int);
int prefer_mixed(const Record&, long);
static_assert(sizeof(prefer_mixed(Record{1}, 1)) == sizeof(int), "whole candidate");

// Once both candidates need the extension, ordinary conversion ranks apply.
char prefer_two(Record&, Record&);
int prefer_two(const Record&, Record&);
static_assert(sizeof(prefer_two(Record{1}, Record{1})) == sizeof(char), "both extended"); // standard-error {{no matching function}}
char swapped(Record&, const Record&);
int swapped(const Record&, Record&);
void preserve_ambiguous_extensions() {
  swapped(Record{1}, Record{1}); // standard-error {{no matching function}} ms-error {{call to 'swapped' is ambiguous}}
}

// ANGLE compares a temporary bitset with an lvalue. The mutable non-member
// template must not compete with the member accepting a const reference.
template <int N> struct BitSet {
  constexpr bool operator==(const BitSet&) const { return true; }
};
template <int N> bool operator==(BitSet<N>&, BitSet<N>&) { return false; }
constexpr bool compare_bitset() {
  BitSet<8> value;
  return BitSet<8>{} == value;
}
static_assert(compare_bitset(), "standard comparison wins");
