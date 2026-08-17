#include "pch.h"
#include "hook.h"
#include "config.h"
#include "debug.h"
#include "wvs/packet.h"
#include "wvs/util.h"
#include "ztl/ztl.h"
#include "ztl/zcoll.h"
#include "ztl/zstr.h"

#include <string>


// ============================================================================
// Hyper Telerock — click world map to teleport
// ============================================================================
namespace {

struct WorldMapSubItem {
    int x;                          // +0x00
    int y;                          // +0x04
    int type;                       // +0x08
    char* streetName;               // +0x0C
    char* mapName;                  // +0x10
    char* mapDescription;           // +0x14
    int isActive;                   // +0x18
    char pad[16];                   // +0x1C
    ZArray<uint32_t> mapData;       // +0x2C
    char pad_[20];                  // +0x34
};
// total size = 68 bytes (0x44)

struct WorldMapPanel {
    char pad[0x5B4];
    ZArray<WorldMapSubItem> subItems; // +0x5B4
};

using CUtilDlg__YesNo_t = int(__cdecl*)(void* sMsg, bool bCenter, bool bFixedW);
static auto CUtilDlg__YesNo = reinterpret_cast<CUtilDlg__YesNo_t>(0x00992DFD);

using ZXString_char___Assign_t = void(__fastcall*)(void* _this, void* edx, const char* s, int n);
static auto ZXString_char___Assign = reinterpret_cast<ZXString_char___Assign_t>(0x00414617);

// CClientSocket::SendPacket
using SendPacket_t = void(__thiscall*)(void*, const COutPacket&);
static auto CClientSocket__SendPacket = reinterpret_cast<SendPacket_t>(0x0049637B);

static auto pWorldMapOnMouseButton = reinterpret_cast<void(__thiscall*)(void*, unsigned int, unsigned int, int, int)>(0x009EE6E5);

void __fastcall Hook_WorldMapOnMouseButton(void* _this, void* _EDX, unsigned int a2, unsigned int a3, int32_t x, int32_t y) {
    WorldMapPanel* panel = static_cast<WorldMapPanel*>(_this);
    const size_t count = panel->subItems.GetCount();
    int32_t fix_x = x - 10;
    int32_t fix_y = y - 35;
    bool sendpacket = false;

    for (size_t i = 0; i < count && a2 == 514; i++) {
        const auto subItem = &panel->subItems[i];
        int distance = ((fix_x - subItem->x) * (fix_x - subItem->x)) + ((fix_y - subItem->y) * (fix_y - subItem->y));

        if (distance <= 17) {
            if (subItem->type == 2) { break; }

            bool canTP = false;
            void* msg = nullptr;
            std::string toCopy = "Would you like to teleport to:";

            if (subItem->mapName != nullptr) {
                toCopy += "\r\n[";
                toCopy += subItem->mapName;
                toCopy += "]";
                canTP = true;
            } else if (subItem->streetName != nullptr) {
                toCopy += "\r\n[";
                toCopy += subItem->streetName;
                toCopy += "]";
                canTP = true;
            }

            if (canTP) {
                ZXString_char___Assign(&msg, nullptr, toCopy.c_str(), -1);

                if (CUtilDlg__YesNo(msg, true, false) == 6) {
                    uint32_t mapID = subItem->mapData[0];
                    COutPacket oPacket(0xF1);
                    oPacket.Encode4(mapID);

                    void* pSocket = *reinterpret_cast<void**>(0x00BE7914);
                    CClientSocket__SendPacket(pSocket, oPacket);
                    sendpacket = true;

                    // Close world map dialog: call vftable[13] with -1
                    void** vftable = *reinterpret_cast<void***>((char*)_this - 4);
                    reinterpret_cast<void(__thiscall*)(void*, int)>(vftable[13])((char*)_this - 4, -1);
                }
            }
        }
    }

    if (!sendpacket) {
        pWorldMapOnMouseButton(_this, a2, a3, x, y);
    }
}


// ============================================================================
// Item ID Tooltip — show item ID in every item's tooltip description
// ============================================================================
using CItemInfo__GetItemDesc_t = ZXString<char>*(__thiscall*)(void*, ZXString<char>*, int);
static auto pGetItemDesc = reinterpret_cast<CItemInfo__GetItemDesc_t>(0x005CF69E);

ZXString<char>* __fastcall Hook_GetItemDesc(void* pThis, void* edx, ZXString<char>* result, int nItemID) {
    auto ret = pGetItemDesc(pThis, result, nItemID);

    int type = nItemID / 1000000;
    if (type >= 1 && type <= 5) {
        if (ret->GetLength() > 0) {
            *ret += "\r\n";
        }
        *ret += "#cItem ID: ";
        *ret += std::to_string(nItemID).c_str();
        *ret += "#";
    }

    return ret;
}


// ============================================================================
// Meso Drop Color — changes meso pickup message color
// ============================================================================
const DWORD dwMesoDropColorRet = 0x00A20B7B;

__declspec(naked) void MesoDropColor_CodeCave() {
    __asm {
        push    13                      // color type
        push    07                      // font parameter
        push    dword ptr[ebp - 0x10]   // meso string pointer
        jmp     dword ptr[dwMesoDropColorRet]
    }
}


// ============================================================================
// Cash Weapon Overlay — any NX weapon cover on any weapon type
// ============================================================================
using IsAbleToStick_t = BOOL(__thiscall*)(void*, int);
static auto pIsAbleToStickWithWeapon = reinterpret_cast<IsAbleToStick_t>(0x0046D39C);

BOOL __fastcall Hook_IsAbleToStickWithWeapon(void* pThis, void* edx, int nItemID) {
    return TRUE;
}


// ============================================================================
// Enable Windows Key — keep Win key enabled while game runs
// ============================================================================
using CWvsApp__EnableWinkey_t = void(__thiscall*)(void*, int);
static auto pEnableWinkey = reinterpret_cast<CWvsApp__EnableWinkey_t>(0x009FEC62);

void __fastcall Hook_EnableWinkey(void* pThis, void* edx, int bEnable) {
    pEnableWinkey(pThis, 1);  // always force enabled
}


// ============================================================================
// Flash Jump Expansion — modified physics + multi-class support
// + vertical Flash Jump variant (skill IDs 1054 / 10001054 / 20001054)
// ============================================================================
const DWORD dwFlashJumpVar = 0x0096BF52;
const DWORD dwFlashJumpRet = 0x0096BF12;

// Default (horizontal) momentum — recomputed from FJ_SPEED / FJ_HEIGHT config.
// Vertical override is hardcoded to MapleRoot's vertical-jump values.
void __cdecl ApplyFlashJumpDefault() {
    constexpr int spd = Config::FJ_SPEED < 1 ? 1 : (Config::FJ_SPEED > 10 ? 10 : Config::FJ_SPEED);
    constexpr int hgt = Config::FJ_HEIGHT < 1 ? 1 : (Config::FJ_HEIGHT > 10 ? 10 : Config::FJ_HEIGHT);
    constexpr int xVel     = -(683 + (spd - 1) * 38);
    constexpr int momentum = -(688 + (spd - 1) * 57);
    constexpr int yVel     = 606 - (hgt - 1) * 50;
    Patch4(0x0096C00A + 1, static_cast<unsigned int>(xVel));
    Patch4(0x0096C021 + 3, static_cast<unsigned int>(yVel));
    Patch4(0x0096C031 + 1, static_cast<unsigned int>(momentum));
}

void __cdecl ApplyFlashJumpVertical() {
    Patch4(0x0096C00A + 1, 0xFFFFFFFFu);  // X = -1 (kill horizontal travel)
    Patch4(0x0096C021 + 3, 0x00000000u);  // Y = 0  (no down-velocity)
    Patch4(0x0096C031 + 1, 0xFFFFFB50u);  // momentum = -1200 (strong upward)
}

// Boot-time call so the bytes start in horizontal mode
inline void ApplyFlashJumpMomentum() { ApplyFlashJumpDefault(); }

__declspec(naked) void FlashJumpAll_CodeCave() {
    __asm {
        cmp     eax, 0xD72A0C       // MapleRoot pattern marker
        je      applyDefault
        cmp     eax, 1050           // Flash Jump (Explorer)
        je      applyDefault
        cmp     eax, 10001050       // Flash Jump (Cygnus)
        je      applyDefault
        cmp     eax, 20001050       // Flash Jump (Aran)
        je      applyDefault
        cmp     eax, 1054           // Vertical Flash Jump (Explorer)
        je      applyOverride
        cmp     eax, 10001054       // Vertical Flash Jump (Cygnus)
        je      applyOverride
        cmp     eax, 20001054       // Vertical Flash Jump (Aran)
        je      applyOverride

        jmp     dword ptr[dwFlashJumpRet]

    applyOverride:
        push    ebp
        mov     ebp, esp
        call    ApplyFlashJumpVertical
        mov     esp, ebp
        pop     ebp
        jmp     dword ptr[dwFlashJumpVar]

    applyDefault:
        push    ebp
        mov     ebp, esp
        call    ApplyFlashJumpDefault
        mov     esp, ebp
        pop     ebp
        jmp     dword ptr[dwFlashJumpVar]
    }
}


// ============================================================================
// Ladder speed — storage for WriteDouble
// ============================================================================
static double s_ladderSpeed = Config::LADDER_SPEED_VALUE;


// ============================================================================
// Mouse Scroll Fix — prevent cursor position jumping on mouse wheel
// ============================================================================
static auto SetCursorVectorPos = reinterpret_cast<void(__cdecl*)(int, int)>(0x0059A0CB);
const DWORD dwMouseScrollFixRet = 0x009E809F;

__declspec(naked) void MouseScrollFix_CodeCave() {
    __asm {
        cmp     eax, 522            // WM_MOUSEWHEEL
        je      skip
        mov     eax, dword ptr[edi]
        shr     eax, 0x10
        push    eax
        movzx   eax, word ptr[edi]
        push    eax
        call    SetCursorVectorPos
    skip:
        jmp     dword ptr[dwMouseScrollFixRet]
    }
}



// ============================================================================
// Hair/Face ID Uncap — proper code caves (MapleRoot port)
// ============================================================================
const DWORD dwHairFaceUncapCapRetn  = 0x005C9505;
const DWORD dwHairFaceUncapFaceRetn = 0x005C95BF;
const DWORD dwHairFaceUncapHairRetn = 0x005C958D;
const DWORD dwHairFaceUncapRetn     = 0x009ACAAD;

__declspec(naked) void HairFaceIdUncap1_cave() {
    __asm {
        cmp     eax, 0x2
        je      face_ret
        cmp     eax, 0x5
        je      face_ret
        cmp     eax, 0x3
        je      hair_ret
        cmp     eax, 0x4
        je      hair_ret
        cmp     eax, 0x6
        je      hair_ret
        jmp     cap_ret
    face_ret:
        jmp     dword ptr[dwHairFaceUncapFaceRetn]
    hair_ret:
        jmp     dword ptr[dwHairFaceUncapHairRetn]
    cap_ret:
        jmp     dword ptr[dwHairFaceUncapCapRetn]
    }
}

__declspec(naked) void HairFaceIdUncap2_cave() {
    __asm {
        cmp     eax, 0x2
        je      face_ret
        cmp     eax, 0x5
        je      face_ret
        cmp     eax, 0x3
        je      hair_ret
        cmp     eax, 0x4
        je      hair_ret
        cmp     eax, 0x6
        je      hair_ret
        jmp     skin_ret
    face_ret:
        mov     eax, 0x0
        mov     ecx, 0x0
        jmp     jmp_ret
    hair_ret:
        mov     eax, 0x1
        mov     ecx, 0x1
        jmp     jmp_ret
    skin_ret:
        mov     eax, 0x2
        mov     ecx, 0x2
    jmp_ret:
        jmp     dword ptr[dwHairFaceUncapRetn]
    }
}


// Hover effect is now in avatar.cpp using the correct kaentake CUser::Update hook

} // namespace


