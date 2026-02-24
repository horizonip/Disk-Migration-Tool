#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "DriveInfo.h"

class DestinationTree {
public:
    DestinationTree();
    ~DestinationTree();

    void SetTreeView(HWND hTree);

    // Drive management
    void AddDrive(const DriveEntry& drive);
    void RemoveDrive(int index);
    int GetDriveCount() const;
    const DriveEntry& GetDrive(int index) const;
    DriveEntry& GetDrive(int index);

    // Rebuild the tree from the assignment map
    // assignments: relativePath -> driveIndex
    // fileSizes: relativePath -> size (for display)
    void Rebuild(const std::unordered_map<std::wstring, int>& assignments,
                 const std::unordered_map<std::wstring, uint64_t>& fileSizes);

    // Get the root HTREEITEM for a drive
    HTREEITEM GetDriveNode(int index) const;

    // Sum of assigned file sizes for a specific drive
    uint64_t GetAssignedBytes(int index, const std::unordered_map<std::wstring, int>& assignments,
                              const std::unordered_map<std::wstring, uint64_t>& fileSizes) const;

    // Build display label for a drive: "D: [Backup] - 120 GB free (45 GB assigned)"
    std::wstring BuildDriveLabel(int index, uint64_t assignedBytes) const;

    // Clear the tree and all drives
    void Clear();

private:
    HWND hTree_ = nullptr;
    std::vector<DriveEntry> drives_;
    std::vector<HTREEITEM> driveNodes_;

    // Insert a file path under the correct drive node, creating folder nodes as needed
    void InsertPath(int driveIndex, const std::wstring& relativePath, uint64_t fileSize,
                    std::unordered_map<std::wstring, HTREEITEM>& folderCache);
};
