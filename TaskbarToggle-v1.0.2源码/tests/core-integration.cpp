// Opt-in desktop integration check. Run only while a dedicated blank Edge test
// window is maximized; every test path attempts to restore the desktop state.
#define TASKBAR_TOGGLE_TEST
#include "../main.cpp"
#include <cstdio>

namespace {
DWORD edgePid{};
HWND edgeWindow{};
HWND stubbornWindow{};
DWORD stubbornPid{};
int failures = 0;
BOOL CALLBACK FindEdge(HWND window, LPARAM) {
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    wchar_t cls[128]{};
    GetClassNameW(window, cls, 128);
    if (pid == edgePid && IsWindowVisible(window) && IsZoomed(window) &&
        wcscmp(cls, L"Chrome_WidgetWin_1") == 0) edgeWindow = window;
    return TRUE;
}
BOOL CALLBACK FindStubborn(HWND window, LPARAM) {
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    wchar_t cls[128]{};
    GetClassNameW(window, cls, 128);
    if (pid == stubbornPid && IsWindowVisible(window) && IsZoomed(window) &&
        wcscmp(cls, L"TaskbarToggle.IgnoresWorkArea") == 0) stubbornWindow = window;
    return TRUE;
}
void Check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    edgePid = wcstoul(argv[1], nullptr, 10);
    stubbornPid = wcstoul(argv[2], nullptr, 10);
    EnumWindows(FindEdge, 0);
    EnumWindows(FindStubborn, 0);
    if (!edgeWindow) { std::puts("No maximized dedicated Edge window found; no desktop changes made."); return 3; }
    if (!stubbornWindow) { std::puts("No maximized independent test window found; no desktop changes made."); return 5; }
    HWND taskbar{};
    MONITORINFO baseline{};
    DWORD pid{};
    if (!GetTaskbarInfo(taskbar, baseline, pid) || !IsWindowVisible(taskbar)) return 4;
    RECT originalStubborn{};
    GetWindowRect(stubbornWindow, &originalStubborn);
    Check(IsZoomed(stubbornWindow), "independent test process starts maximized");
    g_configPath = L"build\\core-integration-config.ini";
    RECT originalEdge{};
    GetWindowRect(edgeWindow, &originalEdge);
    const LONG expansion = baseline.rcMonitor.bottom - baseline.rcWork.bottom;
    struct Guard { ~Guard() { RestoreTaskbar(false); } } guard;

    for (int iteration = 1; iteration <= 2; ++iteration) {
        std::printf("Cycle %d\n", iteration);
        const bool hidden = HideTaskbar();
        Check(hidden && g_errorCount == 0, "production HideTaskbar succeeds");
        if (!hidden) break;
        Sleep(1000); // Test observation delay only; the product has no polling.
        MONITORINFO after{};
        after.cbSize = sizeof(after);
        GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY), &after);
        Check(!IsWindowVisible(taskbar) && EqualRectValue(after.rcWork, baseline.rcMonitor),
              "taskbar remains hidden and full work area survives application notifications");
        RECT edge{};
        GetWindowRect(edgeWindow, &edge);
        std::printf("Edge bottom: original=%ld hidden=%ld expansion=%ld\n", originalEdge.bottom, edge.bottom, expansion);
        Check(IsZoomed(edgeWindow) && edge.bottom == originalEdge.bottom + expansion &&
              edge.top == originalEdge.top && edge.left == originalEdge.left && edge.right == originalEdge.right,
              "existing Edge expands into taskbar area while remaining maximized");
        RECT stubborn{};
        GetWindowRect(stubbornWindow, &stubborn);
        Check(IsZoomed(stubbornWindow) && stubborn.bottom == originalStubborn.bottom + expansion &&
              stubborn.top == originalStubborn.top && stubborn.left == originalStubborn.left &&
              stubborn.right == originalStubborn.right,
              "independent maximized window ignoring work-area notification expands without losing maximized state");
        Check(RestoreTaskbar(), "production RestoreTaskbar succeeds");
        Sleep(300);
        GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY), &after);
        Check(IsWindowVisible(taskbar) && EqualRectValue(after.rcWork, baseline.rcWork),
              "original taskbar and work area restored");
        GetWindowRect(edgeWindow, &edge);
        Check(IsZoomed(edgeWindow) && EqualRectValue(edge, originalEdge),
              "Edge returns to original maximized bounds");
        GetWindowRect(stubbornWindow, &stubborn);
        Check(IsZoomed(stubbornWindow) && EqualRectValue(stubborn, originalStubborn),
              "independent window ignoring work-area notification returns to original maximized bounds");
    }
    RestoreTaskbar(false);
    Check(g_errorCount == 0, "no production error or rollback path triggered");
    DeleteFileW(g_configPath.c_str());
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
