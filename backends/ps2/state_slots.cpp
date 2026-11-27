#include "state_slots.h"
#include "state_archive.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <io.h>
#include <memory>

namespace cave::ps2 {
namespace {
constexpr uint32_t MaxSnapshot = 256u << 20;
constexpr std::array<uint8_t, 8> Magic{'E','V','A','C','P','S','2',0};

std::string TwoDigits(unsigned slot) {
    if (!slot || slot > StateSlots::Count) throw std::out_of_range("PS2 state slot outside 01-99");
    return {char('0' + slot / 10), char('0' + slot % 10)};
}

void WriteBytes(FILE* file, std::span<const uint8_t> bytes) {
    if (std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size())
        throw std::runtime_error("Could not write PS2 state file");
}

template<class Write>
void AtomicWrite(const std::filesystem::path& path, Write write) {
    std::filesystem::create_directories(path.parent_path());
    static std::atomic<uint64_t> counter{};
    std::filesystem::path temporary;
    FILE* raw{};
    do {
        temporary = path.native() + L".pending-" + std::to_wstring(GetCurrentProcessId()) +
                    L"-" + std::to_wstring(++counter);
        const auto error = _wfopen_s(&raw, temporary.c_str(), L"wbx");
        if (error != 0 && error != EEXIST) throw std::runtime_error("Could not create PS2 state file");
    } while (!raw);
    struct Cleanup {
        const std::filesystem::path& path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{temporary};
    std::unique_ptr<FILE, decltype(&std::fclose)> file(raw, std::fclose);
    write(file.get());
    if (std::fflush(file.get()) != 0 || _commit(_fileno(file.get())) != 0)
        throw std::runtime_error("Could not flush PS2 state file");
    if (std::fclose(file.release()) != 0 ||
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not replace PS2 state file");
}

std::vector<uint8_t> Header(std::string_view identity) {
    std::vector<uint8_t> bytes;
    StateWriter writer(bytes);
    writer.Bytes(Magic);
    writer.Field(uint32_t{1});
    writer.String(identity);
    return bytes;
}
}

StateSlots::StateSlots(std::filesystem::path directory, std::string programIdentity)
    : directory_(std::move(directory)), identity_(std::move(programIdentity)) {
    if (directory_.empty() || identity_.empty()) throw std::invalid_argument("Missing PS2 state identity");
}

std::filesystem::path StateSlots::Path(unsigned slot) const {
    return directory_ / ("slot-" + TwoDigits(slot) + ".state");
}

std::array<std::optional<std::filesystem::file_time_type>, StateSlots::Count> StateSlots::List() const {
    std::array<std::optional<std::filesystem::file_time_type>, Count> slots{};
    if (!std::filesystem::exists(directory_)) return slots;
    for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
        const auto name = entry.path().filename().native();
        if (name.size() != 13 || !name.starts_with(L"slot-") || !name.ends_with(L".state") ||
            name[5] < L'0' || name[5] > L'9' || name[6] < L'0' || name[6] > L'9') continue;
        const unsigned slot = (name[5] - L'0') * 10 + name[6] - L'0';
        if (slot && entry.is_regular_file()) slots[slot - 1] = entry.last_write_time();
    }
    return slots;
}

void StateSlots::Save(unsigned slot, std::span<const uint8_t> state) const {
    if (state.empty() || state.size() > MaxSnapshot) throw std::invalid_argument("Invalid PS2 snapshot size");
    auto header = Header(identity_);
    StateWriter(header).Field(static_cast<uint32_t>(state.size()));
    AtomicWrite(Path(slot), [&](FILE* file) {
        WriteBytes(file, header);
        mz_stream stream{};
        if (mz_deflateInit(&stream, MZ_BEST_SPEED) != MZ_OK)
            throw std::runtime_error("Could not initialize PS2 state compression");
        struct End { mz_stream& stream; ~End() { mz_deflateEnd(&stream); } } end{stream};
        stream.next_in = state.data();
        stream.avail_in = static_cast<unsigned>(state.size());
        std::array<uint8_t, 64 * 1024> buffer;
        for (;;) {
            stream.next_out = buffer.data();
            stream.avail_out = static_cast<unsigned>(buffer.size());
            const int status = mz_deflate(&stream, MZ_FINISH);
            if (status != MZ_OK && status != MZ_STREAM_END) throw std::runtime_error("Cannot compress PS2 state");
            WriteBytes(file, std::span(buffer).first(buffer.size() - stream.avail_out));
            if (status == MZ_STREAM_END) break;
        }
    });
}

std::vector<uint8_t> StateSlots::Read(unsigned slot) const {
    return ReadProgram(slot,{}).bytes;
}

StateSlots::SavedProgram StateSlots::ReadProgram(unsigned slot,std::span<const std::string_view> discPrograms) const {
    std::ifstream input(Path(slot), std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Could not open PS2 state file");
    const auto length = static_cast<std::streamoff>(input.tellg());
    input.seekg(0);
    std::array<uint8_t,Magic.size()> magic{};
    uint32_t version{},nameSize{};
    if (!input.read(reinterpret_cast<char*>(magic.data()),magic.size()) || magic!=Magic ||
        !input.read(reinterpret_cast<char*>(&version),sizeof(version)) || version!=1 ||
        !input.read(reinterpret_cast<char*>(&nameSize),sizeof(nameSize)) || !nameSize || nameSize>1024)
        throw std::runtime_error("Incompatible or damaged PS2 state header");
    std::string identity(nameSize,'\0');
    if (!input.read(identity.data(),identity.size())) throw std::runtime_error("Incomplete PS2 state identity");
    const auto slash=identity.rfind('/');
    const auto program=identity.substr(slash==std::string::npos?0:slash+1);
    if (identity!=identity_) {
        const auto currentSlash=identity_.rfind('/');
        if (currentSlash==std::string::npos || slash!=currentSlash ||
            identity.compare(0,slash+1,identity_,0,slash+1)!=0 ||
            std::find(discPrograms.begin(),discPrograms.end(),program)==discPrograms.end())
            throw std::runtime_error("Save state belongs to a different game or mode");
    }
    uint32_t rawSize{};
    if (!input.read(reinterpret_cast<char*>(&rawSize), sizeof(rawSize)) || !rawSize || rawSize > MaxSnapshot)
        throw std::runtime_error("Incompatible or damaged PS2 state header");
    const auto body = length - static_cast<std::streamoff>(input.tellg());
    if (body <= 0 || static_cast<uint64_t>(body) > mz_compressBound(rawSize))
        throw std::runtime_error("Invalid PS2 state length");
    std::vector<uint8_t> compressed(static_cast<size_t>(body));
    if (!input.read(reinterpret_cast<char*>(compressed.data()), body))
        throw std::runtime_error("Could not read PS2 state");
    std::vector<uint8_t> result(rawSize);
    mz_ulong unpacked = rawSize, consumed = static_cast<mz_ulong>(body);
    if (mz_uncompress2(result.data(), &unpacked, compressed.data(), &consumed) != MZ_OK ||
        unpacked != rawSize || consumed != compressed.size())
        throw std::runtime_error("Damaged PS2 state data");
    return {program,std::move(result)};
}

StateSlots::Selection StateSlots::LastUsed() const {
    std::ifstream input(directory_ / "last-slot", std::ios::binary);
    std::array<char,6> bytes{};
    input.read(bytes.data(),bytes.size());
    const auto size=input.gcount();
    const auto slotAt=[&](unsigned i) -> unsigned {
        if (bytes[i]<'0' || bytes[i]>'9' || bytes[i+1]<'0' || bytes[i+1]>'9') return 0;
        return (bytes[i]-'0')*10 + bytes[i+1]-'0';
    };
    const unsigned save=slotAt(0);
    if (size==2 && save) return {save,save,false}; // Earlier builds remembered one shared slot.
    const unsigned load=slotAt(2);
    if (size==5 && save && load && (bytes[4]=='S' || bytes[4]=='L'))
        return {save,load,bytes[4]=='L'};
    return {};
}

void StateSlots::Remember(Selection selection) const {
    const auto digits = TwoDigits(selection.saveSlot) + TwoDigits(selection.loadSlot) + (selection.load?'L':'S');
    AtomicWrite(directory_ / "last-slot", [&](FILE* file) {
        WriteBytes(file, {reinterpret_cast<const uint8_t*>(digits.data()), digits.size()});
    });
}
}
