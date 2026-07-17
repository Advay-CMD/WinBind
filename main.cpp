// WinBind - Entry point
// Creates a hidden message-only window, runs the keyboard hook message loop,
// and periodically applies window transparency & window styling.

#include "keybinder.h"
#include "Config.h"
#include "Transparency/Transparency.h"
#include "WindowStyler/WindowStyler.h"
#include <windows.h>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>
#include <psapi.h>

#define TIMER_TRANSPARENCY 1
#define TIMER_WINDOWSTYLER 2

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static std::vector<std::string> StartupApps;

// Parse a ["app.exe", "app2.exe"] list from config
static std::vector<std::string> ParseList(const std::string& val) {
    std::vector<std::string> result;
    std::string v = val;
    if (v.size() >= 2 && v[0] == '[' && v.back() == ']')
        v = v.substr(1, v.size() - 2);
    v = Trim(v);
    if (v.empty()) return result;
    size_t start = 0;
    while (true) {
        size_t comma = v.find(',', start);
        std::string item = Trim(v.substr(start, (comma == std::string::npos) ? std::string::npos : comma - start));
        if (item.size() >= 2 && item[0] == '"' && item.back() == '"')
            item = item.substr(1, item.size() - 2);
        if (!item.empty()) result.push_back(item);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

void LoadStartupConfig() {
    StartupApps.clear();

    std::ifstream file(g_ConfigPath.c_str());
    if (!file.is_open())
    {
        std::wstring wpath(g_ConfigPath.begin(), g_ConfigPath.end());
        std::wstring msg = L"Could not open '" + wpath + L"'.\n\n"
            L"Are you sure you didn't delete it?\n"
            L" Something is SUS...";
        MessageBoxW(
            nullptr,
            msg.c_str(),
            L"WinBind - Startup Error",
            MB_OK | MB_ICONERROR);
            return;
    }

    std::string line;
    bool inStartup = false;
    std::string startupAccum;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            if (!inStartup) continue;
            // Inside multi-line, empty lines and comments are allowed
            continue;
        }

        if (inStartup) {
            if (line == "]") {
                StartupApps = ParseList(startupAccum);
                inStartup = false;
            } else if (line == "[") {
                // Opening bracket alone, accumulator starts fresh
                startupAccum = "";
            } else {
                // Quoted or unquoted item
                if (!startupAccum.empty()) startupAccum += ",";
                if (line.size() >= 2 && line[0] == '"' && line.back() == '"')
                    startupAccum += line;
                else
                    startupAccum += "\"" + line + "\"";
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = Trim(line.substr(0, eq));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (key == "startup") {
            std::string val = Trim(line.substr(eq + 1));
            if (val.find(']') != std::string::npos) {
                // Single-line format: Startup = ["a", "b"]
                StartupApps = ParseList(val);
            } else {
                // Multi-line starts: collect lines until ]
                inStartup = true;
                startupAccum = val;
            }
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KB_EXECUTE) {
        Keybinder::Instance().ExecuteQueuedAction((int)wParam);
        return 0;
    }
    if (msg == WM_TIMER && wParam == TIMER_TRANSPARENCY) {
        int opacity = LoadTransparencyConfig();
        if (opacity > 0 && opacity < 100)
            EnumWindows(EnumWindowsProc, (LPARAM)opacity);
        return 0;
    }
    if (msg == WM_TIMER && wParam == TIMER_WINDOWSTYLER) {
        WindowStylerSettings ws = LoadWindowStylerConfig();
        if (ws.enabled)
            EnumWindows(WindowStylerEnumProc, (LPARAM)&ws);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    Keybinder& kb = Keybinder::Instance();

    const char* cmdLine = GetCommandLineA();
    const char* flag = strstr(cmdLine, "--config");
    if (flag) {
        const char* val = flag + 8;
        while (*val == ' ' || *val == '=') val++;
        if (*val) g_ConfigPath = val;
    }

    kb.LoadConfig(g_ConfigPath);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"KeybinderHiddenWnd";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, L"KeybinderHiddenWnd", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, hInst, NULL);
    kb.SetWindowHandle(hWnd);

    if (!kb.Init()) return 1;

    // Apply StealFocus at startup if enabled
    {
        WindowStylerSettings ws = LoadWindowStylerConfig();
        if (ws.stealFocus) StealFocus(TRUE);
    }

    int delayMs = LoadTransparencyDelayMs();
    SetTimer(hWnd, TIMER_TRANSPARENCY, delayMs, NULL);

    int wsDelayMs = LoadWindowStylerDelayMs();
    SetTimer(hWnd, TIMER_WINDOWSTYLER, wsDelayMs, NULL);

    LoadStartupConfig();

    for (const auto& app : StartupApps) {
        // Split app into path + args (handles quoted paths with spaces)
        std::string appPath = app;
        std::string appArgs;
        if (appPath.size() >= 2 && appPath[0] == '"') {
            size_t closeQuote = appPath.find('"', 1);
            if (closeQuote != std::string::npos) {
                appArgs = Trim(appPath.substr(closeQuote + 1));
                appPath = appPath.substr(1, closeQuote - 1);
            }
        } else {
            size_t sp = appPath.find(' ');
            if (sp != std::string::npos) {
                appArgs = Trim(appPath.substr(sp + 1));
                appPath = appPath.substr(0, sp);
            }
        }

    HINSTANCE result = ShellExecuteA(
        nullptr,
        "open",
        appPath.c_str(),
        appArgs.empty() ? nullptr : appArgs.c_str(),
        nullptr,
        SW_SHOWNORMAL);

    if ((INT_PTR)result <= 32) {
        std::wstring wapp(app.begin(), app.end());
        std::wstring msg;

        switch ((INT_PTR)result) {
        case 0:
            msg = L"Failed to launch '" + wapp +
                  L"': Out of memory or resources.";
            break;

        case ERROR_FILE_NOT_FOUND:
            msg = L"Failed to launch '" + wapp +
                  L"': File not found.";
            break;

        case ERROR_PATH_NOT_FOUND:
            msg = L"Failed to launch '" + wapp +
                  L"': Path not found.";
            break;

        case ERROR_BAD_FORMAT:
            msg = L"Failed to launch '" + wapp +
                  L"': Invalid executable.";
            break;

    case SE_ERR_ACCESSDENIED:
        msg = L"Failed to launch '" + wapp +
              L"': Access denied.";
        break;

    case SE_ERR_ASSOCINCOMPLETE:
        msg = L"Failed to launch '" + wapp +
              L"': File association incomplete.";
        break;

    case SE_ERR_DDEBUSY:
        msg = L"Failed to launch '" + wapp +
              L"': DDE busy.";
        break;

    case SE_ERR_DDEFAIL:
        msg = L"Failed to launch '" + wapp +
              L"': DDE failed.";
        break;

    case SE_ERR_DDETIMEOUT:
        msg = L"Failed to launch '" + wapp +
              L"': DDE timeout.";
        break;

        case SE_ERR_DLLNOTFOUND:
            msg = L"Failed to launch '" + wapp +
                  L"': DLL not found.";
            break;

        case SE_ERR_NOASSOC:
            msg = L"Failed to launch '" + wapp +
                  L"': No file association.";
            break;

        case SE_ERR_OOM:
            msg = L"Failed to launch '" + wapp +
                  L"': Out of memory.";
            break;

        case SE_ERR_SHARE:
            msg = L"Failed to launch '" + wapp +
                  L"': Sharing violation.";
            break;

        default:
            msg = L"Failed to launch '" + wapp +
                  L"': Unknown ShellExecute error.";
        break;
    }

    DWORD err = GetLastError();

    msg += L"\n\nGetLastError() = ";
    msg += std::to_wstring(err);

    MessageBoxW(
        nullptr,
        msg.c_str(),
        L"WinBind - Startup Error",
        MB_OK | MB_ICONERROR);
    } 
};
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    kb.Stop();
    return 0;
}
