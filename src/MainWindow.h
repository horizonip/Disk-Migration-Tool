#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "DriveInfo.h"
#include "FileTree.h"
#include "DestinationTree.h"
#include "Migration.h"
#include "TransferLog.h"

// Control IDs
#define IDC_SOURCE_EDIT     1002
#define IDC_BROWSE_BTN      1003
#define IDC_FILE_TREE       1004
#define IDC_STATUS_LABEL    1005
#define IDC_CAPACITY_BAR    1006
#define IDC_SELECT_ALL      1007
#define IDC_DESELECT_ALL    1008
#define IDC_AUTO_SELECT     1009
#define IDC_COPY_BTN        1010
#define IDC_MOVE_BTN        1011
#define IDC_PROGRESS_BAR    1012
#define IDC_PROGRESS_LABEL  1013
#define IDC_CANCEL_BTN      1014
#define IDC_SPEED_LABEL     1015
#define IDC_VERIFY_CHECK    1016
#define IDC_DEST_TREE       1017
#define IDC_ADD_DRIVE_BTN   1018
#define IDC_REMOVE_DRIVE_BTN 1019

// Custom messages
#define WM_TREE_CHECK_CHANGED (WM_USER + 200)

class MainWindow {
public:
    static bool Register(HINSTANCE hInstance);
    static HWND Create(HINSTANCE hInstance);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static MainWindow* GetInstance(HWND hWnd);

    void OnCreate(HWND hWnd);
    void OnSize(int width, int height);
    void OnCommand(WORD id, WORD code);
    void OnNotify(NMHDR* pnm);
    void OnBrowseFolder();
    void OnSelectAll();
    void OnDeselectAll();
    void OnAutoSelect();
    void OnCopy();
    void OnMove();
    void OnCancel();
    void OnAddDrive();
    void OnRemoveDrive();
    void UpdateStatusBar();
    void SetOperationInProgress(bool inProgress);
    void StartMigration(bool moveMode);

    // Assignment model
    void UpdateAssignments();
    void OnAssignmentsChanged();

    // Message handlers for migration progress
    void OnMigrationProgress(int progress);
    void OnMigrationFile(const wchar_t* filename);
    void OnMigrationComplete(int status);
    void OnMigrationError(const wchar_t* errorMsg);

    HWND hWnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    HFONT hFont_ = nullptr;

    // Controls — left side (source)
    HWND hSourceLabel_ = nullptr;
    HWND hSourceEdit_ = nullptr;
    HWND hBrowseBtn_ = nullptr;
    HWND hTreeView_ = nullptr;

    // Controls — right side (destination)
    HWND hDestLabel_ = nullptr;
    HWND hDestTreeView_ = nullptr;
    HWND hAddDriveBtn_ = nullptr;
    HWND hRemoveDriveBtn_ = nullptr;

    // Controls — bottom (shared)
    HWND hStatusLabel_ = nullptr;
    HWND hCapacityBar_ = nullptr;
    HWND hSelectAllBtn_ = nullptr;
    HWND hDeselectAllBtn_ = nullptr;
    HWND hAutoSelectBtn_ = nullptr;
    HWND hCopyBtn_ = nullptr;
    HWND hMoveBtn_ = nullptr;
    HWND hVerifyCheck_ = nullptr;
    HWND hProgressBar_ = nullptr;
    HWND hProgressLabel_ = nullptr;
    HWND hSpeedLabel_ = nullptr;
    HWND hCancelBtn_ = nullptr;

    // Data
    FileTree fileTree_;
    DestinationTree destTree_;
    Migration migration_;
    TransferLog transferLog_;
    std::wstring exeDir_;
    std::wstring jsonLogPath_;
    ULONGLONG migrationStartTick_ = 0;
    uint64_t migrationTotalBytes_ = 0;

    // Assignment map: relativePath -> driveIndex in destTree_
    std::unordered_map<std::wstring, int> assignments_;
    // File sizes for quick lookup: relativePath -> size
    std::unordered_map<std::wstring, uint64_t> fileSizes_;

    static const wchar_t* CLASS_NAME;
};
