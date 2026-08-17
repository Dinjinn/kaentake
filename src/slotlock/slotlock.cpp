#include "pch.h"
#include "hook.h"
#include "ztl/ztl.h"
#include "wvs/util.h"
#include "wvs/packet.h"
#include "wvs/secure.h"
#include "wvs/wnd.h"
#include <array>
#include <set>

namespace SlotLock {
// Mod Config

constexpr auto InventoryDrawColor = 0xAA4A90E2;
// example
// 0x884A90E2 light blue
// 0xCCDAA520 Antique Gold
// 0xCCFF7F50 Soft Coral
// 0xCC9966CC Muted Purple
constexpr int InventoryDrawThickness = 2;  

constexpr int DragLockKey = VK_SHIFT;
// VK_SHIFT
// VK_CONTROL

// ~Mod Config


class CClientSocket;
struct GW_ItemSlotBase : public ZRefCounted {    
    MEMBER_AT(TSecType<int>, 0xC, nItemID);
    MEMBER_AT(_LARGE_INTEGER, 0x18, liCashItemSN);
    virtual ~GW_ItemSlotBase() = 0;
    virtual int IsProtectedItem() = 0;
    virtual int IsPreventSlipItem() = 0;
    virtual int IsSupportWarmItem() = 0;
    virtual int IsBindedItem() = 0;
    virtual int IsPossibleTradingItem() = 0;
    virtual int GetType() = 0;
    virtual int GetDataSize() = 0;
    virtual int GetItemNumber() = 0;
    // ... ignore unnecessary virtual methods
};

class CharacterData {
public:
    // v83
    // 
    // v95
    // .text:0042A480 ; int __thiscall CharacterData::GetItemSlotCount(CharacterData *this, int nTI)

    // v83 has no CharacterData::GetItemSlotCount, so madebygpt
    int GetItemSlotCount(int ti)
    {
        int** slots = (int**)((char*)this + 1095);

        int* arr = slots[ti];

        if (!arr)
            return 0;

        return *(arr - 1);
    }
     
    // v83
    // ___:004282F7 ; int __thiscall CharacterData::GetItem(int this, int result, signed int nTI, int nPos)
    // v95
    // .text:0042B990 ; ZRef<GW_ItemSlotBase> *__thiscall CharacterData::GetItem(CharacterData *this, ZRef<GW_ItemSlotBase> *result, int nTI, int nPos)
    ZRef<GW_ItemSlotBase>* GetItem(ZRef<GW_ItemSlotBase>* result, int nTI, int nPos) {
        return reinterpret_cast<ZRef<GW_ItemSlotBase>*(__thiscall*)(CharacterData*, ZRef<GW_ItemSlotBase>*, int, int)>(0x004282F7)(this, result, nTI, nPos);
    }
};


class CWvsContext : public TSingleton<CWvsContext, 0x00BE7918> {
public:
    // v83
    // 20A4
    // v95
    // 000020B8     int m_bExclRequestSent;
    MEMBER_AT(int, 0x20A4, m_bExclRequestSent)        
    // v83 
    // 20A8 
    // v95 
    // 000020BC     int m_tExclRequestSent;
    MEMBER_AT(int, 0x20A8, m_tExclRequestSent)   

    // v83
    // ___:00425D0B ; _DWORD *__thiscall CWvsContext::GetCharacterData(_DWORD *this, _DWORD *result)
    // v95
    // .text:0042B960 ; ZRef<CharacterData> *__thiscall CWvsContext::GetCharacterData(CWvsContext *this, ZRef<CharacterData> *result)
    ZRef<CharacterData> * GetCharacterData( ZRef<CharacterData> *result) {
        return reinterpret_cast<ZRef<CharacterData> *(__thiscall*)(CWvsContext*, ZRef<CharacterData> *)>(0x00425D0B)(this, result);
    }
};

// Inventory tab types (nTI)
// Based on v95 enum
// enum $3681F0796203820C8E23634CF5E1463B : __int32
enum InventoryType {
    IT_EQUIP   = 0x1,
    IT_CONSUME = 0x2,
    IT_INSTALL = 0x3,
    IT_ETC     = 0x4,
    IT_CASH    = 0x5,

