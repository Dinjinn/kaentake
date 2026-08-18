#include "pch.h"
#include "debug.h"
#ifdef KAENTAKE_HAS_SPDLOG
#include "util/log.h"
#endif
#include <windows.h>
#include <strsafe.h>


void DebugMessage(const char* pszFormat, ...) {
    char pszDest[1024];
    size_t cbDest = 1024 * sizeof(char);
    va_list argList;
    va_start(argList, pszFormat);
    StringCbVPrintfA(pszDest, cbDest, pszFormat, argList);
    va_end(argList);
#ifdef KAENTAKE_HAS_SPDLOG
    if (Kaentake::Log::IsInitialized())
        Kaentake::Log::Debug(pszDest);
    else
        OutputDebugStringA(pszDest);
#else
    OutputDebugStringA(pszDest);
#endif
}

void ErrorMessage(const char* pszFormat, ...) {
    char pszDest[1024];
    size_t cbDest = 1024 * sizeof(char);
    va_list argList;
    va_start(argList, pszFormat);
    StringCbVPrintfA(pszDest, cbDest, pszFormat, argList);
    va_end(argList);
#ifdef KAENTAKE_HAS_SPDLOG
    Kaentake::Log::Error(pszDest);
#endif
    MessageBoxA(nullptr, pszDest, "Error", MB_ICONERROR);
}
