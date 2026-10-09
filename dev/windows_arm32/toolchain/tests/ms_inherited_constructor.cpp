// Run with Clang -cc1 -std=c++17 -verify=standard -verify-ignore-unexpected=note.
// Also run with -fms-compatibility -verify=ms instead of -verify=standard.
struct Parent {
  template<class T, class = void> Parent(T&&) {}
};
struct Compatible : Parent {
  using Parent::Parent;
  template<class T> explicit Compatible(T&& value) : Parent(value) {}
};
void take(const Compatible&);
void positive() {
  take(1); // standard-error {{no matching function}}
  take(true); // standard-error {{no matching function}}
}

// Compatibility must not turn an explicit-only constructor into an implicit one.
struct ExplicitOnly {
  template<class T> explicit ExplicitOnly(T&&) {}
};
void reject(const ExplicitOnly&);
void negative() {
  reject(1); // standard-error {{no matching function}} ms-error {{no matching function}}
}

// Ordinary inherited member templates retain their existing hiding behavior.
struct MethodParent {
  template<class T, class = void> void call(T) {}
};
struct MethodChild : MethodParent {
  using MethodParent::call;
  template<class T> void call(T) = delete;
};
void ordinary_member(MethodChild& value) {
  value.call(1); // standard-error {{call to deleted member function}} ms-error {{call to deleted member function}}
}
