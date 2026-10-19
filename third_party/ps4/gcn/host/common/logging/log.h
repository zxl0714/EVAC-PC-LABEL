#pragma once
#include <cstdio>
#include <fmt/format.h>

namespace KetsuiGcnHost {
template<typename... Args> void Log(const char* level, fmt::format_string<Args...> format, Args&&... args) {
    const auto message = fmt::format(format, std::forward<Args>(args)...);
    std::fprintf(stderr, "[gcn %s] %s\n", level, message.c_str());
}
}
#define LOG_TRACE(category, ...) ((void)0)
#define LOG_DEBUG(category, ...) ((void)0)
#define LOG_INFO(category, ...) KetsuiGcnHost::Log("info", __VA_ARGS__)
#define LOG_WARNING(category, ...) KetsuiGcnHost::Log("warning", __VA_ARGS__)
#define LOG_ERROR(category, ...) KetsuiGcnHost::Log("error", __VA_ARGS__)
#define LOG_CRITICAL(category, ...) KetsuiGcnHost::Log("critical", __VA_ARGS__)
