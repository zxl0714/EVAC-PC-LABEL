#pragma once
#include "launcher/update_check.h"

namespace cave::launcher::update {
bool PackagedInstallation(const fs::path& root,std::string_view version) noexcept;
bool RecoveryPending(const fs::path& root);
// True when a 206 response's Content-Range continues a download at 'start'
// and runs to the end of a 'total'-byte archive.
bool ResumedRange(std::wstring_view header,uint64_t start,uint64_t total);
fs::path PartialDownload(const fs::path& root,const Release& release);
void DownloadProgram(const fs::path& root,const Release& release);
void InstallProgram(const fs::path& root,const Release& release);
void RecoverProgram(const fs::path& root);
void StartProgramWorker(const fs::path& root,std::wstring_view action);
int RunProgramWorker(const fs::path& root,std::wstring_view action,unsigned long parent);
}
