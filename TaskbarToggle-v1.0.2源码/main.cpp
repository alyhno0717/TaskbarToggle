#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <algorithm>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace {
constexpr wchar_t kClassName[] = L"TaskbarToggle.Settings.v1";
constexpr wchar_t kMutexName[] = L"Local\\TaskbarToggle_Instance";
#ifdef TASKBAR_TOGGLE_TEST
constexpr wchar_t kRunKey[] = L"Software\\TaskbarToggle\\IntegrationTest\\Run";
int g_errorCount = 0;
#else
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif
constexpr wchar_t kRunValue[] = L"TaskbarToggle";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kReopenMessage = WM_APP + 2;
constexpr UINT kToggleCommand = 101;
constexpr UINT kSettingsCommand = 102;
constexpr UINT kAutostartCommand = 103;
constexpr UINT kExitCommand = 104;
constexpr int kHotkeyControl = 201;
constexpr int kAutostartControl = 202;
constexpr int kSaveControl = 203;

HINSTANCE g_instance{};
HWND g_window{}, g_label{}, g_hotkeyControl{}, g_autostartControl{}, g_saveControl{};
HFONT g_font{};
HICON g_icon{};
HANDLE g_mutex{};
std::wstring g_exePath, g_configPath;
UINT g_modifiers = MOD_ALT, g_key = 'Z';
int g_hotkeyId = 1;
bool g_registered = false;
bool g_hidden = false;
bool g_changingTaskbar = false;
RECT g_originalWorkArea{};
RECT g_monitorArea{};
DWORD g_explorerPid{};
UINT g_taskbarCreated{};

void Error(const wchar_t* message, DWORD code = ERROR_SUCCESS) {
#ifdef TASKBAR_TOGGLE_TEST
    (void)message;
    (void)code;
    ++g_errorCount;
    return;
#endif
    std::wstring text(message);
    if (code != ERROR_SUCCESS) {
        wchar_t* detail{};
        if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, code, 0, reinterpret_cast<wchar_t*>(&detail), 0, nullptr)) {
            text += L"\n\n";
            text += detail;
            LocalFree(detail);
        }
    }
    MessageBoxW(g_window, text.c_str(), L"TaskbarToggle", MB_OK | MB_ICONWARNING);
}

bool EqualRectValue(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool ValidRectOnMonitor(const RECT& r, const RECT& monitor) {
    return r.left >= monitor.left && r.top >= monitor.top &&
           r.right <= monitor.right && r.bottom <= monitor.bottom &&
           r.right > r.left && r.bottom > r.top;
}

bool ValidHotkey(UINT modifiers, UINT key) {
    constexpr UINT allowed = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
    return modifiers != 0 && (modifiers & ~allowed) == 0 &&
           ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
            (key >= VK_F1 && key <= VK_F11));
}

WORD HotkeyControlValue(UINT modifiers, UINT key) {
    BYTE flags = 0;
    if (modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
    if (modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
    if (modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
    return MAKEWORD(static_cast<BYTE>(key), flags);
}

std::wstring ExecutablePath() {
    std::wstring path(512, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) return {};
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        if (path.size() >= 32768) return {};
        path.resize(path.size() * 2);
    }
}

bool InitializeConfigPath() {
    PWSTR localAppData{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)))
        return false;
    std::wstring directory(localAppData);
    CoTaskMemFree(localAppData);
    directory += L"\\TaskbarToggle";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    g_configPath = directory + L"\\config.ini";
    return true;
}

bool WriteNumber(const wchar_t* section, const wchar_t* name, long long value) {
    const auto text = std::to_wstring(value);
    return WritePrivateProfileStringW(section, name, text.c_str(), g_configPath.c_str()) != FALSE;
}

bool WriteSettings(UINT modifiers, UINT key) {
    return WriteNumber(L"Settings", L"HotkeyModifiers", modifiers) &&
           WriteNumber(L"Settings", L"HotkeyKey", key);
}

