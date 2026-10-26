#include "native_dialogs.h"
#include "native_diagnostics.h"
#include "native_screenshot.h"
#include "native_ngs2.h"
#include "native_avplayer.h"
#include "native_content.h"
#include "native_trophy.h"
#include "native_np.h"
#include "native_np_score.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>

#define KT_SYSV __attribute__((sysv_abi))

namespace {
using U32 = std::uint32_t;
constexpr int CommonError(unsigned code) { return static_cast<int>(0x80b80000u | code); }
constexpr int kNoSystem = CommonError(1), kSystemAlready = CommonError(2);
constexpr int kNotInitialized = CommonError(3), kAlreadyInitialized = CommonError(4);
constexpr int kNotFinished = CommonError(5), kState = CommonError(6), kBusy = CommonError(8);
constexpr int kMemory = CommonError(9), kParameter = CommonError(10), kNotRunning = CommonError(11);
constexpr int kNull = CommonError(13), kFatal = CommonError(14), kUnsupported = CommonError(15);
constexpr int kInvalidModule = static_cast<int>(0x805a1000u);
constexpr int kModuleNotLoaded = static_cast<int>(0x805a1001u);
constexpr int kModuleBusy = static_cast<int>(0x80020010u);
constexpr UINT kRefresh = WM_APP + 100, kFinish = WM_APP + 101;
constexpr wchar_t kClass[] = L"KetsuiNativeDialog";

// ABI layout facts, kept independent of any emulator's C++ types.
struct BaseParam { std::uint64_t size; unsigned char reserved[36]; U32 magic; };
struct Buttons { const char* first; const char* second; unsigned char reserved[32]; };
struct UserParam { U32 type, padding; const char* message; const Buttons* buttons; unsigned char reserved[24]; };
struct ProgressParam { U32 type, padding; const char* message; unsigned char reserved[64]; };
struct SystemParam { U32 type; unsigned char reserved[32]; };
struct MessageParam {
    BaseParam base;
    std::uint64_t size;
    U32 mode, padding;
    const UserParam* user;
    const ProgressParam* progress;
    const SystemParam* system;
    int user_id;
    unsigned char reserved[40];
    U32 padding2;
};
struct ErrorParam { int size, code, user_id, reserved; };
struct Result { U32 reserved_mode{}, result{}, button{}; unsigned char reserved[32]{}; };
struct SaveUserParam { U32 buttons, kind; const char* message; unsigned char reserved[32]; };
struct SaveSystemParam { U32 kind, padding; std::uint64_t blocks; unsigned char reserved[32]; };
struct SaveErrorParam { U32 code; unsigned char reserved[32]; };
struct SaveProgressParam { U32 type, padding; const char* message; U32 system_message; unsigned char reserved[28]; };
struct SaveOption { U32 back; unsigned char reserved[32]; };
struct SaveItems {
    int user, padding;
    const void* title;
    const char (*directories)[32];
    U32 count, padding2;
};
struct SaveParam {
    BaseParam base;
    U32 size, mode, operation, padding;
    const void* animation;
    const SaveItems* items;
    const SaveUserParam* user;
    const SaveSystemParam* system;
    const SaveErrorParam* error;
    const SaveProgressParam* progress;
    void* cookie;
    const SaveOption* option;
    unsigned char reserved[24];
};
struct SaveResult {
    U32 mode, result, button, padding;
    char (*directory)[32];
    void* metadata;
    void* cookie;
    unsigned char reserved[32];
};
struct ProfileParam {
    BaseParam base;
    std::uint64_t size;
    U32 mode;
    int user;
    char online[17];
    unsigned char padding[7];
    void* cookie;
    unsigned char reserved[32];
};
struct ProfileAccountParam {
    BaseParam base;
    std::uint64_t size;
    U32 mode;
    int user;
    std::uint64_t account, padding;
    void* cookie;
    unsigned char reserved[32];
};
struct ProfileResult { int result; U32 action; void* cookie; unsigned char reserved[32]; };
static_assert(sizeof(ProfileParam) == 128 && offsetof(ProfileParam, cookie) == 88);
static_assert(sizeof(ProfileAccountParam) == 120 && offsetof(ProfileAccountParam, account) == 64);
static_assert(offsetof(ProfileAccountParam, cookie) == 80 && sizeof(ProfileResult) == 48);
static_assert(sizeof(BaseParam) == 48 && offsetof(BaseParam, magic) == 44);
static_assert(sizeof(MessageParam) == 136 && offsetof(MessageParam, user) == 64);
static_assert(sizeof(UserParam) == 48 && sizeof(ProgressParam) == 80);
static_assert(sizeof(ErrorParam) == 16 && sizeof(Result) == 44);
static_assert(sizeof(SaveParam) == 152 && offsetof(SaveParam, user) == 80 && offsetof(SaveParam, cookie) == 112);
static_assert(sizeof(SaveUserParam) == 48 && sizeof(SaveProgressParam) == 48 && sizeof(SaveResult) == 72);

struct Display {
    U32 mode{}, type{}, value{};
    bool error{}, cancel{}, profile{};
    unsigned default_button{1};
    std::wstring message, first, second;
    U32 api_mode{};
    void* cookie{};
    std::string directory;
};
struct Session;
struct Job {
    Session* session{};
    Display display;
    HANDLE ready{}, thread{};
    HWND window{}, label{}, progress{};
    bool failed{};
    Result outcome{0, 1, 0, {}};
    ~Job() {
        if (thread) { CloseHandle(thread); }
        if (ready) { CloseHandle(ready); }
    }
};
struct Session {
    const unsigned id;
    const char* const name;
    unsigned references{}, status{};
    bool changing{};
    std::shared_ptr<Job> job;
};
std::mutex gate;
Session message_session{0xa4, "libSceMsgDialog", 0, 0, false, {}};
Session error_session{0xac, "libSceErrorDialog", 0, 0, false, {}};
Session save_session{0xa0, "libSceSaveDataDialog", 0, 0, false, {}};
Session profile_session{0xb2, "libSceNpProfileDialog", 0, 0, false, {}};
bool common_initialized{};
bool hidden_dialog_test{}; // set only by the standalone native UI self-test
INIT_ONCE ui_once = INIT_ONCE_STATIC_INIT;

template <typename Function> int Api(Function&& function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc&) { return kMemory; }
    catch (...) { return kFatal; }
}

