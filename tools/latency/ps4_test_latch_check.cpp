#include "ps4_test_latch.h"
#include <cassert>
#include <cstdio>

int main() {
  evac_latency::Ps4TestLatch pad;
  pad.Update(1, 0);
  assert(!pad.Buttons() && !pad.pending && pad.Source() == 1);
  pad.Update(2, 0x1000);
  assert(pad.pending == 0x1000 && pad.Buttons() == 0x1000 && pad.Source() == 2);
  pad.Update(2, 0x1000);
  assert(pad.pending == 0x1000);
  pad.Acknowledge();
  assert(!pad.pending && pad.Buttons() == 0x1000 && pad.Source() == 2);
  pad.Update(3, 0);
  assert(!pad.Buttons() && !pad.pending && pad.Source() == 3);
  pad.Update(4, 0x80);
  pad.Update(5, 0);
  assert(pad.Buttons() == 0x80 && pad.Source() == 4);
  pad.Acknowledge();
  assert(!pad.Buttons() && pad.Source() == 5);
  pad.Update(6, 0x1008);
  assert(pad.Buttons() == 0x1008 && pad.pending == 0x1008 && pad.Source() == 6);
  pad.Update(7, 0);
  pad.Update(8, 0x20);
  assert(pad.Buttons() == 0x1028 && !pad.Source());
  pad.Acknowledge();
  assert(pad.Buttons() == 0x20 && pad.Source() == 8);
  std::puts("PS4 synthetic pending latch: 10 checks passed");
}
