# Repository Guidelines

## Project Structure & Module Organization

Kaentake is a Windows x86 C++ launcher and injected DLL for MapleStory v83. `src/` contains both targets plus feature folders such as `autologin/`, `customskills/`, and `damagerank/`. Reverse-engineered types live under `src/wvs/`, shared ZTL code under `src/ztl/`, and plugin-owned helpers and logging under `src/util/`. `src/Custom.wz` is the game asset. Dependencies are submodules in `external/`.

## Build, Test, and Development Commands

Initialize submodules after cloning with `git submodule update --init --recursive`. Use the repository wrapper from PowerShell rather than assembling CMake commands manually:

- `.\kaentake.ps1 build` builds Release Win32 and publishes the EXE/DLL to the configured client.
- `.\kaentake.ps1 build debug` builds and publishes Debug.
- `.\kaentake.ps1 check` builds and publishes Debug followed by Release.
- `.\kaentake.ps1 setup -GameDir 'C:\path\to\MapleStory'` records an ignored local client path.
- `.\kaentake.ps1 deploy debug -WhatIf` validates deployment without copying files.

Visual Studio with Desktop development with C++ and CMake is required. Do not commit build output.

## Coding Style & Naming Conventions

Use C++20 and format changed code with the root `.clang-format`: four-space indentation, no tabs, LLVM-based braces, left-aligned pointers, and preserved include order. Feature files generally use lowercase names; reconstructed client classes retain names such as `CClientSocket`. Keep mutable implementation details in `.cpp` files. Preserve x86 ABI details—calling conventions, offsets, sizes, ownership, and patch lengths—and document verified addresses. Use `static_assert` for reconstructed sizes and offsets. Use ZTL types (`ZArray`, `ZMap`, `ZXString`) at client ABI and ownership boundaries; prefer STL for isolated plugin-owned functionality.

## Testing Guidelines

There is no standalone unit-test suite. Run `.\kaentake.ps1 check` before submitting every change. For hook or gameplay changes, manually test injection and the affected behavior in a disposable clean v83 client; record the scenario and result in the pull request. Never deploy while MapleStory is running.

## Commit & Pull Request Guidelines

Recent commits use short, imperative subjects such as `Fix CUIChannelShift` and `Set CClientSocket::m_tTimeout`. Keep each commit focused and reference an issue or PR when applicable. Pull requests should explain the behavior change, identify ABI/address evidence, list Debug and Release verification, and describe manual client testing. Include screenshots for visible UI changes and call out submodule or `Custom.wz` changes explicitly.

## Configuration & Safety

Do not commit `.kaentake.local.json`, client binaries, logs, dumps, or PDBs. Keep the local client set to `C:\Users\guill\Desktop\V83\MapleStory`. Every successful build publishes only `Kaentake.exe` and `Kaentake.dll` there; never copy PDBs or replace `Custom.wz`.

## Agent-Specific Instructions

For DLL, hook, ABI, ZTL, or WzLib changes, follow `.agents/skills/kaentake-dll-development/SKILL.md`. For builds and deployment, follow `.agents/skills/kaentake-build-deploy/SKILL.md`.
