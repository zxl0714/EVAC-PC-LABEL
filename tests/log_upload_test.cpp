#include "launcher/log_upload.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cave;
int wmain(int argc,wchar_t** argv) {
  try {
    for(auto language:{launcher::Language::Chinese,launcher::Language::Japanese,launcher::Language::English})
      for(auto key:{L"上传日志",L"游玩记录",L"问题描述（选填）",L"查看本地日志",L"取消",L"上传",L"关闭",L"正在打包并上传…",L"已上传，编号：",L"上传失败，可重试",L"还没有游玩日志",L"现在上传日志？"})
        (void)launcher::Translate(language,key);
    if(argc==4 && std::wstring_view(argv[1])==L"--upload") {
      std::cout<<launcher::reports::Upload(Utf8(argv[3]),fs::path(argv[2]))<<'\n';return 0;
    }
    const auto root=fs::temp_directory_path()/(L"evac-report-测试-"+std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root/L"user-data");
    std::ofstream(root/L"user-data/config.ini")<<"[community]\nnickname=Player\n";
    const auto older=root/L"logs/futari/run-20261101T120000Z-1";
    const auto latest=root/L"logs/espgaluda/run-20261102T120000Z-2";
    fs::create_directories(older);fs::create_directories(latest);
    std::ofstream(older/L"game.log")<<"earlier session";
    std::ofstream(latest/L"stderr.log")<<"Missing statically compiled guest target\n";
    std::ofstream(latest/L"game.bin")<<"must not be included";
    std::ofstream(latest/L"game.dmp")<<"must not be included";
    fs::last_write_time(older,fs::last_write_time(latest)-std::chrono::hours(24));
    const auto runs=launcher::reports::Recent(root);
    if(runs.size()!=2 || runs.front().game!="espgaluda")throw std::runtime_error("Newest session not selected across games");
    const auto zip=launcher::reports::Package(root,runs.front(),"最终 BOSS 崩溃");
    if(!fs::is_regular_file(zip) || fs::file_size(zip)==0)throw std::runtime_error("No report ZIP");
    if(argc==2 && std::wstring_view(argv[1])==L"--fixture")std::cout<<Utf8(zip)<<'\n';
    else {fs::remove_all(root);std::cout<<"Report selection and ZIP packaging: PASS\n";}
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
