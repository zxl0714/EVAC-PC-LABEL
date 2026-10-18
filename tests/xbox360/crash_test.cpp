#include "diagnostics/crash_reporter.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc!=2) throw std::runtime_error("Usage: crash_test <NEW-test-directory>");
        const auto directory=std::filesystem::absolute(argv[1]);
        if (std::filesystem::exists(directory)) throw std::runtime_error("Test directory must be new");
        std::filesystem::create_directories(directory);
        if (!espgaluda2::StartCrashReporter(directory) || !espgaluda2::CrashReporterSelfTest())
            throw std::runtime_error("External crash reporter did not complete");
        std::ifstream file(directory/"game-crash.txt",std::ios::binary);
        std::string text(std::istreambuf_iterator<char>{file},{});
        if (text.find("REASON=authored-self-test")==text.npos || text.find("DUMP_OK=1")==text.npos ||
            !std::filesystem::is_regular_file(directory/"game-crash.dmp") ||
            std::filesystem::file_size(directory/"game-crash.dmp")<1024)
            throw std::runtime_error("Incomplete synthetic crash report/dump");
        std::cout<<"External crash writer passed; synthetic context only, no OS crash or window\n";
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
