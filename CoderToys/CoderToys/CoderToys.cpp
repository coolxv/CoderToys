// CoderToys.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "CoderToys.h"
#include <stdexcept>
#include <system_error>

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")


#define MAX_LOADSTRING 100

// 全局变量:
HINSTANCE hInst;                                // 当前实例
HWND hWnd;                                      //窗口句柄
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);





namespace BorderLessManage {

    // BoardLess var
    bool borderless = true; // is the window currently borderless
    bool borderless_resize = true; // should the window allow resizing by dragging the borders while borderless
    bool borderless_drag = true; // should the window allow moving my dragging the client area
    bool borderless_shadow = true; // should the window display a native aero shadow while borderless

    // we cannot just use WS_POPUP style
    // WS_THICKFRAME: without this the window cannot be resized and so aero snap, de-maximizing and minimizing won't work
    // WS_SYSMENU: enables the context menu with the move, close, maximize, minize... commands (shift + right-click on the task bar item)
    // WS_CAPTION: enables aero minimize animation/transition
    // WS_MAXIMIZEBOX, WS_MINIMIZEBOX: enable minimize/maximize
    enum class Style : DWORD {
        windowed = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        aero_borderless = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
        basic_borderless = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX
    };

    auto maximized(HWND hwnd) -> bool {
        WINDOWPLACEMENT placement{ 0 };
        if (!::GetWindowPlacement(hwnd, &placement)) {
            return false;
        }

        return placement.showCmd == SW_MAXIMIZE;
    }

    /* Adjust client rect to not spill over monitor edges when maximized.
     * rect(in/out): in: proposed window rect, out: calculated client rect
     * Does nothing if the window is not maximized.
     */
    auto adjust_maximized_client_rect(HWND window, RECT& rect) -> void {
        if (!maximized(window)) {
            return;
        }

        auto monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
        if (!monitor) {
            return;
        }

        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!::GetMonitorInfoW(monitor, &monitor_info)) {
            return;
        }

        // when maximized, make the client area fill just the monitor (without task bar) rect,
        // not the whole window rect which extends beyond the monitor.
        rect = monitor_info.rcWork;
    }

    auto last_error(const std::string& message) -> std::system_error {
        return std::system_error(
            std::error_code(::GetLastError(), std::system_category()),
            message
        );
    }

    auto composition_enabled() -> bool {
        BOOL composition_enabled = FALSE;
        bool success = ::DwmIsCompositionEnabled(&composition_enabled) == S_OK;
        return composition_enabled && success;
    }

    auto select_borderless_style() -> Style {
        return composition_enabled() ? Style::aero_borderless : Style::basic_borderless;
    }

    auto set_shadow(HWND handle, bool enabled) -> void {
        if (composition_enabled()) {
            static const MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
            ::DwmExtendFrameIntoClientArea(handle, &shadow_state[enabled]);
        }
    }

    void set_borderless(bool enabled) {
        Style new_style = (enabled) ? select_borderless_style() : Style::windowed;
        Style old_style = static_cast<Style>(::GetWindowLongPtrW(hWnd, GWL_STYLE));

        if (new_style != old_style) {
            borderless = enabled;

            ::SetWindowLongPtrW(hWnd, GWL_STYLE, static_cast<LONG>(new_style));

            // when switching between borderless and windowed, restore appropriate shadow state
            set_shadow(hWnd, borderless_shadow && (new_style != Style::windowed));

            // redraw frame
            ::SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
            ::ShowWindow(hWnd, SW_SHOW);
        }
    }

    void set_borderless_shadow(bool enabled) {
        if (borderless) {
            borderless_shadow = enabled;
            set_shadow(hWnd, enabled);
        }
    }

    auto hit_test(POINT cursor)  -> LRESULT {
        // identify borders and corners to allow resizing the window.
        // Note: On Windows 10, windows behave differently and
        // allow resizing outside the visible window frame.
        // This implementation does not replicate that behavior.
        const POINT border{
            ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER),
            ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER)
        };
        RECT window;
        if (!::GetWindowRect(hWnd, &window)) {
            return HTNOWHERE;
        }

        const auto drag = borderless_drag ? HTCAPTION : HTCLIENT;

        enum region_mask {
            client = 0b0000,
            left = 0b0001,
            right = 0b0010,
            top = 0b0100,
            bottom = 0b1000,
        };

        const auto result =
            left * (cursor.x < (window.left + border.x)) |
            right * (cursor.x >= (window.right - border.x)) |
            top * (cursor.y < (window.top + border.y)) |
            bottom * (cursor.y >= (window.bottom - border.y));

        switch (result) {
        case left: return borderless_resize ? HTLEFT : drag;
        case right: return borderless_resize ? HTRIGHT : drag;
        case top: return borderless_resize ? HTTOP : drag;
        case bottom: return borderless_resize ? HTBOTTOM : drag;
        case top | left: return borderless_resize ? HTTOPLEFT : drag;
        case top | right: return borderless_resize ? HTTOPRIGHT : drag;
        case bottom | left: return borderless_resize ? HTBOTTOMLEFT : drag;
        case bottom | right: return borderless_resize ? HTBOTTOMRIGHT : drag;
        case client: return drag;
        default: return HTNOWHERE;
        }
    }

}


