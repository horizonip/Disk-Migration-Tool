# DSplit — Disk Migration Tool

A native Windows utility for migrating files between drives. Select a destination drive, pick a source folder, and DSplit presents a checkbox tree of files and folders — auto-selecting items that fit in the destination's free space. Supports both copy and move operations with real-time progress feedback.

## Features

- **Drive selection** — Lists all fixed, removable, and network drives with free space info
- **Folder browser** — Modern Windows folder picker (IFileDialog)
- **File tree** — Checkbox TreeView showing the full folder hierarchy with file sizes
- **Auto-select** — Greedy algorithm that checks files until the destination drive is full
- **Checkbox propagation** — Checking/unchecking a folder applies to all children; parent state updates automatically
- **Copy or Move** — Background thread operations using `CopyFileEx` with per-file progress
- **Cancellation** — Cancel in-progress operations at any time
- **Status bar** — Real-time display of selected size vs. available space with a capacity indicator

## Screenshot

```
┌─ DSplit — Disk Migration Tool ─────────────────────────────┐
│ Destination Drive:                                         │
│ [D: [Backup] — 120.5 GB free / 500 GB            ▾]       │
│                                                            │
│ Source Folder:                                             │
│ [C:\Users\Me\Documents                    ] [Browse...]    │
│                                                            │
│ ☑ Photos  (2.4 GB)                                        │
│   ☑ vacation  (1.1 GB)                                    │
│     ☑ beach.jpg  (4.2 MB)                                 │
│     ☑ sunset.jpg  (3.8 MB)                                │
│   ☐ screenshots  (890 MB)                                 │
│ ☑ Documents  (650 MB)                                     │
│                                                            │
│ Selected: 3.1 GB / 120.5 GB available                     │
│ [████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]                │
│                                                            │
│ [Select All] [Deselect All] [Auto-Select]                 │
│                    [Copy to Destination] [Move to Dest.]   │
└────────────────────────────────────────────────────────────┘
```

## Building

Requires Visual Studio 2022 (or Build Tools) and CMake 3.15+.

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The executable is output to `build/Release/DSplit.exe`. No external dependencies — just the Windows SDK.

## Project Structure

```
DSplit/
├── CMakeLists.txt
├── src/
│   ├── main.cpp           — Entry point, COM init, message loop
│   ├── MainWindow.h/cpp   — Window procedure, UI controls, layout
│   ├── DriveInfo.h/cpp    — Drive enumeration and free space queries
│   ├── FileTree.h/cpp     — TreeView population, checkbox logic, auto-select
│   ├── Migration.h/cpp    — Background thread copy/move with progress
│   └── Utils.h/cpp        — Size formatting, path helpers
└── resources/
    ├── app.rc             — Icon and manifest resource
    ├── app.ico            — Application icon
    └── app.manifest       — ComCtl32 v6 visual styles, DPI awareness
```

## License

MIT
