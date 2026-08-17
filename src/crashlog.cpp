#include "pch.h"
#include "crashlog.h"

#include <StackWalker.h>
#include <cstdarg>
#include <cstring>

namespace {

volatile LONG g_loggingCrash = 0;
LPTOP_LEVEL_EXCEPTION_FILTER g_fallbackFilter = nullptr;
bool g_installed = false;
char g_symbolPath[3 * MAX_PATH] = ".";

bool IsFatalException(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_STACK_OVERFLOW:
        return true;
    default:
        return false;
    }
}

void WriteLine(HANDLE file, const char* format, ...) {
    char buffer[2048] = {};
    va_list args;
    va_start(args, format);
    const int length = _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    if (length <= 0)
        return;
    DWORD written = 0;
    WriteFile(file, buffer, static_cast<DWORD>(length), &written, nullptr);
}

void WriteTimestamp(HANDLE file) {
    SYSTEMTIME localTime = {};
    SYSTEMTIME utcTime = {};
    GetLocalTime(&localTime);
    GetSystemTime(&utcTime);

    TIME_ZONE_INFORMATION timeZone = {};
    const DWORD timeZoneId = GetTimeZoneInformation(&timeZone);
    LONG bias = timeZone.Bias;
    if (timeZoneId == TIME_ZONE_ID_STANDARD)
        bias += timeZone.StandardBias;
    else if (timeZoneId == TIME_ZONE_ID_DAYLIGHT)
        bias += timeZone.DaylightBias;

    // Windows expresses Bias as minutes added to local time to obtain UTC.
    // ISO 8601 uses the opposite sign for the local offset from UTC.
    const LONG offsetMinutes = -bias;
    const char offsetSign = offsetMinutes < 0 ? '-' : '+';
    const LONG absoluteOffset = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
    WriteLine(file,
            "time=%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02ld:%02ld "
            "utc=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\r\n",
            localTime.wYear, localTime.wMonth, localTime.wDay, localTime.wHour,
            localTime.wMinute, localTime.wSecond, localTime.wMilliseconds, offsetSign,
            absoluteOffset / 60, absoluteOffset % 60, utcTime.wYear, utcTime.wMonth,
            utcTime.wDay, utcTime.wHour, utcTime.wMinute, utcTime.wSecond,
            utcTime.wMilliseconds);
}

const char* FileName(const char* path) {
    if (!path || !path[0])
        return nullptr;
    const char* slash = strrchr(path, '\\');
    const char* forwardSlash = strrchr(path, '/');
    if (!slash || (forwardSlash && forwardSlash > slash))
        slash = forwardSlash;
    return slash ? slash + 1 : path;
}

void AppendDirectory(char* pathList, size_t capacity, const char* filePath) {
    if (!filePath || !filePath[0])
        return;
    char directory[MAX_PATH] = {};
    strcpy_s(directory, filePath);
    char* slash = strrchr(directory, '\\');
    if (!slash)
        return;
    *slash = '\0';
    if (!directory[0] || strstr(pathList, directory))
        return;
    strcat_s(pathList, capacity, ";");
    strcat_s(pathList, capacity, directory);
}

class CrashStackWalker final : public StackWalker {
public:
    CrashStackWalker(HANDLE file, const char* symbolPath)
        : StackWalker(RetrieveSymbol | RetrieveLine | RetrieveModuleInfo | SymBuildPath, symbolPath),
          file_(file) {}

protected:
    void OnCallstackEntry(CallstackEntryType type, CallstackEntry& entry) override {
        if (type == lastEntry || !entry.offset)
            return;
        const char* module = entry.moduleName[0] ? entry.moduleName : FileName(entry.loadedImageName);
        if (!module || !module[0])
            module = "<unknown-module>";
        const char* symbol = entry.undFullName[0]
                                     ? entry.undFullName
                                     : (entry.undName[0] ? entry.undName : entry.name);
        WriteLine(file_, "#%02u  0x%08llX  %s", frame_++, entry.offset, module);
        if (symbol && symbol[0])
            WriteLine(file_, "!%s+0x%llX", symbol, entry.offsetFromSymbol);
        else if (entry.baseOfImage && entry.offset >= entry.baseOfImage)
            WriteLine(file_, "+0x%llX", entry.offset - entry.baseOfImage);
        if (entry.lineFileName[0])
            WriteLine(file_, "  (%s:%lu)", entry.lineFileName, entry.lineNumber);
        WriteLine(file_, "\r\n");
    }

    void OnDbgHelpErr(LPCSTR, DWORD, DWORD64) override {}
    void OnLoadModule(LPCSTR, LPCSTR, DWORD64, DWORD, DWORD, LPCSTR, LPCSTR, ULONGLONG) override {}
    void OnSymInit(LPCSTR searchPath, DWORD, LPCSTR) override {
        WriteLine(file_, "symbols: %s\r\n", searchPath);
    }
    void OnOutput(LPCSTR text) override { WriteLine(file_, "%s", text); }

private:
    HANDLE file_;
    unsigned int frame_ = 0;
};

LONG LogUnhandledException(EXCEPTION_POINTERS* exception) {
    if (!exception || !exception->ExceptionRecord ||
            !IsFatalException(exception->ExceptionRecord->ExceptionCode) ||
            InterlockedCompareExchange(&g_loggingCrash, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    HANDLE file = CreateFileA(".\\Kaentake_crash.log", FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        CONTEXT* context = exception->ContextRecord;
        WriteLine(file, "\r\n=== Kaentake crash ===\r\n");
        WriteTimestamp(file);
        WriteLine(file, "tick=%lu code=0x%08lX address=0x%p\r\n", GetTickCount(),
                exception->ExceptionRecord->ExceptionCode,
                exception->ExceptionRecord->ExceptionAddress);
        if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                exception->ExceptionRecord->NumberParameters >= 2) {
            WriteLine(file, "access=%s bad_address=0x%p\r\n",
                    exception->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" : "write",
                    reinterpret_cast<void*>(exception->ExceptionRecord->ExceptionInformation[1]));
        }
#if defined(_M_IX86)
        if (context)
            WriteLine(file, "EIP=%08lX ESP=%08lX EBP=%08lX EAX=%08lX ECX=%08lX EDX=%08lX\r\n",
                    context->Eip, context->Esp, context->Ebp, context->Eax, context->Ecx, context->Edx);
#endif
        CrashStackWalker walker(file, g_symbolPath);
        if (!context || !walker.ShowCallstack(GetCurrentThread(), context))
            WriteLine(file, "<stack walk unavailable, error %lu>\r\n", GetLastError());
        FlushFileBuffers(file);
        CloseHandle(file);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void InstallCrashLogger() {
    if (g_installed)
        return;

    char modulePath[MAX_PATH] = {};
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&InstallCrashLogger), &module)) {
        GetModuleFileNameA(module, modulePath, MAX_PATH);
        AppendDirectory(g_symbolPath, sizeof(g_symbolPath), modulePath);
    }
    char executablePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    AppendDirectory(g_symbolPath, sizeof(g_symbolPath), executablePath);
    g_installed = true;
    g_fallbackFilter = SetUnhandledExceptionFilter(CrashUnhandledExceptionFilter);
}

LONG WINAPI CrashUnhandledExceptionFilter(EXCEPTION_POINTERS* exception) {
    const LONG result = LogUnhandledException(exception);
    if (g_fallbackFilter && g_fallbackFilter != CrashUnhandledExceptionFilter)
        return g_fallbackFilter(exception);
    return result;
}

void SetCrashLoggerFallback(LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    if (filter != CrashUnhandledExceptionFilter)
        g_fallbackFilter = filter;
}
