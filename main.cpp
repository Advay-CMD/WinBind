// WinBind - Entry point
// Creates a hidden message-only window, initializes the keyboard hook,
// and runs the message loop that dispatches queued hotkey actions.
#include "keybinder.h"

// Window procedure for the hidden message-only window.
// WM_KB_EXECUTE is posted by the keyboard hook to execute
// an action asynchronously outside the hook callback context.
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KB_EXECUTE) {
        Keybinder::Instance().ExecuteQueuedAction((int)wParam);
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

    if (!kb.Init()) {
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    kb.Stop();
    return 0;
}
