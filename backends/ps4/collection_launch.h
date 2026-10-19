#pragma once

namespace cave::ps4 {
bool IsCollectionLaunch(int argc,wchar_t** argv);
int RunCollection(int argc,wchar_t** argv,int (*command)(int,wchar_t**));
bool CollectionInputDiagnosticSelfTest();
// Called only after the window-close path has completed presentation cleanup.
void RecordCollectionWindowClose() noexcept;
}
