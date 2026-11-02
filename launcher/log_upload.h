#pragma once
#include "common/paths.h"
#include "launcher/strings.h"
#include <Windows.h>
#include <functional>

namespace cave::launcher::reports {
struct Session { std::string game; fs::path directory; fs::file_time_type modified; };
std::vector<Session> Recent(const fs::path& root);
fs::path Package(const fs::path& root,const Session& session,std::string_view message);
std::string Upload(std::string endpoint,const fs::path& archive);
void Show(HWND owner,const fs::path& root,Language language,std::string_view preferred_game={});
}
