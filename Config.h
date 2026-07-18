#pragma once
#include <string>
#include <map>

// Global active config path — every subsystem reads from this
extern std::string g_ConfigPath;

class Config;
extern Config LoadConf;

class Config {
    std::map<std::string, std::string> data;
    bool loaded;
public:
    Config();
    std::string operator[](const std::string& arg);
    void Load(const std::string& path);
    void Clear();
    bool Loaded() const { return loaded; }
};
