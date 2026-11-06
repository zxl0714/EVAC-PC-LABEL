#include "log_upload.h"
#include "common/launch.h"
#include "common/settings.h"
#include "common/crash_details.h"
#include <winhttp.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <map>
#include <mutex>
#include <thread>

namespace cave::launcher::reports {
namespace {
using Json=nlohmann::json;
constexpr uint64_t Limit=128ull*1024*1024,ZipLimit=32ull*1024*1024;
bool Plain(const fs::path& path) {
  const auto flags=GetFileAttributesW(path.c_str());
  return flags!=INVALID_FILE_ATTRIBUTES && !(flags&FILE_ATTRIBUTE_REPARSE_POINT);
}
bool TextFile(const fs::path& path) {
  const auto extension=path.extension().wstring();
  return std::set<std::wstring>{L".log",L".txt",L".json",L".jsonl",L".ini",L".csv"}.contains(extension);
}
std::wstring SessionLabel(const Session& session) {
  const auto run=session.directory.filename().wstring();SYSTEMTIME utc{},local{};
  std::wstring time=run;
  if(run.size()>=19 && swscanf_s(run.c_str(),L"run-%4hu%2hu%2huT%2hu%2hu%2hu",
      &utc.wYear,&utc.wMonth,&utc.wDay,&utc.wHour,&utc.wMinute,&utc.wSecond)==6 &&
      SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local)) {
    wchar_t text[40]{};swprintf_s(text,L"%04u-%02u-%02u  %02u:%02u",local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute);time=text;
  }
  return FromUtf8(FindGame(session.game).name).wstring()+L"  ·  "+time;
}
std::string Read(const fs::path& path,size_t limit) {
  const auto size=fs::file_size(path);
  if(size>limit)throw std::runtime_error("Report exceeds the upload size limit");
  std::string bytes(size,'\0');std::ifstream stream(path,std::ios::binary);
  if(!stream.read(bytes.data(),std::streamsize(size)))throw std::runtime_error("Cannot read report file");
  return bytes;
}
void Write(const fs::path& path,std::string_view text) {
  std::ofstream stream(path,std::ios::binary);stream.write(text.data(),std::streamsize(text.size()));
  if(!stream)throw std::runtime_error("Cannot write report file");
}
struct Internet {
  HINTERNET value{};
  ~Internet(){if(value)WinHttpCloseHandle(value);}
};
void Check(BOOL okay){if(!okay)throw std::runtime_error("Network request failed ("+std::to_string(GetLastError())+")");}
}

void RecordRuntimeFailure(const fs::path& root, std::string_view game) {
  const auto diagnostic = RuntimeDiagnostic(FindGame(game), root);
  static std::mutex gate; static std::map<fs::path, std::string> recorded;
  std::lock_guard lock(gate);
  const auto base = DataChild(root, "logs/" + std::string(game));
  if (recorded[base] == diagnostic) return;
  SYSTEMTIME now{}; GetSystemTime(&now); wchar_t name[96]{};
  swprintf_s(name,L"run-%04u%02u%02uT%02u%02u%02u%03uZ-%lu-launcher",
      now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId());
  const auto directory = base / name; fs::create_directories(directory);
  Write(directory/L"runtime-check.txt", diagnostic);
  recorded[base] = diagnostic;
}
std::vector<Session> Recent(const fs::path& root) {
  const auto logs=DataChild(root,"logs");std::vector<Session> result;
  if(!fs::exists(logs))return result;
  const auto ini=Ini::Load(ConfigPath(root));
  for(const auto& game:Catalogue()) {
    const auto data=Resolve(root,ini.DataDirectory(game));
    if(Within(logs,data) || Within(data,logs))throw std::runtime_error("Log folder overlaps game data");
  }
  for(const auto& game:Catalogue()) {
    const auto directory=DataChild(logs,game.id);
    if(!fs::exists(directory))continue;
    for(const auto& entry:fs::directory_iterator(directory)) {
      if(!Plain(entry.path()) || !entry.is_directory() ||
         !entry.path().filename().wstring().starts_with(L"run-"))continue;
      result.push_back({std::string(game.id),entry.path(),entry.last_write_time()});
    }
  }
  std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.modified>b.modified;});
  if(result.size()>30)result.resize(30);
  return result;
}

