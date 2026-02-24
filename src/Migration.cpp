#include "Migration.h"
#include "TransferLog.h"
#include "Utils.h"
#include <string>

// Threshold: files >= 4MB use high-performance unbuffered overlapped copy
static const uint64_t FAST_COPY_THRESHOLD = 4 * 1024 * 1024;
static const DWORD CHUNK_SIZE = 16 * 1024 * 1024; // 16MB per I/O buffer
static const DWORD SECTOR_ALIGN = 4096;            // 4K sector alignment

static const DWORD VERIFY_BUF_SIZE = 4 * 1024 * 1024; // 4MB verify buffer

// Compare source and destination byte-by-byte. Returns true if they match.
static bool VerifyFilesMatch(const std::wstring& srcPath, const std::wstring& dstPath,
                              uint64_t expectedSize, std::atomic<bool>& cancelled) {
    // Quick size check
    WIN32_FILE_ATTRIBUTE_DATA fadSrc, fadDst;
    if (!GetFileAttributesExW(srcPath.c_str(), GetFileExInfoStandard, &fadSrc) ||
        !GetFileAttributesExW(dstPath.c_str(), GetFileExInfoStandard, &fadDst))
        return false;

    uint64_t sizeSrc = (uint64_t(fadSrc.nFileSizeHigh) << 32) | fadSrc.nFileSizeLow;
    uint64_t sizeDst = (uint64_t(fadDst.nFileSizeHigh) << 32) | fadDst.nFileSizeLow;
    if (sizeSrc != sizeDst || sizeSrc != expectedSize)
        return false;

    // Byte-by-byte comparison using large buffered reads
    void* buf1 = VirtualAlloc(nullptr, VERIFY_BUF_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void* buf2 = VirtualAlloc(nullptr, VERIFY_BUF_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf1 || !buf2) {
        if (buf1) VirtualFree(buf1, 0, MEM_RELEASE);
        if (buf2) VirtualFree(buf2, 0, MEM_RELEASE);
        return false;
    }

    HANDLE hSrc = CreateFileW(srcPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    HANDLE hDst = CreateFileW(dstPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

    bool match = true;
    if (hSrc == INVALID_HANDLE_VALUE || hDst == INVALID_HANDLE_VALUE) {
        match = false;
    } else {
        while (!cancelled) {
            DWORD read1 = 0, read2 = 0;
            ReadFile(hSrc, buf1, VERIFY_BUF_SIZE, &read1, nullptr);
            ReadFile(hDst, buf2, VERIFY_BUF_SIZE, &read2, nullptr);
            if (read1 != read2 || memcmp(buf1, buf2, read1) != 0) {
                match = false;
                break;
            }
            if (read1 == 0) break;
        }
    }

    if (hSrc != INVALID_HANDLE_VALUE) CloseHandle(hSrc);
    if (hDst != INVALID_HANDLE_VALUE) CloseHandle(hDst);
    VirtualFree(buf1, 0, MEM_RELEASE);
    VirtualFree(buf2, 0, MEM_RELEASE);
    return match;
}

struct CopyCallbackData {
    Migration* self;
    HWND hWnd;
    uint64_t bytesCopiedBefore; // bytes completed in previous files
    uint64_t totalBytes;
    std::atomic<bool>* cancelled;
    int lastProgress;              // last posted progress value (0-1000)
    ULONGLONG lastPostTime;        // tick of last PostMessage
};

// Post throttled progress update
static void PostProgress(CopyCallbackData* cb, uint64_t bytesInFile) {
    if (!cb) return;
    uint64_t current = cb->bytesCopiedBefore + bytesInFile;
    int progress = cb->totalBytes > 0
        ? static_cast<int>((current * 1000) / cb->totalBytes) : 0;
    ULONGLONG now = GetTickCount64();
    if (progress != cb->lastProgress && (now - cb->lastPostTime >= 50)) {
        PostMessageW(cb->hWnd, WM_MIGRATION_PROGRESS, progress, 0);
        cb->lastProgress = progress;
        cb->lastPostTime = now;
    }
}

// High-performance copy using unbuffered overlapped I/O with double buffering.
static bool FastCopyFile(const std::wstring& src, const std::wstring& dst,
                         uint64_t fileSize, CopyCallbackData* cbData) {
    // Allocate two sector-aligned buffers
    void* buffers[2];
    buffers[0] = VirtualAlloc(nullptr, CHUNK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    buffers[1] = VirtualAlloc(nullptr, CHUNK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffers[0] || !buffers[1]) {
        if (buffers[0]) VirtualFree(buffers[0], 0, MEM_RELEASE);
        if (buffers[1]) VirtualFree(buffers[1], 0, MEM_RELEASE);
        return false;
    }

    // Open source: unbuffered + sequential scan + overlapped
    HANDLE hSrc = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (hSrc == INVALID_HANDLE_VALUE) {
        VirtualFree(buffers[0], 0, MEM_RELEASE);
        VirtualFree(buffers[1], 0, MEM_RELEASE);
        return false;
    }

    // Open destination: unbuffered + overlapped
    HANDLE hDst = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (hDst == INVALID_HANDLE_VALUE) {
        CloseHandle(hSrc);
        VirtualFree(buffers[0], 0, MEM_RELEASE);
        VirtualFree(buffers[1], 0, MEM_RELEASE);
        return false;
    }

    // Pre-allocate destination to reduce fragmentation on HDDs
    LARGE_INTEGER preSize;
    preSize.QuadPart = static_cast<LONGLONG>((fileSize + SECTOR_ALIGN - 1) & ~((uint64_t)SECTOR_ALIGN - 1));
    SetFilePointerEx(hDst, preSize, nullptr, FILE_BEGIN);
    SetEndOfFile(hDst);

    OVERLAPPED ovRead = {}, ovWrite = {};
    ovRead.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ovWrite.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    bool success = true;
    uint64_t readPos = 0;
    uint64_t writePos = 0;
    int curBuf = 0;
    bool writePending = false;

    while (readPos < fileSize && success) {
        if (cbData && *cbData->cancelled) { success = false; break; }

        // Initiate async read into current buffer
        ovRead.Offset = static_cast<DWORD>(readPos);
        ovRead.OffsetHigh = static_cast<DWORD>(readPos >> 32);
        ResetEvent(ovRead.hEvent);

        BOOL readOk = ReadFile(hSrc, buffers[curBuf], CHUNK_SIZE, nullptr, &ovRead);
        if (!readOk && GetLastError() != ERROR_IO_PENDING) {
            success = false;
            break;
        }

        // While read is in flight, wait for previous write to finish
        if (writePending) {
            DWORD bytesWritten;
            if (!GetOverlappedResult(hDst, &ovWrite, &bytesWritten, TRUE)) {
                success = false;
                break;
            }
            writePending = false;
        }

        // Now wait for the read to complete
        DWORD bytesRead;
        if (!GetOverlappedResult(hSrc, &ovRead, &bytesRead, TRUE) || bytesRead == 0) {
            if (readPos < fileSize) success = false;
            break;
        }

        readPos += bytesRead;

        // Round up write size to sector boundary (required for unbuffered I/O)
        DWORD writeSize = (bytesRead + SECTOR_ALIGN - 1) & ~(SECTOR_ALIGN - 1);
        if (writeSize > bytesRead) {
            memset(static_cast<char*>(buffers[curBuf]) + bytesRead, 0, writeSize - bytesRead);
        }

        // Initiate async write from current buffer
        ovWrite.Offset = static_cast<DWORD>(writePos);
        ovWrite.OffsetHigh = static_cast<DWORD>(writePos >> 32);
        ResetEvent(ovWrite.hEvent);

        BOOL writeOk = WriteFile(hDst, buffers[curBuf], writeSize, nullptr, &ovWrite);
        if (!writeOk && GetLastError() != ERROR_IO_PENDING) {
            success = false;
            break;
        }
        writePending = true;
        writePos += writeSize;

        // Swap to other buffer for next iteration
        curBuf = 1 - curBuf;

        // Update progress
        PostProgress(cbData, readPos);
    }

    // Wait for final write to complete
    if (writePending) {
        DWORD bytesWritten;
        if (!GetOverlappedResult(hDst, &ovWrite, &bytesWritten, TRUE)) {
            success = false;
        }
    }

    CloseHandle(ovRead.hEvent);
    CloseHandle(ovWrite.hEvent);
    CloseHandle(hSrc);
    CloseHandle(hDst);

    // Set exact file size (unbuffered writes are sector-padded, may overshoot)
    if (success) {
        HANDLE hFix = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFix != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER liExact;
            liExact.QuadPart = static_cast<LONGLONG>(fileSize);
            SetFilePointerEx(hFix, liExact, nullptr, FILE_BEGIN);
            SetEndOfFile(hFix);
            CloseHandle(hFix);
        }

        // Copy timestamps from source
        HANDLE hSrcInfo = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hSrcInfo != INVALID_HANDLE_VALUE) {
            FILETIME ftCreate, ftAccess, ftWrite;
            if (GetFileTime(hSrcInfo, &ftCreate, &ftAccess, &ftWrite)) {
                HANDLE hDstInfo = CreateFileW(dst.c_str(), FILE_WRITE_ATTRIBUTES, 0,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hDstInfo != INVALID_HANDLE_VALUE) {
                    SetFileTime(hDstInfo, &ftCreate, &ftAccess, &ftWrite);
                    CloseHandle(hDstInfo);
                }
            }
            CloseHandle(hSrcInfo);
        }

        // Copy file attributes
        DWORD attrs = GetFileAttributesW(src.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            SetFileAttributesW(dst.c_str(), attrs);
        }
    } else if (!cbData || !*cbData->cancelled) {
        DeleteFileW(dst.c_str());
    }

    VirtualFree(buffers[0], 0, MEM_RELEASE);
    VirtualFree(buffers[1], 0, MEM_RELEASE);

    // Update progress tracking for caller
    if (success && cbData) {
        cbData->bytesCopiedBefore += fileSize;
    }

    return success;
}

Migration::Migration() {}

Migration::~Migration() {
    Cancel();
    if (hThread_) {
        WaitForSingleObject(hThread_, 5000);
        CloseHandle(hThread_);
    }
}

bool Migration::Start(const MigrationParams& params) {
    if (running_) return false;

    params_ = params;
    cancelled_ = false;
    running_ = true;

    hThread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    if (!hThread_) {
        running_ = false;
        return false;
    }
    return true;
}

void Migration::Cancel() {
    cancelled_ = true;
}

bool Migration::IsRunning() const {
    return running_;
}

DWORD WINAPI Migration::ThreadProc(LPVOID param) {
    auto* self = static_cast<Migration*>(param);
    self->Run();
    return 0;
}

void Migration::Run() {
    uint64_t bytesDone = 0;
    bool hadError = false;

    // Load existing transfer log so we can append
    TransferLog log;
    log.Load(params_.jsonLogPath);
    log.SetSourcePath(params_.sourcePath);

    int saveCounter = 0;

    // First pass: create all necessary directories on each destination drive
    for (auto& item : params_.items) {
        if (cancelled_) break;
        if (!item.isDirectory) continue;
        if (item.destDriveIndex < 0 || item.destDriveIndex >= static_cast<int>(params_.drives.size()))
            continue;

        const auto& drive = params_.drives[item.destDriveIndex];
        std::wstring destPath = Utils::CombinePaths(
            Utils::CombinePaths(drive.rootPath, params_.sourceFolderName),
            item.relativePath);
        Utils::EnsureDirectoryExists(destPath);
    }

    // Second pass: copy/move files
    std::wstring lastVerifiedParent;
    ULONGLONG lastFilePostTime = 0;

    for (auto& item : params_.items) {
        if (cancelled_) break;
        if (item.isDirectory) continue;
        if (item.destDriveIndex < 0 || item.destDriveIndex >= static_cast<int>(params_.drives.size()))
            continue;

        const auto& drive = params_.drives[item.destDriveIndex];
        std::wstring destPath = Utils::CombinePaths(
            Utils::CombinePaths(drive.rootPath, params_.sourceFolderName),
            item.relativePath);

        // Ensure parent directory exists (cached to avoid redundant checks)
        size_t lastSep = destPath.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos) {
            std::wstring parentDir = destPath.substr(0, lastSep);
            if (parentDir != lastVerifiedParent) {
                Utils::EnsureDirectoryExists(parentDir);
                lastVerifiedParent = parentDir;
            }
        }

        // Throttle file name updates
        ULONGLONG now = GetTickCount64();
        if (now - lastFilePostTime >= 80) {
            wchar_t* fileMsg = _wcsdup(item.relativePath.c_str());
            PostMessageW(params_.hWndNotify, WM_MIGRATION_FILE, 0, reinterpret_cast<LPARAM>(fileMsg));
            lastFilePostTime = now;
        }

        CopyCallbackData cbData;
        cbData.self = this;
        cbData.hWnd = params_.hWndNotify;
        cbData.bytesCopiedBefore = bytesDone;
        cbData.totalBytes = params_.totalBytes;
        cbData.cancelled = &cancelled_;
        cbData.lastProgress = -1;
        cbData.lastPostTime = 0;

        BOOL success;
        bool useFastCopy = (item.fileSize >= FAST_COPY_THRESHOLD);

        if (params_.moveMode) {
            // Try MoveFileEx first (same volume = instant rename, no verify needed)
            success = MoveFileExW(item.sourcePath.c_str(), destPath.c_str(),
                MOVEFILE_COPY_ALLOWED);
            if (success) {
                bytesDone += item.fileSize;
                int progress = params_.totalBytes > 0
                    ? static_cast<int>((bytesDone * 1000) / params_.totalBytes) : 0;
                PostMessageW(params_.hWndNotify, WM_MIGRATION_PROGRESS, progress, 0);
            } else {
                // Cross-volume: copy, verify (optional), then delete
                if (useFastCopy) {
                    success = FastCopyFile(item.sourcePath, destPath,
                        item.fileSize, &cbData);
                } else {
                    success = CopyFileExW(item.sourcePath.c_str(), destPath.c_str(),
                        CopyProgressRoutine, &cbData, nullptr, 0);
                }
                if (success) {
                    bytesDone = cbData.bytesCopiedBefore;

                    if (params_.verifyBeforeDelete && !cancelled_) {
                        std::wstring verifyMsg = L"Verifying: " + item.relativePath;
                        wchar_t* vMsg = _wcsdup(verifyMsg.c_str());
                        PostMessageW(params_.hWndNotify, WM_MIGRATION_FILE, 0, reinterpret_cast<LPARAM>(vMsg));

                        if (!VerifyFilesMatch(item.sourcePath, destPath, item.fileSize, cancelled_)) {
                            wchar_t errBuf[512];
                            swprintf_s(errBuf, L"Verify FAILED (source kept): %s",
                                item.relativePath.c_str());
                            wchar_t* errMsg = _wcsdup(errBuf);
                            PostMessageW(params_.hWndNotify, WM_MIGRATION_ERROR, 0, reinterpret_cast<LPARAM>(errMsg));
                            hadError = true;
                            continue;
                        }
                    }

                    DeleteFileW(item.sourcePath.c_str());
                }
            }
        } else {
            if (useFastCopy) {
                success = FastCopyFile(item.sourcePath, destPath,
                    item.fileSize, &cbData);
            } else {
                success = CopyFileExW(item.sourcePath.c_str(), destPath.c_str(),
                    CopyProgressRoutine, &cbData, nullptr, 0);
            }
            if (success) {
                bytesDone = cbData.bytesCopiedBefore;
            }
        }

        // Log successful transfer to JSON
        if (success) {
            log.AddEntry(item.relativePath, drive.serialHex, item.fileSize);
            saveCounter++;
            // Save every 10 files for crash resilience
            if (saveCounter >= 10) {
                log.Save(params_.jsonLogPath);
                saveCounter = 0;
            }
        }

        if (!success && !cancelled_) {
            DWORD err = GetLastError();
            wchar_t errBuf[512];
            swprintf_s(errBuf, L"Error processing: %s\nError code: %lu",
                item.relativePath.c_str(), err);
            hadError = true;

            wchar_t* errMsg = _wcsdup(errBuf);
            PostMessageW(params_.hWndNotify, WM_MIGRATION_ERROR, 0, reinterpret_cast<LPARAM>(errMsg));
        }
    }

    // For move mode, try to remove empty source directories (bottom-up)
    if (params_.moveMode && !cancelled_) {
        for (auto it = params_.items.rbegin(); it != params_.items.rend(); ++it) {
            if (it->isDirectory) {
                RemoveDirectoryW(it->sourcePath.c_str());
            }
        }
    }

    // Final save of the JSON log
    log.Save(params_.jsonLogPath);

    // Signal completion
    PostMessageW(params_.hWndNotify, WM_MIGRATION_COMPLETE,
        cancelled_ ? 1 : (hadError ? 2 : 0), 0);

    running_ = false;
}

DWORD CALLBACK Migration::CopyProgressRoutine(
    LARGE_INTEGER totalFileSize,
    LARGE_INTEGER totalBytesTransferred,
    LARGE_INTEGER /*streamSize*/,
    LARGE_INTEGER /*streamBytesTransferred*/,
    DWORD /*dwStreamNumber*/,
    DWORD /*dwCallbackReason*/,
    HANDLE /*hSourceFile*/,
    HANDLE /*hDestinationFile*/,
    LPVOID lpData)
{
    auto* data = static_cast<CopyCallbackData*>(lpData);

    if (*data->cancelled) {
        return PROGRESS_CANCEL;
    }

    PostProgress(data, totalBytesTransferred.QuadPart);

    // Update bytesCopiedBefore when file completes
    if (totalBytesTransferred.QuadPart == totalFileSize.QuadPart) {
        data->bytesCopiedBefore += totalFileSize.QuadPart;
    }

    return PROGRESS_CONTINUE;
}
