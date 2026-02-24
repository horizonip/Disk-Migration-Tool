#include "MainWindow.h"
#include "Utils.h"
#include <windowsx.h>
#include <shobjidl.h>
#include <shlobj.h>

const wchar_t* MainWindow::CLASS_NAME = L"DSplitMainWindow";

static const int MARGIN = 12;
static const int CONTROL_HEIGHT = 24;
static const int BUTTON_HEIGHT = 28;
static const int LABEL_HEIGHT = 18;
static const int SPLITTER_GAP = 12;

// ---------- Window registration & creation ----------

bool MainWindow::Register(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    return RegisterClassExW(&wc) != 0;
}

HWND MainWindow::Create(HINSTANCE hInstance) {
    HWND hWnd = CreateWindowExW(
        0, CLASS_NAME, L"DSplit \u2014 Disk Migration Tool",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 750,
        nullptr, nullptr, hInstance, nullptr);
    return hWnd;
}

MainWindow* MainWindow::GetInstance(HWND hWnd) {
    return reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = GetInstance(hWnd);

    switch (msg) {
    case WM_CREATE: {
        auto* mw = new MainWindow();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(mw));
        mw->OnCreate(hWnd);
        return 0;
    }

    case WM_SIZE:
        if (self) self->OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_COMMAND:
        if (self) self->OnCommand(LOWORD(wParam), HIWORD(wParam));
        return 0;

    case WM_NOTIFY:
        if (self) {
            auto* pnm = reinterpret_cast<NMHDR*>(lParam);
            self->OnNotify(pnm);

            // Handle custom draw for source tree (dim transferred files)
            if (pnm->idFrom == IDC_FILE_TREE && pnm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMTVCUSTOMDRAW*>(lParam);
                switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    HTREEITEM hItem = reinterpret_cast<HTREEITEM>(cd->nmcd.dwItemSpec);
                    auto& itemMap = self->fileTree_.GetItemMap();
                    auto it = itemMap.find(hItem);
                    if (it != itemMap.end() && self->fileTree_.IsTransferred(it->second.relativePath)) {
                        cd->clrText = GetSysColor(COLOR_GRAYTEXT);
                    }
                    return CDRF_DODEFAULT;
                }
                }
            }
        }
        return 0;

    case WM_MIGRATION_PROGRESS:
        if (self) self->OnMigrationProgress(static_cast<int>(wParam));
        return 0;

    case WM_MIGRATION_FILE:
        if (self) {
            auto* filename = reinterpret_cast<wchar_t*>(lParam);
            self->OnMigrationFile(filename);
            free(filename);
        }
        return 0;

    case WM_MIGRATION_COMPLETE:
        if (self) self->OnMigrationComplete(static_cast<int>(wParam));
        return 0;

    case WM_MIGRATION_ERROR:
        if (self) {
            auto* errorMsg = reinterpret_cast<wchar_t*>(lParam);
            self->OnMigrationError(errorMsg);
            free(errorMsg);
        }
        return 0;

    case WM_TREE_CHECK_CHANGED:
        if (self) {
            HTREEITEM hItem = reinterpret_cast<HTREEITEM>(lParam);
            self->fileTree_.OnCheckChanged(hItem);
            self->UpdateAssignments();
        }
        return 0;

    case WM_DESTROY:
        if (self) {
            if (self->hFont_) DeleteObject(self->hFont_);
            delete self;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void MainWindow::OnCreate(HWND hWnd) {
    hWnd_ = hWnd;
    hInstance_ = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hWnd, GWLP_HINSTANCE));

    // Determine exe directory for log storage
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exePathStr(exePath);
    size_t lastSep = exePathStr.find_last_of(L"\\/");
    if (lastSep != std::wstring::npos) {
        exeDir_ = exePathStr.substr(0, lastSep);
    } else {
        exeDir_ = L".";
    }

    // Create font
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    hFont_ = CreateFontIndirectW(&ncm.lfMessageFont);

    auto createCtrl = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) -> HWND {
        HWND h = CreateWindowExW(0, cls, text,
            WS_CHILD | WS_VISIBLE | style,
            0, 0, 0, 0,
            hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            hInstance_, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
        return h;
    };

    // --- Left side: Source ---
    hSourceLabel_ = createCtrl(L"STATIC", L"Source Folder:", SS_LEFT, 0);
    hSourceEdit_ = createCtrl(L"EDIT", L"",
        ES_AUTOHSCROLL | ES_READONLY | WS_BORDER, IDC_SOURCE_EDIT);
    hBrowseBtn_ = createCtrl(L"BUTTON", L"Browse...",
        BS_PUSHBUTTON, IDC_BROWSE_BTN);

    // Source File TreeView
    hTreeView_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_CHECKBOXES,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(IDC_FILE_TREE),
        hInstance_, nullptr);
    SendMessageW(hTreeView_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    fileTree_.SetTreeView(hTreeView_);

    // --- Right side: Destination ---
    hDestLabel_ = createCtrl(L"STATIC", L"Destination Drives:", SS_LEFT, 0);
    hAddDriveBtn_ = createCtrl(L"BUTTON", L"Add Drive",
        BS_PUSHBUTTON, IDC_ADD_DRIVE_BTN);
    hRemoveDriveBtn_ = createCtrl(L"BUTTON", L"Remove",
        BS_PUSHBUTTON, IDC_REMOVE_DRIVE_BTN);

    // Destination TreeView (no checkboxes — display only)
    hDestTreeView_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(IDC_DEST_TREE),
        hInstance_, nullptr);
    SendMessageW(hDestTreeView_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    destTree_.SetTreeView(hDestTreeView_);

    // --- Bottom: shared controls ---
    hStatusLabel_ = createCtrl(L"STATIC", L"Select source folder and add destination drives",
        SS_LEFT, IDC_STATUS_LABEL);
    hCapacityBar_ = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(IDC_CAPACITY_BAR),
        hInstance_, nullptr);
    SendMessageW(hCapacityBar_, PBM_SETRANGE32, 0, 1000);

    // Action buttons
    hSelectAllBtn_ = createCtrl(L"BUTTON", L"Select All",
        BS_PUSHBUTTON, IDC_SELECT_ALL);
    hDeselectAllBtn_ = createCtrl(L"BUTTON", L"Deselect All",
        BS_PUSHBUTTON, IDC_DESELECT_ALL);
    hAutoSelectBtn_ = createCtrl(L"BUTTON", L"Auto-Select",
        BS_PUSHBUTTON, IDC_AUTO_SELECT);
    hCopyBtn_ = createCtrl(L"BUTTON", L"Copy to Destination",
        BS_PUSHBUTTON, IDC_COPY_BTN);
    hMoveBtn_ = createCtrl(L"BUTTON", L"Move to Destination",
        BS_PUSHBUTTON, IDC_MOVE_BTN);
    hVerifyCheck_ = createCtrl(L"BUTTON", L"Verify before delete",
        BS_AUTOCHECKBOX, IDC_VERIFY_CHECK);
    SendMessageW(hVerifyCheck_, BM_SETCHECK, BST_CHECKED, 0);

    // Progress section (hidden by default)
    hProgressBar_ = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | PBS_SMOOTH,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(IDC_PROGRESS_BAR),
        hInstance_, nullptr);
    SendMessageW(hProgressBar_, PBM_SETRANGE32, 0, 1000);

    hProgressLabel_ = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_LEFT | SS_PATHELLIPSIS,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(IDC_PROGRESS_LABEL),
        hInstance_, nullptr);
    SendMessageW(hProgressLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hSpeedLabel_ = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_RIGHT,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(IDC_SPEED_LABEL),
        hInstance_, nullptr);
    SendMessageW(hSpeedLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hCancelBtn_ = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(IDC_CANCEL_BTN),
        hInstance_, nullptr);
    SendMessageW(hCancelBtn_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    // Trigger initial layout
    RECT rc;
    GetClientRect(hWnd, &rc);
    OnSize(rc.right, rc.bottom);
}

