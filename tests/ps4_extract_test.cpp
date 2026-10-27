#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "extract.h"
#include <fstream>
#include <iostream>
#include <thread>
using namespace ps4extract;
unsigned checks = 0;
void check(bool condition) { ++checks; if (!condition) throw std::runtime_error("Check " + std::to_string(checks) + " failed"); }
template<class F> void fails(F action) { bool failed = false; try { action(); } catch (const Error&) { failed = true; } check(failed); }
void write(const fs::path& path, const std::string& value) { fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << value; }
std::string fixture() {
  std::string data(0x100, '\0');
  auto put = [&](size_t pos, unsigned value) { for(int i=3;i>=0;--i) { data[pos+i] = char(value & 255); value >>= 8; } };
  put(0,0x7f434e54); put(0x10,2); put(0x18,0x40); put(0x40,0x1000); put(0x60,0x1260);
  return data;
}
int wmain(int argc, wchar_t** argv) {
  if (argc > 1) { // Fake CLI dependency: tests invoke this via the production process runner.
    if (argc != 6 && argc != 7) return 9;
    fs::path input = argv[4];
    if (input.stem() == L"failure") return 7;
    if (input.stem() == L"slow") Sleep(30000);
    if (std::wstring(argv[1]) == L"pkg_extract") {
      auto root = fs::path(argv[5]) / L"uroot";
      write(root / (input.stem() == L"patch" ? L"ps4data_patch_body.bin" : L"eboot.bin"), "authored resource");
      return 0;
    }
    if (std::wstring(argv[1]) == L"pkg_extractentry" && argc == 7) {
      write(argv[6], "authored metadata"); return 0;
    }
    return 8;
  }
  fs::path root = fs::temp_directory_path() / (L"evac-ps4-extract-tests-" + std::to_wstring(GetCurrentProcessId()));
  if (fs::exists(root)) return 2;
  fs::create_directory(root);
  try {
    auto input = root / L"中文 spaces & quote' game.pkg";
    write(input,fixture());
    auto entries = read_entries(input);
    check(entries.size()==2); check(entries[0].name==L"param.sfo"); check(entries[1].index==1);
    check(quote(L"a b")==L"\"a b\""); check(quote(L"trailing\\")==L"\"trailing\\\\\"");
    check(quote(L"say\"hello")==L"\"say\\\"hello\"");
    auto bad = root / L"bad.pkg";
    write(bad,"ZIP"); fails([&]{read_entries(bad);});
    auto data=fixture(); data[0x43]=1; write(bad,data); fails([&]{read_entries(bad);});
    data=fixture(); data[0x62]=0x10; data[0x63]=0; write(bad,data); fails([&]{read_entries(bad);});
    data=fixture(); data.resize(0x70); write(bad,data); fails([&]{read_entries(bad);});
    auto tool = root / L"工具 files" / L"PkgTool.exe";
    fs::create_directories(tool.parent_path()); fs::copy_file(argv[0],tool);
    write(tool.parent_path()/L"LibOrbisPkg.dll", "authored dependency sentinel");
    std::atomic_bool cancel{false};
    auto progress=[](const std::wstring&){};
    Options options{input, root/L"输出 data", tool};
    auto result=extract(options,progress,cancel);
    check(fs::is_regular_file(result/L"eboot.bin"));
    check(fs::is_regular_file(result/L"sce_sys/param.sfo"));
    check(fs::is_regular_file(result/L"sce_sys/changeinfo/changeinfo.xml"));
    check(fs::file_size(input)==fixture().size());
    fails([&]{extract(options,progress,cancel);});
    check(fs::file_size(result/L"eboot.bin")==17);
    options.input=root/L"patch.pkg"; write(options.input,fixture()); options.output=root/L"patch-output";
    result=extract(options,progress,cancel); check(!fs::exists(result/L"eboot.bin"));
    check(fs::is_regular_file(result/L"ps4data_patch_body.bin"));
    options.output=root/L"bad-pass"; options.passcode=L"wrong";
    fails([&]{extract(options,progress,cancel);}); check(!fs::exists(options.output));
    options.passcode=std::wstring(32,L'0'); options.tool=root/L"absent.exe";
    fails([&]{extract(options,progress,cancel);}); check(!fs::exists(options.output));
    options.tool=tool; options.input=root/L"failure.pkg"; write(options.input,fixture());
    fails([&]{extract(options,progress,cancel);}); check(fs::is_regular_file(options.output/L"extract.log"));
    options.output=root/L"cancelled"; cancel=true;
    fails([&]{extract(options,progress,cancel);}); check(!fs::exists(options.output));
    cancel=false; options.input=root/L"slow.pkg"; write(options.input,fixture());
    std::thread interrupter([&]{Sleep(150);cancel=true;});
    fails([&]{extract(options,progress,cancel);}); interrupter.join();
    check(fs::is_regular_file(options.output/L"extract.log"));
    std::cout << checks << " extraction checks passed\n";
  } catch (const Error& error) { std::wcerr << error.message << L"\n"; return 1; }
    catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
  // Only this process's newly created fixture directory is removed.
  fs::remove_all(root); return 0;
}
