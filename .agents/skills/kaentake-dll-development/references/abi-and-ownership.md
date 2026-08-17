# ABI and ownership rules

## Client ABI

- Target 32-bit Windows even when the host compiler is newer.
- Spell every reverse-engineered function type with its verified calling convention.
- Treat addresses, patch sizes, registers, stack cleanup, `sizeof`, and offsets as versioned binary data.
- Prefer `std::byte reserved[N]` for unknown storage. Name a field only after IDA evidence establishes its meaning.
- Do not use inheritance, virtual functions, packing directives, or STL members in reconstructed client types without verified layout evidence.

## Allocation domains

| Value | Allocate/copy with | Release with |
|---|---|---|
| `ZXString<T>` | Existing ZTL string functions | Existing `ZXString<T>` destructor |
| `ZArray`/`ZList`/`ZMap` storage | Existing ZTL allocator | Matching ZTL allocator |
| `Ztl_bstr_t` BSTR | `ZComAPI` / exported ZTL task allocator | `ZComSysFreeString` / exported ZTL task allocator |
| COM interface | WzLib smart pointer | Smart-pointer destruction/reset |
| Ordinary plugin object | Matching C++ allocation domain | Matching C++ delete/destructor |

Never transfer ownership across domains implicitly. An ABI function receiving a pointer is not evidence that it owns it.

## Shared wrappers

- `ComPtr<T>` should alias or wrap the established WzLib COM pointer semantics rather than introduce another reference count.
- `Variant` should share `Ztl_variant_t` behavior, expose `native()` deliberately, and make attach/detach/copy ownership visible.
- `EnumVariantRange` should own the enumerator smart pointer, fetch one value at a time, and terminate on `S_FALSE`. Surface other failed HRESULT values through the repository's established COM error path.
- Iterator adapters must not allocate and must not add members to client-compatible ZTL container layouts.

