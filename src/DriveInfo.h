#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

struct DriveEntry {
    std::wstring rootPath;      // e.g. "C:\\"
    std::wstring volumeName;    // e.g. "Local Disk"
    std::wstring driveLetter;   // e.g. "C:"
    DWORD serialNumber = 0;     // Volume serial number
    uint64_t totalBytes;
    uint64_t freeBytes;
    std::wstring displayString; // e.g. "C: [Local Disk] - 45.2 GB free / 256 GB"
};

namespace DriveInfo {

// Enumerate all available fixed/removable drives
std::vector<DriveEntry> EnumerateDrives();

// Refresh free space for a specific drive
bool RefreshDriveSpace(DriveEntry& drive);

} // namespace DriveInfo
