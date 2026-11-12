#pragma once
#include "common/daioujou_music.h"
#include "launcher/strings.h"

namespace cave::launcher {
inline std::wstring ArrangeWarning(const daioujou::ArrangeStatus& status,Language language) {
  if(!status.Problem())return {};
  const auto key=status.state==daioujou::ArrangeState::Missing?L"X Arrange 未启用：缺少 ":
      L"X Arrange 未启用：文件不匹配或无法读取 ";
  return std::wstring(Translate(language,key))+FromUtf8(status.file.empty()?"x-arrange/":status.file).native();
}
}