void MainWindow::OnSize(int width, int height) {
    if (width == 0 || height == 0) return;

    int contentWidth = width - 2 * MARGIN;
    int halfWidth = (contentWidth - SPLITTER_GAP) / 2;

    int leftX = MARGIN;
    int rightX = MARGIN + halfWidth + SPLITTER_GAP;

    int y = MARGIN;

    // --- Left column: Source ---
    MoveWindow(hSourceLabel_, leftX, y, halfWidth, LABEL_HEIGHT, TRUE);

    // --- Right column: Destination label + buttons ---
    int addBtnWidth = 80;
    int rmBtnWidth = 70;
    int labelWidth = halfWidth - addBtnWidth - rmBtnWidth - 12;
    MoveWindow(hDestLabel_, rightX, y, labelWidth, LABEL_HEIGHT, TRUE);
    MoveWindow(hAddDriveBtn_, rightX + labelWidth + 6, y - 3, addBtnWidth, CONTROL_HEIGHT, TRUE);
    MoveWindow(hRemoveDriveBtn_, rightX + labelWidth + 6 + addBtnWidth + 4, y - 3, rmBtnWidth, CONTROL_HEIGHT, TRUE);

    y += LABEL_HEIGHT + 4;

    // Right side: dest tree starts here (no edit row on right side)
    int destTreeTop = y;

    // Source edit + browse button
    int browseWidth = 80;
    MoveWindow(hSourceEdit_, leftX, y, halfWidth - browseWidth - 6, CONTROL_HEIGHT, TRUE);
    MoveWindow(hBrowseBtn_, leftX + halfWidth - browseWidth, y, browseWidth, CONTROL_HEIGHT, TRUE);
    y += CONTROL_HEIGHT + MARGIN;

    // Calculate remaining space for trees
    int bottomSectionHeight = LABEL_HEIGHT + 4 + 8 + BUTTON_HEIGHT + MARGIN +
                               CONTROL_HEIGHT + LABEL_HEIGHT + MARGIN + MARGIN;
    int treeHeight = height - y - bottomSectionHeight;
    if (treeHeight < 100) treeHeight = 100;

    int destTreeHeight = treeHeight + (y - destTreeTop); // dest tree is taller

    // Source tree (left)
    MoveWindow(hTreeView_, leftX, y, halfWidth, treeHeight, TRUE);

    // Dest tree (right) — starts above source tree since no edit row
    MoveWindow(hDestTreeView_, rightX, destTreeTop, halfWidth, destTreeHeight, TRUE);

    y += treeHeight + MARGIN;

    // --- Full-width bottom section ---
    // Status label + capacity bar
    MoveWindow(hStatusLabel_, MARGIN, y, contentWidth, LABEL_HEIGHT, TRUE);
    y += LABEL_HEIGHT + 4;
    MoveWindow(hCapacityBar_, MARGIN, y, contentWidth, 8, TRUE);
    y += 8 + MARGIN;

    // Action buttons row
    int btnSpacing = 6;
    int bx = MARGIN;
    MoveWindow(hSelectAllBtn_, bx, y, 80, BUTTON_HEIGHT, TRUE);
    bx += 80 + btnSpacing;
    MoveWindow(hDeselectAllBtn_, bx, y, 90, BUTTON_HEIGHT, TRUE);
    bx += 90 + btnSpacing;
    MoveWindow(hAutoSelectBtn_, bx, y, 90, BUTTON_HEIGHT, TRUE);
    bx += 90 + btnSpacing + 20;
    int actionBtnWidth = 140;
    MoveWindow(hCopyBtn_, bx, y, actionBtnWidth, BUTTON_HEIGHT, TRUE);
    bx += actionBtnWidth + btnSpacing;
    MoveWindow(hMoveBtn_, bx, y, actionBtnWidth, BUTTON_HEIGHT, TRUE);
    bx += actionBtnWidth + btnSpacing;
    MoveWindow(hVerifyCheck_, bx, y, 160, BUTTON_HEIGHT, TRUE);
    y += BUTTON_HEIGHT + MARGIN;

    // Progress bar + label + cancel
    int cancelWidth = 70;
    MoveWindow(hProgressBar_, MARGIN, y, contentWidth - cancelWidth - 6, CONTROL_HEIGHT, TRUE);
    MoveWindow(hCancelBtn_, MARGIN + contentWidth - cancelWidth, y, cancelWidth, CONTROL_HEIGHT, TRUE);
    y += CONTROL_HEIGHT + 2;
    int speedWidth = 160;
    MoveWindow(hProgressLabel_, MARGIN, y, contentWidth - speedWidth - 6, LABEL_HEIGHT, TRUE);
    MoveWindow(hSpeedLabel_, MARGIN + contentWidth - speedWidth, y, speedWidth, LABEL_HEIGHT, TRUE);
}

