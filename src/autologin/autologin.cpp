#include "../pch.h"
#include "autologin.h"
#include "../ztl/ztl.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace AutoLogin {
namespace {

constexpr char kLoginIni[] = ".\\login.ini";
constexpr size_t kFieldSize = 64;
constexpr bool kEnableConsole = false;

struct LoginConfiguration {
    bool enabled = false;
    char user[kFieldSize] = {};
    char password[kFieldSize] = {};
    char pin[kFieldSize] = {};
    char pic[kFieldSize] = {};
    int world = -1;
    int channel = -1;
    int character = -1;
};

LoginConfiguration g_config;
bool g_credentialsQueued = false;
bool g_worldQueued = false;
bool g_channelQueued = false;
bool g_characterQueued = false;
bool g_avatarSelectionReady = false;
bool g_consoleReady = false;
int g_lastStep = -1;
DWORD g_worldReadyAt = 0;
DWORD g_characterReadyAt = 0;

void Log(const char* format, ...) {
    if (!g_consoleReady) {
        return;
    }

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void EnsureConsole() {
    if (!kEnableConsole || g_consoleReady) {
        return;
    }

    if (!AllocConsole() && GetLastError() != ERROR_ACCESS_DENIED) {
        return;
    }

    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    SetConsoleTitleA("Kaentake Auto Login");
    g_consoleReady = true;
    Log("[autologin] console attached");
    Log("[autologin] configuration enabled; credentials are hidden");
}

bool CopyField(char* destination, const char* source) {
    if (!source || !*source) {
        return false;
    }
    strncpy_s(destination, kFieldSize, source, _TRUNCATE);
    return destination[0] != '\0';
}

bool ParseInteger(const char* value, int* result) {
    if (!value || !*value || !result) {
        return false;
    }
    char* end = nullptr;
    const long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 0 || parsed > 255) {
        return false;
    }
    *result = static_cast<int>(parsed);
    return true;
}

bool ParseLoginValue(const char* value) {
    if (!value || !*value) {
        return false;
    }

    char buffer[512] = {};
    strncpy_s(buffer, sizeof(buffer), value, _TRUNCATE);
    char* context = nullptr;
    char* fields[7] = {};
    char* field = strtok_s(buffer, ":", &context);
    for (size_t i = 0; field && i < 7; ++i) {
        fields[i] = field;
        field = strtok_s(nullptr, ":", &context);
    }
    if (field || !fields[0] || !fields[1] || !fields[2] || !fields[3] ||
        !fields[4] || !fields[5] || !fields[6]) {
        return false;
    }

    int world = -1;
    int channel = -1;
    int character = -1;
    if (!ParseInteger(fields[3], &world) ||
        !ParseInteger(fields[4], &channel) ||
        !ParseInteger(fields[5], &character) ||
        !CopyField(g_config.user, fields[0]) ||
        !CopyField(g_config.password, fields[1]) ||
        !CopyField(g_config.pin, fields[2]) ||
        !CopyField(g_config.pic, fields[6])) {
        return false;
    }

    g_config.world = world;
    g_config.channel = channel;
    g_config.character = character;
    g_config.enabled = true;
    return true;
}

void ReadIniConfiguration() {
    char value[512] = {};
    if (GetPrivateProfileIntA("login", "enabled", 0, kLoginIni) == 0) {
        return;
    }

    GetPrivateProfileStringA("login", "user", "", g_config.user,
        static_cast<DWORD>(kFieldSize), kLoginIni);
    GetPrivateProfileStringA("login", "password", "", g_config.password,
        static_cast<DWORD>(kFieldSize), kLoginIni);
    GetPrivateProfileStringA("login", "pin", "", g_config.pin,
        static_cast<DWORD>(kFieldSize), kLoginIni);
    GetPrivateProfileStringA("login", "pic", "", g_config.pic,
        static_cast<DWORD>(kFieldSize), kLoginIni);
    GetPrivateProfileStringA("login", "world", "-1", value, sizeof(value),
        kLoginIni);
    ParseInteger(value, &g_config.world);
    GetPrivateProfileStringA("login", "channel", "-1", value, sizeof(value),
        kLoginIni);
    ParseInteger(value, &g_config.channel);
    GetPrivateProfileStringA("login", "character", "-1", value,
        sizeof(value), kLoginIni);
    ParseInteger(value, &g_config.character);
    g_config.enabled = g_config.user[0] && g_config.password[0];
}

void ReadCommandLineConfiguration() {
    const char* commandLine = GetCommandLineA();
    if (!commandLine) {
        return;
    }

    const char* login = strstr(commandLine, "-MAPLE_LOGIN=");
    if (login) {
        ParseLoginValue(login + strlen("-MAPLE_LOGIN="));
    }
    const char* toggle = strstr(commandLine, "-MAPLE_AUTOLOGIN=0");
    if (toggle && (!login || toggle < login)) {
        g_config.enabled = false;
    }
}

void SetLoginField(void* login, uintptr_t offset, const char* value) {
    auto* field = reinterpret_cast<ZXString<char>*>(
        reinterpret_cast<uintptr_t>(login) + offset);
    *field = ZXString<char>(value);
}

template <typename T>
T ReadField(void* object, uintptr_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

bool SendWorldChannelPacket(void* login) {
    if (!login || g_config.world < 0 || g_config.channel < 0) {
        return false;
    }

    // v83's SendViewAllCharPacket takes the protocol world ID, while the
    // configuration uses the visible zero-based world index.  Resolve that
    // ID from CLogin::m_aWorldItem before calling the v83 routine.
    auto* worlds = *reinterpret_cast<void**>(
        reinterpret_cast<uintptr_t>(login) + 0x18C);
    const int worldCount = worlds
        ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(worlds) - sizeof(int))
        : 0;
    if (!worlds || g_config.world >= worldCount) {
        Log("[autologin] world index %d unavailable (count=%d)",
            g_config.world, worldCount);
        return false;
    }

    constexpr size_t kWorldItemSize = 32;
    const int worldId = *reinterpret_cast<int*>(
        reinterpret_cast<uintptr_t>(worlds) +
        kWorldItemSize * static_cast<size_t>(g_config.world));
    Log("[autologin] sending world index %d (id=%d), channel index %d",
        g_config.world, worldId, g_config.channel);

    const int result = reinterpret_cast<int(__thiscall*)(void*, int, int)>(
        0x005F6D6A)(login, worldId, g_config.channel);
    Log("[autologin] world/channel packet result=%d request flag=%d",
        result, ReadField<int>(login, 0x170));
    return result != 0;
}

bool SelectFirstAvailableCharacter(void* login) {
    auto* characters = *reinterpret_cast<void**>(
        reinterpret_cast<uintptr_t>(login) + 0x194);
    if (!characters) {
        return false;
    }

    int characterIndex = -1;
    for (int index = 0; index < 15; ++index) {
        const int candidateId = *reinterpret_cast<int*>(
            reinterpret_cast<uintptr_t>(characters) + 684 * index);
        if (candidateId != 0) {
            characterIndex = index;
            break;
        }
    }
    if (characterIndex < 0) {
        Log("[autologin] no available character found");
        return false;
    }
    *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(login) + 0x190) =
        characterIndex;
    return true;
}

