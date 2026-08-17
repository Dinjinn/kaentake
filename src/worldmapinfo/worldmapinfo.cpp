#define NOMINMAX
#include <windows.h>
#include "pch.h"
#include "hook.h"
#include "ztl/ztl.h"

// PCH pulls in windows.h before NOMINMAX takes effect — purge the macros here
#undef min
#undef max

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <mutex>
#include <cstdint>
#include <cstring>   // strlen (render helpers)

#include "wvs/tooltip.h"
#include "wvs/util.h"
#include "wvs/packet.h"
#include "assetutils/getwzinfo.h"
#include "wvs/ctrlwnd.h"
#include "assetutils/iconprovider.h"

// =====================================================
// ADDRESSES
// =====================================================
namespace addr {
constexpr DWORD WM_OnMouseMove = 0x009EE2B3;
constexpr DWORD WM_OnDestroy   = 0x009EB94A;
constexpr DWORD GetFieldOpt    = 0x00437A0C;
} // namespace addr

// =====================================================
// TYPEDEFS
// =====================================================
using t_OnMouseMove = int(__thiscall*)(void*, int, int);
using t_OnDestroy   = void(__thiscall*)(void*);
using t_GetField    = void*(__cdecl*)();

// =====================================================
// ORIGINAL FUNCTIONS
// =====================================================
static auto OnMouseMove_Orig = reinterpret_cast<t_OnMouseMove>(addr::WM_OnMouseMove);
static auto OnDestroy_Orig   = reinterpret_cast<t_OnDestroy>(addr::WM_OnDestroy);
static t_GetField g_GetField = nullptr;

// =====================================================
// UI CONSTANTS
// =====================================================
namespace ui {

// Tooltip size limits
constexpr int MIN_WIDTH  = 200;
constexpr int MAX_WIDTH  = 460;
constexpr int MAX_HEIGHT = 600;

// Padding
constexpr int LEFT_PAD   = 14;
constexpr int RIGHT_PAD  = 14;
constexpr int TOP_PAD    = 8;
constexpr int BOTTOM_PAD = 8;

// Layout metrics
constexpr int HEADER_HEIGHT   = 56; // map icon + street/name block
constexpr int SECTION_LABEL_H = 18; // "Monsters" / "NPCs" label row
constexpr int LINE_HEIGHT     = 46; // each mob/NPC entry row
constexpr int PLAYER_LINE_H   = 18; // each player entry row (text-only, no portrait)
constexpr int CATEGORY_GAP    = 6;  // gap between mob and NPC sections

// Icon
constexpr int ICON_SIZE  = 42; // max rendered icon dimension
constexpr int ICON_COL_W = 52; // icon column width (icon + spacing)

// Cursor offset when placing tooltip
constexpr int CURSOR_OX  = 18;
constexpr int CURSOR_OY  = 18;

// Approximate pixels per character at 12pt Dotum
constexpr int CHAR_W_BODY = 7;

constexpr int MAX_ENTRIES = 10;

// Chrome — title bar + separator strip at the very top
constexpr int CHROME_H     = 28; // total height reserved for title bar area
constexpr int TITLE_BAR_H  = 20; // height of the amber fill strip
constexpr int TITLE_TEXT_Y =  7; // y of the centered title text

// Colors (ARGB)
constexpr unsigned long COL_BG        = 0x99141A24; // dark background fill
constexpr unsigned long COL_TITLEBAR  = 0x88423010; // amber title bar
constexpr unsigned long COL_SEPARATOR = 0xCCE0C070; // gold separator line

} // namespace ui

// =====================================================
// TOOLTIP CORE
// =====================================================
alignas(8) static unsigned char g_ttBuf[0x600];
static bool g_ttInit = false;

inline CUIToolTip* GetTooltip() {
    return reinterpret_cast<CUIToolTip*>(g_ttBuf);
}

void EnsureTooltip() {
    if (g_ttInit) return;
    reinterpret_cast<void(__thiscall*)(void*)>(0x008E49B5)(g_ttBuf);
    g_ttInit = true;
}

void ClearTooltip() {
    if (g_ttInit)
        reinterpret_cast<void(__thiscall*)(void*)>(0x008E6E23)(g_ttBuf);
}

