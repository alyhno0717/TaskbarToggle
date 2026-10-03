// Integration tests exercise the production settings path against real Win32
// hotkey registration and INI APIs, with an isolated registry path and no UI.
#define TASKBAR_TOGGLE_TEST
#include "../main.cpp"
#include <cstdio>

namespace {
int failures = 0;
void Check(bool condition, const char* name) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}
struct KeyProbe {
    UINT modifiers, key;
    HANDLE ready{}, release{};
    bool registered{};
};
DWORD WINAPI ProbeThread(void* context) {
    auto& probe = *static_cast<KeyProbe*>(context);
    HWND owner = CreateWindowExW(0, L"STATIC", L"TestHotkeyOwner", 0, 0, 0, 0, 0,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    probe.registered = RegisterHotKey(owner, 500, probe.modifiers | MOD_NOREPEAT, probe.key) != FALSE;
    SetEvent(probe.ready);
    WaitForSingleObject(probe.release, INFINITE);
    if (probe.registered) UnregisterHotKey(owner, 500);
    DestroyWindow(owner);
    return 0;
}
HANDLE BeginProbe(KeyProbe& probe) {
    probe.ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    probe.release = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, ProbeThread, &probe, 0, nullptr);
    WaitForSingleObject(probe.ready, 5000);
    return thread;
}
void EndProbe(KeyProbe& probe, HANDLE thread) {
    SetEvent(probe.release);
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    CloseHandle(probe.ready);
    CloseHandle(probe.release);
}
bool ProbeAvailable(UINT modifiers, UINT key) {
    KeyProbe probe{modifiers, key};
    HANDLE thread = BeginProbe(probe);
    const bool result = probe.registered;
    EndProbe(probe, thread);
    return result;
}
}

int wmain() {
    g_instance = GetModuleHandleW(nullptr);
    g_configPath = L"build\\integration-config.ini";
    g_exePath = L"C:\\Apps With Spaces\\TaskbarToggle.exe";
    DeleteFileW(g_configPath.c_str());
    SetAutostart(false);
    g_modifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT;
    g_key = VK_F11;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = g_instance;
    cls.lpszClassName = kClassName;
    RegisterClassExW(&cls);
    HWND window = CreateWindowExW(0, kClassName, L"TaskbarToggle Integration Test", 0,
                                  0, 0, 540, 360, nullptr, nullptr, g_instance, nullptr);
    Check(window && g_hotkeyControl && g_autostartControl && g_saveControl, "native controls created");
    g_registered = RegisterHotKey(window, g_hotkeyId, g_modifiers | MOD_NOREPEAT, g_key) != FALSE;
    Check(g_registered, "initial hotkey registered");
    if (!g_registered) {
        DestroyWindow(window);
        return 1;
    }

    KeyProbe blocked{g_modifiers, VK_F10};
    HANDLE blocker = BeginProbe(blocked);
    Check(blocked.registered, "conflicting hotkey held by another thread");
    SendMessageW(g_hotkeyControl, HKM_SETHOTKEY, HotkeyControlValue(g_modifiers, VK_F10), 0);
    SaveSettings();
    Check(g_errorCount == 1 && g_key == VK_F11 && g_hotkeyId == 1, "conflict rejected without changing settings");
    Check(!ProbeAvailable(g_modifiers, VK_F11), "old hotkey remains registered after conflict");
    EndProbe(blocked, blocker);

    SendMessageW(g_hotkeyControl, HKM_SETHOTKEY, HotkeyControlValue(g_modifiers, VK_F9), 0);
    SaveSettings();
    Check(g_key == VK_F9 && g_hotkeyId == 2 && g_registered, "new hotkey saved and activated");
    Check(ProbeAvailable(g_modifiers, VK_F11), "old hotkey released after successful save");
    Check(!ProbeAvailable(g_modifiers, VK_F9), "new hotkey registered globally");
    g_modifiers = MOD_ALT;
    g_key = 'Z';
    LoadSettings();
    Check(g_modifiers == (MOD_ALT | MOD_CONTROL | MOD_SHIFT) && g_key == VK_F9, "INI settings reload correctly");

    SendMessageW(g_hotkeyControl, HKM_SETHOTKEY, 0, 0);
    SaveSettings();
    Check(g_errorCount == 2 && g_key == VK_F9 && !ProbeAvailable(g_modifiers, VK_F9), "invalid hotkey rejected while active hotkey remains");

    SendMessageW(g_hotkeyControl, HKM_SETHOTKEY, HotkeyControlValue(g_modifiers, g_key), 0);
    SendMessageW(g_autostartControl, BM_SETCHECK, BST_CHECKED, 0);
    SaveSettings();
    Check(AutostartEnabled(), "autostart enabled in isolated test key");
    wchar_t command[1024]{};
    DWORD size = sizeof(command);
    const LSTATUS read = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ,
                                      nullptr, command, &size);
    Check(read == ERROR_SUCCESS && std::wstring(command) == L"\"C:\\Apps With Spaces\\TaskbarToggle.exe\" --silent", "autostart command quotes executable path and is silent");
    SendMessageW(g_autostartControl, BM_SETCHECK, BST_UNCHECKED, 0);
    SaveSettings();
    Check(!AutostartEnabled(), "autostart disabled in isolated test key");

    const RECT full{0, 0, 2560, 1600}, work{0, 0, 2560, 1528}, invalid{0, 0, 3000, 1600};
    Check(ValidRectOnMonitor(work, full) && !ValidRectOnMonitor(invalid, full), "stale work-area rectangles rejected");
    Check(SaveRecovery(work, full, 12345), "recovery record persisted before hiding");
    Check(EqualRectValue(ReadRecoveryRect(false), work) && EqualRectValue(ReadRecoveryRect(true), full), "recovery geometry round-trips");
    ClearRecovery();
    Check(GetPrivateProfileIntW(L"Recovery", L"Active", 0, g_configPath.c_str()) == 0, "recovery record cleared after restoration");

    DestroyWindow(window);
    if (g_font) DeleteObject(g_font);
    RegDeleteKeyW(HKEY_CURRENT_USER, kRunKey);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\TaskbarToggle\\IntegrationTest");
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\TaskbarToggle");
    DeleteFileW(g_configPath.c_str());
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
