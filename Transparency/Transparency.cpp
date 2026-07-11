// WinBind - Transparency code cpp
#include "Transparency.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

int LoadTransparencyConfig() {
    int opacity = 100;
    bool enabled = true;
    
    std::ifstream file("winbind.conf");

    if (!file.is_open()) return opacity;

    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        std::transform(val.begin(), val.end(), val.begin(), ::tolower);

        if (key == "transparency") {
            enabled = (val == "enabled" || val == "1" || val == "true");
        } else if (key == "opacity") {
            if (!val.empty() && val.back() == '%') {
                val.pop_back();
                val = Trim(val);
            }
            opacity = std::atoi(val.c_str());
            if (opacity < 0) opacity = 0;
            if (opacity > 100) opacity = 100;
        }
    }

    return enabled ? opacity : 0;
}

int LoadTransparencyDelayMs() {
    std::ifstream file("winbind.conf");
    if (!file.is_open()) return 1000;

    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (key == "transparency_delay") {
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
    return 1000;
}

void SetWindowTransparency(HWND hwnd, int percentage_opacity) {
    if (percentage_opacity <= 0) return;

    SetWindowLongPtr(hwnd, GWL_EXSTYLE,
        GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);

    SetLayeredWindowAttributes(hwnd, 0,
        (BYTE)(255 * percentage_opacity / 100), LWA_ALPHA);
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    int opacity = (int)lParam;

    if (opacity <= 0) return TRUE;

    wchar_t windowTitle[256];
    GetWindowTextW(hwnd, windowTitle, 256);

    if (IsWindowVisible(hwnd) && wcslen(windowTitle) > 0)
        SetWindowTransparency(hwnd, opacity);

    return TRUE;
}