void LoadSettings() {
    const UINT modifiers = GetPrivateProfileIntW(L"Settings", L"HotkeyModifiers", MOD_ALT,
                                                 g_configPath.c_str());
    const UINT key = GetPrivateProfileIntW(L"Settings", L"HotkeyKey", 'Z', g_configPath.c_str());
    if (ValidHotkey(modifiers, key)) {
        g_modifiers = modifiers;
        g_key = key;
    }
}

bool AutostartEnabled() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type{}, size{};
    const LSTATUS status = RegQueryValueExW(key, kRunValue, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ && size > sizeof(wchar_t);
}

LSTATUS SetAutostart(bool enabled) {
    HKEY key{};
    const LSTATUS opened = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                          KEY_SET_VALUE, nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS) return opened;
    LSTATUS status{};
    if (enabled) {
        const std::wstring command = L"\"" + g_exePath + L"\" --silent";
        status = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(command.c_str()),
                               static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kRunValue);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status;
}

// This small recovery record is written only at a toggle, never by a background timer.
// It lets a new instance undo our work-area change after an unexpected process exit.
bool SaveRecovery(const RECT& work, const RECT& monitor, DWORD explorerPid) {
    return WriteNumber(L"Recovery", L"Left", work.left) &&
           WriteNumber(L"Recovery", L"Top", work.top) &&
           WriteNumber(L"Recovery", L"Right", work.right) &&
           WriteNumber(L"Recovery", L"Bottom", work.bottom) &&
           WriteNumber(L"Recovery", L"MonitorLeft", monitor.left) &&
           WriteNumber(L"Recovery", L"MonitorTop", monitor.top) &&
           WriteNumber(L"Recovery", L"MonitorRight", monitor.right) &&
           WriteNumber(L"Recovery", L"MonitorBottom", monitor.bottom) &&
           WriteNumber(L"Recovery", L"ExplorerPid", explorerPid) &&
           WriteNumber(L"Recovery", L"Active", 1);
}

void ClearRecovery() {
    WritePrivateProfileStringW(L"Recovery", nullptr, nullptr, g_configPath.c_str());
}

RECT ReadRecoveryRect(bool monitor) {
    const wchar_t* left = monitor ? L"MonitorLeft" : L"Left";
    const wchar_t* top = monitor ? L"MonitorTop" : L"Top";
    const wchar_t* right = monitor ? L"MonitorRight" : L"Right";
    const wchar_t* bottom = monitor ? L"MonitorBottom" : L"Bottom";
    return {static_cast<LONG>(GetPrivateProfileIntW(L"Recovery", left, 0, g_configPath.c_str())),
            static_cast<LONG>(GetPrivateProfileIntW(L"Recovery", top, 0, g_configPath.c_str())),
            static_cast<LONG>(GetPrivateProfileIntW(L"Recovery", right, 0, g_configPath.c_str())),
            static_cast<LONG>(GetPrivateProfileIntW(L"Recovery", bottom, 0, g_configPath.c_str()))};
}

bool GetTaskbarInfo(HWND& taskbar, MONITORINFO& info, DWORD& pid) {
    taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar) return false;
    info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY), &info)) return false;
    GetWindowThreadProcessId(taskbar, &pid);
    return true;
}

BOOL CALLBACK NotifyWorkAreaWindow(HWND window, LPARAM shellPid) {
    if (window == g_window) return TRUE;
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    if (pid == static_cast<DWORD>(shellPid)) {
        // Shell receives a broadcast by recalculating its taskbar reservation.
        // File Explorer windows can still receive the application notification.
        wchar_t className[128]{};
        GetClassNameW(window, className, 128);
        if (wcscmp(className, L"CabinetWClass") != 0) return TRUE;
    }
    // Include hidden top-level windows: browsers use them to refresh display
    // caches. WM_SETTINGCHANGE has a pointer-bearing message schema, so use
    // synchronous delivery with a bound instead of SendNotifyMessage.
    DWORD_PTR result{};
    SendMessageTimeoutW(window, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result);
    return TRUE;
}

void NotifyApplicationsOfWorkArea(DWORD shellPid) {
    EnumWindows(NotifyWorkAreaWindow, static_cast<LPARAM>(shellPid));
}

struct MaximizedWindowSnapshot {
    HWND window;
    DWORD pid;
    RECT bounds;
};

