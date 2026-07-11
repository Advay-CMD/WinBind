// WinBind - WindowStyler Header Script
// Styles the sh*t outta windows — border color, title bar, dark mode, backdrop, focus
#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <string>

// The DWMWA_BORDER_COLOR enum value is 34
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
// The DWMWA_USE_IMMERSIVE_DARK_MODE enum value is 20
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
// The DWMWA_CAPTION_COLOR enum value is 35
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

// The DWMWA_SYSTEMBACKDROP_TYPE enum value is 38
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

// Holds everything we parsed from winbind.conf's @WindowStyler section
struct WindowStylerSettings {
    bool enabled;               // Master toggle — if false, skip EVERYTHING
    bool stealFocus;            // StealFocus = Enabled
    bool borderColorEnabled;    // Was ChangeBorderColor = [...] given?
    int borderR, borderG, borderB;
    bool titleBarColorEnabled;  // Was ChangeTitleBarColor = [...] given?
    int titleR, titleG, titleB;
    bool darkModeEnabled;       // DarkModeTitleBar = Enabled?
    bool backdropEnabled;       // Was SystemBackdrop = N given?
    int backdropType;           // 1=Auto, 2=None, 3=Mica, 4=Acrylic
};

void StealFocus(BOOL enabled);
void ChangeBorderColor(HWND hwnd, int r, int g, int b);
void ChangeTitleBarColor(HWND hwnd, int r, int g, int b);
void DarkModeTitleBar(HWND hwnd, bool enable);
void SystemBackdrop(HWND hwnd, int backdrop_type);
void StyleWindow(HWND hwnd);

// Load from winbind.conf
WindowStylerSettings LoadWindowStylerConfig();
int LoadWindowStylerDelayMs();

// Applies the loaded settings to a single window (called by the enum callback)
void ApplyStylesToWindow(HWND hwnd, const WindowStylerSettings& s);

// EnumWindows callback — filters visible windows and styles 'em
BOOL CALLBACK WindowStylerEnumProc(HWND hwnd, LPARAM lParam);