bool SendNativeCharacterPacket(void* login) {
    if (!SelectFirstAvailableCharacter(login)) {
        return false;
    }
    const int selected = ReadField<int>(login, 0x190);
    Log("[autologin] calling v83 SendSelectCharPacket for available index %d",
        selected);
    reinterpret_cast<void(__thiscall*)(void*)>(0x005F726D)(login);
    Log("[autologin] native character selection returned; request flag=%d",
        ReadField<int>(login, 0x170));
    return ReadField<int>(login, 0x170) != 0;
}

} // namespace

void LoadConfiguration() {
    ReadIniConfiguration();
    ReadCommandLineConfiguration();
}

void OnWorldInformation(void* login) {
    if (!g_config.enabled || !login || g_config.world < 0 || g_worldQueued) {
        return;
    }
    // Defer the packet until Update, matching the v95 flow and avoiding a
    // nested send while CLogin is still dispatching OnWorldInformation.
    g_worldReadyAt = GetTickCount() + 1000;
    g_worldQueued = true;
    Log("[autologin] world information loaded; world/channel queued");
}

void OnAvatarSelectCharacter(void* avatar, int index) {
    if (!g_config.enabled || !avatar || index < 0 || g_characterQueued) {
        return;
    }
    g_avatarSelectionReady = true;
    Log("[autologin] character UI ready at index %d", index);
}

void BeforeLoginUpdate(void* login) {
    if (!g_config.enabled || !login) {
        return;
    }

    EnsureConsole();
    if (g_credentialsQueued) {
        return;
    }

    // CLogin::Update consumes these two fields on the title step and sends
    // the normal CP_CheckPassword packet through the existing client code.
    SetLoginField(login, 0x218, g_config.user);
    SetLoginField(login, 0x21C, g_config.password);
    g_credentialsQueued = true;
    Log("[autologin] credentials queued (user length=%zu)", strlen(g_config.user));
}

void AfterLoginUpdate(void* login) {
    if (!g_config.enabled || !login) {
        return;
    }

    const int step = ReadField<int>(login, 0x168);
    if (step != g_lastStep) {
        g_lastStep = step;
        Log("[autologin] login step=%d worldQueued=%d channelQueued=%d characterQueued=%d",
            step, g_worldQueued, g_channelQueued, g_characterQueued);
    }
    if (g_worldQueued && !g_channelQueued &&
        static_cast<LONG>(GetTickCount() - g_worldReadyAt) >= 0) {
        if (SendWorldChannelPacket(login)) {
            g_channelQueued = true;
        }
        return;
    }
    if ((step == 2 || step == 5) && g_avatarSelectionReady &&
        g_config.character >= 0 &&
        !g_characterQueued) {
        if (g_characterReadyAt == 0) {
            g_characterReadyAt = GetTickCount() + 1000;
            Log("[autologin] character selection ready; waiting 1 second");
            return;
        }
        if (static_cast<LONG>(GetTickCount() - g_characterReadyAt) < 0) {
            return;
        }
        Log("[autologin] selecting first available character");
        if (SendNativeCharacterPacket(login)) {
            g_characterQueued = true;
        }
    }
}

} // namespace AutoLogin