bool Utf8(const char* text, std::wstring& result) {
    if (!text) { return false; }
    std::size_t size = 0;
    while (size < 16384 && text[size]) { ++size; }
    if (size == 16384) { return false; }
    if (!size) { result.clear(); return true; }
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, static_cast<int>(size), nullptr, 0);
    if (!count) { return false; }
    result.resize(static_cast<std::size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, static_cast<int>(size), result.data(), count) == count;
}

int Describe(const MessageParam* param, Display& display) {
    if (!param) { return kNull; }
    if (param->size != sizeof(MessageParam) || param->base.size != sizeof(BaseParam)) { return kParameter; }
    display.mode = param->mode;
    if (param->mode == 1) {
        if (!param->user || !Utf8(param->user->message, display.message)) { return kParameter; }
        display.type = param->user->type;
        switch (display.type) {
        case 0: display.first = L"OK"; break;
        case 1: case 7: display.first = L"Yes"; display.second = L"No"; break;
        case 2: case 5: display.default_button = 0; break;
        case 3: case 8: display.first = L"OK"; display.second = L"Cancel"; display.cancel = true; break;
        case 6: display.second = L"Cancel"; display.cancel = true; display.default_button = 2; break;
        case 9:
            if (!param->user->buttons || !Utf8(param->user->buttons->first, display.first) ||
                !Utf8(param->user->buttons->second, display.second) || display.first.empty() || display.second.empty()) {
                return kParameter;
            }
            break;
        default: return kParameter;
        }
        if (display.type == 7 || display.type == 8) { display.default_button = 2; }
    } else if (param->mode == 2) {
        if (!param->progress || param->progress->type > 1 ||
            !Utf8(param->progress->message, display.message)) { return kParameter; }
        display.type = param->progress->type;
        display.cancel = display.type == 1;
        display.default_button = display.cancel ? 2 : 0;
        if (display.cancel) { display.second = L"Cancel"; }
    } else if (param->mode == 3) {
        if (!param->system) { return kParameter; }
        switch (param->system->type) {
        case 0: display.message = L"This item is not available in the store."; break;
        case 1: display.message = L"Online chat is restricted for this user."; break;
        case 2: display.message = L"User-generated content is restricted for this user."; break;
        case 4: display.message = L"No camera is connected."; break;
        case 5: display.message = L"Your profile picture and name are not shared."; break;
        default: return kUnsupported;
        }
        display.first = L"OK";
    } else { return kUnsupported; }
    return 0;
}

int DescribeSave(const SaveParam* param, Display& display) {
    if (!param) { return kNull; }
    if (param->size != sizeof(SaveParam) || param->base.size != sizeof(BaseParam) ||
        param->operation < 1 || param->operation > 3 || (param->option && param->option->back > 1)) { return kParameter; }
    display.api_mode = param->mode;
    display.cookie = param->cookie;
    display.mode = 1;
    display.first = L"OK";
    if (param->items && param->items->count) {
        if (param->items->count > 1) { return kUnsupported; }
        if (!param->items->directories) { return kParameter; }
        const auto* name = (*param->items->directories);
        std::size_t length = 0;
        while (length < 32 && name[length]) { ++length; }
        if (length == 32) { return kParameter; }
        display.directory.assign(name, length);
    }
    const auto action = param->operation == 1 ? L"Saving data..." : param->operation == 2 ? L"Loading data..." : L"Deleting data...";
    switch (param->mode) {
    case 2:
        if (!param->user || param->user->kind > 1 || param->user->buttons > 3 || !Utf8(param->user->message, display.message)) { return kParameter; }
        display.type = param->user->buttons;
        if (display.type == 1) { display.first = L"Yes"; display.second = L"No"; }
        if (display.type == 2) { display.first.clear(); display.default_button = 0; }
        if (display.type == 3) { display.second = L"Cancel"; display.cancel = true; }
        break;
    case 3:
        if (!param->system) { return kParameter; }
        switch (param->system->kind) {
        case 1: display.message = L"No saved data is available."; break;
        case 2:
            display.message = param->operation == 1 ? L"Save this data?" : param->operation == 2 ? L"Load this data?" : L"Delete this data?";
            display.first = L"Yes"; display.second = L"No"; break;
        case 3: display.message = L"Replace the existing saved data?"; display.first = L"Yes"; display.second = L"No"; break;
        case 4: case 8:
            display.message = L"There is not enough storage space. Required free blocks: " + std::to_wstring(param->system->blocks); break;
        case 5:
            display.message = action; display.first.clear(); display.default_button = 0;
            if (param->option && !param->option->back) { display.second = L"Cancel"; display.cancel = true; display.default_button = 2; }
            break;
        case 6: display.message = L"The saved data is damaged."; break;
        case 7: display.message = L"The saved-data operation has finished."; break;
        case 10: case 11: case 13:
            display.message = param->system->kind == 13 ? L"Restore the damaged saved data from its backup?" : L"The saved data is damaged. Continue with its replacement?";
            if (!param->option || !param->option->back) { display.second = L"Cancel"; display.cancel = true; }
            break;
        case 14: display.message = L"The saved-data storage limit has been reached."; break;
        default: return kUnsupported;
        }
        break;
    case 4:
        if (!param->error) { return kParameter; }
        {
            wchar_t text[96];
            std::swprintf(text, std::size(text), L"Saved-data error: 0x%08X", param->error->code);
            display.message = text;
        }
        break;
    case 5:
        if (!param->progress || param->progress->type || param->progress->system_message > 2) { return kParameter; }
        display.mode = 2; display.first.clear(); display.default_button = 0;
        if (param->progress->message) {
            if (!Utf8(param->progress->message, display.message)) { return kParameter; }
        } else { display.message = param->progress->system_message == 2 ? L"Restoring saved data..." : action; }
        break;
    default: return kUnsupported; // list selection needs storage enumeration and metadata
    }
    return 0;
}

Result ButtonOutcome(const Display& display, unsigned button) {
    if (button == 2 && display.cancel) { return {0, 1, 0, {}}; }
    return {0, 0, button, {}};
}

void FinishWindow(HWND window, Job* job, Result result) {
    {
        std::lock_guard lock(gate);
        job->outcome = result;
    }
    DestroyWindow(window);
}

