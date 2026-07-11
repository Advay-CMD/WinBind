// WinBind - WindowStyler Code
#include "WindowStyler.h"
#include <windows.h>
#include <dwmapi.h>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

// Ensure dwmapi.lib is linked and loaded(properly)
#pragma comment(lib, "dwmapi.lib")

// --------------------------- Config-only helpers ---------------------------

// Trim a string (yea it's duplicated from Transparency)
static std::string WS_Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

// Parse "[R,G,B]" or "R,G,B" — returns false if it fails
static bool ParseRGB(const std::string& val, int& r, int& g, int& b) {
    std::string v = val;
    // Strip surrounding brackets if present
    if (v.size() >= 2 && v[0] == '[' && v.back() == ']')
        v = v.substr(1, v.size() - 2);
    // Now parse "R,G,B"
    size_t c1 = v.find(',');
    if (c1 == std::string::npos) return false;
    size_t c2 = v.find(',', c1 + 1);
    if (c2 == std::string::npos) return false;
    r = std::atoi(WS_Trim(v.substr(0, c1)).c_str());
    g = std::atoi(WS_Trim(v.substr(c1 + 1, c2 - c1 - 1)).c_str());
    b = std::atoi(WS_Trim(v.substr(c2 + 1)).c_str());
    return true;
}

// ------------------------- Config loader -----------------------------------

WindowStylerSettings LoadWindowStylerConfig() {
    WindowStylerSettings s = { false };
    s.enabled = true; // default: on

    std::ifstream file("winbind.conf");
    if (!file.is_open()) return s;

    std::string line;
    while (std::getline(file, line)) {
        line = WS_Trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = WS_Trim(line.substr(0, eq));
        std::string val = WS_Trim(line.substr(eq + 1));
        std::string keyLow = key;
        std::transform(keyLow.begin(), keyLow.end(), keyLow.begin(), ::tolower);
        std::string valLow = val;
        std::transform(valLow.begin(), valLow.end(), valLow.begin(), ::tolower);

        if (keyLow == "stealfocus") {
            s.stealFocus = (valLow == "enabled" || valLow == "1" || valLow == "true");
        } else if (keyLow == "changebordercolor") {
            s.borderColorEnabled = ParseRGB(val, s.borderR, s.borderG, s.borderB);
        } else if (keyLow == "changetitlebarcolor") {
            s.titleBarColorEnabled = ParseRGB(val, s.titleR, s.titleG, s.titleB);
        } else if (keyLow == "darkmodetitlebar") {
            s.darkModeEnabled = (valLow == "enabled" || valLow == "1" || valLow == "true");
        } else if (keyLow == "systembackdrop") {
            s.backdropType = std::atoi(val.c_str());
            if (s.backdropType >= 1 && s.backdropType <= 4)
                s.backdropEnabled = true;
        }
    }
    return s;
}

