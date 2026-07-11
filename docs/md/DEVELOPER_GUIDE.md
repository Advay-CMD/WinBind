# WinBind — Complete Developer Guide

> **Who is this for?** Anyone who wants to understand, modify, or extend WinBind.
> Every function, every variable, every trick.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [File Structure](#2-file-structure)
3. [How WinBind Starts (Entry Point)](#3-how-winbind-starts-entry-point)
4. [The Hidden Window & Message Loop](#4-the-hidden-window--message-loop)
5. [The Keyboard Hook — How Keypresses Are Caught](#5-the-keyboard-hook)
6. [Config Loading — How winbind.conf Becomes Code](#6-config-loading)
7. [Actions — What Happens When You Press a Keybind](#7-actions)
8. [Desktop Switching — Moving Between Virtual Desktops](#8-desktop-switching)
9. [Moving Windows — 3 Attempts to Move a Window](#9-moving-windows)
10. [Removing Empty Desktops — Flicker-Free](#10-removing-empty-desktops)
11. [Transparency — Making Windows See-Through](#11-transparency)
12. [Key Simulation — SendInput Explained](#12-key-simulation)
13. [COM & The SIGSEGV Guard — Talking to Windows Undocumented APIs](#13-com--the-sigsegv-guard)
14. [The `ComPtr` Template — Automatic COM Cleanup](#14-the-comptr-template)
15. [Modifier Tracking — How WinBind Knows What Keys You're Holding](#15-modifier-tracking)
16. [The `m_busy` / `m_simulating` Guards — Preventing Chaos](#16-the-m_busy--m_simulating-guards)
17. [How to Add a New Action (e.g. `RUN`)](#17-how-to-add-a-new-action)
18. [How to Add a New Config Option](#18-how-to-add-a-new-config-option)
19. [How to Make Something Run Once vs Every Time](#19-how-to-make-something-run-once-vs-every-time)
20. [Build & Run — How to Compile](#20-build--run)
21. [Common Pitfalls](#21-common-pitfalls)

---

## 1. Project Overview

WinBind is a **Windows utility** that runs in the background. It:

- Listens for **keyboard shortcuts** (like `WIN+1`, `CTRL+SHIFT+C`)
- When you press a shortcut, it does something:
  - **Switches** to a different virtual desktop
  - **Moves** the current window to another desktop
  - **Simulates** a different keypress (like making `CTRL+SHIFT+V` act as `CTRL+V`)
- Also applies **window transparency** (makes windows see-through) at a set interval

It uses **NO admin rights**. Everything is done with user-mode Windows APIs.

---

## 2. File Structure

```
WinBind/
├── main.cpp              # Entry point — starts the program
├── keybinder.h           # Declarations — tells what exists
├── keybinder.cpp         # Main logic — hook, switching, COM
├── Transparency.h        # Transparency declarations
├── Transparency.cpp      # Transparency implementation
├── winbind.conf          # User configuration (text file)
├── build.bat             # How to compile
├── run.bat               # How to run (outdated)
├── WinBind.exe           # The compiled program
└── docs/                 # Documentation
```

### What each file does:

| File | Purpose |
|------|---------|
| `main.cpp` | The **start** button. Creates a hidden window, loads config, starts listening. |
| `keybinder.h` | The **table of contents**. Tells what classes, functions, and variables exist. |
| `keybinder.cpp` | The **engine**. All the real work happens here. |
| `Transparency.h` | Tells what transparency functions exist. |
| `Transparency.cpp` | How transparency actually works. |
| `winbind.conf` | The **settings file** users edit. |
| `build.bat` | The **compiler command** — turns source code into `.exe`. |

---

## 3. How WinBind Starts (Entry Point)

**File:** `main.cpp:25` — Function: `WinMain`

Every Windows program starts at `WinMain`. Think of it as `main()` in other languages.

### Step-by-step:

```cpp
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
```

`HINSTANCE hInst` — a handle to the program itself. Windows gives it to you, you pass it along.

```cpp
Keybinder& kb = Keybinder::Instance();
```

Gets the **single, one-and-only** Keybinder object. This is called a **singleton** — there can only ever be one.

```cpp
const char* configPath = "winbind.conf";
const char* cmdLine = GetCommandLineA();
const char* flag = strstr(cmdLine, "--config");
if (flag) {
    const char* val = flag + 8;
    while (*val == ' ' || *val == '=') val++;
    if (*val) configPath = val;
}
```

This checks if the user ran WinBind with a custom config path:
```
WinBind.exe --config "C:\my_settings.conf"
```
If they didn't, it uses `winbind.conf` in the current folder.

```cpp
kb.LoadConfig(configPath);
```

Reads the config file and remembers all the keybinds.

```cpp
WNDCLASSEXW wc = { sizeof(wc) };
wc.lpfnWndProc = WndProc;        // Window's "brain" — handles messages
wc.hInstance = hInst;
wc.lpszClassName = L"KeybinderHiddenWnd";
RegisterClassExW(&wc);

HWND hWnd = CreateWindowExW(0, L"KeybinderHiddenWnd", L"", 0, 0, 0, 0, 0,
    HWND_MESSAGE, NULL, hInst, NULL);
```

This creates a **hidden window** — it doesn't appear on your screen or taskbar. It's just a mailbox for messages.

`HWND_MESSAGE` is the magic flag that makes it invisible.

```cpp
kb.SetWindowHandle(hWnd);
```

Tells the Keybinder: "Here is the mailbox. Post messages here."

```cpp
if (!kb.Init()) return 1;
```

Sets up the keyboard hook (starts listening) and COM (talks to Windows desktop APIs).

```cpp
int delayMs = LoadTransparencyDelayMs();
SetTimer(hWnd, TIMER_TRANSPARENCY, delayMs, NULL);
```

Starts a timer. Every `delayMs` milliseconds (1000ms = 1 second by default), the window gets a "time to apply transparency" message.

```cpp
MSG msg;
while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
}
```

**The message loop.** This runs forever while the program is alive. It waits for messages (keyboard hooks, timer ticks) and sends them to `WndProc` to handle.

```cpp
kb.Stop();
return 0;
```

When the program finally receives a `WM_QUIT` message, it cleans up and exits.

---

## 4. The Hidden Window & Message Loop

**File:** `main.cpp:11` — Function: `WndProc`

This is the "brain" of the hidden window. When something happens (a keybind matches, the timer ticks), this function gets called.

```cpp
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
```

Parameters:
- `hWnd` — which window received the message (we only have one)
- `msg` — **what happened** (a number that means something)
- `wParam`, `lParam` — **details** about what happened

### Message 1: Keybind Match (`WM_KB_EXECUTE`)

```cpp
if (msg == WM_KB_EXECUTE) {
    Keybinder::Instance().ExecuteQueuedAction((int)wParam);
    return 0;
}
```

When a keybind matches, the keyboard hook posts `WM_KB_EXECUTE` to this window. `wParam` is the **index number** of the binding (0 = first binding, 1 = second, etc.).

Why do it this way? Because Windows doesn't allow `SendInput` (simulating keys) inside a keyboard hook callback. So the hook says "hey, do this later" by posting a message.

### Message 2: Timer Tick (`WM_TIMER`)

```cpp
if (msg == WM_TIMER && wParam == TIMER_TRANSPARENCY) {
    int opacity = LoadTransparencyConfig();
    if (opacity > 0 && opacity < 100)
        EnumWindows(EnumWindowsProc, (LPARAM)opacity);
    return 0;
}
```

Every `Transparency_Delay` milliseconds, this runs. It re-reads the config (so you can change opacity without restarting), then goes through every window on screen and applies transparency.

### Message 3: Everything Else

```cpp
return DefWindowProc(hWnd, msg, wParam, lParam);
```

If it's not a message we care about, let Windows handle it normally.

---

## 5. The Keyboard Hook

**File:** `keybinder.cpp:601` — Function: `KeyboardProc` (static)

```cpp
LRESULT CALLBACK Keybinder::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && Instance().HandleKeyEvent(wParam, lParam) == 1)
        return 1;
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}
```

This is a **low-level keyboard hook** (`WH_KEYBOARD_LL`). It fires for **every keystroke** on the entire system — in any app.

- If `HandleKeyEvent` returns `1`, the key is **blocked** (swallowed, never reaches the app).
- If `HandleKeyEvent` returns `0`, the key passes through normally.

### The Real Logic: `HandleKeyEvent`

**File:** `keybinder.cpp:607` — Function: `HandleKeyEvent`

```cpp
LRESULT Keybinder::HandleKeyEvent(WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
    DWORD vk = pKb->vkCode;
    bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
```

`KBDLLHOOKSTRUCT` contains details about the keypress: **which key** (`vkCode`) and **whether it was pressed or released** (`wParam`).

#### Step 1: Track Modifier Keys

```cpp
DWORD mb = 0;
switch (vk) {
    case VK_LWIN: case VK_RWIN:   mb = MODB_WIN;   break;
    case VK_LCONTROL: case VK_RCONTROL: mb = MODB_CTRL; break;
    case VK_LMENU: case VK_RMENU: mb = MODB_ALT;   break;
    case VK_LSHIFT: case VK_RSHIFT: mb = MODB_SHIFT; break;
}
if (mb) { m_modsTracked = down ? (m_modsTracked | mb) : (m_modsTracked & ~mb); return 0; }
```

If you pressed **Win**, **Ctrl**, **Alt**, or **Shift**, it updates `m_modsTracked` (a bitmask remembering which modifiers are held). Then it returns `0` (don't block the key — let it pass through).

#### Step 2: If `m_busy`, Block Real Keys

```cpp
if (m_busy) return (pKb->flags & (LLKHF_INJECTED | 0x02)) ? 0 : 1;
```

While an action is running (e.g., switching desktops), **block all real keys** to prevent interference. But allow injected keys (keys that WinBind itself is simulating) through.

#### Step 3: If `m_simulating`, Skip Everything

```cpp
if (m_simulating) return 0;
```

While WinBind is in the middle of `SendInput` (simulating keystrokes), don't try to match bindings — that would cause an infinite loop.

#### Step 4: Check if Config Changed

```cpp
CheckConfigChanged();
if (!m_enabled) return 0;
```

On every keystroke, check if `winbind.conf` was modified. If so, reload it. If virtual desktop switching is disabled, stop here.

#### Step 5: Match Against Bindings

```cpp
for (size_t i = 0; i < m_bindings.size(); i++)
    if (m_bindings[i].vkCode == vk && m_bindings[i].modifiers == m_modsTracked) {
        if (m_hWnd) PostMessage(m_hWnd, WM_KB_EXECUTE, i, 0);
        return 1;
    }
return 0;
```

Go through every binding. If the pressed key AND the currently-held modifiers match a binding:
1. Post a message to the hidden window: "execute binding number `i`"
2. Return `1` — **block** the key (the user doesn't need the original key to reach the app)

If nothing matches, return `0` — let the key through.

---

## 6. Config Loading

**File:** `keybinder.cpp:697` — Function: `LoadConfig`

This reads `winbind.conf` line by line.

### The Config Format

```
# Comments start with #
FLAG_NAME = Enabled/Disabled
MODIFIERS+KEY = ACTION [ARGUMENT]
```

### How Each Line is Parsed

```cpp
std::ifstream file(path);
std::string line;
while (std::getline(file, line)) {
    line = Trim(line);            // Remove spaces around edges
    if (line.empty() || line[0] == '#') continue;  // Skip blanks and comments

    size_t eq = line.find('=');   // Find the equals sign
    std::string lhs = Trim(line.substr(0, eq));    // Left side: "WIN+1"
    std::string rhs = Trim(line.substr(eq + 1));   // Right side: "SWITCH 1"
```

### Boolean Flags

```cpp
auto parseBool = [&](bool& flag) {
    std::string v = rhs;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    flag = (v == "enabled" || v == "1" || v == "true");
};
if (lhs == "Virtual_Desktop_Switch")   { parseBool(m_enabled); continue; }
if (lhs == "New_Desktop_Creation")     { parseBool(m_newDesktopCreation); continue; }
if (lhs == "Allow_Bad_Keys")           { parseBool(m_allowBadKeys); continue; }
if (lhs == "Window_And_Virtual_Desktop_Switch") { parseBool(m_windowAndDesktopSwitch); continue; }
if (lhs == "Auto_Remove_Virtual_Desktop") { parseBool(m_autoRemoveDesktop); continue; }
```

If the left side is a known flag name, set the corresponding boolean.

### Key Bindings

```cpp
std::istringstream rss(rhs);
std::string act; int arg = 0;
rss >> act;                         // Read action: "SWITCH", "MOVE", etc.
std::string as; if (rss >> as) arg = std::atoi(as.c_str());  // Read argument number
```

### Special Case: `SIMULATE_KEY[CTRL+V]`

```cpp
if (act.find("SIMULATE_KEY[") == 0 && act.back() == ']') {
    std::string inner = act.substr(13, act.size() - 14);  // Extract "CTRL+V"
    // Parse the inner string for modifiers and a key
    // Split on '+' -> ["CTRL", "V"]
    // "CTRL" -> ParseModifier("CTRL") -> MODB_CTRL
    // "V" -> KeyNameToVK("V") -> 0x56
    act = "SIMULATE_KEY";  // Normalize action name
}
```

### Parsing the Left Side (Modifiers + Key)

```cpp
DWORD mods = 0;
std::string keyName;
for (size_t sp = 0;;) {
    size_t pp = lhs.find('+', sp);
    std::string p = Trim(lhs.substr(sp, ...));
    DWORD m = ParseModifier(p);
    if (m) mods |= m;   // It's a modifier (WIN, CTRL, etc.)
    else keyName = p;   // It's the actual key (1, A, F1, etc.)
    if (pp == std::string::npos) break;
    sp = pp + 1;
}
```

### Helper: `ParseModifier`

**File:** `keybinder.cpp:170`

```cpp
static DWORD ParseModifier(const std::string& s) {
    std::string l = s;
    std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    if (l == "win")   return MODB_WIN;
    if (l == "ctrl")  return MODB_CTRL;
    if (l == "alt")   return MODB_ALT;
    if (l == "shift") return MODB_SHIFT;
    return 0;
}
```

### Helper: `KeyNameToVK`

**File:** `keybinder.cpp:180`

Converts a key name string to a Windows virtual-key code.

```cpp
DWORD Keybinder::KeyNameToVK(const std::string& name) {
    std::string u = name;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
    // Single letter/number? "A" → 0x41, "5" → 0x35
    if (u.size() == 1) {
        char c = u[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (DWORD)c;
    }
    // Named keys lookup table
    struct { const char* n; DWORD v; } km[] = {
        {"F1", VK_F1}, {"F2", VK_F2}, ..., {"LEFT", VK_LEFT},
        {"RIGHT", VK_RIGHT}, {"SPACE", VK_SPACE}, {"ENTER", VK_RETURN},
        {"TAB", VK_TAB}, {"ESC", VK_ESCAPE}, ...
    };
    // Search for match
    for (size_t i = 0; i < sizeof(km)/sizeof(km[0]); i++)
        if (u == km[i].n) return km[i].v;
    return 0;
}
```

### System Shortcut Blocklist

**File:** `keybinder.cpp:213` — Function: `IsSystemShortcut`

Before adding a binding, WinBind checks if the shortcut is a **dangerous system shortcut**:

```cpp
static bool IsSystemShortcut(DWORD mods, DWORD vk) {
    struct { DWORD m; DWORD v; } bad[] = {
        {MODB_CTRL, 'C'}, {MODB_CTRL, 'X'},  // Copy, Cut
        {MODB_ALT, VK_F4},                      // Close window
        {MODB_WIN, 'D'},                       // Show desktop
        {MODB_CTRL|MODB_ALT, VK_DELETE},       // Security screen
        // ... ~40 blocked combos
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
        if (bad[i].m == mods && bad[i].v == vk) return true;
    return false;
}
```

If the user tries to bind `CTRL+C`, it's **rejected** unless `Allow_Bad_Keys=Enabled` is set.

### Helper: `Trim`

**File:** `keybinder.cpp:164` (also duplicated in `Transparency.cpp:8`)

```cpp
static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");  // First non-space char
    size_t b = s.find_last_not_of(" \t\r\n");   // Last non-space char
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}
```

Removes spaces, tabs, and newlines from both ends of a string. Every config line is trimmed before processing.

---

## 7. Actions

**File:** `keybinder.cpp:649` — Functions: `ExecuteQueuedAction`, `ExecuteAction`

When a keybind matches, the message loop calls:

```cpp
void Keybinder::ExecuteQueuedAction(int bindingIndex) {
    if (m_busy) return;           // Already doing something? Skip.
    if (bindingIndex < 0 || bindingIndex >= (int)m_bindings.size()) return;  // Invalid?
    m_busy = true;                // Lock — we're busy now
    ExecuteAction(m_bindings[bindingIndex]);  // Do the action
    m_busy = false;               // Unlock
}
```

`ExecuteAction` then decides WHAT to do:

```cpp
void Keybinder::ExecuteAction(const Keybinding& kb) {
    if (kb.action == "SWITCH") {
        int src = m_currentDesktop;          // Remember where we were
        SwitchToDesktop(kb.arg);             // Go to target desktop
        RemoveEmptyDesktops(src);            // Clean up the old desktop
    }
    else if (kb.action == "MOVE") {
        MoveCurrentWindowToDesktop(kb.arg);  // Move window, stay put
    }
    else if (kb.action == "MOVE_SWITCH") {
        // Create desktops if needed
        // Move window to target
        // Switch to target
        // Remove empty from source
    }
    else if (kb.action == "REMOVE_EMPTY") {
        RemoveEmptyDesktops();               // Delete all empty desktops
    }
    else if (kb.action == "SIMULATE_KEY") {
        SimulateKeyCombo(kb.targetMods, kb.targetVk);  // Press different keys
    }
}
```

### Current Actions Summary

| Action | Parameters | What It Does |
|--------|-----------|-------------|
| `SWITCH` | `N` (desktop number) | Switches to virtual desktop N. If `New_Desktop_Creation` is enabled, creates desktops as needed. |
| `MOVE` | `N` (desktop number) | Moves the currently active window to desktop N. You stay on the current desktop. |
| `MOVE_SWITCH` | `N` (desktop number) | Moves the window to desktop N AND switches to it. |
| `REMOVE_EMPTY` | (none) | Removes all virtual desktops that have no windows on them. |
| `SIMULATE_KEY` | `[MODS+KEY]` | Instead of doing something, presses a different key combo. Example: `SIMULATE_KEY[CTRL+V]`. |

---

## 8. Desktop Switching

**File:** `keybinder.cpp:401` — Function: `SwitchToDesktop`

```cpp
void Keybinder::SwitchToDesktop(int num) {
    if (!m_enabled || m_desktops.empty()) return;
    RefreshDesktopList();  // Get latest list from registry

    int targetIdx = num - 1;  // Config uses 1-based, code uses 0-based
    if (targetIdx < 0) return;

    // Create desktops if target doesn't exist yet
    int created = 0;
    while (targetIdx >= (int)m_desktops.size()) {
        if (!m_newDesktopCreation) return;      // Can't create? Give up
        if (++created > 20) return;              // Safety limit: max 20
        SimulateKeyCombo(VK_LWIN, VK_LCONTROL, 'D');  // WIN+CTRL+D = new desktop
        RefreshDesktopList();
    }
    if (m_currentDesktop == targetIdx) return;  // Already there? Done.

    // Calculate direction and number of presses
    int diff = targetIdx - m_currentDesktop;
    WORD dirKey = (diff > 0) ? VK_RIGHT : VK_LEFT;
    int presses = std::min(abs(diff), 20);
    for (int i = 0; i < presses; i++)
        SimulateKeyCombo(VK_LWIN, VK_LCONTROL, dirKey);  // WIN+CTRL+LEFT/RIGHT
    RefreshDesktopList();
}
```

**How it works:**

Windows has no official "switch to desktop N" API. So WinBind simulates the keyboard shortcuts:
- `WIN+CTRL+LEFT` = go to previous desktop
- `WIN+CTRL+RIGHT` = go to next desktop
- `WIN+CTRL+D` = create new desktop

To get from desktop 2 to desktop 5: press `WIN+CTRL+RIGHT` three times.

### How Desktop List is Read: `RefreshDesktopList`

**File:** `keybinder.cpp:286`

```cpp
bool Keybinder::RefreshDesktopList() {
    // Open registry key
    RegOpenKeyExA(HKEY_CURRENT_USER,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops",
        0, KEY_READ, &hKey);

    // Read VirtualDesktopIDs = array of 16-byte GUIDs
    BYTE buf[4096];
    RegQueryValueExA(hKey, "VirtualDesktopIDs", ...);
    DWORD count = size / 16;  // Each GUID is 16 bytes
    for (DWORD i = 0; i < count; i++) {
        memcpy(&di.id, buf + i * 16, 16);
        m_desktops.push_back(di);
    }

    // Read CurrentVirtualDesktop = which GUID is active
    RegQueryValueExA(hKey, "CurrentVirtualDesktop", ...);
    // Find the index of the current GUID in our list
    for (size_t i = 0; i < m_desktops.size(); i++)
        if (memcmp(...) == 0) { m_currentDesktop = (int)i; break; }
}
```

---

## 9. Moving Windows

**File:** `keybinder.cpp:470` — Function: `MoveCurrentWindowToDesktop`

This function tries **3 different ways** to move a window. If one fails, it tries the next.

```cpp
void Keybinder::MoveCurrentWindowToDesktop(int num) {
    HWND hWnd = GetForegroundWindow();  // Get the active window

    // Attempt 1: Undocumented COM service (no screen flicker)
    if (TryMoveWindowViaService(hWnd, m_desktops[targetIdx].id)) { Sleep(100); return; }

    // Attempt 2: Public COM API (IVirtualDesktopManager)
    CoCreateInstance(clsid, ...);
    vt->MoveWindowToDesktop(pObj, hWnd, ...);
    // If it works, done.

    // Attempt 3: Keyboard shortcut fallback
    SimulateKeyCombo(VK_LWIN, VK_LSHIFT, dirKey);  // WIN+SHIFT+LEFT/RIGHT
}
```

### Attempt 1: The Secret COM Service

**File:** `keybinder.cpp:101` — Function: `TryMoveWindowViaService`

This calls an **undocumented** Windows COM interface (`IVirtualDesktopManagerInternal`). Undocumented means Microsoft didn't publish it — it could change in any update.

Because the interface structure changes between Windows versions, WinBind tries **5 different interface IDs** (IIDs):

| IID Constant | Windows Version |
|-------------|----------------|
| `GUID_IID_VDMgr_26100` | Windows 11 24H2+ |
| `GUID_IID_VDMgr_22621` | Windows 11 23H2 |
| `GUID_IID_VDMgr_22000` | Windows 11 21H2 |
| `GUID_IID_VDMgr_25179` | Pre-24H2 dev builds |
| `GUID_IID_VDMgr_OLD` | Before Windows 11 |

If the vtable index is wrong, instead of crashing, the **SIGSEGV guard** catches the crash and silently tries the next IID.

---

## 10. Removing Empty Desktops

**File:** `keybinder.cpp:511` — Function: `RemoveEmptyDesktops`

### Step 1: Flicker-Free Detection

**The clever trick:** Instead of switching to each desktop to see if it's empty (which makes the screen flash), WinBind uses `EnumWindows` + `IVirtualDesktopManager::GetWindowDesktopId`.

```cpp
// For every window on screen, ask "which desktop are you on?"
// If desktop N has at least one window, mark it as non-empty.
g_desktopCheck.mgr = mgr;     // COM interface
g_desktopCheck.guids = guids; // Array of desktop GUIDs
g_desktopCheck.hasWin = hasWin; // Boolean array (true = has a window)
EnumWindows(EnumDesktopCheckProc, 0);  // Enumerate ALL windows
```

### The Enum Callback

**File:** `keybinder.cpp:451` — Function: `EnumDesktopCheckProc`

```cpp
static BOOL CALLBACK EnumDesktopCheckProc(HWND hWnd, LPARAM) {
    if (!IsWindowVisible(hWnd)) return TRUE;  // Skip invisible
    if (IsSystemWindowClass(hWnd)) return TRUE;  // Skip system windows
    if (GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;  // Skip tool windows

    // Ask COM: "which desktop is this window on?"
    vt->GetWindowDesktopId(g_desktopCheck.mgr, hWnd, &winDesk);
    // Mark that desktop as having a window
    for (int i = 0; i < g_desktopCheck.count; i++)
        if (memcmp(&g_desktopCheck.guids[i], &winDesk, sizeof(GUID)) == 0)
            { g_desktopCheck.hasWin[i] = true; break; }
    return TRUE;
}
```

### System Window Classes

**File:** `keybinder.cpp:437` — Function: `IsSystemWindowClass`

Some windows are part of Windows itself (taskbar, desktop, etc.). They shouldn't count as "real" windows:

```cpp
static const char* sys[] = {
    "Progman",           // Desktop icons
    "WorkerW",           // Desktop background
    "Shell_TrayWnd",     // Taskbar
    "Shell_SecondaryTrayWnd",  // Second monitor taskbar
    "Windows.UI.Composition.DesktopWindowContentBridge",  // UWP internals
    "ApplicationManager_DesktopShellWindow",
    "DV2ControlHost",    // Start menu
    "MultitaskingViewFrame",  // Alt+Tab / Task View
    "NotifyIconOverflowWindow",  // System tray
};
```

### Step 2: COM Deletion

For each empty desktop, WinBind uses the secret COM to delete it:

```cpp
// FindDesktop at vtable index [14] — get the desktop object
// GetAdjacentDesktop at [8], direction 3=left, 4=right — get neighbor
// RemoveDesktop at [13] — delete the desktop, merge into neighbor
```

All protected by the SIGSEGV guard (different Windows versions have different vtable layouts).

---

## 11. Transparency

**Files:** `Transparency.cpp`, `Transparency.h`

### Loading Config

**Function:** `LoadTransparencyConfig` (line 14)

```cpp
int LoadTransparencyConfig() {
    // Read winbind.conf
    // Find "Transparency = Enabled/Disabled"
    // Find "Opacity = 87%"
    // Return 0 if disabled, or 1-100 for opacity percentage
}
```

**Function:** `LoadTransparencyDelayMs` (line 50)

```cpp
int LoadTransparencyDelayMs() {
    // Read winbind.conf
    // Find "Transparency_Delay = 1s" or "1000ms"
    // "1s" → 1 * 1000 = 1000ms
    // "1000ms" → 1000 * 1 = 1000ms
    // "500" → 500ms (no unit = raw ms)
    // Clamp between 100 and 60000
    // Default 1000ms
}
```

### Applying Transparency

**Function:** `SetWindowTransparency` (line 82)

```cpp
void SetWindowTransparency(HWND hwnd, int percentage_opacity) {
    SetWindowLongPtr(hwnd, GWL_EXSTYLE,
        GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    // Makes the window "layered" — allows transparency

    SetLayeredWindowAttributes(hwnd, 0,
        (BYTE)(255 * percentage_opacity / 100), LWA_ALPHA);
    // Sets how transparent: 0 = fully transparent, 255 = fully opaque
    // 87% → 255 * 87 / 100 = 221
}
```

### EnumWindows Callback

**Function:** `EnumWindowsProc` (line 92)

```cpp
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    int opacity = (int)lParam;
    if (opacity <= 0) return TRUE;

    wchar_t windowTitle[256];
    GetWindowTextW(hwnd, windowTitle, 256);
    // Only apply to visible windows with a title (not system windows, taskbar, etc.)

    if (IsWindowVisible(hwnd) && wcslen(windowTitle) > 0)
        SetWindowTransparency(hwnd, opacity);
    return TRUE;
}
```

---

## 12. Key Simulation (SendInput)

**File:** `keybinder.cpp` — Two overloads of `SimulateKeyCombo`

### Overload 1: Three Parameters (for WIN+CTRL+D, WIN+CTRL+ARROW, WIN+SHIFT+ARROW)

```cpp
void Keybinder::SimulateKeyCombo(WORD mod1, WORD mod2, WORD key) {
    INPUT inputs[8] = {};
    int idx = 0;

    // Check if user is already holding these modifier keys
    bool held1 = mod1 && IsModTracked(mod1);
    bool held2 = mod2 && IsModTracked(mod2);

    // Press modifiers not already held
    if (!held1 && mod1) { press(mod1); }
    if (!held2 && mod2) { press(mod2); }
    // Press key
    { keyDown; }
    // Release key
    { keyUp; }
    // Release modifiers not originally held (reverse order)
    if (!held2 && mod2) { release(mod2); }
    if (!held1 && mod1) { release(mod1); }

    SendInput(idx, inputs, sizeof(INPUT));
    Sleep(50);
}
```

**Example:** If you're holding `WIN` and press a binding that simulates `WIN+CTRL+LEFT`:
- `mod1 = WIN`, `mod2 = CTRL`
- `held1 = true` (you're already holding WIN), so skip pressing WIN
- `held2 = false`, so press CTRL
- Press LEFT, release LEFT
- Release CTRL
- Result: WIN stays held, CTRL is pressed and released

### Overload 2: Two Parameters (for SIMULATE_KEY and fallback)

```cpp
void Keybinder::SimulateKeyCombo(DWORD mods, DWORD vk) {
    // Which modifiers user is holding but target doesn't need → release temporarily
    DWORD toRelease = m_modsTracked & ~mods;
    // Which modifiers target needs but user isn't holding → press
    DWORD toPress = mods & ~m_modsTracked;

    // 1. Release extra modifiers (e.g., user holds SHIFT, target doesn't need it)
    // 2. Press missing modifiers (e.g., target needs CTRL, user isn't holding it)
    // 3. Key down
    // 4. Key up
    // 5. Release the modifiers we just pressed
    // 6. Re-press the modifiers we released

    m_simulating = true;       // Guard against re-trigger
    SendInput(idx, inputs, sizeof(INPUT));

    // Pump messages for ~20ms to flush injected events through the hook
    MSG msg;
    for (int i = 0; i < 20; i++) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { PostQuitMessage(0); return; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(1);
    }
    m_simulating = false;
}
```

**Why this complexity?** Imagine you have a binding `WIN+1 = SWITCH 2`. You press `WIN+1`. The switch happens via `WIN+CTRL+RIGHT`.

But WIN is simulated with `SendInput`. If you're already holding WIN, and `SimulateKeyCombo` presses `VK_LWIN` again, Windows might see `WIN+WIN` = nothing happens.

So the code: releases your held WIN temporarily, presses CTRL+RIGHT, then re-presses WIN. Smart.

### The `m_simulating` Guard

After `SendInput`, the code pumps messages for ~20ms. During this time, `m_simulating = true`, so the keyboard hook ignores all these injected keystrokes (they don't trigger bindings). This prevents **infinite loops**.

---

## 13. COM & The SIGSEGV Guard

### What is COM?

COM (Component Object Model) is Windows' way of letting programs talk to each other. WinBind uses COM to:
- Ask "which desktop is this window on?"
- Move windows between desktops
- Delete empty desktops

### The DocString Approach

Define the vtable for `IVirtualDesktopManager` manually:
```cpp
struct IVirtualDesktopManagerVT {
    HRESULT (*QueryInterface)(void*, REFIID, void**);
    ULONG   (*AddRef)(void*);
    ULONG   (*Release)(void*);
    HRESULT (*IsWindowOnCurrentVirtualDesktop)(void*, HWND, BOOL*);
    HRESULT (*GetWindowDesktopId)(void*, HWND, GUID*);
    HRESULT (*MoveWindowToDesktop)(void*, HWND, REFGUID);
};
```

Normally you'd `#include <shobj.h>` and get these definitions automatically. But for the **undocumented** interfaces, there are no headers — you have to write the vtable yourself.

### The SIGSEGV Guard

**File:** `keybinder.cpp:64-72, 101-158`

Since the undocumented COM interfaces change between Windows versions, calling the wrong vtable index = instant crash. WinBind uses a **signal handler** to catch crashes:

```cpp
static jmp_buf g_comJmpBuf;

extern "C" void __cdecl SigSegvHandler(int) {
    longjmp(g_comJmpBuf, 1);  // Jump back to safety
}
```

Usage pattern:
```cpp
void (*oldHandler)(int) = signal(SIGSEGV, SigSegvHandler);  // Install crash catcher

if (setjmp(g_comJmpBuf) == 0) {
    // Try the COM call. If it crashes, longjmp back here with return value 1.
    // If it works, continue normally.
    // ...
} else {
    // Crash was caught. Handle gracefully (return false, try next approach).
}

signal(SIGSEGV, oldHandler);  // Restore original handler
```

This is **extremely unusual** in C++ — most programs would just crash. But it lets WinBind try 5 different COM interface IDs and silently skip the ones that don't work.

### Getting the COM Service Provider

**File:** `keybinder.cpp:78` — Function: `GetShellServiceProvider`

Two ways to get access to the Shell's COM services:

1. **Direct:** `CoCreateInstance(CLSID_ImmersiveShell, ...)` — works on most systems
2. **Fallback:** Get an `IShellWindows` object → get the first shell window → `QueryInterface` for `IServiceProvider`

---

## 14. The `ComPtr` Template

**File:** `keybinder.h:28-42`

```cpp
template<typename T>
class ComPtr {
    T* p;
public:
    ComPtr() : p(nullptr) {}
    explicit ComPtr(T* ptr) : p(ptr) {}
    ~ComPtr() { if (p) p->Release(); }  // Auto-release on destruction

    T** operator&() { return &p; }      // For COM out-parameters
    T* operator->() { return p; }
    operator T*() const { return p; }
    T* Get() const { return p; }
    void Reset() { if (p) { p->Release(); p = nullptr; } }
};
```

COM objects must be `Release()`d when you're done. `ComPtr` does this automatically — like a smart pointer.

**But note:** This `ComPtr` is barely used in the actual code! Most COM calls use raw pointers and manual `Release()`. This is an unfinished abstraction. You could improve the code by using `ComPtr` everywhere.

---

## 15. Modifier Tracking

**File:** `keybinder.cpp:614-621`

WinBind keeps a real-time record of which modifier keys are held:

```cpp
DWORD mb = 0;
switch (vk) {
    case VK_LWIN: case VK_RWIN:   mb = MODB_WIN;   break;
    case VK_LCONTROL: case VK_RCONTROL: mb = MODB_CTRL; break;
    case VK_LMENU: case VK_RMENU: mb = MODB_ALT;   break;
    case VK_LSHIFT: case VK_RSHIFT: mb = MODB_SHIFT; break;
}
if (mb) {
    m_modsTracked = down ? (m_modsTracked | mb) : (m_modsTracked & ~mb);
    return 0;
}
```

When a modifier key goes **down**: set the bit (OR `|`).
When it comes **up**: clear the bit (AND-NOT `& ~`).

**Why not use `GetAsyncKeyState`?** On modern Windows, `GetAsyncKeyState` can miss modifier state changes when the hook is processing keys. Tracking from the hook events is more reliable.

### The Bitmask

| Bit | Value | Constant | Key |
|-----|-------|----------|-----|
| 0 | 0x01 | `MODB_WIN` | Windows key |
| 1 | 0x02 | `MODB_CTRL` | Ctrl |
| 2 | 0x04 | `MODB_ALT` | Alt |
| 3 | 0x08 | `MODB_SHIFT` | Shift |

`m_modsTracked = 0x03` means WIN + CTRL are held.
`m_modsTracked = 0x0F` means all four are held.

### Helper: `IsModTracked`

**File:** `keybinder.cpp:327`

```cpp
bool Keybinder::IsModTracked(WORD vk) {
    switch (vk) {
        case VK_LWIN: case VK_RWIN:   return (m_modsTracked & MODB_WIN)  != 0;
        case VK_LCONTROL: case VK_RCONTROL: return (m_modsTracked & MODB_CTRL) != 0;
        case VK_LMENU: case VK_RMENU: return (m_modsTracked & MODB_ALT)  != 0;
        case VK_LSHIFT: case VK_RSHIFT: return (m_modsTracked & MODB_SHIFT) != 0;
        default: return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}
```

Used by `SimulateKeyCombo` to check "is the user already holding this key?"

---

## 16. The `m_busy` / `m_simulating` Guards

### `m_busy` — Prevents Re-entrant Execution

```cpp
void Keybinder::ExecuteQueuedAction(int bindingIndex) {
    if (m_busy) return;  // Already running? Don't run again.
    m_busy = true;
    ExecuteAction(m_bindings[bindingIndex]);
    m_busy = false;
}
```

Also in `HandleKeyEvent` (line 627):
```cpp
if (m_busy) return (pKb->flags & (LLKHF_INJECTED | 0x02)) ? 0 : 1;
```

While an action is executing:
- **Block real keys** (user presses) — they could interfere with `SendInput`
- **Allow injected keys** (from `SendInput`) — these are our own simulated keys

### `m_simulating` — Prevents Infinite Loops

```cpp
void Keybinder::SimulateKeyCombo(DWORD mods, DWORD vk) {
    m_simulating = true;
    SendInput(...);
    // Pump messages for 20ms
    m_simulating = false;
}
```

In `HandleKeyEvent` (line 631):
```cpp
if (m_simulating) return 0;
```

While simulating, don't match bindings. Otherwise: press `WIN+1` → hook triggers → simulates `WIN+CTRL+RIGHT` → hook sees `WIN+CTRL+RIGHT` → matches a `WIN+CTRL+RIGHT = SWITCH 5` binding → simulates more keys → infinite loop.

### Why Two Guards?

- `m_busy` is for when code is actively executing (switching desktops, moving windows)
- `m_simulating` is specifically for the `SendInput` injection window (which happens within `m_busy`)

They solve different problems:
- `m_busy` prevents **concurrent** executions (two keybinds firing at once)
- `m_simulating` prevents **echo** (injected keys triggering more keybinds)

---

## 17. The `RUN` Action — Launch Apps from Keybinds

### What it does

```
CTRL+SHIFT+P = RUN("notepad.exe", "C:\\log.txt")
```

When you press `CTRL+SHIFT+P`, WinBind launches `notepad.exe` with `C:\log.txt` as the argument. Works for any executable, batch script, or document.

### How it's implemented

The `RUN` action touches **4 places** in the code. Here they are:

#### 1. The `Keybinding` struct stores the app + args

**File:** `keybinder.h:52-53`

```cpp
struct Keybinding {
    // ... existing fields ...
    std::string runApp;   // Path to the executable
    std::string runArgs;  // Command-line arguments
};
```

Unlike `SWITCH` which only needs an `int arg` (desktop number), `RUN` needs two strings.

#### 2. Config parsing extracts the expression

**File:** `keybinder.cpp` — `LoadConfig`

When the config parser encounters something like `RUN("notepad.exe", "log.txt")`:

```cpp
if (act.find("RUN(") == 0 && act.back() == ')') {
    // act = 'RUN("NOTEPAD.EXE", "LOG.TXT")' (toupper'd)
    std::string inner = act.substr(4, act.size() - 5);
    // inner = '"NOTEPAD.EXE", "LOG.TXT"'

    size_t comma = inner.find(',');
    if (comma != std::string::npos) {
        runApp = Trim(inner.substr(0, comma));   // '"NOTEPAD.EXE"'
        runArgs = Trim(inner.substr(comma + 1)); // '"LOG.TXT"'
    } else {
        runApp = Trim(inner);  // No comma = no arguments
    }

    // Strip the surrounding quotes
    if (runApp.size() >= 2 && runApp[0] == '"' && runApp.back() == '"')
        runApp = runApp.substr(1, runApp.size() - 2);  // "NOTEPAD.EXE" → NOTEPAD.EXE
    if (runArgs.size() >= 2 && runArgs[0] == '"' && runArgs.back() == '"')
        runArgs = runArgs.substr(1, runArgs.size() - 2);

    act = "RUN";  // Normalize action name
}
```

Then at the end of `LoadConfig`, the parsed strings are stored into the `Keybinding`:

```cpp
Keybinding kb;
kb.modifiers = mods; kb.vkCode = vk; kb.action = act; kb.arg = arg;
kb.targetMods = targetMods; kb.targetVk = targetVk;
kb.runApp = runApp; kb.runArgs = runArgs;    // <--- RUN fields
m_bindings.push_back(kb);
```

#### 3. Action dispatch launches the process

**File:** `keybinder.cpp` — `ExecuteAction`, line 686-690

```cpp
else if (kb.action == "RUN") {
    ShellExecuteA(NULL, "open", kb.runApp.c_str(),
        kb.runArgs.empty() ? NULL : kb.runArgs.c_str(),
        NULL, SW_SHOWNORMAL);
}
```

`ShellExecuteA` is the Windows API for launching files. It's equivalent to typing a command in the Run dialog (WIN+R).

#### 4. Required header

**File:** `keybinder.cpp:20`

```cpp
#include <shellapi.h>   // For ShellExecuteA
```

### The Space Bug (and how it was fixed)

There's a subtle bug that broke `RUN` with spaces in the arguments.

**The problem:** Config parsing uses `rss >> act` (line 736), which reads **whitespace-delimited tokens**. For `SWITCH 3` this is fine: `act = "SWITCH"`, then `arg = 3`. But for:

```
RUN("notepad.exe", "")
```

The operator `>>` reads only up to the first space: `act = "RUN("NOTEPAD.EXE","`. The `act.back()` is `"`, NOT `)` — so the `RUN(...)` detection **fails**.

**The fix**:

```cpp
// RUN("app", "args") has spaces — rss >> act truncates it.
// Reconstruct the full expression from rhs.
if (act.find("RUN(") == 0 && act.back() != ')') {
    size_t closeParen = rhs.find(')');
    if (closeParen != std::string::npos) {
        act = rhs.substr(0, closeParen + 1);
        std::transform(act.begin(), act.end(), act.begin(), ::toupper);
    }
}
```

After `rss >> act` gives us the truncated `RUN("NOTEPAD.EXE","`, we check: "does `act` start with `RUN(` but NOT end with `)`?" If so, we go back to the **original** `rhs` string and extract the text from `RUN(` to the first `)`. Then uppercase it. Now `act = "RUN(\"NOTEPAD.EXE\", \"\")"`, which ends with `)` — parsing succeeds.

### Example config lines

```
CTRL+SHIFT+P = RUN("notepad.exe", "")          # No args
CTRL+SHIFT+O = RUN("C:\Program Files\MyApp\app.exe", "--safe-mode")
WIN+B = RUN("cmd.exe", "/c echo hello")        # Batch command
CTRL+ALT+T = RUN("ms-settings:")               # URI scheme
```

**Note:** Because the action string is uppercased for matching, the app path and arguments are also uppercased during parsing. On Windows this is fine — filenames and command-line args are case-insensitive (for the most part).

### How to extend `RUN` further

Want to add features like "run as admin" or "run hidden"?

Change the `ShellExecuteA` call:

```cpp
// Run as admin (needs a manifest, but the API call is the same with "runas")
ShellExecuteA(NULL, "runas", kb.runApp.c_str(), ...);

// Run minimized
ShellExecuteA(NULL, "open", kb.runApp.c_str(), ..., NULL, SW_MINIMIZE);
```

The `SW_SHOWNORMAL` parameter controls the window state. Options:
- `SW_SHOWNORMAL` — normal window
- `SW_MINIMIZE` — minimized to taskbar
- `SW_HIDE` — completely hidden (good for batch scripts)
- `SW_MAXIMIZE` — full screen

### How to add a NEW action (e.g. `CLOSE_WINDOW`)

Now that you understand `RUN`, here's the recipe for any new action:

1. **`keybinder.h`** — Add any new fields to `Keybinding` struct (if needed)
2. **`keybinder.cpp` `LoadConfig`** — Add parsing logic (if the action has special syntax)
3. **`keybinder.cpp` `ExecuteAction`** — Add `else if (kb.action == "YOUR_ACTION") { ... }`
4. **Add needed headers** — If you use a new API, `#include` the header
5. **Document in `winbind.conf`** — Add a comment showing the syntax

---

## 18. How to Add a New Config Option

Say you want a new boolean setting like `Launch_At_Startup = Enabled`.

### Step 1: Add a member variable

In `keybinder.h`, add to the `Keybinder` class:
```cpp
bool m_launchAtStartup;  // New setting
```

Initialize it in the constructor (`keybinder.cpp:250`):
```cpp
Keybinder::Keybinder()
    : ..., m_launchAtStartup(false) {  // Add this
```

### Step 2: Parse it in `LoadConfig`

In `keybinder.cpp`, `LoadConfig`, after the existing flag parsers:
```cpp
if (lhs == "Launch_At_Startup") { parseBool(m_launchAtStartup); continue; }
```

### Step 3: Use it somewhere

For example, in `Init()` or `WinMain`, if `m_launchAtStartup` is true, add the program to Windows startup registry.

---

## 19. How to Make Something Run Once vs Every Time

### Pattern 1: Inside a Keybind Action (runs once per keypress)

Everything in `ExecuteAction` runs **once** each time you press the keybind. That's the default.

```cpp
else if (kb.action == "MY_ACTION") {
    // This code runs ONCE every time the keybind is pressed
    DoSomething();
}
```

### Pattern 2: On Program Start (runs once at launch)

Put code in `WinMain` (after `kb.Init()` but before the message loop):

```cpp
if (!kb.Init()) return 1;

// --- ONCE AT STARTUP ---
if (kb.SomeCondition()) {
    DoStartupThing();
}
// --- END ONCE ---

int delayMs = LoadTransparencyDelayMs();
SetTimer(hWnd, TIMER_TRANSPARENCY, delayMs, NULL);
```

### Pattern 3: On Every Keystroke (runs constantly)

Put code in `HandleKeyEvent`. It runs for EVERY keypress on the system:

```cpp
LRESULT Keybinder::HandleKeyEvent(WPARAM wParam, LPARAM lParam) {
    // --- RUNS ON EVERY KEYSTROKE ---
    CheckConfigChanged();  // <-- This runs on every keypress!
    // --- END EVERY KEYSTROKE ---
```

`CheckConfigChanged` runs on every keystroke. This is why config changes take effect instantly without restarting.

### Pattern 4: Periodic Timer (runs at a set interval)

Like transparency — uses `SetTimer`:

In `WinMain`:
```cpp
SetTimer(hWnd, TIMER_TRANSPARENCY, 1000, NULL);  // Every 1000ms
```

In `WndProc`:
```cpp
if (msg == WM_TIMER && wParam == TIMER_TRANSPARENCY) {
    // This code runs EVERY 1 SECOND
    DoPeriodicThing();
}
```

### Summary

| Pattern | Runs When | Example |
|---------|-----------|---------|
| Keybind action | When keybind pressed | Switching desktop |
| Program start | Once at launch | Loading config |
| Every keystroke | Each keypress | Checking config changes |
| Timer | Every N ms | Applying transparency |

---

## 20. Build & Run

### Using build.bat

```
build.bat          # Just compile
build.bat --run    # Compile AND launch
```

The build script:
```
g++ -O2 -static -mwindows -std=c++11 -s main.cpp keybinder.cpp Transparency.cpp -o WinBind.exe -lole32 -lgdi32 -luser32 -luuid -ldwmapi
```

### Flags Explained

| Flag | Meaning |
|------|---------|
| `-O2` | Optimize for speed |
| `-static` | Include libraries in the .exe (no DLL dependencies) |
| `-mwindows` | Windows app (no console window) |
| `-std=c++11` | Use C++11 standard |
| `-s` | Strip debug symbols (smaller .exe) |
| `-lole32` | Link with COM library |
| `-lgdi32` | Link with GDI (graphics) |
| `-luser32` | Link with user interface |
| `-luuid` | Link with UUID library |
| `-ldwmapi` | Link with Desktop Window Manager |

### Manual Compile

If build.bat doesn't work, compile manually:
```
g++ -O2 -static -mwindows -std=c++11 -s main.cpp keybinder.cpp Transparency.cpp -o WinBind.exe -lole32 -lgdi32 -luser32 -luuid -ldwmapi
```

### Running

```
WinBind.exe                              # Uses winbind.conf in current folder
WinBind.exe --config "C:\my.conf"        # Uses custom config
WinBind.exe --config=my_settings.conf    # Alternative syntax
```

---

## 21. Common Pitfalls

### Pitfall 1: WIN+ALT+NUMBER doesn't work

**Why:** Windows 11 reserves `WIN+ALT+NUMBER` (taskbar jump lists) and `WIN+SHIFT+NUMBER` (new instance). No user-mode hook can intercept these.

**Fix:** Use `WIN+CTRL+NUMBER` or other non-reserved combos.

### Pitfall 2: Config changes don't take effect

**Why:** `CheckConfigChanged` compares file modification timestamps. Some editors (like Notepad) create a new file on save, which changes the timestamp. But if you edit the file while WinBind isn't running, the timestamp is already newer when WinBind starts — so it loads it once and doesn't detect changes until you save again.

**Fix:** Save the file again after WinBind starts. Or restart WinBind.

### Pitfall 3: Simulated keys don't reach the target app

**Why:** Some apps (especially games, terminals, and admin apps) use `GetAsyncKeyState` or raw input instead of the keyboard message queue. `SendInput` injects into the message queue — it won't reach apps that bypass the queue.

**Fix:** There's no universal fix. Try `WIN+SHIFT+ARROW` (attempt 3 in `MoveCurrentWindowToDesktop`) as a fallback, or run WinBind as admin (though this is not recommended).

### Pitfall 4: Crash on COM calls

**Why:** The undocumented `IVirtualDesktopManagerInternal` interface changes with every Windows update. If the vtable index for `MoveViewToDesktop`, `FindDesktop`, or `RemoveDesktop` changed, the SIGSEGV guard catches it, but the operation fails.

**Fix:** Check if there's a new IID for your Windows version and add it to the `pairs[]` array in `TryMoveWindowViaService` and `RemoveEmptyDesktops`.

### Pitfall 5: `SendInput` doesn't work inside the hook

**Why:** Windows has a restriction: `SendInput` called from within a low-level keyboard hook callback may not work correctly. That's why WinBind **posts a message** to the hidden window and executes actions in the message loop instead.

### Pitfall 6: Infinite loop (binding triggers itself)

If you bind `WIN+1 = SIMULATE_KEY[WIN+1]`, pressing `WIN+1` will:
1. Hook catches `WIN+1`
2. Posts `WM_KB_EXECUTE`
3. Executes `SIMULATE_KEY[WIN+1]`
4. SendInput injects `WIN+1`
5. Hook catches injected `WIN+1`
6. Goes to step 2 — forever

**The fix:** `m_simulating` guard prevents injected keys from matching bindings. But only during the 20ms message pump window. If the timing is unlucky, you can still get loops. Don't bind a key to simulate itself.

---

## Quick Reference: All Functions

### `main.cpp`

| Function | Line | Purpose |
|----------|------|---------|
| `WndProc` | 11 | Handles messages for the hidden window (keybind execution, timer) |
| `WinMain` | 25 | Entry point: loads config, creates window, starts hook, runs loop |

### `keybinder.h`

| Type | Line | Purpose |
|------|------|---------|
| `WM_KB_EXECUTE` | 18 | Custom message: "execute a binding" |
| `MODB_WIN`, `MODB_CTRL`, etc. | 21-24 | Bitmask constants for modifiers |
| `ComPtr<T>` | 28 | RAII smart pointer for COM objects |
| `Keybinding` | 45 | Struct: a single parsed hotkey binding |
| `Keybinder` class | 54 | Main class: hook, switching, COM |

### `keybinder.cpp`

| Function | Line | Purpose |
|----------|------|---------|
| `SigSegvHandler` | 70 | Catches crashes from wrong COM vtable calls |
| `GetShellServiceProvider` | 78 | Gets the COM service for virtual desktop operations |
| `TryMoveWindowViaService` | 101 | Moves window via undocumented COM (attempt 1) |
| `Trim` | 164 | Removes whitespace from string ends |
| `ParseModifier` | 170 | Converts "WIN" to `MODB_WIN` bitmask |
| `KeyNameToVK` | 180 | Converts "F1", "LEFT", "A" to virtual-key codes |
| `IsSystemShortcut` | 213 | Checks if a combo is a dangerous system shortcut |
| `Keybinder::Instance` | 245 | Gets the singleton Keybinder object |
| `Keybinder::Init` | 260 | Starts hook and COM |
| `Keybinder::Stop` | 268 | Stops hook and COM |
| `CheckConfigChanged` | 273 | Checks if config file was modified |
| `RefreshDesktopList` | 286 | Reads virtual desktops from registry |
| `IsModTracked` | 327 | Checks if a modifier key is currently held |
| `SimulateKeyCombo` (3-param) | 337 | Simulates WIN+CTRL+D, WIN+CTRL+ARROW, etc. |
| `SimulateKeyCombo` (2-param) | 353 | Simulates arbitrary modifier+key combos |
| `SwitchToDesktop` | 401 | Switches to desktop N via simulated keys |
| `IsSystemWindowClass` | 437 | Checks if a window is a system window (skip it) |
| `EnumDesktopCheckProc` | 451 | Callback: marks which desktops have windows |
| `MoveCurrentWindowToDesktop` | 470 | Moves active window (3 attempts) |
| `RemoveEmptyDesktops` | 511 | Detects and deletes empty desktops |
| `KeyboardProc` | 601 | Low-level hook callback (static) |
| `HandleKeyEvent` | 607 | Processes each keystroke |
| `ExecuteQueuedAction` | 649 | Wraps ExecuteAction with busy guard |
| `ExecuteAction` | 657 | Dispatches to the right action handler |
| `LoadConfig` | 697 | Reads and parses winbind.conf |

### `Transparency.h`

| Function | Line | Purpose |
|----------|------|---------|
| `LoadTransparencyConfig` | 6 | Reads transparency settings from config |
| `LoadTransparencyDelayMs` | 7 | Reads timer delay from config |
| `SetWindowTransparency` | 8 | Makes a single window transparent |
| `EnumWindowsProc` | 9 | Callback: applies transparency to all visible windows |

### `Transparency.cpp`

| Function | Line | Purpose |
|----------|------|---------|
| `Trim` | 8 | Same as in keybinder.cpp — removes whitespace |
| `LoadTransparencyConfig` | 14 | Reads Transparency + Opacity from winbind.conf |
| `LoadTransparencyDelayMs` | 50 | Reads Transparency_Delay from winbind.conf |
| `SetWindowTransparency` | 82 | Applies `WS_EX_LAYERED` + `LWA_ALPHA` to a window |
| `EnumWindowsProc` | 92 | Callback for EnumWindows — filters and applies transparency |

---

## Quick Reference: All Classes

### `Keybinder` Class

**Purpose:** The brain of WinBind. Manages the keyboard hook, desktop operations, and COM.

**How to use (from outside):**
```cpp
Keybinder& kb = Keybinder::Instance();  // Get the singleton
kb.LoadConfig("winbind.conf");          // Load settings
kb.SetWindowHandle(hWnd);              // Give it a window for messages
kb.Init();                             // Start hook
// ... program runs ...
kb.Stop();                             // Clean up
```

**Configuration flags:**

| Member | Config Key | Default | Purpose |
|--------|-----------|---------|---------|
| `m_enabled` | `Virtual_Desktop_Switch` | `true` | Master switch for desktop features |
| `m_newDesktopCreation` | `New_Desktop_Creation` | `false` | Auto-create desktops when switching beyond last |
| `m_allowBadKeys` | `Allow_Bad_Keys` | `false` | Allow system-reserved shortcuts |
| `m_windowAndDesktopSwitch` | `Window_And_Virtual_Desktop_Switch` | `true` | Enable `MOVE_SWITCH` action |
| `m_autoRemoveDesktop` | `Auto_Remove_Virtual_Desktop` | `true` | Auto-delete empty desktops |

**Internal state:**

| Member | Type | Purpose |
|--------|------|---------|
| `m_hHook` | `HHOOK` | Handle to the keyboard hook |
| `m_hWnd` | `HWND` | Handle to the hidden message window |
| `m_bindings` | `vector<Keybinding>` | The parsed keybinds |
| `m_desktops` | `vector<DesktopInfo>` | List of virtual desktop GUIDs |
| `m_currentDesktop` | `int` | Index of the currently active desktop |
| `m_busy` | `bool` | True while an action is executing |
| `m_simulating` | `bool` | True while SendInput is injecting keys |
| `m_modsTracked` | `DWORD` | Bitmask of currently held modifiers |

### `Keybinding` Struct

**Purpose:** One parsed line from the config file.

| Field | Type | For Which Action | Meaning |
|-------|------|-----------------|---------|
| `modifiers` | `DWORD` | All | Bitmask: which modifiers to hold (WIN, CTRL, etc.) |
| `vkCode` | `DWORD` | All | Virtual-key code of the main key |
| `action` | `string` | All | What to do (`SWITCH`, `MOVE`, etc.) |
| `arg` | `int` | `SWITCH`, `MOVE`, `MOVE_SWITCH` | Target desktop number (1-based) |
| `targetMods` | `DWORD` | `SIMULATE_KEY` | Modifiers to simulate |
| `targetVk` | `DWORD` | `SIMULATE_KEY` | Key to simulate |

### `ComPtr<T>` Template

**Purpose:** Automatic COM pointer cleanup (like `std::unique_ptr`).

| Method | Purpose |
|--------|---------|
| `ComPtr()` | Creates null pointer |
| `ComPtr(T*)` | Wraps existing pointer |
| `~ComPtr()` | Calls `Release()` |
| `operator&()` | Returns `T**` (for COM out-params) |
| `operator->()` | Dereferences |
| `operator T*()` | Implicit conversion |
| `Get()` | Returns raw pointer |
| `Reset()` | Drops current pointer |

### `DesktopInfo` Struct

```cpp
struct DesktopInfo { GUID id; };
```

A virtual desktop identified by its GUID (16-byte unique identifier from the Windows registry).

### `IVirtualDesktopManagerVT` Struct

```cpp
struct IVirtualDesktopManagerVT {
    HRESULT (*QueryInterface)(void*, REFIID, void**);
    ULONG   (*AddRef)(void*);
    ULONG   (*Release)(void*);
    HRESULT (*IsWindowOnCurrentVirtualDesktop)(void*, HWND, BOOL*);
    HRESULT (*GetWindowDesktopId)(void*, HWND, GUID*);
    HRESULT (*MoveWindowToDesktop)(void*, HWND, REFGUID);
};
```

Manual vtable for the public `IVirtualDesktopManager` COM interface. Used to call `MoveWindowToDesktop` and `GetWindowDesktopId` without the official header.

### `MgrIidPair` Struct

```cpp
struct MgrIidPair { const GUID& svc; const GUID& iid; };
```

Pairs a service GUID with an interface GUID. Used to try multiple COM interface IDs for `IVirtualDesktopManagerInternal`.

---

## Next Steps for a Developer

1. **Read the code** — Start with `main.cpp` (entry point), then `keybinder.h` (structure), then `keybinder.cpp` (logic).

2. **Improve `ComPtr` usage** — Currently, most COM calls use raw pointers and manual `Release()`. Wrapping them in `ComPtr` would make the code safer.

3. **Add logging** — Currently, WinBind is silent. Adding `OutputDebugString` or log file writes would help debug issues.

4. **Support more key names** — `KeyNameToVK` doesn't support all keys (e.g., `CAPSLOCK`, `NUMLOCK`, `PRINTSCREEN`, volume keys).

5. **Handle multi-monitor edge cases** — `MoveCurrentWindowToDesktop` uses `GetForegroundWindow`, which may not get the correct window on multi-monitor setups.

6. **Deduplicate `Trim`** — The same function exists in both `keybinder.cpp` and `Transparency.cpp`. Move it to a shared utility.

7. **Test on different Windows versions** — The COM IIDs change per Windows build. Test on Win10, Win11 22H2, Win11 23H2, Win11 24H2.

8. **Add error messages** — If config parsing fails, the program silently uses defaults. A message box or log would help users debug.

9. **Structure the document** — Currently, **this** document is mostly **NOT** structured if you noticed... Please do that, right now me as a dev is just dumpping whatever I create.

10. **Sponser/Recommend this project to others** — MOST of my projects, due to No-Promotion never get to be seen by people who actually might find this useful and as a dev, this **Discourages** me to work on this project. If you like this project, and want it to work out, my first priority would be that you tell your neighbour/friends, and then if you like you can sponser. More the number of users, more motivated I am to work, **Even if I get no money**.

---

*Happy coding! Remember: every expert was once a beginner who didn't give up.*
