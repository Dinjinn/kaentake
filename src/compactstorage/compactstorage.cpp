// ============================================================
// compactstorage.cpp — CTrunkDlg (the storage NPC dialog) as icon grids:
// storage (GET) side 5x5, player inventory (PUT) side 5x4, both scrollable
// by row. Slots past the purchased storage capacity blit the stock
// UI/UIWindow.img/Item/disabled sprite; the selected slot gets a translucent
// orange highlight; USE/SETUP/ETC stacks draw their quantity with the img
// number font the dialog already loads.
//
// 100% client-side and visual: the four hooks only change WHERE items are
// drawn and how mouse coordinates map back to item indices. The hit-test
// returns the same (side, absolute index) space the original used, so every
// existing click / double-click / tooltip path keeps working unmodified, and
// the server needs no change at all.
//
// All four hooks are FULL replacements — the originals are never called. The
// scrollbars are re-synced every frame from the draw hooks because the client
// does not reliably call SetScrollBar after items change; the ranges/positions
// are written straight into the CCtrlScrollBar fields, since calling the real
// SetScrollRange per frame triggers redraw side-effects.
// ============================================================

#include "pch.h"
#include "hook.h"
#include "debug.h"
#include "wvs/iteminfo.h"
#include "wvs/util.h"
#include "ztl/ztl.h"

#include <windows.h>

