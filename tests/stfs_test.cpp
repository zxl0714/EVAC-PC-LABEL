#include "common/stfs.h"
#include "common/launch.h"
#include "common/verification_cache.h"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace cave;
namespace {
void CheckTitleUpdate(const fs::path& source, const fs::path& root, uint64_t bytes,
                      std::string_view hash, std::string_view padded_hash) {
  const auto data = source.parent_path();
  const auto name = Utf8(source.filename());
  const auto container_hash = Sha256(source);
  const std::array patches{XexTitlePatch{{"default.xexp", bytes, hash, true}, bytes + 16, padded_hash}};
  const XexTitleUpdate update{"updated", "1.01", "updated.exe", 0x435A07D7,
      {name, fs::file_size(source), container_hash, true}, patches};
  const std::array files{DataFile{"default.xex", 3,
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", true}};
  const Game game{.id="fixture", .name="Fixture", .backend="xbox360", .data_dir="game-data/fixture",
      .executable="base.exe", .integrated=true, .coming_soon=false, .files=files, .runtime_files={},
      .title_update=&update};
  VerificationCache cache(root);
  const auto raw = Inspect(game, root, data, CheckLevel::Full, &cache, TitleUpdateInput::PatchesOnly);
  if (!raw.variant.empty() || fs::exists(data / "default.xexp"))
    throw std::runtime_error("A raw update was used by prepared-data inspection");
  const auto prepared = InspectLaunchData(game, root, data, CheckLevel::Full, cache);
  const auto patch = data / "default.xexp";
  if (prepared.state != "ready" || prepared.variant != update.id || Sha256(patch) != padded_hash)
    throw std::runtime_error("Launcher did not prepare the padded title update beside the game");
  fs::last_write_time(patch, fs::last_write_time(patch) - std::chrono::hours(24));
  const auto time = fs::last_write_time(patch);
  const auto repeated = InspectLaunchData(game, root, data, CheckLevel::Full, cache);
  if (repeated.state != "ready" || repeated.variant != update.id || fs::last_write_time(patch) != time)
    throw std::runtime_error("Launcher rewrote an already installed title update");
}
}
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc==7 && std::wstring_view(argv[1])==L"--title-update") {
      CheckTitleUpdate(fs::absolute(argv[2]),fs::absolute(argv[3]),std::stoull(argv[4]),Utf8(argv[5]),Utf8(argv[6]));
      return 0;
    }
    if(argc!=5)throw std::runtime_error("Expected input, output, size, SHA256");
    const fs::path input=fs::absolute(argv[1]),output=fs::absolute(argv[2]);
    const auto bytes=std::stoull(argv[3]);const auto hash=Utf8(argv[4]);
    const std::array files{DataFile{"0000000000000000/435A07D7/00000002/package/module/sample.bin",bytes,hash,false}};
    const ContentPackage package{"fixture","Fixture","0000000000000000/435A07D7/00000002/package","package",0x435A07D7,files};
    auto quick=InspectStfs(package,input,CheckLevel::Quick);
    if(quick.state!=DataState::Unverified || quick.hashes_checked)throw std::runtime_error("Quick recognition failed");
    auto full=InspectStfs(package,input,CheckLevel::Full);
    if(full.state!=DataState::Verified || full.container!=input || full.hashes_checked!=1)
      throw std::runtime_error("Full recognition rejected payload");
    StfsPackage archive(input,true);archive.Extract(output);
    if(fs::file_size(output/"module/sample.bin")!=bytes || Sha256(output/"module/sample.bin")!=hash)
      throw std::runtime_error("Extracted bytes differ");
    const std::array packages{package};
    const Game game{"fixture","Fixture","xbox360","unused","unused",true,false,{}, {},packages};
    auto discovered=InspectContent(game,input.parent_path(),CheckLevel::Full);
    if(discovered[0].state!=DataState::Verified || discovered[0].container!=input)
      throw std::runtime_error("Automatic container discovery failed");
    std::cout<<"Native DLC inspection and extraction passed\n";return 0;
  } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
