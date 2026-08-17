#include "pch.h"
#include "rainbownames.h"
#include "hook.h"
#include "config.h"
#include "debug.h"
#include "ztl/ztl.h"

#include <cmath>

namespace RainbowNames {
namespace {

constexpr uintptr_t kAddr_GetBasicFont = 0x0098A707;
constexpr uintptr_t kAddr_MakeNameTag = 0x005F0334;
constexpr uintptr_t kAddr_DrawNameTags = 0x00942DCC;
constexpr uintptr_t kAddr_SetFont = 0x0046341A;
constexpr uintptr_t kAddr_CanvasDrawTextA = 0x004277AD;
constexpr int kPlayerNameTagType = 1000;
constexpr DWORD kUpdateIntervalMs = 16;
constexpr DWORD kRainbowCycleMs = 5000;
constexpr unsigned int kRainbowPaletteSize = 60;
constexpr int kNameTagTextYOffset = 2;

using GetBasicFontFn = IWzFontPtr*(__cdecl*)(IWzFontPtr*, int);
using DrawNameTagsFn = void(__thiscall*)(void*);
using MakeNameTagFn = int(__thiscall*)(void*, const char*, void*, void*, int,
    void*, void*, void*, void*, void*);
using CanvasDrawTextAFn = unsigned int(__thiscall*)(IWzCanvas*, int, int,
    Ztl_bstr_t, IWzFont*, const Ztl_variant_t&, const Ztl_variant_t&);
using SetFontFn = HRESULT(__thiscall*)(IWzFont*, Ztl_bstr_t, unsigned long,
    unsigned long, const Ztl_variant_t&);

static auto pGetBasicFont = reinterpret_cast<GetBasicFontFn>(kAddr_GetBasicFont);
static auto pDrawNameTags = reinterpret_cast<DrawNameTagsFn>(kAddr_DrawNameTags);
static auto pMakeNameTag = reinterpret_cast<MakeNameTagFn>(kAddr_MakeNameTag);
static auto pCanvasDrawTextA =
    reinterpret_cast<CanvasDrawTextAFn>(kAddr_CanvasDrawTextA);
static auto pSetFont = reinterpret_cast<SetFontFn>(kAddr_SetFont);

thread_local bool g_drawPlayerName = false;
thread_local bool g_pendingPlayerName = false;
DWORD g_lastRedrawTick = 0;
struct RainbowFontCacheEntry {
    unsigned long color;
    int height;
    int fontType;
    IWzFontPtr font;
};
std::vector<RainbowFontCacheEntry> g_rainbowFonts;

unsigned long HsvToArgb(float hueDegrees, float saturation = 1.0f,
    float value = 1.0f) {
    const float hue = std::fmod(hueDegrees, 360.0f) / 60.0f;
    const int sector = static_cast<int>(hue);
    const float fraction = hue - static_cast<float>(sector);
    const float p = value * (1.0f - saturation);
    const float q = value * (1.0f - saturation * fraction);
    const float t = value * (1.0f - saturation * (1.0f - fraction));

    float r = value;
    float g = p;
    float b = p;
    switch (sector % 6) {
    case 0: r = 1.0f; g = t; b = p; break;
    case 1: r = q; g = 1.0f; b = p; break;
    case 2: r = p; g = 1.0f; b = t; break;
    case 3: r = p; g = q; b = 1.0f; break;
    case 4: r = t; g = p; b = 1.0f; break;
    default: r = 1.0f; g = p; b = q; break;
    }

    const auto channel = [](float value) -> unsigned long {
        return static_cast<unsigned long>(value * 255.0f + 0.5f);
    };
    return 0xFF000000UL | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

IWzFont* GetRainbowFont(IWzFont* basicFont, int fontType,
    unsigned long color) {
    if (!basicFont) {
        return nullptr;
    }
    int height = 12;
    try {
        height = basicFont->height;
        if (height <= 0) {
            height = 12;
        }
    } catch (...) {
        height = 12;
    }

    for (auto& cached : g_rainbowFonts) {
        if (cached.color == color && cached.height == height &&
            cached.fontType == fontType) {
            return cached.font;
        }
    }

    IWzFontPtr font;
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", font, nullptr);
        if (!font) {
            return nullptr;
        }
        const HRESULT hr = pSetFont(font, Ztl_bstr_t(L"Dotum"),
            static_cast<unsigned long>(height), color, Ztl_variant_t(L""));
        if (FAILED(hr)) {
            return nullptr;
        }
    } catch (...) {
        return nullptr;
    }

    g_rainbowFonts.push_back({color, height, fontType, font});
    return font;
}

IWzFontPtr* __cdecl GetBasicFont_hook(IWzFontPtr* outFont, int fontType) {
    IWzFontPtr* result = pGetBasicFont(outFont, fontType);
    if (!g_drawPlayerName || !result || !*result) {
        return result;
    }

    const DWORD elapsed = GetTickCount() % kRainbowCycleMs;
    const unsigned int paletteIndex =
        (elapsed * kRainbowPaletteSize) / kRainbowCycleMs;
    const float hue = (static_cast<float>(paletteIndex) * 360.0f) /
        static_cast<float>(kRainbowPaletteSize);
    const unsigned long color = HsvToArgb(hue);
    if (IWzFont* font = GetRainbowFont(*result, fontType, color)) {
        *result = font;
    }
    return result;
}

unsigned int __fastcall CanvasDrawTextA_hook(IWzCanvas* pThis, void* /*edx*/,
    int left, int top, Ztl_bstr_t text, IWzFont* font,
    const Ztl_variant_t& alpha, const Ztl_variant_t& tabOrg) {
    if (g_drawPlayerName) {
        top += kNameTagTextYOffset;
    }
    return pCanvasDrawTextA(pThis, left, top, text, font, alpha, tabOrg);
}

int __fastcall MakeNameTag_hook(void* pThis, void* /*edx*/, const char* name,
    void* arg4, void* arg8, int tagType, void* arg10, void* arg14,
    void* arg18, void* arg1C, void* arg20) {
    const bool rainbow = g_pendingPlayerName && tagType == kPlayerNameTagType;
    g_pendingPlayerName = false;
    g_drawPlayerName = rainbow;
    const int result = pMakeNameTag(pThis, name, arg4, arg8, tagType, arg10,
        arg14, arg18, arg1C, arg20);
    g_drawPlayerName = false;
    return result;
}

void __fastcall DrawNameTags_hook(void* pThis, void* /*edx*/) {
    g_pendingPlayerName = true;
    pDrawNameTags(pThis);
    g_pendingPlayerName = false;
    g_drawPlayerName = false;
}

} // namespace

void Attach() {
    if constexpr (Config::RAINBOW_PLAYER_NAMES) {
        DEBUG_MESSAGE("AttachRainbowNames");
        g_rainbowFonts.reserve(kRainbowPaletteSize);
        ATTACH_HOOK(pGetBasicFont, GetBasicFont_hook);
        ATTACH_HOOK(pMakeNameTag, MakeNameTag_hook);
        ATTACH_HOOK(pCanvasDrawTextA, CanvasDrawTextA_hook);
        ATTACH_HOOK(pDrawNameTags, DrawNameTags_hook);
    }
}

void OnUserUpdate(void* pUser) {
    if constexpr (Config::RAINBOW_PLAYER_NAMES) {
        if (!pUser) {
            return;
        }
        const DWORD now = GetTickCount();
        if (now - g_lastRedrawTick >= kUpdateIntervalMs) {
            g_lastRedrawTick = now;
            g_pendingPlayerName = true;
            pDrawNameTags(pUser);
            g_pendingPlayerName = false;
            g_drawPlayerName = false;
        }
    }
}

} // namespace RainbowNames
