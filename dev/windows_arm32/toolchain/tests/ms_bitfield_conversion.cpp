struct Bits {
  unsigned short value : 6;
  void Set(unsigned input) {
    value = input - 1; // gnu-error {{implicit conversion loses integer precision}}
  }
  void SetFloat(float input) {
    value = input; // ms-error {{floating-point number into integer}} gnu-error {{floating-point number into integer}}
  }
};
struct Plain {
  unsigned short value;
  void Set(unsigned input) {
    value = input - 1; // ms-error {{implicit conversion loses integer precision}} gnu-error {{implicit conversion loses integer precision}}
  }
};
void Overflow(Bits &bits) {
  bits.value = 64; // ms-error {{changes value}} gnu-error {{changes value}}
}
short Narrow(short value);
void Nested(Bits &bits, unsigned input) {
  bits.value = Narrow(input); // ms-error {{implicit conversion loses integer precision}} gnu-error {{implicit conversion loses integer precision}}
}