// =====================================================
// WORLD MAP SPOT
// =====================================================
namespace wm {

constexpr int OFF_SPOTS  = 366;
constexpr int STRIDE     = 17;
constexpr int X          = 0;
constexpr int Y          = 1;
constexpr int FIELD_ARR  = 11;
constexpr int ORIGIN_X   = 13;
constexpr int ORIGIN_Y   = 35;
constexpr int HIT_RADIUS = 8;

inline int* getPtr(const void* base, int idx) {
    return *reinterpret_cast<int* const*>(
        reinterpret_cast<const char*>(base) + idx * 4);
}

struct Spot {
    int index = -1;
    int mapId = 0;
};

Spot GetSpot(void* base, int rx, int ry) {
    Spot out{};

    int* spots = getPtr(base, OFF_SPOTS);
    if (!spots) return out;

    int count = *(spots - 1);
    if (count <= 0) return out;

    int cx = rx - ORIGIN_X;
    int cy = ry - ORIGIN_Y;

    int best = -1, bestDist = INT_MAX;

    for (int i = 0; i < count; i++) {
        int*      s   = spots + i * STRIDE;
        unsigned* arr = *(unsigned**)(s + FIELD_ARR);
        if (!arr || *(arr - 1) == 0) continue;

        int dx   = s[X] - cx;
        int dy   = s[Y] - cy;
        int dist = dx * dx + dy * dy;

        if (dist <= HIT_RADIUS * HIT_RADIUS && dist < bestDist) {
            best     = i;
            bestDist = dist;
        }
    }

    if (best < 0) return out;

    int*      s   = spots + best * STRIDE;
    unsigned* arr = *(unsigned**)(s + FIELD_ARR);

    out.index = best;
    out.mapId = (arr && *(arr - 1) > 0) ? arr[0] : 0;
    return out;
}

} // namespace wm

