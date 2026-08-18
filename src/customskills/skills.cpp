#include "../pch.h"
#include "../config.h"
#include "../hook.h"
#include "../ztl/ztl.h"
#include "../wvs/util.h"
#include "skills.h"

#include <array>
#include <unordered_map>

namespace CustomSkills {
namespace {

enum class SkillKind {
    Buff,
    Melee,
    Magic,
    Shoot,
    Teleport,
    Summon,
    BoundJump,
    Flying,
    Passive,
};

struct SkillDefinition {
    int id;
    // Selects the native execution/damage family. The character's current
    // job and equipped weapon still select the actual BasicStat formula;
    // this value must describe what the skill does, not which job owns it.
    SkillKind kind;
};

// Add generic WZ-driven skills here. Routing to the matching native family
// preserves v83's job/weapon BasicStat formula and PDamage/MDamage setup.
// Skills with bespoke native mechanics (charges, combo consumption,
// transformations, special projectiles, etc.) need a dedicated handler;
// assigning their broad family alone does not reproduce those mechanics.
constexpr std::array<SkillDefinition, 2> kSkillDefinitions{{
    {4221100, SkillKind::Melee},
    {4221101, SkillKind::Passive},
}};

// Passive skills are registered in the same table as active skills.  Their
// level and SKILLENTRY pointer come from CharacterData, exactly like native
// v83 skills; SKILLENTRY::GetLevelData then reads the matching Skill.wz level
// node (hp/mp/pad/mad/pdd/mdd/critical/etc.).

constexpr uintptr_t kActiveSkillSplice = 0x00967923;
constexpr uintptr_t kActiveSkillReturn = 0x0096792A;
constexpr uintptr_t kBuffReturn = 0x00969284;
constexpr uintptr_t kMeleeReturn = 0x009690AE;
constexpr uintptr_t kMagicReturn = 0x0096928B;
constexpr uintptr_t kShootReturn = 0x009690E9;
constexpr uintptr_t kTeleportReturn = 0x00969146;
constexpr uintptr_t kSummonReturn = 0x009689DF;
constexpr uintptr_t kBoundReturn = 0x0096897A;
constexpr uintptr_t kFlyingReturn = 0x009683ED;

const SkillDefinition* FindSkill(int skillId) {
    for (const auto& skill : kSkillDefinitions) {
        if (skill.id == skillId) {
            return &skill;
        }
    }
    return nullptr;
}

const SkillDefinition* FindPassiveSkill(int skillId) {
    for (const auto& skill : kSkillDefinitions) {
        if (skill.id == skillId && skill.kind == SkillKind::Passive) {
            return &skill;
        }
    }
    return nullptr;
}

bool HasPassiveSkills() {
    for (const auto& skill : kSkillDefinitions) {
        if (skill.kind == SkillKind::Passive) {
            return true;
        }
    }
    return false;
}

uintptr_t ActiveSkillReturn(int skillId) {
    const SkillDefinition* skill = FindSkill(skillId);
    if (!skill) {
        return kActiveSkillReturn;
    }

    switch (skill->kind) {
    case SkillKind::Buff:
        return kBuffReturn;
    case SkillKind::Melee:
        return kMeleeReturn;
    case SkillKind::Magic:
        return kMagicReturn;
    case SkillKind::Shoot:
        return kShootReturn;
    case SkillKind::Teleport:
        return kTeleportReturn;
    case SkillKind::Summon:
        return kSummonReturn;
    case SkillKind::BoundJump:
        return kBoundReturn;
    case SkillKind::Flying:
        return kFlyingReturn;
    case SkillKind::Passive:
        return kActiveSkillReturn;
    }
    return kActiveSkillReturn;
}

extern "C" uintptr_t __cdecl ResolveActiveSkillReturn(int skillId) {
    return ActiveSkillReturn(skillId);
}

// v83 enters this splice with the skill ID in ESI.  Save the complete CPU
// state while resolving the table so the native branch receives the original
// registers.  EAX carries the selected continuation, as in the original
// code-cave implementation.
__declspec(naked) void ActiveSkillSplice() {
    __asm {
        pushfd
        pushad
        push esi
        call ResolveActiveSkillReturn
        add esp, 4
        mov [esp + 28], eax
        popad
        popfd

        // The original v83 code compares ESI with 5111005 immediately
        // before the continuation at 0x0096792A.  Recreate that comparison
        // for unregistered skills; the native branches do not need it.
        cmp eax, kActiveSkillReturn
        jne selected_branch
        mov eax, 5111005
        cmp esi, eax
        mov ecx, kActiveSkillReturn
        jmp ecx

    selected_branch:
        jmp eax
    }
}

using GetSkillLevelFn = int(__thiscall*)(void*, const void*, int, void**);
GetSkillLevelFn g_getSkillLevel = reinterpret_cast<GetSkillLevelFn>(
    0x007616F6);

using GetLevelDataFn = void*(__thiscall*)(void*, int);
GetLevelDataFn g_getLevelData = reinterpret_cast<GetLevelDataFn>(
    0x00760F23);

using SecureFuseLongFn = int(__cdecl*)(const int*, unsigned int);
SecureFuseLongFn g_secureFuseLong = reinterpret_cast<SecureFuseLongFn>(
    0x00416563);

// v83's _ZtlSecureTear<long> receives the plain value in ECX and the secure
// field destination in EDX (see 0x004165B1). It is a register-only fastcall,
// not cdecl; pushing these arguments corrupts whichever address happens to
// be left in EDX.
using SecureTearLongFn = int(__fastcall*)(int, int*);
SecureTearLongFn g_secureTearLong = reinterpret_cast<SecureTearLongFn>(
    0x004165B1);

using DraggableSkillDoubleClickFn = int(__thiscall*)(void*);
DraggableSkillDoubleClickFn g_draggableSkillDoubleClick =
    reinterpret_cast<DraggableSkillDoubleClickFn>(0x004FB001);

using DraggableSkillDroppedFn = int(__thiscall*)(void*, void*, void*, int, int);
DraggableSkillDroppedFn g_draggableSkillDropped =
    reinterpret_cast<DraggableSkillDroppedFn>(0x004FAA22);

using SkillWindowMouseFn = long(__thiscall*)(void*, unsigned int, unsigned int,
    int, int);
SkillWindowMouseFn g_skillWindowMouse = reinterpret_cast<SkillWindowMouseFn>(
    0x008ABA6B);

using SkillHitTestFn = int(__thiscall*)(void*, int, int, int);
SkillHitTestFn g_skillHitTest = reinterpret_cast<SkillHitTestFn>(0x008AD946);

using VisibleSkillRootFn = void*(__thiscall*)(void*, int);
VisibleSkillRootFn g_visibleSkillRoot =
    reinterpret_cast<VisibleSkillRootFn>(0x008ADA59);

// Offsets are the secure-long value members loaded by v83's
// SKILLLEVELDATA::Load (sub_75F464). Each value is followed by a random
// value and its key, so every field occupies 12 bytes.
enum class LevelNode : int {
    Str = -1,
    Dex = -2,
    Int = -3,
    Luk = -4,
    Hp = 0x04,
    Mp = 0x10,
    Pad = 0x1C,
    Pdd = 0x28,
    Mad = 0x34,
    Mdd = 0x40,
    Acc = 0x4C,
    Eva = 0x58,
    Craft = 0x64,
    Speed = 0x70,
    Jump = 0x7C,
    Damage = 0xD0,
    Mastery = 0x124,
    CriticalDamage = 0x1B0,
    Critical = -5,
};

struct PassiveNodeValues {
    int str = 0;
    int dex = 0;
    int intStat = 0;
    int luk = 0;
    int hp = 0;
    int mp = 0;
    int pad = 0;
    int pdd = 0;
    int mad = 0;
    int mdd = 0;
    int acc = 0;
    int eva = 0;
    int craft = 0;
    int speed = 0;
    int jump = 0;
    int damage = 0;
    int mastery = 0;
    int critical = 0;
    int criticalDamage = 0;
};

std::unordered_map<unsigned long long, PassiveNodeValues> g_passiveNodeCache;

int GetWzInt(IWzPropertyPtr property, const wchar_t* name) {
    if (!property) {
        return 0;
    }
    try {
        return ZtlVariant(property->item[name]).get_int32();
    } catch (...) {
        return 0;
    }
}

const PassiveNodeValues& GetPassiveNodeValues(int skillId, int level) {
    const auto key = (static_cast<unsigned long long>(
                          static_cast<unsigned int>(skillId))
                         << 32) |
        static_cast<unsigned int>(level);
    const auto found = g_passiveNodeCache.find(key);
    if (found != g_passiveNodeCache.end()) {
        return found->second;
    }

    PassiveNodeValues values;
    try {
        wchar_t path[128] = {};
        swprintf_s(path, L"Skill/%03d.img/skill/%d/level/%d",
            skillId / 10000, skillId, level);
        IWzPropertyPtr property = get_rm()->GetObjectA(path).GetUnknown();
        values.str = GetWzInt(property, L"str");
        values.dex = GetWzInt(property, L"dex");
        values.intStat = GetWzInt(property, L"int");
        values.luk = GetWzInt(property, L"luk");
        values.hp = GetWzInt(property, L"hp");
        values.mp = GetWzInt(property, L"mp");
        values.pad = GetWzInt(property, L"pad");
        values.pdd = GetWzInt(property, L"pdd");
        values.mad = GetWzInt(property, L"mad");
        values.mdd = GetWzInt(property, L"mdd");
        values.acc = GetWzInt(property, L"acc");
        values.eva = GetWzInt(property, L"eva");
        values.craft = GetWzInt(property, L"craft");
        values.speed = GetWzInt(property, L"speed");
        values.jump = GetWzInt(property, L"jump");
        values.damage = GetWzInt(property, L"damage");
        values.mastery = GetWzInt(property, L"mastery");
        values.critical = GetWzInt(property, L"critical");
        values.criticalDamage = GetWzInt(property, L"criticalDamage");
    } catch (...) {
    }
    return g_passiveNodeCache.emplace(key, values).first->second;
}

int SelectNode(const PassiveNodeValues& values, LevelNode node) {
    switch (node) {
    case LevelNode::Str: return values.str;
    case LevelNode::Dex: return values.dex;
    case LevelNode::Int: return values.intStat;
    case LevelNode::Luk: return values.luk;
    case LevelNode::Hp: return values.hp;
    case LevelNode::Mp: return values.mp;
    case LevelNode::Pad: return values.pad;
    case LevelNode::Pdd: return values.pdd;
    case LevelNode::Mad: return values.mad;
    case LevelNode::Mdd: return values.mdd;
    case LevelNode::Acc: return values.acc;
    case LevelNode::Eva: return values.eva;
    case LevelNode::Craft: return values.craft;
    case LevelNode::Speed: return values.speed;
    case LevelNode::Jump: return values.jump;
    case LevelNode::Damage: return values.damage;
    case LevelNode::Mastery: return values.mastery;
    case LevelNode::Critical: return values.critical;
    case LevelNode::CriticalDamage: return values.criticalDamage;
    }
    return 0;
}

struct PassiveStatField {
    LevelNode node;
    int destinationOffset;
};

// BasicStat::Set (sub_77F4C9) stores PAD/PDD/MAD/MDD/ACC/EVA/Craft/
// Speed/Jump in this order. Each BasicStat secure long occupies 0x30 bytes,
// even though the encoded value and key used by _ZtlSecureFuse are at +0
// and +8. Using 12-byte spacing corrupts the first field's random storage.
constexpr std::array<PassiveStatField, 8> kBasicStatFields{{
    {LevelNode::Pad, 0},
    {LevelNode::Pdd, 0x30},
    {LevelNode::Mad, 0x60},
    {LevelNode::Mdd, 0x90},
    {LevelNode::Acc, 0xC0},
    {LevelNode::Eva, 0xF0},
    {LevelNode::Craft, 0x120},
    {LevelNode::Jump, 0x180},
}};

int g_passiveSpeedBonus = 0;

using GetSpeedFn = int(__thiscall*)(void*);
GetSpeedFn g_getSpeed = reinterpret_cast<GetSpeedFn>(0x008C457C);

using BasicStatSetFn = int(__thiscall*)(void*, const void*, void*, void*,
    int, int, int);
BasicStatSetFn g_basicStatSet = reinterpret_cast<BasicStatSetFn>(
    0x0077F4C9);

// sub_77EC9F builds the primary character-stat cache. IDA maps its secure
// max-HP and max-MP values to byte offsets 96 and 108 respectively.
using CharacterStatSetFn = int(__thiscall*)(void*, const void*, void*, void*,
    int, int, int, int, int);
CharacterStatSetFn g_characterStatSet = reinterpret_cast<CharacterStatSetFn>(
    0x0077EC9F);

// A __thiscall target must be detoured by an x86 __fastcall function: ECX
// carries `this`, EDX is the unused compiler shim, and the remaining
// arguments stay on the stack.  Omitting the shim shifts CharacterData and
// makes the native ZMap lookup use an invalid object.
int ResolveSkillLevel(const void* characterData, int skillId,
    void** skillEntry) {
    void* skillInfo = *reinterpret_cast<void**>(0x00BE78DC);
    if (!skillInfo) {
        if (skillEntry) {
            *skillEntry = nullptr;
        }
        return 0;
    }

    const int nativeLevel = g_getSkillLevel(
        skillInfo, characterData, skillId, skillEntry);
    return nativeLevel;
}

int ReadPassiveLevelNode(const void* characterData, LevelNode node) {
    int result = 0;

    for (const auto& skill : kSkillDefinitions) {
        if (skill.kind != SkillKind::Passive) {
            continue;
        }

        void* skillEntry = nullptr;
        const int level = ResolveSkillLevel(
            characterData, skill.id, &skillEntry);
        if (level <= 0 || !skillEntry) {
            continue;
        }

        result += SelectNode(GetPassiveNodeValues(skill.id, level), node);
    }

    return result;
}

void AddSecureLong(void* object, int offset, int amount) {
    if (!amount) {
        return;
    }

    auto* value = reinterpret_cast<int*>(
        reinterpret_cast<unsigned char*>(object) + offset);
    const int decoded = g_secureFuseLong(value, *(value + 2));
    // _ZtlSecureTear writes the two encoded values and returns their new key.
    // Native callers always save that return value in the third DWORD.
    value[2] = g_secureTearLong(decoded + amount, value);
}

int __fastcall DraggableSkillDoubleClickHook(void* skill, void* /*edx*/) {
    const int skillId = reinterpret_cast<int*>(skill)[6];
    if (FindPassiveSkill(skillId)) {
        return 1;
    }
    return g_draggableSkillDoubleClick(skill);
}

int __fastcall DraggableSkillDroppedHook(void* skill, void* /*edx*/,
    void* source, void* destination, int x, int y) {
    const int skillId = reinterpret_cast<int*>(skill)[6];
    if (FindPassiveSkill(skillId)) {
        return 0;
    }
    return g_draggableSkillDropped(skill, source, destination, x, y);
}

long __fastcall SkillWindowMouseHook(void* messageHandler, void* /*edx*/,
    unsigned int message, unsigned int wParam, int x, int y) {
    // CUISkill's IUIMsgHandler subobject is four bytes into the complete
    // object. Resolve the same visible slot and SKILLENTRY used by the
    // native 0x008ABA6B drag-start branch, and stop before it allocates a
    // CDraggableSkill for a registered passive.
    if (message == WM_LBUTTONDOWN) {
        auto* skillWindow = reinterpret_cast<unsigned char*>(messageHandler) - 4;
        const int index = g_skillHitTest(skillWindow, x, y, 1);
        if (index >= 0) {
            auto* root = reinterpret_cast<unsigned char*>(
                g_visibleSkillRoot(skillWindow, 0));
            if (root) {
                auto* entries = *reinterpret_cast<unsigned char***>(root + 8);
                if (entries) {
                    void* entry = entries[index * 2 + 1];
                    if (entry && FindPassiveSkill(*reinterpret_cast<int*>(entry))) {
                        return 0;
                    }
                }
            }
        }
    }
    return g_skillWindowMouse(messageHandler, message, wParam, x, y);
}

int __fastcall BasicStatSetHook(void* basicStat, void* /*edx*/,
    const void* characterData, void* basicStatData, void* secondaryStatData,
    int equipA, int equipB, int equipC) {
    const int result = g_basicStatSet(basicStat, characterData,
        basicStatData, secondaryStatData, equipA, equipB, equipC);

    if (!HasPassiveSkills()) {
        return result;
    }

    for (const auto& field : kBasicStatFields) {
        AddSecureLong(basicStat, field.destinationOffset,
            ReadPassiveLevelNode(characterData, field.node));
    }
    g_passiveSpeedBonus =
        ReadPassiveLevelNode(characterData, LevelNode::Speed);
    return result;
}

int __fastcall GetSpeedHook(void* secondaryStat, void* /*edx*/) {
    // Speed displayed by CUIStatDetail and consumed by CUser movement goes
    // through SecondaryStat::GetSpeed, not directly through BasicStat's
    // equipment-speed field. Keep the learned passive contribution here so
    // every native consumer observes it.
    return g_getSpeed(secondaryStat) + g_passiveSpeedBonus;
}

int __fastcall CharacterStatSetHook(void* characterStat, void* /*edx*/,
    const void* characterData, void* forcedStat, void* equipsA, int equipsB,
    int equipsC, int hpRate, int mpRate, int statRate) {
    const int result = g_characterStatSet(characterStat, characterData,
        forcedStat, equipsA, equipsB, equipsC, hpRate, mpRate, statRate);

    AddSecureLong(characterStat, 36,
        ReadPassiveLevelNode(characterData, LevelNode::Str));
    AddSecureLong(characterStat, 48,
        ReadPassiveLevelNode(characterData, LevelNode::Dex));
    AddSecureLong(characterStat, 60,
        ReadPassiveLevelNode(characterData, LevelNode::Int));
    AddSecureLong(characterStat, 72,
        ReadPassiveLevelNode(characterData, LevelNode::Luk));
    AddSecureLong(characterStat, 96,
        ReadPassiveLevelNode(characterData, LevelNode::Hp));
    AddSecureLong(characterStat, 108,
        ReadPassiveLevelNode(characterData, LevelNode::Mp));
    return result;
}

} // namespace
} // namespace CustomSkills

void AttachCustomSkills() {
    if constexpr (!Config::CUSTOM_SKILLS)
        return;

    PatchJmp(0x00967923, &CustomSkills::ActiveSkillSplice);

    if (CustomSkills::HasPassiveSkills()) {
        AttachHook(reinterpret_cast<void**>(&CustomSkills::g_characterStatSet),
            CastHook(&CustomSkills::CharacterStatSetHook));
        AttachHook(reinterpret_cast<void**>(&CustomSkills::g_getSpeed),
            CastHook(&CustomSkills::GetSpeedHook));
    }

    AttachHook(reinterpret_cast<void**>(&CustomSkills::g_draggableSkillDoubleClick),
        CastHook(&CustomSkills::DraggableSkillDoubleClickHook));
    AttachHook(reinterpret_cast<void**>(&CustomSkills::g_draggableSkillDropped),
        CastHook(&CustomSkills::DraggableSkillDroppedHook));
    AttachHook(reinterpret_cast<void**>(&CustomSkills::g_skillWindowMouse),
        CastHook(&CustomSkills::SkillWindowMouseHook));
    AttachHook(reinterpret_cast<void**>(&CustomSkills::g_basicStatSet),
        CastHook(&CustomSkills::BasicStatSetHook));
}
