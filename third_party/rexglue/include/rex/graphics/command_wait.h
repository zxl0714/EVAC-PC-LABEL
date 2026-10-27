// Copyright 2026 EVAC PC LABEL contributors. BSD-3-Clause.
#pragma once
#include <chrono>

namespace rex::graphics {
// Readiness includes shutdown, queued callbacks and a changed ring write index.
// Every producer publishes its predicate before signaling the retained event.
template <typename Ready, typename Wait, typename Yield>
void WaitForCommandWork(bool blocking, Ready ready, Wait wait, Yield yield) {
  unsigned loops = 0;
  while (!ready()) {
    if (blocking || loops > 500) {
      wait(blocking ? std::chrono::milliseconds::max() : std::chrono::milliseconds(5));
    }
    if (!blocking) {
      yield();
      ++loops;
    }
  }
}
}  // namespace rex::graphics
