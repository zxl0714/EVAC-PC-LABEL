#include "write_tracker.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace kt::memory {
__declspec(noinline) void AuthoredWriteTrackerFaultStore(unsigned char* address) {
    *static_cast<volatile unsigned char*>(address) ^= 0xff;
}
bool WriteTrackerPublicationRaceSelfTest(WriteTracker&, unsigned char*, std::uint64_t, std::uint64_t);
bool WriteTrackerProtectionRaceSelfTest(WriteTracker&, const WriteTracker::View&, unsigned char*);
namespace {
LONG WINAPI TestUnhandled(EXCEPTION_POINTERS* info) {
    std::fprintf(stderr, "write_tracker_test_fault tid=%lu code=0x%lx rva=0x%llx address=0x%llx access=%llu\n",
        GetCurrentThreadId(), info->ExceptionRecord->ExceptionCode,
        info->ContextRecord->Rip - reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr)),
        info->ExceptionRecord->NumberParameters >= 2 ? info->ExceptionRecord->ExceptionInformation[1] : 0,
        info->ExceptionRecord->NumberParameters >= 1 ? info->ExceptionRecord->ExceptionInformation[0] : 0);
    std::fflush(stderr); TerminateProcess(GetCurrentProcess(), 22); return EXCEPTION_EXECUTE_HANDLER;
}
struct Mapping {
    HANDLE section{};
    unsigned char *first{}, *alias{}, *partial{};
    Mapping() {
        section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 131072, nullptr);
        if (!section) { throw std::runtime_error("Cannot create authored write-watch fixture"); }
        first = static_cast<unsigned char*>(MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, 131072));
        alias = static_cast<unsigned char*>(MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, 131072));
        partial = static_cast<unsigned char*>(MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 65536, 65536));
        if (!first || !alias || !partial) { throw std::runtime_error("Cannot map authored write-watch aliases"); }
    }
    ~Mapping() { if (partial) UnmapViewOfFile(partial); if (alias) UnmapViewOfFile(alias); if (first) UnmapViewOfFile(first); if (section) CloseHandle(section); }
};
unsigned PageProtection(void* address) { MEMORY_BASIC_INFORMATION info{}; return VirtualQuery(address, &info, sizeof(info)) ? info.Protect : 0; }
__declspec(noinline) void Write(unsigned char* address, unsigned char value) { *static_cast<volatile unsigned char*>(address) = value; }
}
bool WriteTrackerSelfTest() {
    struct FaultDiagnostics {
        LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(TestUnhandled);
        ~FaultDiagnostics() { SetUnhandledExceptionFilter(previous); }
    } diagnostics;
    bool passed = true;
    const auto check = [&](const char* name, bool result) {
        passed &= result; std::printf("{\"event\":\"write_tracker_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
    };
    WriteStamp survivor;
    Mapping mapping;
    {
        WriteTracker tracker{16, 8};
        const auto first = tracker.AddView(mapping.first, 131072, 0, 1, PAGE_READWRITE);
        const auto alias = tracker.AddView(mapping.alias, 131072, 0, 1, PAGE_READWRITE);
        const auto partial = tracker.AddView(mapping.partial, 65536, 65536, 1, PAGE_READWRITE);
        check("explicit_shared_views_register_without_changing_contents", first && alias && partial && mapping.first[0] == 0 && mapping.alias[65537] == 0);
        auto stamp = tracker.Track(1, 65536 + 129, 1024);
        check("unaligned_subrange_arms_every_writable_physical_alias", stamp.Unchanged() && PageProtection(mapping.first + 65536) == PAGE_READONLY &&
            PageProtection(mapping.alias + 65536) == PAGE_READONLY && PageProtection(mapping.partial) == PAGE_READONLY && PageProtection(mapping.first) == PAGE_READWRITE);
        check("reads_and_repeated_observations_do_not_dirty_or_rearm", mapping.alias[65536 + 129] == 0 &&
            tracker.Track(1, 65536 + 129, 1024).Unchanged() && stamp.Unchanged() && tracker.Stats().arms == 1 && tracker.Stats().faults == 0);
        Write(mapping.alias + 65536 + 129, 73);
        check("alias_write_fault_resumes_and_invalidates_original_stamp", !stamp.Unchanged() && mapping.first[65536 + 129] == 73 && tracker.Stats().faults == 1);
        const auto next = tracker.Track(1, 65536 + 129, 1024);
        check("rearming_never_revives_an_old_generation", next.Unchanged() && !stamp.Unchanged());
        Write(mapping.partial + 130, 19);
        check("partial_alias_write_uses_its_physical_offset", !next.Unchanged() && mapping.first[65536 + 130] == 19);
        auto rounded = tracker.Track(1, 65536 + 129, 1024);
        Write(mapping.first + 65536, 27);
        check("writes_to_page_padding_also_invalidate_conservatively", !rounded.Unchanged());
        auto a = tracker.Track(1, 32768, 8192);
        auto b = tracker.Track(1, 32768 + 4096, 8192);
        Write(mapping.first + 32768, 51);
        check("unprotecting_one_range_invalidates_physically_overlapping_ranges", !a.Unchanged() && !b.Unchanged());
        a = tracker.Track(1, 32768, 8192); b = tracker.Track(1, 32768 + 4096, 8192);
        Write(mapping.alias + 32768 + 9000, 52);
        check("overlap_rearming_keeps_the_other_ranges_guarded_tail_safe", !b.Unchanged() && mapping.first[32768 + 9000] == 52);
        auto distinct = tracker.Track(1, 0, 4096);
        auto other = tracker.Track(1, 65536, 4096);
        Write(mapping.partial, 61);
        check("nonoverlapping_physical_ranges_keep_their_generations", distinct.Unchanged() && !other.Unchanged());
        auto old = tracker.Track(1, 0, 4096);
        tracker.RemoveView(alias);
        check("alias_removal_invalidates_and_restores_its_original_permissions", !old.Unchanged() && PageProtection(mapping.alias) == PAGE_READWRITE);
        const auto alias_again = tracker.AddView(mapping.alias, 131072, 0, 1, PAGE_READWRITE);
        old = tracker.Track(1, 0, 4096);
        check("new_alias_is_included_when_rearming_a_preexisting_range", alias_again && old.Unchanged() && PageProtection(mapping.alias) == PAGE_READONLY);
        check("guest_readonly_protection_is_not_treated_as_a_tracking_fault", tracker.ProtectView(first, PAGE_READONLY) &&
            !WriteTracker::HandleWriteFault(mapping.first) && PageProtection(mapping.first) == PAGE_READONLY);
        check("guest_noaccess_view_is_neither_made_readable_nor_handled_as_writable", tracker.ProtectView(first, PAGE_NOACCESS) &&
            !WriteTracker::HandleWriteFault(mapping.first) && PageProtection(mapping.first) == PAGE_NOACCESS);
        check("restoring_guest_writable_protection_invalidates_then_rearms", tracker.ProtectView(first, PAGE_READWRITE) &&
            !old.Unchanged() && tracker.Track(1, 0, 4096).Unchanged());
        unsigned char unrelated{};
        check("unregistered_host_memory_is_never_handled", !WriteTracker::HandleWriteFault(&unrelated));
        check("unknown_overflowed_and_unmapped_ranges_have_no_valid_proof", !tracker.Track(2, 0, 16) && !tracker.Track(1, UINT64_MAX - 1, 4) &&
            !tracker.Track(1, 131071, 2) && !tracker.Track(1, 0, 0));
        const auto partial_stamp = tracker.Track(1, 65536, 8192);
        tracker.RemoveView(partial);
        check("removed_alias_is_restored_and_invalidates_cached_proof", !partial_stamp.Unchanged() && PageProtection(mapping.partial) == PAGE_READWRITE);
        auto concurrent = tracker.Track(1, 0, 4096);
        std::atomic<bool> start{}, done{};
        std::thread writer([&] { while (!start.load()) { SwitchToThread(); } for (unsigned i = 0; i < 2000; ++i) { Write(mapping.alias + 7, static_cast<unsigned char>(i)); } done.store(true); });
        start.store(true);
        unsigned rearms{};
        do { (void)tracker.Track(1, 0, 4096); ++rearms; } while (!done.load());
        writer.join();
        const auto stable = tracker.Track(1, 0, 4096);
        check("concurrent_writer_and_rearming_complete_without_a_handler_lock", !concurrent.Unchanged() && stable.Unchanged() && mapping.first[7] == static_cast<unsigned char>(1999) && rearms > 0);
        check("restore_finishing_after_rearm_invalidates_the_intermediate_snapshot", WriteTrackerPublicationRaceSelfTest(tracker, mapping.alias, 1, 0));
        check("concurrent_restore_never_overwrites_guest_noaccess_protection", WriteTrackerProtectionRaceSelfTest(tracker, alias_again, mapping.alias));
        tracker.RemoveView(alias_again); tracker.RemoveView(first);
        check("removing_last_view_prevents_generation_reuse", !stable.Unchanged() && !tracker.Track(1, 0, 4096));
        const auto reused = tracker.AddView(mapping.first, 131072, 0, 2, PAGE_READWRITE);
        survivor = tracker.Track(2, 0, 4096);
        check("reused_physical_offsets_need_a_new_allocation_identity", reused && survivor.Unchanged() && !tracker.Track(1, 0, 4096));
        tracker.Disable();
        check("disable_restores_protections_and_forces_fallback", !survivor.Unchanged() && !tracker.Track(2, 0, 4096) &&
            PageProtection(mapping.first) == PAGE_READWRITE && !tracker.Stats().enabled);
        tracker.RemoveView(reused);
    }
    check("stamps_outlive_tracker_only_as_invalid_proofs", !survivor.Unchanged());
    WriteTracker::HostWrite surviving_write;
    {
        WriteTracker tracker{4, 8};
        const auto first = tracker.AddView(mapping.first, 131072, 0, 5, PAGE_READWRITE);
        const auto alias = tracker.AddView(mapping.alias, 131072, 0, 5, PAGE_READWRITE);
        auto original = tracker.Track(5, 65536, 4096);
        const auto unrelated = tracker.Track(5, 0, 4096);
        check("logical_write_validation_accepts_guarded_rw_without_dirtying_proofs", tracker.Writable(mapping.first + 65537, 7) &&
            original.Unchanged() && PageProtection(mapping.first + 65536) == PAGE_READONLY);
        auto write = tracker.BeginHostWrite(mapping.alias + 65537, 7);
        check("host_write_restores_destination_alias_and_invalidates_physical_proof", write && !original.Unchanged() && unrelated.Unchanged() &&
            PageProtection(mapping.alias + 65536) == PAGE_READWRITE && PageProtection(mapping.first + 65536) == PAGE_READONLY);
        check("inflight_host_write_blocks_rearming_including_shared_page_padding", !tracker.Track(5, 65536, 4096) &&
            !tracker.Track(5, 65536 + 99, 1) && tracker.Track(5, 8192, 4096).Unchanged());
        const auto before_faults = tracker.Stats().faults;
        const unsigned char value = 113; SIZE_T written{};
        const bool copied = WriteProcessMemory(GetCurrentProcess(), mapping.alias + 65537, &value, 1, &written) && written == 1;
        check("host_kernel_copy_updates_shared_bytes_without_needing_a_cpu_write_fault", copied && mapping.first[65537] == value &&
            tracker.Stats().faults == before_faults && !tracker.Track(5, 65536, 4096));
        auto nested = tracker.BeginHostWrite(mapping.first + 65538, 2);
        write = {};
        check("nested_host_writes_block_rearm_until_the_last_lease_completes", nested && !tracker.Track(5, 65536, 4096));
        auto moved = std::move(nested);
        check("moving_host_write_lease_keeps_its_exclusion_and_invalidates_source", moved && !nested && !tracker.Track(5, 65536, 4096));
        moved = {};
        original = tracker.Track(5, 65536, 4096);
        check("completed_host_write_can_be_rearmed_with_current_bytes", original.Unchanged() && mapping.first[65537] == value &&
            PageProtection(mapping.alias + 65536) == PAGE_READONLY);
        wchar_t directory[MAX_PATH + 1]{}, path[MAX_PATH + 1]{};
        HANDLE file = INVALID_HANDLE_VALUE;
        if (GetTempPathW(MAX_PATH, directory) && GetTempFileNameW(directory, L"ktw", 0, path)) {
            file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (file == INVALID_HANDLE_VALUE) { DeleteFileW(path); }
        }
        bool read_ok{};
        if (file != INVALID_HANDLE_VALUE) {
            const unsigned char payload[]{9, 8, 7, 6, 5}; DWORD count{};
            if (WriteFile(file, payload, sizeof(payload), &count, nullptr) && count == sizeof(payload) && SetFilePointer(file, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
                auto reading = tracker.BeginHostWrite(mapping.alias + 65537, sizeof(payload));
                read_ok = reading && ReadFile(file, mapping.alias + 65537, sizeof(payload), &count, nullptr) && count == sizeof(payload) &&
                    !std::memcmp(mapping.first + 65537, payload, sizeof(payload)) && !tracker.Track(5, 65536, 4096);
            }
            CloseHandle(file);
        }
        check("synchronous_readfile_into_guarded_shared_section_uses_host_write_lease", read_ok && !original.Unchanged() && tracker.Track(5, 65536, 4096).Unchanged());
        check("real_guest_readonly_destination_is_not_granted_host_write_access", tracker.ProtectView(alias, PAGE_READONLY) &&
            !tracker.Writable(mapping.alias, 1) && !tracker.BeginHostWrite(mapping.alias, 1) && PageProtection(mapping.alias) == PAGE_READONLY);
        check("real_guest_noaccess_destination_remains_inaccessible", tracker.ProtectView(alias, PAGE_NOACCESS) &&
            !tracker.Writable(mapping.alias, 1) && !tracker.BeginHostWrite(mapping.alias, 1) && PageProtection(mapping.alias) == PAGE_NOACCESS);
        check("invalid_host_write_span_has_no_partial_unprotect_side_effect", !tracker.BeginHostWrite(mapping.alias + 131071, 2) &&
            !tracker.BeginHostWrite(reinterpret_cast<void*>(UINTPTR_MAX - 1), 8) && !tracker.BeginHostWrite(nullptr, 1));
        check("zero_length_host_write_does_not_require_a_destination", tracker.Writable(nullptr, 0) && tracker.BeginHostWrite(nullptr, 0));
        auto* host = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        bool host_ok{};
        if (host) {
            auto ordinary = tracker.BeginHostWrite(host, 4096);
            DWORD previous{};
            host_ok = ordinary && tracker.Writable(host, 4096) && VirtualProtect(host, 4096, PAGE_READONLY, &previous) &&
                !tracker.Writable(host, 1) && !tracker.BeginHostWrite(host, 1);
            ordinary = {}; VirtualFree(host, 0, MEM_RELEASE);
        }
        check("unregistered_host_buffers_use_actual_windows_write_permissions", host_ok);
        check("writable_alias_restoration_does_not_change_another_alias_guest_protection", tracker.ProtectView(alias, PAGE_READWRITE) &&
            tracker.ProtectView(first, PAGE_READONLY) && tracker.BeginHostWrite(mapping.alias, 1) && PageProtection(mapping.first) == PAGE_READONLY);
        tracker.ProtectView(first, PAGE_READWRITE);
        auto busy = tracker.BeginHostWrite(mapping.alias + 65536, 4096);
        std::atomic<bool> bad{};
        std::thread observer([&] { for (unsigned i = 0; i < 1000; ++i) { if (tracker.Track(5, 65536, 4096)) { bad.store(true); } } });
        for (unsigned i = 0; i < 1000; ++i) { Write(mapping.alias + 65536, static_cast<unsigned char>(i)); }
        observer.join(); busy = {};
        check("concurrent_observers_cannot_rearm_while_a_host_write_is_inflight", !bad.load() && tracker.Track(5, 65536, 4096).Unchanged() && mapping.first[65536] == static_cast<unsigned char>(999));
        surviving_write = tracker.BeginHostWrite(mapping.first + 65536, 4096);
    }
    surviving_write = {};
    check("host_lease_retirement_is_safe_after_the_tracker_has_disabled_and_destroyed", PageProtection(mapping.first + 65536) == PAGE_READWRITE);
    {
        WriteTracker tracker{3, 1};
        const auto view = tracker.AddView(mapping.first, 131072, 0, 3, PAGE_READWRITE);
        auto held = tracker.Track(3, 0, 4096);
        check("range_capacity_keeps_active_proofs_and_falls_back_for_new_ranges", held.Unchanged() && !tracker.Track(3, 8192, 4096));
        held = {};
        const auto replacement = tracker.Track(3, 8192, 4096);
        check("unused_range_slots_can_be_retired_and_reused", replacement.Unchanged() && PageProtection(mapping.first) == PAGE_READWRITE && PageProtection(mapping.first + 8192) == PAGE_READONLY);
        tracker.RemoveView(view);
    }
    {
        WriteTracker tracker{1, 2};
        const auto view = tracker.AddView(mapping.first, 131072, 0, 4, PAGE_READWRITE);
        auto stamp = tracker.Track(4, 0, 4096);
        const auto refused = tracker.AddView(mapping.alias, 131072, 0, 4, PAGE_READWRITE);
        check("view_capacity_failure_disables_tracking_instead_of_missing_alias_writes", !refused && !stamp.Unchanged() &&
            !tracker.Stats().enabled && PageProtection(mapping.first) == PAGE_READWRITE);
        tracker.RemoveView(view);
    }
    std::printf("{\"event\":\"write_tracker_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}
}
