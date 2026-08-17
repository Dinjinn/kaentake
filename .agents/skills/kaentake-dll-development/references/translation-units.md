# Translation-unit boundaries

Use headers as stable interfaces and `.cpp` files as change boundaries.

```cpp
// feature.h
#pragma once
void AttachFeature();
inline constexpr std::uintptr_t kFeaturePatch = 0x00123456;
```

```cpp
// feature.cpp
#include "feature.h"
#include "hook.h"

namespace {
using TargetFn = int(__thiscall*)(ClientType*, int);
TargetFn target = reinterpret_cast<TargetFn>(kFeaturePatch);
}

void AttachFeature() {
    ATTACH_HOOK(target, TargetHook);
}
```

The same rule applies to the global attachment list:

```cpp
// hook.h
void AttachClientHooks();

// hook.cpp
void AttachClientHooks() {
    AttachFeatureA();
    AttachFeatureB();
}
```

Keep these in headers only when required:

- declarations used by another translation unit;
- complete ABI types needed by value;
- templates;
- tiny behavior that must be inline;
- typed `inline constexpr` compile-time constants.

Keep these in `.cpp` files:

- enable/attach call lists;
- hook and patch execution;
- mutable globals and caches;
- non-constexpr tables and strings;
- address-to-function-pointer objects;
- logging implementation;
- feature-private reconstructed types.

Avoid adding volatile project headers to `pch.h`. Use forward declarations, private namespaces, and implementation functions to reduce dependency fan-out.

