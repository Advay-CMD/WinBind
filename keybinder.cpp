// WinBind — keybinder logic
// Low-level keyboard hook, registry-based virtual desktop enumeration,
// SendInput-based switching, and flicker-free empty-desktop deletion
// via the undocumented COM Virtual Desktop Manager service.


#include <windows.h>
#include <winreg.h>
#include <shlobj.h>
#include <objbase.h>
#include <dwmapi.h>
#include <shlguid.h>
#include "keybinder.h"
#include "Config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <signal.h>
#include <setjmp.h>
#include <shellapi.h>
#include <map>

// ===========================================================================
// COM GUIDs for the undocumented IVirtualDesktopManagerInternal
// ===========================================================================

static const GUID GUID_CLSID_ShellWindows =
    {0x9BA05972, 0xF6A8, 0x11CF, {0xA4, 0x42, 0x00, 0xA0, 0xC9, 0x0A, 0x8F, 0x39}};
static const GUID GUID_CLSID_ImmersiveShell =
    {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
static const GUID GUID_IID_IServiceProvider =
    {0x6D5140C1, 0x7436, 0x11CE, {0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA}};
static const GUID GUID_SVC_VDMgr =
    {0xC5E0CDCA, 0x7B6E, 0x41B2, {0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B}};

// Different IVirtualDesktopManagerInternal IIDs per Windows build.
static const GUID GUID_IID_VDMgr_26100 =    // Win11 24H2+
    {0x53F5CA0B, 0x158F, 0x4124, {0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27}};
static const GUID GUID_IID_VDMgr_22621 =    // Win11 23H2
    {0xA3175F2D, 0x239C, 0x4BD2, {0x8A, 0xA0, 0xEE, 0xBA, 0x8B, 0x0B, 0x13, 0x8E}};
static const GUID GUID_IID_VDMgr_22000 =    // Win11 21H2
    {0xB2F925B9, 0x5A0F, 0x4D2E, {0x9F, 0x4D, 0x2B, 0x15, 0x07, 0x59, 0x3C, 0x10}};
static const GUID GUID_IID_VDMgr_25179 =    // pre-24H2 dev
    {0x6B1CB1E5, 0xF8F9, 0x4D7B, {0x9F, 0x7E, 0x5B, 0xE0, 0x8F, 0xC6, 0xF2, 0xF4}};
static const GUID GUID_IID_VDMgr_OLD =      // pre-Win11
    {0xF31574D6, 0xB682, 0x4CDC, {0xBD, 0x56, 0x18, 0x27, 0x86, 0x0A, 0xBE, 0xC6}};

// IApplicationViewCollection — try older IID first, then newer.
static const GUID GUID_IID_AVC_OLD =
    {0x1841C6D2, 0x6F9D, 0x42CA, {0xA6, 0x2D, 0x0C, 0x5F, 0x3A, 0x8D, 0x8C, 0x77}};
static const GUID GUID_IID_AVC_NEW =
    {0x1841C6D7, 0x4F9D, 0x42C0, {0xAF, 0x41, 0x87, 0x47, 0x53, 0x8F, 0x10, 0xE5}};

// VTable for the public IVirtualDesktopManager (used via CoCreateInstance).
struct IVirtualDesktopManagerVT {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG   (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *IsWindowOnCurrentVirtualDesktop)(void*, HWND, BOOL*);
    HRESULT (STDMETHODCALLTYPE *GetWindowDesktopId)(void*, HWND, GUID*);
    HRESULT (STDMETHODCALLTYPE *MoveWindowToDesktop)(void*, HWND, REFGUID);
};

struct MgrIidPair { const GUID& svc; const GUID& iid; };

// ===========================================================================
// SIGSEGV guard — some COM vtable indices differ across Windows builds
// ===========================================================================

static jmp_buf g_comJmpBuf;

extern "C" void __cdecl SigSegvHandler(int) {
    longjmp(g_comJmpBuf, 1);
}

// ===========================================================================
// COM service helpers — undocumented IVirtualDesktopManagerInternal
// ===========================================================================

static IServiceProvider* GetShellServiceProvider() {
    IServiceProvider* pSP = NULL;
    if (SUCCEEDED(CoCreateInstance(GUID_CLSID_ImmersiveShell, NULL,
        CLSCTX_LOCAL_SERVER, GUID_IID_IServiceProvider, (void**)&pSP)) && pSP)
        return pSP;

    IShellWindows* pSW = NULL;
    if (FAILED(CoCreateInstance(GUID_CLSID_ShellWindows, NULL, CLSCTX_ALL,
        IID_IShellWindows, (void**)&pSW)) || !pSW) return NULL;

    VARIANT v = {}; V_VT(&v) = VT_I4; V_I4(&v) = 0;
    IDispatch* pDisp = NULL;
    pSW->Item(v, &pDisp);
    pSW->Release();
    if (!pDisp) return NULL;

    pDisp->QueryInterface(GUID_IID_IServiceProvider, (void**)&pSP);
    pDisp->Release();
    return pSP;
}

// Moves the window identified by hWnd to targetGuid via the undocumented COM
// service.  Returns true on success.  SIGSEGV-protected.

static bool TryMoveWindowViaService(HWND hWnd, const GUID& targetGuid) {
    void (*oldHandler)(int) = signal(SIGSEGV, SigSegvHandler);
    bool success = false;

    if (setjmp(g_comJmpBuf) == 0) {
        IServiceProvider* pSP = GetShellServiceProvider();
        if (!pSP) { signal(SIGSEGV, oldHandler); return false; }

        MgrIidPair pairs[] = {
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_26100},
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_22621},
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_22000},
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_25179},
            {GUID_SVC_VDMgr, GUID_IID_VDMgr_OLD},
        };
        void* pMgr = NULL;
        for (int i = 0; i < 5; i++)
            if (SUCCEEDED(pSP->QueryService(pairs[i].svc, pairs[i].iid, &pMgr)) && pMgr)
                break;
        if (!pMgr) { pSP->Release(); signal(SIGSEGV, oldHandler); return false; }

        // Get IApplicationView for hWnd.
        void* pVC = NULL;
        HRESULT hr = pSP->QueryService(GUID_IID_AVC_OLD, GUID_IID_AVC_OLD, &pVC);
        if (FAILED(hr) || !pVC)
            hr = pSP->QueryService(GUID_IID_AVC_NEW, GUID_IID_AVC_NEW, &pVC);
        if (FAILED(hr) || !pVC) {
            ((IUnknown*)pMgr)->Release(); pSP->Release();
            signal(SIGSEGV, oldHandler); return false;
        }

        void** vcvt = *(void***)pVC;
        typedef HRESULT (STDMETHODCALLTYPE *GetViewFn)(void*, HWND, void**);
        GetViewFn getView = (GetViewFn)vcvt[6];
        void* pView = NULL;
        if (SUCCEEDED(getView(pVC, hWnd, &pView)) && pView) {
            // FindDesktop at [14], MoveViewToDesktop at [4]
            void** vt = *(void***)pMgr;
            typedef HRESULT (STDMETHODCALLTYPE *FindDeskFn)(void*, GUID*, void**);
            FindDeskFn findDesk = (FindDeskFn)vt[14];
            void* pTarget = NULL;
            GUID g = targetGuid;
            if (SUCCEEDED(findDesk(pMgr, &g, &pTarget)) && pTarget) {
                typedef HRESULT (STDMETHODCALLTYPE *MoveViewFn)(void*, void*, void*);
                if (SUCCEEDED(((MoveViewFn)vt[4])(pMgr, pView, pTarget)))
                    success = true;
                ((IUnknown*)pTarget)->Release();
            }
            ((IUnknown*)pView)->Release();
        }
        ((IUnknown*)pVC)->Release();
        ((IUnknown*)pMgr)->Release();
        pSP->Release();
    }

    signal(SIGSEGV, oldHandler);
    return success;
}

