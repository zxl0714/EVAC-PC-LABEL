#include "common/catalogue.h"
#include "common/ini.h"
#include "common/paths.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cave;
namespace {
int checks = 0;
void Require(bool okay, const char* message) {
  ++checks;
  if (!okay) throw std::runtime_error(message);
}
template<class F> void Reject(F action, const char* message) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  Require(rejected, message);
}
void Write(const fs::path& path, std::string_view bytes = "abc") {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file << bytes;
  if (!file) throw std::runtime_error("Test file write failed");
}
struct Fixture {
  fs::path temporary, root, data;
  Fixture() {
    auto base = fs::weakly_canonical(fs::temp_directory_path());
    std::array<unsigned char, 16> nonce{};
    if (BCryptGenRandom(nullptr, nonce.data(), ULONG(nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
      throw std::runtime_error("Test nonce failed");
    std::string name = "cave-common-test-";
    for (auto n : nonce) name += std::to_string(n) + '-';
    temporary = base / name;
    if (!fs::create_directory(temporary)) throw std::runtime_error("Test directory collision");
    root = temporary / FromUtf8("合集 空格");
    data = temporary / FromUtf8("外部 游戏数据");
    fs::create_directories(root); fs::create_directories(data);
  }
  ~Fixture() {
    // Only remove the exact random directory created by this fixture.
    auto base = fs::weakly_canonical(fs::temp_directory_path());
    if (temporary.parent_path() == base && temporary.filename().native().starts_with(L"cave-common-test-")) {
      std::error_code error;
      fs::remove_all(temporary, error);
    }
  }
};
}  // namespace
int main() {
  try {
    Fixture fixture;
    constexpr std::string_view hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::array files{DataFile{"main.bin",3,hash,true}, DataFile{"res/asset.bin",3,hash,false}};
    const std::array<std::string_view,1> runtime{"runtime/fake.exe"};
    Game game{"fixture","Fixture","xbox360","game-data/fixture","runtime/fake.exe",true,false,files,runtime};
    Require(Catalogue().size() == 5 && Catalogue()[0].id == "futari" && Catalogue()[1].id == "espgaluda2" &&
            Catalogue()[2].id == "ketsui" && Catalogue()[3].id == "daioujou" && Catalogue()[4].id == "esprade", "Catalogue order");
    Reject([] { FindGame("unknown"); }, "Unknown game accepted");
    Require(FindGame("espgaluda2").files.size() == 155, "G2 complete inventory");
    auto inspect = [&](CheckLevel level = CheckLevel::Full) { return Inspect(game, fixture.root, fixture.data, level); };
    Require(inspect().state == "missing_data", "Empty data state");
    Write(fixture.data / "main.bin");
    Require(Sha256(fixture.data / "main.bin") == hash, "SHA-256 known vector");
    Require(inspect().state == "incomplete_data", "Missing resource state");
    Write(fixture.data / "res/asset.bin");
    Require(inspect().state == "missing_runtime", "Missing runtime state");
    Write(fixture.root / "runtime/fake.exe");
    Require(inspect(CheckLevel::Quick).state == "needs_verification" && inspect(CheckLevel::Quick).hashes_checked == 0,
            "Quick scan must not claim verified");
    Require(inspect(CheckLevel::Programs).data_state == DataState::Unverified && inspect(CheckLevel::Programs).hashes_checked == 1,
            "Program verification must not claim complete resource hashes");
    auto verified = inspect();
    Require(verified.state == "ready" && verified.hashes_checked == 2, "Full verification");
    Write(fixture.data / "main.bin", "xyz");
    Require(inspect().state == "unsupported_version", "Same-size wrong program accepted");
    Write(fixture.data / "main.bin"); Write(fixture.data / "res/asset.bin", "xyz");
    Require(inspect().state == "incomplete_data", "Corrupt resource accepted");
    Write(fixture.data / "res/asset.bin");
    game.integrated = false;
    Require(inspect().state == "not_integrated" && inspect().data_state == DataState::Verified, "Unintegrated title marked ready");
    game.coming_soon = true;
    Require(inspect().state == "coming_soon", "Future title marked available");
    game.coming_soon = false; game.integrated = true;
    auto paths = Paths(fixture.root, "fixture", fixture.data);
    ValidateOutputs(paths);
    Require(paths.data == fixture.data && Within(paths.logs, fixture.root), "External/Unicode paths");
    Reject([&] { ValidateOutputs(Paths(fixture.root,"fixture",fixture.root)); }, "Data/output overlap accepted");
    Reject([&] { Paths(fixture.root,"fixture",fixture.data,"../unsafe"); }, "Profile traversal accepted");
    Reject([&] { Resolve(fixture.root,L"C:relative"); }, "Drive-relative path accepted");
    Reject([&] { DataChild(fixture.data,"../escape.bin"); }, "Manifest traversal accepted");
    auto alias = paths; alias.logs = paths.profile;
    Reject([&] { ValidateOutputs(alias); }, "Overlapping output roots accepted");
    const auto ini = Ini::Parse("\xef\xbb\xbf[Collection]\nconfig_version=1\n[Game.Fixture]\nDATA_DIR = 外部 游戏数据\n[game.absent]\ndata_dir=\n");
    Require(ini.DataDirectory(game) == FromUtf8("外部 游戏数据"), "Case/BOM/Unicode INI");
    Require(Ini::Parse("").DataDirectory(game) == "game-data/fixture", "Missing config default");
    Reject([] { Ini::Parse("[x]\na=1\nA=2\n"); }, "Duplicate INI key");
    Reject([] { Ini::Parse("[collection]\nconfig_version=99\n"); }, "Future config version accepted");
    Reject([] { Ini::Parse("[bad\n"); }, "Bad INI section");
    Reject([&] { Ini::Parse("[game.fixture]\ndata_dir=\n").DataDirectory(game); }, "Empty selected path accepted");
    Reject([] { Ini::Parse(std::string("[x]\na=") + char(0xff)); }, "Invalid UTF-8 accepted");
    Require(inspect().state == "ready", "Other-game settings affected current title");
    std::cout << checks << " native checks passed; fixture programs were not executed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
