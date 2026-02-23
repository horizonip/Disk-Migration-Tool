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

// ---------- Background drive indexing ----------

struct IndexThreadData {
    std::wstring driveRoot;
    std::unordered_set<std::wstring>* resultSet;
    std::atomic<bool>* cancelled;
    HWND hWndNotify;
    // Log file info
    std::wstring logFilePath;       // Local log: {exe}\logs\DSplit_{serial}.log
    std::wstring destLogFilePath;   // Drive root log: {drive}\DSplit_{serial}.log
    std::wstring logFileName;       // Just the filename to skip during scan
    std::wstring volumeName;
    std::wstring driveLetter;
    std::wstring serialHex;
};

static void WriteToLogs(HANDLE hLog, HANDLE hDestLog, const std::wstring& line) {
    Utils::WriteLogLine(hLog, line);
    Utils::WriteLogLine(hDestLog, line);
}

static void ScanDriveRecursive(const std::wstring& basePath, const std::wstring& relPrefix,
                                std::unordered_set<std::wstring>& results,
                                std::atomic<bool>& cancelled,
                                HANDLE hLog, HANDLE hDestLog,
                                const std::wstring& skipFileName) {
    if (cancelled) return;

    std::wstring searchPath = basePath;
    if (!searchPath.empty() && searchPath.back() != L'\\') searchPath += L'\\';
    searchPath += L'*';

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (cancelled) break;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        // Skip system entries at drive root (e.g. $Recycle.Bin, System Volume Information)
        if (relPrefix.empty() && (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM))
            continue;

        // Skip the log file itself at drive root
        if (relPrefix.empty() && !skipFileName.empty() &&
            _wcsicmp(fd.cFileName, skipFileName.c_str()) == 0)
            continue;

        std::wstring relPath = relPrefix.empty()
            ? std::wstring(fd.cFileName)
            : relPrefix + L"\\" + fd.cFileName;

        std::wstring fullPath = basePath;
        if (!fullPath.empty() && fullPath.back() != L'\\') fullPath += L'\\';
        fullPath += fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            results.insert(relPath + L"\\");
            WriteToLogs(hLog, hDestLog, relPath + L"\\");
            ScanDriveRecursive(fullPath, relPath, results, cancelled, hLog, hDestLog, skipFileName);
        } else {
            results.insert(relPath);
            WriteToLogs(hLog, hDestLog, relPath);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

static HANDLE OpenLogForWrite(const std::wstring& path, const std::wstring& volumeName,
                               const std::wstring& driveLetter, const std::wstring& serialHex) {
    if (path.empty()) return INVALID_HANDLE_VALUE;

    size_t sep = path.find_last_of(L"\\/");
    if (sep != std::wstring::npos) {
        Utils::EnsureDirectoryExists(path.substr(0, sep));
    }

    HANDLE h = CreateFileW(path.c_str(),
        GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (h != INVALID_HANDLE_VALUE) {
        const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        DWORD written;
        WriteFile(h, bom, 3, &written, nullptr);
        Utils::WriteLogLine(h, L"# DSplit Transfer Log");
        Utils::WriteLogLine(h, L"# Volume: " + volumeName +
                                   L" (" + driveLetter + L")");
        Utils::WriteLogLine(h, L"# Serial: " + serialHex);
    }
    return h;
}

static DWORD WINAPI IndexThreadProc(LPVOID param) {
    auto* data = static_cast<IndexThreadData*>(param);

    // Open local log file ({exe}\logs\DSplit_{serial}.log)
    HANDLE hLog = OpenLogForWrite(data->logFilePath,
        data->volumeName, data->driveLetter, data->serialHex);

    // Open destination drive root log ({drive}\DSplit_{serial}.log)
    HANDLE hDestLog = OpenLogForWrite(data->destLogFilePath,
        data->volumeName, data->driveLetter, data->serialHex);

    ScanDriveRecursive(data->driveRoot, L"", *data->resultSet, *data->cancelled,
        hLog, hDestLog, data->logFileName);

    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    if (hDestLog != INVALID_HANDLE_VALUE) CloseHandle(hDestLog);

    PostMessageW(data->hWndNotify, WM_INDEXING_COMPLETE, 0, 0);
    delete data;
    return 0;
}

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

    case WM_INDEXING_COMPLETE:
        if (self) self->OnIndexingComplete();
        return 0;

    case WM_DESTROY:
        if (self) {
            self->CancelDriveIndexing();
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
    hVerifyCheck_ = createCtrl(L"BUTTON", L"Verify before delete",
        BS_AUTOCHECKBOX, IDC_VERIFY_CHECK);
    SendMessageW(hVerifyCheck_, BM_SETCHECK, BST_CHECKED, 0); // on by default

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

    // Populate drives
    drives_ = DriveInfo::EnumerateDrives();
    for (size_t i = 0; i < drives_.size(); i++) {
        SendMessageW(hDriveCombo_, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(drives_[i].displayString.c_str()));
    }
    if (!drives_.empty()) {
        SendMessageW(hDriveCombo_, CB_SETCURSEL, 0, 0);
        selectedDriveIndex_ = 0;

        // Load transfer log for initial drive (no popup on startup)
        std::wstring serialHex = TransferLog::FormatSerial(drives_[0].serialNumber);
        logLoaded_ = transferLog_.Load(exeDir_, serialHex);
        if (logLoaded_) {
            fileTree_.SetExcludedPaths(&transferLog_.GetLoggedPaths());
        }
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
    bx += actionBtnWidth + btnSpacing;
    MoveWindow(hVerifyCheck_, bx, y, 160, BUTTON_HEIGHT, TRUE);
    y += BUTTON_HEIGHT + MARGIN;

    // Progress bar + label + cancel
    int cancelWidth = 70;
    MoveWindow(hProgressBar_, x, y, contentWidth - cancelWidth - 6, CONTROL_HEIGHT, TRUE);
    MoveWindow(hCancelBtn_, x + contentWidth - cancelWidth, y, cancelWidth, CONTROL_HEIGHT, TRUE);
    y += CONTROL_HEIGHT + 2;
    int speedWidth = 160;
    MoveWindow(hProgressLabel_, x, y, contentWidth - speedWidth - 6, LABEL_HEIGHT, TRUE);
    MoveWindow(hSpeedLabel_, x + contentWidth - speedWidth, y, speedWidth, LABEL_HEIGHT, TRUE);
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

// ---------- Drive indexing ----------

void MainWindow::StartDriveIndexing() {
    CancelDriveIndexing();
    driveIndex_.clear();
    filteredExclusions_.clear();
    fileTree_.SetExcludedPaths(nullptr);

    if (selectedDriveIndex_ < 0) return;

    indexCancelled_ = false;

    std::wstring serialHex = TransferLog::FormatSerial(drives_[selectedDriveIndex_].serialNumber);

    std::wstring logFileName = L"DSplit_" + serialHex + L".log";

    auto* data = new IndexThreadData();
    data->driveRoot = drives_[selectedDriveIndex_].rootPath;
    data->resultSet = &driveIndex_;
    data->cancelled = &indexCancelled_;
    data->hWndNotify = hWnd_;
    data->logFilePath = Utils::CombinePaths(
        Utils::CombinePaths(exeDir_, L"logs"), logFileName);
    data->destLogFilePath = Utils::CombinePaths(
        drives_[selectedDriveIndex_].rootPath, logFileName);
    data->logFileName = logFileName;
    data->volumeName = drives_[selectedDriveIndex_].volumeName;
    data->driveLetter = drives_[selectedDriveIndex_].driveLetter;
    data->serialHex = serialHex;

    hIndexThread_ = CreateThread(nullptr, 0, IndexThreadProc, data, 0, nullptr);
    if (hIndexThread_) {
        SetWindowTextW(hStatusLabel_, L"Indexing destination drive...");
        EnableWindow(hAutoSelectBtn_, FALSE);
        EnableWindow(hCopyBtn_, FALSE);
        EnableWindow(hMoveBtn_, FALSE);
    } else {
        delete data;
    }
}

void MainWindow::CancelDriveIndexing() {
    if (hIndexThread_) {
        indexCancelled_ = true;
        WaitForSingleObject(hIndexThread_, 5000);
        CloseHandle(hIndexThread_);
        hIndexThread_ = nullptr;
    }
}

void MainWindow::OnIndexingComplete() {
    if (hIndexThread_) {
        CloseHandle(hIndexThread_);
        hIndexThread_ = nullptr;
    }

    EnableWindow(hAutoSelectBtn_, TRUE);
    EnableWindow(hCopyBtn_, TRUE);
    EnableWindow(hMoveBtn_, TRUE);

    // Reload the log that was just written so logLoaded_ is set
    if (selectedDriveIndex_ >= 0) {
        std::wstring serialHex = TransferLog::FormatSerial(drives_[selectedDriveIndex_].serialNumber);
        logLoaded_ = transferLog_.Load(exeDir_, serialHex);
    }

    // If a source folder is already loaded, build exclusions now
    if (!fileTree_.GetSourceFolder().empty()) {
        BuildFilteredExclusions();
        fileTree_.SetExcludedPaths(&filteredExclusions_);
    }

    UpdateStatusBar();
}

void MainWindow::BuildFilteredExclusions() {
    filteredExclusions_.clear();

    std::wstring sourceFolder = fileTree_.GetSourceFolder();
    if (sourceFolder.empty() || driveIndex_.empty()) return;

    // Extract source folder name
    std::wstring folderName;
    size_t lastSep = sourceFolder.find_last_of(L"\\/");
    if (lastSep != std::wstring::npos)
        folderName = sourceFolder.substr(lastSep + 1);
    else
        folderName = sourceFolder;

    // Filter driveIndex_ paths under folderName\, strip the prefix
    std::wstring prefix = folderName + L"\\";
    for (const auto& p : driveIndex_) {
        if (p.size() > prefix.size() &&
            _wcsnicmp(p.c_str(), prefix.c_str(), prefix.size()) == 0) {
            filteredExclusions_.insert(p.substr(prefix.size()));
        }
    }
}

// ---------- Event handlers ----------

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

        // Load transfer log for selected drive
        std::wstring serialHex = TransferLog::FormatSerial(drives_[sel].serialNumber);
        logLoaded_ = transferLog_.Load(exeDir_, serialHex);
        if (logLoaded_) {
            CancelDriveIndexing();
            driveIndex_.clear();
            filteredExclusions_.clear();
            fileTree_.SetExcludedPaths(&transferLog_.GetLoggedPaths());
            UpdateStatusBar();
        } else {
            int ret = MessageBoxW(hWnd_,
                L"No transfer log found for this drive.\n"
                L"Would you like to index it to detect existing files?",
                L"DSplit", MB_YESNO | MB_ICONQUESTION);
            if (ret == IDYES) {
                StartDriveIndexing();
            } else {
                UpdateStatusBar();
            }
        }
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

                    // Apply exclusions from drive index (if available and no log)
                    if (!logLoaded_ && !driveIndex_.empty()) {
                        BuildFilteredExclusions();
                        fileTree_.SetExcludedPaths(&filteredExclusions_);
                    }

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

    std::wstring serialHex = TransferLog::FormatSerial(drives_[selectedDriveIndex_].serialNumber);

    MigrationParams params;
    params.hWndNotify = hWnd_;
    params.destRoot = destRoot;
    params.moveMode = moveMode;
    params.verifyBeforeDelete = moveMode &&
        (SendMessageW(hVerifyCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    params.totalBytes = selectedSize;
    std::wstring logFileName = L"DSplit_" + serialHex + L".log";
    params.logFilePath = Utils::CombinePaths(
        Utils::CombinePaths(exeDir_, L"logs"), logFileName);
    params.destLogFilePath = Utils::CombinePaths(
        drives_[selectedDriveIndex_].rootPath, logFileName);
    params.logVolumeName = drives_[selectedDriveIndex_].volumeName;
    params.logDriveLetter = drives_[selectedDriveIndex_].driveLetter;
    params.logSerialHex = serialHex;

    for (auto& f : selectedFiles) {
        MigrationItem item;
        item.sourcePath = f.sourcePath;
        item.relativePath = f.relativePath;
        item.fileSize = f.size;
        item.isDirectory = f.isDirectory;
        params.items.push_back(std::move(item));
    }

    migrationTotalBytes_ = selectedSize;
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
    ShowWindow(hSpeedLabel_, inProgress ? SW_SHOW : SW_HIDE);
    ShowWindow(hCancelBtn_, inProgress ? SW_SHOW : SW_HIDE);

    EnableWindow(hDriveCombo_, !inProgress);
    EnableWindow(hBrowseBtn_, !inProgress);
    EnableWindow(hSelectAllBtn_, !inProgress);
    EnableWindow(hDeselectAllBtn_, !inProgress);
    EnableWindow(hAutoSelectBtn_, !inProgress);
    EnableWindow(hCopyBtn_, !inProgress);
    EnableWindow(hMoveBtn_, !inProgress);
    EnableWindow(hVerifyCheck_, !inProgress);

    if (inProgress) {
        SendMessageW(hProgressBar_, PBM_SETPOS, 0, 0);
        SetWindowTextW(hProgressLabel_, L"Starting...");
        SetWindowTextW(hSpeedLabel_, L"");
        migrationStartTick_ = GetTickCount64();
    }
}

void MainWindow::OnMigrationProgress(int progress) {
    SendMessageW(hProgressBar_, PBM_SETPOS, progress, 0);

    // Calculate transfer speed from elapsed time and progress
    ULONGLONG elapsed = GetTickCount64() - migrationStartTick_;
    if (elapsed > 500 && progress > 0 && migrationTotalBytes_ > 0) {
        double bytesTransferred = (static_cast<double>(progress) / 1000.0) * migrationTotalBytes_;
        double seconds = elapsed / 1000.0;
        double bytesPerSec = bytesTransferred / seconds;

        // Format speed + percentage
        std::wstring speed = Utils::FormatSizeShort(static_cast<uint64_t>(bytesPerSec)) + L"/s";

        // ETA
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

    // End transfer logging and reload log
    transferLog_.EndLogging();
    if (selectedDriveIndex_ >= 0) {
        std::wstring serialHex = TransferLog::FormatSerial(drives_[selectedDriveIndex_].serialNumber);
        logLoaded_ = transferLog_.Load(exeDir_, serialHex);
        if (logLoaded_) {
            fileTree_.SetExcludedPaths(&transferLog_.GetLoggedPaths());
        }
    }

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