// =====================================================
// QUEST MARKER STATE
// =====================================================
// Replicates the per-NPC classification in CNpc::SetQuestList @ 0x006D1779 so a
// quest bulb can be shown next to NPCs that have an actionable quest. Verified
// against v83.exe.html (image base 0x400000):
//   - quest ids are enumerated for the NPC via CQuestMan::GetQuestByNpc.
//   - in-progress (key present in CharacterData's ZMap @ +0x5FF):
//       CheckCompleteDemand() == 0 (all requirements met) -> completable;  nonzero -> still in progress.
//   - not-in-progress: CheckStartDemand() != 0 -> available.
//   POLARITY (verified — the two checks differ!): CheckStartDemand returns nonzero when startable
//   (the SetQuestList loop skips a quest on jz/eax==0 in the start branch). CheckCompleteDemand
//   returns ~the unmet-requirement count, so 0 == completable (confirmed: != 0 mis-marked
//   in-progress quests as completable). CheckStartDemand already accounts for
//   completion/repeatability, so no separate completed-map lookup is needed.
//   The [0x4B0,0x578) id band is skipped, as the client does.
namespace quest {

constexpr uintptr_t CWvsContext_Inst   = 0x00BE7918; // *(void**) -> CWvsContext*
constexpr uintptr_t CQuestMan_Inst     = 0x00BED614; // *(void**) -> CQuestMan* (null pre-login)
constexpr uintptr_t Off_CharData       = 0x20B8;     // CWvsContext -> CharacterData* (as androidequip.cpp)
constexpr uintptr_t Off_SecondaryStat  = 0x2134;     // &CWvsContext::SecondaryStat (embedded)
constexpr uintptr_t Off_TamingMobLevel = 0x37C0;     // CWvsContext int
constexpr uintptr_t Off_InProgressMap  = 0x5FF;      // CharacterData -> ZMap<u16,ZXString>
constexpr uintptr_t Addr_GetCurFieldID = 0x00A1238B; // long  __thiscall(CWvsContext*)
constexpr uintptr_t Addr_GetQuestByNpc = 0x0071DDEC; // int   __thiscall(qm, u32 npcId, ZArray<u16>&)
constexpr uintptr_t Addr_GetPos        = 0x00500EA5; // void* __thiscall(map, u16* key)
constexpr uintptr_t Addr_CheckStart    = 0x00721163; // int   __thiscall(qm,u16,u32,cd,ss,int,int)
constexpr uintptr_t Addr_CheckComplete = 0x00721D2C; // long  __thiscall(qm,u16,u32,cd,ss)
constexpr int       QBand_Lo           = 0x4B0;
constexpr int       QBand_Hi           = 0x578;

using t_GetCurFieldID = long (__thiscall*)(void*);
using t_GetQuestByNpc = int  (__thiscall*)(void*, unsigned int, void*);
using t_GetPos        = void*(__thiscall*)(void*, unsigned short*);
using t_CheckStart    = int  (__thiscall*)(void*, unsigned short, unsigned int, void*, void*, int, int);
using t_CheckComplete = long (__thiscall*)(void*, unsigned short, unsigned int, void*, void*);

// Inner worker: holds the RAII ZArray (object unwinding) and the raw client calls.
// Kept SEH-free so the __try wrapper below doesn't trip MSVC C2712.
static int GetNpcQuestStateInner(int npcId) {
    void* qm  = *reinterpret_cast<void**>(CQuestMan_Inst);
    void* ctx = *reinterpret_cast<void**>(CWvsContext_Inst);
    if (!qm || !ctx) return 0;

    void* cd = *reinterpret_cast<void**>(reinterpret_cast<char*>(ctx) + Off_CharData);
    if (!cd) return 0;

    void* ss        = reinterpret_cast<char*>(ctx) + Off_SecondaryStat;
    int   tamingLvl = *reinterpret_cast<int*>(reinterpret_cast<char*>(ctx) + Off_TamingMobLevel);
    int   fieldId   = static_cast<int>(reinterpret_cast<t_GetCurFieldID>(Addr_GetCurFieldID)(ctx));

    ZArray<unsigned short> arr;   // GetQuestByNpc RemoveAll's then GetAt-copies into it; ~ZArray frees
    reinterpret_cast<t_GetQuestByNpc>(Addr_GetQuestByNpc)(qm, static_cast<unsigned int>(npcId), &arr);

    size_t count = arr.GetCount();
    if (count == 0 || count > 4096) return 0;

    void* ipMap = reinterpret_cast<char*>(cd) + Off_InProgressMap;
    bool avail = false, prog = false;   // marker priority: completable > available > in-progress

    for (size_t i = 0; i < count; ++i) {
        unsigned short q = arr[i];
        if (q >= QBand_Lo && q < QBand_Hi) continue;            // info/medal band (client skips it)

        bool inProgress = reinterpret_cast<t_GetPos>(Addr_GetPos)(ipMap, &q) != nullptr;
        if (inProgress) {
            // CheckCompleteDemand returns 0 when every completion requirement is met
            // (~unmet-requirement count). 0 = completable; nonzero = still in progress.
            if (reinterpret_cast<t_CheckComplete>(Addr_CheckComplete)(
                    qm, q, static_cast<unsigned int>(npcId), cd, ss) == 0) {
                return 2;                                       // completable (brown book) — top priority
            }
            prog = true;                                        // accepted, not yet completable (open book)
        } else {
            if (reinterpret_cast<t_CheckStart>(Addr_CheckStart)(
                    qm, q, static_cast<unsigned int>(npcId), cd, ss, tamingLvl, fieldId) != 0) {
                avail = true;                                   // available (white bulb)
            }
        }
    }
    return avail ? 1 : (prog ? 3 : 0);
}

// 0 = none, 1 = available (startable), 2 = completable (turn-in ready), 3 = in-progress.
int GetNpcQuestState(int npcId) {
    __try {
        return GetNpcQuestStateInner(npcId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;   // any bad deref -> no marker, never crash
    }
}

} // namespace quest

// =====================================================
// DATA
// =====================================================
struct MobGroup {
    int id, level, count;
    std::string name;
};

struct NpcEntry {
    int id;
    std::string name;
    int state;   // quest marker: 0 none, 1 available, 2 completable, 3 in-progress
};

// One player present in the map (on any channel of this world). Server-fed.
struct PlayerEntry {
    std::string name;
    int level;
    int channel;
};

struct MapLife {
    std::vector<MobGroup>    mobs;
    std::vector<NpcEntry>    npcs;
    std::vector<PlayerEntry> players;
    int playersState = 0;   // 0 not requested, 1 loading (request in flight), 2 ready
};

// =====================================================
// PLAYERS-IN-MAP  (cross-channel, server-fed)
// =====================================================
// The client cannot know who is in a map on which channel — that is server state
// (NPCs/mobs come from the local WZ; players do not). On hovering a NEW map spot we
// fire a tiny request (CP 0x115, mapId); the server replies (LP 0x178) with every
// player in that map across ALL channels of the world: name, level, channel. The
// reply is cached per-map with a short TTL so hovering doesn't spam the server, and
// the tooltip redraws with the data on the next mouse-move once it lands.
namespace players {

constexpr unsigned short CP_WORLD_MAP_PLAYERS = 0x115; // client -> server request
constexpr unsigned short LP_WORLD_MAP_PLAYERS = 0x178; // server -> client response

constexpr uintptr_t Addr_ClientSocket_Inst = 0x00BE7914; // *(void**) -> CClientSocket
constexpr uintptr_t Addr_ClientSocket_Send = 0x0049637B; // CClientSocket::SendPacket(const COutPacket&)
constexpr uintptr_t Addr_ProcessPacket     = 0x004965F1; // CClientSocket::ProcessPacket(CInPacket*)

constexpr DWORD TTL_MS     = 6000; // cached list considered fresh for 6s
constexpr DWORD MIN_REQ_MS = 2500; // never re-request the same map faster than this
constexpr int   MAX_PLAYERS = 40;  // cap rows (matches the server cap)

struct Entry {
    std::vector<PlayerEntry> list;
    DWORD stamp   = 0;     // tick of last response
    DWORD reqTick = 0;     // tick of last request sent
    bool  ready   = false; // a response has landed at least once
};

static std::mutex                     g_mtx;
static std::unordered_map<int, Entry> g_cache;

constexpr size_t MAX_CACHE = 64; // hard cap on retained per-map player lists

// Evict stale entries (older than TTL_MS) when the cache outgrows MAX_CACHE, so a
// long session hovering many map spots can't retain a permanent entry per map.
// Caller must already hold g_mtx.
static void EvictStaleLocked(DWORD now) {
    if (g_cache.size() <= MAX_CACHE) return;
    for (auto it = g_cache.begin(); it != g_cache.end(); ) {
        if (!it->second.ready || (now - it->second.stamp) > TTL_MS)
            it = g_cache.erase(it);
        else
            ++it;
    }
}

static void Send(const COutPacket& o) {
    void* sock = *reinterpret_cast<void**>(Addr_ClientSocket_Inst);
    if (sock)
        reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(Addr_ClientSocket_Send)(sock, o);
}

// Fire a request when we have no fresh data and haven't asked too recently. Always
// touches g_cache[mapId] so the render side sees at least the "loading" state.
void MaybeRequest(int mapId) {
    if (mapId <= 0) return;
    DWORD now = GetTickCount();
    bool send = false;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        EvictStaleLocked(now);                 // reclaim before (maybe) inserting this map
        Entry& e = g_cache[mapId];
        bool stale  = !e.ready || (now - e.stamp) > TTL_MS;
        bool cooled = (e.reqTick == 0) || (now - e.reqTick) >= MIN_REQ_MS;
        if (stale && cooled) { e.reqTick = now; send = true; }
    }
    if (send) {
        COutPacket o(CP_WORLD_MAP_PLAYERS);
        o.Encode4(static_cast<unsigned int>(mapId));
        Send(o);
    }
}

// Copy out the cached players for mapId. state: 0 none, 1 loading, 2 ready.
void GetCached(int mapId, std::vector<PlayerEntry>& out, int& state) {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_cache.find(mapId);
    if (it == g_cache.end()) { state = 0; return; }
    out   = it->second.list;
    state = it->second.ready ? 2 : 1;
}

// Inbound LP 0x178 parser. All reads are bounds-checked via CInPacket::CanRead /
// Decode<T>, so no SEH is needed (and std::string/vector here would forbid __try).
void HandleResponse(CInPacket* p) {
    if (!p) return;
    size_t entryOff = p->GetOffset();
    p->SetOffset(entryOff + 2);                        // skip the 2-byte opcode RELATIVE to where it
                                                       // actually sits (absolute SetOffset(2) was wrong
                                                       // if the opcode isn't at buffer offset 0)
    int mapId = p->Decode<int>();
    if (mapId <= 0) return;
    int count = static_cast<unsigned short>(p->Decode<uint16_t>());
    if (count > MAX_PLAYERS) count = MAX_PLAYERS;

    std::vector<PlayerEntry> list;
    for (int i = 0; i < count; ++i) {
        uint16_t len = p->Decode<uint16_t>();
        if (len == 0 || len > 64 || !p->CanRead(len)) break;
        std::string name(reinterpret_cast<const char*>(p->CurrentPublic()), len);
        p->SetOffset(p->GetOffset() + len);
        if (!p->CanRead(2 + 1)) break;   // need level(u16)+channel(u8); drop a truncated tail
        int level = static_cast<unsigned short>(p->Decode<uint16_t>());
        int ch    = static_cast<unsigned char>(p->Decode<uint8_t>());
        list.push_back({ std::move(name), level, ch });
    }

    DWORD now = GetTickCount();
    std::lock_guard<std::mutex> lk(g_mtx);
    Entry& e = g_cache[mapId];
    e.list  = std::move(list);
    e.stamp = now;
    e.ready = true;
}

} // namespace players

