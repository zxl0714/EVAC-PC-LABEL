#pragma once
#include "common/paths.h"
#include <Windows.h>
#include <algorithm>
#include <stdexcept>

namespace cave::launcher {
inline UINT ActivationMessage() {
  static const auto message=RegisterWindowMessageW(L"EVACPCLabel.Launcher.Activate.v1");
  if(!message)throw std::runtime_error("Cannot register launcher activation");
  return message;
}
class LauncherInstance final {
 public:
  explicit LauncherInstance(const fs::path& root):property_(L"EVACPCLabel.Launcher.v1."+CollectionIdentity(root)) {
    const auto name=L"Local\\"+property_;
    mutex_=CreateMutexW(nullptr,FALSE,name.c_str());
    if(!mutex_)throw std::runtime_error("Cannot create launcher instance lock");
  }
  LauncherInstance(const LauncherInstance&)=delete;
  LauncherInstance& operator=(const LauncherInstance&)=delete;
  ~LauncherInstance() {
    Unpublish();
    if(owns_)ReleaseMutex(mutex_);
    if(mutex_)CloseHandle(mutex_);
  }
  // True means this process owns startup. False means an existing GUI has
  // acknowledged activation. An abandoned owner can be replaced safely.
  bool ActivateOrAcquire() {
    if(owns_)return true;
    const auto deadline=GetTickCount64()+5000;
    for(;;) {
      const auto wait=WaitForSingleObject(mutex_,0);
      if(wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED) {owns_=true;return true;}
      if(wait!=WAIT_TIMEOUT)throw std::runtime_error("Cannot inspect launcher instance lock");
      const auto window=FindWindow();
      if(window) {
        DWORD process{};GetWindowThreadProcessId(window,&process);
        if(process)AllowSetForegroundWindow(process);
        DWORD_PTR acknowledged{};
        if(SendMessageTimeoutW(window,ActivationMessage(),0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1500,&acknowledged) && acknowledged==1)
          return false;
        // Do not kill a busy GUI or start another one beside it.
        if(IsWindow(window))throw std::runtime_error("The existing launcher is not responding. Please wait and try again.");
      }
      const auto now=GetTickCount64();
      if(now>=deadline)throw std::runtime_error("The existing launcher is still starting. Please try again shortly.");
      const auto waited=WaitForSingleObject(mutex_,static_cast<DWORD>((std::min)(deadline-now,ULONGLONG{50})));
      if(waited==WAIT_OBJECT_0 || waited==WAIT_ABANDONED) {owns_=true;return true;}
      if(waited!=WAIT_TIMEOUT)throw std::runtime_error("Cannot wait for launcher startup");
    }
  }
  void Publish(HWND window) {
    if(!owns_ || window_ || !window || !SetPropW(window,property_.c_str(),reinterpret_cast<HANDLE>(1)))
      throw std::runtime_error("Cannot publish launcher window");
    window_=window;
  }
  void Unpublish() {
    if(window_) {if(IsWindow(window_))RemovePropW(window_,property_.c_str());window_=nullptr;}
  }
 private:
  HWND FindWindow() const {
    struct Search {const wchar_t* property;HWND window{};} search{property_.c_str()};
    EnumWindows([](HWND window,LPARAM argument)->BOOL {
      auto& search=*reinterpret_cast<Search*>(argument);
      if(GetPropW(window,search.property)==reinterpret_cast<HANDLE>(1)) {search.window=window;return FALSE;}
      return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.window;
  }
  std::wstring property_;
  HANDLE mutex_{};
  HWND window_{};
  bool owns_{};
};
}
