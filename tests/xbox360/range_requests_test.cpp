#include <rex/graphics/range_requests.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string_view>

using Requests = rex::graphics::RangeRequests;
using Range = Requests::Range;
constexpr uint32_t kLimit = 1u << 29;
static unsigned checks;
static void Check(bool condition) {
  ++checks;
  if (!condition) { std::fprintf(stderr, "Failed check %u\n", checks); std::exit(1); }
}

// Previous production normalization, kept as the comparison oracle.
static bool Original(const Range* input, size_t count, std::vector<Range>& result) {
  if (!input || !count) return true;
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    auto [start, length] = input[i];
    if (!length) continue;
    if (start > kLimit || length > kLimit - start) return false;
    result.emplace_back(start, length);
  }
  std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.first < b.first; });
  if (result.empty()) return true;
  size_t write = 0;
  for (size_t i = 1; i < result.size(); ++i) {
    auto& previous = result[write];
    const auto current = result[i];
    const uint64_t previous_end = uint64_t(previous.first) + previous.second;
    if (current.first <= previous_end) {
      const uint64_t current_end = uint64_t(current.first) + current.second;
      if (current_end > previous_end) previous.second = uint32_t(current_end - previous.first);
    } else result[++write] = current;
  }
  result.resize(write + 1);
  return true;
}

static void Compare(const Range* input, size_t count) {
  Requests request;
  std::vector<Range> original;
  const bool expected = Original(input, count, original);
  Check(request.Prepare(input, count, kLimit) == expected);
  if (expected) {
    const auto output = request.view();
    Check(std::equal(original.begin(), original.end(), output.begin(), output.end()));
    if (input && count == 1 && input->second) Check(output.data() == input);
  } else Check(request.view().empty());
}

static volatile uint64_t sink;
static void Bench(bool optimized, size_t count) {
  Range inputs[] = {{4096, 512}, {4096 + 512, 1024}, {128, 32}, {65536, 4096}};
  uint64_t checksum = 0;
  constexpr uint32_t iterations = 500000;
  const auto begin = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < iterations; ++i) {
    inputs[0].first = 4096 + (i & 127);
    if (optimized) {
      Requests request;
      if (!request.Prepare(inputs, count, kLimit)) std::abort();
      for (const auto& range : request.view()) checksum += range.first + range.second;
    } else {
      std::vector<Range> original;
      if (!Original(inputs, count, original)) std::abort();
      for (const auto& range : original) checksum += range.first + range.second;
    }
  }
  const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
  sink = checksum;
  std::printf("RANGE_BENCH strategy=%s ranges=%zu ns_per_call=%.3f checksum=%llu\n",
              optimized ? "borrow_single" : "original", count, ns / iterations,
              static_cast<unsigned long long>(checksum));
}

int main(int argc, char** argv) {
  Compare(nullptr, 3);
  const Range boundary[] = {{0,0}, {kLimit,0}, {UINT32_MAX,0}, {0,kLimit}, {kLimit-1,1},
                           {kLimit,1}, {UINT32_MAX,1}, {0,UINT32_MAX}};
  for (const auto& range : boundary) Compare(&range, 1);
  const Range merged[] = {{100,100}, {200,30}, {50,200}, {1000,2}, {0,0}, {1001,1}};
  Compare(merged, std::size(merged));
  Requests reused;
  Check(reused.Prepare(merged, std::size(merged), kLimit));
  Check(reused.Prepare(&boundary[4], 1, kLimit));
  Check(reused.view().data() == &boundary[4]);
  Check(reused.Prepare(nullptr, 0, kLimit));
  Check(reused.view().empty());
  Check(!reused.Prepare(&boundary[5], 1, kLimit));
  Check(reused.view().empty());
  std::mt19937 random(0x42A1);
  for (unsigned i=0; i<6000; ++i) {
    std::vector<Range> ranges(i % 35);
    for (auto& range : ranges) range = {random() % kLimit, random() % 262144};
    if (!ranges.empty() && i % 5 == 0) ranges[0] = boundary[i % std::size(boundary)];
    const auto original = ranges;
    Compare(ranges.data(), ranges.size());
    Check(ranges == original);
  }
  std::printf("Range request checks passed: %u\n", checks);
  if (argc > 1 && std::string_view(argv[1]) == "--bench") {
    for (size_t count : {size_t(1), size_t(4)}) {
      Bench(false, count); Bench(true, count); Bench(true, count); Bench(false, count);
    }
  }
}
