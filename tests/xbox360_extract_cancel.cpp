#include "extract.h"
#include <fstream>
#include <iostream>

// Exercises cancellation and destination races through the public core API.
int wmain(int argc, wchar_t** argv) {
  if (argc != 4) return 2;
  std::atomic_bool cancel{false};
  const xboxextract::Options options{argv[2], argv[3]};
  const std::wstring mode = argv[1];
  bool triggered = false;
  try {
    xboxextract::Extract(options, [&](const xboxextract::Progress& progress) {
      if (triggered || progress.done == 0) return;
      triggered = true;
      if (mode == L"cancel") cancel = true;
      else if (mode == L"destination-race") {
        std::filesystem::create_directory(options.output);
        std::ofstream(options.output / "keep.txt") << "existing data";
      } else throw std::runtime_error("Unknown test mode");
    }, cancel);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return triggered ? 0 : 3;
  }
  return 4;  // Both test cases must fail safely rather than publish.
}
