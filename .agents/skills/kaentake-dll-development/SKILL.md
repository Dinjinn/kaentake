---
name: kaentake-dll-development
description: Develop, refactor, diagnose, or review the Windows x86 Kaentake injected DLL and its hooks. Use for Kaentake C++ changes involving client addresses, reverse-engineered layouts, IDA findings, hook attachment, ZTL containers or strings, WzLib COM objects, variants, allocation-sensitive code, logging, or compile-time/rebuild hygiene.
---

# Kaentake DLL development

## Work from repository truth

1. Read the repository `AGENTS.md` and inspect the affected implementation, shared ZTL/WzLib facilities, and CMake target before editing.
2. Preserve the clean v83 x86 ABI: calling convention, pointer width, object size, member offsets, ownership, and hook patch length are invariants.
3. Use IDA for unknown layouts, signatures, xrefs, vtables, and patch boundaries. Do not guess padding or a function prototype. Record the client version and evidence next to the declaration.
4. Read [abi-and-ownership.md](references/abi-and-ownership.md) before changing ZTL, COM, allocation, or client-facing layouts.
5. Read [translation-units.md](references/translation-units.md) before adding a feature, attachment function, shared address, or broadly included header.
6. Use C++20 language and library facilities for plugin-owned implementation code when they improve safety or clarity. Do not let newer standard-library types leak into client ABI layouts.

## Keep changes allocation-aware

- Avoid allocation in hooks, render/update loops, packet paths, and other hot paths.
- Reuse storage. Prefer `std::span`, `std::string_view`, fixed arrays, stack buffers, moves, and caller-owned output when lifetimes permit.
- Use shared `ZXString`, `ZArray`, `ZList`, and `ZMap` facilities. Improve the shared implementation instead of cloning it inside a feature.
- Standardize aliases as `ZXStringA = ZXString<char>` and `ZXStringW = ZXString<wchar_t>`. Assert that `wchar_t` is 16 bits; do not invent `ZXString<char16_t>` without matching client allocator/string support.
- Add non-owning `begin`/`end` or range adapters for ZTL collections without changing their object layout or allocator.
- Do not place STL-owned objects inside client-reconstructed layouts unless IDA proves that layout and ownership.

## Use typed boundaries

- Prefer a reconstructed class with named members and `std::byte` padding over feature-local `void*` arithmetic.
- Centralize typed addresses and calling-convention signatures. Use `inline constexpr std::uintptr_t` for fixed addresses.
- Verify reconstructed types with `static_assert(sizeof(...))` and `static_assert(offsetof(...))`.
- Keep raw casts at the smallest verified interop boundary; expose typed helpers to feature code.

## Preserve COM ownership

- Use WzLib-generated smart pointer types or the shared `ComPtr<T>` facility. Do not manually `AddRef`/`Release` during ordinary ownership.
- Build shared `Variant` behavior on `Ztl_variant_t`; expose the native value explicitly at ABI boundaries and avoid redundant copies.
- Keep BSTR allocation and destruction paired through `ZtlTaskMemAlloc`/`ZtlTaskMemFree` and the existing `ZComAPI`/`Ztl_bstr_t` code. Never free these strings with `delete` or ordinary `SysFreeString`.
- Use a shared allocation-free `EnumVariantRange` input range instead of open-coded `IEnumVARIANT::Next` loops. Define clear HRESULT/end/error behavior and never collect first unless mutation requires it.

## Keep builds incremental

- Put attachment/enable aggregators, hook registration, mutable state, lookup tables, and non-template behavior in `.cpp` files.
- Keep headers limited to declarations, ABI types required by consumers, templates, and genuine `inline constexpr` constants.
- Declare each feature entry point in its small feature header and define it in that feature's `.cpp`.
- Declare the global attachment entry point in a header and define its call list in one `.cpp`; never inline the call list in an umbrella header.
- Keep feature-specific includes and frequently edited declarations out of `pch.h`.

## Log safely

- When logging support is requested, use a pinned `external/spdlog` git submodule and a synchronous logger initialized after `DllMain`.
- Keep spdlog includes and formatting in `.cpp` files when practical. Do not expose spdlog through feature headers.
- Do not log under loader lock or perform verbose formatting in hot hooks. Compile out verbose Release logging.

## Verify

- Format changed C++ with repository rules.
- Build Debug and Release Win32.
- For header-boundary work, change one feature `.cpp` and confirm an incremental build recompiles that translation unit plus the link, not unrelated features.
- Manually exercise injection and the affected game behavior in a disposable v83 client when authorized.
