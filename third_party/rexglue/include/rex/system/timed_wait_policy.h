#pragma once
namespace rex::system {
// Thread-local, opt-in compatibility scope. Returns previous policy.
// Only positive timed waits that really timed out may skip the extra yield.
// Zero-time polls, success/APC/failure, and other threads keep prior semantics.
bool ExchangeTimedWaitYieldSuppression(bool suppress);
}
