#pragma once
#include "launcher/update_check.h"

namespace cave::launcher::update {
bool PackagedInstallation(const fs::path& root,std::string_view version) noexcept;
bool RecoveryPending(const fs::path& root);
void DownloadProgram(const fs::path& root,const Release& release);
void InstallProgram(const fs::path& root,const Release& release);
void RecoverProgram(const fs::path& root);
void StartProgramWorker(const fs::path& root,std::wstring_view action);
int RunProgramWorker(const fs::path& root,std::wstring_view action,unsigned long parent);
}
