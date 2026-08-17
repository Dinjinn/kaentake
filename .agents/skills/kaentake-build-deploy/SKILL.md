---
name: kaentake-build-deploy
description: Configure, build, verify, or deploy the Windows x86 Kaentake launcher and injected DLL with the repository PowerShell wrapper. Use when Codex needs short deterministic Kaentake CMake commands, Visual Studio auto-detection, Debug or Release checks, local client setup, or safe runtime artifact deployment.
---

# Kaentake build and deploy

Run commands from the repository root through `kaentake.ps1`. Do not reconstruct long CMake or copy commands unless repairing the wrapper itself.

## Commands

```powershell
.\kaentake.ps1 setup -GameDir 'C:\path\to\MapleStory'
.\kaentake.ps1 build
.\kaentake.ps1 build debug
.\kaentake.ps1 check
.\kaentake.ps1 deploy
.\kaentake.ps1 deploy debug -WhatIf
```

- Omitted configuration means Release.
- `build` auto-detects Visual Studio, configures Win32 in an isolated local build directory, and builds `injector` and `launcher`.
- `check` builds both Debug and Release.
- `setup` validates `MapleStory.exe` and writes ignored `.kaentake.local.json`.
- `deploy` builds first, refuses to run while MapleStory is active, copies only EXE/DLL and available PDBs, then verifies SHA-256 hashes.
- Deployment intentionally does not copy or alter `Custom.wz`.

## Failure handling

Report the failing wrapper command and its concise error. Inspect the generated CMake output only when needed. Do not delete build directories or overwrite client files outside the wrapper.

Use `-WhatIf` for deployment validation when copying into the configured client was not explicitly requested.

