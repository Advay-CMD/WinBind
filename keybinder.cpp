// WinBind - keybinder script Code
// Handles low-level keyboard hook dispatch, registry-based virtual desktop
// enumeration, SendInput-based desktop switching, and an undocumented-COM
// approach for moving windows between desktops without elevation.
#include <windows.h>
#include <winreg.h>
#include <shlobj.h>
#include <objbase.h>
#include "keybinder.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <signal.h>
#include <setjmp.h>
#include <shlguid.h>

// ---------------------------------------------------------------------------
// Trace logging — appends a line to kb_trace.log (silent if file can't open)
// ---------------------------------------------------------------------------
static void TraceLog(const char* msg) {
    HANDLE h = CreateFileA("kb_trace.log", FILE_APPEND_DATA, FILE_SHARE_READ,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, msg, lstrlenA(msg), &w, NULL);
        WriteFile(h, "\r\n", 2, &w, NULL);
        CloseHandle(h);
    }
}

// ---------------------------------------------------------------------------
// COM GUIDs for the undocumented Virtual Desktop Manager Internal interface.
// The service GUID (CLSID) stays the same across recent Windows builds;
// the interface IID changes with each major Windows version.  We try them
// in order of likelihood and fall back gracefully on crash via SIGSEGV.
// ---------------------------------------------------------------------------
static const GUID GUID_CLSID_ShellWindows =
    {0x9BA05972, 0xF6A8, 0x11CF, {0xA4, 0x42, 0x00, 0xA0, 0xC9, 0x0A, 0x8F, 0x39}};
