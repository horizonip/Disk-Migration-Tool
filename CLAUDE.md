# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

Requires Visual Studio 2022 (or Build Tools) and CMake 3.15+. No external dependencies beyond the Windows SDK.

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Output: `build/Release/DSplit.exe`

Kill a running instance before rebuilding: `taskkill //im DSplit.exe //f` (use `//` escaping in Git Bash).

There are no tests or linting tools configured.

## Architecture

DSplit is a single-window Win32 GUI application (C++17) for migrating files between drives. It uses raw Win32 API throughout — no frameworks, no external libraries.

### Entry Point
`main.cpp` — Initializes COM (for IFileDialog), common controls v6, registers the window class, and runs the message loop.

### Core Classes

**MainWindow** (`MainWindow.h/cpp`) — Owns the window procedure and all UI controls. Allocates itself on `WM_CREATE` and stores the pointer via `GWLP_USERDATA`. Handles all user interaction, dispatches to the other classes, and manages two background threads (migration + drive indexing).

**FileTree** (`FileTree.h/cpp`) — Wraps a Win32 TreeView with checkbox support. `Populate()` recursively scans a folder via `FindFirstFile/FindNextFile`, builds a `FileNode` tree, and inserts it into the TreeView. Maintains `itemMap_` (HTREEITEM -> ItemData) for tracking sizes and paths. Checkbox propagation: checking a folder checks all children; unchecking propagates up. `AutoSelect()` uses a greedy algorithm that skips already-transferred files (via `excludedPaths_`).

**Migration** (`Migration.h/cpp`) — Runs copy/move operations on a background thread via `CreateThread`. Two-pass approach: first creates directories, then processes files. Posts `WM_MIGRATION_*` messages to the UI thread for progress updates. Contains `FastCopyFile` — a high-performance unbuffered overlapped double-buffered I/O path for files >= 4MB using `VirtualAlloc` + `FILE_FLAG_NO_BUFFERING`. Files < 4MB use standard `CopyFileExW`.

**TransferLog** (`TransferLog.h/cpp`) — Tracks which files have been transferred to a drive. Log files are named `DSplit_{serial_hex}.log` (UTF-8 with BOM). Stored in two locations: `{exe_dir}\logs\` and the destination drive root. The in-memory set (`paths_`) is used to mark files as already-transferred in the FileTree.

**DriveInfo** (`DriveInfo.h/cpp`) — Namespace with `EnumerateDrives()` (fixed, removable, remote) and `RefreshDriveSpace()`. Each `DriveEntry` includes volume serial number used as the log file key.

**Utils** (`Utils.h/cpp`) — Size formatting (`FormatSize`/`FormatSizeShort`), `CombinePaths`, `EnsureDirectoryExists` (recursive mkdir), `WriteLogLine` (wide-to-UTF-8 conversion via Win32 API).

### Threading Model

Two background threads communicate with the UI via `PostMessageW`:
- **Migration thread**: posts `WM_MIGRATION_PROGRESS` (WM_USER+100), `WM_MIGRATION_FILE` (+101), `WM_MIGRATION_COMPLETE` (+102), `WM_MIGRATION_ERROR` (+103). String messages are heap-allocated (`_wcsdup`) and freed by the UI thread.
- **Drive indexing thread**: scans entire destination drive contents, posts `WM_INDEXING_COMPLETE` (WM_USER+201). Results stored in `MainWindow::driveIndex_`.

Checkbox changes in the TreeView post `WM_TREE_CHECK_CHANGED` (WM_USER+200) to defer handling until after the control has updated its state.

### Performance Details

- Progress updates throttled: only when value changes AND >= 50ms since last post
- File name updates throttled to 80ms intervals
- FastCopyFile uses 16MB VirtualAlloc buffers, 4K sector alignment, pre-allocates destination with SetEndOfFile
- Post-copy: truncates to exact size (unbuffered writes overshoot), copies timestamps + attributes
- Verify-before-delete: 4MB buffered byte comparison with `FILE_FLAG_SEQUENTIAL_SCAN`

## Key Win32 Pitfalls

- `WIN32_LEAN_AND_MEAN` strips COM headers — must `#include <objbase.h>` for `CoInitializeEx`
- `GET_X_LPARAM`/`GET_Y_LPARAM` require `#include <windowsx.h>`
- Win32 `PathCombine` from shlwapi.h conflicts with custom function names — this project uses `Utils::CombinePaths`
- MSVC auto-generates manifests; the `/MANIFEST:NO` linker flag + `.rc` embedding approach is used instead
- Resource icon ID 101 must match `MAKEINTRESOURCEW(101)` in code
- `_wfopen_s` with `ccs=UTF-8` silently fails — use Win32 API (`CreateFileW`/`WriteFile`/`ReadFile`) with manual UTF-8 via `WideCharToMultiByte`/`MultiByteToWideChar`
