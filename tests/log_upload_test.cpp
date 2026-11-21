#include "launcher/log_upload.h"
#include "common/launch.h"
#include "crash_dump_fixture.h"
#include <Windows.h>
#include <intrin.h>
#include <werapi.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cave;

static fs::path ExitReportFixture(const fs::path& root,const fs::path& executable,bool fast_fail) {
  fs::create_directories(root/L"user-data");
  fs::path run,other;
  {
    LaunchSpec spec;
    spec.executable=executable;
    spec.paths.root=root;
    spec.arguments={fast_fail?L"--fast-fail-child":L"--abrupt-child"};
    auto child=StartGame(spec);
    child.Wait();
    const auto code=child.ExitCode();
    const auto birth=child.CreationTime();
    if(code!=(fast_fail?0xc0000409u:0x4123u) || !birth)throw std::runtime_error("Fixture exit identity missing");
    const auto suffix=L"Z-"+std::to_wstring(child.pid());
    other=root/L"logs/daioujou"/(L"run-20261101T110000"+suffix);
    run=root/L"logs/daioujou"/(L"run-20261102T110000"+suffix);
    const auto metadata=[&](const fs::path& path,uint64_t stamp) {
      fs::create_directories(path);
      std::ofstream(path/L"run.json")<<"{\"pid\":"<<child.pid()<<",\"processCreated\":"<<stamp<<"}";
    };
    metadata(other,birth-1);
    metadata(run,birth);
    std::ofstream(run/L"stderr.txt")<<"[ps2-dl-wsi] vkCreateSwapchainKHR enter phase=start\n";
    launcher::reports::RecordProcessExit(root,"daioujou",child.pid(),birth,*code);
    const auto receipt=Ini::ReadText(run/L"launcher-exit.txt");
    if(receipt.find("exit_code="+std::to_string(*code)+"\n")==receipt.npos || fs::exists(other/L"launcher-exit.txt"))
      throw std::runtime_error("Exit code missing or linked to a reused PID");
  }
  const launcher::reports::Session session{"daioujou",run,{}};
  if(fast_fail) {
    const auto deadline=GetTickCount64()+10000;
    while(launcher::reports::ProcessAtUpload(session).find("windows_fault=application_error_1000")==std::string::npos) {
      if(GetTickCount64()>=deadline)throw std::runtime_error("Windows Application Error event not found for the fast-fail fixture");
      Sleep(200);
    }
    if(launcher::reports::ProcessAtUpload({"daioujou",other,{}}).find("windows_fault=no_matching_event")==std::string::npos)
      throw std::runtime_error("Windows event attached to the wrong session");
  }
  return launcher::reports::Package(root,session,"abrupt child exit");
}

int wmain(int argc,wchar_t** argv) {
  try {
    if(argc==2 && std::wstring_view(argv[1])==L"--fast-fail-child") {
      // Only this opt-in fixture asks Windows to log its crash without showing a dialog.
      SetErrorMode(GetErrorMode() & ~SEM_NOGPFAULTERRORBOX);
      WerSetFlags(WER_FAULT_REPORTING_NO_UI);
      __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    }
    if(argc==2 && std::wstring_view(argv[1])==L"--abrupt-child") {
      TerminateProcess(GetCurrentProcess(),0x4123);
      return 1;
    }
    if(argc==5 && std::wstring_view(argv[1])==L"--package-session") {
      const fs::path root=argv[2];
      for(const auto& run:launcher::reports::Recent(root)) {
        if(run.game==Utf8(argv[3]) && run.directory.filename()==argv[4]) {
          std::cout<<Utf8(launcher::reports::Package(root,run,"diagnostic fixture"))<<'\n';return 0;
        }
      }
      throw std::runtime_error("Requested fixture run is missing");
    }
    for(auto language:{launcher::Language::Chinese,launcher::Language::Japanese,launcher::Language::English})
      for(auto key:{L"上传日志",L"游玩记录",L"问题描述（选填）",L"查看本地日志",L"取消",L"上传",L"关闭",L"正在打包并上传…",L"已上传，编号：",L"上传失败，可重试",L"还没有游玩日志",L"现在上传日志？",L"运行组件不可用",L"无法读取以下运行组件：",L"文件不存在",L"路径是文件夹",L"文件无法访问",L"请重新解压程序包并替换同名文件，然后按 F5。若文件存在但无法访问，请检查文件权限或占用。仍有问题可点击上传日志。"})
        (void)launcher::Translate(language,key);
    if(argc==4 && std::wstring_view(argv[1])==L"--upload") {
      std::cout<<launcher::reports::Upload(Utf8(argv[3]),fs::path(argv[2]))<<'\n';return 0;
    }
    if(argc==6 && std::wstring_view(argv[1])==L"--upload-file") {
      const auto id=launcher::reports::UploadFile(fs::path(argv[2]),Utf8(argv[3]),fs::path(argv[4]),Utf8(argv[5]),
        [](uint64_t sent,uint64_t total){std::cerr<<sent<<'/'<<total<<'\n';});
      std::cout<<id<<'\n';return 0;
    }
    const auto root=fs::temp_directory_path()/(L"evac-report-测试-"+std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root/L"user-data");
    std::ofstream(root/L"user-data/config.ini")<<"[community]\nnickname=Player\n";
    const bool fast_fail=argc==2 && std::wstring_view(argv[1])==L"--fixture-fault";
    if(argc==1 || fast_fail || (argc==2 && std::wstring_view(argv[1])==L"--fixture-exit")) {
      const auto zip=ExitReportFixture(root/L"exit-evidence",fs::absolute(argv[0]),fast_fail);
      if(argc==2) {std::cout<<Utf8(zip)<<'\n';return 0;}
    }
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