LRESULT WindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* job = reinterpret_cast<Job*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        job = static_cast<Job*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(job));
    }
    if (!job) { return DefWindowProcW(window, message, wparam, lparam); }
    if (message == WM_CREATE) {
        Display display;
        { std::lock_guard lock(gate); display = job->display; }
        const auto font = reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));
        job->label = CreateWindowExW(0, L"STATIC", display.message.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                    24, 24, 504, 135, window, nullptr, nullptr, nullptr);
        if (!job->label) { return -1; }
        SendMessageW(job->label, WM_SETFONT, font, TRUE);
        if (display.mode == 2) {
            job->progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                           24, 163, 504, 22, window, nullptr, nullptr, nullptr);
            if (!job->progress) { return -1; }
            SendMessageW(job->progress, PBM_SETRANGE32, 0, 100);
            SendMessageW(job->progress, PBM_SETPOS, display.value, 0);
        }
        unsigned created = 0;
        for (unsigned button = 1; button <= 2; ++button) {
            const auto& label = button == 1 ? display.first : display.second;
            if (label.empty()) { continue; }
            const bool two = !display.first.empty() && !display.second.empty();
            const int x = two ? 316 + static_cast<int>(button - 1) * 112 : 428;
            const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                (button == display.default_button ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
            const auto child = CreateWindowExW(0, L"BUTTON", label.c_str(), style, x, 207, 100, 30,
                                               window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(button)), nullptr, nullptr);
            if (!child) { return -1; }
            SendMessageW(child, WM_SETFONT, font, TRUE);
            ++created;
        }
        if (!display.cancel && !display.error) {
            EnableMenuItem(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        }
        return created || display.first.empty() ? 0 : -1;
    }
    if (message == DM_GETDEFID) {
        return job->display.default_button ? MAKELONG(job->display.default_button, DC_HASDEFID) : 0;
    }
    if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
        const unsigned button = LOWORD(wparam);
        if (job->display.profile && button == IDCANCEL) {
            FinishWindow(window, job, {0, 1, 0, {}});
            return 0;
        }
        if ((button == 1 && !job->display.first.empty()) || (button == 2 && !job->display.second.empty())) {
            FinishWindow(window, job, ButtonOutcome(job->display, button));
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        if (job->display.cancel || job->display.error) { FinishWindow(window, job, {0, 1, 0, {}}); }
        return 0;
    }
    if (message == kRefresh) {
        std::wstring text;
        unsigned value;
        { std::lock_guard lock(gate); text = job->display.message; value = job->display.value; }
        SetWindowTextW(job->label, text.c_str());
        if (job->progress) { SendMessageW(job->progress, PBM_SETPOS, value, 0); }
        return 0;
    }
    if (message == kFinish) {
        FinishWindow(window, job, {0, 0, 0, {}});
        return 0;
    }
    if (message == WM_DESTROY) {
        {
            std::lock_guard lock(gate);
            if (job->session->job.get() == job) {
                job->session->status = 3;
                std::printf("{\"event\":\"native_dialog_finished\",\"module_id\":%u,\"result\":%u,\"button\":%u}\n",
                            job->session->id, job->outcome.result, job->outcome.button);
            }
        }
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        std::lock_guard lock(gate);
        job->window = nullptr;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    try { return WindowMessage(window, message, wparam, lparam); }
    catch (...) {
        std::fputs("{\"event\":\"native_dialog_host_error\",\"stage\":\"window_message\"}\n", stderr);
        if (message == WM_CREATE || message == WM_NCCREATE) { return message == WM_CREATE ? -1 : FALSE; }
        // A host UI failure must not be reported as a successful user choice.
        UnsupportedApi("native_dialog_window_failure", message, nullptr);
    }
}

BOOL CALLBACK PrepareUi(PINIT_ONCE, PVOID, PVOID*) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS};
    if (!InitCommonControlsEx(&controls)) { return FALSE; }
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kClass;
    return RegisterClassExW(&window_class) != 0;
}

DWORD WINAPI WindowThread(void* raw) noexcept {
    std::unique_ptr<std::shared_ptr<Job>> argument(static_cast<std::shared_ptr<Job>*>(raw));
    const auto job = *argument;
    argument.reset();
    const int x = (GetSystemMetrics(SM_CXSCREEN) - 570) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - 300) / 2;
    const auto window = CreateWindowExW(WS_EX_CONTROLPARENT, kClass,
                      job->display.profile ? L"EVAC PC LABEL - PLAYER CARD" :
                      job->session == &save_session ? L"Ketsui PC - Save data" :
                      job->display.error ? L"Ketsui PC - Error" : L"Ketsui PC - Message",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, y, 570, 300,
                      nullptr, nullptr, GetModuleHandleW(nullptr), job.get());
    {
        std::lock_guard lock(gate);
        job->window = window;
        job->failed = !window;
    }
    SetEvent(job->ready);
    if (!window) { return 1; }
    if (!hidden_dialog_test) {
        ShowWindow(window, SW_SHOWNORMAL);
        UpdateWindow(window);
        if (job->display.default_button) { SetFocus(GetDlgItem(window, static_cast<int>(job->display.default_button))); }
    }
    MSG message{};
    int received;
    while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (received == -1) { UnsupportedApi("native_dialog_message_pump", GetLastError(), nullptr); }
    return 0;
}

Session* Module(unsigned id) {
    return id == message_session.id ? &message_session : id == error_session.id ? &error_session :
           id == save_session.id ? &save_session : id == profile_session.id ? &profile_session : nullptr;
}

