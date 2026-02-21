#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

// Custom messages posted from background thread to UI
#define WM_MIGRATION_PROGRESS   (WM_USER + 100)
#define WM_MIGRATION_FILE       (WM_USER + 101)
#define WM_MIGRATION_COMPLETE   (WM_USER + 102)
#define WM_MIGRATION_ERROR      (WM_USER + 103)

struct MigrationItem {
    std::wstring sourcePath;
    std::wstring relativePath;
    bool isDirectory;
};

struct MigrationParams {
    HWND hWndNotify;                    // Window to post progress messages to
    std::wstring destRoot;              // Destination root path
    std::vector<MigrationItem> items;   // Files/folders to process
    bool moveMode;                      // true = move, false = copy
    uint64_t totalBytes;                // Total bytes to transfer
};

class Migration {
public:
    Migration();
    ~Migration();

    // Start migration on a background thread
    bool Start(const MigrationParams& params);

    // Request cancellation
    void Cancel();

    // Check if migration is running
    bool IsRunning() const;

private:
    static DWORD WINAPI ThreadProc(LPVOID param);
    void Run();

    MigrationParams params_;
    HANDLE hThread_ = nullptr;
    std::atomic<bool> cancelled_{ false };
    std::atomic<bool> running_{ false };

    // Progress callback for CopyFileEx
    static DWORD CALLBACK CopyProgressRoutine(
        LARGE_INTEGER totalFileSize,
        LARGE_INTEGER totalBytesTransferred,
        LARGE_INTEGER streamSize,
        LARGE_INTEGER streamBytesTransferred,
        DWORD dwStreamNumber,
        DWORD dwCallbackReason,
        HANDLE hSourceFile,
        HANDLE hDestinationFile,
        LPVOID lpData
    );
};