namespace IconManage {

    void icon_reset()
    {
        HWND  hwndParent = ::FindWindow(L"Progman", L"Program Manager");
        HWND  hwndSHELLDLL_DefView = ::FindWindowEx(hwndParent, NULL, L"SHELLDLL_DefView", NULL);
        HWND  hwndSysListView32 = ::FindWindowEx(hwndSHELLDLL_DefView, NULL, L"SysListView32", L"FolderView");
        int Nm = ListView_GetItemCount(hwndSysListView32);

        int sNm = 0;
        if (Nm >= 10)
        {
            sNm = 10;
        }
        else
        {
            sNm = Nm;
        }

        for (int i = 0; i < sNm; i++)
        {
            int x = 400 + 150 * cos(i * 36 * 3.1415926 / 180);
            int y = 400 + 150 * sin(i * 36 * 3.1415926 / 180);
            ::SendMessage(hwndSysListView32, LVM_SETITEMPOSITION, i, MAKELPARAM(x, y));
        }

        ListView_RedrawItems(hwndSysListView32, 0, ListView_GetItemCount(hwndSysListView32) - 1);
        ::UpdateWindow(hwndSysListView32);

    }
}

namespace WallManage {

    // 背景窗体句柄
    HWND hProgmanWnd = nullptr;
    HWND hWorkerWnd = nullptr;

    BOOL CALLBACK enum_window_callback(HWND hTop, LPARAM lparam)
    {
        // 查找 SHELLDLL_DefView 窗体句柄
        // 存在多个WorkerW窗体，只有底下存在SHELLDLL_DefView的才是
        HWND hWndShl = ::FindWindowExW(
            hTop, nullptr, L"SHELLDLL_DefView", nullptr);
        if (hWndShl == nullptr) { return true; }

        // XP 直接查找SysListView32窗体
        // g_hWorker = ::FindWindowEx(hWndShl, 0, _T("SysListView32"),_T("FolderView"));

        // win7或更高版本
        // 查找 WorkerW 窗口句柄(以桌面窗口为父窗口)
        hWorkerWnd = ::FindWindowExW(nullptr, hTop, L"WorkerW", nullptr);
        return hWorkerWnd == nullptr;
    }