struct MaximizedWindowCapture {
    DWORD shellPid;
    std::vector<MaximizedWindowSnapshot> windows;
};

BOOL CALLBACK CaptureMaximizedWindow(HWND window, LPARAM contextValue) {
    auto& capture = *reinterpret_cast<MaximizedWindowCapture*>(contextValue);
    if (window == g_window || !IsWindowVisible(window) || !IsZoomed(window)) return TRUE;
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    if (pid == capture.shellPid) {
        wchar_t className[128]{};
        GetClassNameW(window, className, 128);
        if (wcscmp(className, L"CabinetWClass") != 0) return TRUE;
    }
    RECT bounds{};
    if (GetWindowRect(window, &bounds)) capture.windows.push_back({window, pid, bounds});
    return TRUE;
}

MaximizedWindowCapture CaptureMaximizedWindows(DWORD shellPid) {
    MaximizedWindowCapture capture{shellPid, {}};
    EnumWindows(CaptureMaximizedWindow, reinterpret_cast<LPARAM>(&capture));
    return capture;
}

unsigned int ResizeMaximizedWindows(const MaximizedWindowCapture& capture,
                                    const RECT& oldArea, const RECT& newArea) {
    unsigned int failures = 0;
    for (const auto& snapshot : capture.windows) {
        if (!IsWindow(snapshot.window) || !IsWindowVisible(snapshot.window) ||
            !IsZoomed(snapshot.window)) continue;
        DWORD pid{};
        GetWindowThreadProcessId(snapshot.window, &pid);
        if (pid != snapshot.pid) continue;
        const RECT desired{
            snapshot.bounds.left + newArea.left - oldArea.left,
            snapshot.bounds.top + newArea.top - oldArea.top,
            snapshot.bounds.right + newArea.right - oldArea.right,
            snapshot.bounds.bottom + newArea.bottom - oldArea.bottom};
        RECT current{};
        if (!GetWindowRect(snapshot.window, &current) || EqualRectValue(current, desired)) continue;
        // Preserve the app's maximized state, invisible frame margins, focus,
        // and Z order. ASYNCWINDOWPOS avoids blocking on another UI thread.
        if (!SetWindowPos(snapshot.window, nullptr, desired.left, desired.top,
                          desired.right - desired.left, desired.bottom - desired.top,
                          SWP_ASYNCWINDOWPOS | SWP_NOACTIVATE | SWP_NOZORDER |
                              SWP_NOOWNERZORDER)) ++failures;
    }
    return failures;
}

// Used only if the saved geometry no longer matches the current display.
RECT WorkAreaFromTaskbar(const MONITORINFO& info) {
    RECT area = info.rcWork;
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    if (SHAppBarMessage(ABM_GETSTATE, &data) & ABS_AUTOHIDE) return area;
    if (!SHAppBarMessage(ABM_GETTASKBARPOS, &data)) return area;
    RECT candidate = info.rcMonitor;
    switch (data.uEdge) {
        case ABE_BOTTOM: candidate.bottom = data.rc.top; break;
        case ABE_TOP: candidate.top = data.rc.bottom; break;
        case ABE_LEFT: candidate.left = data.rc.right; break;
        case ABE_RIGHT: candidate.right = data.rc.left; break;
        default: return area;
    }
    return ValidRectOnMonitor(candidate, info.rcMonitor) ? candidate : area;
}

