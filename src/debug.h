#pragma once

#ifdef _DEBUG
#include <set>
#define DEBUG_MESSAGE(FORMAT, ...) DebugMessage(FORMAT, __VA_ARGS__)
// Diagnostics that fire once per call site (or once per id) and then go quiet,
// for conditions that would otherwise spam every frame.
#define LOG_ONCE(FORMAT, ...) \
    do { \
        static bool _bLogged = false; \
        if (!_bLogged) { \
            _bLogged = true; \
            DebugMessage(FORMAT, __VA_ARGS__); \
        } \
    } while (0)
#define LOG_ONCE_PER_ID(ID, FORMAT, ...) \
    do { \
        static std::set<int> _sLogged; \
        if (_sLogged.insert(static_cast<int>(ID)).second) { \
            DebugMessage(FORMAT, __VA_ARGS__); \
        } \
    } while (0)
#else
#define DEBUG_MESSAGE(FORMAT, ...)
#define LOG_ONCE(FORMAT, ...)
#define LOG_ONCE_PER_ID(ID, FORMAT, ...)
#endif

#define LogMessage DEBUG_MESSAGE


void DebugMessage(const char* sFormat, ...);

void ErrorMessage(const char* sFormat, ...);