static const GUID GUID_CLSID_ImmersiveShell =
    {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
static const GUID GUID_IID_IServiceProvider =
    {0x6D5140C1, 0x7436, 0x11CE, {0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA}};

static const GUID GUID_SVC_VDMgr =
    {0xC5E0CDCA, 0x7B6E, 0x41B2, {0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B}};
// Build 26100+  (Win11 24H2+)
static const GUID GUID_IID_VDMgr_26100 =
    {0x53F5CA0B, 0x158F, 0x4124, {0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27}};
// Build 22621   (Win11 23H2)
static const GUID GUID_IID_VDMgr_22621 =
    {0xA3175F2D, 0x239C, 0x4BD2, {0x8A, 0xA0, 0xEE, 0xBA, 0x8B, 0x0B, 0x13, 0x8E}};
// Build 22000   (Win11 21H2)
static const GUID GUID_IID_VDMgr_22000 =
    {0xB2F925B9, 0x5A0F, 0x4D2E, {0x9F, 0x4D, 0x2B, 0x15, 0x07, 0x59, 0x3C, 0x10}};
// Build 25179   (pre-24H2 dev)
static const GUID GUID_IID_VDMgr_25179 =
    {0x6B1CB1E5, 0xF8F9, 0x4D7B, {0x9F, 0x7E, 0x5B, 0xE0, 0x8F, 0xC6, 0xF2, 0xF4}};
// Pre-Win11
static const GUID GUID_IID_VDMgr_OLD =
    {0xF31574D6, 0xB682, 0x4CDC, {0xBD, 0x56, 0x18, 0x27, 0x86, 0x0A, 0xBE, 0xC6}};

// IApplicationViewCollection — two known IIDs; try older first, then newer.
static const GUID GUID_IID_AVC_OLD =
    {0x1841C6D2, 0x6F9D, 0x42CA, {0xA6, 0x2D, 0x0C, 0x5F, 0x3A, 0x8D, 0x8C, 0x77}};
static const GUID GUID_IID_AVC_NEW =
    {0x1841C6D7, 0x4F9D, 0x42C0, {0xAF, 0x41, 0x87, 0x47, 0x53, 0x8F, 0x10, 0xE5}};

// setjmp/longjmp buffer for the SIGSEGV guard around COM calls.
static jmp_buf g_comJmpBuf;

extern "C" void __cdecl SigSegvHandler(int) {
    longjmp(g_comJmpBuf, 1);
}

// Obtains an IServiceProvider from the shell — either via ImmersiveShell
// (preferred, CLSCTX_LOCAL_SERVER) or IShellWindows (fallback).
static IServiceProvider* GetShellServiceProvider() {
    IServiceProvider* pSP = NULL;
    HRESULT hr = CoCreateInstance(GUID_CLSID_ImmersiveShell, NULL,
        CLSCTX_LOCAL_SERVER, GUID_IID_IServiceProvider, (void**)&pSP);
    if (SUCCEEDED(hr) && pSP) return pSP;

    IShellWindows* pSW = NULL;
    hr = CoCreateInstance(GUID_CLSID_ShellWindows, NULL, CLSCTX_ALL,
        IID_IShellWindows, (void**)&pSW);
    if (FAILED(hr) || !pSW) return NULL;

    VARIANT v;
    V_VT(&v) = VT_I4; V_I4(&v) = 0;
    IDispatch* pDisp = NULL;
    hr = pSW->Item(v, &pDisp);
    pSW->Release();
    if (FAILED(hr) || !pDisp) return NULL;

    pDisp->QueryInterface(GUID_IID_IServiceProvider, (void**)&pSP);
    pDisp->Release();
    return pSP;
}

struct MgrIidPair { const GUID& svc; const GUID& iid; };

// Tries to move hWnd to the desktop identified by targetGuid using
// the undocumented IVirtualDesktopManagerInternal COM service.
// Protected by SIGSEGV signal — if the vtable layout is wrong for this
// Windows build, we catch the crash and return false instead of dying.
static bool TryMoveWindowViaService(HWND hWnd, const GUID& targetGuid) {
    void (*oldHandler)(int) = signal(SIGSEGV, SigSegvHandler);
    bool success = false;

    if (setjmp(g_comJmpBuf) == 0) {
        IServiceProvider* pSP = GetShellServiceProvider();
        if (!pSP) { signal(SIGSEGV, oldHandler); TraceLog("COM: no SP"); return false; }

        MgrIidPair pairs[] = {
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_26100},
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_22621},
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_22000},
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_25179},
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_OLD},
        };

        void* pMgr = NULL;
        for (int i = 0; i < 5; i++) {
            HRESULT hr = pSP->QueryService(pairs[i].svc, pairs[i].iid, &pMgr);
            if (SUCCEEDED(hr) && pMgr) break;
        }
        if (!pMgr) {
            TraceLog("COM: QS all failed");
            pSP->Release();
            signal(SIGSEGV, oldHandler);
            return false;
        }

        void** vt = *(void***)pMgr;

        // Get IApplicationViewCollection.
        void* pVC = NULL;
        HRESULT hr = pSP->QueryService(GUID_IID_AVC_OLD, GUID_IID_AVC_OLD, &pVC);
        if (FAILED(hr) || !pVC)
            hr = pSP->QueryService(GUID_IID_AVC_NEW, GUID_IID_AVC_NEW, &pVC);
        if (FAILED(hr) || !pVC) {
            TraceLog("COM: QS AVC failed");
            ((IUnknown*)pMgr)->Release();
            pSP->Release();
            signal(SIGSEGV, oldHandler);
            return false;
        }

        void** vcvt = *(void***)pVC;

        // GetViewForHwnd is at vtable index 6 on IApplicationViewCollection.
        typedef HRESULT (STDMETHODCALLTYPE *GetViewFn)(void*, HWND, void**);
        GetViewFn getView = (GetViewFn)vcvt[6];
        void* pView = NULL;
        hr = getView(pVC, hWnd, &pView);

        if (SUCCEEDED(hr) && pView) {
            // FindDesktop is at vtable index 14 on the 26100+ layout
            // (a new SwitchDesktopAndMoveForegroundView at index 10
            //  shifted everything after it by one).
            typedef HRESULT (STDMETHODCALLTYPE *FindDeskFn)(void*, GUID*, void**);
            FindDeskFn findDesk = (FindDeskFn)vt[14];
            void* pTarget = NULL;
            GUID g = targetGuid;
            hr = findDesk(pMgr, &g, &pTarget);

            if (SUCCEEDED(hr) && pTarget) {
                // MoveViewToDesktop is at vtable index 4.
                typedef HRESULT (STDMETHODCALLTYPE *MoveViewFn)(void*, void*, void*);
                MoveViewFn moveView = (MoveViewFn)vt[4];
                hr = moveView(pMgr, pView, pTarget);

                char t[128];
                wsprintfA(t, "COM MoveView hr=0x%08lX", hr);
                TraceLog(t);
                success = SUCCEEDED(hr);

                ((IUnknown*)pTarget)->Release();
            }
            ((IUnknown*)pView)->Release();
        }

        ((IUnknown*)pVC)->Release();
        ((IUnknown*)pMgr)->Release();
        pSP->Release();
    } else {
        TraceLog("SIGSEGV in COM service call");
    }

    signal(SIGSEGV, oldHandler);
    return success;
}