int KT_SYSV ModuleLoad(unsigned id) {
    if (id == 0x1e) { return CommunityScoreModule(CommunityScoreModuleAction::Load); }
    if (id == 0xa5) { return AvPlayerModule(AvPlayerModuleAction::Load); }
    if (id == 0xb4) { return ContentModule(ContentModuleAction::Load, __builtin_return_address(0)); }
    if (id == 0xb) { return Ngs2Module(Ngs2ModuleAction::Load); }
    if (id == 0x9c) { return ScreenshotModule(CaptureModuleAction::Load); }
    if (id == 0xad) { return TrophyModule(TrophyModuleAction::Load); }
    if (!id) { return kInvalidModule; }
    // These optional platform services have no installed PC provider. Report
    // absence without publishing a handle or incrementing a reference count.
    // The first three M2 startup requests are unconditional. Its raw-USB
    // initializer checks libSceUsbd load failure at main +0x233b18 and exits
    // that path. This runtime exposes controllers through NativePad/XInput,
    // not a host USB driver/passthrough provider. Later direct use must still
    // resolve a real implementation, never a success placeholder.
    // The online-context initializer also requests Json2, then separately
    // checks NetInit at main +0x13cccd. Neither provider is installed here.
    // PngEnc (0x8d) is not SaveDataDialog (0xa0). The startup PNG helper
    // tail-calls LoadModule at main +0x1ee518; no encoder is installed.
    // NpProfileDialog (0xb2) has a local player-card provider below; it
    // never opens a Sony session. Rudp still has no provider.
    if (id == 7 || id == 0x81 || id == 0x85 || id == 0x8d || id == 0xb7 || id == 0xe7 ||
        id == 0x21) {
        TraceNativeApi("sceSysmoduleLoadModule_unavailable", id, __builtin_return_address(0));
        return kModuleNotLoaded;
    }
    if (!Module(id)) { UnsupportedApi("sceSysmoduleLoadModule", id, __builtin_return_address(0)); }
    return Api([&] {
        std::lock_guard lock(gate);
        if (!InitOnceExecuteOnce(&ui_once, PrepareUi, nullptr, nullptr)) { return kMemory; }
        auto& session = *Module(id);
        if (session.references == 0xffffffffu) { return kMemory; }
        ++session.references;
        std::printf("{\"event\":\"native_system_module\",\"operation\":\"load\",\"id\":%u,\"name\":\"%s\",\"references\":%u}\n",
                    id, session.name, session.references);
        return 0;
    });
}

int KT_SYSV ModuleUnload(unsigned id) {
    if (id == 0x1e) { return CommunityScoreModule(CommunityScoreModuleAction::Unload); }
    if (id == 0xa5) { return AvPlayerModule(AvPlayerModuleAction::Unload); }
    if (id == 0xb4) { return ContentModule(ContentModuleAction::Unload, __builtin_return_address(0)); }
    if (id == 0xb) { return Ngs2Module(Ngs2ModuleAction::Unload); }
    if (id == 0x9c) { return ScreenshotModule(CaptureModuleAction::Unload); }
    if (id == 0xad) { return TrophyModule(TrophyModuleAction::Unload); }
    return Api([&] {
        std::lock_guard lock(gate);
        auto* session = Module(id);
        if (!id) { return kInvalidModule; }
        if (!session || !session->references) { return kModuleNotLoaded; }
        if (session->references == 1 && (session->status || session->changing)) { return kModuleBusy; }
        --session->references;
        std::printf("{\"event\":\"native_system_module\",\"operation\":\"unload\",\"id\":%u,\"name\":\"%s\",\"references\":%u}\n",
                    id, session->name, session->references);
        return 0;
    });
}

int KT_SYSV ModuleIsLoaded(unsigned id) {
    if (id == 0x1e) { return CommunityScoreModule(CommunityScoreModuleAction::IsLoaded); }
    if (id == 0xa5) { return AvPlayerModule(AvPlayerModuleAction::IsLoaded); }
    if (id == 0xb4) { return ContentModule(ContentModuleAction::IsLoaded, __builtin_return_address(0)); }
    if (id == 0xb) { return Ngs2Module(Ngs2ModuleAction::IsLoaded); }
    if (id == 0x9c) { return ScreenshotModule(CaptureModuleAction::IsLoaded); }
    if (id == 0xad) { return TrophyModule(TrophyModuleAction::IsLoaded); }
    std::lock_guard lock(gate);
    const auto* session = Module(id);
    return !id ? kInvalidModule : session && session->references ? 0 : kModuleNotLoaded;
}

int KT_SYSV CommonInitialize() {
    std::lock_guard lock(gate);
    if (common_initialized) { return kSystemAlready; }
    common_initialized = true;
    return 0;
}
int KT_SYSV CommonIsUsed() { std::lock_guard lock(gate); return message_session.status != 0 || save_session.status != 0 || profile_session.status != 0; }

int Initialize(Session& session) {
    std::lock_guard lock(gate);
    if (!session.references) { return kNotInitialized; }
    if (&session != &error_session && !common_initialized) { return kNoSystem; }
    if (session.status) { return kAlreadyInitialized; }
    if (&session != &error_session) {
        for (const auto* other : {&message_session, &save_session, &profile_session}) {
            if (other != &session && other->status) { return kBusy; }
        }
    }
    session.status = 1;
    return 0;
}

int Status(Session& session) { std::lock_guard lock(gate); return static_cast<int>(session.status); }

int Open(Session& session, Display display) {
    std::unique_lock lock(gate);
    if (session.changing) { return kBusy; }
    if (session.status != 1 && session.status != 3) { return kState; }
    session.changing = true;
    const auto previous = session.job;
    lock.unlock();
    if (previous && WaitForSingleObject(previous->thread, 5000) != WAIT_OBJECT_0) {
        lock.lock(); session.changing = false; return kFatal;
    }
    lock.lock();
    // Allocate everything that can throw before publishing the running state.
    std::shared_ptr<Job> job;
    std::unique_ptr<std::shared_ptr<Job>> argument;
    try {
        job = std::make_shared<Job>();
        job->session = &session;
        job->display = std::move(display);
        argument = std::make_unique<std::shared_ptr<Job>>(job);
    } catch (...) { session.changing = false; throw; }
    job->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!job->ready) { session.changing = false; return kMemory; }
    session.job = job;
    session.status = 2;
    job->thread = CreateThread(nullptr, 0, WindowThread, argument.get(), 0, nullptr);
    if (!job->thread) {
        session.job.reset(); session.status = 1; session.changing = false; return kMemory;
    }
    argument.release();
    lock.unlock();
    const auto ready = WaitForSingleObject(job->ready, 5000);
    lock.lock();
    session.changing = false;
    if (ready != WAIT_OBJECT_0) {
        UnsupportedApi("native_dialog_window_creation_timeout", session.id, nullptr);
    }
    if (job->failed) { session.status = 1; return kFatal; }
    std::printf("{\"event\":\"native_dialog_open\",\"module_id\":%u,\"mode\":%u,\"type\":%u}\n",
                session.id, job->display.mode, job->display.type);
    return 0;
}

