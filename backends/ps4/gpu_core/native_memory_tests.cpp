#include "native_memory_tests.h"
#include "../memory/write_tracker.h"
#include "gpu_worker.h"
#include "native_memory_backend.h"
#include "recompiler_tests.h"
#include "test_diagnostics.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstring>
#include <future>
#include <map>

namespace Ketsui::GpuCore {
namespace {
using AllocateFn = int(__attribute__((sysv_abi)) *)(std::int64_t, std::int64_t,
                                                    u64, u64, int,
                                                    std::int64_t *);
using MapFn = int(__attribute__((sysv_abi)) *)(void **, u64, int, int,
                                               std::int64_t, u64);
using UnmapFn = int(__attribute__((sysv_abi)) *)(void *, u64);
using ReleaseFn = int(__attribute__((sysv_abi)) *)(u64, u64);
using ProtectFn = int(__attribute__((sysv_abi)) *)(void *, u64, int);
const auto allocate =
    reinterpret_cast<AllocateFn>(ResolveMemoryImport("rTXw65xmLIA"));
const auto map = reinterpret_cast<MapFn>(ResolveMemoryImport("L-Q3LEjIbgA"));
const auto unmap =
    reinterpret_cast<UnmapFn>(ResolveMemoryImport("cQke9UuBQOk"));
const auto release =
    reinterpret_cast<ReleaseFn>(ResolveMemoryImport("MBuItvba6z8"));
const auto protect =
    reinterpret_cast<ProtectFn>(ResolveMemoryImport("vSMAm3cxYTY"));
constexpr u64 allocation_bytes = 0x40000, offset = 0x10000,
              buffer_bytes = 0x8000;
void Require(bool ok, const char *text) {
  if (!ok) {
    throw std::runtime_error(text);
  }
}
struct DirectAllocation {
  std::int64_t physical{-1};
  DirectAllocation() {
    Require(allocate(0, 0x100000000ull, allocation_bytes, 0x10000, 3,
                     &physical) == 0,
            "Native allocation failed");
  }
  ~DirectAllocation() {
    if (physical >= 0 &&
        release(static_cast<u64>(physical), allocation_bytes)) {
      std::terminate();
    }
  }
  u8 *Map(u64 base, u64 byte_offset, u64 bytes, int rights = 0x33) {
    void *view = reinterpret_cast<void *>(base);
    Require(map(&view, bytes, rights, 0x90, physical + byte_offset, 0) == 0,
            "Native view mapping failed");
    return static_cast<u8 *>(view);
  }
};
DWORD Protection(void *address) {
  MEMORY_BASIC_INFORMATION info{};
  Require(VirtualQuery(address, &info, sizeof(info)) != 0,
          "Native view query failed");
  return info.Protect;
}
// SEH is used only to assert that real logical access violations are NOT
// consumed by cache tracking. There are no C++ objects to unwind here.
bool WriteDenied(volatile u8 *address) {
  __try {
    AuthoredNativeMemoryFaultStore(address);
    return false;
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                  ? EXCEPTION_EXECUTE_HANDLER
                  : EXCEPTION_CONTINUE_SEARCH) {
    return true;
  }
}
bool ReadDenied(volatile u8 *address) {
  __try {
    const volatile auto value = AuthoredNativeMemoryFaultLoad(address);
    (void)value;
    return false;
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                  ? EXCEPTION_EXECUTE_HANDLER
                  : EXCEPTION_CONTINUE_SEARCH) {
    return true;
  }
}
template <typename Function>
bool Unsupported(const char *name, Function operation) {
  try {
    operation();
  } catch (const NativeMemoryTestUnsupported &error) {
    return std::string_view{error.what()} == name;
  }
  return false;
}
struct CachePair {
  Vulkan::Scheduler scheduler;
  Vulkan::Runtime runtime;
  VideoCore::PageManager pages;
  VideoCore::BufferCache buffers;
  VideoCore::TextureCache textures;
  explicit CachePair(const Vulkan::Instance &instance)
      : scheduler{instance}, runtime{instance, scheduler}, pages{nullptr},
        buffers{instance, scheduler, runtime, nullptr, textures, pages},
        textures{instance, scheduler, runtime, nullptr, buffers, pages} {
    scheduler.SetSessionCallback([this] { runtime.FlushBarriers(); });
    scheduler.SetSubmitCallback([this](Vulkan::SubmitInfo &info) {
      buffers.SubmitPendingArenaBinds(info);
    });
  }
  ~CachePair() { scheduler.Finish(); }
};
struct NativeSession {
  GpuWorker worker;
  std::unique_ptr<CachePair> pair;
  std::map<u64, NativeMemoryRangeInfo> mappings;
  std::atomic<unsigned> read_faults{}, write_faults{}, drains{},
      mapped_callbacks{}, change_callbacks{};
  NativeMemoryCacheBackend memory{Hooks()};
  CacheMemoryScope scope{memory};
  NativeCacheHooks Hooks() {
    return {
        .sync = [this](auto command) { worker.Sync(std::move(command)); },
        .is_gpu_thread = [this] { return worker.IsCurrent(); },
        .mapped =
            [this](const auto &range) {
              NativeMemoryRangeInfo queried;
              Require(worker.IsCurrent() &&
                          QueryNativeMemoryRange(range.address, range.bytes,
                                                 &queried) &&
                          queried == range,
                      "Mapped callback ran under the allocator lock or "
                      "received stale metadata");
              pair->buffers.InvalidateMemory(range.address, range.bytes, true);
              pair->textures.UnmapMemory(range.address, range.bytes);
              pair->pages.OnGpuMap(range.address, range.bytes);
              mappings[range.mapping_address] = range;
              ++mapped_callbacks;
            },
        .before_change = [this](auto ranges) { BeforeChange(ranges); },
        .access =
            [this](auto ranges, bool write) {
              Require(worker.IsCurrent(),
                      "Native cache invalidation escaped GPU worker");
              if (write) {
                ++write_faults;
              } else {
                ++read_faults;
              }
              for (const auto &range : ranges) {
                if (write) {
                  pair->buffers.InvalidateMemory(range.address, range.bytes,
                                                 true);
                  pair->textures.InvalidateMemory(range.address, range.bytes);
                } else {
                  pair->buffers.ReadMemory(range.address, range.bytes, false,
                                           true);
                }
              }
            },
        .drain =
            [this] {
              ++drains;
              std::vector<NativeMemoryRangeInfo> all;
              for (const auto &[base, range] : mappings) {
                (void)base;
                all.push_back(range);
              }
              BeforeChange(all);
              pair->scheduler.Finish();
            }};
  }
  void BeforeChange(std::span<const NativeMemoryRangeInfo> ranges) {
    Require(worker.IsCurrent(), "Native mapping lifecycle escaped GPU worker");
    for (const auto &range : ranges) {
      NativeMemoryRangeInfo queried;
      Require(QueryNativeMemoryRange(range.address, range.bytes, &queried),
              "Mapping disappeared before GPU drain");
      // Flush/invalidate every physical alias before destroying a view.
      for (const auto &alias :
           NativeMemoryAliases(range.address, range.bytes)) {
        pair->buffers.InvalidateMemory(alias.address, alias.bytes, true);
        pair->textures.InvalidateMemory(alias.address, alias.bytes);
      }
      pair->textures.UnmapMemory(range.address, range.bytes);
      pair->pages.OnGpuUnmap(range.address, range.bytes);
      mappings.erase(range.mapping_address);
      ++change_callbacks;
    }
    pair->scheduler.Finish();
  }
  explicit NativeSession(const Vulkan::Instance &instance) {
    worker.Sync([&] { pair = std::make_unique<CachePair>(instance); });
    memory.Attach();
  }
  ~NativeSession() {
    memory.Detach();
    worker.Sync([&] { pair.reset(); });
  }
};
struct KernelRead {
  HANDLE file{INVALID_HANDLE_VALUE};
  explicit KernelRead(const std::array<u8, 6> &bytes) {
    wchar_t directory[MAX_PATH], path[MAX_PATH];
    Require(GetTempPathW(MAX_PATH, directory) &&
                GetTempFileNameW(directory, L"ktg", 0, path),
            "Cannot create kernel-read fixture");
    file = CreateFileW(
        path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "Cannot open kernel-read fixture");
    DWORD written{};
    Require(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                      &written, nullptr) &&
                written == bytes.size(),
            "Kernel fixture write failed");
    Require(SetFilePointer(file, 0, nullptr, FILE_BEGIN) == 0,
            "Kernel fixture seek failed");
  }
  ~KernelRead() {
    if (file != INVALID_HANDLE_VALUE) {
      CloseHandle(file);
    }
  }
  void Read(u8 *destination) {
    DWORD read{};
    Require(ReadFile(file, destination, 3, &read, nullptr) && read == 3,
            "Kernel native destination write failed");
  }
};
} // namespace

