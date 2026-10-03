#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <vector>

HWND taskbar;
HMONITOR monitor;
RECT original;
bool originallyVisible;
DWORD explorerPid;
struct MaximizedWindow { HWND window; RECT rect; };
std::vector<MaximizedWindow> maximized;

BOOL CALLBACK CaptureMaximized(HWND window, LPARAM) {
    if (IsWindowVisible(window) && IsZoomed(window)) {
        RECT rect{};
        GetWindowRect(window, &rect);
        maximized.push_back({window, rect});
    }
    return TRUE;
}

BOOL CALLBACK NotifyWindow(HWND window, LPARAM includeFileExplorer) {
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    wchar_t cls[128]{};
    GetClassNameW(window, cls, 128);
    if (pid != explorerPid ||
        (includeFileExplorer && wcscmp(cls, L"CabinetWClass") == 0)) {
        DWORD_PTR result{};
        SendMessageTimeoutW(window, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result);
    }
    return TRUE;
}

void WindowSizes(const char* label) {
    for (const auto& item : maximized) {
        if (!IsWindow(item.window)) continue;
        wchar_t cls[128]{};
        RECT r{};
        DWORD pid{};
        GetClassNameW(item.window, cls, 128);
        GetWindowRect(item.window, &r);
        GetWindowThreadProcessId(item.window, &pid);
        std::printf("%s class=%ls pid=%lu maximized=%d rect=[%ld,%ld,%ld,%ld] baselineBottom=%ld\n",
                    label, cls, pid, IsZoomed(item.window), r.left, r.top, r.right, r.bottom, item.rect.bottom);
    }
}

void Snapshot(const char* label) {
    MONITORINFO info{sizeof(info)};
    RECT spi{};
    GetMonitorInfoW(monitor, &info);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &spi, 0);
    std::printf("%s visible=%d work=[%ld,%ld,%ld,%ld] spi=[%ld,%ld,%ld,%ld]\n",
        label, IsWindowVisible(taskbar), info.rcWork.left, info.rcWork.top,
        info.rcWork.right, info.rcWork.bottom, spi.left, spi.top, spi.right, spi.bottom);
    std::fflush(stdout);
}

void Restore() {
    ShowWindow(taskbar, originallyVisible ? SW_SHOW : SW_HIDE);
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &original, SPIF_SENDCHANGE);
    if (!originallyVisible) ShowWindow(taskbar, SW_HIDE);
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar) return 2;
    monitor = MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return 3;
    original = info.rcWork;
    originallyVisible = IsWindowVisible(taskbar) != FALSE;
    GetWindowThreadProcessId(taskbar, &explorerPid);
    if (!originallyVisible) { std::puts("Taskbar already hidden; probe aborted without changes."); return 4; }
    struct Guard { ~Guard() { Restore(); } } guard;
    const HWND foreground = GetForegroundWindow();
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = L"TaskbarToggle.ProbeFixture";
    RegisterClassW(&cls);
    HWND fixture = CreateWindowExW(0, cls.lpszClassName, L"TaskbarToggle temporary diagnostic",
        WS_OVERLAPPEDWINDOW, 100, 100, 640, 400, nullptr, nullptr, cls.hInstance, nullptr);
    ShowWindow(fixture, SW_SHOWMAXIMIZED);
    EnumWindows(CaptureMaximized, 0);
    Snapshot("baseline");
    std::printf("monitor=[%ld,%ld,%ld,%ld]\n", info.rcMonitor.left, info.rcMonitor.top,
                info.rcMonitor.right, info.rcMonitor.bottom);
    for (int trial = 3; trial <= 5; ++trial) {
        std::printf("TRIAL %d\n", trial);
        if (trial == 2) {
            SetLastError(0);
            const BOOL ok = SystemParametersInfoW(SPI_SETWORKAREA, 0, &info.rcMonitor, SPIF_SENDCHANGE);
            std::printf("SPI first result=%d error=%lu\n", ok, GetLastError());
            Snapshot("after SPI first");
            ShowWindow(taskbar, SW_HIDE);
        } else {
            ShowWindow(taskbar, SW_HIDE);
            Snapshot("after hide");
            SetLastError(0);
            const BOOL ok = SystemParametersInfoW(SPI_SETWORKAREA, 0, &info.rcMonitor,
                                                  trial == 1 ? SPIF_SENDCHANGE : 0);
            std::printf("SPI after hide result=%d error=%lu\n", ok, GetLastError());
        }
        Snapshot("immediate");
        WindowSizes("immediate app");
        if (trial >= 4) {
            EnumWindows(NotifyWindow, trial == 5);
            Snapshot("after selective notification");
        }
        Sleep(100);
        Snapshot("100ms");
        Sleep(500);
        Snapshot("600ms");
        WindowSizes("600ms app");
        Restore();
        Sleep(200);
        Snapshot("restored");
    }
    DestroyWindow(fixture);
    SetForegroundWindow(foreground);
    return 0;
}
