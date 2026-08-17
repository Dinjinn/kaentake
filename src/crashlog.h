#pragma once

void InstallCrashLogger();
LONG WINAPI CrashUnhandledExceptionFilter(EXCEPTION_POINTERS* exception);
void SetCrashLoggerFallback(LPTOP_LEVEL_EXCEPTION_FILTER filter);
