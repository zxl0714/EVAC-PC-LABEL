#include "launcher/log_upload.h"
#include "crash_dump_fixture.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cave;
int wmain(int argc,wchar_t** argv) {
  try {
    for(auto language:{launcher::Language::Chinese,launcher::Language::Japanese,launcher::Language::English})
      for(auto key:{L"上传日志",L"游玩记录",L"问题描述（选填）",L"查看本地日志",L"取消",L"上传",L"关闭",L"正在打包并上传…",L"已上传，编号：",L"上传失败，可重试",L"还没有游玩日志",L"现在上传日志？",L"运行组件不可用 · 点击详情",L"无法读取以下运行组件：",L"文件不存在",L"路径是文件夹",L"文件无法访问",L"请重新解压程序包并替换同名文件，然后点击刷新。若文件存在但无法访问，请检查文件权限或占用。仍有问题可点击上传日志。"})
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
    {const auto dump=AuthoredCrashDump();std::ofstream output(latest/L"crash.dmp",std::ios::binary);output.write(dump.data(),dump.size());}
    fs::last_write_time(older,fs::last_write_time(latest)-std::chrono::hours(24));
    const auto runs=launcher::reports::Recent(root);
    if(runs.size()!=2 || runs.front().game!="espgaluda")throw std::runtime_error("Newest session not selected across games");
    {
      const auto& session=runs.front();
      if(launcher::reports::ProcessAtUpload(session).find("identity_unavailable")==std::string::npos)
        throw std::runtime_error("Old log without process identity was treated as running");
      FILETIME created{},exited{},kernel{},user{};
      if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))throw std::runtime_error("Process fixture failed");
      const auto stamp=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
      auto metadata=[&](uint64_t creation){std::ofstream(session.directory/L"run.ini")<<"[run]\npid="<<GetCurrentProcessId()<<"\nprocess_created="<<creation<<"\n";};
      metadata(stamp);
      if(launcher::reports::ProcessAtUpload(session).find("process_state=running")==std::string::npos)
        throw std::runtime_error("Active process state missing");
      metadata(stamp+1);
      if(launcher::reports::ProcessAtUpload(session).find("pid_reused")==std::string::npos)
        throw std::runtime_error("Unrelated process with reused PID accepted");
      {std::ofstream(session.directory/L"run.ini")<<"broken metadata";}
      if(launcher::reports::ProcessAtUpload(session).find("metadata_unreadable")==std::string::npos)
        throw std::runtime_error("Malformed optional metadata blocked reporting");
      metadata(stamp);
    }
    const auto zip=launcher::reports::Package(root,runs.front(),"最终 BOSS 崩溃");
    if(!fs::is_regular_file(zip) || fs::file_size(zip)==0)throw std::runtime_error("No report ZIP");
    if(fs::exists(latest/L"auto-crash-1.txt"))throw std::runtime_error("Upload modified original session");
    launcher::reports::RecordRuntimeFailure(root,"ketsui");
    const auto startupRuns=launcher::reports::Recent(root);
    if(startupRuns.size()!=3 || startupRuns.front().game!="ketsui")throw std::runtime_error("Launch failure was not uploadable");
    launcher::reports::RecordRuntimeFailure(root,"ketsui");
    if(launcher::reports::Recent(root).size()!=3)throw std::runtime_error("Refresh duplicated the same runtime diagnostic");
    if(argc==2 && std::wstring_view(argv[1])==L"--fixture")std::cout<<Utf8(zip)<<'\n';
    else {fs::remove_all(root);std::cout<<"Report selection and ZIP packaging: PASS\n";}
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
