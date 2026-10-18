#include "community/http_client.h"
#include <rex/logging.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace cave::community;
using namespace std::chrono_literals;
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}

void UnitTests() {
  Require(ValidEndpoint("https://example.invalid/cave/api/v1"),"HTTPS endpoint refused");
  Require(ValidEndpoint("http://127.0.0.1:12345/test") && ValidEndpoint("http://localhost/test"),"Loopback HTTP refused");
  for(const auto* endpoint:{"http://example.invalid/api","ftp://localhost/api","https://user:secret@example.invalid/api",
      "https://example.invalid/api?game=g2","https://example.invalid/#part","https://bad host/api","/api",""})
    Require(!ValidEndpoint(endpoint),"Invalid endpoint accepted");
  Require(NormalizeNickname("  Player  ")=="Player","Nickname whitespace");
  Require(NormalizeNickname("Ｐｌａｙｅｒ")=="Player","Nickname normalization");
  Require(ValidNickname("Player") && ValidNickname("大佐"),"Valid nickname refused");
  Require(!ValidNickname("") && !ValidNickname(" Player") && !ValidNickname("a\nb") &&
      !ValidNickname(std::string(49,'x')) && !ValidNickname("a\u200db"),"Invalid nickname accepted");
  HttpClient client("https://example.invalid/cave/api/v1/");
  // Every path is rejected before DNS/HTTP. This unit test never connects.
  for(const auto* path:{"", "relative", "//other-host/path", "/path#fragment", "/path\nHeader", "/path\\segment"}) {
    auto pending=client.Request(path);
    Require(pending.wait_for(2s)==std::future_status::ready,"Invalid request did not complete");
    Require(pending.get().error==87,"Invalid resource path was not rejected");
  }
  client.Stop();client.Stop();
  Require(client.Request("/health").get().error==1223,"Stopped client accepted a request");
}

nlohmann::json Probe(std::string endpoint) {
  Require(endpoint.starts_with("http://127.0.0.1:"),"Probe is restricted to the local synthetic test server");
  HttpClient client(std::move(endpoint));
  auto health=client.Request("/health").get();Require(bool(health),"Health request failed");
  auto echo=client.Request("/echo",{{"nickname","TransportTest"},{"score","9007199254740993"}}).get();
  Require(bool(echo),"POST failed");
  auto redirect=client.Request("/redirect").get();
  auto invalid=client.Request("/invalid-json").get();
  auto large=client.Request("/large").get();
  std::vector<std::future<HttpResult>> pending;
  auto began=std::chrono::steady_clock::now();
  for(int i=0;i<40;++i)pending.push_back(client.Request("/slow"));
  auto enqueue_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
  std::this_thread::sleep_for(100ms);
  began=std::chrono::steady_clock::now();client.Stop();
  auto stop_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
  unsigned busy=0,cancelled=0;
  for(auto& future:pending) {
    Require(future.wait_for(1s)==std::future_status::ready,"Shutdown left an unresolved request");
    auto result=future.get();busy+=result.error==170;cancelled+=result.error==1223 || result.error==995;
  }
  return {{"health",health.body},{"echo",echo.body},{"redirectStatus",redirect.status},
      {"invalidJsonError",invalid.error},{"largeResponseError",large.error},
      {"busy",busy},{"cancelled",cancelled},{"enqueueMs",enqueue_ms},{"stopMs",stop_ms}};
}

int main(int argc,char** argv) {
  try {
    rex::InitLoggingEarly();auto logging=rex::BuildLogConfig(nullptr,"error",{});rex::InitLogging(logging);
    if(argc==1) {UnitTests();std::cout<<"Community HTTP unit tests passed; no connections made\n";}
    else if(argc==3 && std::string_view(argv[1])=="--probe")std::cout<<Probe(argv[2]).dump()<<'\n';
    else throw std::runtime_error("Usage: community_http_tests [--probe <loopback URL>]");
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
