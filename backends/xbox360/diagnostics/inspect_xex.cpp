// Bounded static census. Never launches a guest thread or creates a window.
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/export_resolver.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>
#include <rex/system/xmemory.h>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using rex::X_STATUS;

namespace {
bool Within(const fs::path& path, const fs::path& root) {
    auto p = path.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++p) {
        if (p == path.end() || CompareStringOrdinal(p->c_str(), -1, r->c_str(), -1, TRUE) != CSTR_EQUAL) return false;
    }
    return true;
}
void CheckStatus(X_STATUS result, const char* action) {
    if (result != X_STATUS_SUCCESS) throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}
Json Allocation(rex::Runtime& runtime, std::uint32_t address) {
    rex::memory::HeapAllocationInfo info{};
    if (!runtime.memory()->LookupHeap(address)->QueryRegionInfo(address, &info))
        throw std::runtime_error("Cannot inspect loaded image allocation");
    return Json{{"base",info.allocation_base}, {"bytes",info.allocation_size},
        {"state",info.state}, {"protect",info.protect}};
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 4 || argc > 6) throw std::runtime_error("Usage: inspect <game-data> <module> <NEW-output> [--dump-sections] [--game=sdoj]");
        bool dump{}, sdoj{}, gameSpecified{};
        for (int index = 4; index < argc; ++index) {
            const std::wstring option = argv[index];
            if (option == L"--dump-sections" && !dump) dump = true;
            else if (option == L"--game=sdoj" && !gameSpecified) { sdoj = true; gameSpecified = true; }
            else throw std::runtime_error("Unknown or repeated option");
        }
        const std::wstring selected = argv[2];
        constexpr std::array<const wchar_t*, 8> g2Modules{L"default.xex", L"dat00.xex", L"dat11.xex", L"dat12.xex",
            L"dat15.xex", L"dat162.xex", L"dat163.xex", L"dat164.xex"};
        constexpr std::array<const wchar_t*, 3> sdojModules{L"default.xex", L"CA022100.bin", L"CA022110.bin"};
        const std::span<const wchar_t* const> modules = sdoj ? std::span<const wchar_t* const>(sdojModules) : std::span<const wchar_t* const>(g2Modules);
        bool known{};
        for (auto name : modules) known |= selected == name;
        if (!known) throw std::runtime_error("Expected a declared module filename, not an arbitrary guest path");
        const std::string moduleName(selected.begin(), selected.end());
        const fs::path game = fs::canonical(argv[1]);
        const fs::path output = fs::weakly_canonical(fs::absolute(argv[3]));
        if (Within(output, game) || Within(game, output) || fs::exists(output))
            throw std::runtime_error("Output must be NEW and separate from game data");
        fs::create_directories(output);
        rex::InitLoggingEarly();
        auto logging = rex::BuildLogConfig(nullptr, "warn", {});
        logging.log_to_console = true;
        rex::InitLogging(logging);
        rex::Runtime runtime(game, output / "user-data", fs::path{}, output / "cache", output / "metadata");
        CheckStatus(runtime.Setup(rex::RuntimeConfig{.kernel_init=rex::kernel::InitializeKernel, .tool_mode=true}), "Tool setup");
        if (!runtime.is_tool_mode() || runtime.graphics_system() || runtime.audio_system() || runtime.input_system())
            throw std::runtime_error("Analysis unexpectedly initialized a presentation, audio or input system");
        CheckStatus(runtime.LoadXexImage("game:\\default.xex"), "Main XEX load");
        auto owner = runtime.kernel_state()->GetExecutableModule();
        const std::uint32_t titleId = sdoj ? 0x435A07E7u : 0x435A07D8u;
        auto checkTitle = [titleId](const auto& loaded) {
            if (!loaded || !loaded->xex_module()) throw std::runtime_error("Missing XEX image");
            const auto* execution = loaded->xex_module()->opt_execution_info();
            if (!execution || std::uint32_t(execution->title_id) != titleId)
                throw std::runtime_error("Loaded image belongs to another title");
        };
        checkTitle(owner);
        const auto mainBase = owner->xex_module()->base_address();
        const auto mainAllocation = Allocation(runtime, mainBase);
        if (moduleName != "default.xex") {
            owner = runtime.kernel_state()->LoadUserModule("game:\\" + moduleName, false);
            if (!owner) throw std::runtime_error("Overlay load failed");
            checkTitle(owner);
        }
        if (Allocation(runtime, mainBase) != mainAllocation)
            throw std::runtime_error("Overlay load invalidated the main image allocation");
        std::size_t loadCycles = 1;
        if (moduleName != "default.xex") {
            const auto overlayBase = owner->xex_module()->base_address();
            // Loading without calling DllMain permits a static lifecycle test:
            // exercise ownership/reuse, never gameplay or guest dispatch.
            for (; loadCycles < 3; ++loadCycles) {
                runtime.kernel_state()->UnloadUserModule(owner, false);
                owner.reset();
                if (Allocation(runtime, overlayBase)["state"] != 0)
                    throw std::runtime_error("Overlay allocation remained live after unload");
                if (Allocation(runtime, mainBase) != mainAllocation)
                    throw std::runtime_error("Overlay unload changed the main allocation");
                owner = runtime.kernel_state()->LoadUserModule("game:\\" + moduleName, false);
                if (!owner || Allocation(runtime, mainBase) != mainAllocation)
                    throw std::runtime_error("Static overlay reload failed");
                checkTitle(owner);
            }
            const auto overlayAllocation = Allocation(runtime, overlayBase);
            const auto codeAddress = owner->entry_point();
            std::array<unsigned char, 32> codeBefore{};
            std::memcpy(codeBefore.data(), runtime.memory()->TranslateVirtual(codeAddress), codeBefore.size());
            const auto other = sdoj ? (moduleName == "CA022100.bin" ? "CA022110.bin" : "CA022100.bin") :
                (moduleName == "dat00.xex" ? "dat12.xex" : "dat00.xex");
            std::cout << "Expected negative test: loading " << other << " into a live overlay range\n";
            auto rejected = runtime.kernel_state()->LoadUserModule(std::string("game:\\") + other, false);
            if (rejected || Allocation(runtime, overlayBase) != overlayAllocation ||
                Allocation(runtime, mainBase) != mainAllocation ||
                std::memcmp(codeBefore.data(), runtime.memory()->TranslateVirtual(codeAddress), codeBefore.size()))
                throw std::runtime_error("Overlapping image load was not rejected safely");
        }
        auto* module = owner->xex_module();
        if (!module) throw std::runtime_error("Module has no XEX image");
        const auto imageBase = module->base_address();
        const auto imageSize = module->image_size();
        if (!imageSize || imageSize > 128 * 1024 * 1024 || std::uint64_t(imageBase) + imageSize > 0x100000000ull)
            throw std::runtime_error("Unexpected image bounds");
        Json report{{"format",1}, {"game",sdoj ? "sdoj" : "espgaluda2"}, {"module",moduleName}, {"toolMode",true}, {"guestExecution",false},
            {"gpuInitialized",false}, {"audioInitialized",false}, {"inputInitialized",false},
            {"imageBase",imageBase}, {"imageBytes",imageSize}, {"entryPoint",module->entry_point()},
            {"exportTable",module->export_table_address()}, {"sections",Json::array()}, {"imports",Json::array()}};
        report["mainImageAllocationPreserved"] = true;
        report["staticLoadCycles"] = loadCycles;
        report["overlappingLoadRejectedSafely"] = moduleName != "default.xex";
        if (const auto* execution = module->opt_execution_info()) report["titleId"] = std::uint32_t(execution->title_id);
        std::uint64_t totalSectionBytes{};
        std::size_t index{};
        for (const auto& section : module->binary_sections()) {
            const std::uint64_t end = std::uint64_t(section.virtual_address) + section.virtual_size;
            const bool inImage = section.virtual_address >= imageBase &&
                end <= std::uint64_t(imageBase) + imageSize;
            if (!inImage && section.executable)
                throw std::runtime_error("Executable section is outside the loaded image");
            if (inImage) totalSectionBytes += section.virtual_size;
            if (totalSectionBytes > 128 * 1024 * 1024 ||
                (inImage && section.virtual_size && !section.host_data))
                throw std::runtime_error("Invalid mapped section bounds");
            Json item{{"name",section.name}, {"address",section.virtual_address}, {"bytes",section.virtual_size},
                {"executable",section.executable}, {"writable",section.writable}, {"inImage",inImage}};
            // XEXs can retain PE section headers for stripped metadata. Match
            // BinaryView's mapping rule; report the header without reading it.
            if (!inImage) item["excludedReason"] = "outside-loaded-XEX-image";
            if (dump && inImage) {
                const auto filename = std::to_string(index) + ".section.bin";
                std::ofstream bytes(output / filename, std::ios::binary);
                bytes.exceptions(std::ios::badbit | std::ios::failbit);
                bytes.write(reinterpret_cast<const char*>(section.host_data), section.virtual_size);
                item["dump"] = filename;
            }
            report["sections"].push_back(std::move(item));
            ++index;
        }
        std::size_t registered{}, declared{}, stub{}, absent{};
        for (const auto& library : *module->import_libraries()) {
            for (const auto& symbol : library.imports) {
                const auto* entry = runtime.export_resolver()->GetExportByOrdinal(library.name, std::uint16_t(symbol.ordinal));
                Json item{{"library",library.name}, {"ordinal",symbol.ordinal},
                    {"valueAddress",symbol.value_address}, {"thunkAddress",symbol.thunk_address},
                    {"sdkRegistered",entry != nullptr}};
                if (entry) {
                    ++registered;
                    item["name"] = entry->name;
                    item["sdkTags"] = entry->tags;
                    item["declaredImplemented"] = entry->is_implemented();
                    item["declaredStub"] = bool(entry->tags & rex::runtime::ExportTag::kStub);
                    declared += entry->is_implemented();
                    stub += bool(entry->tags & rex::runtime::ExportTag::kStub);
                } else { ++absent; }
                report["imports"].push_back(std::move(item));
            }
        }
        report["importSummary"] = Json{{"registered",registered}, {"declaredImplemented",declared},
            {"declaredStub",stub}, {"unregistered",absent}, {"functionalBehaviorVerified",false}};
        owner.reset();
        runtime.Shutdown();
        std::ofstream summary(output / "census.json", std::ios::binary);
        summary.exceptions(std::ios::badbit | std::ios::failbit);
        summary << report.dump(2) << '\n';
        summary.close();
        std::cout << Json{{"module",moduleName}, {"sections",index}, {"imports",registered + absent},
            {"unregistered",absent}, {"guestExecution",false}}.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Static census failed: " << error.what() << '\n';
        return 2;
    }
}