// ============================================================================
// Feature initialization
// ============================================================================
#define LOG_FEATURE(name) ((void)0)  // perf: silenced startup spam

void AttachGameMods() {
    DEBUG_MESSAGE("AttachGameMods");

    // --- Hyper Telerock ---
    if constexpr (Config::TELEROCK) {
       LOG_FEATURE(TELEROCK);
        LOG_FEATURE(TELEROCK);
        ATTACH_HOOK(pWorldMapOnMouseButton, Hook_WorldMapOnMouseButton);
        Patch1(0x009EA030, 0x81);
        Patch1(0x009EA031, 0xFE);
        Patch1(0x009EA032, 0xB7);
    }

    // --- No Gender Lock ---
    if constexpr (Config::NO_GENDER_LOCK) {
        LOG_FEATURE(NO_GENDER_LOCK);
        Patch1(0x00460AED, 0x90);
        Patch1(0x00460AEE, 0x90);
    }

    // --- Move Attack ---
    if constexpr (Config::MOVE_ATTACK) {
        LOG_FEATURE(MOVE_ATTACK);
        Patch1(0x0095F97A, 0xEB);
        Patch1(0x0095F97A + 1, 0x59);
        Patch1(0x009CBFB0, 0xEB);
    }

    // --- No Breath Popup ---
    if constexpr (Config::NO_BREATH_POPUP) {
        LOG_FEATURE(NO_BREATH_POPUP);
        Patch1(0x00452316, 0x7C);
    }

    // --- Remove AP Popup ---
    if constexpr (Config::REMOVE_AP_POPUP) {
        LOG_FEATURE(REMOVE_AP_POPUP);
        Patch1(0x00A20091, 0xEB);
    }

    // --- Swear Filter Removal ---
    if constexpr (Config::SWEAR_FILTER) {
        LOG_FEATURE(SWEAR_FILTER);
        PatchNop(0x007A03C8, 0x007A03CA);
    }

    // --- Super Tubi ---
    if constexpr (Config::SUPER_TUBI) {
        LOG_FEATURE(SUPER_TUBI);
        Patch1(0x00485C01, 0x90); Patch1(0x00485C02, 0x90);
        Patch1(0x00485C21, 0x90); Patch1(0x00485C22, 0x90);
        Patch1(0x00485C32, 0x90); Patch1(0x00485C33, 0x90);
    }

    // --- Cash Trade ---
    if constexpr (Config::CASH_TRADE) {
        LOG_FEATURE(CASH_TRADE);
        PatchNop(0x004F3FB8, 0x004F3FBE);
        PatchNop(0x004F3FC4, 0x004F3FCA);
        PatchNop(0x007C6EF7, 0x007C6EFD);
        PatchNop(0x007C6F03, 0x007C6F09);
    }

    // --- Chat Spam Removal ---
    if constexpr (Config::CHAT_SPAM) {
        LOG_FEATURE(CHAT_SPAM);
        Patch1(0x004905EB, 0xEB);
        Patch1(0x004CAA09, 0xEB);
        Patch1(0x004CAA84, 0xEB);
        Patch1(0x00490607, 0xEB);
        Patch1(0x00490609, 0x27);
        Patch1(0x00490651, 0xEB);
        Patch1(0x00490652, 0x1D);
    }

    // --- Login Spam Bypass ---
    if constexpr (Config::LOGIN_SPAM_BYPASS) {
        LOG_FEATURE(LOGIN_SPAM_BYPASS);
        Patch1(0x00620F2B + 1, 0x1F);
    }

    // --- PIC Modifier ---
    if constexpr (Config::PIC_MODIFIER) {
        LOG_FEATURE(PIC_MODIFIER);
        PatchNop(0x004CA8BA, 0x004CA8BC);
    }

    // ========================================================================
    // NEW FEATURES
    // ========================================================================

    // --- Item ID Tooltip ---
    if constexpr (Config::ITEM_ID_TOOLTIP) {
        LOG_FEATURE(ITEM_ID_TOOLTIP);
        ATTACH_HOOK(pGetItemDesc, Hook_GetItemDesc);
    }

    // --- Meso Drop Color ---
    if constexpr (Config::MESO_DROP_COLOR) {
        LOG_FEATURE(MESO_DROP_COLOR);
        PatchJmp(0x00A20B75, MesoDropColor_CodeCave);
        Patch1(0x00A20B75 + 5, 0x90);  // NOP remaining byte (6 total)
    }

    // --- Damage Cap Removal ---
    if constexpr (Config::UNCAP_DAMAGE_CAP) {
        LOG_FEATURE(UNCAP_DAMAGE_CAP);
        double dmgCap = 999999999.0;
        PatchMemory(TO_PVOID(0x00AFE8A0), &dmgCap, sizeof(dmgCap));
        Patch4(0x008C3304 + 1, 0x7FFFFFFF);  // stat display uncap
    }

    // --- Stat Uncap (STR/DEX/INT/LUK) ---
    // Write at ADDRESS+1 to change the immediate value, NOT the opcode
    if constexpr (Config::UNCAP_STATS) {
        LOG_FEATURE(UNCAP_STATS);
        Patch4(0x00780620 + 1, 999999);  // mov edi, 1999 → 999999
        Patch4(0x0077E055 + 1, 999999);  // mov eax, 1999 → 999999
        Patch4(0x0077E12F + 1, 999999);  // mov eax, 1999 → 999999
        Patch4(0x0077E215 + 1, 999999);  // mov eax, 1999 → 999999
        Patch4(0x0078FF5F + 1, 999999);  // mov ecx, 1999 → 999999
        Patch4(0x0079166C + 1, 999999);  // mov esi, 1999 → 999999
        Patch4(0x00791CD5 + 1, 999999);  // mov ecx, 1999 → 999999
        Patch4(0x007806D0 + 1, 999999);  // mov ecx, 999  → 999999
        Patch4(0x00780702 + 1, 999999);  // mov ecx, 999  → 999999
    }

    // --- Mob Stat Uncap + Accuracy ---
    if constexpr (Config::UNCAP_MOBSTAT) {
        LOG_FEATURE(UNCAP_MOBSTAT);
        Patch4(0x0067DD1D + 1, 999999);
        Patch4(0x00793499 + 1, 999999);
        Patch4(0x00793107 + 1, 999999);
        Patch4(0x007926DD + 1, 999999);
        Patch1(0x007930C5, 0xEB);  // accuracy always hit
        Patch1(0x00793484, 0xEB);
    }

    // --- Cash Weapon Overlay ---
    if constexpr (Config::CASH_WEAPON_OVERLAY) {
        LOG_FEATURE(CASH_WEAPON_OVERLAY);
        ATTACH_HOOK(pIsAbleToStickWithWeapon, Hook_IsAbleToStickWithWeapon);
    }

    // --- Pet Equipment Uncap ---
    if constexpr (Config::PET_EQUIP_UNCAP) {
        LOG_FEATURE(PET_EQUIP_UNCAP);
        Patch1(0x0046D43B, 0xEB);
    }

    // --- Pet Behind Player ---
    if constexpr (Config::PET_BEHIND_PLAYER) {
        LOG_FEATURE(PET_BEHIND_PLAYER);
        Patch1(0x0070451B + 2, 0x05);  // render layer behind character
    }

    // --- Enable Windows Key ---
    if constexpr (Config::ENABLE_WINKEY) {
        LOG_FEATURE(ENABLE_WINKEY);
        ATTACH_HOOK(pEnableWinkey, Hook_EnableWinkey);
    }

    // --- Jump Shoot (archers/thieves in air) ---
    if constexpr (Config::JUMP_SHOOT) {
        LOG_FEATURE(JUMP_SHOOT);
        Patch1(0x009539FA, 0xEB);
    }

    // --- Jump Magic (mages in air) ---
    if constexpr (Config::JUMP_MAGIC) {
        LOG_FEATURE(JUMP_MAGIC);
        Patch1(0x009559E5, 0xEB);
    }

    // --- Jump Attack (melee in air) ---
    if constexpr (Config::JUMP_ATTACK) {
        LOG_FEATURE(JUMP_ATTACK);
        PatchNop(0x0094C3BB, 0x0094C3BB + 6);
    }

    // --- Mid-Air Teleport ---
    if constexpr (Config::MID_AIR_TELEPORT) {
        LOG_FEATURE(MID_AIR_TELEPORT);
        PatchNop(0x00957C2D, 0x00957C2D + 6);
    }

    // --- Battleship Climb ---
    if constexpr (Config::BATTLESHIP_CLIMB) {
        LOG_FEATURE(BATTLESHIP_CLIMB);
        Patch1(0x009CC11F, 0xEB);
    }

    // --- Battleship Faster Mount ---
    if constexpr (Config::BATTLESHIP_FAST_MOUNT) {
        LOG_FEATURE(BATTLESHIP_FAST_MOUNT);
        Patch1(0x00936B2D + 6, 28);
    }

    // --- Flash Jump Expansion ---
    if constexpr (Config::FLASH_JUMP_MOD) {
        LOG_FEATURE(FLASH_JUMP_MOD);
        PatchJmp(0x0096BF0B, FlashJumpAll_CodeCave);
        PatchNop(0x0096C073, 0x0096C073 + 6);
        ApplyFlashJumpMomentum();
    }

    // --- Unlimited Flash Jump (MapleRoot "PROPER FJ" patches) ---
    // Strips the conditional gates that block FJ in air / after-attack / cooldown.
    if constexpr (Config::FLASH_JUMP_UNLIMITED) {
        LOG_FEATURE(FLASH_JUMP_UNLIMITED);
        PatchNop(0x0096BED2, 0x0096BED2 + 2);  // NOP `jne -0x32`
        PatchNop(0x0096BEE2, 0x0096BEE2 + 2);  // NOP `jg  -0x42`
        PatchNop(0x0096BF59, 0x0096BF59 + 6);  // NOP `jne -0xBD` (long form)
        Patch1 (0x0096BF86, 0xEB);             // `je +0xF` → `jmp +0xF` (force unconditional)
        PatchNop(0x0096BFAE, 0x0096BFAE + 6);  // NOP `je  -0x112`
    }

    // --- Remove "Skill Not Ready" Message ---
    if constexpr (Config::REMOVE_SKILL_NOT_READY) {
        LOG_FEATURE(REMOVE_SKILL_NOT_READY);
        PatchNop(0x00967707, 0x00967707 + 12);
    }

    // --- Allow all skill job tiers ---
    // Direct v83 jump used by the original game-mod patch.  This skips the
    // job-tier restriction and resumes at the shared skill path.
    if constexpr (Config::REMOVE_SKILL_JOB_TIER_CHECK) {
        LOG_FEATURE(REMOVE_SKILL_JOB_TIER_CHECK);
        constexpr unsigned char jmpOpcode = 0xE9;
        Patch1(0x008AD01A, jmpOpcode);
        Patch4(0x008AD01A + 1,
            0x008AD227 - (0x008AD01A + 5));
    }

    // --- Speed Cap Modification ---
    if constexpr (Config::SPEED_CAP_MOD) {
        LOG_FEATURE(SPEED_CAP_MOD);
        Patch4(0x00780746, Config::SPEED_CAP_VALUE);
        Patch4(0x008C4287, Config::SPEED_CAP_VALUE);
        Patch4(0x0094D91F, Config::SPEED_CAP_VALUE);
    }

    // --- Ladder Speed ---
    if constexpr (Config::LADDER_SPEED_MOD) {
        LOG_FEATURE(LADDER_SPEED_MOD);
        Patch4(0x009CC6F9 + 2, reinterpret_cast<unsigned int>(&s_ladderSpeed));
    }

    // --- Close Range Removed (no wack on bow/claw) ---
    if constexpr (Config::CLOSE_RANGE_REMOVED) {
        LOG_FEATURE(CLOSE_RANGE_REMOVED);
        Patch1(0x009516C2,     0xE9);
        Patch1(0x009516C2 + 1, 0xC8);
        Patch1(0x009516C2 + 2, 0xFC);
        Patch1(0x009516C2 + 3, 0xFF);
        Patch1(0x009516C2 + 4, 0xFF);
    }

    // --- Mouse Scroll Fix ---
    if constexpr (Config::MOUSE_SCROLL_FIX) {
        LOG_FEATURE(MOUSE_SCROLL_FIX);
        PatchJmp(0x009E8090, MouseScrollFix_CodeCave);
        PatchNop(0x009E8090 + 5, 0x009E809F);
    }

    // ========================================================================
    // LOW-COMPLEXITY BATCH (23 features)
    // ========================================================================

    // --- Custom UI Color ---
    if constexpr (Config::CUSTOM_UI_COLOR) {
        LOG_FEATURE(CUSTOM_UI_COLOR);
        Patch4(0x0098B70C + 1, Config::UI_COLOR_VALUE);
        Patch1(0x008C4944 + 1, 25);   // stat window name/level color index
        Patch1(0x008AA6CB + 1, 25);   // skill strings color index
    }

    // --- No Bulb (quest marker above head) ---
    if constexpr (Config::NO_BULB) {
        LOG_FEATURE(NO_BULB);
        PatchNop(0x00A08D5B, 0x00A08D5B + 5);
    }

    // --- Tooltip Colors (13) ---
    if constexpr (Config::TOOLTIP_COLORS) {
        LOG_FEATURE(TOOLTIP_COLORS);
        Patch4(0x008E6F35 + 1, Config::TOOLTIP_COLOR);  // String
        Patch4(0x008E70C5 + 1, Config::TOOLTIP_COLOR);  // MultiLine
        Patch4(0x008E7317 + 1, Config::TOOLTIP_COLOR);  // String2
        Patch4(0x008E7716 + 1, Config::TOOLTIP_COLOR);  // WorldMap
        Patch4(0x008E7E49 + 1, Config::TOOLTIP_COLOR);  // Ring
        Patch4(0x008E97D2 + 1, Config::TOOLTIP_COLOR);  // Equip
        Patch4(0x008E97AE + 1, Config::TOOLTIP_COLOR);  // Equip (cash item variant, stock 0xA0400000)
        Patch4(0x008EDBCF + 1, Config::TOOLTIP_COLOR);  // Pet
        Patch4(0x008EEEF1 + 1, Config::TOOLTIP_COLOR);  // Bundle
        Patch4(0x008F0460 + 1, Config::TOOLTIP_COLOR);  // Package
        Patch4(0x008F1D6B + 1, Config::TOOLTIP_COLOR);  // SlotInc
        Patch4(0x008F214B + 1, Config::TOOLTIP_COLOR);  // EquipExt
        Patch4(0x008F22BB + 1, Config::TOOLTIP_COLOR);  // MacroSys
        Patch4(0x008F2876 + 1, Config::TOOLTIP_COLOR);  // Skill
    }

    // --- Keyboard Tooltip Fix ---
    if constexpr (Config::KEYBOARD_TOOLTIP_FIX) {
        LOG_FEATURE(KEYBOARD_TOOLTIP_FIX);
        Patch1(0x008339A1 + 2, 0x2C);
    }

    // --- Cash Shop Tooltip Fix ---
    if constexpr (Config::CASH_SHOP_TOOLTIP_FIX) {
        LOG_FEATURE(CASH_SHOP_TOOLTIP_FIX);
        Patch4(0x004B7379 + 3, 0);
    }

    // --- Weapon Multipliers (all point at 0xAFE858) ---
    if constexpr (Config::WEAPON_MULTIPLIERS) {
        LOG_FEATURE(WEAPON_MULTIPLIERS);
        Patch4(0x0078F60A + 2, 0xAFE858);
        Patch4(0x0078F6B0 + 2, 0xAFE858);
        Patch4(0x0078F1A4 + 2, 0xAFE858);
        Patch4(0x0078F24A + 2, 0xAFE858);
        Patch4(0x0078F3FB + 2, 0xAFE858);
        Patch4(0x0078F4A8 + 2, 0xAFE858);
        Patch4(0x0078FE3E + 2, 0xAFE858);
        Patch4(0x0078FABD + 2, 0xAFE858);
        Patch4(0x0078F555 + 2, 0xAFE858);
        Patch4(0x0078FD81 + 2, 0xAFE858);
        Patch4(0x0078FCD4 + 2, 0xAFE858);
        Patch4(0x0078FB6B + 2, 0xAFE858);
        Patch4(0x0078FC2E + 2, 0xAFE858);
        Patch4(0x0078F042 + 2, 0xAFE858);
        Patch4(0x0078F0EF + 2, 0xAFE858);
        Patch4(0x0078EB28 + 2, 0xAFE858);
        Patch4(0x0078EBD5 + 2, 0xAFE858);
        // Stat window defs
        Patch1(0x008C35C9 + 1, 0x2C);
        Patch1(0x008C374A + 1, 0x1A);
        Patch1(0x008C39E9 + 1, 0x62);
        Patch1(0x008C3B9C + 1, 0x50);
        Patch1(0x008C3D4F + 1, 0x3E);
        Patch1(0x008C3F8E + 1, 0x74);
    }

    // --- Boomerang Step in Air (jump to skip ground check) ---
    if constexpr (Config::BOOMERANG_STEP_AIR) {
        LOG_FEATURE(BOOMERANG_STEP_AIR);
        unsigned int rel = 0x00950C53 - (0x00950B4D + 6);
        Patch4(0x00950B4D + 2, rel);
    }

    // --- Boss Damage Uncap (Snipe / Tempest / Heaven's Hammer) ---
    if constexpr (Config::BOSS_DAMAGE_UNCAP) {
        LOG_FEATURE(BOSS_DAMAGE_UNCAP);
        Patch4(0x0078E4D6 + 1, 3222222);    // Snipe calc skip
        Patch4(0x0078E5CE + 1, 3222222);    // Tempest
        Patch4(0x0078E699 + 2, 202200202);  // Heaven's Hammer
        Patch1(0x0078E4B0, 0xEB);
        Patch1(0x0078E4DB, 0xEB);
        Patch1(0x0078E55C, 0xEB);
        Patch1(0x0078E5D3, 0xEB);
        Patch1(0x0078E934, 0xEB);
    }

    // --- Summon DEX*5 damage ---
    if constexpr (Config::SUMMON_DEX_X5) {
        LOG_FEATURE(SUMMON_DEX_X5);
        Patch4(0x00792509 + 2, 0xAFE860);
    }

    // --- Show Mob for Snipe ---
    if constexpr (Config::SHOW_MOB_FOR_SNIPE) {
        LOG_FEATURE(SHOW_MOB_FOR_SNIPE);
        PatchNop(0x00668DDF, 0x00668DDF + 27);
    }

    // --- Combo Smash 10 orbs ---
    if constexpr (Config::COMBO_SMASH_10) {
        LOG_FEATURE(COMBO_SMASH_10);
        Patch1(0x007669B7 + 1, 0x0A);
        Patch1(0x007669B3 + 1, 0x1E);
    }

    // --- Monster Magnet Fix ---
    if constexpr (Config::MONSTER_MAGNET_FIX) {
        LOG_FEATURE(MONSTER_MAGNET_FIX);
        PatchNop(0x0096C554, 0x0096C554 + 4);
    }

    // --- Maker Instant ---
    if constexpr (Config::MAKER_INSTANT) {
        LOG_FEATURE(MAKER_INSTANT);
        Patch1(0x00826F92 + 2, 0x08);
        Patch1(0x00826F92 + 3, 0x01);
        Patch1(0x00826F92 + 4, 0x00);
        Patch1(0x00826F92 + 5, 0x00);
    }

    // --- Remove Nexon Intro ---
    if constexpr (Config::REMOVE_NEXON_INTRO) {
        LOG_FEATURE(REMOVE_NEXON_INTRO);
        PatchNop(0x0062EE54, 0x0062EE54 + 21);
    }

    // --- Remove "Card Full" Message ---
    if constexpr (Config::REMOVE_CARD_FULL) {
        LOG_FEATURE(REMOVE_CARD_FULL);
        PatchNop(0x00A08283, 0x00A08283 + 18);
    }

    // --- Hair/Face ID Uncap (full MapleRoot code caves) ---
    if constexpr (Config::HAIR_ID_FIX) {
        LOG_FEATURE(HAIR_ID_FIX);
        PatchJmp(0x005C94F3, HairFaceIdUncap1_cave);
        PatchNop(0x005C94F3 + 5, 0x005C94F3 + 18);
        PatchJmp(0x009ACA9B, HairFaceIdUncap2_cave);
        PatchNop(0x009ACA9B + 5, 0x009ACA9B + 18);
    }

    // --- More Jobs (>6) ---
    if constexpr (Config::MORE_JOBS) {
        LOG_FEATURE(MORE_JOBS);
        Patch1(0x00BE2C67 + 1, 6);
    }

    // --- Stat Always Show (same NOP as JOB_NAME_CHECK_NOP) ---
    if constexpr (Config::STAT_ALWAYS_SHOW && !Config::JOB_NAME_CHECK_NOP) {
        PatchNop(0x008C5AFC, 0x008C5AFC + 6);
    }

    // --- Instant Final Attack ---
    if constexpr (Config::INSTANT_FA) {
        LOG_FEATURE(INSTANT_FA);
        Patch1(0x0095795E,     0x83);
        Patch1(0x0095795E + 1, 0xC0);
        Patch1(0x0095795E + 2, 0x00);
    }

    // --- DrawLimitedView / HT Circle Uncap ---
    if constexpr (Config::HT_CIRCLE_UNCAP) {
        LOG_FEATURE(HT_CIRCLE_UNCAP);
        Patch1(0x0055BEEC + 2, 0x7F);
        Patch4(0x0055BEE6 + 2, 485);
        Patch1(0x0055C07F + 2, 0x7F);
        Patch4(0x0055C086 + 1, 485);
        Patch1(0x0055C1C5 + 2, 0x7F);
        Patch4(0x0055C1CD + 1, 485);
    }

    // --- Cash Effect ID Expansion (6 patches) ---
    // Extends the engine's hardcoded item-ID gating so item effects render
    // for IDs outside the original cash range. Without these, only items
    // matching the engine's narrow ID checks (1103xxx for capes, etc.) get
    // their Effect/ItemEff.img/<id>/effect/* rendered. With these, IDs
    // dividing to 501 (1102xxx, 5010xxx, etc.) also pass.
    if constexpr (Config::CASH_EFFECT_ID_EXPAND) {
        Patch4(0x0093C144 + 1, 0x2710);   // mov ecx, 10000  (was 1000)
        Patch4(0x0093C14F + 1, 0x1F5);    // cmp eax, 501
        Patch4(0x0093C67E + 1, 0x2710);   // mov ecx, 10000
        Patch4(0x0093C689 + 1, 0x1F5);    // cmp eax, 501
        Patch4(0x0095B112 + 1, 0x2710);   // mov ecx, 10000
        Patch4(0x0095B11F + 1, 0x1F5);    // cmp eax, 501
    }

    // --- Super Beginner Wears Anything ---
    if constexpr (Config::SUPER_BEGINNER_EQUIP) {
        LOG_FEATURE(SUPER_BEGINNER_EQUIP);
        Patch1(0x004F2D9B + 2, 0x07);
    }

    // --- Chain Lightning Bonus (1.25x per mob) ---
    if constexpr (Config::CHAIN_LIGHTNING_BONUS) {
        LOG_FEATURE(CHAIN_LIGHTNING_BONUS);
        Patch4(0x0075BF65 + 3, 0x00B3D108);
    }

    // Hover effect is handled in avatar.cpp (uses existing CUser::Update hook)
}
