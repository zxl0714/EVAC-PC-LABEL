// Included after the fixture helpers in ui_test.cpp. Every child/window/path
// below belongs to this test; no desktop input or real game is used.
std::string ReadFixture(const fs::path& path) {
  std::ifstream stream(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),{}};
}
fs::path ReportPart(const fs::path& report,const wchar_t* suffix) {return fs::path(report.native()+suffix);}
struct FixtureProcess {
  PROCESS_INFORMATION process{};
  explicit FixtureProcess(const std::vector<std::wstring>& arguments) {
    std::wstring executable(32768,L'\0');
    const auto size=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    Require(size && size<executable.size(),"Find instance-test executable");executable.resize(size);
    auto command=QuoteArgument(executable);
    for(const auto& argument:arguments)command+=L" "+QuoteArgument(argument);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    Require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),
            "Start owned instance fixture");
    CloseHandle(process.hThread);process.hThread=nullptr;
  }
  FixtureProcess(const FixtureProcess&)=delete;
  ~FixtureProcess() {
    if(process.hProcess) {
      // Failure cleanup is limited to the exact child handle created above.
      if(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT) {TerminateProcess(process.hProcess,77);WaitForSingleObject(process.hProcess,3000);}
      CloseHandle(process.hProcess);
    }
  }
  void Wait(DWORD expected=0) {
    const auto waited=WaitForSingleObject(process.hProcess,10000);DWORD code=1;
    GetExitCodeProcess(process.hProcess,&code);
    Require(waited==WAIT_OBJECT_0 && code==expected,"Owned instance fixture did not finish successfully");
  }
  void Crash() {
    Require(TerminateProcess(process.hProcess,66)!=FALSE,"Terminate only the owned crash fixture");Wait(66);
  }
};
std::string WaitReport(FixtureProcess& child,const fs::path& report) {
  const auto deadline=GetTickCount64()+8000;
  while(!fs::is_regular_file(report)) {
    const auto state=WaitForSingleObject(child.process.hProcess,15);
    if(state!=WAIT_TIMEOUT && !fs::is_regular_file(report))throw std::runtime_error("Fixture exited before writing its report");
    if(GetTickCount64()>=deadline)throw std::runtime_error("Timed out waiting for a live instance fixture");
  }
  // The producer writes tiny markers atomically by rename below.
  return ReadFixture(report);
}
void Marker(const fs::path& report,std::string_view value) {
  const auto pending=ReportPart(report,L".pending");Write(pending,value);
  Require(MoveFileExW(pending.c_str(),report.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Publish instance-test marker");
}
void InstanceHost(const fs::path& root,const fs::path& report,std::wstring_view mode) {
  cave::launcher::LauncherInstance instance(root);
  if(!instance.ActivateOrAcquire()) {Marker(report,"existing");return;}
  if(mode==L"hold") {
    Marker(report,"primary");
    while(!fs::exists(ReportPart(report,L".stop")))WaitForSingleObject(GetCurrentProcess(),25);
    return;
  }
  Frame frame(root,"ketsui",mode==L"minimized");auto& app=frame.app;
  SetWindowTextW(app.nickname_control,L"Unsaved instance draft");
  const auto before=Ini::ReadText(ConfigPath(root));
  const auto own_process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,GetCurrentProcessId());
  Require(own_process!=nullptr,"Open fixture-only running-child handle");
  app.child.emplace(own_process,GetCurrentProcessId());app.active_game="ketsui";
  instance.Publish(app.hwnd);Marker(report,"primary");
  unsigned observed{};
  while(IsWindow(app.hwnd)) {
    MsgWaitForMultipleObjects(0,nullptr,FALSE,25,QS_ALLINPUT);
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
      if(message.message!=WM_QUIT) {TranslateMessage(&message);DispatchMessageW(&message);}
    }
    if(observed!=show_requests) {
      observed=show_requests;
      const bool preserved=Catalogue()[app.selected].id=="ketsui" && app.preferences_dirty &&
          ControlText(app.nickname_control)==L"Unsaved instance draft" && app.child && app.child->pid()==GetCurrentProcessId() &&
          app.active_game=="ketsui" && Ini::ReadText(ConfigPath(root))==before && foreground_requests==show_requests &&
          last_show_request==(mode==L"minimized"?SW_RESTORE:SW_SHOW) && unexpected_dialogs==0;
      Marker(ReportPart(report,L".activated"),preserved?"preserved":"failed");
    }
    if(fs::exists(ReportPart(report,L".stop"))) {
      instance.Unpublish();app.child.reset();app.active_game.clear();App::Window(app.hwnd,WM_CLOSE,0,0);
    }
  }
}
void InstanceProbe(const fs::path& root,const fs::path& report) {
  cave::launcher::LauncherInstance instance(root);
  Marker(ReportPart(report,L".waiting"),"opened");
  Marker(report,instance.ActivateOrAcquire()?"primary":"existing");
}
void InstanceChecks() {
  Fixture fixture;
  const auto control=fixture.directory/"control";fs::create_directory(control);
  const auto stop=[&](FixtureProcess& process,const fs::path& report) {Marker(ReportPart(report,L".stop"),"stop");process.Wait();};
  {
    const auto report=control/"normal";
    FixtureProcess owner({L"--instance-host",fixture.root.native(),report.native(),L"normal"});
    Require(WaitReport(owner,report)=="primary","First process did not own launcher startup");
    const auto before=Ini::ReadText(ConfigPath(fixture.root));
    const auto probe_report=control/"normal-probe";
    FixtureProcess probe({L"--instance-probe",(fixture.root/L".").native(),probe_report.native()});
    Require(WaitReport(probe,probe_report)=="existing","Root alias started a duplicate GUI");probe.Wait();
    Require(WaitReport(owner,ReportPart(report,L".activated"))=="preserved" && Ini::ReadText(ConfigPath(fixture.root))==before,
            "Reactivation changed selection, unsaved fields, running child or configuration");
    stop(owner,report);
    const auto restarted=control/"after-normal-close";
    FixtureProcess next({L"--instance-probe",fixture.root.native(),restarted.native()});
    Require(WaitReport(next,restarted)=="primary","Normal close left a stale instance lock");next.Wait();
  }
  {
    const auto root=fixture.directory/"minimized";fs::create_directory(root);
    const auto report=control/"minimized";
    FixtureProcess owner({L"--instance-host",root.native(),report.native(),L"minimized"});
    Require(WaitReport(owner,report)=="primary","Minimized fixture did not own startup");
    const auto activated=control/"minimized-probe";
    FixtureProcess probe({L"--instance-probe",root.native(),activated.native()});
    Require(WaitReport(probe,activated)=="existing","Minimized launcher was duplicated");probe.Wait();
    Require(WaitReport(owner,ReportPart(report,L".activated"))=="preserved","Minimized activation did not request restore while preserving the form and child");
    stop(owner,report);
  }
  {
    const auto root=fixture.directory/"abandoned";fs::create_directory(root);
    const auto report=control/"abandoned";
    FixtureProcess owner({L"--instance-host",root.native(),report.native(),L"hold"});
    Require(WaitReport(owner,report)=="primary","Crash fixture did not acquire the startup lock");
    const auto replacement=control/"after-crash";
    FixtureProcess probe({L"--instance-probe",root.native(),replacement.native()});
    // The competing process retains the named object while its owner dies.
    Require(WaitReport(probe,ReportPart(replacement,L".waiting"))=="opened","Competing process did not retain the named object");
    owner.Crash();
    Require(WaitReport(probe,replacement)=="primary","Abandoned startup lock could not be reclaimed");probe.Wait();
  }
  {
    const auto root=fixture.directory/"independent";fs::create_directory(root);
    const auto first_report=control/"independent-a",second_report=control/"independent-b";
    FixtureProcess first({L"--instance-host",fixture.root.native(),first_report.native(),L"normal"});
    FixtureProcess second({L"--instance-host",root.native(),second_report.native(),L"normal"});
    Require(WaitReport(first,first_report)=="primary" && WaitReport(second,second_report)=="primary","Different installation roots shared one GUI lock");
    stop(first,first_report);stop(second,second_report);
  }
  {
    const auto root=fixture.directory/"simultaneous";fs::create_directory(root);
    const auto first_report=control/"simultaneous-a",second_report=control/"simultaneous-b";
    FixtureProcess first({L"--instance-host",root.native(),first_report.native(),L"normal"});
    FixtureProcess second({L"--instance-host",root.native(),second_report.native(),L"normal"});
    const auto a=WaitReport(first,first_report),b=WaitReport(second,second_report);
    Require((a=="primary" && b=="existing") || (a=="existing" && b=="primary"),"Concurrent startup did not elect exactly one GUI");
    if(a=="primary") {second.Wait();stop(first,first_report);}else {first.Wait();stop(second,second_report);}
  }
}
