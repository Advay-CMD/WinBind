// WinBind - Transparency code cpp
#include "Transparency.h"
#include "../Config.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>
#include <psapi.h>

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

// Exclusion list for transparency — loaded from config, checked per-window
static std::vector<std::string> g_transparencyExclusion;

// Parse a ["app.exe", "app2.exe"] list from config
static std::vector<std::string> ParseExclusionList(const std::string& val) {
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

// Get lowercase .exe name for a window (e.g. "code.exe")
static std::string GetWindowProcessName(HWND hwnd) {
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return "";
    char path[MAX_PATH];
    DWORD size = MAX_PATH;
    std::string name;
    if (QueryFullProcessImageNameA(hProc, 0, path, &size)) {
        char* slash = strrchr(path, '\\');
        name = slash ? (slash + 1) : path;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    }
    CloseHandle(hProc);
    return name;
}

// Check if name matches any entry — ["*"] blocks all
static bool IsExcluded(const std::string& name, const std::vector<std::string>& exclusion) {
    if (exclusion.empty()) return false;
    for (size_t i = 0; i < exclusion.size(); i++) {
        if (exclusion[i] == "*") return true;
        std::string e = exclusion[i];
        std::transform(e.begin(), e.end(), e.begin(), ::tolower);
        if (name == e) return true;
    }
    return false;
}

int LoadTransparencyConfig() {
    int opacity = 100;
    bool enabled = true;
    g_transparencyExclusion.clear();
    
    std::ifstream file(g_ConfigPath.c_str());

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
        } else if (key == "transparencyexclusion") {
            // Re-parse from original val (not lowered) to preserve case of exe names
            g_transparencyExclusion = ParseExclusionList(Trim(line.substr(eq + 1)));
        }
    }

    return enabled ? opacity : 0;
}

int LoadTransparencyDelayMs() {
    std::ifstream file(g_ConfigPath.c_str());
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

    if (!IsWindowVisible(hwnd) || wcslen(windowTitle) == 0) return TRUE;

    // Check exclusion before applying
    if (!g_transparencyExclusion.empty()) {
        std::string procName = GetWindowProcessName(hwnd);
        if (!procName.empty() && IsExcluded(procName, g_transparencyExclusion))
            return TRUE; // Skipped
    }

    SetWindowTransparency(hwnd, opacity);
    return TRUE;
}
