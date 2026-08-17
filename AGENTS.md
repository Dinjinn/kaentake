# Repository Guidelines

## Project Structure & Module Organization

Kaentake is a Windows x86 C++ launcher and injected DLL for MapleStory v83. `src/` contains both targets: `launcher.cpp` builds the launcher, while `injector.cpp`, hooks, and feature folders such as `autologin/`, `customskills/`, and `damagerank/` build the DLL. Reverse-engineered types live under `src/wvs/`; shared ZTL code is under `src/ztl/`. `src/Custom.wz` is the packaged game asset. Dependencies are Git submodules in `external/`.

## Build, Test, and Development Commands

Initialize submodules after cloning with `git submodule update --init --recursive`. Use the repository wrapper from PowerShell rather than assembling CMake commands manually:

- `.\kaentake.ps1 build` builds Release `injector` and `launcher` targets for Win32.
- `.\kaentake.ps1 build debug` builds the Debug configuration.
- `.\kaentake.ps1 check` builds both configurations and is the primary automated verification.
- `.\kaentake.ps1 setup -GameDir 'C:\path\to\MapleStory'` records an ignored local client path.
- `.\kaentake.ps1 deploy debug -WhatIf` validates deployment without copying files.

Visual Studio with Desktop development with C++ and CMake is required. Do not commit build output.

## Coding Style & Naming Conventions

Format changed C++ with the root `.clang-format`: four-space indentation, no tabs, LLVM-based braces, left-aligned pointers, and preserved include order. Follow existing file and symbol conventions in the affected module; feature directories and files generally use lowercase names, while reconstructed client classes retain names such as `CClientSocket`. Keep hooks and mutable implementation details in `.cpp` files. Preserve x86 ABI details—calling conventions, offsets, sizes, ownership, and patch lengths—and document verified addresses. Use `static_assert` for reconstructed sizes and offsets.

## Testing Guidelines

There is no standalone unit-test suite. Run `.\kaentake.ps1 check` before submitting every change. For hook or gameplay changes, manually test injection and the affected behavior in a disposable clean v83 client; record the scenario and result in the pull request. Never deploy while MapleStory is running.

## Commit & Pull Request Guidelines

Recent commits use short, imperative subjects such as `Fix CUIChannelShift` and `Set CClientSocket::m_tTimeout`. Keep each commit focused and reference an issue or PR when applicable. Pull requests should explain the behavior change, identify ABI/address evidence, list Debug and Release verification, and describe manual client testing. Include screenshots for visible UI changes and call out submodule or `Custom.wz` changes explicitly.

## Configuration & Safety

Do not commit `.kaentake.local.json`, client binaries, logs, dumps, or PDBs. Deployment copies runtime EXE/DLL artifacts and available symbols only; it intentionally does not replace `Custom.wz`.
