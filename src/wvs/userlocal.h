#pragma once
#include "hook.h"
#include "ztl/ztl.h"

class CUserLocal : public TSingleton<CUserLocal, 0x00C68754> {
public:
    MEMBER_AT(uint32_t, 0x19E8, m_dwCharacterId)
    MEMBER_AT(ZXString<char>, 0x19EC, m_sCharacterName)
    inline static auto OnKey = reinterpret_cast<void(__thiscall*)(CUserLocal*, unsigned int, unsigned int)>(0x0094C856);

    int32_t GetJobCode() {
        return reinterpret_cast<int32_t(__thiscall*)(CUserLocal*)>(0x00908EB0)(this);
    }
};
