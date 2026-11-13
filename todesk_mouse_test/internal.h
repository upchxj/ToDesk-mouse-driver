#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

struct TestLog {
    char text[32768]{};
    void add(const char* format, ...) {
        const size_t used = strlen(text);
        if (used >= sizeof(text) - 1) return;
        va_list args;
        va_start(args, format);
        _vsnprintf_s(text + used, sizeof(text) - used, _TRUNCATE, format, args);
        va_end(args);
    }
};
int test_window_run(TestLog& log, bool hidden);
