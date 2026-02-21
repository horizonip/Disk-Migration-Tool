#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include "MainWindow.h"

#pragma comment(lib, "comctl32.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Initialize common controls (v6 for visual styles)
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    // Initialize COM for IFileDialog
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!MainWindow::Register(hInstance)) {
        MessageBoxW(nullptr, L"Failed to register window class.", L"DSplit", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hWnd = MainWindow::Create(hInstance);
    if (!hWnd) {
        MessageBoxW(nullptr, L"Failed to create window.", L"DSplit", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
