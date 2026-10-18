// src/common/log.h — journal minimal, vers stderr, horodaté, sans allocation.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/time.h>

namespace visio {

enum class Level { Debug, Info, Warn, Error };

inline const char* level_name(Level l) noexcept {
    switch (l) {
        case Level::Debug: return "debug";
        case Level::Info:  return "info";
        case Level::Warn:  return "avert";
        case Level::Error: return "erreur";
    }
    return "?";
}

inline void logf(Level l, const char* fmt, ...) noexcept {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    tm lt{};
    localtime_r(&tv.tv_sec, &lt);
    std::fprintf(stderr, "%02d:%02d:%02d.%03ld %-6s ", lt.tm_hour, lt.tm_min, lt.tm_sec,
                 (long)(tv.tv_usec / 1000), level_name(l));
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

#define VISIO_DEBUG(...) ::visio::logf(::visio::Level::Debug, __VA_ARGS__)
#define VISIO_INFO(...)  ::visio::logf(::visio::Level::Info,  __VA_ARGS__)
#define VISIO_WARN(...)  ::visio::logf(::visio::Level::Warn,  __VA_ARGS__)
#define VISIO_ERROR(...) ::visio::logf(::visio::Level::Error, __VA_ARGS__)

} // namespace visio