bool RestoreTaskbar(bool reportError = true) {
    if (!g_hidden) return true;
    HWND taskbar{};
    MONITORINFO info{};
    DWORD pid{};
    if (!GetTaskbarInfo(taskbar, info, pid)) {
        if (reportError) Error(L"未找到 Windows 任务栏，暂时无法恢复。请待 Explorer 启动后再次运行程序。");
        return false;
    }
    const RECT previousArea = info.rcWork;
    const auto windows = CaptureMaximizedWindows(pid);
    g_changingTaskbar = true;
    ShowWindow(taskbar, SW_SHOW);
    // Never apply a stale rectangle after a display or Explorer change.
    RECT area = g_originalWorkArea;
    if (pid != g_explorerPid || !EqualRectValue(info.rcMonitor, g_monitorArea) ||
        !ValidRectOnMonitor(area, info.rcMonitor)) {
        GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY), &info);
        area = WorkAreaFromTaskbar(info);
    }
    const BOOL changed = SystemParametersInfoW(SPI_SETWORKAREA, 0, &area, SPIF_SENDCHANGE);
    const DWORD error = changed ? ERROR_SUCCESS : GetLastError();
    MONITORINFO after{};
    after.cbSize = sizeof(after);
    const bool verified = IsWindowVisible(taskbar) && changed &&
        GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY), &after) &&
        EqualRectValue(after.rcWork, area);
    if (!verified) {
        g_changingTaskbar = false;
        if (reportError) Error(L"任务栏或工作区未完全恢复。请再次运行 EXE 重试；若仍失败，可重启 Windows Explorer。", error);
        return false;
    }
    const unsigned int windowFailures = ResizeMaximizedWindows(windows, previousArea, area);
    g_changingTaskbar = false;
    g_hidden = false;
    ClearRecovery();
    if (windowFailures && reportError)
        Error(L"任务栏已恢复，但部分最大化窗口未能自动缩回。请检查这些程序是否以管理员权限运行。");
    return true;
}

void RecoverPreviousRun() {
    if (GetPrivateProfileIntW(L"Recovery", L"Active", 0, g_configPath.c_str()) == 0) return;
    HWND taskbar{};
    MONITORINFO info{};
    DWORD pid{};
    if (!GetTaskbarInfo(taskbar, info, pid)) return;
    const DWORD savedPid = GetPrivateProfileIntW(L"Recovery", L"ExplorerPid", 0, g_configPath.c_str());
    // A fresh Explorer owns fresh taskbar/work-area state. Leave that state intact.
    if (pid != savedPid) {
        ClearRecovery();
        return;
    }
    g_originalWorkArea = ReadRecoveryRect(false);
    g_monitorArea = ReadRecoveryRect(true);
    g_explorerPid = pid;
    g_hidden = true;
    RestoreTaskbar();
}

bool HideTaskbar() {
    HWND taskbar{};
    MONITORINFO info{};
    DWORD pid{};
    if (!GetTaskbarInfo(taskbar, info, pid)) {
        Error(L"未找到 Windows 任务栏。本工具面向 Windows 11 原生任务栏。");
        return false;
    }
    if (GetSystemMetrics(SM_CMONITORS) != 1) {
        Error(L"当前版本仅支持单显示器。检测到多块屏幕，未修改任务栏。");
        return false;
    }
    if (!IsWindowVisible(taskbar)) {
        Error(L"任务栏当前已被其他程序隐藏。请先恢复任务栏，再使用 TaskbarToggle。");
        return false;
    }
    g_originalWorkArea = info.rcWork;
    g_monitorArea = info.rcMonitor;
    g_explorerPid = pid;
    if (!SaveRecovery(g_originalWorkArea, g_monitorArea, pid)) {
        Error(L"无法保存恢复信息，未隐藏任务栏。请检查本地配置目录是否可写。", GetLastError());
        return false;
    }
    const auto windows = CaptureMaximizedWindows(pid);
    g_changingTaskbar = true;
    ShowWindow(taskbar, SW_HIDE);
    RECT fullArea = info.rcMonitor;
    // On this Windows 11 build, SPIF_SENDCHANGE makes Explorer immediately
    // restore the taskbar reservation. Set the session work area first, then
    // notify application windows while leaving Shell's bookkeeping untouched.
    const BOOL changed = SystemParametersInfoW(SPI_SETWORKAREA, 0, &fullArea, 0);
    const DWORD error = changed ? ERROR_SUCCESS : GetLastError();
    if (changed) NotifyApplicationsOfWorkArea(pid);
    MONITORINFO after{};
    after.cbSize = sizeof(after);
    const bool taskbarHidden = !IsWindowVisible(taskbar);
    const bool verified = taskbarHidden && changed &&
        GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY), &after) &&
        EqualRectValue(after.rcWork, fullArea);
    g_hidden = true; // Keep ownership until rollback or restoration has completed.
    if (!verified) {
        g_changingTaskbar = false;
        RestoreTaskbar(false);
        if (!changed) Error(L"Windows 拒绝修改工作区。已尝试恢复，核心切换未完成。", error);
        else if (!taskbarHidden) Error(L"Windows 未保持任务栏隐藏。已尝试恢复，核心切换未完成。");
        else Error(L"Windows 未保持完整工作区。已尝试恢复，核心切换未完成。");
        return false;
    }
    const unsigned int windowFailures = ResizeMaximizedWindows(windows, g_originalWorkArea, fullArea);
    g_changingTaskbar = false;
    if (windowFailures)
        Error(L"任务栏已隐藏，但部分最大化窗口未能自动扩展。请检查这些程序是否以管理员权限运行。");
    return true;
}