std::string ProcessAtUpload(const Session& session) {
  try {
    uint64_t pid{},expected{};
    const auto iniPath=session.directory/L"run.ini",jsonPath=session.directory/L"run.json";
    if(fs::is_regular_file(iniPath) && Plain(iniPath)) {
      const auto ini=Ini::Load(iniPath);
      if(const auto value=ini.Get("run","pid"))pid=std::stoull(value->text);
      if(const auto value=ini.Get("run","process_created"))expected=std::stoull(value->text);
    } else if(fs::is_regular_file(jsonPath) && Plain(jsonPath)) {
      const auto metadata=Json::parse(Read(jsonPath,1024*1024));
      pid=metadata.value("pid",uint64_t{});expected=metadata.value("processCreated",uint64_t{});
    }
    auto text="pid="+std::to_string(pid)+"\nprocess_created="+std::to_string(expected)+"\n";
    if(!pid || pid>MAXDWORD || !expected)return text+"process_state=identity_unavailable\n";
    const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,DWORD(pid));
    if(!process) {
      const auto error=GetLastError();
      return text+"process_state="+(error==ERROR_INVALID_PARAMETER?"not_found":"query_failed")+
          "\nwindows_error="+std::to_string(error)+"\n";
    }
    FILETIME created{},exited{},kernel{},user{};DWORD code{};
    const bool readable=GetProcessTimes(process,&created,&exited,&kernel,&user) && GetExitCodeProcess(process,&code);
    const auto wait=WaitForSingleObject(process,0);CloseHandle(process);
    if(!readable)return text+"process_state=query_failed\n";
    const auto actual=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    if(actual!=expected)return text+"process_state=pid_reused\n";
    if(wait==WAIT_TIMEOUT)return text+"process_state=running\n";
    if(wait==WAIT_OBJECT_0)return text+"process_state=exited\nexit_code="+std::to_string(code)+"\n";
    return text+"process_state=query_failed\n";
  }catch(const std::exception&){return "process_state=metadata_unreadable\n";}
}