int Close(Session& session) {
    std::lock_guard lock(gate);
    if (session.changing) { return kBusy; }
    if (session.status != 2 || !session.job || !session.job->window) { return kNotRunning; }
    return PostMessageW(session.job->window, kFinish, 0, 0) ? 0 : kFatal;
}

int Terminate(Session& session) {
    std::unique_lock lock(gate);
    if (session.changing) { return kBusy; }
    if (!session.status) { return kNotInitialized; }
    session.changing = true;
    const auto job = session.job;
    if (session.status == 2 && job && job->window && !PostMessageW(job->window, kFinish, 0, 0)) {
        session.changing = false; return kFatal;
    }
    lock.unlock();
    const bool joined = !job || WaitForSingleObject(job->thread, 5000) == WAIT_OBJECT_0;
    lock.lock();
    session.changing = false;
    if (!joined) { return kFatal; }
    session.job.reset();
    session.status = 0;
    return 0;
}

int KT_SYSV ProfileInitialize() { return Api([] { return Initialize(profile_session); }); }
int KT_SYSV ProfileTerminate() { return Api([] { return Terminate(profile_session); }); }
int KT_SYSV ProfileStatus() { return Status(profile_session); }
int KT_SYSV ProfileClose() { return Api([] { return Close(profile_session); }); }
int OpenProfile(std::string name, void* cookie) {
    Display display;
    display.profile = display.cancel = true;
    display.first = L"CLOSE";
    display.cookie = cookie;
    if (!Utf8(name.c_str(), display.message)) { return kParameter; }
    return Open(profile_session, std::move(display));
}
int KT_SYSV ProfileOpen(const ProfileParam* param) {
    return Api([&] {
        const auto status = ProfileStatus();
        if (status != 1 && status != 3) { return kState; }
        if (!param) { return kNull; }
        if (param->size != sizeof(*param) || param->base.size != sizeof(BaseParam)) { return kParameter; }
        if (param->mode != 1) { return kUnsupported; }
        const auto end = std::find(std::begin(param->online), std::end(param->online), '\0');
        if (end == std::begin(param->online) || end == std::end(param->online)) { return kParameter; }
        return OpenProfile(std::string(param->online, end), param->cookie);
    });
}
int KT_SYSV ProfileOpenAccount(const ProfileAccountParam* param) {
    return Api([&] {
        const auto status = ProfileStatus();
        if (status != 1 && status != 3) { return kState; }
        if (!param) { return kNull; }
        if (param->size != sizeof(*param) || param->base.size != sizeof(BaseParam) || !param->account) { return kParameter; }
        if (param->mode != 1) { return kUnsupported; }
        auto name = NativeScoreProfileName(param->account);
        if (name.empty()) { name = "Player ID: " + std::to_string(param->account); }
        return OpenProfile(std::move(name), param->cookie);
    });
}
int KT_SYSV ProfileGetResult(ProfileResult* result) {
    std::lock_guard lock(gate);
    if (!profile_session.status) { return kNotInitialized; }
    if (!result) { return kNull; }
    if (profile_session.status != 3 || !profile_session.job) { return kNotFinished; }
    *result = {0, profile_session.job->outcome.result, profile_session.job->display.cookie, {}};
    return 0;
}

int KT_SYSV MessageInitialize() { return Api([] { return Initialize(message_session); }); }
int KT_SYSV MessageTerminate() { return Api([] { return Terminate(message_session); }); }
int KT_SYSV MessageStatus() { return Status(message_session); }
int KT_SYSV MessageClose() { return Api([] { return Close(message_session); }); }
int KT_SYSV MessageOpen(const MessageParam* param) {
    return Api([&] {
        const auto status = Status(message_session);
        if (status != 1 && status != 3) { return kState; }
        Display display;
        const auto error = Describe(param, display);
        return error ? error : Open(message_session, std::move(display));
    });
}
int KT_SYSV MessageResult(Result* result) {
    std::lock_guard lock(gate);
    if (message_session.status != 3) { return kNotFinished; }
    if (!result) { return kNull; }
    *result = message_session.job->outcome;
    return 0;
}
int Progress(Session& session, unsigned target, unsigned value, const char* text, unsigned operation) {
    return Api([&] {
        std::lock_guard lock(gate);
        if (session.changing) { return kBusy; }
        if (session.status != 2 || !session.job) { return kNotRunning; }
        auto& job = *session.job;
        if (job.display.mode != 2) { return kUnsupported; }
        if (target || (operation == 0 && value > 100)) { return kParameter; }
        if (operation == 2) {
            if (!Utf8(text, job.display.message)) { return kParameter; }
        } else if (operation == 1) {
            job.display.value = std::min(100u, job.display.value + std::min(100u, value));
        } else { job.display.value = value; }
        return PostMessageW(job.window, kRefresh, 0, 0) ? 0 : kFatal;
    });
}
int KT_SYSV ProgressSetValue(unsigned target, unsigned value) { return Progress(message_session, target, value, nullptr, 0); }
int KT_SYSV ProgressIncrease(unsigned target, unsigned value) { return Progress(message_session, target, value, nullptr, 1); }
int KT_SYSV ProgressSetMessage(unsigned target, const char* text) { return Progress(message_session, target, 0, text, 2); }
int KT_SYSV SaveInitialize() { return Api([] { return Initialize(save_session); }); }
int KT_SYSV SaveTerminate() { return Api([] { return Terminate(save_session); }); }
int KT_SYSV SaveStatus() { return Status(save_session); }
int KT_SYSV SaveClose() { return Api([] { return Close(save_session); }); }
int KT_SYSV SaveReady() { std::lock_guard lock(gate); return save_session.references && !save_session.changing; }
int KT_SYSV SaveOpen(const SaveParam* param) {
    const auto caller = __builtin_return_address(0);
    return Api([&] {
        const auto status = Status(save_session);
        if (status != 1 && status != 3) { return kState; }
        Display display;
        const auto error = DescribeSave(param, display);
        if (error == kUnsupported) { UnsupportedApi("sceSaveDataDialogOpen_mode", param ? param->mode : 0, caller); }
        return error ? error : Open(save_session, std::move(display));
    });
}
int KT_SYSV SaveGetResult(SaveResult* result) {
    std::lock_guard lock(gate);
    if (save_session.status != 3) { return kNotFinished; }
    if (!result) { return kNull; }
    const auto& job = *save_session.job;
    if (result->metadata) { UnsupportedApi("sceSaveDataDialogGetResult_metadata", job.display.api_mode, __builtin_return_address(0)); }
    result->mode = job.display.api_mode;
    result->result = job.outcome.result;
    result->button = job.outcome.button;
    result->cookie = job.display.cookie;
    if (result->directory) {
        std::memset(result->directory, 0, 32);
        std::memcpy(result->directory, job.display.directory.data(), job.display.directory.size());
    }
    return 0;
}
int KT_SYSV SaveProgressSet(unsigned target, unsigned value) { return Progress(save_session, target, value, nullptr, 0); }
int KT_SYSV SaveProgressIncrease(unsigned target, unsigned value) { return Progress(save_session, target, value, nullptr, 1); }
int KT_SYSV ErrorInitialize() { return Api([] { return Initialize(error_session); }); }
int KT_SYSV ErrorTerminate() { return Api([] { return Terminate(error_session); }); }
int KT_SYSV ErrorStatus() { return Status(error_session); }
int KT_SYSV ErrorClose() { return Api([] { return Close(error_session); }); }
int KT_SYSV ErrorOpen(const ErrorParam* param) {
    return Api([&] {
        const auto status = Status(error_session);
        if (status != 1 && status != 3) { return kState; }
        if (!param) { return kNull; }
        if (param->size != sizeof(ErrorParam)) { return kParameter; }
        Display display;
        display.error = true;
        wchar_t message[128];
        std::swprintf(message, std::size(message), L"An error has occurred.\nCode: 0x%08X", static_cast<unsigned>(param->code));
        display.message = message;
        display.first = L"OK";
        return Open(error_session, std::move(display));
    });
}
} // namespace