    // Aliases / Unknown (kept for compatibility & reversing reference)
    // IT_NO      = 0x5, 
    // IT_EXNO    = 0x6,
};

void SaveSlotLock();
void LoadSlotLock();


class CConfig : public TSingleton<CConfig, 0x00BEBF9C> {
public:
    enum {
        GLOBAL_OPT = 0x0,
        LAST_CONNECT_INFO = 0x1,
        CHARACTER_OPT = 0x2,
    };

    
    int GetOpt_Int(int nType, const char* sKey, int nDefaultX, int nLowBound, int nHighBound) {
        return reinterpret_cast<int(__thiscall*)(CConfig*, int, const char*, int, int, int)>(0x0049EF65)(this, nType, sKey, nDefaultX, nLowBound, nHighBound);
    }
    void SetOpt_Int(int nType, const char* sKey, int nValue) {
        reinterpret_cast<void(__thiscall*)(CConfig*, int, const char*, int)>(0x0049EFB5)(this, nType, sKey, nValue);
    }

    // v83
    // ___:0049F079 ; int __thiscall sub_49F079(CConfig *this, int nType, int, int *)
    // v95
    // .text:004B2830 ; void __thiscall CConfig::SetOpt_Binary(CConfig *this, int nType, const char *sKey, ZArray<unsigned char> *abData)
    void SetOpt_Binary(int nType, const char *sKey, ZArray<unsigned char> *abData) {
        reinterpret_cast<void(__thiscall*)(CConfig*, int, const char*, ZArray<unsigned char>*)>(0x0049F079)(this, nType, sKey, abData);
    }

    // v83
    // ___:0049EFDE ; int __thiscall sub_49EFDE(CConfig *this, int nType, int, int arg0)
    // v95
    // .text:004B2F00 ; void __thiscall CConfig::GetOpt_Binary(CConfig *this, unsigned int nType, char *sKey, ZArray<unsigned char> *abData)
    void GetOpt_Binary(int nType, char *sKey, ZArray<unsigned char> *abData) {
        reinterpret_cast<void(__thiscall*)(CConfig*, int, const char*, ZArray<unsigned char>*)>(0x0049EFDE)(this, nType, sKey, abData);
    }

    // v83
    // ___:0049D90D ; void __thiscall CConfig::SaveCharacter(CConfig *this)
    // v95
    // .text:004B5B00 ; void __thiscall CConfig::SaveCharacter(CConfig *this)    
    MEMBER_HOOK(void, 0x0049D90D, SaveCharacter)

    // v83
    // ___:0049D0B6 ; void __thiscall CConfig::LoadCharacter(CConfig *this, int nWorldID, unsigned int dwCharacterId)
    // v95
    // .text:004B6C00 ; void __thiscall CConfig::LoadCharacter(CConfig *this, int nWorldID, unsigned int dwCharacterId)
    MEMBER_HOOK(void, 0x0049D0B6, LoadCharacter, int nWorldID, unsigned int dwCharacterId)
};

void CConfig::SaveCharacter_hook() {
    SaveCharacter(this);
    SaveSlotLock();
}
void CConfig::LoadCharacter_hook(int nWorldID, unsigned int dwCharacterId) {
    LoadCharacter(this, nWorldID, dwCharacterId);
    LoadSlotLock();
}


static std::array<std::set<unsigned char>, 6> SlotLocks;

// CConfig::GetOpt_Binary is bug can't get Binary
// CConfig::GetOpt_String is 256 limit
void SaveSlotLock() {
    for (int i = 1; i < 6; ++i) {
        int masks[8] = {}; 

        for (unsigned char slot : SlotLocks[i])
            masks[slot / 32] |= (1u << (slot % 32));

        for (int k = 0; k < 8; ++k) {
            char key[32];
            snprintf(key, sizeof(key), "SlotLock_%d_%d", i, k);
            CConfig::GetInstance()->SetOpt_Int(CConfig::CHARACTER_OPT, key, masks[k]);
        }
    }
}

void LoadSlotLock() {
    for (int i = 1; i < 6; ++i)
    {
        SlotLocks[i].clear();
        for (int k = 0; k < 8; ++k)
        {
            char key[32];
            snprintf(key, sizeof(key), "SlotLock_%d_%d", i, k);

            int mask = CConfig::GetInstance()->GetOpt_Int(CConfig::CHARACTER_OPT, key, 0, INT_MIN, INT_MAX);

            for (int bit = 0; bit < 32; ++bit)
                if (mask & (1u << bit))
                    SlotLocks[i].insert((unsigned char)(k * 32 + bit));
        }
    }
}


// v83 00BED654
// v95 00C6ABE0
class CUIItem : CUIWnd, public TSingleton<CUIItem, 0x00BED654> {
public:
    // v83
    // 05E0
    // v95
    // 00000B30     int m_nFirstPosition;
    // when scroll down showing first slot number
    MEMBER_AT(int, 0x05E0, m_nFirstPosition)   

