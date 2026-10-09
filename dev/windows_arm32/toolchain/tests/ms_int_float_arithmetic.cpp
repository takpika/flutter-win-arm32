// MSVC 19.44 accepts usual arithmetic conversions with /W4 /we4244,
// while rejecting lossy implicit return conversions.
double Ratio(long long current, long long frequency) {
  return static_cast<double>(current) / frequency; // gnu-error {{may lose precision}}
}
double DirectDouble(long long value) {
  return value; // ms-error {{may lose precision}} gnu-error {{may lose precision}}
}
float DirectFloat(long long value) {
  return value; // ms-error {{may lose precision}} gnu-error {{may lose precision}}
}
double Sum(long long value, double base) {
  return value + base; // gnu-error {{may lose precision}}
}
int FloatToInt(float value) {
  return value; // ms-error {{floating-point number into integer}} gnu-error {{floating-point number into integer}}
}
float Conditional(bool choose, unsigned integer, float real) {
  return choose ? integer : real; // gnu-error {{may lose precision}}
}
float IntegerConditional(bool choose, unsigned a, unsigned b) {
  return choose ? a : b; // ms-error 2 {{may lose precision}} gnu-error 2 {{may lose precision}}
}
float NestedConditional(bool outer, bool inner, unsigned a, unsigned b, float real) {
  return outer ? (inner ? a : b) : real; // gnu-error 2 {{may lose precision}}
}
bool Less(float real, int integer) {
  return real < integer; // gnu-error {{may lose precision}}
}
bool Equal(float real, int integer) {
  return real == integer; // gnu-error {{may lose precision}}
}
bool GreaterEqual(double real, long long integer) {
  return real >= integer; // gnu-error {{may lose precision}}
}
float Clamp(float value, int minimum, int maximum) {
  return value > minimum ? (value > maximum ? maximum : value) : minimum; // gnu-error 4 {{may lose precision}}
}
void Add(float &real, int integer) {
  real += integer; // gnu-error {{may lose precision}}
}
void Subtract(float &real, int integer) {
  real -= integer; // gnu-error {{may lose precision}}
}
void Multiply(float &real, int integer) {
  real *= integer; // gnu-error {{may lose precision}}
}
void Divide(float &real, int integer) {
  real /= integer; // gnu-error {{may lose precision}}
}
void DoubleAdd(float &real, double value) {
  real += value; // ms-error {{loses floating-point precision}} gnu-error {{loses floating-point precision}}
}
void DirectAssign(float &real, int integer) {
  real = integer; // ms-error {{may lose precision}} gnu-error {{may lose precision}}
}