std::uint64_t ResolveDialogImport(std::string_view identifier, std::string_view library, std::string_view module) {
    const auto hash = identifier.substr(0, identifier.find('#'));
    if (module == "libSceNpProfileDialog" && library == "libSceNpProfileDialogCompat" && hash == "uj9Cz7Tk0cc") {
        return reinterpret_cast<std::uint64_t>(&ProfileOpen);
    }
    if (library != module) { return 0; }
    struct Binding { std::string_view hash, library; std::uint64_t address; };
    const Binding bindings[]{
        {"g8cM39EUZ6o", "libSceSysmodule", reinterpret_cast<std::uint64_t>(&ModuleLoad)},
        {"eR2bZFAAU0Q", "libSceSysmodule", reinterpret_cast<std::uint64_t>(&ModuleUnload)},
        {"fMP5NHUOaMk", "libSceSysmodule", reinterpret_cast<std::uint64_t>(&ModuleIsLoaded)},
        {"uoUpLGNkygk", "libSceCommonDialog", reinterpret_cast<std::uint64_t>(&CommonInitialize)},
        {"BQ3tey0JmQM", "libSceCommonDialog", reinterpret_cast<std::uint64_t>(&CommonIsUsed)},
        {"HTrcDKlFKuM", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&MessageClose)},
        {"Lr8ovHH9l6A", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&MessageResult)},
        {"CWVW78Qc3fI", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&MessageStatus)},
        {"lDqxaY1UbEo", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&MessageInitialize)},
        {"b06Hh0DPEaE", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&MessageOpen)},
        {"Gc5k1qcK4fs", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&ProgressIncrease)},
        {"6H-71OdrpXM", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&ProgressSetMessage)},
        {"wTpfglkmv34", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&ProgressSetValue)},
        {"ePw-kqZmelo", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&MessageTerminate)},
        {"6fIC3XKt2k0", "libSceMsgDialog", reinterpret_cast<std::uint64_t>(&MessageStatus)},
        {"ekXHb1kDBl0", "libSceErrorDialog", reinterpret_cast<std::uint64_t>(&ErrorClose)},
        {"t2FvHRXzgqk", "libSceErrorDialog", reinterpret_cast<std::uint64_t>(&ErrorStatus)},
        {"I88KChlynSs", "libSceErrorDialog", reinterpret_cast<std::uint64_t>(&ErrorInitialize)},
        {"M2ZF-ClLhgY", "libSceErrorDialog", reinterpret_cast<std::uint64_t>(&ErrorOpen)},
        {"9XAxK2PMwk8", "libSceErrorDialog", reinterpret_cast<std::uint64_t>(&ErrorTerminate)},
        {"WWiGuh9XfgQ", "libSceErrorDialog", reinterpret_cast<std::uint64_t>(&ErrorStatus)},
        {"fH46Lag88XY", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveClose)},
        {"yEiJ-qqr6Cg", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveGetResult)},
        {"ERKzksauAJA", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveStatus)},
        {"s9e3+YpRnzw", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveInitialize)},
        {"en7gNVnh878", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveReady)},
        {"4tPhsP6FpDI", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveOpen)},
        {"V-uEeFKARJU", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveProgressIncrease)},
        {"hay1CfTmLyA", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveProgressSet)},
        {"YuH2FA7azqQ", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveTerminate)},
        {"KK3Bdg1RWK0", "libSceSaveDataDialog", reinterpret_cast<std::uint64_t>(&SaveStatus)},
        {"Lg+NCE6pTwQ", "libSceNpProfileDialog", reinterpret_cast<std::uint64_t>(&ProfileInitialize)},
        {"0Sp9vJcB1-w", "libSceNpProfileDialog", reinterpret_cast<std::uint64_t>(&ProfileTerminate)},
        {"3BqoiFOjSsk", "libSceNpProfileDialog", reinterpret_cast<std::uint64_t>(&ProfileStatus)},
        {"haVZE9FgKqE", "libSceNpProfileDialog", reinterpret_cast<std::uint64_t>(&ProfileStatus)},
        {"wkwjz0Xdo2A", "libSceNpProfileDialog", reinterpret_cast<std::uint64_t>(&ProfileClose)},
        {"uj9Cz7Tk0cc", "libSceNpProfileDialog", reinterpret_cast<std::uint64_t>(&ProfileOpen)},
        {"nrQRlLKzdwE", "libSceNpProfileDialog", reinterpret_cast<std::uint64_t>(&ProfileOpenAccount)},
        {"8rhLl1-0W-o", "libSceNpProfileDialog", reinterpret_cast<std::uint64_t>(&ProfileGetResult)},
    };
    for (const auto& binding : bindings) {
        if (binding.hash == hash && binding.library == library) { return binding.address; }
    }
    return 0;
}