    // v83 
    // 5E4
    // v95 
    // 00000B34     int m_nItemTI;
    // current inv Type
    MEMBER_AT(int, 0x05E4, m_nItemTI)   

    // v83
    // ___:0081D42E ; void __thiscall CUIItem::OnMouseButton(CUIItem *this, unsigned int msg, unsigned int wParam, int rx, int ry)
    // v95
    // .text:007CC590 ; void __thiscall CUIItem::OnMouseButton(CUIItem *this, unsigned int msg, unsigned int wParam, int rx, int ry)
    // void __thiscall CUIItem::OnMouseButton(CUIItem *this, unsigned int msg, unsigned int wParam, int rx, int ry)
    MEMBER_HOOK(void, 0x0081D42E, OnMouseButton, unsigned int msg, unsigned int wParam, int rx, int ry)

    // v83
    // ___:0081D8AD ; int __thiscall CUIItem::OnMouseMove(CUIItem *this, int rx, int ry)
    // v95
    // .text:007CCA90 ; int __thiscall CUIItem::OnMouseMove(CUIItem *this, int rx, int ry)
    MEMBER_HOOK(int, 0x0081D8AD, OnMouseMove, int rx, int ry)

    // v83
    // ___:0081DB7E ; int __thiscall CUIItem::GetSlotPositionFromPoint(CUIItem *this, int rx, int ry)
    // v95 i think v95 is wrong
    // .text:007CC220 ; int __thiscall CUIItem::GetSlotPositionFromPoint(CUIItem *this, tagPOINT rx)    
    int GetSlotPositionFromPoint(int rx, int ry) {
        return reinterpret_cast<int(__thiscall*)(CUIItem*, int, int)>(0x0081DB7E)(this, rx, ry);
    }
    // v83
    // ___:0081E2C8 ; int __thiscall CUIItem::GetItemSlotRect(CUIItem *this, int nSlotPosition, tagRECT *pRc)
    // v95
    // .text:007CBE90 ; int __thiscall CUIItem::GetItemSlotRect(CUIItem *this, int nSlotPosition, tagRECT *pRc)
    int GetItemSlotRect(int nSlotPosition, tagRECT *pRc) {
        return reinterpret_cast<int(__thiscall*)(CUIItem*, int, tagRECT*)>(0x0081E2C8)(this, nSlotPosition, pRc);
    }
};

static struct DragLockState {
    bool active = false;
    bool lockMode = false;      // true=lock, false=unlock
    int invType = 0;
    std::set<int> visited;      // processed slots (avoid duplicates)