    HWND find_wall_window()
    {
        HWND wid = 0;
        // 遍历桌面所有顶层窗口去查找桌面背景窗口句柄
        if (hProgmanWnd == nullptr) {
            // https://www.codeproject.com/articles/856020/draw-behind-desktop-icons-in-windows
            // 先找到Progman 窗口
            hProgmanWnd = ::FindWindowExW(GetDesktopWindow(), nullptr, L"Progman", L"Program Manager");
            if (hProgmanWnd == nullptr) {
                return wid;
            }
            // 发送消息到Program Manager窗口
            // 要在桌面图标和壁纸之间触发创建WorkerW窗口，必须向Program Manager发送一个消息
            // 这个消息没有一个公开的WindowsAPI来执行，只能使用SendMessageTimeout来发送0x052C
            // win10 1903 下未能成功（无法分离）
            DWORD_PTR lpdwResult = 0;
            ::SendMessage(hProgmanWnd, 0x052C, 0xD, 0);
            ::SendMessage(hProgmanWnd, 0x052C, 0xD, 1);
            //        ::SendMessageTimeoutW(s_hProgmanWnd, 0x052C, 0, 0, SMTO_NORMAL, 1000, &lpdwResult);
            //        ::SendMessageTimeoutW(s_hProgmanWnd, 0x052C, 0, 1, SMTO_NORMAL, 1000, &lpdwResult);

            // 查找到 WorkerW 窗口，设置显示
            EnumWindows(enum_window_callback, 0);
            // ::ShowWindowAsync(s_hWorkerWnd, SW_HIDE);
            ::ShowWindow(hWorkerWnd, SW_NORMAL);
        }
        if (hWorkerWnd == nullptr) {
            ::SendMessage(hProgmanWnd, 0x052C, 0, 0);
            EnumWindows(enum_window_callback, 0);
        }
        if (hWorkerWnd != nullptr) {
            // 检测是否是 win8 后版本
            OSVERSIONINFOEX osvi;
            DWORDLONG dwlConditionMask = 0;

            ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
            osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
            osvi.dwMajorVersion = 6;
            osvi.dwMinorVersion = 2;

            VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
            VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);

            if (!VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION, dwlConditionMask)) {
                // 检测到 win7 系统，隐藏 WorkerW
                ShowWindow(hWorkerWnd, SW_HIDE);
                hWorkerWnd = hProgmanWnd;
            }
        }
        if (hWorkerWnd == nullptr) {
            return wid;
        }
        wid = hWorkerWnd;
        return wid;
    }


    void leave_wall_window()
    {
        if (hProgmanWnd != nullptr) {
            // ::SendMessage(s_hProgmanWnd, 0x052C, 0xD, 0);
            // 发送下面消息后，桌面会重新绘制
            ::SendMessage(hProgmanWnd, 0x052C, 0xD, 1);
        }
    }


}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: 在此处放置代码。

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_CODERTOYS, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_CODERTOYS));

    MSG msg;

    // 主消息循环:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{ 0 };

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CODERTOYS));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    //wcex.lpszMenuName = 0;
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_CODERTOYS);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 将实例句柄存储在全局变量中


   hWnd = ::CreateWindowW(szWindowClass, szTitle, static_cast<DWORD>(BorderLessManage::Style::aero_borderless),
      0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, hInstance, nullptr);


   /* 透明度
   hWnd = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST, szWindowClass, szTitle, static_cast<DWORD>(BorderLessManage::Style::aero_borderless),
      0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, hInstance, nullptr);   */
   

   if (!hWnd)
   {
      return FALSE;
   }
   //设置透明度
   COLORREF crKey{ 0 };
   ::SetLayeredWindowAttributes(hWnd, crKey, 128, LWA_ALPHA);

   ::ShowWindow(hWnd, nCmdShow);
   ::UpdateWindow(hWnd);

   return TRUE;
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_COMMAND  - 处理应用程序菜单
//  WM_PAINT    - 绘制主窗口
//  WM_DESTROY  - 发送退出消息并返回
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_NCCALCSIZE: {
            if (wParam == TRUE && BorderLessManage::borderless) {
                auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                BorderLessManage::adjust_maximized_client_rect(hWnd, params.rgrc[0]);
                return 0;
            }
            break;
        }
        case WM_NCHITTEST: {
            // When we have no border or title bar, we need to perform our
            // own hit testing to allow resizing and moving.
            if (BorderLessManage::borderless) {
                return BorderLessManage::hit_test(POINT{
                    GET_X_LPARAM(lParam),
                    GET_Y_LPARAM(lParam)
                    });
            }
            break;
        }
        case WM_NCACTIVATE: {
            if (!BorderLessManage::composition_enabled()) {
                // Prevents window frame reappearing on window activation
                // in "basic" theme, where no aero shadow is present.
                return 1;
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            switch (wParam) {
                //case VK_F8: { BorderLessManage::borderless_drag = !BorderLessManage::borderless_drag;        return 0; }
                //case VK_F9: { BorderLessManage::borderless_resize = !BorderLessManage::borderless_resize;    return 0; }
                case VK_F10: { BorderLessManage::set_borderless(!BorderLessManage::borderless);               return 0; }
                //case VK_F11: { BorderLessManage::set_borderless_shadow(!BorderLessManage::borderless_shadow); return 0; }

                case VK_F7: { IconManage::icon_reset();               return 0; }
                case VK_F6: { ::SetParent(hWnd, WallManage::find_wall_window());               return 0; }
            }

            break;
        }
        case WM_CLOSE: {
            ::DestroyWindow(hWnd);
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
#if 0
        case WM_COMMAND:
            {
                int wmId = LOWORD(wParam);
                // 分析菜单选择:
                switch (wmId)
                {
                case IDM_ABOUT:
                    DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                    break;
                case IDM_EXIT:
                    DestroyWindow(hWnd);
                    break;
                default:
                    return DefWindowProc(hWnd, message, wParam, lParam);
                }
            }
            break;
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hWnd, &ps);
                // TODO: 在此处添加使用 hdc 的任何绘图代码...
                EndPaint(hWnd, &ps);
            }
            break;
#endif

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    return 0;
}
#if 0
// “关于”框的消息处理程序。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
#endif