#include "common/catalogue.h"
#include "common/ini.h"
#include "common/paths.h"
#include "common/settings.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>

namespace {
std::string Json(std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  std::string out = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
    else out += char(c);
  }
  return out + '"';
}
void Strings(const std::vector<std::string>& values) {
  std::cout << '[';
  bool first = true;
  for (const auto& value : values) { if (!first) std::cout << ','; first = false; std::cout << Json(value); }
  std::cout << ']';
}
}  // namespace
int wmain(int argc, wchar_t** argv) {
  try {
    wchar_t executable[32768];
    auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length == 32768) throw std::runtime_error("Cannot determine application root");
    cave::fs::path root = cave::fs::path(executable).parent_path();
    cave::fs::path override_data,override_content;
    std::string selected;
    cave::CheckLevel level = cave::CheckLevel::Quick;
    for (int i = 1; i < argc; ++i) {
      std::wstring option = argv[i];
      if (option == L"--help") {
        std::cout << "cave_inspect [--root <collection>] [--game <id>] [--data-dir <directory>] [--dlc-dir <directory>] [--programs|--full]\n"
                     "Read-only JSON report. No game execution, downloads or configuration writes.\n";
        return 0;
      }
      if (option == L"--full") level = cave::CheckLevel::Full;
      else if (option == L"--programs") level = cave::CheckLevel::Programs;
      else if (option == L"--root" || option == L"--game" || option == L"--data-dir" || option == L"--dlc-dir") {
        if (++i == argc) throw std::runtime_error("Missing option value");
        if (option == L"--root") root = argv[i];
        else if (option == L"--game") selected = cave::Utf8(cave::fs::path(argv[i]));
        else if(option==L"--dlc-dir")override_content=argv[i];
        else override_data = argv[i];
      } else throw std::runtime_error("Unknown option");
    }
    if (!selected.empty()) (void)cave::FindGame(selected);
    if (!override_data.empty() && selected.empty()) throw std::runtime_error("--data-dir requires --game");
    if (!override_content.empty() && (selected.empty() || cave::FindGame(selected).content.empty()))
      throw std::runtime_error("--dlc-dir requires a game with optional content");
    root = cave::fs::weakly_canonical(cave::fs::absolute(root));
    const auto ini = cave::Ini::Load(cave::ConfigPath(root));
    std::cout << "{\"format\":1,\"guestExecution\":false,\"games\":[";
    bool first = true;
    for (const auto& game : cave::Catalogue()) {
      if (!selected.empty() && game.id != selected) continue;
      cave::Inspection info;
      try {
        info = cave::Inspect(game, root, override_data.empty() ? ini.DataDirectory(game) : override_data, level);
      } catch (const std::exception& error) {
        info.game = game.id; info.state = "invalid_config"; info.reason = error.what();
      }
      if (!first) std::cout << ',';
      first = false;
      std::cout << "{\"id\":" << Json(game.id) << ",\"name\":" << Json(game.name)
                << ",\"backend\":" << Json(game.backend) << ",\"state\":" << Json(info.state)
                << ",\"dataState\":" << Json(cave::Name(info.data_state)) << ",\"reason\":" << Json(info.reason)
                << ",\"dataDirectory\":" << Json(cave::Utf8(info.data_dir))
                << ",\"dataVariant\":" << Json(info.variant) << ",\"dataVersion\":" << Json(info.version)
                << ",\"dataLayout\":" << Json(info.layout)
                << ",\"filesChecked\":" << info.files_checked << ",\"hashesChecked\":" << info.hashes_checked
                << ",\"problems\":";
      Strings(info.problems);
      std::cout << ",\"missingRuntime\":";
      Strings(info.missing_runtime);
      std::cout << ",\"candidateRoots\":";
      Strings(info.candidate_roots);
      std::cout << ",\"optionalContent\":[";
      cave::fs::path content_source;
      std::vector<cave::ContentInspection> content;
      try {
        content_source = override_content.empty()?ini.ContentDirectory(game, info.data_dir):override_content;
        if (!content_source.empty()) content_source = cave::Resolve(root, content_source);
        content = cave::InspectContent(game, content_source, level);
      } catch (const std::exception& error) {
        for (const auto& package : game.content)
          content.push_back({std::string(package.id),std::string(package.name),cave::DataState::Invalid,0,0,{error.what()},{},{}});
      }
      bool first_content = true;
      for (const auto& package : content) {
        if (!first_content) std::cout << ',';
        first_content = false;
        std::cout << "{\"id\":" << Json(package.id) << ",\"name\":" << Json(package.name)
                  << ",\"state\":" << Json(cave::Name(package.state))
                  << ",\"directory\":" << Json(cave::Utf8(package.directory))
                  << ",\"container\":" << Json(cave::Utf8(package.container))
                  << ",\"filesChecked\":" << package.files_checked << ",\"hashesChecked\":" << package.hashes_checked
                  << ",\"problems\":";
        Strings(package.problems); std::cout << '}';
      }
      std::cout << "],\"contentDirectory\":" << Json(cave::Utf8(content_source));
      std::cout << '}';
    }
    std::cout << "]}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cout << "{\"error\":" << Json(error.what()) << "}\n";
    return 2;
  }
}
