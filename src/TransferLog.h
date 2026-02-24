#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct TransferEntry {
    std::wstring relativePath;
    std::wstring serialHex;  // destination drive serial
    uint64_t size;
};

class TransferLog {
public:
    TransferLog();
    ~TransferLog();

    // Load JSON log from file. Returns true if file existed and parsed.
    bool Load(const std::wstring& logPath);

    // Save all entries to JSON file via CreateFileW/WriteFile.
    bool Save(const std::wstring& logPath) const;

    // Check if a relative path has been transferred
    bool Contains(const std::wstring& relativePath) const;

    // Get the destination serial for a transferred path (empty if not found)
    std::wstring GetSerial(const std::wstring& relativePath) const;

    // Add a new transfer entry
    void AddEntry(const std::wstring& relativePath, const std::wstring& serialHex, uint64_t size);

    // Get all entries
    const std::vector<TransferEntry>& GetEntries() const { return entries_; }

    // Get the path->serial map
    const std::unordered_map<std::wstring, std::wstring>& GetPathMap() const { return pathMap_; }

    // Get/set source path stored in the log
    const std::wstring& GetSourcePath() const { return sourcePath_; }
    void SetSourcePath(const std::wstring& path) { sourcePath_ = path; }

    // Reset all state
    void Clear();

    // Hash a source folder path to 16 hex chars (FNV-1a) for log filename
    static std::wstring HashSourcePath(const std::wstring& path);

    // Format a DWORD serial as 8-char uppercase hex
    static std::wstring FormatSerial(DWORD serial);

    // Build the log file path for a given source folder under exeDir
    static std::wstring GetLogPath(const std::wstring& exeDir, const std::wstring& sourcePath);

private:
    std::wstring sourcePath_;
    std::vector<TransferEntry> entries_;
    std::unordered_map<std::wstring, std::wstring> pathMap_; // relativePath -> serialHex
};