CacheResults TestNativeMemoryCaches(const Vulkan::Instance &instance) {
  CacheResults result;
  const auto check = [&](bool ok, const char *name) {
    Require(ok, name);
    ++result.checks;
    std::printf("{\"event\":\"native_gpu_memory_case\",\"name\":\"%s\","
                "\"passed\":true}\n",
                name);
  };
  DirectAllocation allocation;
  DirectAllocation scattered;
  auto *primary = allocation.Map(0x3000000000ull, 0, allocation_bytes);
  auto *alias = allocation.Map(0x3100000000ull, offset, 0x10000);
  auto *cpu = primary + offset;
  std::fill_n(reinterpret_cast<u32 *>(cpu), buffer_bytes / 4, 0x11112222);
  auto old_proof = TrackNativeMemoryRange(cpu, buffer_bytes);
  check(old_proof.Unchanged(), "legacy_proof_exists_before_backend_switch");
  NativeSession session{instance};
  auto &pair = *session.pair;
  check(!old_proof.Unchanged() && !TrackNativeMemoryRange(cpu, buffer_bytes),
        "new_backend_retires_old_page_proofs");
  NativeMemoryRangeInfo a, b;
  check(QueryNativeMemoryRange(reinterpret_cast<u64>(cpu), buffer_bytes, &a) &&
            QueryNativeMemoryRange(reinterpret_cast<u64>(alias), buffer_bytes,
                                   &b) &&
            a.physical == b.physical &&
            a.allocation_identity == b.allocation_identity &&
            NativeMemoryAliases(a.address, a.bytes).size() == 2,
        "native_metadata_preserves_physical_alias_identity");
  const auto snapshot_at = [&](u64 address) {
    std::vector<u8> output(buffer_bytes);
    session.worker.Sync([&] {
      const auto [buffer, source] =
          pair.buffers.ObtainBuffer(address, buffer_bytes, false);
      const auto download = pair.runtime.GetStagingPool().Request(
          buffer_bytes, VideoCore::MemoryType::HostCached);
      const vk::BufferCopy copy{.srcOffset = source,
                                .dstOffset = download.offset,
                                .size = buffer_bytes};
      pair.runtime.CopyBuffer(buffer, download.buffer, {&copy, 1});
      pair.scheduler.Finish();
      download.Invalidate();
      std::memcpy(output.data(), download.mapped, buffer_bytes);
    });
    result.verified_bytes += buffer_bytes;
    return output;
  };
  const auto snapshot = [&] { return snapshot_at(a.address); };
  const auto fill_at = [&](u64 address, u32 value) {
    session.worker.Sync([&] {
      const auto [buffer, destination] =
          pair.buffers.ObtainBuffer(address, buffer_bytes, true);
      pair.runtime.FillBuffer(buffer, destination, buffer_bytes, value);
    });
  };
  const auto fill = [&](u32 value) { fill_at(a.address, value); };
  auto *gpu_scratch = allocation.Map(0x3800000000ull, 0x20000, 0x10000, 0x30);
  RecompilerResults compiler;
  session.worker.Sync([&] {
    compiler = TestRecompiler(
        instance, session.memory, primary + 0x20000,
        reinterpret_cast<u64>(gpu_scratch), [&](u64 address, u32 value) {
          const auto [buffer, destination] =
              pair.buffers.ObtainBuffer(address, 4, true);
          pair.runtime.FillBuffer(buffer, destination, 4, value);
        });
  });
  result.recompiler_checks = compiler.checks;
  result.recompiler_modules = compiler.modules;
  result.recompiler_spirv_words = compiler.spirv_words;
  auto pixels = snapshot();
  check(std::memcmp(pixels.data(), cpu, buffer_bytes) == 0,
        "real_allocator_upload_matches_gpu_buffer");
  alias[11] = 0x9a;
  pixels = snapshot();
  check(pixels[11] == 0x9a && session.write_faults > 0,
        "cpu_partial_alias_write_reuploads_resident_buffer");
  // Two CPUs can fault on the same tracked page before either handler finishes.
  // Subsequent stores target distinct bytes, not a C++ data race.
  bool concurrent_equal = true;
  for (unsigned round = 0; round < 16; ++round) {
    std::barrier ready{3};
    std::jthread first{[&] {
      ready.arrive_and_wait();
      static_cast<volatile u8 *>(cpu)[101] = static_cast<u8>(round + 1);
    }};
    std::jthread second{[&] {
      ready.arrive_and_wait();
      static_cast<volatile u8 *>(alias)[102] = static_cast<u8>(round + 33);
    }};
    ready.arrive_and_wait();
    first.join();
    second.join();
    pixels = snapshot();
    concurrent_equal &= pixels[101] == round + 1 && pixels[102] == round + 33;
  }
  check(concurrent_equal, "concurrent_cpu_alias_faults_preserve_both_writes");
  // Simulate entering a handler after a competing handler restored the page.
  session.worker.Sync(
      [&] { pair.buffers.InvalidateMemory(a.address, buffer_bytes, true); });
  check(session.memory.HandleAccess(a.address, 1, true, false),
        "already_resolved_guard_fault_can_resume");
  fill(0x33334444);
  check(Protection(alias) == PAGE_NOACCESS && Protection(cpu) == PAGE_NOACCESS,
        "gpu_write_guards_all_cpu_aliases");
  check(*reinterpret_cast<volatile u32 *>(alias) == 0x33334444 &&
            session.read_faults > 0,
        "cpu_alias_read_drains_gpu_into_same_section");
  fill(0x55667788);
  check(NativeMemoryWritable(alias, buffer_bytes),
        "host_write_checks_logical_not_temporary_permissions");
  auto lease = BeginNativeHostWrite(alias + 5, 3);
  check(bool(lease) && Protection(alias) == PAGE_READWRITE &&
            Protection(cpu) == PAGE_READWRITE,
        "host_write_lease_unprotects_physical_destination");
  check(Unsupported("sceKernelMunmap", [&] { unmap(alias, 0x10000); }) &&
            Unsupported("sceKernelMprotect",
                        [&] { protect(alias, 0x10000, 0x31); }) &&
            Unsupported("sceKernelReleaseDirectMemory",
                        [&] {
                          release(static_cast<u64>(allocation.physical),
                                  allocation_bytes);
                        }),
        "host_write_lease_blocks_unmap_release_and_lost_write_rights");
  session.memory.Protect(a.address, 0x1000, 0);
  check(Protection(alias) == PAGE_READWRITE &&
            !NativeGpuMemoryFault(b.address, true),
        "host_write_excludes_cache_reprotection");
  session.memory.Protect(a.address, 0x1000, 3);
  KernelRead file{{1, 2, 3, 4, 5, 6}};
  file.Read(alias + 5);
  pixels = snapshot();
  check(pixels[5] == 1 && pixels[6] == 2 && pixels[7] == 3 && pixels[8] == 0x88,
        "windows_readfile_updates_gpu_without_destroying_unwritten_data");
  check(Protection(alias) == PAGE_READWRITE,
        "inflight_kernel_write_stays_writable_after_gpu_upload");
  file.Read(alias + 5);
  lease = {};
  pixels = snapshot();
  check(pixels[5] == 4 && pixels[6] == 5 && pixels[7] == 6,
        "host_completion_invalidates_cache_uploaded_midlease");
  fill(0xaabbccdd);
  auto *late = allocation.Map(0x3200000000ull, offset + 0x4000, 0x4000);
  check(Protection(late) == PAGE_NOACCESS,
        "late_16k_alias_inherits_existing_gpu_read_guard");
  check(*reinterpret_cast<volatile u32 *>(late) == 0xaabbccdd,
        "late_alias_fault_resolves_to_original_gpu_buffer");
  auto *ro = allocation.Map(0x3300000000ull, offset, 0x10000, 0x31);
  check(!NativeGpuMemoryFault(reinterpret_cast<u64>(ro), true) &&
            !NativeMemoryWritable(ro, 1) && WriteDenied(ro),
        "genuine_readonly_write_is_not_swallowed_by_gpu_fault_handler");
  auto *none = allocation.Map(0x3400000000ull, offset, 0x10000, 0x30);
  check(!NativeGpuMemoryFault(reinterpret_cast<u64>(none), false) &&
            ReadDenied(none) && WriteDenied(none),
        "genuine_noaccess_faults_remain_access_violations");
  const std::array<u8, 4> gpu_bytes{7, 8, 9, 10};
  check(session.memory.WriteBacking(reinterpret_cast<u64>(none),
                                    gpu_bytes.data(), gpu_bytes.size()) &&
            Protection(none) == PAGE_NOACCESS && cpu[0] == 7,
        "gpu_backing_write_does_not_grant_cpu_access");
  auto *cpu_only = allocation.Map(0x3500000000ull, offset, 0x10000, 3);
  std::array<u8, 4> read{};
  check(!ReadNativeGpuMemory(reinterpret_cast<u64>(cpu_only), read.data(),
                             read.size()) &&
            !WriteNativeGpuMemory(reinterpret_cast<u64>(cpu_only), read.data(),
                                  read.size()),
        "gpu_copy_rejects_cpu_only_mapping");
  read.fill(0xff);
  session.memory.ReadSparse(0x3600000000ull, read.data(), read.size());
  check(read == std::array<u8, 4>{}, "sparse_unmapped_gpu_read_is_zero");
  fill(0x12345678);
  const auto changes_before = session.change_callbacks.load();
  check(unmap(late, 0x4000) == 0 && session.change_callbacks > changes_before &&
            *reinterpret_cast<volatile u32 *>(alias) == 0x12345678,
        "live_unmap_flushes_gpu_data_before_removing_alias");
  late = allocation.Map(0x3200000000ull, offset + 0x4000, 0x4000);
  check(*reinterpret_cast<volatile u32 *>(late) == 0x12345678,
        "remapped_16k_alias_keeps_original_section_data");
  check(protect(ro, 0x10000, 0x33) == 0 && NativeMemoryWritable(ro, 1),
        "logical_permission_change_rebinds_cache_metadata");
  // One cache block covers two 16-KiB views, disjoint section offsets, and
  // unmapped holes at either end. Populate the second view after first upload.
  auto *left = scattered.Map(0x3700004000ull, 0, 0x4000);
  std::fill_n(left, 0x4000, 0x12);
  auto fragmented = snapshot_at(reinterpret_cast<u64>(left));
  check(std::all_of(fragmented.begin(), fragmented.begin() + 0x4000,
                    [](auto byte) { return byte == 0x12; }) &&
            std::all_of(fragmented.begin() + 0x4000, fragmented.end(),
                        [](auto byte) { return byte == 0; }),
        "cache_block_upload_handles_unmapped_native_holes");
  auto *right = scattered.Map(0x3700008000ull, 0x8000, 0x4000);
  std::fill_n(right, 0x4000, 0x34);
  check(session.memory.IsMapped(reinterpret_cast<u64>(left), buffer_bytes),
        "mapping_query_spans_adjacent_native_views");
  fragmented = snapshot_at(reinterpret_cast<u64>(left));
  check(std::all_of(fragmented.begin(), fragmented.begin() + 0x4000,
                    [](auto byte) { return byte == 0x12; }) &&
            std::all_of(fragmented.begin() + 0x4000, fragmented.end(),
                        [](auto byte) { return byte == 0x34; }),
        "mapping_a_former_sparse_hole_invalidates_cached_zeros");
  fill_at(reinterpret_cast<u64>(left), 0x10203040);
  check(*reinterpret_cast<volatile u32 *>(right) == 0x10203040 &&
            *reinterpret_cast<volatile u32 *>(left) == 0x10203040,
        "gpu_readback_spans_adjacent_views_with_disjoint_section_offsets");
  fill(0xfedcba98);
  const auto old_identity = a.allocation_identity;
  const auto old_physical = allocation.physical;
  check(release(static_cast<u64>(allocation.physical), allocation_bytes) == 0 &&
            !QueryNativeMemoryRange(a.address, 1, &b),
        "live_release_drains_and_removes_all_virtual_aliases");
  allocation.physical = -1;
  Require(allocate(0, 0x100000000ull, allocation_bytes, 0x10000, 3,
                   &allocation.physical) == 0,
          "Native physical reuse allocation failed");
  primary = allocation.Map(0x3000000000ull, 0, allocation_bytes);
  cpu = primary + offset;
  alias = allocation.Map(0x3100000000ull, offset, 0x10000);
  none = allocation.Map(0x3400000000ull, offset, 0x10000, 0x30);
  Require(QueryNativeMemoryRange(reinterpret_cast<u64>(cpu), buffer_bytes, &a),
          "Native reused mapping query failed");
  check(allocation.physical == old_physical &&
            a.allocation_identity != old_identity,
        "physical_address_reuse_gets_new_allocation_identity");
  pixels = snapshot();
  check(std::all_of(pixels.begin(), pixels.end(),
                    [](auto byte) { return byte == 0; }),
        "reused_virtual_and_physical_addresses_do_not_reuse_old_gpu_contents");
  fill(0x87654321);
  auto final_lease = BeginNativeHostWrite(alias + 17, 1);
  check(bool(final_lease), "detach_test_has_live_host_write_lease");
  auto detached =
      std::async(std::launch::async, [&] { session.memory.Detach(); });
  check(detached.wait_for(std::chrono::milliseconds(30)) ==
                std::future_status::timeout &&
            session.drains == 0,
        "backend_detach_waits_for_actual_host_write_completion");
  alias[17] = 0xee;
  final_lease = {};
  detached.get();
  check(session.drains == 1 && cpu[17] == 0xee &&
            Protection(alias) == PAGE_READWRITE &&
            Protection(none) == PAGE_NOACCESS,
        "detach_drains_then_restores_only_guest_logical_rights");
  check(!TrackNativeMemoryRange(cpu, buffer_bytes) && !old_proof.Unchanged(),
        "backend_detach_never_revives_stale_legacy_proofs");
  const auto faults = session.memory.FaultCounts();
  result.read_faults = faults.first;
  result.write_faults = faults.second;
  return result;
}
} // namespace Ketsui::GpuCore
