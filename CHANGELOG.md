# Changelog

## Reloaded

- Fixed current Steam compatibility by turning `umpdc.dll` into a proper proxy that forwards the real Windows `UMPDC` exports through `umpdc_system.dll`.
- Fixed the monitor-thread stack overflow by moving large process snapshots off the stack and onto the heap.
- Limited the DLL logic to the main `steam.exe` process.
- Delayed startup initialization to avoid interfering with Steam's early UI bootstrap.
- Disabled the tray override UI in the compatibility build because that path was unstable on current Steam builds.
- Added a cleaner release layout and updated build packaging for immediate GitHub publishing.
