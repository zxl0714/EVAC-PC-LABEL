#include "config/settings.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace espgaluda2;
void Require(bool valid, const char* reason) {
    if (!valid) throw std::runtime_error(reason);
}
void Reject(const std::string& text, const std::string& expected = {}) {
    try { ParseSettings(text); }
    catch (const std::runtime_error& error) {
        Require(std::string(error.what()).find(expected) != std::string::npos,"Error lost its context");
        return;
    }
    throw std::runtime_error("Invalid configuration was accepted");
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc==2,"Usage: settings_test <default.ini>");
        auto defaults = ParseSettings("");
        auto file = LoadSettings(argv[1]);
        Require(!file.fullscreen && !file.vsync && file.gamepad_enabled && file.community_enabled &&
            file.deadzone==0.2 && file.volume==1.0 && file.nickname=="Player" && file.endpoint=="https://xiaolu.life/cave/api/v1","Default template differs");
        for (std::size_t n=0; n<kButtons.size(); ++n)
            Require(file.keyboard[n].keys==defaults.keyboard[n].keys,"Default keyboard template differs");
        Require(!defaults.vsync && ParseSettings("[display]\nvsync=true\n").vsync,
                "Explicit vertical synchronization preference changed");
        Require(ParseSettings("\xEF\xBB\xBF[DISPLAY]\r\nfullscreen = TRUE\r\n").fullscreen,"BOM/CRLF/case handling");
        auto multi = ParseSettings("[keyboard]\nsouth = Z, Numpad0\nl3 = none\n");
        Require(multi.keyboard[4].keys.size()==2 && multi.keyboard[14].keys.empty(),"Multi-key/none parsing");
        Require(ParseSettings("[community]\nnickname = 大佐\n").nickname=="大佐","UTF-8 nickname");
        Require(!ParseSettings("[community]\nenabled=false\n").community_enabled,
                "Explicit offline preference must override the online default");
        Require(ParseSettings("[gamepad]\ndeadzone = 0\nenabled = false\n[audio]\nvolume=0\n").volume==0,"Numeric zero bounds");
        Require(KeyCode("Enter")==KeyCode("Return") && KeyCode("z")==90 && !KeyCode("Mouse5"),"Key aliases");
        for (const auto& text : std::vector<std::string>{
            "[display]\noops=1", "[keyboard]\nshot=Z", "a=Z", "[display]\nfullscreen=1",
            "[display]\nfullscreen=true ; inline", "[display]\nfullscreen=false\nFULLSCREEN=true",
            "[gamepad]\ndeadzone=nan", "[gamepad]\ndeadzone=inf", "[gamepad]\ndeadzone=1",
            "[gamepad]\ndeadzone=-0.01", "[audio]\nvolume=1.1", "[audio]\nvolume=0garbage",
            "[keyboard]\nsouth=", "[keyboard]\nsouth=Z,", "[keyboard]\nsouth=Z,z", "[keyboard]\nstart=Enter,Return",
            "[keyboard]\nsouth=F8", "[keyboard]\nsouth=Backtick", "[keyboard]\nsouth=none,Z", "[keyboard]\nsouth=Unknown",
            "[keyboard]\nsouth=X", "[keyboard]\nwest=Z", "[logging]\nlevel=quiet", "[community]\nnickname=",
            "[community]\nendpoint=ftp://example.org", "[community]\nendpoint=https://bad host"}) Reject(text);
        Reject("[keyboard]\n; context\nsouth = X", "line 3 [keyboard].south");
        Reject(std::string(1048577,' '),"1 MiB");
        Reject(std::string("[community]\nnickname=")+char(0xff),"UTF-8");
        Require(!ValidUtf8(std::string("x\0y",3)) && !ValidUtf8("\xC0\x80") &&
                !ValidUtf8("\xED\xA0\x80") && !ValidUtf8("\xF4\x90\x80\x80") &&
                !ValidUtf8("\xE2\x82") && ValidUtf8("\xF0\x9F\x8E\xAE"),"UTF-8 bounds");
        std::cout << "Settings authored regressions passed; no game, devices or windows opened\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
