#include "client.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace kt::community {
bool OutboxSelfTest(const std::filesystem::path& directory);
namespace {
void Require(bool value, const char* error) { if (!value) { throw std::runtime_error(error); } }
Reply Await(std::future<Reply> future) {
    Require(future.wait_for(std::chrono::seconds(10)) == std::future_status::ready, "Community fixture request timed out");
    return future.get();
}
std::vector<std::byte> Bytes(std::string_view text) {
    const auto view = std::as_bytes(std::span(text.data(), text.size()));
    return {view.begin(), view.end()};
}
}
bool ClientSelfTest(const std::filesystem::path& directory) {
    Require(directory.is_absolute() && !std::filesystem::exists(directory), "Community self-test requires a new absolute directory");
    bool okay = true;
    const auto check = [&](bool value, const char* name) {
        std::printf("{\"event\":\"community_client_self_test_case\",\"name\":\"%s\",\"passed\":%s}\n", name, value ? "true" : "false");
        okay &= value;
    };
    const auto rejects = [](auto&& operation) { try { operation(); return false; } catch (...) { return true; } };
    const auto disabled = ParseConfig("[community]\nenabled = false\nnickname =\n");
    check(!disabled.enabled && disabled.nickname.empty(), "explicit_disabled_config_has_no_implicit_name");
    const auto defaults = ParseConfig("[community]\nenabled=true\n");
    check(defaults.enabled && defaults.nickname == kDefaultNickname && defaults.endpoint == kDefaultEndpoint,
          "enabled_config_uses_shared_colonel_nickname_and_deployment_endpoint_defaults");
    const auto configured = ParseConfig("\xef\xbb\xbf[community]\r\nenabled=true\r\nnickname=\xef\xbc\xa1lice\r\nendpoint=http://127.0.0.1:1234/ketsui/api/v1/\r\n");
    check(configured.enabled && configured.nickname == "\xef\xbc\xa1lice" && ParseEndpoint(configured.endpoint).port == 1234,
          "utf8_bom_crlf_and_explicit_loopback_config");
    for (const auto text : {"[other]\nenabled=true\n", "[community]\nenabled=1\n", "[community]\nenabled=false\nenabled=true\n",
                            "[community]\nnickname=Authored\n", "[community]\nenabled=true\nnickname=\n", "[community]\nenabled=false\nsecret=x\n"}) {
        check(rejects([&] { (void)ParseConfig(text); }), "invalid_config_is_rejected");
    }
    for (const auto endpoint : {"http://example.invalid/ketsui/api/v1", "http://localhost/ketsui/api/v1", "https://user:password@example.invalid/ketsui/api/v1",
                               "https://example.invalid/futari/api/v1", "https://example.invalid/ketsui/api/v1?secret=x", "https://example.invalid/ketsui/api/v1#x"}) {
        check(rejects([&] { (void)ParseEndpoint(endpoint); }), "unsafe_or_other_title_endpoint_rejected");
    }
    check(ParseEndpoint("https://example.invalid/ketsui/api/v1").secure, "https_endpoint_keeps_default_verification");
    check(ProtocolFor("ketsui").title_id=="CUSA09062" && ProtocolFor("daioujou").title_id=="CUSA32997" &&
          ProtocolFor("esprade").title_id=="CUSA15155" && ProtocolFor("esprade").version=="1.04" &&
          ProtocolFor("daioujou").version=="1.04" && ProtocolFor("daioujou").replay_format=="daioujou-npscore-1.04",
          "ps4_transport_identity_and_replay_namespace_are_title_scoped");
    const auto feveron_defaults=ParseConfig("[community]\nenabled=true\n","feveron");
    check(ProtocolFor("feveron").title_id=="CUSA06735" && ProtocolFor("feveron").version=="1.00" &&
          ProtocolFor("feveron").replay_format=="feveron-npscore-1.00" &&
          feveron_defaults.game=="feveron" && feveron_defaults.nickname=="Player" &&
          feveron_defaults.endpoint=="https://xiaolu.life/cave/api/v1/games/feveron" &&
          rejects([&] { (void)ParseEndpoint("https://example.invalid/cave/api/v1/games/esprade","feveron"); }) &&
          AccountId("Alice","feveron")!=AccountId("Alice","esprade"),
          "feveron_application_version_route_and_account_are_title_scoped");
    const auto other_defaults=ParseConfig("[community]\nenabled=true\n","daioujou");
    check(other_defaults.game=="daioujou" && other_defaults.nickname=="Player" &&
          other_defaults.endpoint=="https://xiaolu.life/cave/api/v1/games/daioujou" &&
          rejects([&] { (void)ParseConfig("[community]\nenabled=false\nendpoint=https://example.invalid/ketsui/api/v1\n","daioujou"); }),
          "standalone_title_selection_sets_defaults_and_rejects_peer_config");
    check(ParseEndpoint("https://example.invalid/cave/api/v1/games/daioujou","daioujou").secure &&
          rejects([&] { (void)ParseEndpoint("https://example.invalid/cave/api/v1/games/ketsui","daioujou"); }) &&
          rejects([&] { (void)ParseEndpoint("https://example.invalid/cave/api/v1/games/ketsui","esprade"); }) &&
          rejects([&] { (void)ProtocolFor("unlisted-title"); }),"ps4_transport_rejects_peer_routes_and_unknown_titles");
    check(ParseJson("{\"a\":1,\"nested\":[{\"a\":2},{\"a\":3}]}").at("a") == 1, "json_sibling_object_keys_have_separate_scopes");
    check(rejects([&] { (void)ParseJson("{\"a\":1,\"a\":2}"); }) &&
          rejects([&] { (void)ParseJson("{\"a\":NaN}"); }) &&
          rejects([&] { (void)ParseJson(std::string(20, '[') + "0" + std::string(20, ']')); }) &&
          rejects([&] { (void)ParseJson("{}", 1); }), "json_duplicate_depth_syntax_and_extent_guards");
    check(Base64(Bytes("abc")) == "YWJj" && Base64(Bytes("a")) == "YQ==" && Base64({}).empty() &&
          Unbase64("YWJj", 3) == Bytes("abc") && Unbase64("", 0).empty(), "canonical_base64_roundtrip");
    check(rejects([&] { (void)Unbase64("AB==", 1); }) && rejects([&] { (void)Unbase64("YWJj\n", 4); }) &&
          rejects([&] { (void)Unbase64("YWJj", 2); }), "base64_noncanonical_padding_whitespace_and_size_rejected");
    check(Sha256(std::string_view{}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" &&
          Sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256_known_vectors");
    check(Identifier(NewId()) && !Identifier("../not-a-reference") && AccountId("Alice") > 0 && AccountId("Alice") < (1ull << 63),
          "random_references_and_stable_positive_native_account_range");
    check(AccountId("Alice")==392319542754098719ull && AccountId("Alice","daioujou")==4491676318865695736ull &&
          AccountId("Alice","ketsui")==AccountId("Alice") &&
          rejects([&] { (void)AccountId("Alice","unknown"); }),"native_account_mapping_isolated_without_changing_ketsui");
    check(Decimal("9007199254740993") == 9007199254740993ull && Decimal("9223372036854775807") == 0x7fffffffffffffffull &&
          rejects([&] { (void)Decimal("9223372036854775808"); }) && rejects([&] { (void)Decimal("01"); }), "exact_decimal_not_json_double");
    check(ShortOnlineId("abcdefghijklmnopq") == "abcdefghijklmnop" && ShortOnlineId("abcdefghijklmn\xe5\xb0\x8f") == "abcdefghijklmn",
          "native_short_name_does_not_split_utf8_character");
    {
        Config daioujou{false,"Player","https://example.invalid/cave/api/v1/games/daioujou","daioujou"};
        Client other(daioujou,directory/"daioujou-disabled");
        check(other.Game()=="daioujou" && !other.State().ready &&
              Await(other.Request("/health")).error==Error::Disabled &&
              !std::filesystem::exists(directory/"daioujou-disabled"),"disabled_second_title_starts_no_transport_or_storage");
        check(rejects([&] { Client invalid(daioujou,directory/"wrong-rounds",true); }),
              "ketsui_whole_round_recovery_is_not_reused_for_daioujou");
        Client client(disabled, directory / "must-not-exist");
        check(!client.State().ready && Await(client.Request("/health")).error == Error::Disabled &&
              !std::filesystem::exists(directory / "must-not-exist"), "disabled_client_has_no_worker_storage_or_transport");
        check(Await(client.Request("/../futari/api/v1")).error == Error::Protocol &&
              Await(client.Request("//example.invalid")).error == Error::Protocol, "request_paths_cannot_escape_ketsui_scope");
        check(Await(client.Request("/entries/00000000000000000000000000000000/replay")).error == Error::Disabled,
              "well_formed_entry_download_path_is_not_rejected_as_malformed");
        client.Stop(); client.Stop();
        check(!client.State().ready, "stop_is_idempotent");
    }
    okay &= OutboxSelfTest(directory);
    std::printf("{\"event\":\"community_client_self_test\",\"passed\":%s}\n", okay ? "true" : "false");
    return okay;
}
bool ClientSmoke(const std::filesystem::path& config_path, const std::filesystem::path& directory,std::string_view game) {
    const auto config = LoadConfig(config_path,game);
    const auto endpoint = ParseEndpoint(config.endpoint,game);
    Require(config.enabled && endpoint.host == L"127.0.0.1" && !endpoint.secure, "Authored client smoke is restricted to literal HTTP loopback");
    Require(directory.is_absolute() && !std::filesystem::exists(directory), "Authored client smoke requires a new absolute directory");
    Client client(config, directory);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!client.State().ready && std::chrono::steady_clock::now() < deadline) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    const auto identity = client.State();
    Require(identity.ready && identity.account == AccountId(identity.nickname,game), "Authored community identity not ready");
    const auto board=game=="daioujou" ? 6u : 0u;
    const auto query="/leaderboards?board="+std::to_string(board)+"&rules=standard";
    Json request{{"submission_id", NewId()}, {"nickname", identity.nickname}, {"board", board}, {"rules", "standard"},
                 {"score", game=="ketsui"?"9007199254740993":"123456789"}, {"comment_base64", Base64(std::vector<std::byte>(64))}, {"game_info_base64", ""}, {"build", "authored-cpp-client"}};
    const auto score = Await(client.Request("/scores", request, true));
    Require(bool(score) && score.body.at("accepted") == true, "Authored community score failed");
    const auto entry = score.body.at("entry_id").get<std::string>();
    const auto page = Await(client.Request(query+"&start=1&count=20"));
    Require(bool(page) && page.body.at("entries").size() == 1 && page.body.at("entries").at(0).at("score") == request.at("score"), "Authored community ranking failed");
    const auto own = Await(client.Request(query+"&account_id=" + std::to_string(identity.account)));
    Require(bool(own) && own.body.at("entries").size() == 1, "Authored personal ranking failed");
    if (game=="ketsui") {
    const auto payload = Bytes("AUTHORED-OPAQUE-ATTACHMENT-NOT-A-GAME-REPLAY");
    const auto upload = Await(client.Upload(Json{{"entry_id", entry}, {"submission_id", request.at("submission_id")},
        {"nickname", identity.nickname}, {"format", kReplayFormat}, {"sha256", Sha256(payload)}}, payload));
    Require(bool(upload), "Authored community upload failed");
    const auto download = Await(client.Request("/entries/" + entry + "/replay"));
    Require(bool(download) && download.bytes == payload, "Authored community download failed");
    const auto missing = Await(client.Request("/entries/00000000000000000000000000000000/replay"));
    Require(!missing && missing.status == 404 && client.State().ready, "Missing replay must not sign out a working service session");
    }
    auto lower_request = request;
    lower_request["submission_id"] = NewId(); lower_request["score"] = "42";
    const auto lower = Await(client.Request("/scores", lower_request, true));
    Require(bool(lower) && lower.body.at("accepted") == false && lower.body.at("upload_eligible") == false, "Authored lower-score response failed");
    client.Stop();
    std::puts(Json{{"event","community_client_smoke"},{"game",game},{"passed",true},{"native_game_integration",false},
        {"authored_data_only",true},{"opaque_attachment_tested",game=="ketsui"},{"native_replay_tested",false}}.dump().c_str());
    return true;
}
bool ClientDrainSmoke(const std::filesystem::path& config_path, const std::filesystem::path& directory) {
    const auto config = LoadConfig(config_path);
    const auto endpoint = ParseEndpoint(config.endpoint);
    Require(config.enabled && endpoint.host == L"127.0.0.1" && !endpoint.secure && directory.is_absolute() &&
            std::filesystem::is_directory(directory / "community-outbox"), "Drain fixture requires an existing isolated loopback outbox");
    Client client(config, directory);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while ((!client.State().ready || client.PendingCount()) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(client.State().ready && client.PendingCount() == 0, "Pending fixture did not drain");
    const auto page = Await(client.Request("/leaderboards?board=0&rules=standard&start=1&count=20"));
    Require(bool(page) && page.body.at("entries").size() == 1 &&
            page.body.at("entries").at(0).at("score") == "9007199254740993", "Drained fixture score missing");
    client.Stop();
    std::puts("{\"event\":\"community_client_drain_smoke\",\"passed\":true,\"authored_data_only\":true}");
    return true;
}
bool ClientReplayEnvelopeSmoke(const std::filesystem::path& config_path, const std::filesystem::path& directory,
                               const std::filesystem::path& payload_path) {
    const auto config = LoadConfig(config_path);
    const auto endpoint = ParseEndpoint(config.endpoint);
    Require(config.enabled && endpoint.host == L"127.0.0.1" && !endpoint.secure && directory.is_absolute() &&
            !std::filesystem::exists(directory), "Replay envelope smoke requires a new isolated loopback directory");
    std::ifstream input(payload_path, std::ios::binary);
    Require(bool(input), "Missing authored replay envelope");
    std::vector<std::byte> payload(kReplayLimit + 1);
    input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    Require(!input.bad(), "Cannot read authored replay envelope");
    payload.resize(static_cast<std::size_t>(input.gcount()));
    Require(!payload.empty() && payload.size() <= kReplayLimit, "Authored replay envelope exceeds limit");
    Client client(config, directory);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!client.State().ready && std::chrono::steady_clock::now() < deadline) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    const auto identity = client.State();
    Require(identity.ready, "Authored replay client not ready");
    const auto submission = NewId();
    const auto score = Await(client.Request("/scores", Json{{"submission_id", submission}, {"nickname", identity.nickname},
        {"board", 0}, {"rules", "standard"}, {"score", "9001"}, {"comment_base64", Base64(std::vector<std::byte>(64))},
        {"game_info_base64", ""}, {"build", "authored-replay-envelope"}}, true));
    Require(bool(score) && score.body.at("accepted") == true, "Authored replay score failed");
    const auto entry = score.body.at("entry_id").get<std::string>();
    const Json metadata{{"entry_id", entry}, {"submission_id", submission}, {"nickname", identity.nickname}};
    // Deliberately invalid bytes must not be staged or sent, even with a valid
    // score receipt. The same receipt remains available for the valid payload.
    Require(Await(client.UploadReplayEnvelope(metadata, Bytes("AUTHORED-BROKEN-ZLIB"))).error == Error::Protocol &&
            client.PendingCount() == 0, "Invalid replay was staged or sent");
    auto invalid_metadata = metadata; invalid_metadata["submission_id"] = "invalid";
    Require(Await(client.UploadReplayEnvelope(invalid_metadata, payload)).error == Error::Protocol &&
            client.PendingCount() == 0, "Invalid receipt metadata was staged or sent");
    const auto uploaded = Await(client.UploadReplayEnvelope(metadata, payload));
    Require(bool(uploaded) && uploaded.body.at("sha256") == Sha256(payload), "Authored typed replay upload failed");
    const auto newer_submission = NewId();
    const auto newer = Await(client.Request("/scores", Json{{"submission_id", newer_submission}, {"nickname", identity.nickname},
        {"board", 0}, {"rules", "standard"}, {"score", "9002"}, {"comment_base64", Base64(std::vector<std::byte>(64))},
        {"game_info_base64", ""}, {"build", "authored-replay-envelope"}}, true));
    Require(bool(newer) && newer.body.at("accepted") == true, "Authored newer score failed");
    const auto newer_entry = newer.body.at("entry_id").get<std::string>();
    const auto opaque = Bytes("AUTHORED-OPAQUE-NOT-A-REPLAY");
    Require(bool(Await(client.Upload(Json{{"entry_id", newer_entry}, {"submission_id", newer_submission},
        {"nickname", identity.nickname}, {"format", kReplayFormat}, {"sha256", Sha256(opaque)}}, opaque))), "Authored opaque transport fixture failed");
    const auto invalid_envelope = Await(client.DownloadReplayEnvelope(newer_entry, opaque.size(), Sha256(opaque)));
    Require(invalid_envelope.error == Error::Protocol && invalid_envelope.bytes.empty(),
            "Digest-correct opaque bytes were accepted as a replay envelope");
    // The earlier immutable entry remains selected even after the same account
    // submits a new best score and attaches different bytes.
    const auto downloaded = Await(client.DownloadReplayEnvelope(entry, payload.size(), Sha256(payload)));
    Require(bool(downloaded) && downloaded.bytes == payload, "Authored pinned replay download failed");
    const auto wrong_size = Await(client.DownloadReplayEnvelope(entry, payload.size() + 1, Sha256(payload)));
    const auto wrong_digest = Await(client.DownloadReplayEnvelope(entry, payload.size(), std::string(64, '0')));
    Require(wrong_size.error == Error::Protocol && wrong_size.bytes.empty() && wrong_digest.error == Error::Protocol &&
            wrong_digest.bytes.empty() && client.State().ready, "Pinned replay expectation mismatch was accepted or signed the client out");
    Require(Await(client.DownloadReplayEnvelope("../wrong", payload.size(), Sha256(payload))).error == Error::Protocol &&
            Await(client.DownloadReplayEnvelope(entry, 0, Sha256(payload))).error == Error::Protocol &&
            Await(client.DownloadReplayEnvelope(entry, payload.size(), "wrong")).error == Error::Protocol,
            "Invalid replay identity or expectation reached transport");
    client.Stop();
    std::printf("{\"event\":\"community_replay_envelope_smoke\",\"passed\":true,\"caller_thread_id\":%lu,\"authored_data_only\":true,\"native_playback_validated\":false}\n", static_cast<unsigned long>(GetCurrentThreadId()));
    return true;
}
bool ClientCancelSmoke(const std::filesystem::path& config_path, const std::filesystem::path& directory) {
    const auto config = LoadConfig(config_path);
    const auto endpoint = ParseEndpoint(config.endpoint);
    Require(config.enabled && endpoint.host == L"127.0.0.1" && !endpoint.secure && directory.is_absolute() &&
            !std::filesystem::exists(directory), "Cancel fixture requires a new isolated loopback directory");
    Client client(config, directory);
    std::vector<std::future<Reply>> futures;
    for (unsigned i = 0; i < 18; ++i) { futures.push_back(client.Request("/boards")); }
    std::puts("{\"event\":\"community_client_cancel_ready_for_parent\"}");
    std::fflush(stdout);
    // Authored test parent releases us only after its server has admitted the
    // active health request. This is not a timing guess or a game input hook.
    Require(std::getchar() == '!', "Cancel fixture parent did not authorize the checkpoint");
    const auto began = std::chrono::steady_clock::now();
    client.Stop();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
    unsigned cancelled{}, full{};
    for (auto& future : futures) {
        const auto reply = Await(std::move(future));
        cancelled += reply.error == Error::Cancelled;
        full += reply.error == Error::QueueFull;
    }
    Require(cancelled == 16 && full == 2 && !client.State().ready && duration < 4500, "Active request cancellation or queue bound failed");
    std::printf("{\"event\":\"community_client_cancel_smoke\",\"passed\":true,\"stop_ms\":%lld,\"cancelled\":%u,\"queue_full\":%u}\n", static_cast<long long>(duration), cancelled, full);
    return true;
}
}