void ToggleTaskbar() {
    if (g_changingTaskbar) return;
    if (g_hidden) RestoreTaskbar();
    else HideTaskbar();
}

HICON CreateAppIcon() {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 32;
    info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels{};
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                                     reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!color) return LoadIconW(nullptr, IDI_APPLICATION);
    std::fill_n(pixels, 32 * 32, 0u);
    for (int y = 2; y < 30; ++y) {
        for (int x = 2; x < 30; ++x) {
            const int dx = std::max(7 - x, std::max(x - 24, 0));
            const int dy = std::max(7 - y, std::max(y - 24, 0));
            if (dx * dx + dy * dy <= 25) pixels[y * 32 + x] = 0xFF000000;
        }
    }
    for (int y = 9; y <= 18; ++y)
        for (int x = 8; x <= 23; ++x)
            if (y == 9 || y == 18 || x == 8 || x == 23) pixels[y * 32 + x] = 0xFFFFFFFF;
    for (int y = 22; y <= 24; ++y)
        for (int x = 8; x <= 23; ++x) pixels[y * 32 + x] = 0xFFFFFFFF;
    const BYTE maskBits[128]{};
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, maskBits);
    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = color;
    iconInfo.hbmMask = mask;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(color);
    DeleteObject(mask);
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

bool AddTrayIcon() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = g_icon;
    wcscpy_s(data.szTip, L"TaskbarToggle");
    if (!Shell_NotifyIconW(NIM_ADD, &data)) return false;
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    return true;
}

void DeleteTrayIcon() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_window;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void RefreshControls() {
    SendMessageW(g_hotkeyControl, HKM_SETHOTKEY, HotkeyControlValue(g_modifiers, g_key), 0);
    SendMessageW(g_autostartControl, BM_SETCHECK, AutostartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
}

void ShowSettings() {
    RefreshControls();
    ShowWindow(g_window, SW_RESTORE);
    SetForegroundWindow(g_window);
    SetFocus(g_hotkeyControl);
}

void SaveSettings() {
    const WORD captured = static_cast<WORD>(SendMessageW(g_hotkeyControl, HKM_GETHOTKEY, 0, 0));
    const BYTE flags = HIBYTE(captured);
    UINT modifiers = 0;
    if (flags & HOTKEYF_ALT) modifiers |= MOD_ALT;
    if (flags & HOTKEYF_CONTROL) modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_SHIFT) modifiers |= MOD_SHIFT;
    const UINT key = LOBYTE(captured);
    if (!ValidHotkey(modifiers, key)) {
        Error(L"请使用 Ctrl、Alt 或 Shift 搭配字母、数字或 F1–F11。至少需要一个修饰键。");
        return;
    }
    const bool changed = !g_registered || modifiers != g_modifiers || key != g_key;
    const int nextId = g_hotkeyId == 1 ? 2 : 1;
    if (changed && !RegisterHotKey(g_window, nextId, modifiers | MOD_NOREPEAT, key)) {
        Error(L"快捷键无法注册，可能已被占用。请更换组合键；旧快捷键保持有效。", GetLastError());
        return;
    }
    const bool oldAutostart = AutostartEnabled();
    const bool newAutostart = SendMessageW(g_autostartControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const LSTATUS startupStatus = SetAutostart(newAutostart);
    if (startupStatus != ERROR_SUCCESS) {
        if (changed) UnregisterHotKey(g_window, nextId);
        Error(L"无法保存开机启动设置。", startupStatus);
        return;
    }
    if (!WriteSettings(modifiers, key)) {
        const DWORD error = GetLastError();
        SetAutostart(oldAutostart);
        WriteSettings(g_modifiers, g_key);
        if (changed) UnregisterHotKey(g_window, nextId);
        Error(L"无法保存快捷键配置，已保留原设置。", error);
        return;
    }
    if (changed) {
        if (g_registered) UnregisterHotKey(g_window, g_hotkeyId);
        g_hotkeyId = nextId;
        g_registered = true;
        g_modifiers = modifiers;
        g_key = key;
    }
    ShowWindow(g_window, SW_HIDE);
}

void ExitApp() {
    if (!RestoreTaskbar()) return;
    DestroyWindow(g_window);
}

void TrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kToggleCommand, g_hidden ? L"显示任务栏" : L"隐藏任务栏");
    AppendMenuW(menu, MF_STRING, kSettingsCommand, L"设置");
    AppendMenuW(menu, MF_STRING | (AutostartEnabled() ? MF_CHECKED : MF_UNCHECKED),
                kAutostartCommand, L"开机启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitCommand, L"退出");
    SetForegroundWindow(g_window);
    const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        point.x, point.y, 0, g_window, nullptr);
    DestroyMenu(menu);
    PostMessageW(g_window, WM_NULL, 0, 0);
    if (selected) SendMessageW(g_window, WM_COMMAND, selected, 0);
}

