#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <unordered_map>
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

    // Auto-select items that fit within availableBytes
    void AutoSelect(uint64_t availableBytes);

    // Get total size of all checked items
    uint64_t GetSelectedSize() const;

    // Collect full paths of all checked files (not folders)
    struct SelectedFile {
        std::wstring sourcePath;
        std::wstring relativePath;
        bool isDirectory;
    };
    std::vector<SelectedFile> GetSelectedFiles() const;

    // Get the source root folder
    const std::wstring& GetSourceFolder() const { return sourceFolder_; }

private:
    HWND hTree_ = nullptr;
    std::wstring sourceFolder_;
    FileNode root_;

    // Map TreeView items to file nodes for size tracking
    struct ItemData {
        uint64_t size;
        bool isDirectory;
        std::wstring fullPath;
        std::wstring relativePath;
    };
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
    };
    void CollectLeaves(HTREEITEM hItem, std::vector<LeafItem>& leaves);

    bool suppressCheckHandling_ = false;
};
