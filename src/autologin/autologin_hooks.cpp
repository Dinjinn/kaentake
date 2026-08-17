#include "../pch.h"
#include "autologin_hooks.h"
#include "autologin.h"

class CUIAvatar {
public:
    MEMBER_HOOK(void, 0x0060599B, SelectCharacter, int index)
};

void CLogin::Update_hook() {
    AutoLogin::BeforeLoginUpdate(this);
    Update(this);
    AutoLogin::AfterLoginUpdate(this);
}

void CLogin::OnWorldInformation_hook(void* packet) {
    OnWorldInformation(this, packet);
    AutoLogin::OnWorldInformation(this);
}

void CUIAvatar::SelectCharacter_hook(int index) {
    SelectCharacter(this, index);
    AutoLogin::OnAvatarSelectCharacter(this, index);
}

void AttachAutoLoginHooks() {
    ATTACH_HOOK(CLogin::Update, CLogin::Update_hook);
    ATTACH_HOOK(CLogin::OnWorldInformation, CLogin::OnWorldInformation_hook);
    ATTACH_HOOK(CUIAvatar::SelectCharacter, CUIAvatar::SelectCharacter_hook);
}