void Layout(UINT dpi) {
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
    HFONT newFont = CreateFontIndirectW(&metrics.lfMessageFont);
    if (newFont) {
        for (HWND control : {g_label, g_hotkeyControl, g_autostartControl, g_saveControl})
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
        if (g_font) DeleteObject(g_font);
        g_font = newFont;
    }
    MoveWindow(g_label, scale(24), scale(21), scale(320), scale(24), TRUE);
    MoveWindow(g_hotkeyControl, scale(24), scale(49), scale(312), scale(30), TRUE);
    MoveWindow(g_autostartControl, scale(24), scale(99), scale(200), scale(26), TRUE);
    MoveWindow(g_saveControl, scale(248), scale(146), scale(88), scale(30), TRUE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreated && message == g_taskbarCreated) {
        // This broadcast also occurs on some DPI changes. Restore before discarding
        // an owned snapshot; RestoreTaskbar checks Explorer and monitor identity.
        if (g_hidden && !g_changingTaskbar) RestoreTaskbar(false);
        else if (!g_hidden) ClearRecovery();
        AddTrayIcon();
        return 0;
    }
    switch (message) {
        case WM_CREATE: {
            g_window = window;
            g_label = CreateWindowExW(0, L"STATIC", L"切换快捷键", WS_CHILD | WS_VISIBLE,
                                      0, 0, 0, 0, window, nullptr, g_instance, nullptr);
            g_hotkeyControl = CreateWindowExW(WS_EX_CLIENTEDGE, HOTKEY_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(kHotkeyControl), g_instance, nullptr);
            g_autostartControl = CreateWindowExW(0, L"BUTTON", L"开机启动",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(kAutostartControl), g_instance, nullptr);
            g_saveControl = CreateWindowExW(0, L"BUTTON", L"保存",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(kSaveControl), g_instance, nullptr);
            Layout(GetDpiForWindow(window));
            RefreshControls();
            return 0;
        }
        case WM_DPICHANGED: {
            const auto rect = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                         rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            Layout(HIWORD(wParam));
            return 0;
        }
        case WM_DISPLAYCHANGE:
            if (g_hidden && !g_changingTaskbar) RestoreTaskbar();
            return 0;
        case WM_HOTKEY:
            if (g_registered && static_cast<int>(wParam) == g_hotkeyId &&
                !(IsWindowVisible(window) && GetFocus() == g_hotkeyControl)) ToggleTaskbar();
            return 0;
        case kReopenMessage:
            RestoreTaskbar();
            ShowSettings();
            return 0;
        case kTrayMessage: {
            const UINT event = LOWORD(lParam);
            if (event == WM_LBUTTONDBLCLK) ShowSettings();
            else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
                POINT point{};
                GetCursorPos(&point);
                TrayMenu(point);
            } else if (event == NIN_KEYSELECT) ShowSettings();
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kToggleCommand: ToggleTaskbar(); break;
                case kSettingsCommand: ShowSettings(); break;
                case IDOK:
                case kSaveControl: SaveSettings(); break;
                case IDCANCEL: ShowWindow(window, SW_HIDE); break;
                case kAutostartCommand: {
                    const LSTATUS status = SetAutostart(!AutostartEnabled());
                    if (status != ERROR_SUCCESS) Error(L"无法修改开机启动设置。", status);
                    RefreshControls();
                    break;
                }
                case kExitCommand: ExitApp(); break;
            }
            return 0;
        case WM_CLOSE:
            ShowWindow(window, SW_HIDE);
            return 0;
        case WM_QUERYENDSESSION:
            RestoreTaskbar(false);
            return TRUE;
        case WM_ENDSESSION:
            if (wParam) DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (g_registered) UnregisterHotKey(window, g_hotkeyId);
            DeleteTrayIcon();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_instance = instance;
    g_mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!g_mutex) {
        Error(L"无法创建单实例标记。", GetLastError());
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kClassName, nullptr);
        if (existing) {
            DWORD pid{};
            GetWindowThreadProcessId(existing, &pid);
            AllowSetForegroundWindow(pid);
            DWORD_PTR result{};
            if (!SendMessageTimeoutW(existing, kReopenMessage, 0, 0,
                                     SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &result))
                Error(L"现有进程未响应恢复请求。请在任务管理器中结束 TaskbarToggle 后再次运行。");
        } else {
            Error(L"TaskbarToggle 已在运行，但尚未找到其窗口。请稍后再次运行。");
        }
        CloseHandle(g_mutex);
        return 0;
    }
    g_exePath = ExecutablePath();
    if (g_exePath.empty() || !InitializeConfigPath()) {
        Error(L"无法读取程序路径或创建本地配置目录。", GetLastError());
        CloseHandle(g_mutex);
        return 1;
    }
    const bool firstRun = GetFileAttributesW(g_configPath.c_str()) == INVALID_FILE_ATTRIBUTES;
    bool silent = false;
    int argc{};
    PWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i)
            if (wcscmp(argv[i], L"--silent") == 0) silent = true;
        LocalFree(argv);
    }
    LoadSettings();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    g_icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, 0));
    if (!g_icon) g_icon = CreateAppIcon();
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.hIcon = g_icon;
    cls.hIconSm = g_icon;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = kClassName;
    if (!RegisterClassExW(&cls)) {
        Error(L"无法注册设置窗口。", GetLastError());
        CloseHandle(g_mutex);
        return 1;
    }
    const UINT dpi = GetDpiForSystem();
    RECT rect{0, 0, MulDiv(360, static_cast<int>(dpi), 96), MulDiv(200, static_cast<int>(dpi), 96)};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    HWND window = CreateWindowExW(0, kClassName, L"TaskbarToggle", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                  rect.right - rect.left, rect.bottom - rect.top,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) {
        Error(L"无法创建设置窗口。", GetLastError());
        CloseHandle(g_mutex);
        return 1;
    }
    RecoverPreviousRun();
    g_registered = RegisterHotKey(window, g_hotkeyId, g_modifiers | MOD_NOREPEAT, g_key) != FALSE;
    const DWORD hotkeyError = g_registered ? ERROR_SUCCESS : GetLastError();
    const bool trayAdded = AddTrayIcon();
    if (firstRun) WriteSettings(g_modifiers, g_key);
    if ((firstRun && !silent) || !g_registered || !trayAdded) ShowSettings();
    if (!g_registered) Error(L"快捷键无法注册，可能已被占用。请在设置中选择其他组合键。", hotkeyError);
    if (!trayAdded) Error(L"暂时无法添加托盘图标。设置窗口将保持打开；再次运行 EXE 也可打开设置。");
    MSG message{};
    BOOL result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (result == -1) RestoreTaskbar(false);
    if (g_font) DeleteObject(g_font);
    if (g_icon) DestroyIcon(g_icon);
    CloseHandle(g_mutex);
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
