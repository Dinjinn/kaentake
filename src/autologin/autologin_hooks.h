#pragma once

#include "../hook.h"
#include "../ztl/ztl.h"

class CLogin {
public:
    struct WORLDITEM;
    struct BALLOON;

    MEMBER_AT(int, 0x170, m_bRequestSent)
    MEMBER_HOOK(void, 0x005F4C16, Update)
    MEMBER_HOOK(void, 0x005F95B7, OnWorldInformation, void* packet)
    MEMBER_AT(ZArray<WORLDITEM>, 0x18C, m_aWorldItem)
    MEMBER_AT(ZArray<BALLOON>, 0x204, m_aBalloon)
    MEMBER_HOOK(int, 0x005F6952, SendCheckPasswordPacket, char* sID, char* sPasswd)

};

void AttachAutoLoginHooks();
