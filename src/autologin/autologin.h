#pragma once

namespace AutoLogin {

void LoadConfiguration();
void OnWorldInformation(void* login);
void OnAvatarSelectCharacter(void* avatar, int index);
void BeforeLoginUpdate(void* login);
void AfterLoginUpdate(void* login);

} // namespace AutoLogin