void MainWindow::OnCommand(WORD id, WORD code) {
    switch (id) {
    case IDC_BROWSE_BTN:
        OnBrowseFolder();
        break;
    case IDC_SELECT_ALL:
        OnSelectAll();
        break;
    case IDC_DESELECT_ALL:
        OnDeselectAll();
        break;
    case IDC_AUTO_SELECT:
        OnAutoSelect();
        break;
    case IDC_COPY_BTN:
        OnCopy();
        break;
    case IDC_MOVE_BTN:
        OnMove();
        break;
    case IDC_CANCEL_BTN:
        OnCancel();
        break;
    case IDC_ADD_DRIVE_BTN:
        OnAddDrive();
        break;
    case IDC_REMOVE_DRIVE_BTN:
        OnRemoveDrive();
        break;
    }
}

void MainWindow::OnNotify(NMHDR* pnm) {
    if (pnm->idFrom == IDC_FILE_TREE) {
        if (pnm->code == NM_CLICK || pnm->code == NM_DBLCLK) {
            // Detect checkbox click
            TVHITTESTINFO ht = {};
            DWORD pos = GetMessagePos();
            ht.pt.x = GET_X_LPARAM(pos);
            ht.pt.y = GET_Y_LPARAM(pos);
            ScreenToClient(hTreeView_, &ht.pt);
            HTREEITEM hItem = TreeView_HitTest(hTreeView_, &ht);
            if (hItem && (ht.flags & TVHT_ONITEMSTATEICON)) {
                PostMessageW(hWnd_, WM_TREE_CHECK_CHANGED, 0,
                    reinterpret_cast<LPARAM>(hItem));
            }
        } else if (pnm->code == TVN_KEYDOWN) {
            auto* kd = reinterpret_cast<NMTVKEYDOWN*>(pnm);
            if (kd->wVKey == VK_SPACE) {
                HTREEITEM hItem = TreeView_GetSelection(hTreeView_);
                if (hItem) {
                    PostMessageW(hWnd_, WM_TREE_CHECK_CHANGED, 0,
                        reinterpret_cast<LPARAM>(hItem));
                }
            }
        }
    }
}