fs::path Package(const fs::path& root,const Session& session,std::string_view message) {
  if(message.size()>8000 || !ValidUtf8(message))throw std::runtime_error("Message is too long");
  // Resolve a known run beneath logs; never walk game data or linked folders.
  const auto runs=Recent(root);
  if(std::none_of(runs.begin(),runs.end(),[&](const auto& s){return s.game==session.game && s.directory==session.directory;}))
    throw std::runtime_error("The selected log session is no longer available");
  GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot create report ID");
  wchar_t id[40]{};StringFromGUID2(guid,id,40);
  const auto output=DataChild(root,"user-data/reports/"+Utf8(std::wstring_view(id+1,36)));
  const auto stage=output/L"files";fs::create_directories(stage/L"logs");
  uint64_t bytes=0;unsigned count=0;std::vector<fs::path> dumps;
  for(auto it=fs::recursive_directory_iterator(session.directory);it!=fs::recursive_directory_iterator();++it) {
    if(!Plain(it->path())){if(it->is_directory())it.disable_recursion_pending();continue;}
    if(it.depth()>3){it.disable_recursion_pending();continue;}
    if(!it->is_regular_file())continue;
    if(it->path().extension()==L".dmp") {if(dumps.size()<16)dumps.push_back(it->path());continue;}
    if(!TextFile(it->path()))continue;
    bytes+=it->file_size();
    if(bytes>Limit || ++count>250)throw std::runtime_error("Log session exceeds 128 MiB or 250 text files");
    const auto target=stage/L"logs"/it->path().lexically_relative(session.directory);
    fs::create_directories(target.parent_path());fs::copy_file(it->path(),target);
  }
  unsigned summaryIndex=0;
  for(const auto& dump:dumps) {
    std::string text;
    try{text=SummarizeCrashDump(dump);}
    catch(const std::exception& error){text="source=existing_minidump\nsummary_error="+std::string(error.what())+"\n";}
    // Include metadata from old crashes in the existing upload flow. Original
    // dumps stay on disk; a damaged dump must not suppress the other logs.
    bytes+=text.size();
    if(bytes>Limit || ++count>250)throw std::runtime_error("Log session exceeds 128 MiB or 250 text files");
    fs::path target;
    do {target=stage/L"logs"/(L"auto-crash-"+std::to_wstring(++summaryIndex)+L".txt");} while(fs::exists(target));
    Write(target,"dump="+Utf8(dump.lexically_relative(session.directory))+"\n"+text);
  }
  if(!count)throw std::runtime_error("This session has no text logs");
  const auto runtime = "Runtime files checked at upload time:\n" + RuntimeDiagnostic(FindGame(session.game),root);
  bytes += runtime.size(); ++count;
  if(bytes>Limit || count>250)throw std::runtime_error("Report exceeds the upload size limit");
  Write(stage/L"logs"/L"runtime-at-upload.txt",runtime);
  const auto process_state=ProcessAtUpload(session);
  bytes+=process_state.size();++count;
  if(bytes>Limit || count>250)throw std::runtime_error("Report exceeds the upload size limit");
  Write(stage/L"logs"/L"process-at-upload.txt",process_state);
  const auto settings=ResolveSharedSettings(Ini::Load(ConfigPath(root)));
  Json meta{{"schema",1},{"game",session.game},{"run",Utf8(session.directory.filename())},
            {"nickname",settings.nickname},{"message",message},{"files",count},{"bytes",bytes}};
  try {meta["version"]=Json::parse(Read(root/L"release.json",128*1024)).at("version");}catch(...){meta["version"]="development";}
  Write(stage/L"report.json",meta.dump(2));
  const auto archive=output/L"report.zip";
  std::array<wchar_t,MAX_PATH> system{};if(!GetSystemDirectoryW(system.data(),DWORD(system.size())))throw std::runtime_error("Cannot locate Windows tar");
  const auto tar=fs::path(system.data())/L"tar.exe";
  std::wstring command=QuoteArgument(tar.wstring())+L" -a -c -f "+QuoteArgument(archive.wstring())+L" -C "+QuoteArgument(stage.wstring())+L" .";
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  if(!CreateProcessW(tar.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
    throw std::runtime_error("Cannot start Windows tar to package logs");
  CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);
  DWORD code{};GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);
  if(code || !fs::exists(archive))throw std::runtime_error("Log packaging failed");
  if(fs::file_size(archive)>ZipLimit)throw std::runtime_error("Compressed report exceeds 32 MiB");
  // Only our newly created staging directory is removed. Keep the ZIP for retry.
  fs::remove_all(stage);
  return archive;
}

