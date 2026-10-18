#include "diagnostics/run_context.h"
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc!=2 && argc!=3) throw std::runtime_error("Usage: preflight <project/package root> [isolated profile]");
        const auto run=espgaluda2::PrepareRun(argv[1],argc==3?std::filesystem::path(argv[2]):std::filesystem::path{});
        std::wcout<<L"Startup preflight passed; no game/device/window initialized. Report: "<<run.directory.c_str()<<L'\n';
        return 0;
    } catch (const std::exception& error) {
        if (argc>=2) {try {espgaluda2::RecordEarlyStartupError(argv[1],error.what());} catch (...) {}}
        std::cerr<<error.what()<<'\n';return 2;
    }
}
