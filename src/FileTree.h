#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

struct FileNode {
    std::wstring name;
    std::wstring fullPath;
    uint64_t size;          // file size, or sum of children for folders
    bool isDirectory;
    std::vector<FileNode> children;
};

class FileTree {
public:
    FileTree();
    ~FileTree();

    // Set the TreeView control handle
    void SetTreeView(HWND hTree);

    // Scan a folder and populate the TreeView
    void Populate(const std::wstring& folderPath);

    // Clear the tree
    void Clear();

    // Handle checkbox toggle notification (TVN_ITEMCHANGED or NM_CLICK)
    void OnCheckChanged(HTREEITEM hItem);

    // Select/deselect all items
    void SelectAll();
    void DeselectAll();

    // Set pointer to transferred paths (for dimming in custom draw)
    void SetTransferredPaths(const std::unordered_map<std::wstring, std::wstring>* transferred);

    // Auto-select items that fit within availableBytes, skipping transferred files
    void AutoSelect(uint64_t availableBytes);

    // Get total size of all checked items
    uint64_t GetSelectedSize() const;

    // Collect full paths of all checked files (not folders)
    struct SelectedFile {
        std::wstring sourcePath;
        std::wstring relativePath;
        uint64_t size;
        bool isDirectory;
    };
    std::vector<SelectedFile> GetSelectedFiles() const;

    // Get the source root folder
    const std::wstring& GetSourceFolder() const { return sourceFolder_; }

    // Get all leaf (non-directory) files in tree order
    struct LeafFile {
        HTREEITEM hItem;
        std::wstring relativePath;
        uint64_t size;
    };
    std::vector<LeafFile> GetAllLeafFiles() const;

    // Public checkbox control
    void SetItemChecked(HTREEITEM hItem, bool checked);

    // Bottom-up parent check propagation after bulk changes
    void PropagateCheckStates();

    // Const reference to the item map
    struct ItemData {
        uint64_t size;
        bool isDirectory;
        std::wstring fullPath;
        std::wstring relativePath;
    };
    const std::unordered_map<HTREEITEM, ItemData>& GetItemMap() const { return itemMap_; }

    // Check if a relative path is transferred (for custom draw)
    bool IsTransferred(const std::wstring& relativePath) const;

private:
    HWND hTree_ = nullptr;
    std::wstring sourceFolder_;
    FileNode root_;

    std::unordered_map<HTREEITEM, ItemData> itemMap_;

    // Internal recursive helpers
    void ScanFolder(const std::wstring& path, FileNode& node);
    HTREEITEM InsertNode(HTREEITEM hParent, const FileNode& node, const std::wstring& relPath);
    void SetCheckState(HTREEITEM hItem, bool checked);
    bool GetCheckState(HTREEITEM hItem) const;
    void SetChildrenCheckState(HTREEITEM hItem, bool checked);
    void UpdateParentCheckState(HTREEITEM hItem);
    void CollectCheckedFiles(HTREEITEM hItem, std::vector<SelectedFile>& files) const;
    uint64_t CalcCheckedSize(HTREEITEM hItem) const;

    // For auto-select: collect all leaf items in tree order
    struct LeafItem {
        HTREEITEM hItem;
        uint64_t size;
        std::wstring relativePath;
    };
    void CollectLeaves(HTREEITEM hItem, std::vector<LeafItem>& leaves) const;

    bool suppressCheckHandling_ = false;
    const std::unordered_map<std::wstring, std::wstring>* transferredPaths_ = nullptr;
};
