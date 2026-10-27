#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "extract.h"
#include <array>
#include <fstream>
#include <map>
#include <set>

namespace ps4extract {
namespace {
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
};
void fail(const std::wstring& message) { throw Error{message}; }
unsigned big32(std::ifstream& stream) {
  std::array<unsigned char, 4> data{};
  if (!stream.read(reinterpret_cast<char*>(data.data()), data.size())) fail(L"Truncated PKG header / PKG 文件不完整");
  return (unsigned(data[0]) << 24) | (unsigned(data[1]) << 16) | (unsigned(data[2]) << 8) | data[3];
}
void check_path(const fs::path& path) {
  for (auto part = path; !part.empty();) {
    const auto attributes = GetFileAttributesW(part.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      fail(L"Choose a normal folder, not a linked folder / 请选择普通目录，不要使用链接目录");
    const auto parent = part.parent_path();
    if (parent == part) break;
    part = parent;
  }
}
void run(const fs::path& tool, const std::vector<std::wstring>& args, HANDLE log,
         const std::atomic_bool& cancel) {
  if (cancel) fail(L"Cancelled / 已取消");
  std::wstring command = quote(tool.wstring());
  for (const auto& arg : args) command += L" " + quote(arg);
  Handle job(CreateJobObjectW(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    fail(L"Cannot prepare extraction process / 无法创建解包进程");
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  Handle null_input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr));
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = null_input.value;
  startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log;
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::vector<char> storage(size);
  startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) fail(L"Cannot initialize extraction / 无法初始化解包");
  struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Attributes() { DeleteProcThreadAttributeList(p); } } attributes{startup.lpAttributeList};
  HANDLE inherited[] = {null_input.value, log};
  if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                inherited, sizeof(inherited), nullptr, nullptr)) fail(L"Cannot redirect extraction log / 无法记录解包日志");
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(tool.c_str(), command.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                      nullptr, tool.parent_path().c_str(), &startup.StartupInfo, &process))
    fail(L"Cannot start PkgTool. Keep its dependencies beside it. / 无法启动 PkgTool，请保留配套依赖");
  Handle process_handle(process.hProcess), thread(process.hThread);
  if (!AssignProcessToJobObject(job.value, process_handle.value)) {
    TerminateProcess(process_handle.value, 1);
    WaitForSingleObject(process_handle.value, INFINITE);
    fail(L"Cannot manage extraction process / 无法管理解包进程");
  }
  if (ResumeThread(thread.value) == DWORD(-1)) fail(L"Cannot start extraction / 无法开始解包");
  for (;;) {
    const auto wait = WaitForSingleObject(process_handle.value, 100);
    if (wait == WAIT_OBJECT_0) break;
    if (wait == WAIT_FAILED) fail(L"Cannot wait for extraction / 无法等待解包进程");
    if (cancel) {
      TerminateJobObject(job.value, 1);
      WaitForSingleObject(process_handle.value, INFINITE);
      fail(L"Cancelled; partial output retained / 已取消，未完成的输出已保留");
    }
  }
  DWORD exit_code = 1;
  if (!GetExitCodeProcess(process_handle.value, &exit_code) || exit_code != 0)
    fail(L"PkgTool failed. Check the package/passcode and extract.log. / 解包失败，请检查 PKG、passcode 和 extract.log");
}
}

std::wstring quote(const std::wstring& argument) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t ch : argument) {
    if (ch == L'\\') { ++slashes; continue; }
    result.append(slashes * (ch == L'\"' ? 2 : 1), L'\\');
    slashes = 0;
    if (ch == L'\"') result += L'\\';
    result += ch;
  }
  result.append(slashes * 2, L'\\');
  return result + L'\"';
}