// Resolve the WZ-derived (mobs + NPCs + quest markers) portion of a map. This is the
// expensive part: a WZ property-tree walk plus per-NPC native quest calls. It is map-
// static, so GetMapLife memoizes it by mapId and only re-runs this on a map change.
static void ComputeMapWzLife(int mapId, MapLife& out) {
    std::unordered_map<int, MobGroup> mobMap;
    std::unordered_set<int> npcSeen;

    try {
        wchar_t path[128];
        swprintf(path, L"Map/Map/Map%d/%08d.img", mapId / 100000000, mapId);

        IWzPropertyPtr map = get_rm()->GetObjectA(path).GetUnknown();
        if (!map) return;

        IWzPropertyPtr life = map->item[L"life"].GetUnknown();
        if (!life) return;

        for (int i = 0, n = (int)life->Getcount(); i < n; i++) {
            wchar_t idx[16];
            _itow_s(i, idx, 10);

            IWzPropertyPtr entry = get_unknown(life->item[idx]);
            if (!entry) continue;

            int id             = get_int32(entry->item[L"id"], 0);
            std::wstring type  = (const wchar_t*)_bstr_t(entry->item[L"type"]);

            if (type == L"m") {
                auto& m = mobMap[id];
                if (m.count == 0)
                    m = { id, GetMobLevelById(id), 0, GetMobNameById(id) };
                m.count++;
            } else if (type == L"n" && npcSeen.insert(id).second) {
                out.npcs.push_back({ id, GetNpcById(id), quest::GetNpcQuestState(id) });
            }
        }
    } catch (...) {}

    for (auto& p : mobMap)
        out.mobs.push_back(p.second);

    std::sort(out.mobs.begin(), out.mobs.end(),
        [](const MobGroup& a, const MobGroup& b) { return a.level < b.level; });

    if (out.mobs.size() > ui::MAX_ENTRIES) out.mobs.resize(ui::MAX_ENTRIES);
    if (out.npcs.size() > ui::MAX_ENTRIES) out.npcs.resize(ui::MAX_ENTRIES);
}

