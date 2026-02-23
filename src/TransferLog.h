#pragma once
#include <windows.h>
#include <string>
#include <unordered_set>
#include <cstdio>

class TransferLog {
public:
    TransferLog();
    ~TransferLog();

    // Load existing log into memory. Returns true if file existed.
    bool Load(const std::wstring& exeDir, const std::wstring& serialHex);

    // Check if a relative path is in the logged set
    bool Contains(const std::wstring& relativePath) const;

    // Open log file for appending. Writes header if new file.
    bool BeginLogging(const std::wstring& exeDir, const std::wstring& serialHex,
                      const std::wstring& volumeName, const std::wstring& driveLetter);

    // Append a single entry to the open log file
    void AppendEntry(const std::wstring& relativePath, bool isDirectory);

    // Close the log file
    void EndLogging();

    // Add a path to the in-memory exclusion set (for indexing)
    void AddExclusion(const std::wstring& relativePath);

    // Get const ref to the logged paths set
    const std::unordered_set<std::wstring>& GetLoggedPaths() const;

    // Reset all state
    void Clear();

    // Format a DWORD serial as 8-char uppercase hex
    static std::wstring FormatSerial(DWORD serial);

private:
    std::unordered_set<std::wstring> paths_;
    FILE* logFile_ = nullptr;
    std::wstring logFilePath_;
};
