#include "diagnostics/run_context.h"
#include "build_metadata.h"
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc<2 || argc>5 || (argc==5 && std::wstring_view(argv[4])!=L"--check-profile-lock"))
            throw std::runtime_error("Usage: preflight <collection root> [isolated profile] [data directory] [--check-profile-lock]");
        const auto run=cave::xbox360::PrepareRun(argv[1],espgaluda2::kBuildIdentity,argc>=3?std::filesystem::path(argv[2]):std::filesystem::path{},
                                            argc>=4?std::filesystem::path(argv[3]):std::filesystem::path{});
        if (argc==5) {
            bool blocked=false;
            try { cave::xbox360::PrepareRun(argv[1],espgaluda2::kBuildIdentity,run.profile,argv[3]); }
            catch (const std::runtime_error& error) {
                blocked=std::string_view(error.what()).find("already in use")!=std::string_view::npos;
            }
            if (!blocked) throw std::runtime_error("Concurrent profile access was not rejected");
            std::cout<<"Concurrent profile access correctly rejected\n";
        }
        std::wcout<<L"Startup preflight passed; no game/device/window initialized. Report: "<<run.directory.c_str()<<L'\n';
        return 0;
    } catch (const std::exception& error) {
        if (argc>=2) {try {cave::xbox360::RecordEarlyStartupError(argv[1],"espgaluda2",error.what());} catch (...) {}}
        std::cerr<<error.what()<<'\n';return 2;
    }
}
