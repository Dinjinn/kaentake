#pragma once
#include "ztl/ztl.h"

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>


template <typename T>
struct TSecData {
    T data;
    unsigned char bKey;
    unsigned char FakePtr1;
    unsigned char FakePtr2;
    unsigned short wChecksum;
};

static_assert(sizeof(TSecData<unsigned char>) == 0x6);
static_assert(sizeof(TSecData<unsigned int>) == 0xC);


template <typename T>
class TSecType {
protected:
    unsigned int FakePtr1;
    unsigned int FakePtr2;
    TSecData<T>* m_secdata;

public:
    explicit TSecType(const T op) : m_secdata(new TSecData<T>()) {
        FakePtr1 = rand();
        FakePtr2 = rand();
        m_secdata->FakePtr1 = FakePtr1;
        m_secdata->FakePtr2 = FakePtr2;
        SetData(op);
    }
    TSecType() : TSecType(0) {
    }
    ~TSecType() {
        delete m_secdata;
    }

    operator T() const {
        return GetData();
    }
    TSecType& operator=(const T& value) {
        SetData(value);
        return *this;
    }

    T GetData() const {
        T tmp;
        unsigned char bKey = m_secdata->bKey;
        unsigned short wChecksum = 0x9A65;
        for (size_t i = 0; i < sizeof(T); ++i) {
            if (!bKey) {
                bKey = 42;
            }
            unsigned char bEnc = reinterpret_cast<unsigned char*>(&m_secdata->data)[i];
            reinterpret_cast<unsigned char*>(&tmp)[i] = bEnc ^ bKey;
            bKey = bEnc + 42 + bKey;
            wChecksum = (wChecksum << 3) | (bKey + (wChecksum >> 13));
        }
        if (wChecksum != m_secdata->wChecksum || (FakePtr1 & 0xFF) != m_secdata->FakePtr1 || (FakePtr1 & 0xFF) != m_secdata->FakePtr1) {
            throw ZException(5);
        }
        return tmp;
    }
    void SetData(const T data) {
        m_secdata->bKey = rand();
        m_secdata->wChecksum = 0x9A65;
        unsigned char bKey = m_secdata->bKey;
        for (size_t i = 0; i < sizeof(T); ++i) {
            if (!bKey) {
                bKey = 42;
            }
            unsigned char bVal = reinterpret_cast<const unsigned char*>(&data)[i];
            reinterpret_cast<unsigned char*>(&m_secdata->data)[i] = bKey ^ bVal;
            bKey = (bKey ^ bVal) + 42 + bKey;
            m_secdata->wChecksum = (m_secdata->wChecksum << 3) | (bKey + (m_secdata->wChecksum >> 13));
        }
    }
};

static_assert(sizeof(TSecType<unsigned char>) == 0xC);
static_assert(sizeof(TSecType<unsigned int>) == 0xC);


inline constexpr std::uint32_t kZtlSecureMask = 0xBAADF00D;

template <typename T>
std::uint32_t __fastcall ZtlSecureTear(T* at, T value) {
    static_assert(std::is_trivially_copyable_v<T>);

    std::uint32_t checksum = kZtlSecureMask;
    if constexpr ((sizeof(T) % sizeof(std::uint32_t)) == 0) {
        constexpr std::size_t blockCount = sizeof(T) / sizeof(std::uint32_t);
        const auto plain = std::bit_cast<std::array<std::uint32_t, blockCount>>(value);
        std::array<std::uint32_t, blockCount> keys{};
        std::array<std::uint32_t, blockCount> encoded{};

        for (std::size_t i = 0; i < blockCount; ++i) {
            keys[i] = static_cast<std::uint32_t>(rand());
            encoded[i] = std::rotr(plain[i] ^ keys[i], 5);
            checksum = encoded[i] + std::rotr(keys[i] ^ checksum, 5);
        }

        std::memcpy(&at[0], keys.data(), sizeof(T));
        std::memcpy(&at[1], encoded.data(), sizeof(T));
    } else {
        constexpr std::size_t blockCount = sizeof(T);
        const auto plain = std::bit_cast<std::array<std::uint8_t, blockCount>>(value);
        std::array<std::uint8_t, blockCount> keys{};
        std::array<std::uint8_t, blockCount> encoded{};

        for (std::size_t i = 0; i < blockCount; ++i) {
            keys[i] = static_cast<std::uint8_t>(rand());
            encoded[i] = plain[i] ^ keys[i];
            checksum = encoded[i] + std::rotr(static_cast<std::uint32_t>(keys[i]) ^ checksum, 5);
        }

        std::memcpy(&at[0], keys.data(), sizeof(T));
        std::memcpy(&at[1], encoded.data(), sizeof(T));
    }
    return checksum;
}

template <typename T>
T __fastcall ZtlSecureFuse(const T* at, std::uint32_t expectedChecksum) {
    static_assert(std::is_trivially_copyable_v<T>);

    std::uint32_t checksum = kZtlSecureMask;
    if constexpr ((sizeof(T) % sizeof(std::uint32_t)) == 0) {
        constexpr std::size_t blockCount = sizeof(T) / sizeof(std::uint32_t);
        std::array<std::uint32_t, blockCount> keys{};
        std::array<std::uint32_t, blockCount> encoded{};
        std::array<std::uint32_t, blockCount> plain{};
        std::memcpy(keys.data(), &at[0], sizeof(T));
        std::memcpy(encoded.data(), &at[1], sizeof(T));

        for (std::size_t i = 0; i < blockCount; ++i) {
            plain[i] = keys[i] ^ std::rotl(encoded[i], 5);
            checksum = encoded[i] + std::rotr(keys[i] ^ checksum, 5);
        }

#ifdef _DEBUG
        assert(checksum == expectedChecksum);
#endif
        return std::bit_cast<T>(plain);
    } else {
        constexpr std::size_t blockCount = sizeof(T);
        std::array<std::uint8_t, blockCount> keys{};
        std::array<std::uint8_t, blockCount> encoded{};
        std::array<std::uint8_t, blockCount> plain{};
        std::memcpy(keys.data(), &at[0], sizeof(T));
        std::memcpy(encoded.data(), &at[1], sizeof(T));

        for (std::size_t i = 0; i < blockCount; ++i) {
            plain[i] = keys[i] ^ encoded[i];
            checksum = encoded[i] + std::rotr(static_cast<std::uint32_t>(keys[i]) ^ checksum, 5);
        }

#ifdef _DEBUG
        assert(checksum == expectedChecksum);
#endif
        return std::bit_cast<T>(plain);
    }
}

template <typename T>
struct ZtlSecure {
    T at[2];
    unsigned int cs;

    operator T() const {
        return ZtlSecureFuse<T>(at, cs);
    }
    ZtlSecure& operator=(const T& value) {
        cs = ZtlSecureTear<T>(at, value);
        return *this;
    }
};

#pragma pack(push, 1)
template <typename T>
struct ZtlSecurePacked {
    T at[2];
    unsigned int cs;

    operator T() const {
        return ZtlSecureFuse<T>(at, cs);
    }
    ZtlSecurePacked& operator=(const T& value) {
        cs = ZtlSecureTear<T>(at, value);
        return *this;
    }
};
#pragma pack(pop)

static_assert(sizeof(ZtlSecure<std::uint8_t>) == 0x8);
static_assert(sizeof(ZtlSecure<std::int16_t>) == 0x8);
static_assert(sizeof(ZtlSecure<std::int32_t>) == 0xC);
static_assert(sizeof(ZtlSecurePacked<std::uint8_t>) == 0x6);
