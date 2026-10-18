#include <rex/audio/diagnostic_stats.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
int main() {
  using namespace rex::audio::diag;
  std::atomic<uint64_t> maximum{0};
  std::vector<std::thread> workers;
  for (uint64_t i=0; i<4; ++i) workers.emplace_back([&,i]{for(uint64_t j=0;j<10000;++j)Max(maximum,i*10000+j);});
  for(auto& worker:workers)worker.join();
  assert(maximum==39999);
  const auto before=NowUs();
  { XmaWorkTimer timer(7); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
  assert(NowUs()>=before);
  assert(counters.xma_active==0);
  assert(counters.xma_work==(Enabled()?1:0));
  if(Enabled()){assert(counters.xma_busy_us>=1000);assert(counters.xma_context==7);}
  std::cout << "AUDIO_COUNTER_TEST_OK enabled=" << Enabled() << '\n';
}
