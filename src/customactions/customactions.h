#pragma once

class CAvatar;
class CUser;
struct IWzArchive;
struct IWzProperty;

namespace CustomActions {

constexpr int StockActionCount = 162;
constexpr int MaxActionCount = 363;

void Attach();
void OnCustomWzMounted();
void OnPropertySerialized(IWzProperty* property, IWzArchive* archive);
void OnAvatarConstructed(CAvatar* avatar);
bool IsEnabled();
int FindActionCode(const wchar_t* actionName);
bool PlayCustomAction(CAvatar* avatar, int actionCode);
bool PlayCustomAction(CAvatar* avatar, const wchar_t* actionName);
bool SetCustomMovementSet(CUser* user, int movementSetId);
void ClearCustomMovementSet(CUser* user);

} // namespace CustomActions

void AttachCustomActions();
