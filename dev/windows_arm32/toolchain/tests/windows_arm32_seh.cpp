extern "C" { volatile unsigned finally_allocation_size = 19; }
extern "C" int _abnormal_termination();
extern "C" __declspec(dllimport) void __stdcall RaiseException(unsigned, unsigned, unsigned, const void*);
extern "C" unsigned long _exception_code();
extern "C" __declspec(dllexport) int seh_filter_capture() {
  volatile int state = 3;
  __try { RaiseException(0xe0550071u, 0, 0, nullptr); }
  __except (_exception_code() == 0xe0550071u && state == 3 ? 1 : 0) { state += 7; }
  return state;
}
extern "C" __declspec(dllexport) int seh_finally_unwind() {
  alignas(32) volatile int state = 1; volatile char* scratch = (volatile char*)__builtin_alloca(finally_allocation_size); scratch[0] = 1;
  __try {
    __try { RaiseException(0xe0550071u, 0, 0, nullptr); }
    __finally { state += _abnormal_termination() ? 10 : 1000; }
  } __except (1) { state += 100; }
  return state;
}
extern "C" __declspec(dllexport) int seh_continue_search() {
  volatile int state = 0;
  __try {
    __try { RaiseException(0xe0550071u, 0, 0, nullptr); }
    __except (0) { state = -1; }
  } __except (1) { state = 23; }
  return state;
}
extern "C" __declspec(dllexport) int seh_hardware_fault() {
  __try { return *(volatile int*)0; }
  __except (_exception_code() == 0xc0000005u ? 1 : 0) { return 47; }
}

extern "C" __declspec(dllexport) int seh_finally_normal() {
  alignas(32) volatile int state = 1; volatile char* scratch = (volatile char*)__builtin_alloca(finally_allocation_size); scratch[0] = 1;
  __try { state += 2; }
  __finally { state += _abnormal_termination() ? 1000 : 10; }
  return state;
}
