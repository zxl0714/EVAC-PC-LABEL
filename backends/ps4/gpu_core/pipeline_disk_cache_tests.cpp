#include "pipeline_disk_cache.h"
#include <Windows.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Ketsui::GpuCore;
namespace {
void Check(bool value, const char *name) {
  if (!value)
    throw std::runtime_error(name);
}
} // namespace
int main() {
  try {
    const auto root =
        std::filesystem::temp_directory_path() /
        (L"evac-pipeline-cache-test-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64())) /
        L"\u7f13\u5b58";
    PipelineDiskIdentity id{
        0x10de,
        0x2704,
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    std::vector<std::uint8_t> bytes(48, 0x73);
    const std::array<std::uint32_t, 4> header{32, 1, id.vendor, id.device};
    std::memcpy(bytes.data(), header.data(), 16);
    std::memcpy(bytes.data() + 16, id.uuid.data(), 16);
    const auto path = PipelineDiskPath(root, id);
    Check(ReadPipelineDisk(path, id).empty(), "missing cache");
    Check(PipelineDiskPath({}, id).empty(), "disabled cache path");
    Check(ValidPipelineDiskData(bytes, id), "valid Vulkan cache header");
    Check(WritePipelineDisk(path, bytes, id), "write Unicode cache path");
    Check(ReadPipelineDisk(path, id) == bytes, "cache round trip");
    auto changed = id;
    changed.vendor ^= 1;
    Check(ReadPipelineDisk(path, changed).empty(), "vendor isolation");
    changed = id;
    changed.device ^= 1;
    Check(ReadPipelineDisk(path, changed).empty(), "device isolation");
    changed = id;
    changed.uuid[15] ^= 1;
    Check(ReadPipelineDisk(path, changed).empty(), "driver UUID isolation");
    auto invalid = bytes;
    invalid[0] = 31;
    Check(!WritePipelineDisk(path, invalid, id), "invalid header refused");
    Check(ReadPipelineDisk(path, id) == bytes,
          "invalid write preserves old cache");
    {
      std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
      file.seekp(-1, std::ios::end);
      file.put('!');
    }
    Check(ReadPipelineDisk(path, id).empty(), "corrupt payload refused");
    Check(WritePipelineDisk(path, bytes, id), "replace corrupt cache");
    {
      std::ofstream file(path, std::ios::binary | std::ios::app);
      file.put('!');
    }
    Check(ReadPipelineDisk(path, id).empty(), "appended bytes refused");
    {
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      file << "EV";
    }
    Check(ReadPipelineDisk(path, id).empty(), "truncated cache refused");
    {
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      file << "block directory";
    }
    Check(!WritePipelineDisk(path / L"blocked", bytes, id),
          "unwritable destination is optional");
    std::cout << "{\"passed\":true,\"checks\":15}\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
