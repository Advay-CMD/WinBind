// WinBind - keybinder script Header
// Declares the Keybinder class: a low-level keyboard hook that intercepts
// configurable hotkeys and translates them into virtual desktop operations.
// Also my magnum opus — 800+ lines of pure Windows jank.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <objbase.h>

// Undo any pre-existing macro definitions that would clash with our modifier constants.
#undef MOD_WIN
#undef MOD_ALT
#undef MOD_SHIFT

// Custom window message posted by the hook to the hidden window.
// The action is executed in the message loop, not inside the hook callback,
// to avoid restrictions on SendInput from within a hook context.
#define WM_KB_EXECUTE (WM_APP + 100)

// Bitmask constants for modifier keys used in Keybinding::modifiers.
#define MODB_WIN   0x01
#define MODB_CTRL  0x02
#define MODB_ALT   0x04
#define MODB_SHIFT 0x08

// Minimal RAII wrapper around COM IUnknown pointers.
// Handles AddRef/Release automatically.
template<typename T>
class ComPtr {
    T* p;
public:
    ComPtr() : p(nullptr) {}
    explicit ComPtr(T* ptr) : p(ptr) {}
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
    operator T*() const { return p; }
    T* Get() const { return p; }
    void Reset() { if (p) { p->Release(); p = nullptr; } }
};

// A single parsed hotkey binding read from the config file.
struct Keybinding {
    DWORD modifiers;      // Bitmask (MODB_WIN | MODB_CTRL | etc.)
    DWORD vkCode;         // Virtual-key code
    std::string action;   // "SWITCH", "MOVE", "MOVE_SWITCH", "REMOVE_EMPTY", "SIMULATE_KEY"
    int arg;              // Target desktop number (1-based)
    DWORD targetMods;     // For SIMULATE_KEY: modifiers to simulate
    DWORD targetVk;       // For SIMULATE_KEY: key to simulate
    std::string runApp;
    std::string runArgs;
    std::string confLoadPath;  // For RELOAD_CONFIG: path passed to LoadConf
};

class Keybinder {
public:
    static Keybinder& Instance();
    bool Init();
    void LoadConfig(const std::string& path);
    void Stop();

public:
    void SetWindowHandle(HWND hWnd) { m_hWnd = hWnd; }
    void ExecuteQueuedAction(int bindingIndex);

private:
    Keybinder();
    ~Keybinder();
    Keybinder(const Keybinder&) = delete;
    Keybinder& operator=(const Keybinder&) = delete;

    static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    LRESULT HandleKeyEvent(WPARAM wParam, LPARAM lParam);
    void ExecuteAction(const Keybinding& kb);

    struct DesktopInfo { GUID id; };
    bool RefreshDesktopList();
    bool CheckConfigChanged();

    void SwitchToDesktop(int num);
    void MoveCurrentWindowToDesktop(int num);
    void RemoveEmptyDesktops(int sourceDesktop = -1);
    bool IsModTracked(WORD vk);
    DWORD GetModifierState();
    DWORD KeyNameToVK(const std::string& name);
    void SimulateKeyCombo(WORD mod1, WORD mod2, WORD key);
    void SimulateKeyCombo(DWORD mods, DWORD vk);

    HHOOK m_hHook;
    HWND m_hWnd;
    std::vector<Keybinding> m_bindings;
    std::vector<DesktopInfo> m_desktops;
    int m_currentDesktop;
    bool m_enabled;                 // Virtual_Desktop_Switch config flag
    bool m_newDesktopCreation;      // New_Desktop_Creation config flag
    bool m_allowBadKeys;            // Allow_Bad_Keys config flag
    bool m_windowAndDesktopSwitch;  // Window_And_Virtual_Desktop_Switch config flag
    bool m_autoRemoveDesktop;       // Auto_Remove_Virtual_Desktop config flag
    bool m_busy;                    // Prevents re-entrant key processing
    bool m_simulating;              // Guards against re-trigger from injected keys
    DWORD m_modsTracked;            // Real-time modifier state from hook events
    std::string m_configPath;
