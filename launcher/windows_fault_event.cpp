#include "windows_fault_event.h"
#include "common/paths.h"
#include <windows.h>
#include <winevt.h>
#include <iterator>
#include <memory>
#include <vector>

namespace cave::launcher::reports {
namespace {
using Event = std::unique_ptr<void, decltype(&EvtClose)>;

std::uint64_t Number(const EVT_VARIANT& value) {
  switch(value.Type) {
    case EvtVarTypeHexInt32: case EvtVarTypeUInt32: return value.UInt32Val;
    case EvtVarTypeHexInt64: case EvtVarTypeUInt64: return value.UInt64Val;
    default: return 0;
  }
}

std::string Field(const EVT_VARIANT& value) {
  if(value.Type!=EvtVarTypeString || !value.StringVal)return {};
  std::wstring text(value.StringVal);
  if(text.size()>512 || text.find_first_of(L"\r\n")!=text.npos)return {};
  return Utf8(text);
}
}

std::string WindowsApplicationFault(unsigned long pid, std::uint64_t created) {
  // Event 1000 belongs to the faulting process only when both identity fields match.
  const auto filter=L"*[System[Provider[@Name='Application Error'] and EventID=1000] and EventData[Data[@Name='ProcessId']="+
                    std::to_wstring(pid)+L"]]";
  Event query(EvtQuery(nullptr,L"Application",filter.c_str(),EvtQueryChannelPath|EvtQueryReverseDirection),EvtClose);
  const auto failure=[](DWORD error) {return "windows_fault=query_unavailable\nwindows_event_error="+std::to_string(error)+"\n";};
  if(!query)return failure(GetLastError());
  LPCWSTR paths[]{L"Event/EventData/Data[@Name='ProcessId']",L"Event/EventData/Data[@Name='ProcessCreationTime']",
                  L"Event/EventData/Data[@Name='ModuleName']",L"Event/EventData/Data[@Name='ModuleVersion']",
                  L"Event/EventData/Data[@Name='ExceptionCode']",L"Event/EventData/Data[@Name='FaultingOffset']"};
  Event context(EvtCreateRenderContext(DWORD(std::size(paths)),paths,EvtRenderContextValues),EvtClose);
  if(!context)return failure(GetLastError());
  for(;;) {
    EVT_HANDLE raw{};DWORD returned{};
    if(!EvtNext(query.get(),1,&raw,500,0,&returned)) {
      const auto error=GetLastError();
      return error==ERROR_NO_MORE_ITEMS ? "windows_fault=no_matching_event\n" : failure(error);
    }
    Event event(raw,EvtClose);
    DWORD bytes{},count{};
    if(EvtRender(context.get(),event.get(),EvtRenderEventValues,0,nullptr,&bytes,&count) ||
       GetLastError()!=ERROR_INSUFFICIENT_BUFFER || bytes>65536)continue;
    std::vector<EVT_VARIANT> values((bytes+sizeof(EVT_VARIANT)-1)/sizeof(EVT_VARIANT));
    if(!EvtRender(context.get(),event.get(),EvtRenderEventValues,bytes,values.data(),&bytes,&count) ||
       count!=std::size(paths) || Number(values[0])!=pid || Number(values[1])!=created)continue;
    const auto module=Field(values[2]),code=Field(values[4]),offset=Field(values[5]);
    if(module.empty() || code.empty() || offset.empty())continue;
    return "windows_fault=application_error_1000\nwindows_fault_module="+Utf8(FromUtf8(module).filename())+
           "\nwindows_fault_module_version="+Field(values[3])+"\nwindows_exception_code_hex="+code+
           "\nwindows_fault_offset_hex="+offset+"\n";
  }
}
}