MapLife GetMapLife(int mapId) {
    // Memoize the WZ/quest-derived mobs+npcs by mapId: OnMouseMove fires per pixel, so
    // while the cursor sits on (or drifts within the hit radius of) one spot we must NOT
    // re-walk the WZ life node and re-run the native quest sweep every event. Recompute
    // that only when the hovered mapId actually changes; players are always refreshed
    // below since they arrive asynchronously via the server cache (LP 0x178).
    static int     g_lastMapId = -1;
    static MapLife g_lastLife;

    if (mapId != g_lastMapId) {
        MapLife fresh;
        ComputeMapWzLife(mapId, fresh);
        g_lastLife  = std::move(fresh);
        g_lastMapId = mapId;
    }

    MapLife out;
    out.mobs = g_lastLife.mobs;
    out.npcs = g_lastLife.npcs;

    // Players come from the server cache (filled by the LP 0x178 hook), not the WZ.
    players::GetCached(mapId, out.players, out.playersState);

    return out;
}

// =====================================================
// FONT SYSTEM
// =====================================================
static IWzFontPtr g_fontTitle;    // gold  — "Map Info" centered title
static IWzFontPtr g_fontMapName;  // green — street / map name
static IWzFontPtr g_fontMonsters; // red   — Monsters section label
static IWzFontPtr g_fontNpcs;     // blue  — NPCs section label (also reused for Quests label)
static IWzFontPtr g_fontPlayers;  // purple — Players section label
static IWzFontPtr g_fontBody;     // white — entry text
static IWzFontPtr g_fontShadow;   // black — 1px text shadow

