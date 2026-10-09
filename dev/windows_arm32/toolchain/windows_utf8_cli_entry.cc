// Private host-tool entry adapter: Windows UTF-16 argv -> upstream UTF-8 main.
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

int flutter_msix_utf8_main(int argc, char* argv[]);

int wmain(int argc, wchar_t* argv[]) {
  std::vector<std::string> values;
  values.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i],
                                        -1, nullptr, 0, nullptr, nullptr);
    if (!size) return 1;
    std::string value(size, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                            &value[0], size, nullptr, nullptr) != size) {
      return 1;
    }
    values.push_back(std::move(value));
  }
  std::vector<char*> arguments;
  arguments.reserve(argc + 1);
  for (auto& value : values) arguments.push_back(&value[0]);
  arguments.push_back(nullptr);
  return flutter_msix_utf8_main(argc, arguments.data());
}
