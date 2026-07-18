#include "Config.h"
#include <fstream>
#include <algorithm>
#include <cctype>

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

Config::Config() : loaded(false) {}

void Config::Load(const std::string& path) {
    data.clear();
    loaded = false;
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        data[key] = val;
    }
    loaded = true;
}

std::string Config::operator[](const std::string& arg) {
    // If it looks like a config path, load it
    if (arg.size() > 5 && arg.substr(arg.size() - 5) == ".conf") {
        Load(arg);
        return "";
    }
    // Normal key lookup
    std::string k = arg;
    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
    std::map<std::string, std::string>::iterator it = data.find(k);
    return (it != data.end()) ? it->second : "";
}

void Config::Clear() {
    data.clear();
    loaded = false;
}

Config LoadConf;
std::string g_ConfigPath = "winbind.conf";