bool CreateFont(IWzFontPtr& out, unsigned long color, int size) {
    if (out) return true;
    PcCreateObject<IWzFontPtr>(L"Canvas#Font", out, nullptr);
    if (!out) return false;
    auto fn = reinterpret_cast<HRESULT(__thiscall*)(
        IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(0x0046341A);
    return SUCCEEDED(fn(out, L"Dotum", size, color, Ztl_variant_t(L"B")));
}

void EnsureFonts() {
    CreateFont(g_fontTitle,    0xFFFFD45A, 12); // gold
    CreateFont(g_fontMapName,  0xFF6EDB7E, 13); // green
    CreateFont(g_fontMonsters, 0xFFFF6A6A, 12); // red-orange
    CreateFont(g_fontNpcs,     0xFF6FAFFF, 12); // blue (also used for Quests label)
    CreateFont(g_fontPlayers,  0xFFC9A0FF, 12); // light purple
    CreateFont(g_fontBody,     0xFFFFFFFF, 12); // white
    CreateFont(g_fontShadow,   0xFF000000, 12); // black shadow
}

// =====================================================
// SIZING
// =====================================================
int ComputeWidth(const MapLife& data, const std::string& street, const std::string& map) {
    // Map name text starts after the map mark icon
    const int mapTextX = ui::LEFT_PAD + ui::ICON_SIZE + 8;
    int w = mapTextX + (int)(std::max)(street.size(), map.size()) * ui::CHAR_W_BODY + ui::RIGHT_PAD;

    // Mob / NPC entry rows
    for (auto& m : data.mobs) {
        std::string txt = "[Lv. " + std::to_string(m.level) + "] " + m.name
                        + " (x" + std::to_string(m.count) + ")";
        int ew = ui::LEFT_PAD + ui::ICON_COL_W + (int)txt.size() * ui::CHAR_W_BODY + ui::RIGHT_PAD;
        w = (std::max)(w, ew);
    }
    for (auto& n : data.npcs) {
        int ew = ui::LEFT_PAD + ui::ICON_COL_W + (int)n.name.size() * ui::CHAR_W_BODY + ui::RIGHT_PAD;
        w = (std::max)(w, ew);
    }
    // Player rows are text-only (no icon column): "[Lv.200] LongName  (Ch.20)"
    for (auto& p : data.players) {
        int chars = (int)p.name.size() + 18; // "[Lv.NNN] " + "  (Ch.NN)"
        int ew = ui::LEFT_PAD + chars * ui::CHAR_W_BODY + ui::RIGHT_PAD;
        w = (std::max)(w, ew);
    }

    return (std::max)(ui::MIN_WIDTH, (std::min)(w, ui::MAX_WIDTH));
}

int ComputeHeight(const MapLife& data) {
    bool hasM = !data.mobs.empty();
    bool hasN = !data.npcs.empty();
    bool hasP = data.playersState != 0;   // a Players section is shown once a map is hovered

    int h = ui::CHROME_H + ui::TOP_PAD + ui::HEADER_HEIGHT;

    if (hasM) h += ui::SECTION_LABEL_H + (int)data.mobs.size() * ui::LINE_HEIGHT;
    if (hasM && hasN) h += ui::CATEGORY_GAP;
    if (hasN) h += ui::SECTION_LABEL_H + (int)data.npcs.size() * ui::LINE_HEIGHT;

    if (hasP) {
        if (hasM || hasN) h += ui::CATEGORY_GAP;
        // ready shows one row per player (none when empty); loading shows just the label
        int pRows = (data.playersState == 2) ? (int)data.players.size() : 0;
        h += ui::SECTION_LABEL_H + pRows * ui::PLAYER_LINE_H;
    }

    h += ui::BOTTOM_PAD;
    return std::min(h, ui::MAX_HEIGHT);
}

// =====================================================
// RENDER HELPERS
// =====================================================
int CenterText(const char* txt, int width) {
    return (width - (int)strlen(txt) * 7) / 2;
}

void DrawIconScaled(IWzCanvasPtr canvas, IWzCanvasPtr icon, int x, int y) {
    if (!icon) return;
    const int w = static_cast<int>(icon->Getwidth());
    const int h = static_cast<int>(icon->Getheight());
    if (w <= 0 || h <= 0) return; // skip empty/unloaded canvas — CopyEx would E_INVALIDARG
    int nw = (w >= h) ? ui::ICON_SIZE : (w * ui::ICON_SIZE) / h;
    int nh = (h >= w) ? ui::ICON_SIZE : (h * ui::ICON_SIZE) / w;
    canvas->CopyEx(x, y, icon, CA_OVERWRITE, nw, nh, 0, 0, w, h, vtEmpty);
}

void DrawShadowText(IWzCanvasPtr canvas, int x, int y, const char* txt, IWzFontPtr font) {
    canvas->DrawTextA(x + 1, y + 1, txt, g_fontShadow);
    canvas->DrawTextA(x,     y,     txt, font);
}

// Blit a marker scaled to fit `maxSize` (the QuestIcon bubbles are 44x44).
void DrawMarker(IWzCanvasPtr canvas, IWzCanvasPtr mark, int x, int y, int maxSize) {
    if (!mark) return;
    const int w = static_cast<int>(mark->Getwidth());
    const int h = static_cast<int>(mark->Getheight());
    if (w <= 0 || h <= 0) return; // empty/unloaded canvas — CopyEx would E_INVALIDARG
    const int nw = (w >= h) ? maxSize : (w * maxSize) / h;
    const int nh = (h >= w) ? maxSize : (h * maxSize) / w;
    canvas->CopyEx(x, y, mark, CA_OVERWRITE, nw, nh, 0, 0, w, h, vtEmpty);
}

// =====================================================
// TOOLTIP RENDER
// =====================================================
void ShowTooltip(const std::string& street, const std::string& map, int mapId, int winX, int winY) {

    EnsureTooltip();
    EnsureFonts();

    auto* tt = GetTooltip();
    tt->ClearToolTip();

    auto data  = GetMapLife(mapId);
    int  width  = ComputeWidth(data, street, map);
    int  height = ComputeHeight(data);

    // Position tooltip with bounds clamping so it never leaves the client area
    HWND hWnd = GetForegroundWindow();
    RECT cr   = {};
    GetClientRect(hWnd, &cr);

    int ttX = winX + ui::CURSOR_OX;
    int ttY = winY + ui::CURSOR_OY;

    if (ttX + width  > cr.right)  ttX = winX - width  - 4;
    if (ttY + height > cr.bottom) ttY = winY - height - 4;

    ttX = std::max(ttX, 0);
    ttY = std::max(ttY, 0);

    // Grow the native tooltip box to exactly our computed content height. We MEASURE
    // the tooltip's real per-line height instead of assuming a fixed value: the old
    // 14px/line guess overshot badly whenever the true line height was larger (it
    // only ever counts lines UP), leaving a big dead strip of empty COL_BG below the
    // last row. Two probe sizings give the slope; from that we pick the exact line
    // count, so the box hugs the content (overshoot < one line).
    ZXString<char> zTitle("");
    auto setLines = [&](int n) -> int {
        std::string s((std::max)(0, n), '\n');
        ZXString<char> zDesc(s.c_str());
        tt->ClearToolTip();
        tt->SetToolTip_String2(ttX, ttY, zTitle, zDesc, 0, 0, 0, width, 1, 0);
        return tt->m_nHeight;
    };
    int h2  = setLines(2);
    int h12 = setLines(12);
    int perLine = (h12 > h2) ? (std::max)(1, (h12 - h2) / 10) : 14; // px per blank line
    int base    = h2 - 2 * perLine;                                 // box height at 0 content lines
    int need    = (std::max)(1, (height - base + perLine - 1) / perLine); // ceil to reach height
    setLines(need);
    for (int i = 0; i < 4 && tt->m_nHeight < height; ++i) setLines(++need); // fix rounding under-shoot

    if (!tt->m_pLayer) return;
    auto canvas = tt->m_pLayer->canvas[0];
    if (!canvas) return;

    try {

    // --- Chrome ---
    canvas->DrawRectangle(2, 2, width - 4, tt->m_nHeight - 4, ui::COL_BG);
    canvas->DrawRectangle(4, 4, width - 8, ui::TITLE_BAR_H, ui::COL_TITLEBAR);
    canvas->DrawRectangle(8, 4 + ui::TITLE_BAR_H + 2, width - 16, 1, ui::COL_SEPARATOR);

    const char* titleStr = "Map Info";
    int titleW = (int)strlen(titleStr) * 7;
    DrawShadowText(canvas, (width - titleW) / 2, ui::TITLE_TEXT_Y, titleStr, g_fontTitle);

    // --- Map name header ---
    int hdrY = ui::CHROME_H;
    int hdrX = ui::LEFT_PAD;

    auto mapIcon = GetMapIcon(mapId);
    if (mapIcon) {
        DrawIconScaled(canvas, mapIcon, hdrX, hdrY + ui::TOP_PAD);
        hdrX += ui::ICON_SIZE + 8;
    }

    DrawShadowText(canvas, hdrX, hdrY + ui::TOP_PAD + 4,      street.c_str(), g_fontMapName);
    DrawShadowText(canvas, hdrX, hdrY + ui::TOP_PAD + 4 + 16, map.c_str(),    g_fontMapName);

    // --- Content sections ---
    int yPos = ui::CHROME_H + ui::TOP_PAD + ui::HEADER_HEIGHT;

    auto drawSection = [&](const char* label, IWzFontPtr labelFont, auto& list, auto iconFn, auto textFn, auto markFn) {
        std::string hdr = std::string(label) + " (" + std::to_string(list.size()) + "):";
        DrawShadowText(canvas, CenterText(hdr.c_str(), tt->m_nWidth), yPos, hdr.c_str(), labelFont);
        yPos += ui::SECTION_LABEL_H;

        for (auto& e : list) {
            DrawIconScaled(canvas, iconFn(e.id), ui::LEFT_PAD, yPos + 2);
            int qst = markFn(e);   // 0 none / 1 available (bulb) / 2 completable (brown book) / 3 in-progress (open book)
            if (qst) {
                const int ms = 22;                                   // marker badge size
                int mx = ui::LEFT_PAD + ui::ICON_SIZE - ms + 2;      // top-right corner of the portrait
                int my = yPos;
                IWzCanvasPtr mk = GetQuestMarkerIcon(qst);
                if (mk) {
                    DrawMarker(canvas, mk, mx, my, ms);
                } else {
                    unsigned long fb = (qst == 2) ? 0xFF8B5A2B    // brown  (completable)
                                     : (qst == 3) ? 0xFFE0A050    // tan    (in-progress)
                                     :              0xFFF0F0F0;   // white  (available)
                    canvas->DrawRectangle(mx, my, 10, 10, fb);
                }
            }
            std::string txt = textFn(e);
            DrawShadowText(canvas, ui::LEFT_PAD + ui::ICON_COL_W, yPos + 16, txt.c_str(), g_fontBody);
            yPos += ui::LINE_HEIGHT;
        }
    };

    if (!data.mobs.empty())
        drawSection("Monsters", g_fontMonsters, data.mobs, GetMobIcon,
            [](const MobGroup& m) -> std::string {
                return "[Lv. " + std::to_string(m.level) + "] " + m.name
                       + " (x" + std::to_string(m.count) + ")";
            },
            [](const MobGroup&) { return 0; });   // mobs never get a quest marker

    if (!data.mobs.empty() && !data.npcs.empty())
        yPos += ui::CATEGORY_GAP;

    if (!data.npcs.empty())
        drawSection("NPCs", g_fontNpcs, data.npcs, GetNpcIcon,
            [](const NpcEntry& n) -> std::string { return n.name; },
            [](const NpcEntry& n) { return n.state; });   // quest bulb when actionable

    // --- Players section (server-fed, cross-channel) — text only, no portrait ---
    if (data.playersState != 0) {
        if (!data.mobs.empty() || !data.npcs.empty())
            yPos += ui::CATEGORY_GAP;

        std::string hdr = (data.playersState == 2)
            ? "Players (" + std::to_string(data.players.size()) + "):"
            : std::string("Players (loading...):");
        DrawShadowText(canvas, CenterText(hdr.c_str(), tt->m_nWidth), yPos, hdr.c_str(), g_fontPlayers);
        yPos += ui::SECTION_LABEL_H;

        // empty + ready shows just the "Players (0):" label, no filler row
        for (auto& pl : data.players) {
            std::string line = "[Lv." + std::to_string(pl.level) + "] " + pl.name
                             + "  (Ch." + std::to_string(pl.channel) + ")";
            DrawShadowText(canvas, ui::LEFT_PAD, yPos, line.c_str(), g_fontBody);
            yPos += ui::PLAYER_LINE_H;
        }
    }

    } catch (...) {
        // Swallow any COM exception (_com_error from DrawTextA / CopyEx / DrawRectangle)
        // so a failed render never crashes the game process.
    }
}

// =====================================================
// HOOK
// =====================================================
void* GetFieldCtx() {
    if (!g_GetField)
        g_GetField = reinterpret_cast<t_GetField>(addr::GetFieldOpt);
    return g_GetField ? g_GetField() : nullptr;
}

int __fastcall OnMouseMove_Hook(void* ecx, void*, int x, int y) {
    int ret = OnMouseMove_Orig(ecx, x, y);

    if (!ecx || !GetFieldCtx()) {
        ClearTooltip();
        return ret;
    }

    auto spot = wm::GetSpot((char*)ecx - 4, x, y);
    if (spot.index < 0 || spot.mapId <= 0) {
        ClearTooltip();
        return ret;
    }

    reinterpret_cast<IUIMsgHandler*>(ecx)->ClearToolTip();

    std::string full    = GetMapById(spot.mapId);
    size_t      split   = full.find(" - ");
    std::string street  = full.substr(0, split);
    std::string mapName = (split != std::string::npos) ? full.substr(split + 3) : full;

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(GetForegroundWindow(), &pt);

    players::MaybeRequest(spot.mapId);   // ask the server who's in this map (throttled + cached)
    ShowTooltip(street, mapName, spot.mapId, pt.x, pt.y);
    return ret;
}

void __fastcall OnDestroy_Hook(void* ecx, void*) {
    ClearTooltip();
    OnDestroy_Orig(ecx);
}

// Inbound LP 0x178 reply is dispatched by PacketDispatcher.cpp (the central CClientSocket::
// ProcessPacket hook), which calls players::HandleResponse below. A SEPARATE Detours hook here on
// the heavily-chained 0x004965F1 (7+ detours deep) did not reliably fire, so the server's reply
// went undecoded and the tooltip was stuck on "Players (loading...)". HandleResponse stays public
// (non-static, namespace players) so PacketDispatcher can call it.

// =====================================================
// INSTALL
// =====================================================
void AttachMapInfoToolTip() {
    ATTACH_HOOK(OnMouseMove_Orig, OnMouseMove_Hook);
    ATTACH_HOOK(OnDestroy_Orig, OnDestroy_Hook);
    // LP 0x178 reply is handled in PacketDispatcher.cpp (central inbound hook) — no separate
    // ProcessPacket detour here; the chained hook on 0x004965F1 was not firing reliably.
}
