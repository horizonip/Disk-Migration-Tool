#include "MainWindow.h"
#include "Utils.h"
#include <windowsx.h>
#include <shobjidl.h>
#include <shlobj.h>

const wchar_t* MainWindow::CLASS_NAME = L"DSplitMainWindow";

static const int MARGIN = 12;
static const int CONTROL_HEIGHT = 24;
static const int BUTTON_HEIGHT = 28;
static const int BUTTON_WIDTH = 120;
static const int LABEL_HEIGHT = 18;

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
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 650,
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
        if (self) self->OnNotify(reinterpret_cast<NMHDR*>(lParam));
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
            self->UpdateStatusBar();
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

    // Destination drive section
    hDriveLabel_ = createCtrl(L"STATIC", L"Destination Drive:", SS_LEFT, 0);
    hDriveCombo_ = createCtrl(L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | WS_VSCROLL, IDC_DRIVE_COMBO);

    // Source folder section
    hSourceLabel_ = createCtrl(L"STATIC", L"Source Folder:", SS_LEFT, 0);
    hSourceEdit_ = createCtrl(L"EDIT", L"",
        ES_AUTOHSCROLL | ES_READONLY | WS_BORDER, IDC_SOURCE_EDIT);
    hBrowseBtn_ = createCtrl(L"BUTTON", L"Browse...",
        BS_PUSHBUTTON, IDC_BROWSE_BTN);

    // File TreeView
    hTreeView_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_CHECKBOXES,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(IDC_FILE_TREE),
        hInstance_, nullptr);
    SendMessageW(hTreeView_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    fileTree_.SetTreeView(hTreeView_);

    // Status bar
    hStatusLabel_ = createCtrl(L"STATIC", L"Selected: 0 bytes / 0 bytes available",
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

    // Progress section
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

    hCancelBtn_ = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hWnd, reinterpret_cast<HMENU>(IDC_CANCEL_BTN),
        hInstance_, nullptr);
    SendMessageW(hCancelBtn_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    // Populate drives
    drives_ = DriveInfo::EnumerateDrives();
    for (size_t i = 0; i < drives_.size(); i++) {
        SendMessageW(hDriveCombo_, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(drives_[i].displayString.c_str()));
    }
    if (!drives_.empty()) {
        SendMessageW(hDriveCombo_, CB_SETCURSEL, 0, 0);
        selectedDriveIndex_ = 0;
    }

    // Trigger initial layout
    RECT rc;
    GetClientRect(hWnd, &rc);
    OnSize(rc.right, rc.bottom);
    UpdateStatusBar();
}

void MainWindow::OnSize(int width, int height) {
    if (width == 0 || height == 0) return;

    int x = MARGIN;
    int y = MARGIN;
    int contentWidth = width - 2 * MARGIN;

    // Drive label
    MoveWindow(hDriveLabel_, x, y, contentWidth, LABEL_HEIGHT, TRUE);
    y += LABEL_HEIGHT + 4;

    // Drive combo
    MoveWindow(hDriveCombo_, x, y, contentWidth, CONTROL_HEIGHT * 8, TRUE);
    y += CONTROL_HEIGHT + MARGIN;

    // Source label
    MoveWindow(hSourceLabel_, x, y, contentWidth, LABEL_HEIGHT, TRUE);
    y += LABEL_HEIGHT + 4;

    // Source edit + browse button
    int browseWidth = 80;
    MoveWindow(hSourceEdit_, x, y, contentWidth - browseWidth - 6, CONTROL_HEIGHT, TRUE);
    MoveWindow(hBrowseBtn_, x + contentWidth - browseWidth, y, browseWidth, CONTROL_HEIGHT, TRUE);
    y += CONTROL_HEIGHT + MARGIN;

    // Calculate remaining space for tree
    int bottomSectionHeight = LABEL_HEIGHT + 4 + 8 + BUTTON_HEIGHT + MARGIN +
                               CONTROL_HEIGHT + LABEL_HEIGHT + MARGIN + MARGIN;
    int treeHeight = height - y - bottomSectionHeight;
    if (treeHeight < 100) treeHeight = 100;

    MoveWindow(hTreeView_, x, y, contentWidth, treeHeight, TRUE);
    y += treeHeight + MARGIN;

    // Status label + capacity bar
    MoveWindow(hStatusLabel_, x, y, contentWidth, LABEL_HEIGHT, TRUE);
    y += LABEL_HEIGHT + 4;
    MoveWindow(hCapacityBar_, x, y, contentWidth, 8, TRUE);
    y += 8 + MARGIN;

    // Action buttons row
    int btnSpacing = 6;
    int bx = x;
    MoveWindow(hSelectAllBtn_, bx, y, 80, BUTTON_HEIGHT, TRUE);
    bx += 80 + btnSpacing;
    MoveWindow(hDeselectAllBtn_, bx, y, 90, BUTTON_HEIGHT, TRUE);
    bx += 90 + btnSpacing;
    MoveWindow(hAutoSelectBtn_, bx, y, 90, BUTTON_HEIGHT, TRUE);
    bx += 90 + btnSpacing + 20; // extra gap before action buttons
    int actionBtnWidth = 140;
    MoveWindow(hCopyBtn_, bx, y, actionBtnWidth, BUTTON_HEIGHT, TRUE);
    bx += actionBtnWidth + btnSpacing;
    MoveWindow(hMoveBtn_, bx, y, actionBtnWidth, BUTTON_HEIGHT, TRUE);
    y += BUTTON_HEIGHT + MARGIN;

    // Progress bar + label + cancel
    int cancelWidth = 70;
    MoveWindow(hProgressBar_, x, y, contentWidth - cancelWidth - 6, CONTROL_HEIGHT, TRUE);
    MoveWindow(hCancelBtn_, x + contentWidth - cancelWidth, y, cancelWidth, CONTROL_HEIGHT, TRUE);
    y += CONTROL_HEIGHT + 2;
    MoveWindow(hProgressLabel_, x, y, contentWidth, LABEL_HEIGHT, TRUE);
}

void MainWindow::OnCommand(WORD id, WORD code) {
    switch (id) {
    case IDC_DRIVE_COMBO:
        if (code == CBN_SELCHANGE) OnDriveSelChanged();
        break;
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
                // Post a message to handle after the state has changed
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

void MainWindow::OnDriveSelChanged() {
    int sel = static_cast<int>(SendMessageW(hDriveCombo_, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < static_cast<int>(drives_.size())) {
        selectedDriveIndex_ = sel;
        DriveInfo::RefreshDriveSpace(drives_[sel]);
        // Update combo text
        SendMessageW(hDriveCombo_, CB_DELETESTRING, sel, 0);
        SendMessageW(hDriveCombo_, CB_INSERTSTRING, sel,
            reinterpret_cast<LPARAM>(drives_[sel].displayString.c_str()));
        SendMessageW(hDriveCombo_, CB_SETCURSEL, sel, 0);
        UpdateStatusBar();
    }
}

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
                    fileTree_.Populate(path);
                    UpdateStatusBar();
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
    UpdateStatusBar();
}

void MainWindow::OnDeselectAll() {
    fileTree_.DeselectAll();
    UpdateStatusBar();
}

void MainWindow::OnAutoSelect() {
    if (selectedDriveIndex_ < 0) {
        MessageBoxW(hWnd_, L"Please select a destination drive first.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
        return;
    }
    uint64_t available = drives_[selectedDriveIndex_].freeBytes;
    fileTree_.AutoSelect(available);
    UpdateStatusBar();
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

    if (selectedDriveIndex_ < 0) {
        MessageBoxW(hWnd_, L"Please select a destination drive.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
        return;
    }

    auto selectedFiles = fileTree_.GetSelectedFiles();
    if (selectedFiles.empty()) {
        MessageBoxW(hWnd_, L"No files selected.",
            L"DSplit", MB_OK | MB_ICONINFORMATION);
        return;
    }

    uint64_t selectedSize = fileTree_.GetSelectedSize();
    uint64_t available = drives_[selectedDriveIndex_].freeBytes;
    if (selectedSize > available) {
        MessageBoxW(hWnd_,
            L"Selected files exceed available space on destination drive.",
            L"DSplit", MB_OK | MB_ICONERROR);
        return;
    }

    // Build destination path: drive root + source folder name
    std::wstring sourceFolder = fileTree_.GetSourceFolder();
    std::wstring folderName;
    size_t lastSep = sourceFolder.find_last_of(L"\\/");
    if (lastSep != std::wstring::npos) {
        folderName = sourceFolder.substr(lastSep + 1);
    } else {
        folderName = sourceFolder;
    }

    std::wstring destRoot = Utils::CombinePaths(
        drives_[selectedDriveIndex_].rootPath, folderName);

    MigrationParams params;
    params.hWndNotify = hWnd_;
    params.destRoot = destRoot;
    params.moveMode = moveMode;
    params.totalBytes = selectedSize;

    for (auto& f : selectedFiles) {
        MigrationItem item;
        item.sourcePath = f.sourcePath;
        item.relativePath = f.relativePath;
        item.isDirectory = f.isDirectory;
        params.items.push_back(std::move(item));
    }

    SetOperationInProgress(true);
    migration_.Start(params);
}

void MainWindow::UpdateStatusBar() {
    uint64_t selected = fileTree_.GetSelectedSize();
    uint64_t available = 0;
    if (selectedDriveIndex_ >= 0) {
        available = drives_[selectedDriveIndex_].freeBytes;
    }

    std::wstring status = L"Selected: " + Utils::FormatSize(selected) +
                          L" / " + Utils::FormatSize(available) + L" available";
    SetWindowTextW(hStatusLabel_, status.c_str());

    // Update capacity bar
    int pct = 0;
    if (available > 0) {
        pct = static_cast<int>((selected * 1000) / available);
        if (pct > 1000) pct = 1000;
    }
    SendMessageW(hCapacityBar_, PBM_SETPOS, pct, 0);

    // Color the bar red if over capacity
    if (selected > available && available > 0) {
        SendMessageW(hCapacityBar_, PBM_SETBARCOLOR, 0, RGB(220, 50, 50));
    } else {
        SendMessageW(hCapacityBar_, PBM_SETBARCOLOR, 0, RGB(60, 160, 60));
    }
}

void MainWindow::SetOperationInProgress(bool inProgress) {
    ShowWindow(hProgressBar_, inProgress ? SW_SHOW : SW_HIDE);
    ShowWindow(hProgressLabel_, inProgress ? SW_SHOW : SW_HIDE);
    ShowWindow(hCancelBtn_, inProgress ? SW_SHOW : SW_HIDE);

    EnableWindow(hDriveCombo_, !inProgress);
    EnableWindow(hBrowseBtn_, !inProgress);
    EnableWindow(hSelectAllBtn_, !inProgress);
    EnableWindow(hDeselectAllBtn_, !inProgress);
    EnableWindow(hAutoSelectBtn_, !inProgress);
    EnableWindow(hCopyBtn_, !inProgress);
    EnableWindow(hMoveBtn_, !inProgress);

    if (inProgress) {
        SendMessageW(hProgressBar_, PBM_SETPOS, 0, 0);
        SetWindowTextW(hProgressLabel_, L"Starting...");
    }
}

void MainWindow::OnMigrationProgress(int progress) {
    SendMessageW(hProgressBar_, PBM_SETPOS, progress, 0);
}

void MainWindow::OnMigrationFile(const wchar_t* filename) {
    SetWindowTextW(hProgressLabel_, filename);
}

void MainWindow::OnMigrationComplete(int status) {
    SetOperationInProgress(false);

    // Refresh drive info
    if (selectedDriveIndex_ >= 0) {
        DriveInfo::RefreshDriveSpace(drives_[selectedDriveIndex_]);
        SendMessageW(hDriveCombo_, CB_DELETESTRING, selectedDriveIndex_, 0);
        SendMessageW(hDriveCombo_, CB_INSERTSTRING, selectedDriveIndex_,
            reinterpret_cast<LPARAM>(drives_[selectedDriveIndex_].displayString.c_str()));
        SendMessageW(hDriveCombo_, CB_SETCURSEL, selectedDriveIndex_, 0);
    }
    UpdateStatusBar();

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
    // Show in progress label rather than blocking with a dialog during operation
    SetWindowTextW(hProgressLabel_, errorMsg);
}
