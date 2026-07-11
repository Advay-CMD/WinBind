// WinBind - Entry point
// Creates a hidden message-only window, runs the keyboard hook message loop,
// and periodically applies window transparency & window styling.

#include "keybinder.h"
#include "Transparency/Transparency.h"
#include "WindowStyler/WindowStyler.h"
#include <windows.h>

#define TIMER_TRANSPARENCY 1
#define TIMER_WINDOWSTYLER 2

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

    const char* configPath = "winbind.conf";
    const char* cmdLine = GetCommandLineA();
    const char* flag = strstr(cmdLine, "--config");
    if (flag) {
        const char* val = flag + 8;
        while (*val == ' ' || *val == '=') val++;
        if (*val) configPath = val;
    }

    kb.LoadConfig(configPath);

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

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    kb.Stop();
    return 0;
}
