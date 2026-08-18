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
- `build` auto-detects Visual Studio, configures Win32 in an isolated local build directory, builds `injector` and `launcher`, then publishes the selected configuration.
- `check` builds and publishes Debug followed by Release, so Release is left in the client directory.
- `setup` validates `MapleStory.exe` and writes ignored `.kaentake.local.json`.
- Every build command refuses to run while MapleStory is active and publishes exactly `Kaentake.exe` and `Kaentake.dll` to the configured client, verifying SHA-256 hashes.
- For this workspace, keep the configured client fixed at `C:\Users\guill\Desktop\V83\MapleStory`.
- Never copy or alter PDBs or `Custom.wz`.

## Failure handling

Report the failing wrapper command and its concise error. Inspect the generated CMake output only when needed. Do not delete build directories or overwrite client files outside the wrapper.

Use `-WhatIf` only when the user requests a dry run; normal builds are explicitly authorized to publish the EXE and DLL to the configured client.
