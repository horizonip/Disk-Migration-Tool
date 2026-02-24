#include "DestinationTree.h"
#include "Utils.h"

DestinationTree::DestinationTree() {}
DestinationTree::~DestinationTree() {}

void DestinationTree::SetTreeView(HWND hTree) {
    hTree_ = hTree;
}

void DestinationTree::AddDrive(const DriveEntry& drive) {
    drives_.push_back(drive);
}

void DestinationTree::RemoveDrive(int index) {
    if (index < 0 || index >= static_cast<int>(drives_.size())) return;
    drives_.erase(drives_.begin() + index);
}

int DestinationTree::GetDriveCount() const {
    return static_cast<int>(drives_.size());
}

const DriveEntry& DestinationTree::GetDrive(int index) const {
    return drives_[index];
}

DriveEntry& DestinationTree::GetDrive(int index) {
    return drives_[index];
}

void DestinationTree::Clear() {
    if (hTree_) {
        TreeView_DeleteAllItems(hTree_);
    }
    drives_.clear();
    driveNodes_.clear();
}

std::wstring DestinationTree::BuildDriveLabel(int index, uint64_t assignedBytes) const {
    if (index < 0 || index >= static_cast<int>(drives_.size())) return L"";
    const auto& d = drives_[index];
    std::wstring label = d.driveLetter;
    if (!d.volumeName.empty()) {
        label += L" [" + d.volumeName + L"]";
    }
    label += L" \u2014 " + Utils::FormatSize(d.freeBytes) + L" free";
    if (assignedBytes > 0) {
        label += L" (" + Utils::FormatSize(assignedBytes) + L" assigned)";
    }
    return label;
}

HTREEITEM DestinationTree::GetDriveNode(int index) const {
    if (index < 0 || index >= static_cast<int>(driveNodes_.size())) return nullptr;
    return driveNodes_[index];
}

uint64_t DestinationTree::GetAssignedBytes(int index,
    const std::unordered_map<std::wstring, int>& assignments,
    const std::unordered_map<std::wstring, uint64_t>& fileSizes) const
{
    uint64_t total = 0;
    for (const auto& [path, driveIdx] : assignments) {
        if (driveIdx == index) {
            auto sizeIt = fileSizes.find(path);
            if (sizeIt != fileSizes.end()) {
                total += sizeIt->second;
            }
        }
    }
    return total;
}

void DestinationTree::InsertPath(int driveIndex, const std::wstring& relativePath,
                                  uint64_t fileSize,
                                  std::unordered_map<std::wstring, HTREEITEM>& folderCache) {
    if (driveIndex < 0 || driveIndex >= static_cast<int>(driveNodes_.size())) return;

    HTREEITEM hDriveRoot = driveNodes_[driveIndex];

    // Split path into components
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start < relativePath.size()) {
        size_t sep = relativePath.find(L'\\', start);
        if (sep == std::wstring::npos) {
            parts.push_back(relativePath.substr(start));
            break;
        }
        parts.push_back(relativePath.substr(start, sep - start));
        start = sep + 1;
    }

    // Create folder nodes for each intermediate component
    HTREEITEM hParent = hDriveRoot;
    std::wstring cumulPath;
    // Prefix with drive index to scope folder cache per drive
    wchar_t drivePrefix[16];
    swprintf_s(drivePrefix, L"%d:", driveIndex);

    for (size_t i = 0; i < parts.size(); i++) {
        if (!cumulPath.empty()) cumulPath += L'\\';
        cumulPath += parts[i];

        std::wstring cacheKey = std::wstring(drivePrefix) + cumulPath;

        bool isLast = (i == parts.size() - 1);

        if (!isLast) {
            // Folder node — look up in cache
            auto it = folderCache.find(cacheKey);
            if (it != folderCache.end()) {
                hParent = it->second;
            } else {
                TVINSERTSTRUCTW tvis = {};
                tvis.hParent = hParent;
                tvis.hInsertAfter = TVI_LAST;
                tvis.item.mask = TVIF_TEXT;
                tvis.item.pszText = const_cast<wchar_t*>(parts[i].c_str());
                HTREEITEM hFolder = TreeView_InsertItem(hTree_, &tvis);
                folderCache[cacheKey] = hFolder;
                hParent = hFolder;
            }
        } else {
            // File leaf node
            std::wstring display = parts[i];
            if (fileSize > 0) {
                display += L"  (" + Utils::FormatSizeShort(fileSize) + L")";
            }
            TVINSERTSTRUCTW tvis = {};
            tvis.hParent = hParent;
            tvis.hInsertAfter = TVI_LAST;
            tvis.item.mask = TVIF_TEXT;
            tvis.item.pszText = const_cast<wchar_t*>(display.c_str());
            TreeView_InsertItem(hTree_, &tvis);
        }
    }
}

void DestinationTree::Rebuild(const std::unordered_map<std::wstring, int>& assignments,
                               const std::unordered_map<std::wstring, uint64_t>& fileSizes) {
    if (!hTree_) return;

    SendMessageW(hTree_, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(hTree_);
    driveNodes_.clear();

    // Create root nodes for each drive
    for (int i = 0; i < static_cast<int>(drives_.size()); i++) {
        uint64_t assigned = GetAssignedBytes(i, assignments, fileSizes);
        std::wstring label = BuildDriveLabel(i, assigned);

        TVINSERTSTRUCTW tvis = {};
        tvis.hParent = TVI_ROOT;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_STATE;
        tvis.item.pszText = const_cast<wchar_t*>(label.c_str());
        tvis.item.state = TVIS_EXPANDED | TVIS_BOLD;
        tvis.item.stateMask = TVIS_EXPANDED | TVIS_BOLD;

        HTREEITEM hDrive = TreeView_InsertItem(hTree_, &tvis);
        driveNodes_.push_back(hDrive);
    }

    // Insert assigned files under their drive nodes
    std::unordered_map<std::wstring, HTREEITEM> folderCache;
    for (const auto& [path, driveIdx] : assignments) {
        uint64_t size = 0;
        auto sizeIt = fileSizes.find(path);
        if (sizeIt != fileSizes.end()) size = sizeIt->second;
        InsertPath(driveIdx, path, size, folderCache);
    }

    // Expand drive root nodes
    for (auto hNode : driveNodes_) {
        TreeView_Expand(hTree_, hNode, TVE_EXPAND);
    }

    SendMessageW(hTree_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree_, nullptr, TRUE);
}
