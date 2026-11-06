#include "common/verification_cache.h"
#include "common/paths.h"
#include "common/launch.h"
#include "common/ini.h"
#include <Windows.h>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace cave;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Write(const fs::path& path,std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path,std::ios::binary);stream<<text;
  Require(bool(stream),"Fixture write failed");
}
void Checks(const fs::path& root) {
  const auto data=root/"game-data/fixture",file=data/"main.bin",asset=data/"asset.bin";
  Write(file,"abc");Write(asset,"resource");
  const auto original=Sha256(file),resource=Sha256(asset);
  const std::array files{DataFile{"main.bin",3,original,true},DataFile{"asset.bin",8,resource,false}};
  Game game{"fixture","Fixture","ps4","game-data/fixture","runtime/fake.exe",true,false,files,{}};
  {
    VerificationCache cache(root);
    Require(Inspect(game,root,data,CheckLevel::Full,&cache).state=="ready","Initial verification failed");
    Require(cache.reads==2 && cache.bytes_read==11,"Initial files were not hashed");
    Require(cache.Digest(file)==original && cache.hits==1,"In-process cache failed");
  }
  {
    VerificationCache cache(root);
    Require(Inspect(game,root,data,CheckLevel::Full,&cache).state=="ready","Persisted verification failed");
    Require(cache.hits==2 && cache.reads==0,"Persistent digests not reused");
    // The licence sidecar is absent from runtime-required manifests.
    Write(data/"sce_sys/license.info","different package metadata");
    Require(Inspect(game,root,data,CheckLevel::Full,&cache).state=="ready","Unreferenced metadata blocked game");
    const auto modified=fs::last_write_time(file);
    Write(file,"xyz");fs::last_write_time(file,modified);
    Require(Inspect(game,root,data,CheckLevel::Full,&cache).state=="unsupported_version","Same-size write with restored mtime reused stale digest");
    Write(file,"abc");
    Write(asset,"damaged!");
    Require(Inspect(game,root,data,CheckLevel::Full,&cache).state=="incomplete_data","Damaged resource accepted");
    Write(asset,"resource");
  }
  {
    VerificationCache cache(root);
    cache.Digest(file);
    const auto modified=fs::last_write_time(file);
    Write(data/"replacement.bin","xyz");fs::last_write_time(data/"replacement.bin",modified);
    Require(MoveFileExW((data/"replacement.bin").c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING),"Replacement failed");
    Require(cache.Digest(file)!=original,"Replacement reused old file identity");
    Write(file,"abc");
    const std::array changed{DataFile{"main.bin",3,resource,true},files[1]};
    game.files=changed;
    Require(Inspect(game,root,data,CheckLevel::Full,&cache).state=="unsupported_version","Changed manifest rule ignored");
    game.files=files;
    const auto range=cache.DigestRange(file,1,2);
    Require(range==Sha256Range(file,1,2) && range!=original,"Range and full digests mixed");
    fs::remove(file);
    Require(Inspect(game,root,data,CheckLevel::Full,&cache).state=="incomplete_data","Missing cached file accepted");
    Write(file,"abc");
  }
  const auto persisted=root/"user-data/cache/verification-v1.txt";
  {
    auto text=Ini::ReadText(persisted);
    text[std::string("EVAC file verification 1\n").size()+65]^=1;
    Write(persisted,text);
    VerificationCache cache(root);cache.Digest(file);
    Require(cache.reads==1,"Corrupted cache digest was trusted");
  }
  Write(persisted,"corrupt cache");
  {
    VerificationCache cache(root);
    Require(cache.Digest(file)==original && cache.reads==1,"Corrupt cache did not fall back");
  }
  {
    VerificationCache first(root),second(root);
    first.DigestRange(file,0,1);second.DigestRange(file,1,1);
    first.Flush();second.Flush();
    VerificationCache merged(root);
    merged.DigestRange(file,0,1);merged.DigestRange(file,1,1);
    Require(merged.hits==2,"Concurrent cache snapshots lost committed entries");
  }
  std::exception_ptr failureA,failureB;
  auto worker=[&](std::exception_ptr& failure){try {
    VerificationCache cache(root);
    Require(cache.Digest(file)==original,"Concurrent verification changed result");
    cache.Flush();
  }catch(...){failure=std::current_exception();}};
  std::thread a(worker,std::ref(failureA)),b(worker,std::ref(failureB));a.join();b.join();
  if(failureA)std::rethrow_exception(failureA);if(failureB)std::rethrow_exception(failureB);
  fs::remove(persisted);
  {
    VerificationCache cache(root);cache.Digest(file);
    Require(cache.reads==1,"Missing cache did not rehash");
  }
  {
    const auto moved=root/"moved-game-data";
    fs::rename(data,moved);
    VerificationCache cache(root);
    Require(Inspect(game,root,moved,CheckLevel::Full,&cache).state=="ready","Moved data was not verified correctly");
    Require(!fs::exists(data),"Verification recreated original data directory");
  }
  for(const auto& title:Catalogue())if(title.backend=="ps4") {
    auto check=[](auto entries){for(const auto& entry:entries)
      Require(entry.path!="sce_sys/license.info" && entry.path!="sce_sys/license.info.encrypted",
              "Package licence sidecar is still a required game identity");};
    check(title.files);for(const auto& variant:title.variants)check(variant.files);
  }
}
}
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc==5 && std::wstring_view(argv[1])==L"--measure") {
      const fs::path root=argv[2],data=argv[4];const auto& game=FindGame(Utf8(fs::path(argv[3])));
      VerificationCache cache(root);
      const auto started=std::chrono::steady_clock::now();
      const auto result=Inspect(game,root,data,CheckLevel::Full,&cache);
      const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
      Require(result.data_state==DataState::Verified,"Measurement data verification failed");
      std::cout<<"{\"elapsed_ms\":"<<ms<<",\"hits\":"<<cache.hits<<",\"reads\":"<<cache.reads
               <<",\"bytes\":"<<cache.bytes_read<<"}\n";return 0;
    }
    const auto root=fs::temp_directory_path()/("evac-verification-"+std::to_string(GetCurrentProcessId()));
    Require(!fs::exists(root),"Fixture already exists");fs::create_directory(root);
    Checks(root);fs::remove_all(root);
    std::cout<<"Verification persistence, mutation, replacement, rules, corruption and concurrency: PASS\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