bool DialogSelfTest() {
    bool passed = true;
    const auto check = [&](const char* name, bool result) {
        std::printf("{\"event\":\"dialog_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, result ? "true" : "false");
        passed &= result;
    };
    check("unloaded", ModuleIsLoaded(0xa4) == kModuleNotLoaded && ModuleUnload(0xa4) == kModuleNotLoaded && MessageInitialize() == kNotInitialized);
    check("invalid_module", ModuleLoad(0) == kInvalidModule && ModuleIsLoaded(0) == kInvalidModule && ModuleUnload(0) == kInvalidModule);
    check("png_module_does_not_enable_save_dialog", ModuleLoad(0x8d) == kModuleNotLoaded &&
          ModuleIsLoaded(0x8d) == kModuleNotLoaded && ModuleUnload(0x8d) == kModuleNotLoaded &&
          ModuleIsLoaded(0xa0) == kModuleNotLoaded && !SaveReady() && SaveInitialize() == kNotInitialized);
    check("optional_module_absence_is_not_a_loaded_provider", ModuleLoad(7) == kModuleNotLoaded && ModuleIsLoaded(7) == kModuleNotLoaded &&
          ModuleUnload(7) == kModuleNotLoaded && ModuleLoad(0x81) == kModuleNotLoaded && ModuleLoad(0x85) == kModuleNotLoaded &&
          ModuleLoad(0xb7) == kModuleNotLoaded && ModuleIsLoaded(0xb7) == kModuleNotLoaded && ModuleUnload(0xb7) == kModuleNotLoaded &&
          ModuleLoad(0xe7) == kModuleNotLoaded && ModuleIsLoaded(0xe7) == kModuleNotLoaded && ModuleUnload(0xe7) == kModuleNotLoaded);
    check("load_references", ModuleLoad(0xa4) == 0 && ModuleLoad(0xa4) == 0 && ModuleUnload(0xa4) == 0 && ModuleIsLoaded(0xa4) == 0);
    check("common_init", MessageInitialize() == kNoSystem && CommonInitialize() == 0 && CommonInitialize() == kSystemAlready);
    check("message_lifecycle", MessageStatus() == 0 && MessageInitialize() == 0 && MessageInitialize() == kAlreadyInitialized &&
          CommonIsUsed() && ModuleUnload(0xa4) == kModuleBusy && MessageStatus() == 1 && MessageClose() == kNotRunning && MessageResult(nullptr) == kNotFinished);
    MessageParam param{}; param.size = sizeof(param); param.base.size = sizeof(param.base);
    Display display;
    check("invalid_open", MessageOpen(nullptr) == kNull && MessageOpen(&param) == kUnsupported && MessageStatus() == 1);
    const char bad_utf8[]{static_cast<char>(0xff), 0};
    UserParam user{0, 0, bad_utf8, nullptr, {}}; param.mode = 1; param.user = &user;
    check("utf8_validation", MessageOpen(&param) == kParameter && MessageStatus() == 1);
    user.message = "Yes or no"; user.type = 7;
    check("button_mapping", Describe(&param, display) == 0 && display.default_button == 2 &&
          ButtonOutcome(display, 2).button == 2 && ButtonOutcome(display, 2).result == 0);
    user.type = 8; display = {};
    check("cancel_mapping", Describe(&param, display) == 0 && display.default_button == 2 &&
          ButtonOutcome(display, 2).button == 0 && ButtonOutcome(display, 2).result == 1);
    check("message_terminate", MessageTerminate() == 0 && MessageStatus() == 0 && !CommonIsUsed() && MessageTerminate() == kNotInitialized && ModuleUnload(0xa4) == 0);
    check("reload", ModuleLoad(0xa4) == 0 && MessageInitialize() == 0 && MessageTerminate() == 0 && ModuleUnload(0xa4) == 0);
    check("error_lifecycle", ModuleLoad(0xac) == 0 && ErrorInitialize() == 0 && ErrorStatus() == 1 && ErrorOpen(nullptr) == kNull &&
          ErrorTerminate() == 0 && ModuleUnload(0xac) == 0);
    check("save_lifecycle", ModuleLoad(0xa0) == 0 && ModuleLoad(0xa4) == 0 && SaveReady() && SaveInitialize() == 0 && SaveStatus() == 1 &&
          MessageInitialize() == kBusy && CommonIsUsed() && SaveOpen(nullptr) == kNull && SaveGetResult(nullptr) == kNotFinished &&
          SaveTerminate() == 0 && !CommonIsUsed() && ModuleUnload(0xa0) == 0 && !SaveReady() && ModuleUnload(0xa4) == 0);
    SaveParam save{}; save.base.size = sizeof(save.base); save.size = sizeof(save); save.operation = 1;
    save.mode = 1; display = {};
    check("save_list_unimplemented", DescribeSave(&save, display) == kUnsupported);
    SaveUserParam save_user{1, 0, "Confirm saved-data test", {}};
    save.mode = 2; save.user = &save_user; save.cookie = reinterpret_cast<void*>(0x1234); display = {};
    check("save_message_translation", DescribeSave(&save, display) == 0 && display.api_mode == 2 &&
          display.cookie == save.cookie && ButtonOutcome(display, 2).button == 2 && ButtonOutcome(display, 2).result == 0);
    SaveProgressParam save_progress{}; save_progress.system_message = 1;
    save.mode = 5; save.progress = &save_progress; display = {};
    check("save_progress_translation", DescribeSave(&save, display) == 0 && display.mode == 2 && display.first.empty() && display.second.empty());
    std::printf("{\"event\":\"dialog_self_test\",\"passed\":%s}\n", passed ? "true" : "false");
    return passed;
}

bool ProfileDialogSelfTest() {
    // Runs the production windows/lifecycle without showing or focusing them.
    hidden_dialog_test = true;
    bool passed = true;
    const auto check = [&](const char* name, bool ok) {
        std::printf("{\"event\":\"profile_dialog_test\",\"name\":\"%s\",\"passed\":%s}\n", name, ok ? "true" : "false");
        passed &= ok;
    };
    const auto resolve = [](const char* hash) {
        return ResolveDialogImport(hash, "libSceNpProfileDialog", "libSceNpProfileDialog");
    };
    using Plain = int (KT_SYSV*)();
    using OpenId = int (KT_SYSV*)(const ProfileAccountParam*);
    using OpenName = int (KT_SYSV*)(const ProfileParam*);
    using GetResult = int (KT_SYSV*)(ProfileResult*);
    const auto init = reinterpret_cast<Plain>(resolve("Lg+NCE6pTwQ#BD#t"));
    const auto status = reinterpret_cast<Plain>(resolve("haVZE9FgKqE"));
    const auto close = reinterpret_cast<Plain>(resolve("wkwjz0Xdo2A"));
    const auto terminate = reinterpret_cast<Plain>(resolve("0Sp9vJcB1-w"));
    const auto open = reinterpret_cast<OpenId>(resolve("nrQRlLKzdwE"));
    const auto open_name = reinterpret_cast<OpenName>(resolve("uj9Cz7Tk0cc"));
    const auto result = reinterpret_cast<GetResult>(resolve("8rhLl1-0W-o"));
    if (!init || !status || !close || !terminate || !open || !open_name || !result) { return false; }
    check("namespaces", !ResolveDialogImport("Lg+NCE6pTwQ", "wrong", "libSceNpProfileDialog") &&
          ResolveDialogImport("uj9Cz7Tk0cc", "libSceNpProfileDialogCompat", "libSceNpProfileDialog") == resolve("uj9Cz7Tk0cc") &&
          resolve("3BqoiFOjSsk") == resolve("haVZE9FgKqE"));
    check("unloaded", init() == kNotInitialized && result(nullptr) == kNotInitialized);
    check("load", ModuleLoad(0xb2) == 0 && ModuleIsLoaded(0xb2) == 0 && init() == kNoSystem);
    check("initialize", CommonInitialize() == 0 && init() == 0 && init() == kAlreadyInitialized && status() == 1 && CommonIsUsed());
    check("exclusive", ModuleLoad(0xa4) == 0 && MessageInitialize() == kBusy && ModuleUnload(0xb2) == kModuleBusy);
    ProfileAccountParam param{}; param.base.size = sizeof(BaseParam); param.size = sizeof(param);
    param.mode = 1; param.user = 0x1000; param.account = 73; param.cookie = reinterpret_cast<void*>(0x12345678);
    check("null_and_size", open(nullptr) == kNull && open_name(nullptr) == kNull);
    param.size = 0; check("invalid_size", open(&param) == kParameter); param.size = sizeof(param);
    param.mode = 2; check("no_psn_actions", open(&param) == kUnsupported && status() == 1); param.mode = 1;
    ProfileResult answer{};
    check("not_finished", result(&answer) == kNotFinished && close() == kNotRunning);
    const auto wait_finished = [&] {
        const auto end = GetTickCount64() + 3000;
        while (status() == 2 && GetTickCount64() < end) { Sleep(1); }
        return status() == 3;
    };
    for (unsigned round = 0; round != 4; ++round) {
        check("open_account", open(&param) == 0 && status() == 2 && open(&param) == kState);
        HWND window{};
        { std::lock_guard lock(gate); if (profile_session.job) { window = profile_session.job->window; } }
        if (round == 0) { check("api_close", close() == 0); }
        else if (round == 1) { PostMessageW(window, WM_COMMAND, MAKEWPARAM(1, BN_CLICKED), 0); }
        else if (round == 2) { PostMessageW(window, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); }
        else { PostMessageW(window, WM_CLOSE, 0, 0); }
        check("finished", wait_finished() && result(&answer) == 0 && answer.result == 0 &&
              answer.action == (round >= 2 ? 1u : 0u) && answer.cookie == param.cookie);
    }
    ProfileParam named{}; named.base.size = sizeof(BaseParam); named.size = sizeof(named); named.mode = 1;
    std::memset(named.online, 'x', sizeof(named.online));
    check("bounded_name", open_name(&named) == kParameter);
    std::memcpy(named.online, "Player\0", 7);
    check("open_name", open_name(&named) == 0 && status() == 2);
    check("terminate_running", terminate() == 0 && status() == 0 && !CommonIsUsed());
    check("reload", ModuleUnload(0xb2) == 0 && ModuleLoad(0xb2) == 0 && init() == 0 && terminate() == 0 && ModuleUnload(0xb2) == 0);
    check("message_unchanged", MessageInitialize() == 0 && MessageTerminate() == 0 && ModuleUnload(0xa4) == 0);
    hidden_dialog_test = false;
    return passed;
}

int DialogDemo(std::wstring_view kind) {
    const bool error = kind == L"error";
    const unsigned id = error ? 0xac : 0xa4;
    if (ModuleLoad(id)) { return 1; }
    if (!error && CommonInitialize()) { return 1; }
    if ((error ? ErrorInitialize() : MessageInitialize())) { return 1; }
    int opened;
    if (error) {
        ErrorParam param{sizeof(ErrorParam), static_cast<int>(0x80020002u), -1, 0};
        opened = ErrorOpen(&param);
    } else {
        MessageParam param{}; param.base.size = sizeof(param.base); param.size = sizeof(param);
        UserParam user{7, 0, "Native Windows dialog test.\n\nSelect No to verify button 2.\nJapanese text: \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", nullptr, {}};
        ProgressParam progress{1, 0, "Native progress dialog - 75%", {}};
        param.mode = kind == L"progress" ? 2u : 1u; param.user = &user; param.progress = &progress;
        opened = MessageOpen(&param);
        if (!opened && param.mode == 2) { opened = ProgressSetValue(0, 75); }
    }
    if (opened) { std::fprintf(stderr, "dialog_demo_open_error=0x%x\n", static_cast<unsigned>(opened)); return 1; }
    const auto start = GetTickCount64();
    while ((error ? ErrorStatus() : MessageStatus()) == 2 && GetTickCount64() - start < 180000) { Sleep(20); }
    Result result{};
    const bool finished = (error ? ErrorStatus() : MessageStatus()) == 3;
    if (!error && finished && MessageResult(&result)) { return 1; }
    if ((error ? ErrorTerminate() : MessageTerminate()) || ModuleUnload(id)) { return 1; }
    std::printf("{\"event\":\"dialog_demo\",\"finished_by_user\":%s,\"result\":%u,\"button\":%u}\n",
                finished ? "true" : "false", result.result, result.button);
    return finished ? 0 : 1;
}
