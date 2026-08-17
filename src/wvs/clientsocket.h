#pragma once
#include "hook.h"
#include "wvs/packet.h"

class CClientSocket : public TSingleton<CClientSocket, 0x00BE7914> {
public:
    void SendPacket(const COutPacket& packet) {
        reinterpret_cast<void(__thiscall*)(CClientSocket*, const COutPacket&)>(
                0x0049637B)(this, packet);
    }
};