namespace CompactStorage {

// =====================================================
// ADDRESSES (v83, image base 0x400000)
// =====================================================
constexpr uintptr_t kAddr_DrawGetItem           = 0x007C77EE;   // CTrunkDlg::DrawGetItem
constexpr uintptr_t kAddr_DrawPutItem           = 0x007C7C56;   // CTrunkDlg::DrawPutItem
constexpr uintptr_t kAddr_GetItemIndexFromPoint = 0x007C8220;   // CTrunkDlg::GetItemIndexFromPoint
constexpr uintptr_t kAddr_SetScrollBar          = 0x007C7073;   // CTrunkDlg::SetScrollBar
constexpr uintptr_t kAddr_DrawNumberByImage     = 0x00988345;   // draw_number_by_image (__cdecl)

// ---- CTrunkDlg field offsets ------------------------------------------------
constexpr int kOff_SBGet        = 0xBC;   // CCtrlScrollBar* — storage side
constexpr int kOff_SBPut        = 0xC4;   // CCtrlScrollBar* — inventory side
constexpr int kOff_aGetItem     = 0xD4;   // ZArray<ITEM> — storage items
constexpr int kOff_aPutItem     = 0xD8;   // ZArray<ITEM> — inventory items shown in dialog
constexpr int kOff_GetSlotCount = 0xF8;   // purchased storage capacity (slot count)
constexpr int kOff_GetSelected  = 0xF0;   // selected storage slot index (-1 = none)
constexpr int kOff_PutSelected  = 0xF4;   // selected inventory slot index
constexpr int kOff_ImgFontNum   = 0xE4;   // IWzProperty* — img-number font for quantities
constexpr int kOff_SBCurPos     = 0x38;   // CCtrlScrollBar: current position
constexpr int kOff_SBRange      = 0x3C;   // CCtrlScrollBar: range (position count)
constexpr int kItemSize         = 0x1C;   // sizeof(CTrunkDlg::ITEM) — array element stride
constexpr int kItem_nItemID     = 0x00;   // ITEM +0x00: nItemID
constexpr int kItem_SlotPtr     = 0x18;   // ITEM +0x18: ZRef<GW_ItemSlotBase> (raw ptr slot)

// ---- grid shape ---------------------------------------------------------------
constexpr int kCols    = 5;
constexpr int kRowsGet = 5;    // storage visible rows
constexpr int kRowsPut = 4;    // inventory visible rows
constexpr int kPitch   = 40;   // px between cell origins, both axes

// ---- TUNING KNOBS (matched to the redrawn Trunk backgrnd art) -----------------
// DrawItemIconForSlot positions icons by their BOTTOM-LEFT BASELINE, like the
// game's own inventory — which is why the hit rects and overlay sprites subtract
// ~33-35px to get a cell's top edge.
constexpr int kGetDrawX0 = 8,   kGetDrawY0 = 120;   // storage grid baseline origin
constexpr int kPutDrawX0 = 238, kPutDrawY0 = 160;   // inventory grid baseline origin
constexpr int kHitYAdjust    = 33;   // hit rects start this far above the baseline
constexpr int kHitSize       = 36;   // clickable square per cell
constexpr int kSlotTopAdjust = 35;   // disabled sprite top = baseline - this
constexpr int kDisabledDX = -3, kDisabledDY = 1;    // disabled-sprite nudge
constexpr int kDisabledSize = 34;    // disabled sprite scaled to this square
constexpr int kNumDX = 0, kNumDY = -12;             // quantity-digits nudge
// Selection highlight (ARGB, semi-transparent orange) drawn behind the icon.
// IWzCanvas::DrawRectangle blends, which is exactly what a translucent overlay wants.
constexpr unsigned kSelColor = 0x88EE9922;
constexpr int kSelSize = 34, kSelTopAdjust = 35, kSelDX = -1, kSelDY = 2;

auto draw_number_by_image =
    reinterpret_cast<int(__cdecl*)(IWzCanvas*, int, int, int, IWzProperty*, int)>(kAddr_DrawNumberByImage);

int   RdInt(void* b, int off) { return *reinterpret_cast<int*>(reinterpret_cast<char*>(b) + off); }
void* RdPtr(void* b, int off) { return *reinterpret_cast<void**>(reinterpret_cast<char*>(b) + off); }

int MaxRow(int total, int visRows) {
    int rows = (total + kCols - 1) / kCols;
    return rows > visRows ? rows - visRows : 0;
}

// ZArray<T>: element count sits one size_t BEFORE the data pointer. The count is
// sanity-clamped because the array may be empty/unallocated while the dialog is
// opening or closing.
int ArrayCount(void* pThis, int offArr) {
    void* a = RdPtr(pThis, offArr);
    if (!a) return 0;
    int n = *(reinterpret_cast<int*>(a) - 1);
    return (n < 0 || n > 100000) ? 0 : n;
}

// Keep a scrollbar's range in sync with the item count every frame (the game's
// own SetScrollBar isn't reliably called after items change). Range = number of
// positions; the scrollbar disables itself at range <= 1, and the position is
// clamped to [0, range-1]. Fields are written DIRECTLY — the real SetScrollRange
// per frame triggers redraw side-effects.
void SyncScroll(void* sb, int total, int visRows) {
    if (!sb) return;
    int mr = MaxRow(total, visRows);
    int range = mr > 0 ? mr + 1 : 0;
    int* pRange = reinterpret_cast<int*>(reinterpret_cast<char*>(sb) + kOff_SBRange);
    int* pPos   = reinterpret_cast<int*>(reinterpret_cast<char*>(sb) + kOff_SBCurPos);
    *pRange = range;
    int pos = *pPos;
    if (range > 1) {
        if (pos < 0) pos = 0;
        if (pos > range - 1) pos = range - 1;
    } else {
        pos = 0;
    }
    *pPos = pos;
}

// GW_ItemSlotBase vtable slot 8 = GetNumber() (quantity). SEH-guarded: a stale or
// freed slot pointer must not crash the draw loop, and under /EHsc a C++
// catch(...) does NOT catch access violations — only __try/__except does.
int SafeGetQty(void* slot) {
    __try {
        void** vt = *reinterpret_cast<void***>(slot);
        return reinterpret_cast<int(__thiscall*)(void*)>(vt[8])(slot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// ---- WZ disabled-slot sprite (loaded once, cached for the process lifetime) ---
IWzCanvasPtr LoadCanvas(const wchar_t* sPath) {
    IWzCanvasPtr pCanvas;
    try {
        pCanvas = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(sPath)));
    } catch (...) {
    }
    return pCanvas;
}

bool         g_disabledTried = false;
IWzCanvasPtr g_disabled;
IWzCanvas* DisabledSprite() {
    if (!g_disabledTried) {
        g_disabledTried = true;
        g_disabled = LoadCanvas(L"UI/UIWindow.img/Item/disabled");
        if (!g_disabled) g_disabled = LoadCanvas(L"UI/UIWindow.img/item/disabled");
    }
    return g_disabled;
}

void DrawDisabled(IWzCanvas* pCanvas, int x, int yBaseline) {
    IWzCanvas* pd = DisabledSprite();
    if (!pd || !pCanvas) return;
    int sw = 0, sh = 0;
    try {
        sw = static_cast<int>(pd->width);
        sh = static_cast<int>(pd->height);
    } catch (...) {
    }
    if (sw <= 0 || sh <= 0) return;
    const int top = yBaseline - kSlotTopAdjust + kDisabledDY;
    try {
        pCanvas->CopyEx(x + kDisabledDX, top, pd, CANVAS_ALPHATYPE::CA_OVERWRITE,
                        kDisabledSize, kDisabledSize, 0, 0, sw, sh);
    } catch (...) {
    }
}

void DrawSelected(IWzCanvas* pCanvas, int x, int yBaseline) {
    if (!pCanvas) return;
    try {
        pCanvas->DrawRectangle(x + kSelDX, yBaseline - kSelTopAdjust + kSelDY,
                               kSelSize, kSelSize, kSelColor);
    } catch (...) {
    }
}

struct View {
    char* a = nullptr;
    int count = 0;
    int row0 = 0;
};

View ReadView(void* pThis, int offSb, int offArr, int scrollTotal, int visRows) {
    View v;
    void* a = RdPtr(pThis, offArr);
    if (!a) return v;
    int count = *(reinterpret_cast<int*>(a) - 1);
    if (count < 0 || count > 100000) return v;
    v.a = reinterpret_cast<char*>(a);
    v.count = count;
    int total = (scrollTotal >= 0) ? scrollTotal : count;
    int maxRow = MaxRow(total, visRows);
    int curPos = 0;
    void* sb = RdPtr(pThis, offSb);
    if (sb) curPos = RdInt(sb, kOff_SBCurPos);
    if (curPos < 0) curPos = 0;
    if (curPos > maxRow) curPos = maxRow;
    v.row0 = curPos;
    return v;
}

void DrawGrid(void* pThis, IWzCanvas* pCanvas, int offSb, int offArr,
              int x0, int y0, int visRows, int slotCount, int scrollTotal, int selectedIdx) {
    const View v = ReadView(pThis, offSb, offArr, scrollTotal, visRows);
    if (!v.a) return;
    auto* pItemInfo = CItemInfo::GetInstance();
    if (!pItemInfo) return;

    IWzProperty* pFontRaw =
        *reinterpret_cast<IWzProperty**>(reinterpret_cast<char*>(pThis) + kOff_ImgFontNum);
    const int slots = kCols * visRows;

    for (int i = 0; i < slots; ++i) {
        const int index = v.row0 * kCols + i;
        const int x = x0 + (i % kCols) * kPitch;
        const int y = y0 + (i / kCols) * kPitch;

        if (slotCount >= 0 && index >= slotCount) {
            DrawDisabled(pCanvas, x, y);
            continue;
        }
        if (index >= v.count) continue;

        char* item = v.a + index * kItemSize;
        const int nItemID = RdInt(item, kItem_nItemID);
        if (nItemID <= 0) continue;

        if (index == selectedIdx) DrawSelected(pCanvas, x, y);   // highlight behind the icon
        // Guarded per call: DrawItemIconForSlot reaches GetEquipItem, whose loader
        // raises _com_error E_FAIL for an id whose icon node is missing. Unguarded
        // that unwinds out of the dialog's Draw and blanks the rest of the frame.
        try {
            pItemInfo->DrawItemIconForSlot(pCanvas, nItemID, x, y, 0, 0, 0, 0, 0, 0);
        } catch (...) {
            LOG_ONCE("compactstorage: icon draw threw for item %d", nItemID);
        }

        // Quantity digits for USE(2)/SETUP(3)/ETC(4) stacks.
        const int invType = nItemID / 1000000;
        if ((invType == 2 || invType == 3 || invType == 4) && pFontRaw) {
            void* slot = RdPtr(item, kItem_SlotPtr);
            if (slot) {
                int qty = SafeGetQty(slot);
                if (qty > 0) {
                    draw_number_by_image(pCanvas, x + kNumDX, y + kNumDY, qty, pFontRaw, 0);
                }
            }
        }
    }
}

bool HitGrid(void* pThis, int offSb, int offArr, int scrollTotal, int visRows,
             int drawX0, int drawY0, int px, int py, int* pnIdx) {
    const View v = ReadView(pThis, offSb, offArr, scrollTotal, visRows);
    if (!v.a) return false;
    const int hitY0 = drawY0 - kHitYAdjust;
    const int slots = kCols * visRows;
    for (int i = 0; i < slots; ++i) {
        const int index = v.row0 * kCols + i;
        if (index >= v.count) break;
        const int x = drawX0 + (i % kCols) * kPitch;
        const int y = hitY0 + (i / kCols) * kPitch;
        RECT rc;
        SetRect(&rc, x, y, x + kHitSize, y + kHitSize);
        if (PtInRect(&rc, POINT{ static_cast<LONG>(px), static_cast<LONG>(py) })) {
            if (pnIdx) *pnIdx = index;
            return true;
        }
    }
    return false;
}

bool InRegion(int px, int py, int drawX0, int drawY0, int visRows) {
    const int hitY0 = drawY0 - kHitYAdjust;
    return px >= drawX0 && px < drawX0 + kCols * kPitch &&
           py >= hitY0 && py < hitY0 + visRows * kPitch;
}

int GetCapacity(void* pThis) {
    int cap = RdInt(pThis, kOff_GetSlotCount);
    if (cap < 0 || cap > kCols * kRowsGet * 64) cap = -1;
    return cap;
}

// The storage side scrolls over its purchased CAPACITY, so empty-but-owned slots
// stay reachable and draw as empty; the inventory side scrolls over its item count.
int GetScrollTotal(void* pThis) {
    int cap = GetCapacity(pThis);
    return cap >= 0 ? cap : ArrayCount(pThis, kOff_aGetItem);
}

// =====================================================
// THE HOOKS — full replacements, the originals never run
// =====================================================
auto CTrunkDlg__DrawGetItem =
    reinterpret_cast<void(__thiscall*)(void*, IWzCanvas*)>(kAddr_DrawGetItem);
auto CTrunkDlg__DrawPutItem =
    reinterpret_cast<void(__thiscall*)(void*, IWzCanvas*)>(kAddr_DrawPutItem);
auto CTrunkDlg__GetItemIndexFromPoint =
    reinterpret_cast<int(__thiscall*)(void*, unsigned, unsigned, int*, int*)>(kAddr_GetItemIndexFromPoint);
auto CTrunkDlg__SetScrollBar =
    reinterpret_cast<void(__thiscall*)(void*)>(kAddr_SetScrollBar);

void __fastcall CTrunkDlg__DrawGetItem_hook(void* pThis, void* /*edx*/, IWzCanvas* pCanvas) {
    if (!pThis) return;
    int total = GetScrollTotal(pThis);
    SyncScroll(RdPtr(pThis, kOff_SBGet), total, kRowsGet);   // keep the range live
    int cap = GetCapacity(pThis);
    DrawGrid(pThis, pCanvas, kOff_SBGet, kOff_aGetItem, kGetDrawX0, kGetDrawY0, kRowsGet,
             cap, total, RdInt(pThis, kOff_GetSelected));
}

void __fastcall CTrunkDlg__DrawPutItem_hook(void* pThis, void* /*edx*/, IWzCanvas* pCanvas) {
    if (!pThis) return;
    int total = ArrayCount(pThis, kOff_aPutItem);
    SyncScroll(RdPtr(pThis, kOff_SBPut), total, kRowsPut);
    DrawGrid(pThis, pCanvas, kOff_SBPut, kOff_aPutItem, kPutDrawX0, kPutDrawY0, kRowsPut,
             -1, total, RdInt(pThis, kOff_PutSelected));
}

// Returns 1 on hit. *pbBuy: 1 = storage (GET) side, 0 = inventory (PUT) side.
// *pnIdx: absolute item index with the scroll offset already applied — the same
// index space the original produced, which is what keeps every existing
// click/double-click/tooltip path working.
int __fastcall CTrunkDlg__GetItemIndexFromPoint_hook(
    void* pThis, void* /*edx*/, unsigned rx, unsigned ry, int* pbBuy, int* pnIdx) {
    const int px = static_cast<int>(rx), py = static_cast<int>(ry);
    int idx = 0;
    if (InRegion(px, py, kGetDrawX0, kGetDrawY0, kRowsGet)) {
        if (HitGrid(pThis, kOff_SBGet, kOff_aGetItem, GetScrollTotal(pThis), kRowsGet,
                    kGetDrawX0, kGetDrawY0, px, py, &idx)) {
            if (pbBuy) *pbBuy = 1;
            if (pnIdx) *pnIdx = idx;
            return 1;
        }
        return 0;
    }
    if (InRegion(px, py, kPutDrawX0, kPutDrawY0, kRowsPut)) {
        if (HitGrid(pThis, kOff_SBPut, kOff_aPutItem, -1, kRowsPut,
                    kPutDrawX0, kPutDrawY0, px, py, &idx)) {
            if (pbBuy) *pbBuy = 0;
            if (pnIdx) *pnIdx = idx;
            return 1;
        }
        return 0;
    }
    return 0;
}

void __fastcall CTrunkDlg__SetScrollBar_hook(void* pThis, void* /*edx*/) {
    if (!pThis) return;
    SyncScroll(RdPtr(pThis, kOff_SBGet), GetScrollTotal(pThis), kRowsGet);
    SyncScroll(RdPtr(pThis, kOff_SBPut), ArrayCount(pThis, kOff_aPutItem), kRowsPut);
}

} // namespace CompactStorage

void AttachCompactStorageMod() {
    using namespace CompactStorage;
    ATTACH_HOOK(CTrunkDlg__DrawGetItem, CTrunkDlg__DrawGetItem_hook);
    ATTACH_HOOK(CTrunkDlg__DrawPutItem, CTrunkDlg__DrawPutItem_hook);
    ATTACH_HOOK(CTrunkDlg__GetItemIndexFromPoint, CTrunkDlg__GetItemIndexFromPoint_hook);
    ATTACH_HOOK(CTrunkDlg__SetScrollBar, CTrunkDlg__SetScrollBar_hook);
}
