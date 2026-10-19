#pragma once
#include <stdexcept>
#include "common/logging/log.h"
#define SHAD_NO_INLINE __declspec(noinline)
#define ASSERT(condition) do { if (!(condition)) { throw std::runtime_error("GCN assertion: " #condition); } } while (false)
#define ASSERT_MSG(condition, ...) do { if (!(condition)) { throw std::runtime_error(fmt::format("GCN assertion: " __VA_ARGS__)); } } while (false)
#define UNREACHABLE() throw std::runtime_error("GCN unreachable path")
#define UNREACHABLE_MSG(...) throw std::runtime_error(fmt::format(__VA_ARGS__))
#define DEBUG_ASSERT(condition) ASSERT(condition)
#define DEBUG_ASSERT_MSG(condition, ...) ASSERT_MSG(condition, __VA_ARGS__)
#define UNIMPLEMENTED() UNREACHABLE()
#define UNIMPLEMENTED_MSG(...) UNREACHABLE_MSG(__VA_ARGS__)
#define UNIMPLEMENTED_IF(condition) ASSERT(!(condition))
#define UNIMPLEMENTED_IF_MSG(condition, ...) ASSERT_MSG(!(condition), __VA_ARGS__)
