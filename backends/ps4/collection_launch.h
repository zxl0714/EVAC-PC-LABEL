#pragma once

namespace cave::ps4 {
struct CollectionConfiguration;
bool IsCollectionLaunch(int argc,wchar_t** argv);
int RunCollection(int argc,wchar_t** argv,int (*command)(int,wchar_t**,const CollectionConfiguration*));
bool CollectionInputDiagnosticSelfTest();
// Called only after the window-close path has completed presentation cleanup.
void RecordCollectionWindowClose() noexcept;
}
