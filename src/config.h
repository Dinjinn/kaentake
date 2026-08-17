#pragma once

// Compile-time feature toggles — ALL ENABLED

namespace Config {
// --- Existing toggles ---
constexpr bool TELEROCK = true;
// Custom action registry, table patches, conversion hooks, and property injection.
constexpr bool CUSTOM_ACTIONS = true;

// Restored to "5 hours ago" config when item effects worked.
// CustomActions reads from the explicitly mounted `Custom/` namespace and does
// not require native WZ paths to fall back into Custom.wz.  Keep the legacy
// NAMESPACE.DLL fallback disabled: it participates in failed Mob magic-resource
// lookups and can leave the returned VARIANT in an invalid ownership state.
constexpr bool CUSTOM_PROPERTY_MERGE = false;
// CWzCanvas::raw_Serialize is also used when a Mob attack's `magic` hit/effect
// canvas is materialized. Mutating that canvas property while NAMESPACE.DLL is
// still deserializing it can leave a VARIANT with invalid ownership and crash in
// OLEAUT32!VariantClear when the magic attack actually hits CUserLocal.
// Custom-action injection uses the separate CWzProperty serializer hook and does
// not depend on this legacy _inlink rewrite.
constexpr bool INLINK_CANVAS_SERIALIZE = false;
// These hooks intercept every WZ VARIANT-to-IUnknown conversion, including
// Mob attack/hit canvases. Canvas serialization already resolves _inlink;
// keep the process-wide conversion hooks off to preserve native Mob magic.
constexpr bool INLINK_GET_UNKNOWN_HOOKS = false;

// Diagnostic logger — OFF for perf. Each call did fopen/fprintf/fclose
// per PCOM Serialize, hundreds per second in heavy GFX. Flip to true only
// when actively diagnosing a CANVAS/ZLZ crash.
constexpr bool LOG_SERIALIZE_PATHS = true;

// MapleRoot magic-damage formula + stat-display rewrite. Hooks
// ztlSecureFuse and bstr_t which fire many times per second — turn off
// if you observe a perf regression in busy maps.
constexpr bool DAMAGE_FORMULA = true;

constexpr bool NO_GENDER_LOCK = true;
constexpr bool MOVE_ATTACK = true;
constexpr bool NO_BREATH_POPUP = true;
constexpr bool REMOVE_AP_POPUP = true;
constexpr bool SWEAR_FILTER = true;
constexpr bool SUPER_TUBI = true;
constexpr bool CASH_TRADE = true;
constexpr bool CHAT_SPAM = true;
constexpr bool LOGIN_SPAM_BYPASS = true;
constexpr bool PIC_MODIFIER = true;
constexpr bool RAINBOW_PLAYER_NAMES = false;

// --- New toggles ---
constexpr bool ITEM_ID_TOOLTIP = true;
constexpr bool MESO_DROP_COLOR = true;
constexpr bool UNCAP_DAMAGE_CAP = true;
constexpr bool UNCAP_STATS = true;
constexpr bool UNCAP_MOBSTAT = true;
constexpr bool CASH_WEAPON_OVERLAY = true;
constexpr bool PET_EQUIP_UNCAP = true;
constexpr bool PET_BEHIND_PLAYER = true;
constexpr bool ENABLE_WINKEY = true;
constexpr bool JUMP_SHOOT = true;
constexpr bool JUMP_MAGIC = true;
constexpr bool JUMP_ATTACK = true;
constexpr bool MID_AIR_TELEPORT = true;
constexpr bool BATTLESHIP_CLIMB = true;
constexpr bool BATTLESHIP_FAST_MOUNT = true;
constexpr bool BATTLESHIP_SKILLS = true;
constexpr bool FLASH_JUMP_MOD = true;
constexpr bool FLASH_JUMP_UNLIMITED = true;
constexpr bool REMOVE_SKILL_NOT_READY = true;
constexpr bool SPEED_CAP_MOD = true;
constexpr bool LADDER_SPEED_MOD = true;
constexpr bool CLOSE_RANGE_REMOVED = true;
constexpr bool MOUSE_SCROLL_FIX = true;

// --- Expanded UI modules ---
constexpr bool QUICKSLOT_EXPANDED = true;
constexpr bool STORAGE_EXPANDED = true;
constexpr bool VENDOR_EXPANDED = true;
constexpr bool SKILL_WINDOW_EXPANDED = true;
constexpr bool SHOULDER_SLOT = true;
constexpr bool SHOULDER_HOOKS = true;
constexpr bool SHOULDER_PATCH_115 = true;
constexpr bool SHOULDER_PATCH_EMBLEM1 = true;
constexpr bool SHOULDER_PATCH_EMBLEM2 = true;
constexpr bool SHOULDER_PATCH_ITER_A = true;
constexpr bool SHOULDER_PATCH_ITER_B = true;
constexpr bool SHOULDER_PATCH_ITER_C = true;
constexpr bool SHOULDER_PATCH_ITER_D = true;

// (removed GFX_TOGGLE / SKILL_TRANSPARENCY / CHAIR_TRANSPARENCY — the alpha
// override hook was making big cash effects invisible. GFX cache flush in
// gfx.cpp is unconditional and handled separately.)

// --- Skill code caves ---
constexpr bool SHADOW_PARTNER_FIX = true;
constexpr bool FIRE_ARROW_MULTI = true;
constexpr bool PIERCE_ARROW_NO_CHARGE = true;
constexpr bool CORKSCREW_BLOW = true;
constexpr bool RECOIL_SHOT = true;
constexpr bool ASSASSINATE_DARK_SIGHT = true;

// --- Custom Active Skill System ---
// Master switch for custom active/passive skill routing and UI hooks.
constexpr bool CUSTOM_SKILLS = true;
// Allow active skills from all v83 job tiers, including first/second/third
// job IDs.  The client otherwise rejects tier digits 0 and 9 in
// CSkillInfo::CheckConsumeForActiveSkill before reading skill data.
constexpr bool REMOVE_SKILL_JOB_TIER_CHECK = true;

// --- MapleRoot Extras ---
constexpr bool MAPLEROOT_EXTRAS = true;
constexpr bool MR_BARE_HAND = true;
constexpr bool MR_NOVICE_SP_BLOCK = true;
constexpr bool MR_BLOCK_MONSTER_BOOK = true; // user explicitly off
constexpr bool MR_IS_ATTACK_AREA = true;
constexpr bool MR_RECOIL_NO_CD = true;
constexpr bool MR_REMOVE_BULLET = true;
constexpr bool MR_LTRB_EVAL = true;
constexpr bool MR_CRIT_ALL_CLASSES = true;
constexpr bool MR_DCRITS = true;
constexpr bool MR_NW_MULTI = true;
constexpr bool MR_CLAW_5 = true;
constexpr bool MR_CRIT_PATCHES = true;
constexpr bool MR_DRAW_WEAPON_SPEED = true;
constexpr bool MR_NOP_668C04 = true;
constexpr bool MR_WEAPON_STAT_REFS = true;

// --- UI mods ---
constexpr bool STAT_WINDOW_EXPANDED = true;
// Cave at 0x008C5112 shifts X by +53 for all callers of that function —
// on this client it breaks skill icon placement and other UIs that share
// the code path. Keep byte patches, disable the cave.
constexpr bool STAT_WINDOW_X_SHIFT_CAVE = true;
constexpr bool IGNORE_LIST_EXPANDED = true;
constexpr bool CUSTOM_JOB_NAMES = true;
constexpr bool JOB_NAME_CHECK_NOP = true;
constexpr bool POS_HOOKS = true;

// --- Low-complexity batch ---
constexpr bool CUSTOM_UI_COLOR = true;
constexpr unsigned int UI_COLOR_VALUE = 0xFF000000;
constexpr bool NO_BULB = true;
constexpr bool TOOLTIP_COLORS = true;
constexpr unsigned int TOOLTIP_COLOR = 0xE6000000; // near-opaque black (was 0xBB204491 navy)
constexpr bool KEYBOARD_TOOLTIP_FIX = true;
constexpr bool CASH_SHOP_TOOLTIP_FIX = true;
constexpr bool WEAPON_MULTIPLIERS = true;
constexpr bool BOOMERANG_STEP_AIR = true;
constexpr bool BOSS_DAMAGE_UNCAP = true;
constexpr bool SUMMON_DEX_X5 = true;
constexpr bool SHOW_MOB_FOR_SNIPE = true;
constexpr bool COMBO_SMASH_10 = true;
constexpr bool MONSTER_MAGNET_FIX = true;
constexpr bool MAKER_INSTANT = true;
constexpr bool REMOVE_NEXON_INTRO = true;
constexpr bool REMOVE_CARD_FULL = true;
constexpr bool HAIR_ID_FIX = true;
constexpr bool MORE_JOBS = true;
constexpr bool STAT_ALWAYS_SHOW = true;
constexpr bool INSTANT_FA = true;
constexpr bool HT_CIRCLE_UNCAP = true;
constexpr bool CASH_EFFECT_ID_EXPAND = true;
constexpr bool SUPER_BEGINNER_EQUIP = true;
constexpr bool CHAIN_LIGHTNING_BONUS = true;

// --- Asset override system ---
constexpr bool DATA_FOLDER_LOADING = true;

// --- Configurable values ---
constexpr int SPEED_CAP_VALUE = 600;
constexpr double LADDER_SPEED_VALUE = 10.0;
constexpr int FJ_SPEED = 4;
constexpr int FJ_HEIGHT = 1;

// Hover effect
constexpr bool HOVER_EFFECT = true;
constexpr double HOVER_AMPLITUDE = 3.0;
constexpr int HOVER_BASE_LIFT = 5;
constexpr int HOVER_SPEED_MS = 2000;
} // namespace Config
