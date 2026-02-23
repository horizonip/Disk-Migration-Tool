#include "FileTree.h"
#include "Utils.h"
#include <algorithm>

FileTree::FileTree() {}
FileTree::~FileTree() {}

void FileTree::SetTreeView(HWND hTree) {
    hTree_ = hTree;
}

void FileTree::Populate(const std::wstring& folderPath) {
    Clear();
    sourceFolder_ = folderPath;
    root_.name = folderPath;
    root_.fullPath = folderPath;
    root_.isDirectory = true;
    root_.size = 0;

    SendMessageW(hTree_, WM_SETREDRAW, FALSE, 0);

    ScanFolder(folderPath, root_);

    // Insert items into TreeView
    for (auto& child : root_.children) {
        InsertNode(TVI_ROOT, child, child.name);
    }

    SendMessageW(hTree_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree_, nullptr, TRUE);
}

void FileTree::Clear() {
    if (hTree_) {
        TreeView_DeleteAllItems(hTree_);
    }
    itemMap_.clear();
    root_.children.clear();
    sourceFolder_.clear();
}

void FileTree::ScanFolder(const std::wstring& path, FileNode& node) {
    WIN32_FIND_DATAW fd;
    std::wstring searchPath = path + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<FileNode> folders, files;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        FileNode child;
        child.name = fd.cFileName;
        child.fullPath = Utils::CombinePaths(path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            child.isDirectory = true;
            child.size = 0;
            ScanFolder(child.fullPath, child);
            // Sum children sizes
            for (auto& c : child.children) {
                child.size += c.size;
            }
            folders.push_back(std::move(child));
        } else {
            child.isDirectory = false;
            child.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            files.push_back(std::move(child));
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    // Sort: folders first (alphabetical), then files (alphabetical)
    std::sort(folders.begin(), folders.end(),
        [](const FileNode& a, const FileNode& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    std::sort(files.begin(), files.end(),
        [](const FileNode& a, const FileNode& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });

    for (auto& f : folders) node.children.push_back(std::move(f));
    for (auto& f : files) node.children.push_back(std::move(f));
}

HTREEITEM FileTree::InsertNode(HTREEITEM hParent, const FileNode& node, const std::wstring& relPath) {
    // Build display text: "name (size)"
    std::wstring display = node.name;
    if (node.size > 0 || !node.isDirectory) {
        display += L"  (" + Utils::FormatSizeShort(node.size) + L")";
    }

    TVINSERTSTRUCTW tvis = {};
    tvis.hParent = hParent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_STATE;
    tvis.item.pszText = const_cast<wchar_t*>(display.c_str());
    tvis.item.stateMask = TVIS_STATEIMAGEMASK;
    tvis.item.state = INDEXTOSTATEIMAGEMASK(1); // unchecked

    HTREEITEM hItem = TreeView_InsertItem(hTree_, &tvis);

    // Store item data
    ItemData data;
    data.size = node.size;
    data.isDirectory = node.isDirectory;
    data.fullPath = node.fullPath;
    data.relativePath = relPath;
    itemMap_[hItem] = std::move(data);

    // Insert children
    for (auto& child : node.children) {
        std::wstring childRelPath = relPath + L"\\" + child.name;
        InsertNode(hItem, child, childRelPath);
    }

    return hItem;
}

void FileTree::SetCheckState(HTREEITEM hItem, bool checked) {
    TVITEMW tvi = {};
    tvi.mask = TVIF_HANDLE | TVIF_STATE;
    tvi.hItem = hItem;
    tvi.stateMask = TVIS_STATEIMAGEMASK;
    tvi.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
    TreeView_SetItem(hTree_, &tvi);
}

bool FileTree::GetCheckState(HTREEITEM hItem) const {
    UINT state = TreeView_GetCheckState(hTree_, hItem);
    return state == 1; // TreeView_GetCheckState returns 1 for checked
}

void FileTree::SetChildrenCheckState(HTREEITEM hItem, bool checked) {
    HTREEITEM hChild = TreeView_GetChild(hTree_, hItem);
    while (hChild) {
        SetCheckState(hChild, checked);
        SetChildrenCheckState(hChild, checked);
        hChild = TreeView_GetNextSibling(hTree_, hChild);
    }
}

void FileTree::UpdateParentCheckState(HTREEITEM hItem) {
    HTREEITEM hParent = TreeView_GetParent(hTree_, hItem);
    if (!hParent) return;

    // Check if any child is checked
    bool anyChecked = false;
    HTREEITEM hChild = TreeView_GetChild(hTree_, hParent);
    while (hChild) {
        if (GetCheckState(hChild)) {
            anyChecked = true;
            break;
        }
        hChild = TreeView_GetNextSibling(hTree_, hChild);
    }

    SetCheckState(hParent, anyChecked);
    UpdateParentCheckState(hParent);
}

void FileTree::OnCheckChanged(HTREEITEM hItem) {
    if (suppressCheckHandling_) return;
    suppressCheckHandling_ = true;

    bool checked = GetCheckState(hItem);

    // Propagate to children
    SetChildrenCheckState(hItem, checked);

    // Update parents
    UpdateParentCheckState(hItem);

    suppressCheckHandling_ = false;
}

void FileTree::SelectAll() {
    suppressCheckHandling_ = true;
    SendMessageW(hTree_, WM_SETREDRAW, FALSE, 0);

    HTREEITEM hItem = TreeView_GetRoot(hTree_);
    while (hItem) {
        SetCheckState(hItem, true);
        SetChildrenCheckState(hItem, true);
        hItem = TreeView_GetNextSibling(hTree_, hItem);
    }

    SendMessageW(hTree_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree_, nullptr, TRUE);
    suppressCheckHandling_ = false;
}

void FileTree::DeselectAll() {
    suppressCheckHandling_ = true;
    SendMessageW(hTree_, WM_SETREDRAW, FALSE, 0);

    HTREEITEM hItem = TreeView_GetRoot(hTree_);
    while (hItem) {
        SetCheckState(hItem, false);
        SetChildrenCheckState(hItem, false);
        hItem = TreeView_GetNextSibling(hTree_, hItem);
    }

    SendMessageW(hTree_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree_, nullptr, TRUE);
    suppressCheckHandling_ = false;
}

void FileTree::CollectLeaves(HTREEITEM hItem, std::vector<LeafItem>& leaves) {
    HTREEITEM hChild = TreeView_GetChild(hTree_, hItem);
    if (!hChild) {
        // This is a leaf
        auto it = itemMap_.find(hItem);
        if (it != itemMap_.end() && !it->second.isDirectory) {
            leaves.push_back({ hItem, it->second.size });
        }
        return;
    }
    while (hChild) {
        CollectLeaves(hChild, leaves);
        hChild = TreeView_GetNextSibling(hTree_, hChild);
    }
}

void FileTree::SetExcludedPaths(const std::unordered_set<std::wstring>* excluded) {
    excludedPaths_ = excluded;
}

void FileTree::AutoSelect(uint64_t availableBytes) {
    suppressCheckHandling_ = true;
    SendMessageW(hTree_, WM_SETREDRAW, FALSE, 0);

    // First deselect all
    HTREEITEM hRoot = TreeView_GetRoot(hTree_);
    HTREEITEM hItem = hRoot;
    while (hItem) {
        SetCheckState(hItem, false);
        SetChildrenCheckState(hItem, false);
        hItem = TreeView_GetNextSibling(hTree_, hItem);
    }

    // Collect all leaf files in tree order
    std::vector<LeafItem> leaves;
    hItem = hRoot;
    while (hItem) {
        CollectLeaves(hItem, leaves);
        hItem = TreeView_GetNextSibling(hTree_, hItem);
    }

    // Greedy select until we exceed available space
    uint64_t cumulative = 0;
    for (auto& leaf : leaves) {
        // Skip files already on destination
        if (excludedPaths_) {
            auto it = itemMap_.find(leaf.hItem);
            if (it != itemMap_.end() && excludedPaths_->count(it->second.relativePath)) {
                continue;
            }
        }
        if (cumulative + leaf.size > availableBytes) {
            continue; // skip files that don't fit, try smaller ones
        }
        cumulative += leaf.size;
        SetCheckState(leaf.hItem, true);

        // Check parent folders
        HTREEITEM hParent = TreeView_GetParent(hTree_, leaf.hItem);
        while (hParent) {
            SetCheckState(hParent, true);
            hParent = TreeView_GetParent(hTree_, hParent);
        }
    }

    SendMessageW(hTree_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree_, nullptr, TRUE);
    suppressCheckHandling_ = false;
}

uint64_t FileTree::GetSelectedSize() const {
    uint64_t total = 0;
    for (auto& [hItem, data] : itemMap_) {
        if (!data.isDirectory && GetCheckState(hItem)) {
            total += data.size;
        }
    }
    return total;
}

void FileTree::CollectCheckedFiles(HTREEITEM hItem, std::vector<SelectedFile>& files) const {
    auto it = itemMap_.find(hItem);
    if (it != itemMap_.end() && GetCheckState(hItem)) {
        SelectedFile sf;
        sf.sourcePath = it->second.fullPath;
        sf.relativePath = it->second.relativePath;
        sf.size = it->second.size;
        sf.isDirectory = it->second.isDirectory;
        files.push_back(sf);
    }

    HTREEITEM hChild = TreeView_GetChild(hTree_, hItem);
    while (hChild) {
        CollectCheckedFiles(hChild, files);
        hChild = TreeView_GetNextSibling(hTree_, hChild);
    }
}

std::vector<FileTree::SelectedFile> FileTree::GetSelectedFiles() const {
    std::vector<SelectedFile> files;
    HTREEITEM hItem = TreeView_GetRoot(hTree_);
    while (hItem) {
        CollectCheckedFiles(hItem, files);
        hItem = TreeView_GetNextSibling(hTree_, hItem);
    }
    return files;
}
