#pragma once

#include "WzLib/zcomdef.h"

#include <memory>
#include <utility>

IUnknownPtr* __cdecl get_unknown_hook(IUnknownPtr* result, Ztl_variant_t& value);

class ZtlVariant final : public Ztl_variant_t {
public:
    using Ztl_variant_t::Ztl_variant_t;

    ZtlVariant(const Ztl_variant_t& value) : Ztl_variant_t(value) {
    }

    ZtlVariant(Ztl_variant_t&& value) : Ztl_variant_t(value.GetVARIANT(), false) {
    }

    [[nodiscard]] int get_int32(int fallback = 0) const {
        if (V_VT(this) == VT_EMPTY || V_VT(this) == VT_ERROR)
            return fallback;

        Ztl_variant_t converted;
        if (FAILED(ZComAPI::ZComVariantChangeType(&converted, const_cast<ZtlVariant*>(this), 0, VT_I4)))
            return fallback;
        return V_I4(&converted);
    }

    [[nodiscard]] IUnknownPtr get_unknown() {
        IUnknownPtr result;
        get_unknown_hook(std::addressof(result), *this);
        return result;
    }

    [[nodiscard]] IUnknownPtr get_unknown() const {
        ZtlVariant copy(*this);
        return copy.get_unknown();
    }

    template <typename ComPtr>
    [[nodiscard]] ComPtr as_com() {
        const IUnknownPtr unknown = get_unknown();
        return unknown ? ComPtr(unknown) : ComPtr{};
    }

    template <typename ComPtr>
    [[nodiscard]] ComPtr as_com() const {
        ZtlVariant copy(*this);
        return copy.template as_com<ComPtr>();
    }
};

static_assert(sizeof(ZtlVariant) == sizeof(Ztl_variant_t));
static_assert(alignof(ZtlVariant) == alignof(Ztl_variant_t));
