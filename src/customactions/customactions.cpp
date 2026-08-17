#include "../pch.h"
#include "customactions.h"
#include "../config.h"
#include "../debug.h"
#include "../hook.h"
#include "../wvs/avatar.h"
#include "../wvs/util.h"
#include "../ztl/ztl.h"
#include <array>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace CustomActions {
namespace {

constexpr uintptr_t kStockTable = 0x00BEC620;
constexpr size_t kRecordSize = 0x18;
constexpr uintptr_t kInitCountInsn = 0x004073A2;
constexpr uintptr_t kItemActionCacheCountInsn = 0x0040ACDA;
constexpr uintptr_t kItemActionCacheBytesInsn = 0x0040ACFE;

using GetName_t = Ztl_bstr_t*(__cdecl*)(Ztl_bstr_t*, int);
using GetCode_t = int(__cdecl*)(Ztl_bstr_t);
using MoveToAction_t = int(__thiscall*)(CAvatar*, int, int*);
using SetMove_t = void(__thiscall*)(CAvatar*, int, int);
using SetAction_t = void(__thiscall*)(CAvatar*, int);

GetName_t g_getName = reinterpret_cast<GetName_t>(0x004A8CE6);
GetCode_t g_getCode = reinterpret_cast<GetCode_t>(0x004A8D14);
MoveToAction_t g_moveToAction = reinterpret_cast<MoveToAction_t>(0x00451EC8);
SetMove_t g_setMove = reinterpret_cast<SetMove_t>(0x004520F1);
SetAction_t g_setAction = reinterpret_cast<SetAction_t>(0x004571AB);

struct Action {
    int code = -1;
    int templateCode = -1;
    int movementSet = 0;
    int baseMoveAction = 0;
    int moveAction = 0;
    bool movement = false;
    std::wstring name;
    std::wstring lowerName;
    IWzPropertyPtr frames;
};

struct MovementSet {
    std::wstring name;
    std::map<int, int> byBaseAction;
};

alignas(8) unsigned char g_table[MaxActionCount * kRecordSize];
std::array<Action*, MaxActionCount> g_byCode{};
std::array<int, 128> g_byMoveAction{};
std::map<std::wstring, int> g_byName;
std::map<int, MovementSet> g_movementSets;
std::vector<Action> g_actions;
bool g_enabled = false;
bool g_loaded = false;
bool g_attached = false;
std::wstring g_validationError;

// Every non-constructor operand in the v83 executable that indexes ACTIONDATA.
constexpr uintptr_t kTableInstructions[] = {
    0x00406C12,
    0x00406C19,
    0x00406F80,
    0x004108FA,
    0x004123EB,
    0x00412956,
    0x0045283F,
    0x00454557,
    0x006434A7,
    0x0095176B,
    0x009542A6,
    0x00955C10,
    0x00957676,
    0x0096A44A,
    0x0096B668,
    0x0096D536,
    0x00978D13,
    0x00978EC2,
    0x009807CF,
    0x00980DDF,
};

std::wstring Lower(const wchar_t* text) {
    std::wstring result = text ? text : L"";
    std::transform(result.begin(), result.end(), result.begin(), towlower);
    return result;
}

IWzPropertyPtr AsProperty(const Ztl_variant_t& value) {
    IWzPropertyPtr property;
    IUnknownPtr unknown = value.GetUnknown();
    if (unknown)
        unknown->QueryInterface(&property);
    return property;
}

IWzPropertyPtr Child(IWzProperty* parent, const wchar_t* name) {
    if (!parent || !name)
        return nullptr;
    try {
        return AsProperty(parent->item[name]);
    } catch (...) {
        return nullptr;
    }
}

std::wstring StringValue(IWzProperty* parent, const wchar_t* name) {
    if (!parent)
        return {};
    try {
        Ztl_variant_t value = parent->item[name], converted;
        if (V_VT(&value) == VT_EMPTY || V_VT(&value) == VT_ERROR ||
                FAILED(ZComAPI::ZComVariantChangeType(&converted, &value, 0, VT_BSTR)))
            return {};
        return V_BSTR(&converted) ? V_BSTR(&converted) : L"";
    } catch (...) {
        return {};
    }
}

int IntValue(IWzProperty* parent, const wchar_t* name, int fallback) {
    if (!parent)
        return fallback;
    try {
        Ztl_variant_t value = parent->item[name];
        return get_int32(value, fallback);
    } catch (...) {
        return fallback;
    }
}

bool HasChild(IWzProperty* parent, const wchar_t* name) {
    if (!parent)
        return false;
    try {
        Ztl_variant_t value = parent->item[name];
        return V_VT(&value) != VT_EMPTY && V_VT(&value) != VT_ERROR;
    } catch (...) {
        return false;
    }
}

std::vector<std::wstring> Names(IWzProperty* property) {
    std::vector<std::wstring> result;
    if (!property)
        return result;
    try {
        IEnumVARIANTPtr enumerator = property->_NewEnum;
        while (enumerator) {
            Ztl_variant_t value;
            ULONG fetched = 0;
            if (FAILED(enumerator->Next(1, &value, &fetched)) || !fetched)
                break;
            if (V_VT(&value) == VT_BSTR && V_BSTR(&value))
                result.emplace_back(V_BSTR(&value));
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

bool Decimal(const std::wstring& text, int* result) {
    if (!result || text.empty())
        return false;
    int value = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9')
            return false;
        value = value * 10 + c - L'0';
        if (value > 100000000)
            return false;
    }
    *result = value;
    return true;
}

std::wstring StockName(int code) {
    if (code < 0 || code >= StockActionCount)
        return {};
    try {
        Ztl_bstr_t name;
        g_getName(&name, code);
        return name.GetBSTR() ? name.GetBSTR() : L"";
    } catch (...) {
        return {};
    }
}

int StockCode(const std::wstring& name) {
    for (int code = 0; code < StockActionCount; ++code)
        if (_wcsicmp(StockName(code).c_str(), name.c_str()) == 0)
            return code;
    return -1;
}

void Reset() {
    g_enabled = false;
    g_actions.clear();
    g_byName.clear();
    g_movementSets.clear();
    g_byCode.fill(nullptr);
    g_byMoveAction.fill(-1);
}

bool Invalid(const wchar_t* format, ...) {
    wchar_t message[512];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);
    g_validationError = message;
    return false;
}

bool StockBodyFrameExists(const std::wstring& actionName, int frameIndex) {
    wchar_t path[256];
    swprintf_s(path, L"Character/00002000.img/%ls/%d", actionName.c_str(), frameIndex);
    try {
        IWzPropertyPtr frame = AsProperty(get_rm()->GetObjectA(path));
        return frame && HasChild(frame, L"body");
    } catch (...) {
        return false;
    }
}

bool ValidateFrames(const Action& action, const std::map<std::wstring, int>& known) {
    std::set<int> indexes;
    if (!action.frames)
        return Invalid(L"actions/%d (%ls): missing frames property", action.code, action.name.c_str());
    for (const auto& name : Names(action.frames)) {
        if (_wcsicmp(name.c_str(), L"info") == 0)
            continue;
        int index = -1;
        IWzPropertyPtr frame = Child(action.frames, name.c_str());
        std::wstring sourceName = StringValue(frame, L"action");
        std::wstring source = Lower(sourceName.c_str());
        if (!Decimal(name, &index) || index < 0)
            return Invalid(L"actions/%d/frames/%ls: node name must be a non-negative integer", action.code,
                    name.c_str());
        if (!frame)
            return Invalid(L"actions/%d/frames/%ls: frame must be a property", action.code, name.c_str());
        if (!indexes.insert(index).second)
            return Invalid(L"actions/%d/frames/%ls: duplicate frame index", action.code, name.c_str());
        if (source.empty())
            return Invalid(L"actions/%d/frames/%ls: missing string property 'action'", action.code, name.c_str());
        if (known.find(source) == known.end())
            return Invalid(L"actions/%d/frames/%ls: source action '%ls' is unknown or not earlier", action.code,
                    name.c_str(), source.c_str());
        int sourceFrame = IntValue(frame, L"frame", -1);
        if (sourceFrame < 0)
            return Invalid(L"actions/%d/frames/%ls: 'frame' must be a non-negative integer", action.code,
                    name.c_str());
        auto sourceCode = known.find(source);
        if (sourceCode->second < StockActionCount && !StockBodyFrameExists(sourceName, sourceFrame))
            return Invalid(L"CustomActions.img/%ls/%ls: Character/00002000.img/%ls/%d has no body canvas",
                    action.name.c_str(), name.c_str(), sourceName.c_str(), sourceFrame);
        (void)IntValue(frame, L"delay", 0);
    }
    if (indexes.empty())
        return Invalid(L"actions/%d (%ls): frames property is empty", action.code, action.name.c_str());
    for (int i = 0; i < static_cast<int>(indexes.size()); ++i)
        if (!indexes.count(i))
            return Invalid(L"actions/%d (%ls): frame indexes must be contiguous from 0", action.code,
                    action.name.c_str());
    return true;
}

bool LoadManifest(IWzProperty* root) {
    Reset();
    g_validationError.clear();
    IWzPropertyPtr sets = Child(root, L"movementSets");
    for (const auto& name : Names(sets)) {
        int id = 0;
        if (!Decimal(name, &id) || id <= 0)
            return Invalid(L"movementSets/%ls: id must be a positive integer", name.c_str());
        MovementSet set;
        set.name = StringValue(Child(sets, name.c_str()), L"name");
        if (!g_movementSets.emplace(id, std::move(set)).second)
            return Invalid(L"movementSets/%d: duplicate id", id);
    }

    std::map<std::wstring, int> known;
    for (int code = 0; code < StockActionCount; ++code) {
        std::wstring name = Lower(StockName(code).c_str());
        if (!name.empty())
            known.emplace(name, code);
    }

    // Mirror Character/00002000.img: each root child is named after its action and
    // contains numbered composite frames directly. Sort names so all clients assign
    // identical numeric codes regardless of COM enumeration order.
    std::map<std::wstring, std::pair<std::wstring, IWzPropertyPtr>> nodes;
    int skippedStockActions = 0;
    for (const auto& name : Names(root)) {
        if (_wcsicmp(name.c_str(), L"movementSets") == 0 || _wcsicmp(name.c_str(), L"items") == 0 ||
                _wcsicmp(name.c_str(), L"info") == 0)
            continue;
        IWzPropertyPtr actionNode = Child(root, name.c_str());
        if (!actionNode)
            return Invalid(L"CustomActions.img/%ls: action must be a property", name.c_str());
        std::wstring lowerName = Lower(name.c_str());
        if (known.count(lowerName)) {
            ++skippedStockActions;
            continue;
        }
        if (!nodes.emplace(lowerName, std::make_pair(name, actionNode)).second)
            return Invalid(L"CustomActions.img/%ls: duplicate action name", name.c_str());
    }
    if (nodes.empty())
        return Invalid(L"CustomActions.img contains no named action properties");
    if (StockActionCount + nodes.size() > MaxActionCount)
        return Invalid(L"CustomActions.img contains %d genuinely new actions; this client layout safely supports "
                       L"at most %d",
                static_cast<int>(nodes.size()), MaxActionCount - StockActionCount);

    // Make every custom name visible while validating frame references. Cycles are
    // rejected separately by the native loader if they cannot resolve to stock frames.
    int assignedCode = StockActionCount;
    for (const auto& node : nodes) {
        known.emplace(node.first, assignedCode++);
    }

    g_actions.reserve(nodes.size());
    assignedCode = StockActionCount;
    for (const auto& node : nodes) {
        IWzPropertyPtr actionNode = node.second.second;
        IWzPropertyPtr info = Child(actionNode, L"info");
        Action action;
        action.code = assignedCode++;
        action.name = node.second.first;
        action.lowerName = node.first;
        std::wstring templateName = StringValue(info, L"template");
        if (templateName.empty()) {
            IWzPropertyPtr firstFrame = Child(actionNode, L"0");
            templateName = StringValue(firstFrame, L"action");
        }
        action.templateCode = StockCode(templateName);
        action.movement = _wcsicmp(StringValue(info, L"type").c_str(), L"movement") == 0;
        action.movementSet = IntValue(info, L"movementSet", 0);
        action.baseMoveAction = IntValue(info, L"baseMoveAction", 0);
        action.moveAction = IntValue(info, L"moveAction", 0);
        action.frames = actionNode;
        if (action.templateCode < 0)
            return Invalid(L"CustomActions.img/%ls: frame 0 action or info/template must name a stock action",
                    action.name.c_str());
        if (action.movement && action.movementSet <= 0)
            return Invalid(L"actions/%d/info: movementSet must be positive", action.code);
        if (action.movement && (action.baseMoveAction < 1 || action.baseMoveAction > 10))
            return Invalid(L"actions/%d/info: baseMoveAction must be in the range 1-10", action.code);
        if (action.movement && (action.moveAction < 11 || action.moveAction > 127))
            return Invalid(L"actions/%d/info: moveAction must be in the range 11-127", action.code);
        if (!ValidateFrames(action, known))
            return false;
        known.emplace(action.lowerName, action.code); // Earlier-only references make cycles impossible.
        g_actions.push_back(std::move(action));
    }

    for (Action& action : g_actions) {
        g_byCode[action.code] = &action;
        g_byName.emplace(action.lowerName, action.code);
        if (!action.movement)
            continue;
        auto set = g_movementSets.find(action.movementSet);
        if (set == g_movementSets.end())
            return Invalid(L"actions/%d/info: movementSet %d is not declared", action.code, action.movementSet);
        if (g_byMoveAction[action.moveAction] != -1)
            return Invalid(L"actions/%d/info: moveAction %d is already assigned", action.code, action.moveAction);
        if (!set->second.byBaseAction.emplace(action.baseMoveAction, action.code).second)
            return Invalid(L"actions/%d/info: movement set %d already maps baseMoveAction %d", action.code,
                    action.movementSet, action.baseMoveAction);
        g_byMoveAction[action.moveAction] = action.code;
    }
    DEBUG_MESSAGE("CustomActions: ignored %d stock action nodes and found %d new action nodes.\n",
            skippedStockActions, static_cast<int>(g_actions.size()));
    return true;
}

bool FindOperand(uintptr_t instruction, uintptr_t* operand, uintptr_t* oldValue) {
    int found = 0;
    for (size_t offset = 0; offset <= 8; ++offset) {
        uintptr_t value = *reinterpret_cast<const uint32_t*>(instruction + offset);
        if (value >= kStockTable && value < kStockTable + kRecordSize) {
            ++found;
            *operand = instruction + offset;
            *oldValue = value;
        }
    }
    return found == 1;
}

bool InstallTable() {
    struct Patch {
        uintptr_t operand;
        uintptr_t oldValue;
    };
    std::vector<Patch> patches;
    for (uintptr_t instruction : kTableInstructions) {
        Patch patch{};
        if (!FindOperand(instruction, &patch.operand, &patch.oldValue)) {
            ErrorMessage("CustomActions: action-table signature mismatch at 0x%08X; disabled.", instruction);
            return false;
        }
        patches.push_back(patch);
    }
    const unsigned char expected[] = { 0x81, 0xFE, 0xA2, 0, 0, 0 };
    if (memcmp(reinterpret_cast<void*>(kInitCountInsn), expected, sizeof(expected))) {
        ErrorMessage("CustomActions: CActionMan::Init signature mismatch; disabled.");
        return false;
    }
    const unsigned char updateBound[] = { 0x81, 0x7D, 0xEC, 0xA2, 0, 0, 0 };
    const unsigned char prepareBound[] = { 0x81, 0xFB, 0xA2, 0, 0, 0 };
    const unsigned char clearCount[] = { 0xBB, 0xA2, 0, 0, 0 };
    const unsigned char itemCacheCount[] = { 0xBE, 0xA2, 0, 0, 0 };
    const unsigned char itemCacheBytes[] = { 0x68, 0x20, 0x0A, 0, 0 };
    if (memcmp(reinterpret_cast<void*>(0x004522F4), updateBound, sizeof(updateBound)) ||
            memcmp(reinterpret_cast<void*>(0x00453B2B), prepareBound, sizeof(prepareBound)) ||
            memcmp(reinterpret_cast<void*>(0x00453A3F), clearCount, sizeof(clearCount)) ||
            memcmp(reinterpret_cast<void*>(kItemActionCacheCountInsn), itemCacheCount,
                    sizeof(itemCacheCount)) ||
            memcmp(reinterpret_cast<void*>(kItemActionCacheBytesInsn), itemCacheBytes,
                    sizeof(itemCacheBytes))) {
        ErrorMessage("CustomActions: CAvatar action-bound signature mismatch; disabled.");
        return false;
    }
    memset(g_table, 0, sizeof(g_table));
    // ACTIONDATA is not trivially copyable.  +0 is a ref-counted Ztl_bstr_t and
    // +0x14 is a runtime frame-array pointer populated by CActionMan::Init.
    // memcpy would alias every stock frame array into this table; the next init
    // or cleanup then mutates/releases storage still owned by the native table.
    // Copy only the two static behavior flags (+4 and +8), construct an owned
    // name, and leave +0x0C..+0x14 clear for native initialization.
    for (int code = 0; code < StockActionCount; ++code) {
        unsigned char* destination = g_table + code * kRecordSize;
        const unsigned char* source = reinterpret_cast<const unsigned char*>(kStockTable + code * kRecordSize);
        std::wstring name = StockName(code);
        new (destination) Ztl_bstr_t(name.c_str());
        *reinterpret_cast<int*>(destination + 4) = *reinterpret_cast<const int*>(source + 4);
        *reinterpret_cast<int*>(destination + 8) = *reinterpret_cast<const int*>(source + 8);
    }
    for (const Action& action : g_actions) {
        unsigned char* destination = g_table + action.code * kRecordSize;
        new (destination) Ztl_bstr_t(action.name.c_str());
        *reinterpret_cast<int*>(destination + 4) =
                *reinterpret_cast<const int*>(g_table + action.templateCode * kRecordSize + 4);

        // ACTIONDATA+8 is the native composite-action discriminator.  Stock
        // canvas actions such as alert leave it clear, while actions such as
        // 00002000.img/savage set it and are consequently parsed as numbered
        // { action, frame, delay } descriptors.  A custom action inherits the
        // remaining behavior from its template, but its manifest shape is
        // always composite unless an item supplies an explicit canvas override.
        // Leaving the template's zero here makes CActionMan treat descriptor
        // properties as canvases and BuildAvatarActionFrames faults at 0040223F.
        *reinterpret_cast<int*>(destination + 8) = 1;
    }
    uintptr_t base = reinterpret_cast<uintptr_t>(g_table);
    for (const Patch& patch : patches)
        Patch4(patch.operand, static_cast<unsigned int>(base + patch.oldValue - kStockTable));
    unsigned int count = static_cast<unsigned int>(StockActionCount + g_actions.size());
    Patch4(kInitCountInsn + 2, count);
    Patch4(0x004522F4 + 3, count);
    Patch4(0x00453B2B + 2, count);
    Patch4(0x00453A3F + 1, count);
    // CActionMan's item/effect loader owns a separate array of 16-byte records.
    // It does not reference ACTIONDATA, so an executable-wide table-operand scan
    // cannot find it. Extend both its allocation count and its zero-fill size.
    Patch4(kItemActionCacheCountInsn + 1, count);
    Patch4(kItemActionCacheBytesInsn + 1, count * 16);
    return true;
}

Ztl_bstr_t* __cdecl GetNameHook(Ztl_bstr_t* result, int code) {
    if (g_enabled && code >= StockActionCount && code < MaxActionCount && g_byCode[code]) {
        *result = g_byCode[code]->name.c_str();
        return result;
    }
    if (code < 0 || code >= StockActionCount) {
        LOG_ONCE_PER_ID(code, "CustomActions: invalid action code %d; using stand1.\n", code);
        code = 2;
    }
    return g_getName(result, code);
}

int __cdecl GetCodeHook(Ztl_bstr_t name) {
    const wchar_t* value = name.GetBSTR();
    if (g_enabled && value) {
        auto found = g_byName.find(Lower(value));
        if (found != g_byName.end())
            return found->second;
    }
    // The native routine consumes its by-value BSTR manually, so calling it from a
    // C++ wrapper would release the temporary twice. Resolve stock names safely here.
    return value ? StockCode(value) : -1;
}

int __fastcall MoveToActionHook(CAvatar* avatar, void*, int packed, int* direction) {
    int moveAction = packed >> 1;
    if (direction)
        *direction = packed & 1;
    if (g_enabled && moveAction >= 11 && moveAction <= 127) {
        int code = g_byMoveAction[moveAction];
        if (code >= StockActionCount)
            return code;
        LOG_ONCE_PER_ID(moveAction, "CustomActions: unknown move action %d; using stand1.\n", moveAction);
        return 2;
    }
    return g_moveToAction(avatar, packed, direction);
}

void __fastcall SetMoveHook(CAvatar* avatar, void*, int packed, int reload) {
    if (!g_enabled || !avatar || !avatar->m_pCustomData)
        return g_setMove(avatar, packed, reload);
    int moveAction = packed >> 1;
    if (moveAction >= 1 && moveAction <= 10) {
        avatar->m_pCustomData->nNativeMoveAction = packed;
        auto set = g_movementSets.find(avatar->m_pCustomData->nCustomMovementSet);
        if (set != g_movementSets.end()) {
            auto mapped = set->second.byBaseAction.find(moveAction);
            if (mapped != set->second.byBaseAction.end()) {
                const Action* action = g_byCode[mapped->second];
                packed = (action->moveAction << 1) | (packed & 1);
            }
        }
    } else if (moveAction >= 11 && moveAction <= 127 && g_byMoveAction[moveAction] < 0) {
        packed = (2 << 1) | (packed & 1);
    }
    g_setMove(avatar, packed, reload);
}

bool CharacterItemId(const wchar_t* uol, int* itemId) {
    if (!uol || !itemId)
        return false;
    std::wstring path = uol;
    std::replace(path.begin(), path.end(), L'\\', L'/');
    if (path.size() < 13 || path.compare(0, 10, L"Character/") ||
            path.compare(path.size() - 4, 4, L".img"))
        return false;
    size_t slash = path.find_last_of(L'/');
    std::wstring file = path.substr(slash + 1, path.size() - slash - 5);
    return file.size() == 8 && Decimal(file, itemId);
}

IWzPropertyPtr ItemOverride(int itemId, const std::wstring& actionName) {
    wchar_t path[160];
    swprintf_s(path, L"Custom/CustomActions.img/items/%08d/%ls", itemId, actionName.c_str());
    try {
        return AsProperty(get_rm()->GetObjectA(path));
    } catch (...) {
        return nullptr;
    }
}

IWzPropertyPtr NewProperty() {
    IWzPropertyPtr property;
    try {
        PcCreateObject<IWzPropertyPtr>(L"Property", property, nullptr);
    } catch (...) {
        try {
            PcCreateObject<IWzPropertyPtr>(L"Canvas#Property", property, nullptr);
        } catch (...) {
        }
    }
    return property;
}

IWzPropertyPtr ClonePropertyTree(IWzProperty* source) {
    if (!source)
        return nullptr;
    IWzPropertyPtr clone = NewProperty();
    if (!clone)
        return nullptr;
    try {
        for (const std::wstring& name : Names(source)) {
            Ztl_variant_t value = source->item[name.c_str()];
            IUnknownPtr unknown = value.GetUnknown();
            if (unknown) {
                // A canvas also implements IWzProperty. Keep the canvas itself so
                // its pixels survive, but clone ordinary property containers so a
                // Character image never shares ownership of CustomActions.img's
                // internal property nodes.
                IWzCanvasPtr canvas;
                if (FAILED(unknown->QueryInterface(&canvas)) || !canvas) {
                    IWzVector2DPtr vector;
                    if (SUCCEEDED(unknown->QueryInterface(&vector)) && vector) {
                        IWzVector2DPtr vectorClone;
                        PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", vectorClone, nullptr);
                        if (!vectorClone || FAILED(vectorClone->raw_Init(vector->x, vector->y)))
                            return nullptr;
                        value = Ztl_variant_t(static_cast<IUnknown*>(vectorClone));
                        if (FAILED(clone->Add(name.c_str(), value, false)))
                            return nullptr;
                        continue;
                    }
                    IWzPropertyPtr child;
                    if (SUCCEEDED(unknown->QueryInterface(&child)) && child) {
                        IWzPropertyPtr childClone = ClonePropertyTree(child);
                        if (!childClone)
                            return nullptr;
                        value = Ztl_variant_t(static_cast<IUnknown*>(childClone));
                    }
                }
            }
            if (FAILED(clone->Add(name.c_str(), value, false)))
                return nullptr;
        }
    } catch (...) {
        return nullptr;
    }
    return clone;
}

IWzPropertyPtr BuildCompositeFrames(const Action& action) {
    IWzPropertyPtr result = NewProperty();
    if (!result || !action.frames)
        return nullptr;
    try {
        for (const std::wstring& name : Names(action.frames)) {
            int index = -1;
            if (!Decimal(name, &index) || index < 0)
                continue;
            IWzPropertyPtr sourceFrame = Child(action.frames, name.c_str());
            IWzPropertyPtr frame = ClonePropertyTree(sourceFrame);
            if (!sourceFrame || !frame)
                return nullptr;

            // Copy only numbered frame nodes, but preserve every field inside a
            // frame. Native composites use optional metadata such as `move` and
            // `flip`; ClonePropertyTree gives vectors independent ownership.
            Ztl_variant_t frameValue(static_cast<IUnknown*>(frame));
            if (FAILED(result->Add(name.c_str(), frameValue, false)))
                return nullptr;
        }
    } catch (...) {
        return nullptr;
    }
    return result;
}

bool IsBodyImage(int itemId) {
    // Character body images are 00002000.img and the adjacent skin variants.
    // Native composite actions (for example 00002000.img/savage) live on the
    // body only.  Equipment, hair, and head images are resolved through the
    // composite frame's referenced stock action; adding the descriptor node to
    // those images makes the avatar builder interpret `action`/`frame` as sprite
    // data and eventually leaves it with a null canvas record.
    return itemId >= 2000 && itemId < 3000;
}

bool InjectItemActions(IWzProperty* property, int itemId) {
    if (!property)
        return false;
    bool succeeded = true;
    for (const Action& action : g_actions) {
        if (HasChild(property, action.name.c_str()))
            continue;
        IWzPropertyPtr itemOverride = ItemOverride(itemId, action.name);
        if (!itemOverride && !IsBodyImage(itemId))
            continue;
        try {
            IWzPropertyPtr owned =
                    itemOverride ? ClonePropertyTree(itemOverride) : BuildCompositeFrames(action);
            if (!owned)
                throw _com_error(E_FAIL);
            Ztl_variant_t value(static_cast<IUnknown*>(owned));
            property->Add(action.name.c_str(), value, false);
        } catch (...) {
            succeeded = false;
            LOG_ONCE_PER_ID(action.code, "CustomActions: failed to inject action %d.\n", action.code);
        }
    }
    return succeeded;
}

} // namespace

void Attach() {
    if constexpr (!Config::CUSTOM_ACTIONS)
        return;
    if (g_attached)
        return;
    g_attached = true;
    if (!ATTACH_HOOK(g_getName, GetNameHook) || !ATTACH_HOOK(g_getCode, GetCodeHook) ||
            !ATTACH_HOOK(g_moveToAction, MoveToActionHook) || !ATTACH_HOOK(g_setMove, SetMoveHook))
        g_enabled = false;
}

void OnCustomWzMounted() {
    if constexpr (!Config::CUSTOM_ACTIONS)
        return;
    if (g_loaded)
        return;
    g_loaded = true;
    IWzPropertyPtr root;
    try {
        root = AsProperty(get_rm()->GetObjectA(L"Custom/CustomActions.img"));
    } catch (...) {
        return;
    }
    if (!root) {
        DEBUG_MESSAGE("CustomActions: CustomActions.img not present; disabled.\n");
        return;
    }
    if (!LoadManifest(root)) {
        if (!g_validationError.empty())
            ErrorMessage("CustomActions: invalid CustomActions.img:\n\n%ls\n\nFeature disabled.",
                    g_validationError.c_str());
        else
            ErrorMessage("CustomActions: patch verification failed; feature disabled.");
        Reset();
        return;
    }
    // Frame validation above reads Character/00002000.img before the property
    // serialization detour is installed by InitializeResMan. ResMan may retain
    // that object, so relying only on the later serialization callback makes the
    // first avatar's custom actions depend on cache timing. Inject the cached
    // base body synchronously before publishing the registry as enabled.
    try {
        IWzPropertyPtr baseBody = AsProperty(get_rm()->GetObjectA(L"Character/00002000.img"));
        if (!baseBody) {
            Invalid(L"Character/00002000.img could not be loaded for action injection");
            ErrorMessage("CustomActions: invalid CustomActions.img:\n\n%ls\n\nFeature disabled.",
                    g_validationError.c_str());
            Reset();
            return;
        }
        if (!InjectItemActions(baseBody, 2000))
            throw _com_error(E_FAIL);
    } catch (...) {
        Invalid(L"Character/00002000.img action injection failed");
        ErrorMessage("CustomActions: invalid CustomActions.img:\n\n%ls\n\nFeature disabled.",
                g_validationError.c_str());
        Reset();
        return;
    }
    if (!InstallTable()) {
        ErrorMessage("CustomActions: patch verification failed; feature disabled.");
        Reset();
        return;
    }
    g_enabled = true;
    DEBUG_MESSAGE("CustomActions: registered %d actions and %d movement sets.\n",
            static_cast<int>(g_actions.size()), static_cast<int>(g_movementSets.size()));
}

void OnPropertySerialized(IWzProperty* property, IWzArchive* archive) {
    if (!g_enabled || !property || !archive)
        return;
    int itemId = 0;
    if (!CharacterItemId(archive->absoluteUOL, &itemId))
        return;
    (void)InjectItemActions(property, itemId);
}

void OnAvatarConstructed(CAvatar* avatar) {
    if (!avatar || !avatar->m_pCustomData)
        return;
    avatar->m_pCustomData->nCustomMovementSet = 0;
    avatar->m_pCustomData->nNativeMoveAction = 4;
}

bool IsEnabled() { return g_enabled; }

int FindActionCode(const wchar_t* name) {
    if (!g_enabled || !name)
        return -1;
    auto found = g_byName.find(Lower(name));
    return found == g_byName.end() ? -1 : found->second;
}

bool PlayCustomAction(CAvatar* avatar, int code) {
    if (!g_enabled || !avatar || code < StockActionCount || code >= MaxActionCount || !g_byCode[code])
        return false;
    g_setAction(avatar, code);
    return true;
}

bool PlayCustomAction(CAvatar* avatar, const wchar_t* name) {
    return PlayCustomAction(avatar, FindActionCode(name));
}

bool SetCustomMovementSet(CUser* user, int id) {
    if (!g_enabled || !user || g_movementSets.find(id) == g_movementSets.end())
        return false;
    CAvatar* avatar = reinterpret_cast<CAvatar*>(reinterpret_cast<unsigned char*>(user) + 0x88);
    if (!avatar->m_pCustomData)
        return false;
    avatar->m_pCustomData->nCustomMovementSet = id;
    SetMoveHook(avatar, nullptr, avatar->m_pCustomData->nNativeMoveAction, 1);
    return true;
}

void ClearCustomMovementSet(CUser* user) {
    if (!user)
        return;
    CAvatar* avatar = reinterpret_cast<CAvatar*>(reinterpret_cast<unsigned char*>(user) + 0x88);
    if (!avatar->m_pCustomData)
        return;
    int native = avatar->m_pCustomData->nNativeMoveAction;
    avatar->m_pCustomData->nCustomMovementSet = 0;
    g_setMove(avatar, native, 1);
}

} // namespace CustomActions

void AttachCustomActions() { CustomActions::Attach(); }
