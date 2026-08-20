# NoSteamWebHelper Reloaded

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)


`NoSteamWebHelper Reloaded` is a maintained compatibility fork of the original `NoSteamWebHelper` DLL mod for current Steam builds.

Original upstream project:

- https://github.com/Aetopia/NoSteamWebHelper

It keeps Steam's browser stack available while idle, then automatically parks `steamwebhelper.exe` in a low-impact mode while Steam is running a game. The current build prioritizes compatibility and stable game frametimes.

## What changed

- Uses a real `umpdc.dll` proxy layer so Steam still gets the Windows `UMPDC` exports it expects.
- Fixes the stack-overflow crash that happened in the original monitor path on current Steam versions.
- Only activates the mod logic inside `steam.exe`.
- Defers startup work until Steam finishes its early browser bootstrap.
- Ships in automatic mode only. The tray toggle is intentionally disabled in this build.
- Keeps WebHelper responsive while gaming, using a lower priority and Windows Efficiency Mode instead of suspending its threads.
- Takes a process snapshot only when a game starts rather than continuously while the game is running.

## Install

1. Close Steam completely.
2. Copy the files from `release/steam` into your Steam install directory beside `steam.exe`.
3. Start Steam normally.

## Repo layout

- `src`: DLL source and export definition.
- `tests`: small regression test for the auto-toggle state logic.
- `release/steam`: ready-to-copy release payload.

## Build

Requirements:

- Visual Studio 2022 Build Tools with x64 C/C++ tools.

Build and test:

```bat
Build.cmd
build\bin\StateTests.exe
```

The build outputs:

- `build\bin\umpdc.dll`
- `build\bin\umpdc_system.dll`
- `build\bin\StateTests.exe`

It also refreshes the release payload in `release\steam`.

## Notes

- This fork currently runs in automatic mode only.
- If you pass `-silent` to Steam, the browser UI will stay out of the way more reliably when Steam restores itself.