// ---------- Drive management ----------

void MainWindow::OnAddDrive() {
    // Enumerate all drives
    auto allDrives = DriveInfo::EnumerateDrives();

    // Filter out already-added drives and the source drive
    std::wstring sourceFolder = fileTree_.GetSourceFolder();
    std::wstring sourceDriveLetter;
    if (sourceFolder.size() >= 2) {
        sourceDriveLetter = sourceFolder.substr(0, 2);
    }

    std::vector<DriveEntry> available;
    for (auto& d : allDrives) {
        // Skip source drive
        if (!sourceDriveLetter.empty() &&
            _wcsicmp(d.driveLetter.c_str(), sourceDriveLetter.c_str()) == 0)
            continue;

        // Skip already-added drives
        bool alreadyAdded = false;
        for (int i = 0; i < destTree_.GetDriveCount(); i++) {
            if (destTree_.GetDrive(i).serialNumber == d.serialNumber &&
                destTree_.GetDrive(i).driveLetter == d.driveLetter) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) {
            available.push_back(d);
        }
    }

    if (available.empty()) {
        MessageBoxW(hWnd_, L"No additional drives available.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Show popup menu at button location
    HMENU hMenu = CreatePopupMenu();
    for (size_t i = 0; i < available.size(); i++) {
        AppendMenuW(hMenu, MF_STRING, 1000 + i, available[i].displayString.c_str());
    }

    RECT btnRect;
    GetWindowRect(hAddDriveBtn_, &btnRect);
    int sel = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
        btnRect.left, btnRect.bottom, hWnd_, nullptr);
    DestroyMenu(hMenu);

    if (sel >= 1000 && sel < 1000 + static_cast<int>(available.size())) {
        destTree_.AddDrive(available[sel - 1000]);
        UpdateAssignments();
    }
}

void MainWindow::OnRemoveDrive() {
    if (destTree_.GetDriveCount() == 0) return;

    // Find which drive is selected in the dest tree
    HTREEITEM hSel = TreeView_GetSelection(hDestTreeView_);
    if (!hSel) {
        MessageBoxW(hWnd_, L"Select a drive in the destination tree to remove.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Walk up to find the drive root node
    HTREEITEM hRoot = hSel;
    HTREEITEM hParent;
    while ((hParent = TreeView_GetParent(hDestTreeView_, hRoot)) != nullptr) {
        hRoot = hParent;
    }

    // Find drive index
    int driveIndex = -1;
    for (int i = 0; i < destTree_.GetDriveCount(); i++) {
        if (destTree_.GetDriveNode(i) == hRoot) {
            driveIndex = i;
            break;
        }
    }

    if (driveIndex < 0) return;

    // Confirm removal
    std::wstring msg = L"Remove drive " + destTree_.GetDrive(driveIndex).driveLetter + L"?";
    if (MessageBoxW(hWnd_, msg.c_str(), L"DSplit", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    // Clear assignments for this drive, re-index remaining
    std::unordered_map<std::wstring, int> newAssignments;
    for (auto& [path, idx] : assignments_) {
        if (idx == driveIndex) continue;
        int newIdx = (idx > driveIndex) ? idx - 1 : idx;
        newAssignments[path] = newIdx;
    }
    assignments_ = std::move(newAssignments);

    destTree_.RemoveDrive(driveIndex);
    OnAssignmentsChanged();
}

// ---------- Assignment model ----------

void MainWindow::UpdateAssignments() {
    assignments_.clear();
    fileSizes_.clear();

    int driveCount = destTree_.GetDriveCount();
    if (driveCount == 0) {
        OnAssignmentsChanged();
        return;
    }

    // Get checked files from source tree
    auto selectedFiles = fileTree_.GetSelectedFiles();

    // Build file sizes map
    for (auto& f : selectedFiles) {
        if (!f.isDirectory) {
            fileSizes_[f.relativePath] = f.size;
        }
    }

    // Track available space per drive
    std::vector<uint64_t> available(driveCount);
    for (int i = 0; i < driveCount; i++) {
        available[i] = destTree_.GetDrive(i).freeBytes;
    }

    // Assign files to drives: skip transferred, assign to first drive with room
    for (auto& f : selectedFiles) {
        if (f.isDirectory) continue;

        // Skip already transferred
        if (transferLog_.Contains(f.relativePath)) continue;

        // Find first drive with enough space
        for (int i = 0; i < driveCount; i++) {
            if (f.size <= available[i]) {
                assignments_[f.relativePath] = i;
                available[i] -= f.size;
                break;
            }
        }
    }

    OnAssignmentsChanged();
}

void MainWindow::OnAssignmentsChanged() {
    destTree_.Rebuild(assignments_, fileSizes_);
    UpdateStatusBar();
}

// ---------- Event handlers ----------

void MainWindow::OnBrowseFolder() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) return;

    IFileDialog* pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
        IID_IFileDialog, reinterpret_cast<void**>(&pfd));
    if (SUCCEEDED(hr)) {
        DWORD options;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        pfd->SetTitle(L"Select Source Folder");

        hr = pfd->Show(hWnd_);
        if (SUCCEEDED(hr)) {
            IShellItem* psi = nullptr;
            hr = pfd->GetResult(&psi);
            if (SUCCEEDED(hr)) {
                wchar_t* path = nullptr;
                hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(hr) && path) {
                    SetWindowTextW(hSourceEdit_, path);

                    // Clear assignments when source changes
                    assignments_.clear();
                    fileSizes_.clear();

                    // Load JSON transfer log for this source
                    jsonLogPath_ = TransferLog::GetLogPath(exeDir_, path);
                    transferLog_.Clear();
                    transferLog_.Load(jsonLogPath_);
                    transferLog_.SetSourcePath(path);

                    // Set transferred paths for dimming
                    fileTree_.SetTransferredPaths(&transferLog_.GetPathMap());

                    // Populate source tree
                    fileTree_.Populate(path);

                    UpdateAssignments();
                    CoTaskMemFree(path);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    CoUninitialize();
}

void MainWindow::OnSelectAll() {
    fileTree_.SelectAll();
    UpdateAssignments();
}

void MainWindow::OnDeselectAll() {
    fileTree_.DeselectAll();
    UpdateAssignments();
}

void MainWindow::OnAutoSelect() {
    int driveCount = destTree_.GetDriveCount();
    if (driveCount == 0) {
        MessageBoxW(hWnd_, L"Please add at least one destination drive.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Calculate total available across all drives
    uint64_t totalAvailable = 0;
    for (int i = 0; i < driveCount; i++) {
        totalAvailable += destTree_.GetDrive(i).freeBytes;
    }

    // Deselect all first
    fileTree_.DeselectAll();

    // Get all leaf files
    auto leaves = fileTree_.GetAllLeafFiles();

    // Track available space per drive
    std::vector<uint64_t> available(driveCount);
    for (int i = 0; i < driveCount; i++) {
        available[i] = destTree_.GetDrive(i).freeBytes;
    }

    // Greedy fill across drives
    SendMessageW(hTreeView_, WM_SETREDRAW, FALSE, 0);

    for (auto& leaf : leaves) {
        // Skip transferred
        if (transferLog_.Contains(leaf.relativePath)) continue;

        // Find first drive with space
        for (int i = 0; i < driveCount; i++) {
            if (leaf.size <= available[i]) {
                available[i] -= leaf.size;
                fileTree_.SetItemChecked(leaf.hItem, true);
                break;
            }
        }
    }

    fileTree_.PropagateCheckStates();

    SendMessageW(hTreeView_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTreeView_, nullptr, TRUE);

    UpdateAssignments();
}

void MainWindow::OnCopy() {
    StartMigration(false);
}

void MainWindow::OnMove() {
    int ret = MessageBoxW(hWnd_,
        L"Move will delete source files after copying. Continue?",
        L"Confirm Move", MB_YESNO | MB_ICONWARNING);
    if (ret == IDYES) {
        StartMigration(true);
    }
}

void MainWindow::OnCancel() {
    migration_.Cancel();
}

void MainWindow::StartMigration(bool moveMode) {
    if (migration_.IsRunning()) {
        MessageBoxW(hWnd_, L"A migration is already in progress.",
            L"DSplit", MB_OK | MB_ICONWARNING);
        return;
    }

    if (destTree_.GetDriveCount() == 0) {
        MessageBoxW(hWnd_, L"Please add at least one destination drive.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (assignments_.empty()) {
        MessageBoxW(hWnd_, L"No files assigned to destination drives.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Build source folder name
    std::wstring sourceFolder = fileTree_.GetSourceFolder();
    std::wstring folderName;
    size_t lastSep = sourceFolder.find_last_of(L"\\/");
    if (lastSep != std::wstring::npos) {
        folderName = sourceFolder.substr(lastSep + 1);
    } else {
        folderName = sourceFolder;
    }

    MigrationParams params;
    params.hWndNotify = hWnd_;
    params.sourcePath = sourceFolder;
    params.sourceFolderName = folderName;
    params.moveMode = moveMode;
    params.verifyBeforeDelete = moveMode &&
        (SendMessageW(hVerifyCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    params.jsonLogPath = jsonLogPath_;

    // Build drives list
    for (int i = 0; i < destTree_.GetDriveCount(); i++) {
        const auto& d = destTree_.GetDrive(i);
        DestinationDriveInfo ddi;
        ddi.rootPath = d.rootPath;
        ddi.serialHex = TransferLog::FormatSerial(d.serialNumber);
        ddi.volumeName = d.volumeName;
        ddi.driveLetter = d.driveLetter;
        params.drives.push_back(std::move(ddi));
    }

    // Build items from selected files + assignments
    // First pass: collect all checked items (dirs + files)
    auto selectedFiles = fileTree_.GetSelectedFiles();

    uint64_t totalBytes = 0;
    for (auto& f : selectedFiles) {
        MigrationItem item;
        item.sourcePath = f.sourcePath;
        item.relativePath = f.relativePath;
        item.fileSize = f.size;
        item.isDirectory = f.isDirectory;

        if (f.isDirectory) {
            // Directories go to all drives that have files in them
            // We'll set driveIndex = 0 and handle dir creation for all drives
            item.destDriveIndex = -1; // will be handled specially in migration
            // Actually, we need to create dirs on all destination drives.
            // Add a dir item for each drive that has files under this dir.
            std::unordered_set<int> drivesForDir;
            for (auto& [path, idx] : assignments_) {
                // Check if path starts with this dir's relative path
                if (path.size() > f.relativePath.size() &&
                    path[f.relativePath.size()] == L'\\' &&
                    _wcsnicmp(path.c_str(), f.relativePath.c_str(), f.relativePath.size()) == 0) {
                    drivesForDir.insert(idx);
                }
            }
            for (int driveIdx : drivesForDir) {
                MigrationItem dirItem;
                dirItem.sourcePath = f.sourcePath;
                dirItem.relativePath = f.relativePath;
                dirItem.fileSize = 0;
                dirItem.isDirectory = true;
                dirItem.destDriveIndex = driveIdx;
                params.items.push_back(std::move(dirItem));
            }
            continue;
        }

        // Look up assignment for this file
        auto assignIt = assignments_.find(f.relativePath);
        if (assignIt == assignments_.end()) continue; // not assigned (transferred or no room)

        item.destDriveIndex = assignIt->second;
        totalBytes += f.size;
        params.items.push_back(std::move(item));
    }

    if (params.items.empty()) {
        MessageBoxW(hWnd_, L"No files to transfer.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
        return;
    }

    params.totalBytes = totalBytes;
    migrationTotalBytes_ = totalBytes;
    SetOperationInProgress(true);
    migration_.Start(params);
}

void MainWindow::UpdateStatusBar() {
    uint64_t selected = fileTree_.GetSelectedSize();
    uint64_t assigned = 0;
    for (auto& [path, idx] : assignments_) {
        auto sizeIt = fileSizes_.find(path);
        if (sizeIt != fileSizes_.end()) {
            assigned += sizeIt->second;
        }
    }

    uint64_t totalAvailable = 0;
    int driveCount = destTree_.GetDriveCount();
    for (int i = 0; i < driveCount; i++) {
        totalAvailable += destTree_.GetDrive(i).freeBytes;
    }

    std::wstring status = L"Selected: " + Utils::FormatSize(selected) +
                          L" | Assigned: " + Utils::FormatSize(assigned) +
                          L" | Available: " + Utils::FormatSize(totalAvailable) +
                          L" across " + std::to_wstring(driveCount) + L" drive";
    if (driveCount != 1) status += L"s";
    SetWindowTextW(hStatusLabel_, status.c_str());

    // Update capacity bar based on assigned vs total available
    int pct = 0;
    if (totalAvailable > 0) {
        pct = static_cast<int>((assigned * 1000) / totalAvailable);
        if (pct > 1000) pct = 1000;
    }
    SendMessageW(hCapacityBar_, PBM_SETPOS, pct, 0);

    if (assigned > totalAvailable && totalAvailable > 0) {
        SendMessageW(hCapacityBar_, PBM_SETBARCOLOR, 0, RGB(220, 50, 50));
    } else {
        SendMessageW(hCapacityBar_, PBM_SETBARCOLOR, 0, RGB(60, 160, 60));
    }
}

void MainWindow::SetOperationInProgress(bool inProgress) {
    ShowWindow(hProgressBar_, inProgress ? SW_SHOW : SW_HIDE);
    ShowWindow(hProgressLabel_, inProgress ? SW_SHOW : SW_HIDE);
    ShowWindow(hSpeedLabel_, inProgress ? SW_SHOW : SW_HIDE);
    ShowWindow(hCancelBtn_, inProgress ? SW_SHOW : SW_HIDE);

    EnableWindow(hBrowseBtn_, !inProgress);
    EnableWindow(hSelectAllBtn_, !inProgress);
    EnableWindow(hDeselectAllBtn_, !inProgress);
    EnableWindow(hAutoSelectBtn_, !inProgress);
    EnableWindow(hCopyBtn_, !inProgress);
    EnableWindow(hMoveBtn_, !inProgress);
    EnableWindow(hVerifyCheck_, !inProgress);
    EnableWindow(hAddDriveBtn_, !inProgress);
    EnableWindow(hRemoveDriveBtn_, !inProgress);

    if (inProgress) {
        SendMessageW(hProgressBar_, PBM_SETPOS, 0, 0);
        SetWindowTextW(hProgressLabel_, L"Starting...");
        SetWindowTextW(hSpeedLabel_, L"");
        migrationStartTick_ = GetTickCount64();
    }
}

void MainWindow::OnMigrationProgress(int progress) {
    SendMessageW(hProgressBar_, PBM_SETPOS, progress, 0);

    ULONGLONG elapsed = GetTickCount64() - migrationStartTick_;
    if (elapsed > 500 && progress > 0 && migrationTotalBytes_ > 0) {
        double bytesTransferred = (static_cast<double>(progress) / 1000.0) * migrationTotalBytes_;
        double seconds = elapsed / 1000.0;
        double bytesPerSec = bytesTransferred / seconds;

        std::wstring speed = Utils::FormatSizeShort(static_cast<uint64_t>(bytesPerSec)) + L"/s";

        double remaining = migrationTotalBytes_ - bytesTransferred;
        int etaSec = (bytesPerSec > 0) ? static_cast<int>(remaining / bytesPerSec) : 0;
        if (etaSec > 0) {
            int mins = etaSec / 60;
            int secs = etaSec % 60;
            wchar_t etaBuf[32];
            if (mins > 0)
                swprintf_s(etaBuf, L"  ETA %d:%02d", mins, secs);
            else
                swprintf_s(etaBuf, L"  ETA %ds", secs);
            speed += etaBuf;
        }

        SetWindowTextW(hSpeedLabel_, speed.c_str());
    }
}

void MainWindow::OnMigrationFile(const wchar_t* filename) {
    SetWindowTextW(hProgressLabel_, filename);
}

void MainWindow::OnMigrationComplete(int status) {
    SetOperationInProgress(false);

    // Reload JSON transfer log
    transferLog_.Clear();
    transferLog_.Load(jsonLogPath_);
    fileTree_.SetTransferredPaths(&transferLog_.GetPathMap());

    // Refresh drive info for all dest drives
    for (int i = 0; i < destTree_.GetDriveCount(); i++) {
        DriveInfo::RefreshDriveSpace(destTree_.GetDrive(i));
    }

    // Clear assignments and rebuild
    assignments_.clear();
    fileSizes_.clear();
    OnAssignmentsChanged();

    if (status == 0) {
        MessageBoxW(hWnd_, L"Migration completed successfully.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
    } else if (status == 1) {
        MessageBoxW(hWnd_, L"Migration was cancelled.",
            L"DSplit", MB_OK | MB_ICONWARNING);
    } else {
        MessageBoxW(hWnd_, L"Migration completed with errors.",
            L"DSplit", MB_OK | MB_ICONWARNING);
    }
}

void MainWindow::OnMigrationError(const wchar_t* errorMsg) {
    SetWindowTextW(hProgressLabel_, errorMsg);
}
