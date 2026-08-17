#pragma once

// Dispatch surface of coloringprism.cpp, which owns no Detours of its own.
// The cash-use and drag-drop addresses (0x00A0A63F / 0x00A1DC5B / 0x004863D5 /
// 0x004EF140) belong to fusionanvil.cpp's hooks; those call these instead of
// this feature adding a second Detour on them.
bool ColorPrism_IsPrismItem(int nItemID);
void ColorPrism_OnUse(int nPOS, int nItemID);
// Returns true when the drop was consumed; the caller must then NOT forward it.
bool ColorPrism_HandleItemDrop(void* pTo, int invType, int invPos);
