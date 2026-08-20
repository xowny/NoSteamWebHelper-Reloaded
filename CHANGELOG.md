# Changelog

## Unreleased

- Replaced full `steamwebhelper.exe` thread suspension with reversible `BELOW_NORMAL` priority and Windows Efficiency Mode. This avoids Steam IPC stalls that could produce sharp frametime and uncapped-FPS swings.
- Removed the system-wide process snapshot and repeated priority changes that previously ran every four seconds during gameplay. A snapshot is now taken only once when a game starts.
- Restores the original WebHelper priority and power-throttling state when the game exits.
- Connected the runtime monitor to the shared, regression-tested auto-toggle state function.
- Removed worker-thread synchronization from `DllMain` and pin the active proxy for the lifetime of `steam.exe`, avoiding a loader-lock timeout during shutdown.

## 1.1 — 2026-06-25

- Removed dead tray code entirely (ShowTrayMenu, WndProc, TrayThreadProc, g_manualOverride, shellapi.h include). The DLL now runs fully automatic; no tray icon or manual override.
- Updated the auto-disable logic to match current Steam behavior: liveGameProcess check removed, registry RunningAppID + Running flag is the authoritative source.
- Fixed the test to match the updated logic.

## Reloaded

- Fixed current Steam compatibility by turning `umpdc.dll` into a proper proxy that forwards the real Windows `UMPDC` exports through `umpdc_system.dll`.
- Fixed the monitor-thread stack overflow by moving large process snapshots off the stack and onto the heap.
- Limited the DLL logic to the main `steam.exe` process.
- Delayed startup initialization to avoid interfering with Steam's early UI bootstrap.
- Disabled the tray override UI in the compatibility build because that path was unstable on current Steam builds.
