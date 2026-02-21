#include "DriveInfo.h"
#include "Utils.h"

namespace DriveInfo {

static std::wstring BuildDisplayString(const DriveEntry& d) {
    std::wstring display = d.driveLetter;
    if (!d.volumeName.empty()) {
        display += L" [" + d.volumeName + L"]";
    }
    display += L" \u2014 " + Utils::FormatSize(d.freeBytes) +
               L" free / " + Utils::FormatSize(d.totalBytes);
    return display;
}

std::vector<DriveEntry> EnumerateDrives() {
    std::vector<DriveEntry> drives;

    wchar_t buffer[512];
    DWORD len = GetLogicalDriveStringsW(512, buffer);
    if (len == 0) return drives;

    for (const wchar_t* p = buffer; *p; p += wcslen(p) + 1) {
        UINT type = GetDriveTypeW(p);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_REMOTE) {
            continue;
        }

        DriveEntry entry;
        entry.rootPath = p;

        // Drive letter (strip trailing backslash)
        entry.driveLetter = entry.rootPath.substr(0, 2);

        // Volume name
        wchar_t volName[MAX_PATH + 1] = {};
        if (GetVolumeInformationW(p, volName, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0)) {
            entry.volumeName = volName;
        }

        // Free space
        ULARGE_INTEGER freeBytesAvailable, totalBytes;
        if (GetDiskFreeSpaceExW(p, &freeBytesAvailable, &totalBytes, nullptr)) {
            entry.freeBytes = freeBytesAvailable.QuadPart;
            entry.totalBytes = totalBytes.QuadPart;
        } else {
            entry.freeBytes = 0;
            entry.totalBytes = 0;
        }

        entry.displayString = BuildDisplayString(entry);
        drives.push_back(std::move(entry));
    }

    return drives;
}

bool RefreshDriveSpace(DriveEntry& drive) {
    ULARGE_INTEGER freeBytesAvailable, totalBytes;
    if (!GetDiskFreeSpaceExW(drive.rootPath.c_str(), &freeBytesAvailable, &totalBytes, nullptr)) {
        return false;
    }
    drive.freeBytes = freeBytesAvailable.QuadPart;
    drive.totalBytes = totalBytes.QuadPart;
    drive.displayString = BuildDisplayString(drive);
    return true;
}

} // namespace DriveInfo
