# DSplit — Disk Migration Tool

A native Windows utility for splitting file migrations across multiple destination drives. Select a source folder, add destination drives, and DSplit automatically assigns files across drives using a greedy first-fit algorithm. Supports both copy and move operations with high-performance unbuffered I/O, real-time progress, and a persistent JSON transfer log that tracks which files went where.

## Features

- **Multi-drive destinations** — Add multiple destination drives; files overflow from one to the next
- **Split-panel UI** — Source file tree (left) and destination assignment tree (right) side by side
- **Auto-select** — Greedy algorithm fills drives in order, skipping already-transferred files
- **JSON transfer log** — Source-keyed log (`DSplit_{hash}.json`) tracks every file's destination drive serial, enabling instant detection of previously transferred files across sessions
- **High-performance copy** — Files >= 4 MB use unbuffered overlapped double-buffered I/O (16 MB VirtualAlloc buffers, FILE_FLAG_NO_BUFFERING); smaller files use CopyFileEx
- **Verify before delete** — Optional byte-by-byte comparison after cross-volume moves (4 MB buffered reads with FILE_FLAG_SEQUENTIAL_SCAN)
- **Transferred file dimming** — Previously transferred files appear grayed out in the source tree
- **Checkbox propagation** — Checking/unchecking a folder applies to all children; parent state updates automatically
- **Copy or Move** — Background thread operations with per-file progress, speed display, and ETA
- **Cancellation** — Cancel in-progress operations at any time
- **Status bar** — Real-time display of selected, assigned, and available space across all drives

## Screenshot

```
+--[ DSplit - Disk Migration Tool ]---------------------------------------------+
| Source Folder:                  | Destination Drives:       [Add Drive][Remove]|
| [C:\Users\Me\Documents] [Browse]                                              |
|                                 |                                              |
| [x] Photos  (2.4 GB)           | D: [Backup] - 120 GB free (2.1 GB assigned) |
|   [x] vacation  (1.1 GB)       |   Photos\                                   |
|     [x] beach.jpg  (4.2 MB)    |     vacation\                               |
|     [x] sunset.jpg  (3.8 MB)   |       beach.jpg  (4.2 MB)                   |
|   [ ] screenshots  (890 MB)    |       sunset.jpg  (3.8 MB)                  |
| [x] Documents  (650 MB)        |     Documents\                              |
|   [x] report.docx  (1.2 MB)   |       report.docx  (1.2 MB)                 |
|                                 |                                              |
|                                 | E: [Archive] - 45 GB free (890 MB assigned) |
|                                 |   Photos\                                   |
|                                 |     screenshots\                            |
|                                 |       ...                                   |
|                                 |                                              |
|----------------------------------------------------------------------+--------|
| Selected: 3.1 GB | Assigned: 3.0 GB | Available: 165 GB across 2 drives      |
| [===========>                                                        ]        |
|                                                                               |
| [Select All] [Deselect All] [Auto-Select]   [Copy] [Move] [x] Verify        |
+-------------------------------------------------------------------------------+
```

## Building

Requires Visual Studio 2022 (or Build Tools) and CMake 3.15+. No external dependencies beyond the Windows SDK.

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Output: `build/Release/DSplit.exe`

## Project Structure

```
DSplit/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                — Entry point, COM init, message loop
│   ├── MainWindow.h/cpp       — Split-panel layout, drive management, assignment model
│   ├── FileTree.h/cpp         — Source TreeView with checkboxes, auto-select, custom draw
│   ├── DestinationTree.h/cpp  — Display-only TreeView with drive roots and file hierarchy
│   ├── DriveInfo.h/cpp        — Drive enumeration and free space queries
│   ├── Migration.h/cpp        — Multi-dest background copy/move with high-perf I/O
│   ├── TransferLog.h/cpp      — JSON transfer log (source-keyed, FNV-1a hash)
│   └── Utils.h/cpp            — Size formatting, path helpers, JSON escape/unescape
└── resources/
    ├── app.rc                 — Icon and manifest resource
    ├── app.ico                — Application icon
    └── app.manifest           — ComCtl32 v6 visual styles, DPI awareness
```

## How It Works

1. **Browse** for a source folder — the left tree populates with files and sizes
2. **Add Drive** to pick destination drives from a popup menu (source drive filtered out)
3. **Check files** manually or use **Auto-Select** to greedily fill drives in order
4. Files are **assigned** to the first drive with enough free space; the right tree shows assignments per drive
5. **Copy** or **Move** runs on a background thread, posting progress to the UI
6. A **JSON log** is saved every 10 files and on completion, recording each file's destination serial
7. On reopening the same source folder, transferred files appear **grayed out** and are skipped by auto-select

## License

MIT