    void Reset() {
        active = false;
        lockMode = false;
        invType = 0;
        visited.clear();
    }
} s_DragState;

void CUIItem::OnMouseButton_hook(unsigned int msg, unsigned int wParam, int rx, int ry) {
    if (msg == WM_RBUTTONDOWN) {

        // Decompiled: GetSlotPositionFromPoint receives (this - 4) as CUIItem*
        // The hook's `this` points into the vtable dispatch, -4 corrects to the actual object base
        CUIItem* ui = (CUIItem*)((char*)this - 4);
        
        int nSlotPos = ui->GetSlotPositionFromPoint(rx, ry);
        int invType = ui->m_nItemTI;
        if (invType < 1 || invType > 5) return;

        // DEBUG_MESSAGE("nSlotPos: %d ,firstPosition:%d , invType: %d", nSlotPos, ui->m_nFirstPosition ,invType);

        ZRef<CharacterData> result;
        // CharacterData* cd = *CWvsContext::GetInstance()->GetCharacterData(&result);

        if (nSlotPos > 0) {
            auto& set = SlotLocks[invType];
            bool isLocked = set.count(nSlotPos);

            if (isLocked) 
                set.erase(nSlotPos); // unlock
             else 
                set.insert(nSlotPos); // lock

            if (GetKeyState(DragLockKey) & 0x8000) {
                s_DragState.Reset();
                s_DragState.active = true;
                s_DragState.lockMode = !isLocked;
                s_DragState.invType = invType;
                s_DragState.visited.insert(nSlotPos);
            }
                
            // Trigger a redraw of the UI components
            ui->InvalidateRect(nullptr);
        }
    }
    // done drag
    else if (msg == WM_RBUTTONUP) {
        s_DragState.Reset();
    }
    
    // DEBUG_MESSAGE("OnMouseButton_hook | msg: 0x%X, wParam: 0x%X, rx: %d, ry: %d", msg, wParam, rx, ry);
    OnMouseButton(this, msg, wParam, rx, ry);
}


int CUIItem::OnMouseMove_hook(int rx, int ry) {
    // early exit if not dragging or key released
    if (!s_DragState.active || !(GetKeyState(DragLockKey) & 0x8000)) {
        if (s_DragState.active)
            s_DragState.Reset();
        return OnMouseMove(this, rx, ry);
    }

    CUIItem* ui = (CUIItem*)((char*)this - 4);

    // skip if moved to different inventory
    if (ui->m_nItemTI != s_DragState.invType)
        return OnMouseMove(this, rx, ry);

    int nSlotPos = ui->GetSlotPositionFromPoint(rx, ry);

    // apply lock/unlock to unvisited slots only
    if (nSlotPos > 0 && !s_DragState.visited.count(nSlotPos)) {
        s_DragState.visited.insert(nSlotPos);

        auto& set = SlotLocks[s_DragState.invType];
        if (s_DragState.lockMode)
            set.insert((unsigned char)nSlotPos);
        else
            set.erase((unsigned char)nSlotPos);

        ui->InvalidateRect(nullptr);
    }

    return OnMouseMove(this, rx, ry);
}


// v83
// ___:0049637B ; void __fastcall CClientSocket::SendPacket(void *arg0)
// v95
// .text:004AF9F0 ; void __fastcall CClientSocket::SendPacket(CClientSocket *this, int, const COutPacket *oPacket)
constexpr auto CClientSocket_SendPacket = 0x0049637B;


static void AppendSlotLockData(COutPacket* pPacket) {
    const int tab = CUIItem::GetInstance()->m_nItemTI;
    pPacket->Encode1((unsigned char)SlotLocks[tab].size());
    for (auto pos : SlotLocks[tab]) pPacket->Encode1(pos);
}

// v83
// ___:00A08FD5     call    ?SendPacket@CClientSocket@@QAEXABVCOutPacket@@@Z ; Call Procedure
// v95
// .text:009D5D07                 call    ?SendPacket@CClientSocket@@QAEXABVCOutPacket@@@Z ; CClientSocket::SendPacket(COutPacket const &)
constexpr auto CWvsContext_SendSortItemRequest_SendPacket_call = 0x00A08FD5;

void __fastcall CWvsContext_SendSortItemRequest_SendPacket_hook(void* pSocket, void* edx, COutPacket* pPacket) {    
    AppendSlotLockData(pPacket);
    
    // CClientSocket::SendPacket(CClientSocket::GetInstance(), oPacket);
    reinterpret_cast<void(__thiscall*)(CClientSocket*, const COutPacket&)>(CClientSocket_SendPacket)((CClientSocket*)pSocket, *pPacket);
}

// v83
// ___:00A08F43     call    ?SendPacket@CClientSocket@@QAEXABVCOutPacket@@@Z ; Call Procedure
// v95
// .text:009D5C17                 call    ?SendPacket@CClientSocket@@QAEXABVCOutPacket@@@Z ; CClientSocket::SendPacket(COutPacket const &)
constexpr auto CWvsContext_SendGatherItemRequest_SendPacket_call = 0x00A08F43;

void __fastcall CWvsContext_SendGatherItemRequest_SendPacket_hook(void* pSocket, void* edx, COutPacket* pPacket) {    
    AppendSlotLockData(pPacket);
    
    // CClientSocket::SendPacket(CClientSocket::GetInstance(), oPacket);
    reinterpret_cast<void(__thiscall*)(CClientSocket*, const COutPacket&)>(CClientSocket_SendPacket)((CClientSocket*)pSocket, *pPacket);
}



void __stdcall CUIItem_Draw_SlotLoopEnd_helper(CUIItem* pThis, int nPos, IWzCanvas* pCanvas) {        
    auto invType = pThis->m_nItemTI;
    auto& set = SlotLocks[invType];

    if (auto search = set.find(nPos); search != set.end()) {
        tagRECT pvargDest = { 0 };
        pThis->GetItemSlotRect(nPos, &pvargDest);
        const auto x = pvargDest.left;
        const auto y = pvargDest.bottom;
        
        // slot full 
        // pCanvas->DrawRectangle(x + 1, y - 31, 31, 31, 0x600055AA);   

        constexpr auto color = InventoryDrawColor;
        constexpr int thickness = InventoryDrawThickness;       
        constexpr int slotSize = 32;  
        
        // 1. Top horizontal bar
        pCanvas->DrawRectangle(x, y - slotSize, slotSize, thickness, color);
        // 2. Bottom horizontal bar
        pCanvas->DrawRectangle(x, y - thickness, slotSize, thickness, color);
        // 3. Left vertical bar (Exclude top/bottom thickness to prevent alpha overlap)
        pCanvas->DrawRectangle(x, y - slotSize + thickness, thickness, slotSize - (thickness * 2), color);
        // 4. Right vertical bar (Exclude top/bottom thickness to prevent alpha overlap)
        pCanvas->DrawRectangle(x + slotSize - thickness, y - slotSize + thickness, thickness, slotSize - (thickness * 2), color);
    }
}

// v95 maybe here 007CD482 increase for loop
constexpr auto CUIItem_Draw_SlotLoopEnd_jmp = 0x0081DF5C;
constexpr auto CUIItem_Draw_SlotLoopEnd_ret = 0x0081DF62;
void __declspec(naked) CUIItem_Draw_SlotLoopEnd_hook() {
    __asm {
        pushad
        pushfd
        
        push dword ptr [ebp-0x10] ; pCanvas
        push dword ptr [ebp+0x08] ; npos
        push ebx

        call CUIItem_Draw_SlotLoopEnd_helper

        popfd
        popad

        // restore original code
        inc dword ptr [ebp+8h]
        mov edx, [ebp-1Ch]
        jmp CUIItem_Draw_SlotLoopEnd_ret
    }
}

void AttachSlotLockModInternal() {    
    ATTACH_HOOK(CUIItem::OnMouseButton, CUIItem::OnMouseButton_hook);
    ATTACH_HOOK(CUIItem::OnMouseMove, CUIItem::OnMouseMove_hook);

    PatchCall(CWvsContext_SendGatherItemRequest_SendPacket_call, CWvsContext_SendGatherItemRequest_SendPacket_hook);
    PatchCall(CWvsContext_SendSortItemRequest_SendPacket_call, CWvsContext_SendSortItemRequest_SendPacket_hook);
    
    PatchJmp(CUIItem_Draw_SlotLoopEnd_jmp, &CUIItem_Draw_SlotLoopEnd_hook);
    PatchNop(CUIItem_Draw_SlotLoopEnd_jmp + 5, CUIItem_Draw_SlotLoopEnd_ret);
    
    ATTACH_HOOK(CConfig::SaveCharacter, CConfig::SaveCharacter_hook);
    ATTACH_HOOK(CConfig::LoadCharacter, CConfig::LoadCharacter_hook);
}

};

void AttachSlotLockMod() {
    SlotLock::AttachSlotLockModInternal();
}