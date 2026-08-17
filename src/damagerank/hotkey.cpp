#include "pch.h"
#include "hook.h"

#include "uiDamageRank.h"
#include "wvs/field.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace {

auto CFieldOnKey = CField::OnKey;

static void AppendF12Log(const char* /*fmt*/, ...) {
    // Logging disabled.
}

void __fastcall CFieldOnKey_hook(
        CField* pThis,
        void* _EDX,
        unsigned int wParam,
        int lParam) {
    const bool isKeyUp = (lParam & 0x80000000) != 0;
    const bool wasAlreadyDown = (lParam & 0x40000000) != 0;

    if (wParam == VK_F12 && !isKeyUp && !wasAlreadyDown) {
        auto& ui = CUIDamageRank::GetInstance();
        const bool wasVisible = ui.IsVisible();

        CUIDamageRank::ToggleByHotkey();

        if (!wasVisible && ui.IsVisible()) {
            CUIDamageRank::PlayUISound(L"MenuUp");
        }
        return;
    }

    CFieldOnKey(pThis, wParam, lParam);
}

} // namespace

void AttachDamageRankHotkey() {
    AppendF12Log("[AttachF12test] begin");
    ATTACH_HOOK(CFieldOnKey, CFieldOnKey_hook);
    AppendF12Log("[AttachF12test] key hook attached");
}
