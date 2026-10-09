// default-no-diagnostics
float Literal() {
  return 0.1; // ms-warning {{floating-point precision}} gnu-error {{floating-point precision}} wx-error {{floating-point precision}}
}
float Variable(double value) {
  return value; // ms-error {{floating-point precision}} gnu-error {{floating-point precision}} wx-error {{floating-point precision}}
}
float Arithmetic(double value) {
  return value * 0.1; // ms-error {{floating-point precision}} gnu-error {{floating-point precision}} wx-error {{floating-point precision}}
}
float ConstantExpression() {
  return 0.1 + 0.2; // ms-warning {{floating-point precision}} gnu-error {{floating-point precision}} wx-error {{floating-point precision}}
}
