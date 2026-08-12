# Building Cemu (Toypad fork) — any PC

Complete, copy-paste build guide for this repo (`Cemu 2.6 Remote Toypad`). Written from an actual successful build on a clean machine (VS 2022, MSVC 14.44, CMake 4.3, Ninja, vcpkg 2026-07-27). Use this for this fork **and** any future modified version of it.

**Rule of thumb:** after every source-code edit you only ever need step 3 (rebuild). Setup is done once.

---

## 0. Prerequisites

| Tool | How to check | Needed for |
|---|---|---|
| Visual Studio 2022 + **"Desktop development with C++"** workload | `where cl` (inside VS prompt) | Compiler |
| CMake >= 3.16 | `cmake --version` | Build system |
| Ninja | `where ninja` | Fast build tool |
| Git | `git --version` | Dependencies |
| Internet | — | vcpkg downloads |

> You do **not** need to ever open the Visual Studio IDE. Everything runs from a command prompt.
> If `where ninja` finds nothing, install via `pip install ninja` or add VS's own Ninja (it ships with the C++ workload).

All commands below must run **from the repo root**. In cmd:

```bat
cd /d C:\full\path\to\this\repo
```

If the path contains a backslash right before a quote (e.g. ending in `\`), quoting breaks. Never write a quoted path that ends in `\`.

---

## 1. One-time setup

### 1a. Populate dependencies

This repo's submodules are sometimes not wired into git (flattened copy). Two paths — try the git way first, fall back to manual clones if any folder stays empty.

```bat
git submodule update --init --recursive
```

Check each of these folders actually has files (not empty):

- `dependencies\vcpkg`
- `dependencies\imgui`
- `dependencies\cubeb`
- `dependencies\Vulkan-Headers`
- `dependencies\ZArchive`

If any is empty, clone them manually (pinned to the commits upstream Cemu currently uses):

```bat
git clone https://github.com/microsoft/vcpkg.git dependencies\vcpkg
git clone https://github.com/ocornut/imgui.git dependencies\imgui
git clone https://github.com/mozilla/cubeb.git dependencies\cubeb
git clone https://github.com/KhronosGroup/Vulkan-Headers.git dependencies\Vulkan-Headers
git clone https://github.com/Exzap/ZArchive.git dependencies\ZArchive
```

Then pin each to the exact commit upstream Cemu references (see §5 for how to find them):

```bat
git -C dependencies\vcpkg checkout master
git -C dependencies\imgui checkout f65bcf481ab34cd07d3909aab1479f409fa79f2f
git -C dependencies\cubeb checkout 2071354a69aca7ed6df3b4222e305746c2113f60
git -C dependencies\Vulkan-Headers checkout 01393c3df0e5285b54ee6527466513f9e614be94
git -C dependencies\ZArchive checkout d2c717730092c7bf8cbb033b12fd4001b7c4d932
```

cubeb has nested submodules that CMake requires — always initialize them:

```bat
git -C dependencies\cubeb submodule update --init --recursive
```

### 1b. Bootstrap vcpkg

```bat
.\dependencies\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

Success looks like: `vcpkg package management program version ...`.

**If it fails with "file is being used by another process" / access denied on `vcpkg.exe`:**
a previous bootstrap left a stuck process holding the file. Kill it and delete the stale exe, then retry:

```bat
taskkill /f /im vcpkg.exe
del /f dependencies\vcpkg\vcpkg.exe
.\dependencies\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

If the download 404s (pkgconf mirror issue), sync vcpkg to a current commit first, then re-bootstrap:

```bat
cd dependencies\vcpkg
git fetch origin
git checkout origin/master
cd ..\..
.\dependencies\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

### 1c. Configure (this is the long step — builds all dependencies)

Use the **x64 Native Tools Command Prompt for VS 2022** (Start menu → Visual Studio 2022 → x64 Native Tools Command Prompt for VS 2022), or load the toolchain manually:

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

(Edition may be `Community`, `Professional`, or `BuildTools` — adjust the path. Verify with `where cl`.)

Then configure. **The `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` flag is required** — the vendored `discord-rpc` uses a CMake version older than 3.5, which modern CMake refuses to run:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
```

> First run downloads/compiles every dependency via vcpkg (SDL2, wxWidgets, Boost, OpenSSL, libusb, …). Expect 15–60 minutes and a few GB. Later runs are fast because results are cached in `build\vcpkg_installed`.

### 1d. Build

```bat
cmake --build build --config Release --parallel
```

Output: `bin\Cemu_release.exe`.

---

## 2. Daily rebuild loop (after editing any source file)

```bat
cd /d C:\full\path\to\this\repo
cmake --build build --config Release --parallel
```

That's it. Re-configure is only needed if `CMakeLists.txt`, `vcpkg.json`, or dependencies change.

---

## 3. Run

```bat
bin\Cemu_release.exe
```

Cemu's window appears; enable the emulated Dimensions Toypad in settings as usual. The toypad TCP listener starts automatically on `127.0.0.1:9191` once the emulated pad attaches.

---

## 4. Clean rebuild (broken/weird state)

```bat
rmdir /s /q build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build --config Release --parallel
```

---

## 5. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `no such file or directory` + `Generator: build tool execution failed, command was:` (empty) | `build\` dir is a partial/failed configure, or you're not in the right repo | `rmdir /s /q build` then run §1c fresh. Make sure the repo has the real submodules (§1a) |
| `Could not find toolchain file ...vcpkg\scripts\buildsystems\vcpkg.cmake` | vcpkg submodule empty | §1a manual clone, §1b bootstrap |
| `CMAKE_CXX_COMPILER not set` | `cl` not on PATH — not using the VS environment | Run from x64 Native Tools prompt, or `call vcvars64.bat` first (§1c) |
| `Compatibility with CMake < 3.5 has been removed` (discord-rpc) | Modern CMake vs old vendored subproject | Always pass `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` |
| `Could not find sanitizers-cmake ... in dependencies\cubeb` | cubeb nested submodules not initialized | `git -C dependencies\cubeb submodule update --init --recursive` |
| `Downloading vcpkg.exe failed` / file locked / `Access denied` on vcpkg.exe | Stuck `vcpkg.exe` process from a prior attempt (often Defender scanning) | `taskkill /f /im vcpkg.exe`, `del /f dependencies\vcpkg\vcpkg.exe`, re-run bootstrap. Wait a few seconds between |
| `git checkout origin/master` → `pathspec ... did not match` | Fresh shallow/empty vcpkg clone, no remote-tracking ref | `git fetch origin` first (or after manual clone just stay on `master`) |
| `\` is not recognized as ... command | cmd quoting — path ending in `\` before the closing quote | Don't end quoted paths with a backslash; use forward slashes or add a space |

---

## 6. Getting the right submodule commits for a future/upstream version

The pinned commits in §1a were taken from upstream Cemu. To refresh them against a newer upstream version, run (any machine with network):

```bat
git ls-remote https://github.com/Exzap/cemu HEAD
```

Then fetch the submodule SHAs from the GitHub tree API for that commit and read the `dependencies\...` entries (look for `"type": "commit"`):

```
https://api.github.com/repos/Exzap/cemu/git/trees/<commit>?recursive=1
```

Or, simplest: clone upstream Cemu fully once and read its gitlinks:

```bat
git clone --depth 1 https://github.com/Exzap/cemu.git %TEMP%\cemu-ref
git -C %TEMP%\cemu-ref ls-tree HEAD dependencies/
```

The lines with mode `160000` are the submodule commits. Note: for `vcpkg` it's safest to stay on `master` (that's what §1b does), not the pinned commit.

---

## 7. Where the toypad modifications live (this fork)

| Area | File(s) |
|---|---|
| Toypad emulation + command replies (incl. 0xE1/0xD0/0xE5/0xFF ack fix) | `src/Cafe/OS/libs/nsyshid/Dimensions.cpp` |
| TCP listener (LegoToypad wire protocol) | `src/Cafe/OS/libs/nsyshid/DimensionsNetworkListener.h/.cpp` |
| Listener lifecycle | `src/Cafe/OS/libs/nsyshid/BackendEmulated.cpp/.h` |
| Controller handoff event (`Local\CemuToypadPickerInputActive`) | `src/input/api/Controller.cpp` |
| Port config | `src/config/CemuConfig.h/.cpp` |
| Protocol / page layout spec | `TOYPAD_TECHNICAL.md` |

The vehicle-ID corruption bug fixed in this build: commands the game uses before writing password-protected tag pages (vehicle-ID region, bytes 140–151 / pages 35–37) previously replied with an all-zero buffer (no `0x55` marker). They now return a proper success ack (`55 02 <seq> 00 <checksum>`) so the game's unlock exchange completes and the write persists. See `Dimensions.cpp` `SendCommand`, cases `0xD0/0xE1/0xE5/0xFF`.
