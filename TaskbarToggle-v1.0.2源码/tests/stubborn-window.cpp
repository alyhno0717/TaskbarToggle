// Independent GUI process that deliberately ignores work-area notifications.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA) return 0;
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = L"TaskbarToggle.IgnoresWorkArea";
    if (!RegisterClassW(&cls)) return 1;
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"TaskbarToggle work area test",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  640, 480, nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    // Start-Process supplies a hidden startup hint for the test helper. The
    // first ShowWindow consumes that hint; the second creates the visible
    // maximized window whose geometry the production code must update.
    ShowWindow(window, SW_SHOWMAXIMIZED);
    ShowWindow(window, SW_SHOWMAXIMIZED);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
