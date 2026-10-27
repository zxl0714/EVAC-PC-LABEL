// Developer harness. The production GPU plugin owns all graphics/global state.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

int main(int argc, char** argv) {
  wchar_t executable[32768];
  const auto count = GetModuleFileNameW(nullptr, executable, 32768);
  if (!count || count == 32768) return 2;
  wchar_t* slash = wcsrchr(executable, L'\\');
  if (!slash) return 2;
  wcscpy_s(slash + 1, 32768 - (slash + 1 - executable), L"rexgpu-xenos.dll");
  auto plugin = LoadLibraryExW(executable, nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!plugin) {
    std::fprintf(stderr, "Cannot load the sibling GPU plugin: %lu\n", GetLastError());
    return 2;
  }
  using Run = int (*)(int, char**);
  auto run = reinterpret_cast<Run>(GetProcAddress(plugin, "rex_gpu_trace_benchmark"));
  if (!run) return 2;
  // Keep the plugin loaded until process exit, like the normal runtime loader.
  return run(argc, argv);
}