std::string Upload(std::string endpoint,const fs::path& archive) {
  while(endpoint.ends_with('/'))endpoint.pop_back();
  const auto address=FromUtf8(endpoint+"/reports").wstring();URL_COMPONENTS url{};url.dwStructSize=sizeof(url);
  url.dwHostNameLength=url.dwUrlPathLength=url.dwExtraInfoLength=DWORD(-1);
  url.dwUserNameLength=url.dwPasswordLength=DWORD(-1);
  if(!WinHttpCrackUrl(address.c_str(),DWORD(address.size()),0,&url) ||
     (url.nScheme!=INTERNET_SCHEME_HTTPS && url.nScheme!=INTERNET_SCHEME_HTTP) || url.dwExtraInfoLength || url.dwUserNameLength)
    throw std::runtime_error("Invalid report server address");
  const std::wstring host(url.lpszHostName,url.dwHostNameLength),path(url.lpszUrlPath,url.dwUrlPathLength);
  if(url.nScheme==INTERNET_SCHEME_HTTP && host!=L"127.0.0.1" && host!=L"localhost")throw std::runtime_error("Report server requires HTTPS");
  const auto body=Read(archive,ZipLimit);
  Internet session{WinHttpOpen(L"EVAC-PC-LABEL-reports/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0)};
  if(!session.value)Check(FALSE);
  Check(WinHttpSetTimeouts(session.value,5000,5000,30000,30000));
  Internet connection{WinHttpConnect(session.value,host.c_str(),url.nPort,0)};
  Internet request{WinHttpOpenRequest(connection.value,L"POST",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    url.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0)};
  if(!connection.value || !request.value)Check(FALSE);
  DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;Check(WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects)));
  DWORD autologon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;Check(WinHttpSetOption(request.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&autologon,sizeof(autologon)));
  Check(WinHttpSendRequest(request.value,L"Content-Type: application/zip\r\n",DWORD(-1),const_cast<char*>(body.data()),DWORD(body.size()),DWORD(body.size()),0));
  Check(WinHttpReceiveResponse(request.value,nullptr));DWORD status{},size=sizeof(status);
  Check(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr));
  std::string response;std::array<char,4096> buffer{};DWORD read{};
  while(WinHttpReadData(request.value,buffer.data(),DWORD(buffer.size()),&read) && read) {
    response.append(buffer.data(),read);if(response.size()>16384)throw std::runtime_error("Invalid report response");
  }
  if(status!=201)throw std::runtime_error("Upload failed (HTTP "+std::to_string(status)+")");
  const auto id=Json::parse(response).at("id").get<std::string>();
  if(id.empty() || id.size()>80 || id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyz-")!=std::string::npos)
    throw std::runtime_error("Invalid report ID");
  return id;
}

namespace {
constexpr UINT Finished=WM_APP+41;
struct Dialog {
  HWND hwnd{},combo{},edit{},status{},send{},close{};fs::path root;
  Language language;std::vector<Session> sessions;std::thread worker;
  std::string result,error;bool uploading=false,succeeded=false;
  const wchar_t* T(std::wstring_view key)const{return Translate(language,key);}
  void Start() {
    const auto selected=SendMessageW(combo,CB_GETCURSEL,0,0);if(selected<0)return;
    std::wstring message(GetWindowTextLengthW(edit)+1,L'\0');GetWindowTextW(edit,message.data(),int(message.size()));message.resize(wcslen(message.c_str()));
    if(worker.joinable())worker.join();uploading=true;
    EnableWindow(send,FALSE);EnableWindow(close,FALSE);EnableWindow(combo,FALSE);EnableWindow(edit,FALSE);
    SetWindowTextW(status,T(L"正在打包并上传…"));
    worker=std::thread([this,selected,message=std::move(message)] {
      try {const auto archive=Package(root,sessions.at(size_t(selected)),Utf8(message));
        result=Upload(ResolveSharedSettings(Ini::Load(ConfigPath(root))).endpoint,archive);
      }catch(const std::exception& e){error=e.what();}
      PostMessageW(hwnd,Finished,0,0);
    });
  }
  static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* self=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->hwnd=window;SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(self));}
    if(!self)return DefWindowProcW(window,message,w,l);
    if(message==WM_CTLCOLORSTATIC){SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));}
    if(message==WM_COMMAND) {
      if(LOWORD(w)==IDOK){if(self->succeeded)DestroyWindow(window);else self->Start();return 0;}
      if(LOWORD(w)==IDCANCEL){if(!self->uploading)DestroyWindow(window);return 0;}
      if(LOWORD(w)==103){const auto i=SendMessageW(self->combo,CB_GETCURSEL,0,0);if(i>=0)ShellExecuteW(window,L"open",self->sessions[size_t(i)].directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
    }
    if(message==Finished) {
      if(self->worker.joinable())self->worker.join();self->uploading=false;
      EnableWindow(self->close,TRUE);EnableWindow(self->send,TRUE);
      if(self->error.empty()) {
        self->succeeded=true;SetWindowTextW(self->send,self->T(L"关闭"));
        SetWindowTextW(self->status,(std::wstring(self->T(L"已上传，编号："))+FromUtf8(self->result).wstring()).c_str());
      } else {
        EnableWindow(self->combo,TRUE);EnableWindow(self->edit,TRUE);
        SetWindowTextW(self->status,self->T(L"上传失败，可重试"));
        MessageBoxW(window,FromUtf8(self->error).c_str(),self->T(L"上传日志"),MB_OK|MB_ICONWARNING);self->error.clear();
      }
      return 0;
    }
    if(message==WM_CLOSE){if(!self->uploading)DestroyWindow(window);return 0;}
    return DefWindowProcW(window,message,w,l);
  }
};
}