std::vector<Entry> read_entries(const fs::path& input) {
  static const std::map<unsigned, std::wstring> names = {
    {0x400,L"license.dat"}, {0x401,L"license.info"}, {0x402,L"nptitle.dat"}, {0x403,L"npbind.dat"},
    {0x409,L"psreserved.dat"}, {0x1000,L"param.sfo"}, {0x1001,L"playgo-chunk.dat"},
    {0x1002,L"playgo-chunk.sha"}, {0x1003,L"playgo-manifest.xml"}, {0x1004,L"pronunciation.xml"},
    {0x1005,L"pronunciation.sig"}, {0x1006,L"pic1.png"}, {0x1008,L"app/playgo-chunk.dat"},
    {0x100b,L"shareparam.json"}, {0x1200,L"icon0.png"}, {0x1220,L"pic0.png"}, {0x1240,L"snd0.at9"},
    {0x1260,L"changeinfo/changeinfo.xml"}, {0x1280,L"icon0.dds"}, {0x12a0,L"pic0.dds"},
    {0x12c0,L"pic1.dds"}, {0x1400,L"trophy/trophy00.trp"}
  };
  std::ifstream stream(input, std::ios::binary);
  if (!stream || big32(stream) != 0x7f434e54) fail(L"Select a PS4 PKG file / 请选择 PS4 PKG 文件");
  stream.seekg(0x10); const auto count = big32(stream);
  stream.seekg(0x18); const auto table = big32(stream);
  const auto size = fs::file_size(input);
  if (!count || count > 4096 || table < 0x20 || uint64_t(table) + uint64_t(count) * 32 > size)
    fail(L"Invalid PKG entry table / PKG 条目表不完整");
  std::vector<Entry> entries;
  std::set<unsigned> seen;
  for (unsigned index = 0; index < count; ++index) {
    stream.seekg(uint64_t(table) + uint64_t(index) * 32);
    auto id = big32(stream);
    if (auto name = names.find(id); name != names.end()) {
      if (!seen.insert(id).second) fail(L"Duplicate PKG metadata / PKG 元数据重复");
      entries.push_back({index, name->second});
    }
  }
  if (!seen.contains(0x1000)) fail(L"PKG has no param.sfo / PKG 缺少 param.sfo");
  return entries;
}

fs::path extract(const Options& options, const Progress& progress, const std::atomic_bool& cancel) {
  if (options.input.empty() || options.output.empty() || options.tool.empty()) fail(L"Choose a PKG and output folder / 请选择 PKG 和输出目录");
  auto input = fs::absolute(options.input).lexically_normal();
  auto output = fs::absolute(options.output).lexically_normal();
  auto tool = fs::absolute(options.tool).lexically_normal();
  check_path(input); check_path(output); check_path(tool);
  if (options.passcode.size() != 32 || options.passcode.find(L'\0') != std::wstring::npos)
    fail(L"PKG passcode must be 32 characters / PKG passcode 须为 32 个字符");
  if (!fs::is_regular_file(input)) fail(L"PKG file not found / 找不到 PKG 文件");
  const auto entries = read_entries(input);
  if (!fs::is_regular_file(tool) || !fs::is_regular_file(tool.parent_path() / L"LibOrbisPkg.dll"))
    fail(L"PkgTool.exe or LibOrbisPkg.dll is missing. Re-extract the complete program package. / 解包组件缺失，请完整解压程序包");
  if (fs::exists(output)) fail(L"Output folder already exists. Choose a new name. / 输出目录已存在，请换一个新目录名");
  if (!fs::is_directory(output.parent_path())) fail(L"Output parent folder does not exist / 输出目录的上一级不存在");
  ULARGE_INTEGER free{};
  if (GetDiskFreeSpaceExW(output.parent_path().c_str(), &free, nullptr, nullptr) && free.QuadPart < fs::file_size(input))
    fail(L"Not enough free disk space / 磁盘可用空间不足");
  if (cancel) fail(L"Cancelled / 已取消");
  if (!fs::create_directory(output)) fail(L"Cannot create output folder / 无法创建输出目录");
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  Handle log(CreateFileW((output / L"extract.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                        &inherit, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (log.value == INVALID_HANDLE_VALUE) fail(L"Cannot create extract.log / 无法创建解包日志");
  progress(L"Extracting game files / 正在提取游戏文件…");
  run(tool, {L"pkg_extract", L"--passcode", options.passcode, input.wstring(), output.wstring()}, log.value, cancel);
  auto game = output / L"uroot";
  check_path(game);
  // Patches may contain only changed resources, so do not require eboot.bin here.
  if (!fs::is_directory(game) || fs::is_empty(game)) fail(L"No game files extracted; check the PKG and passcode / 未提取出游戏文件，请检查 PKG 和 passcode");
  for (const auto& entry : entries) {
    auto target = game / L"sce_sys" / entry.name;
    check_path(target);
    fs::create_directories(target.parent_path());
    progress(L"Metadata / 元数据: " + entry.name);
    run(tool, {L"pkg_extractentry", L"--passcode", options.passcode, input.wstring(),
               std::to_wstring(entry.index), target.wstring()}, log.value, cancel);
    if (!fs::is_regular_file(target)) fail(L"Metadata extraction failed / 元数据提取失败: " + entry.name);
  }
  if (cancel) fail(L"Cancelled / 已取消");
  progress(L"Complete / 解包完成");
  return game;
}
}