// ---------------------------------------------------------------------------
// Keybinder singleton
// ---------------------------------------------------------------------------
Keybinder& Keybinder::Instance() {
    static Keybinder inst;
    return inst;
}

Keybinder::Keybinder()
    : m_hHook(NULL), m_hWnd(NULL), m_currentDesktop(-1), m_enabled(true),
      m_newDesktopCreation(false), m_allowBadKeys(false), m_windowAndDesktopSwitch(true) {
    ZeroMemory(&m_configTime, sizeof(m_configTime));
}

Keybinder::~Keybinder() { Stop(); }

bool Keybinder::Init() {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    RefreshDesktopList();
    CheckConfigChanged();
    m_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    return m_hHook != NULL;
}

void Keybinder::Stop() {
    if (m_hHook) {
        UnhookWindowsHookEx(m_hHook);
        m_hHook = NULL;
    }
    CoUninitialize();
}

// ---------------------------------------------------------------------------
// Config hot-reload: checks file timestamp, re-parses if modified.
// ---------------------------------------------------------------------------
bool Keybinder::CheckConfigChanged() {
    HANDLE hFile = CreateFileA(m_configPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    FILETIME ft;
    BOOL ok = GetFileTime(hFile, NULL, NULL, &ft);
    CloseHandle(hFile);
    if (!ok) return false;

    if (CompareFileTime(&ft, &m_configTime) == 0) return false;
    m_configTime = ft;
    LoadConfig(m_configPath);
    return true;
}

// ---------------------------------------------------------------------------
// Reads HKCU\VirtualDesktops to enumerate desktop GUIDs and determine
// which desktop is currently active.
// ---------------------------------------------------------------------------
bool Keybinder::RefreshDesktopList() {
    m_desktops.clear();
    m_currentDesktop = -1;

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops",
            0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    BYTE buf[4096];
    DWORD type, size = sizeof(buf);
    if (RegQueryValueExA(hKey, "VirtualDesktopIDs", NULL, &type, buf, &size) != ERROR_SUCCESS
            || type != REG_BINARY) {
        RegCloseKey(hKey);
        return false;
    }

    DWORD count = size / 16;
    for (DWORD i = 0; i < count; i++) {
        DesktopInfo di;
        memcpy(&di.id, buf + i * 16, 16);
        m_desktops.push_back(di);
    }

    size = sizeof(buf);
    if (RegQueryValueExA(hKey, "CurrentVirtualDesktop", NULL, &type, buf, &size)
            == ERROR_SUCCESS && type == REG_BINARY && size >= 16) {
        GUID current;
        memcpy(&current, buf, 16);
        for (size_t i = 0; i < m_desktops.size(); i++) {
            if (memcmp(&m_desktops[i].id, &current, 16) == 0) {
                m_currentDesktop = (int)i;
                break;
            }
        }
    }

    RegCloseKey(hKey);
    return !m_desktops.empty();
}

// Returns true if either the left or right variant of vk is physically held.
static bool IsModFamilyHeld(WORD vk) {
    switch (vk) {
        case VK_LWIN: case VK_RWIN:
            return (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
        case VK_LCONTROL: case VK_RCONTROL:
            return (GetAsyncKeyState(VK_LCONTROL) & 0x8000) || (GetAsyncKeyState(VK_RCONTROL) & 0x8000);
        case VK_LMENU: case VK_RMENU:
            return (GetAsyncKeyState(VK_LMENU) & 0x8000) || (GetAsyncKeyState(VK_RMENU) & 0x8000);
        case VK_LSHIFT: case VK_RSHIFT:
            return (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
        default:
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}

// ---------------------------------------------------------------------------
// SimulateKeyCombo — uses SendInput to produce a chord like WIN+CTRL+LEFT.
// Only presses modifiers that aren't already held (avoids interfering with
// modifier keys the user is still holding when the async action fires).
// ---------------------------------------------------------------------------
void Keybinder::SimulateKeyCombo(WORD mod1, WORD mod2, WORD key) {
    INPUT inputs[8];
    ZeroMemory(inputs, sizeof(inputs));
    int idx = 0;

    bool held1 = mod1 && IsModFamilyHeld(mod1);
    bool held2 = mod2 && IsModFamilyHeld(mod2);

    if (!held1 && mod1) { inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = mod1; idx++; }
    if (!held2 && mod2) { inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = mod2; idx++; }

    inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = key; idx++;
    inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = key; inputs[idx].ki.dwFlags = KEYEVENTF_KEYUP; idx++;

    if (!held2 && mod2) { inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = mod2; inputs[idx].ki.dwFlags = KEYEVENTF_KEYUP; idx++; }
    if (!held1 && mod1) { inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = mod1; inputs[idx].ki.dwFlags = KEYEVENTF_KEYUP; idx++; }

    if (idx > 0) SendInput(idx, inputs, sizeof(INPUT));
    Sleep(50);
}

// ---------------------------------------------------------------------------
// SwitchToDesktop — switches to virtual desktop num (1-based).
// Uses WIN+CTRL+LEFT/RIGHT via SendInput.  Auto-creates desktops up to
// the requested index if New_Desktop_Creation is enabled.
// ---------------------------------------------------------------------------
void Keybinder::SwitchToDesktop(int num) {
    if (!m_enabled) return;
    if (m_desktops.empty()) return;
    RefreshDesktopList();

    int targetIdx = num - 1;
    if (targetIdx < 0) return;

    int created = 0;
    while (targetIdx >= (int)m_desktops.size()) {
        if (!m_newDesktopCreation) return;
        if (++created > 20) return;
        SimulateKeyCombo(VK_LWIN, VK_LCONTROL, 'D');
        RefreshDesktopList();
    }

    if (m_currentDesktop == targetIdx) return;

    int diff = targetIdx - m_currentDesktop;
    WORD dirKey = (diff > 0) ? VK_RIGHT : VK_LEFT;
    int presses = diff > 0 ? diff : -diff;
    if (presses > 20) presses = 20;

    for (int i = 0; i < presses; i++)
        SimulateKeyCombo(VK_LWIN, VK_LCONTROL, dirKey);

    RefreshDesktopList();
}

// VTable for the documented (but integrity-checked) IVirtualDesktopManager.
// Used only as a fallback when the service approach fails.
struct IVirtualDesktopManagerVT {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG   (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *IsWindowOnCurrentVirtualDesktop)(void*, HWND, BOOL*);
    HRESULT (STDMETHODCALLTYPE *GetWindowDesktopId)(void*, HWND, GUID*);
    HRESULT (STDMETHODCALLTYPE *MoveWindowToDesktop)(void*, HWND, REFGUID);
};

// ---------------------------------------------------------------------------
// MoveCurrentWindowToDesktop — moves the foreground window to desktop num.
// Tries three strategies in order:
//   1. COM service approach (IVirtualDesktopManagerInternal via QueryService)
//   2. Public IVirtualDesktopManager (CoCreateInstance — E_ACCESSDENIED at ML)
//   3. SendInput WIN+SHIFT+LEFT/RIGHT (moves between monitors, not desktops)
// ---------------------------------------------------------------------------
void Keybinder::MoveCurrentWindowToDesktop(int num) {
    if (!m_enabled) return;
    RefreshDesktopList();
    if (m_desktops.empty()) return;

    int targetIdx = num - 1;
    if (targetIdx < 0 || targetIdx >= (int)m_desktops.size()) return;
    if (m_currentDesktop == targetIdx) return;

    HWND hWnd = GetForegroundWindow();
    if (!hWnd) return;

    // Attempt 1: COM service approach (IVirtualDesktopManagerInternal, works
    // on Win11 24H2+ without elevation because it runs inside Explorer's COM
    // server context via QueryService).
    if (TryMoveWindowViaService(hWnd, m_desktops[targetIdx].id)) {
        Sleep(100);
        return;
    }

    // Attempt 2: Public IVirtualDesktopManager API.
    // This uses CoCreateInstance and is subject to UIPI — it returns
    // E_ACCESSDENIED at Medium IL on most recent builds.
    CLSID clsid;
    IID iid;
    if (SUCCEEDED(CLSIDFromString((LPOLESTR)L"{AA509086-5CA9-4C25-8F95-589D3C07B48A}", &clsid))
        && SUCCEEDED(CLSIDFromString((LPOLESTR)L"{A5CD92FF-29BE-454C-8D04-D82879FB3F1B}", &iid))) {
        void* pObj = NULL;
        HRESULT hr = CoCreateInstance(clsid, NULL, CLSCTX_ALL, iid, &pObj);
        if (SUCCEEDED(hr) && pObj) {
            IVirtualDesktopManagerVT* vt = *(IVirtualDesktopManagerVT**)pObj;
            hr = vt->MoveWindowToDesktop(pObj, hWnd, m_desktops[targetIdx].id);
            ((IUnknown*)pObj)->Release();
            char t[128];
            wsprintfA(t, "MoveWinToDesktop hr=0x%08lX", hr);
            TraceLog(t);
            if (SUCCEEDED(hr)) {
                Sleep(100);
                return;
            }
        }
    }

    // Attempt 3: SendInput WIN+SHIFT+LEFT/RIGHT.
    // This is a best-effort fallback — on most Windows versions this moves
    // the window between monitors, not virtual desktops.
    int diff = targetIdx - m_currentDesktop;
    WORD dirKey = (diff > 0) ? VK_RIGHT : VK_LEFT;
    int presses = diff > 0 ? diff : -diff;
    if (presses > 20) presses = 20;

    for (int i = 0; i < presses; i++)
        SimulateKeyCombo(VK_LWIN, VK_LSHIFT, dirKey);
    Sleep(100);
}

// ---------------------------------------------------------------------------
// Low-level keyboard hook (WH_KEYBOARD_LL)
// ---------------------------------------------------------------------------
LRESULT CALLBACK Keybinder::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        if (Instance().HandleKeyEvent(wParam, lParam) == 1)
            return 1;
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// HandleKeyEvent — called for every key-down event.
// Checks if the current modifier + key combination matches any binding.
// If so, posts WM_KB_EXECUTE to the hidden window (async execution) and
// returns 1 to block the key from reaching other applications.
// ---------------------------------------------------------------------------
LRESULT Keybinder::HandleKeyEvent(WPARAM wParam, LPARAM lParam) {
    if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN) return 0;

    KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
    DWORD vk = pKb->vkCode;

    // Never block raw modifier keys — they must pass through.
    if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_LCONTROL ||
        vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU ||
        vk == VK_LSHIFT || vk == VK_RSHIFT)
        return 0;

    CheckConfigChanged();

    if (!m_enabled) return 0;

    DWORD mods = GetModifierState();

    for (size_t i = 0; i < m_bindings.size(); i++) {
        if (m_bindings[i].vkCode == vk && m_bindings[i].modifiers == mods) {
            if (m_hWnd) PostMessage(m_hWnd, WM_KB_EXECUTE, i, 0);
            return 1;
        }
    }
    return 0;
}

void Keybinder::ExecuteQueuedAction(int bindingIndex) {
    if (bindingIndex < 0 || bindingIndex >= (int)m_bindings.size()) return;
    ExecuteAction(m_bindings[bindingIndex]);
}

DWORD Keybinder::GetModifierState() {
    DWORD mods = 0;
    if (GetAsyncKeyState(VK_LWIN) & 0x8000 || GetAsyncKeyState(VK_RWIN) & 0x8000) mods |= MODB_WIN;
    if (GetAsyncKeyState(VK_LCONTROL) & 0x8000 || GetAsyncKeyState(VK_RCONTROL) & 0x8000) mods |= MODB_CTRL;
    if (GetAsyncKeyState(VK_LMENU) & 0x8000 || GetAsyncKeyState(VK_RMENU) & 0x8000) mods |= MODB_ALT;
    if (GetAsyncKeyState(VK_LSHIFT) & 0x8000 || GetAsyncKeyState(VK_RSHIFT) & 0x8000) mods |= MODB_SHIFT;
    return mods;
}

// ---------------------------------------------------------------------------
// ExecuteAction — dispatches a parsed binding.
// MOVE_SWITCH: ensures target desktop exists, navigates back to the original
// desktop if desktop creation moved us, moves the window, then switches.
// ---------------------------------------------------------------------------
void Keybinder::ExecuteAction(const Keybinding& kb) {
    if (kb.action == "SWITCH") SwitchToDesktop(kb.arg);
    else if (kb.action == "MOVE") MoveCurrentWindowToDesktop(kb.arg);
    else if (kb.action == "MOVE_SWITCH") {
        if (!m_windowAndDesktopSwitch) return;
        RefreshDesktopList();
        int origDesktop = m_currentDesktop;
        int targetIdx = kb.arg - 1;

        while (targetIdx >= (int)m_desktops.size()) {
            if (!m_newDesktopCreation) break;
            SimulateKeyCombo(VK_LWIN, VK_LCONTROL, 'D');
            Sleep(100);
            RefreshDesktopList();
        }

        if (m_currentDesktop != origDesktop && origDesktop >= 0) {
            int backDiff = origDesktop - m_currentDesktop;
            WORD backKey = (backDiff > 0) ? VK_RIGHT : VK_LEFT;
            int backPresses = backDiff > 0 ? backDiff : -backDiff;
            if (backPresses > 20) backPresses = 20;
            for (int i = 0; i < backPresses; i++)
                SimulateKeyCombo(VK_LWIN, VK_LCONTROL, backKey);
            Sleep(100);
            RefreshDesktopList();
        }

        MoveCurrentWindowToDesktop(kb.arg);
        Sleep(100);
        SwitchToDesktop(kb.arg);
    }
}

// ---------------------------------------------------------------------------
// Config parsing helpers
// ---------------------------------------------------------------------------
static DWORD ParseModifier(const std::string& s) {
    std::string l = s;
    std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    if (l == "win") return MODB_WIN;
    if (l == "ctrl") return MODB_CTRL;
    if (l == "alt") return MODB_ALT;
    if (l == "shift") return MODB_SHIFT;
    return 0;
}

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

DWORD Keybinder::KeyNameToVK(const std::string& name) {
    std::string u = name;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);

    if (u.length() == 1) {
        char c = u[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (DWORD)c;
    }

    struct { const char* n; DWORD v; } km[] = {
        {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4},
        {"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8},
        {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
        {"F13", VK_F13}, {"F14", VK_F14}, {"F15", VK_F15}, {"F16", VK_F16},
        {"F17", VK_F17}, {"F18", VK_F18}, {"F19", VK_F19}, {"F20", VK_F20},
        {"F21", VK_F21}, {"F22", VK_F22}, {"F23", VK_F23}, {"F24", VK_F24},
        {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"UP", VK_UP}, {"DOWN", VK_DOWN},
        {"SPACE", VK_SPACE}, {"RETURN", VK_RETURN}, {"ENTER", VK_RETURN},
        {"TAB", VK_TAB}, {"ESC", VK_ESCAPE}, {"ESCAPE", VK_ESCAPE},
        {"BACK", VK_BACK}, {"DELETE", VK_DELETE}, {"DEL", VK_DELETE},
        {"INSERT", VK_INSERT}, {"INS", VK_INSERT}, {"HOME", VK_HOME},
        {"END", VK_END}, {"PGUP", VK_PRIOR}, {"PGDN", VK_NEXT},
        {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
        {"MINUS", VK_OEM_MINUS}, {"EQUAL", VK_OEM_PLUS},
        {"LBRACKET", VK_OEM_4}, {"RBRACKET", VK_OEM_6},
        {"SEMICOLON", VK_OEM_1}, {"QUOTE", VK_OEM_7},
        {"COMMA", VK_OEM_COMMA}, {"PERIOD", VK_OEM_PERIOD},
        {"SLASH", VK_OEM_2}, {"BACKSLASH", VK_OEM_5},
        {"TILDE", VK_OEM_3},
    };

    for (size_t i = 0; i < sizeof(km)/sizeof(km[0]); i++)
        if (u == km[i].n) return km[i].v;
    return 0;
}

// ---------------------------------------------------------------------------
// IsSystemShortcut — returns true if the given modifier+key combo is a
// well-known Windows system shortcut that should not be overridden unless
// Allow_Bad_Keys is explicitly set.
// WIN+1..9 / WIN+SHIFT+1..9 / WIN+CTRL+1..9 / WIN+ALT+1..9 are always
// exempted from this blocklist.
// ---------------------------------------------------------------------------
static bool IsSystemShortcut(DWORD mods, DWORD vk) {
    struct { DWORD mods; DWORD vk; } bad[] = {
        {MODB_CTRL, 'C'}, {MODB_CTRL, 'X'}, {MODB_CTRL, 'V'},
        {MODB_CTRL, 'Z'}, {MODB_CTRL, 'Y'}, {MODB_CTRL, 'A'},
        {MODB_CTRL, 'S'}, {MODB_CTRL, 'O'}, {MODB_CTRL, 'P'},
        {MODB_CTRL, 'F'}, {MODB_CTRL, 'H'}, {MODB_CTRL, 'N'},
        {MODB_CTRL, 'W'}, {MODB_CTRL, 'T'},
        {MODB_ALT, VK_F4}, {MODB_ALT, VK_TAB}, {MODB_ALT, VK_ESCAPE},
        {MODB_ALT, VK_SPACE}, {MODB_ALT, VK_RETURN},
        {MODB_WIN, 'D'}, {MODB_WIN, 'E'}, {MODB_WIN, 'I'},
        {MODB_WIN, 'L'}, {MODB_WIN, 'R'}, {MODB_WIN, 'V'},
        {MODB_WIN, VK_TAB}, {MODB_WIN, VK_SPACE}, {MODB_WIN, VK_PAUSE},
        {MODB_WIN, 'A'}, {MODB_WIN, 'B'}, {MODB_WIN, 'G'},
        {MODB_WIN, 'H'}, {MODB_WIN, 'K'}, {MODB_WIN, 'M'},
        {MODB_WIN, 'O'}, {MODB_WIN, 'P'}, {MODB_WIN, 'S'},
        {MODB_WIN, 'T'}, {MODB_WIN, 'U'}, {MODB_WIN, 'W'},
        {MODB_WIN, 'X'}, {MODB_WIN, 'Z'},
        {MODB_WIN, VK_LEFT}, {MODB_WIN, VK_RIGHT},
        {MODB_WIN, VK_UP}, {MODB_WIN, VK_DOWN},
        {MODB_WIN | MODB_SHIFT, 'S'},
        {MODB_CTRL | MODB_ALT, VK_DELETE},
        {MODB_CTRL | MODB_SHIFT, VK_ESCAPE},
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
        if (bad[i].mods == mods && bad[i].vk == vk) return true;
    return false;
}

// ---------------------------------------------------------------------------
// LoadConfig — reads winbind.conf, parses settings and key bindings.
// Settings lines (Virtual_Desktop_Switch, New_Desktop_Creation, etc.) are
// handled first and removed from the token stream via 'continue'.
// Remaining lines are parsed as MODIFIERS+KEY = ACTION ARG bindings.
// ---------------------------------------------------------------------------
void Keybinder::LoadConfig(const std::string& path) {
    m_configPath = path;
    m_bindings.clear();
    m_enabled = true;
    m_newDesktopCreation = false;
    m_allowBadKeys = false;
    m_windowAndDesktopSwitch = true;

    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string lhs = Trim(line.substr(0, eqPos));
        std::string rhs = Trim(line.substr(eqPos + 1));
        if (lhs.empty() || rhs.empty()) continue;

        if (lhs == "Virtual_Desktop_Switch") {
            std::string val = rhs;
            std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            m_enabled = (val == "enabled" || val == "1" || val == "true");
            continue;
        }
        if (lhs == "New_Desktop_Creation") {
            std::string val = rhs;
            std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            m_newDesktopCreation = (val == "enabled" || val == "1" || val == "true");
            continue;
        }
        if (lhs == "Allow_Bad_Keys") {
            std::string val = rhs;
            std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            m_allowBadKeys = (val == "enabled" || val == "1" || val == "true");
            continue;
        }
        if (lhs == "Window_And_Virtual_Desktop_Switch") {
            std::string val = rhs;
            std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            m_windowAndDesktopSwitch = (val == "enabled" || val == "1" || val == "true");
            continue;
        }

        // ----- Key binding line -----
        std::istringstream rss(rhs);
        std::string actionName;
        int actionArg = 0;
        rss >> actionName;
        std::string argStr;
        if (rss >> argStr) actionArg = std::atoi(argStr.c_str());
        std::transform(actionName.begin(), actionName.end(), actionName.begin(), ::toupper);

        DWORD modifiers = 0;
        std::string keyName;
        size_t sp = 0, pp;
        while ((pp = lhs.find('+', sp)) != std::string::npos) {
            std::string p = Trim(lhs.substr(sp, pp - sp));
            DWORD m = ParseModifier(p);
            if (m) modifiers |= m; else keyName = p;
            sp = pp + 1;
        }
        std::string lp = Trim(lhs.substr(sp));
        DWORD m = ParseModifier(lp);
        if (m) modifiers |= m; else keyName = lp;

        DWORD vk = KeyNameToVK(keyName);
        if (vk == 0) continue;

        if (!m_allowBadKeys && IsSystemShortcut(modifiers, vk)) continue;
        if (!m_windowAndDesktopSwitch && actionName == "MOVE_SWITCH") continue;

        Keybinding kb;
        kb.modifiers = modifiers;
        kb.vkCode = vk;
        kb.action = actionName;
        kb.arg = actionArg;
        m_bindings.push_back(kb);
    }
}
