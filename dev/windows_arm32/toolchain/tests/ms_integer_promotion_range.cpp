// MSVC 19.44 accepts these value-preserving averages with /W4 /we4244.
inline signed char Average(signed char a, signed char b) {
  return ((short)a + (short)b) / 2; // gnu-error {{implicit conversion loses integer precision}}
}
inline short Average(short a, short b) {
  return ((int)a + (int)b) / 2; // gnu-error {{implicit conversion loses integer precision}}
}
inline unsigned char Average(unsigned char a, unsigned char b) {
  return ((unsigned short)a + (unsigned short)b) / 2; // gnu-error {{implicit conversion loses integer precision}}
}
short Unsafe(int value) {
  return value; // ms-error {{implicit conversion loses integer precision}} gnu-error {{implicit conversion loses integer precision}}
}
unsigned char SignChange(signed char value) {
  return (unsigned short)value; // ms-error {{implicit conversion loses integer precision}} gnu-error {{implicit conversion loses integer precision}}
}
short UnsafeWiden(int value) {
  return (long long)value; // ms-error {{implicit conversion loses integer precision}} gnu-error {{implicit conversion loses integer precision}}
}
unsigned char LiteralShift(unsigned x) {
  return 1 << x; // gnu-error {{implicit conversion loses integer precision}}
}
unsigned char LargeLiteralShift(unsigned x) {
  return 256 << x; // ms-error {{implicit conversion loses integer precision}} gnu-error {{implicit conversion loses integer precision}}
}
unsigned char ByteShift(unsigned char value, unsigned x) {
  return value << x; // gnu-error {{implicit conversion loses integer precision}}
}
unsigned char ShortShift(unsigned short value, unsigned x) {
  return value << x; // ms-error {{implicit conversion loses integer precision}} gnu-error {{implicit conversion loses integer precision}}
}
unsigned char WideShift(unsigned value, unsigned x) {
  return value << x; // ms-error {{implicit conversion loses integer precision}} gnu-error {{implicit conversion loses integer precision}}
}
unsigned char MaskedShift(unsigned value, unsigned x) {
  return (value & 127) << x; // gnu-error {{implicit conversion loses integer precision}}
}
unsigned char SignedShift(signed char value, unsigned x) {
  return value << x; // gnu-error {{implicit conversion loses integer precision}}
}
unsigned char ExpressionShift(unsigned char value) {
  return ((value - 1) << 1) | 1; // gnu-error {{implicit conversion loses integer precision}}
}