// ===========================================================================
// Config parsing helpers
// ===========================================================================

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static DWORD ParseModifier(const std::string& s) {
    std::string l = s;
    std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    if (l == "win") return MODB_WIN;
    if (l == "ctrl") return MODB_CTRL;
    if (l == "alt") return MODB_ALT;
    if (l == "shift") return MODB_SHIFT;
    return 0;
}

DWORD Keybinder::KeyNameToVK(const std::string& name) {
    std::string u = name;
    std::transform(u.begin(), u.end(), u.begin(), ::toupper);
    if (u.size() == 1) {
        char c = u[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (DWORD)c;
    }
    struct { const char* n; DWORD v; } km[] = {
        {"F1",VK_F1},{"F2",VK_F2},{"F3",VK_F3},{"F4",VK_F4},
        {"F5",VK_F5},{"F6",VK_F6},{"F7",VK_F7},{"F8",VK_F8},
        {"F9",VK_F9},{"F10",VK_F10},{"F11",VK_F11},{"F12",VK_F12},
        {"F13",VK_F13},{"F14",VK_F14},{"F15",VK_F15},{"F16",VK_F16},
        {"F17",VK_F17},{"F18",VK_F18},{"F19",VK_F19},{"F20",VK_F20},
        {"F21",VK_F21},{"F22",VK_F22},{"F23",VK_F23},{"F24",VK_F24},
        {"LEFT",VK_LEFT},{"RIGHT",VK_RIGHT},{"UP",VK_UP},{"DOWN",VK_DOWN},
        {"SPACE",VK_SPACE},{"RETURN",VK_RETURN},{"ENTER",VK_RETURN},
        {"TAB",VK_TAB},{"ESC",VK_ESCAPE},{"ESCAPE",VK_ESCAPE},
        {"BACK",VK_BACK},{"DELETE",VK_DELETE},{"DEL",VK_DELETE},
        {"INSERT",VK_INSERT},{"INS",VK_INSERT},{"HOME",VK_HOME},
        {"END",VK_END},{"PGUP",VK_PRIOR},{"PGDN",VK_NEXT},
        {"PAGEUP",VK_PRIOR},{"PAGEDOWN",VK_NEXT},
        {"MINUS",VK_OEM_MINUS},{"EQUAL",VK_OEM_PLUS},
        {"LBRACKET",VK_OEM_4},{"RBRACKET",VK_OEM_6},
        {"SEMICOLON",VK_OEM_1},{"QUOTE",VK_OEM_7},
        {"COMMA",VK_OEM_COMMA},{"PERIOD",VK_OEM_PERIOD},
        {"SLASH",VK_OEM_2},{"BACKSLASH",VK_OEM_5},
        {"TILDE",VK_OEM_3},
    };
    for (size_t i = 0; i < sizeof(km)/sizeof(km[0]); i++)
        if (u == km[i].n) return km[i].v;
    return 0;
}

static bool IsSystemShortcut(DWORD mods, DWORD vk) {
    struct { DWORD m; DWORD v; } bad[] = {
        {MODB_CTRL,'C'},{MODB_CTRL,'X'},{MODB_CTRL,'V'},
        {MODB_CTRL,'Z'},{MODB_CTRL,'Y'},{MODB_CTRL,'A'},
        {MODB_CTRL,'S'},{MODB_CTRL,'O'},{MODB_CTRL,'P'},
        {MODB_CTRL,'F'},{MODB_CTRL,'H'},{MODB_CTRL,'N'},
        {MODB_CTRL,'W'},{MODB_CTRL,'T'},
        {MODB_ALT,VK_F4},{MODB_ALT,VK_TAB},{MODB_ALT,VK_ESCAPE},
        {MODB_ALT,VK_SPACE},{MODB_ALT,VK_RETURN},
        {MODB_WIN,'D'},{MODB_WIN,'E'},{MODB_WIN,'I'},
        {MODB_WIN,'L'},{MODB_WIN,'R'},{MODB_WIN,'V'},
        {MODB_WIN,VK_TAB},{MODB_WIN,VK_SPACE},{MODB_WIN,VK_PAUSE},
        {MODB_WIN,'A'},{MODB_WIN,'B'},{MODB_WIN,'G'},
        {MODB_WIN,'H'},{MODB_WIN,'K'},{MODB_WIN,'M'},
        {MODB_WIN,'O'},{MODB_WIN,'P'},{MODB_WIN,'S'},
        {MODB_WIN,'T'},{MODB_WIN,'U'},{MODB_WIN,'W'},
        {MODB_WIN,'X'},{MODB_WIN,'Z'},
        {MODB_WIN,VK_LEFT},{MODB_WIN,VK_RIGHT},
        {MODB_WIN,VK_UP},{MODB_WIN,VK_DOWN},
        {MODB_WIN|MODB_SHIFT,'S'},
        {MODB_CTRL|MODB_ALT,VK_DELETE},
        {MODB_CTRL|MODB_SHIFT,VK_ESCAPE},
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
        if (bad[i].m == mods && bad[i].v == vk) return true;
    return false;
}

// ===========================================================================
// Keybinder — singleton, lifecycle, config reload, desktop enumeration
// ===========================================================================

Keybinder& Keybinder::Instance() {
    static Keybinder inst;
    return inst;
}

Keybinder::Keybinder()
    : m_hHook(NULL), m_hWnd(NULL), m_currentDesktop(-1), m_enabled(true),
      m_newDesktopCreation(false), m_allowBadKeys(false),
      m_windowAndDesktopSwitch(true), m_autoRemoveDesktop(true),
      m_busy(false), m_simulating(false), m_modsTracked(0) {
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
    if (m_hHook) { UnhookWindowsHookEx(m_hHook); m_hHook = NULL; }
    CoUninitialize();
}

bool Keybinder::CheckConfigChanged() {
    HANDLE h = CreateFileA(m_configPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    FILETIME ft;
    if (!GetFileTime(h, NULL, NULL, &ft)) { CloseHandle(h); return false; }
    CloseHandle(h);
    if (CompareFileTime(&ft, &m_configTime) == 0) return false;
    m_configTime = ft;
    LoadConfig(m_configPath);
    return true;
}

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
        RegCloseKey(hKey); return false;
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
        GUID cur;
        memcpy(&cur, buf, 16);
        for (size_t i = 0; i < m_desktops.size(); i++)
            if (memcmp(&m_desktops[i].id, &cur, 16) == 0) { m_currentDesktop = (int)i; break; }
    }

    RegCloseKey(hKey);
    return !m_desktops.empty();
}

// ===========================================================================
// Desktop switching — via SendInput (WIN+CTRL+LEFT / WIN+CTRL+RIGHT)
// ===========================================================================

bool Keybinder::IsModTracked(WORD vk) {
    switch (vk) {
        case VK_LWIN: case VK_RWIN:   return (m_modsTracked & MODB_WIN)  != 0;
        case VK_LCONTROL: case VK_RCONTROL: return (m_modsTracked & MODB_CTRL) != 0;
        case VK_LMENU: case VK_RMENU: return (m_modsTracked & MODB_ALT)  != 0;
        case VK_LSHIFT: case VK_RSHIFT: return (m_modsTracked & MODB_SHIFT) != 0;
        default: return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}

void Keybinder::SimulateKeyCombo(WORD mod1, WORD mod2, WORD key) {
    INPUT inputs[8] = {};
    int idx = 0;
    bool held1 = mod1 && IsModTracked(mod1);
    bool held2 = mod2 && IsModTracked(mod2);

    if (!held1 && mod1) { inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = mod1; idx++; }
    if (!held2 && mod2) { inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = mod2; idx++; }
    inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = key; idx++;
    inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = key; inputs[idx].ki.dwFlags = KEYEVENTF_KEYUP; idx++;
    if (!held2 && mod2) { inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = mod2; inputs[idx].ki.dwFlags = KEYEVENTF_KEYUP; idx++; }
    if (!held1 && mod1) { inputs[idx].type = INPUT_KEYBOARD; inputs[idx].ki.wVk = mod1; inputs[idx].ki.dwFlags = KEYEVENTF_KEYUP; idx++; }
    if (idx) SendInput(idx, inputs, sizeof(INPUT));
    Sleep(50);
}

void Keybinder::SimulateKeyCombo(DWORD mods, DWORD vk) {
    INPUT inputs[6] = {};
    int idx = 0;

    auto add = [&](WORD vk, bool up) {
        inputs[idx].type = INPUT_KEYBOARD;
        inputs[idx].ki.wVk = vk;
        if (up) inputs[idx].ki.dwFlags = KEYEVENTF_KEYUP;
        idx++;
    };

    if (mods & MODB_WIN)   add(VK_LWIN, false);
    if (mods & MODB_CTRL)  add(VK_LCONTROL, false);
    if (mods & MODB_ALT)   add(VK_LMENU, false);
    if (mods & MODB_SHIFT) add(VK_LSHIFT, false);

    add((WORD)vk, false);
    add((WORD)vk, true);

    if (mods & MODB_SHIFT) add(VK_LSHIFT, true);
    if (mods & MODB_ALT)   add(VK_LMENU, true);
    if (mods & MODB_CTRL)  add(VK_LCONTROL, true);
    if (mods & MODB_WIN)   add(VK_LWIN, true);

    if (!idx) return;

    m_simulating = true;
    SendInput(idx, inputs, sizeof(INPUT));

    // Pump messages to flush injected events through the hook while
    // m_simulating prevents them from re-triggering this binding.
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

void Keybinder::SwitchToDesktop(int num) {
    if (!m_enabled || m_desktops.empty()) return;
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
    int presses = std::min(abs(diff), 20);
    for (int i = 0; i < presses; i++)
        SimulateKeyCombo(VK_LWIN, VK_LCONTROL, dirKey);
    RefreshDesktopList();
}

// ===========================================================================
// Flicker-free empty-desktop detection via public IVirtualDesktopManager
// ===========================================================================

static struct {
    IVirtualDesktopManager* mgr;
    GUID* guids;
    int count;
    bool* hasWin;
} g_desktopCheck;

// Skips system-internal windows that shouldn't count as "user windows."
static bool IsSystemWindowClass(HWND hWnd) {
    char cls[128];
    if (!GetClassNameA(hWnd, cls, sizeof(cls))) return false;
    static const char* sys[] = {
        "Progman","WorkerW","Shell_TrayWnd","Shell_SecondaryTrayWnd",
        "Windows.UI.Composition.DesktopWindowContentBridge",
        "ApplicationManager_DesktopShellWindow",
        "DV2ControlHost","MultitaskingViewFrame","NotifyIconOverflowWindow",
    };
    for (size_t i = 0; i < sizeof(sys)/sizeof(sys[0]); i++)
        if (strcmp(cls, sys[i]) == 0) return true;
    return false;
}

static BOOL CALLBACK EnumDesktopCheckProc(HWND hWnd, LPARAM) {
    if (!IsWindowVisible(hWnd)) return TRUE;
    if (IsSystemWindowClass(hWnd)) return TRUE;
    if (GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;

    IVirtualDesktopManagerVT* vt = *(IVirtualDesktopManagerVT**)g_desktopCheck.mgr;
    GUID winDesk;
    if (FAILED(vt->GetWindowDesktopId(g_desktopCheck.mgr, hWnd, &winDesk))) return TRUE;

    for (int i = 0; i < g_desktopCheck.count; i++)
        if (memcmp(&g_desktopCheck.guids[i], &winDesk, sizeof(GUID)) == 0)
            { g_desktopCheck.hasWin[i] = true; break; }
    return TRUE;
}

// ===========================================================================
// Move window to another desktop — COM service, then public API, then fallback
// ===========================================================================

void Keybinder::MoveCurrentWindowToDesktop(int num) {
    if (!m_enabled) return;
    RefreshDesktopList();
    if (m_desktops.empty()) return;

    int targetIdx = num - 1;
    if (targetIdx < 0 || targetIdx >= (int)m_desktops.size()) return;
    if (m_currentDesktop == targetIdx) return;

    HWND hWnd = GetForegroundWindow();
    if (!hWnd) return;

    // Attempt 1 — undocumented COM service
    if (TryMoveWindowViaService(hWnd, m_desktops[targetIdx].id)) { Sleep(100); return; }

    // Attempt 2 — public IVirtualDesktopManager::MoveWindowToDesktop
    CLSID clsid; IID iid;
    if (SUCCEEDED(CLSIDFromString((LPOLESTR)L"{AA509086-5CA9-4C25-8F95-589D3C07B48A}", &clsid))
        && SUCCEEDED(CLSIDFromString((LPOLESTR)L"{A5CD92FF-29BE-454C-8D04-D82879FB3F1B}", &iid))) {
        void* pObj = NULL;
        if (SUCCEEDED(CoCreateInstance(clsid, NULL, CLSCTX_ALL, iid, &pObj)) && pObj) {
            IVirtualDesktopManagerVT* vt = *(IVirtualDesktopManagerVT**)pObj;
            if (SUCCEEDED(vt->MoveWindowToDesktop(pObj, hWnd, m_desktops[targetIdx].id)))
                { ((IUnknown*)pObj)->Release(); Sleep(100); return; }
            ((IUnknown*)pObj)->Release();
        }
    }

    // Attempt 3 — SendInput WIN+SHIFT+LEFT/RIGHT (system's move-window shortcut)
    int diff = targetIdx - m_currentDesktop;
    WORD dirKey = (diff > 0) ? VK_RIGHT : VK_LEFT;
    int presses = std::min(abs(diff), 20);
    for (int i = 0; i < presses; i++)
        SimulateKeyCombo(VK_LWIN, VK_LSHIFT, dirKey);
    Sleep(100);
}

// ===========================================================================
// Remove empty desktops — flicker-free detection + COM deletion
// ===========================================================================

void Keybinder::RemoveEmptyDesktops(int sourceDesktop) {
    if (!m_enabled || !m_autoRemoveDesktop) return;
    RefreshDesktopList();
    if (m_desktops.size() <= 1 || m_currentDesktop < 0) return;

    GUID* guids = (GUID*)malloc(m_desktops.size() * sizeof(GUID));
    bool* hasWin = (bool*)calloc(m_desktops.size(), 1);
    if (!guids || !hasWin) { free(guids); free(hasWin); return; }
    for (size_t i = 0; i < m_desktops.size(); i++)
        guids[i] = m_desktops[i].id;

    // Flicker-free: query every window's desktop via COM (no switching).
    {
        CLSID clsid; IID iid;
        if (FAILED(CLSIDFromString((LPOLESTR)L"{AA509086-5CA9-4C25-8F95-589D3C07B48A}", &clsid))
            || FAILED(CLSIDFromString((LPOLESTR)L"{A5CD92FF-29BE-454C-8D04-D82879FB3F1B}", &iid)))
            { free(guids); free(hasWin); return; }
        IVirtualDesktopManager* mgr = NULL;
        if (FAILED(CoCreateInstance(clsid, NULL, CLSCTX_ALL, iid, (void**)&mgr)) || !mgr)
            { free(guids); free(hasWin); return; }
        g_desktopCheck.mgr = mgr;
        g_desktopCheck.guids = guids;
        g_desktopCheck.count = (int)m_desktops.size();
        g_desktopCheck.hasWin = hasWin;
        EnumWindows(EnumDesktopCheckProc, 0);
        ((IVirtualDesktopManagerVT*)*(void**)mgr)->Release(mgr);
    }

    // Determine iteration range: sourceDesktop >= 0 → single; < 0 → all.
    int start = (sourceDesktop >= 0) ? sourceDesktop : (int)m_desktops.size() - 1;
    int end   = (sourceDesktop >= 0) ? sourceDesktop : 0;
    int dir   = (sourceDesktop >= 0) ? 1 : -1;

    for (int i = start; (sourceDesktop >= 0) ? (i <= end) : (i >= end); i += dir) {
        if (i < 0 || i >= (int)m_desktops.size() || i == m_currentDesktop || hasWin[i])
            continue;

        void (*oldHandler)(int) = signal(SIGSEGV, SigSegvHandler);
        if (setjmp(g_comJmpBuf) == 0) {
            IServiceProvider* pSP = GetShellServiceProvider();
            if (!pSP) { signal(SIGSEGV, oldHandler); continue; }

            void* pMgr = NULL;
            MgrIidPair pairs[] = {
                {GUID_SVC_VDMgr, GUID_IID_VDMgr_26100},
                {GUID_SVC_VDMgr, GUID_IID_VDMgr_22621},
                {GUID_SVC_VDMgr, GUID_IID_VDMgr_22000},
                {GUID_SVC_VDMgr, GUID_IID_VDMgr_25179},
                {GUID_SVC_VDMgr, GUID_IID_VDMgr_OLD},
            };
            for (int pi = 0; pi < 5; pi++)
                if (SUCCEEDED(pSP->QueryService(pairs[pi].svc, pairs[pi].iid, &pMgr)) && pMgr)
                    break;

            if (pMgr) {
                void** vt = *(void***)pMgr;

                // FindDesktop [14]
                typedef HRESULT (STDMETHODCALLTYPE *FindDeskFn)(void*, GUID*, void**);
                void* pDesk = NULL;
                if (SUCCEEDED(((FindDeskFn)vt[14])(pMgr, &guids[i], &pDesk)) && pDesk) {
                    // GetAdjacentDesktop [8], direction 3=left 4=right
                    typedef HRESULT (STDMETHODCALLTYPE *AdjDeskFn)(void*, void*, int, void**);
                    void* pFallback = NULL;
                    if (FAILED(((AdjDeskFn)vt[8])(pMgr, pDesk, 3, &pFallback)) || !pFallback)
                        ((AdjDeskFn)vt[8])(pMgr, pDesk, 4, &pFallback);

                    if (pFallback) {
                        // RemoveDesktop [13]
                        typedef HRESULT (STDMETHODCALLTYPE *RmDeskFn)(void*, void*, void*);
                        if (SUCCEEDED(((RmDeskFn)vt[13])(pMgr, pDesk, pFallback)))
                            RefreshDesktopList();
                        ((IUnknown*)pFallback)->Release();
                    }
                    ((IUnknown*)pDesk)->Release();
                }
                ((IUnknown*)pMgr)->Release();
            }
            pSP->Release();
        }
        signal(SIGSEGV, oldHandler);
    }
    free(guids);
    free(hasWin);
}

// ===========================================================================
// Low-level keyboard hook — modifier tracking, binding dispatch
// ===========================================================================

LRESULT CALLBACK Keybinder::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && Instance().HandleKeyEvent(wParam, lParam) == 1)
        return 1;
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT Keybinder::HandleKeyEvent(WPARAM wParam, LPARAM lParam) {
    KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
    DWORD vk = pKb->vkCode;
    bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

    // Track modifier keys from hook events (more reliable than GetAsyncKeyState
    // on modern Windows builds, which can miss concurrent modifier state).
    DWORD mb = 0;
    switch (vk) {
        case VK_LWIN: case VK_RWIN:   mb = MODB_WIN;   break;
        case VK_LCONTROL: case VK_RCONTROL: mb = MODB_CTRL; break;
        case VK_LMENU: case VK_RMENU: mb = MODB_ALT;   break;
        case VK_LSHIFT: case VK_RSHIFT: mb = MODB_SHIFT; break;
    }
    if (mb) { m_modsTracked = down ? (m_modsTracked | mb) : (m_modsTracked & ~mb); return 0; }
    if (!down) return 0;

    // During an action, eat real keys to prevent interference with SendInput.
    // Check both LLKHF_INJECTED (0x10) and LLKHF_LOWERIL_INJECTED (0x02)
    // because SendInput from non-elevated processes may set either flag.
    if (m_busy) return (pKb->flags & (LLKHF_INJECTED | 0x02)) ? 0 : 1;

    // While simulating, skip binding matching to prevent re-trigger loops
    // from injected keys arriving after m_busy is cleared.
    if (m_simulating) return 0;

    CheckConfigChanged();
    if (!m_enabled) return 0;

    // Match against configured bindings using tracked modifier state.
    for (size_t i = 0; i < m_bindings.size(); i++)
        if (m_bindings[i].vkCode == vk && m_bindings[i].modifiers == m_modsTracked) {
            if (m_hWnd) PostMessage(m_hWnd, WM_KB_EXECUTE, i, 0);
            return 1;
        }
    return 0;
}

// ===========================================================================
// Action execution — wraps ExecuteAction with m_busy guard
// ===========================================================================

void Keybinder::ExecuteQueuedAction(int bindingIndex) {
    if (m_busy) return;
    if (bindingIndex < 0 || bindingIndex >= (int)m_bindings.size()) return;
    m_busy = true;
    ExecuteAction(m_bindings[bindingIndex]);
    m_busy = false;
}

void Keybinder::ExecuteAction(const Keybinding& kb) {
    if (kb.action == "SWITCH") {
        int src = m_currentDesktop;
        SwitchToDesktop(kb.arg);
        RemoveEmptyDesktops(src);
    } else if (kb.action == "MOVE") {
        MoveCurrentWindowToDesktop(kb.arg);
    } else if (kb.action == "MOVE_SWITCH") {
        if (!m_windowAndDesktopSwitch) return;
        RefreshDesktopList();
        int src = m_currentDesktop;
        int tgt = kb.arg - 1;

        while (tgt >= (int)m_desktops.size()) {
            if (!m_newDesktopCreation) break;
            SimulateKeyCombo(VK_LWIN, VK_LCONTROL, 'D');
            Sleep(100); RefreshDesktopList();
        }
        if (m_currentDesktop != src && src >= 0) {
            int d = src - m_currentDesktop;
            WORD k = (d > 0) ? VK_RIGHT : VK_LEFT;
            int n = std::min(abs(d), 20);
            for (int i = 0; i < n; i++) SimulateKeyCombo(VK_LWIN, VK_LCONTROL, k);
            Sleep(100); RefreshDesktopList();
        }
        MoveCurrentWindowToDesktop(kb.arg);
        Sleep(100);
        SwitchToDesktop(kb.arg);
        RemoveEmptyDesktops(src);
    } else if (kb.action == "REMOVE_EMPTY") {
        RemoveEmptyDesktops();
    } else if (kb.action == "SIMULATE_KEY") {
        SimulateKeyCombo(kb.targetMods, kb.targetVk);
    } else if (kb.action == "RUN") {
        ShellExecuteA(NULL, "open", kb.runApp.c_str(),
            kb.runArgs.empty() ? NULL : kb.runArgs.c_str(),
            NULL, SW_SHOWNORMAL);
    } else if (kb.action == "RELOAD_CONFIG") {
        std::string p = kb.confLoadPath.empty() ? g_ConfigPath : kb.confLoadPath;
        LoadConf[p];
        m_configPath = p;
        g_ConfigPath = p;
        LoadConfig(p);
    }
}

// ===========================================================================
// Config loading
// ===========================================================================

void Keybinder::LoadConfig(const std::string& path) {
    m_configPath = path;
    m_bindings.clear();
    m_enabled = true;
    m_newDesktopCreation = false;
    m_allowBadKeys = false;
    m_windowAndDesktopSwitch = true;
    m_autoRemoveDesktop = true;

    std::ifstream file(path);
    if (!file.is_open()) return;

    // Variable store — var NAME = "value"
    std::map<std::string, std::string> vars;

    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string lhs = Trim(line.substr(0, eq));
        std::string rhs = Trim(line.substr(eq + 1));
        if (lhs.empty() || rhs.empty()) continue;

        // ---- Variable definition: var NAME = "value" ----
        {
            std::string lc = lhs;
            std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
            if (lc.find("var ") == 0) {
                std::string varKey = Trim(lhs.substr(4));
                std::string varVal = rhs;
                if (varVal.size() >= 2 && varVal[0] == '"' && varVal.back() == '"')
                    varVal = varVal.substr(1, varVal.size() - 2);
                vars[varKey] = varVal;
                continue;
            }
        }

        // ---- Substitute $VAR$ in rhs before processing ----
        for (std::map<std::string, std::string>::iterator it = vars.begin(); it != vars.end(); ++it) {
            std::string pattern = "$" + it->first + "$";
            size_t pos = 0;
            while ((pos = rhs.find(pattern, pos)) != std::string::npos) {
                rhs.replace(pos, pattern.length(), it->second);
                pos += it->second.length();
            }
        }

        // Boolean flags
        auto parseBool = [&](bool& flag) {
            std::string v = rhs;
            std::transform(v.begin(), v.end(), v.begin(), ::tolower);
            flag = (v == "enabled" || v == "1" || v == "true");
        };
        if (lhs == "Virtual_Desktop_Switch")              { parseBool(m_enabled); continue; }
        if (lhs == "New_Desktop_Creation")                 { parseBool(m_newDesktopCreation); continue; }
        if (lhs == "Allow_Bad_Keys")                       { parseBool(m_allowBadKeys); continue; }
        if (lhs == "Window_And_Virtual_Desktop_Switch")    { parseBool(m_windowAndDesktopSwitch); continue; }
        if (lhs == "Auto_Remove_Virtual_Desktop")          { parseBool(m_autoRemoveDesktop); continue; }

        // Key binding: "WIN+SHIFT+1 = MOVE_SWITCH 3"
        std::istringstream rss(rhs);
        std::string act; int arg = 0;
        rss >> act;
        std::string as; if (rss >> as) arg = std::atoi(as.c_str());
        std::transform(act.begin(), act.end(), act.begin(), ::toupper);

        // RUN("app", "args") has spaces — rss >> act truncates it.
        // Reconstruct the full expression from rhs.
        if (act.find("RUN(") == 0 && act.back() != ')') {
            size_t closeParen = rhs.find(')');
            if (closeParen != std::string::npos) {
                act = rhs.substr(0, closeParen + 1);
                std::transform(act.begin(), act.end(), act.begin(), ::toupper);
            }
        }

        DWORD targetMods = 0; DWORD targetVk = 0;
        if (act.find("SIMULATE_KEY[") == 0 && act.back() == ']') {
            // Extract inner combo from SIMULATE_KEY[CTRL+V]
            std::string inner = act.substr(13, act.size() - 14);
            std::string kn;
            for (size_t sp = 0;;) {
                size_t pp = inner.find('+', sp);
                std::string p = Trim(inner.substr(sp, (pp == std::string::npos) ? std::string::npos : pp - sp));
                DWORD m = ParseModifier(p);
                if (m) targetMods |= m; else kn = p;
                if (pp == std::string::npos) break;
                sp = pp + 1;
            }
            targetVk = KeyNameToVK(kn);
            act = "SIMULATE_KEY";
        }

        // LoadConf["path"] — same space issue as RUN()
        std::string confLoadPath;
        if (act.find("LOADCONF[") == 0) {
            size_t openB = rhs.find('[');