void Show(HWND owner,const fs::path& root,Language language,std::string_view preferred_game) {
  Dialog dialog;dialog.root=root;dialog.language=language;dialog.sessions=Recent(root);
  if(dialog.sessions.empty()){MessageBoxW(owner,dialog.T(L"还没有游玩日志"),dialog.T(L"上传日志"),MB_OK);return;}
  const auto instance=GetModuleHandleW(nullptr);
  WNDCLASSW wc{};wc.lpfnWndProc=Dialog::Proc;wc.hInstance=instance;wc.lpszClassName=L"EvacReportDialog";
  wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
  const UINT dpi=GetDpiForWindow(owner);const auto scale=[&](int x){return MulDiv(x,int(dpi),96);};
  RECT parent{};GetWindowRect(owner,&parent);
  const auto window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,dialog.T(L"上传日志"),
      WS_CAPTION|WS_SYSMENU,parent.left+scale(35),parent.top+scale(80),scale(570),scale(350),owner,nullptr,instance,&dialog);
  if(!window)throw std::runtime_error("Cannot create upload dialog");
  const auto font=CreateFontW(-scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");
  const auto add=[&](const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id) {
    const auto control=CreateWindowExW(type==std::wstring_view(L"EDIT")?WS_EX_CLIENTEDGE:0,type,text,
       WS_CHILD|WS_VISIBLE|style,scale(x),scale(y),scale(width),scale(height),window,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
    SendMessageW(control,WM_SETFONT,WPARAM(font),TRUE);return control;
  };
  add(L"STATIC",dialog.T(L"游玩记录"),0,20,16,500,20,0);
  dialog.combo=add(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,20,40,514,220,101);
  size_t initial=0;
  for(size_t i=0;i<dialog.sessions.size();++i) {
    const auto& s=dialog.sessions[i];const auto label=SessionLabel(s);
    SendMessageW(dialog.combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
    if(!preferred_game.empty() && s.game==preferred_game){initial=i;preferred_game={};}
  }
  SendMessageW(dialog.combo,CB_SETCURSEL,initial,0);
  add(L"STATIC",dialog.T(L"问题描述（选填）"),0,20,80,500,20,0);
  dialog.edit=add(L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,20,106,514,104,102);
  SendMessageW(dialog.edit,EM_SETLIMITTEXT,2000,0);
  dialog.status=add(L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL,20,220,514,25,0);
  add(L"BUTTON",dialog.T(L"查看本地日志"),WS_TABSTOP,20,263,136,32,103);
  dialog.close=add(L"BUTTON",dialog.T(L"取消"),WS_TABSTOP,342,263,90,32,IDCANCEL);
  dialog.send=add(L"BUTTON",dialog.T(L"上传"),WS_TABSTOP|BS_DEFPUSHBUTTON,444,263,90,32,IDOK);
  EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetFocus(dialog.edit);
  MSG message{};int quit=0;
  while(IsWindow(window) && (quit=GetMessageW(&message,nullptr,0,0))>0) {
    if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  }
  if(dialog.worker.joinable())dialog.worker.join();
  if(IsWindow(window))DestroyWindow(window);
  DeleteObject(font);EnableWindow(owner,TRUE);SetForegroundWindow(owner);
  if(quit==0)PostQuitMessage(int(message.wParam));
}
}