int LoadWindowStylerDelayMs() {
    std::ifstream file("winbind.conf");
    if (!file.is_open()) return 2000;

    std::string line;
    while (std::getline(file, line)) {
        line = WS_Trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = WS_Trim(line.substr(0, eq));
        std::string val = WS_Trim(line.substr(eq + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (key == "windowstyler_delay") {
            std::string v = val;
            std::transform(v.begin(), v.end(), v.begin(), ::tolower);
            int multiplier = 1;
            if (v.size() > 2 && v.substr(v.size() - 2) == "ms")
                { v = v.substr(0, v.size() - 2); multiplier = 1; }
            else if (v.size() > 1 && v.back() == 's')
                { v.pop_back(); multiplier = 1000; }
            int ms = std::atoi(v.c_str()) * multiplier;
            if (ms < 100) ms = 100;
            if (ms > 60000) ms = 60000;
            return ms;
        }
    }
    return 2000;
}

void StealFocus(BOOL enabled) 
{
    if(enabled == TRUE) 
    {
        SystemParametersInfo(SPI_SETACTIVEWINDOWTRACKING, 0, (void*)TRUE, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        SystemParametersInfo(SPI_SETACTIVEWNDTRKTIMEOUT, 0, 0, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }
}

//Declare the ChangeBorderColor for Border
void ChangeBorderColor(HWND hwnd, int r, int g, int b) 
{
    // First, Convert the RGB val to colorref, so that comp can easily understand
    COLORREF borderColor = RGB(r, g, b); 
    // Set the color :D
    HRESULT hr = DwmSetWindowAttribute(
        hwnd, 
        DWMWA_BORDER_COLOR, 
        &borderColor, 
        sizeof(borderColor)
    );
    // Can add error logic later
}

//Declare the ChangeTitleBarColor
void ChangeTitleBarColor(HWND hwnd, int r, int g, int b) 
{
    // First, Convert the RGB val to colorref, so that comp can easily understand
    COLORREF titlebarColor = RGB(r, g, b); 
    // Set the color :D
    HRESULT hr = DwmSetWindowAttribute(
        hwnd, 
        DWMWA_CAPTION_COLOR, 
        &titlebarColor, 
        sizeof(titlebarColor)
    );
    // Can add error logic later
}

//Declare the DarkModeTitleBar
void DarkModeTitleBar(HWND hwnd, bool enable) 
{
    // First, convert my true and false to TRUE and FALSE
    BOOL use = enable ? TRUE : FALSE;
    // Set the Attrib :)
    HRESULT hr = DwmSetWindowAttribute(
        hwnd,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &use,
        sizeof(use)
    );
    // Can add error logic later
}

void SystemBackdrop(HWND hwnd, int backdrop_type) 
{
    // Set the Attribute >:) (Emoji's are good for health lol)
    HRESULT hr = DwmSetWindowAttribute(
        hwnd,
        DWMWA_SYSTEMBACKDROP_TYPE,
        &backdrop_type,
        sizeof(backdrop_type)
    );
    // Can add error logic later
}

// All Styles
void StyleWindow(HWND hwnd) 
{
    ChangeBorderColor(hwnd, 0, 0, 255);
    DarkModeTitleBar(hwnd, true);
    // ChangeTitleBarColor(hwnd, 0, 0, 255);
    SystemBackdrop(hwnd, 2);
}

// Standalone callback — used by the old main(), kept static so it don't clash with Transparency's EnumWindowsProc
static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    // Filter for visible windows only
    if (IsWindowVisible(hwnd)) {
    wchar_t windowTitle[256];
    GetWindowTextW(hwnd, windowTitle, 256);
        
        if (windowTitle[0] != '\0') {
            StyleWindow(hwnd);
        }
    }
    return TRUE; // Return TRUE to continue enumeration
}

// ---------------------------------------------------------------------------
// Config-aware style applicator — what WinBind actually calls
// ---------------------------------------------------------------------------

// Apply whatever the user set in winbind.conf to a single window
void ApplyStylesToWindow(HWND hwnd, const WindowStylerSettings& s) {
    if (!s.enabled) return;
    if (s.borderColorEnabled)
        ChangeBorderColor(hwnd, s.borderR, s.borderG, s.borderB);
    if (s.titleBarColorEnabled)
        ChangeTitleBarColor(hwnd, s.titleR, s.titleG, s.titleB);
    if (s.darkModeEnabled)
        DarkModeTitleBar(hwnd, true);
    if (s.backdropEnabled)
        SystemBackdrop(hwnd, s.backdropType);
}

// EnumWindows callback — run by the timer in WinBind
BOOL CALLBACK WindowStylerEnumProc(HWND hwnd, LPARAM lParam) {
    const WindowStylerSettings* s = (const WindowStylerSettings*)lParam;
    if (!s || !s->enabled) return TRUE;

    wchar_t windowTitle[256];
    GetWindowTextW(hwnd, windowTitle, 256);

    if (IsWindowVisible(hwnd) && windowTitle[0] != '\0')
        ApplyStylesToWindow(hwnd, *s);
    return TRUE;
}
