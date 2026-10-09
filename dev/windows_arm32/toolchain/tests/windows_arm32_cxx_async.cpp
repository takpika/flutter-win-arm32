extern "C" __declspec(dllexport) int cxx_async_cleanup() {
  int observed = 0;
  struct Probe {
    int& observed;
    ~Probe() { observed = 1; }
  };
  try {
    Probe probe{observed};
    throw 17;
  } catch (int value) {
    return value == 17 && observed == 1 ? 0 : 1;
  }
}
