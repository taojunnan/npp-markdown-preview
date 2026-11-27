// Markdown Preview for Notepad++
// A dockable, live Markdown preview panel with a modern (WebView2) renderer.
//
// Build: see build.ps1 (zig cc / MinGW-w64, x64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

#include "sdk\Docking.h"
#include "sdk\PluginInterface.h"
#include "i18n.h"
#include "log.h"
#include "webview.h"

// ------------------------------------------------------------- plugin data --

static std::wstring g_pluginName = L"Markdown 预览";
static std::wstring g_panelTitle = L"Markdown 预览";

// getName() runs before setInfo(), so the language has to be resolved from the
// default config location at that point.
static void DetectLanguageEarly() {
    wchar_t appData[MAX_PATH] = {};
    std::wstring ini;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        ini = std::wstring(appData) +
              L"\\Notepad++\\plugins\\config\\MarkdownPreview\\MarkdownPreview.ini";
    }
    wchar_t buf[16] = {};
    if (!ini.empty()) GetPrivateProfileStringW(L"general", L"language", L"auto", buf, 16, ini.c_str());
    mdtr::Detect(buf[0] ? buf : L"auto");
}

static const wchar_t* PluginDisplayName() {
    static std::wstring name;
    const wchar_t* want = mdtr::English() ? L"Markdown Preview" : L"Markdown 预览";
    if (name != want) name = want;
    return name.c_str();
}

static void LocalizeMenu();

static void ApplyLanguage() {
    g_pluginName = mdtr::English() ? L"Markdown Preview" : L"Markdown 预览";
    g_panelTitle = mdtr::English() ? L"Markdown Preview" : L"Markdown 预览";
    LocalizeMenu();
}
#define kPluginName g_pluginName.c_str()
#define kPanelTitle g_panelTitle.c_str()
static const wchar_t* kDllModuleName = L"MarkdownPreview.dll";

NppData nppData = {};
HINSTANCE g_inst = nullptr;

static HWND g_panel = nullptr;
static WebViewHost* g_web = nullptr;
static bool g_dark = false;
static bool g_userHidden = false;
static bool g_webFailed = false;
// true until NPPN_READY: Notepad++ may restore the dock from the last session,
// and the panel is meant to stay closed until the user opens it.
static bool g_startup = true;

static std::wstring g_pluginDir;
static std::wstring g_configDir;
static std::wstring g_iniPath;

static std::wstring g_lastText;
static std::wstring g_lastPath;
static ULONGLONG g_lastSyncTick = 0;
static bool g_printMode = false;

// ---------------------------------------------------------------- settings --

struct Config {
    bool autoOpen = false;
    bool preload = false;          // opt-in: warm up WebView2 in the background
    bool releaseOnHide = true;     // free the browser once the panel stays closed
    bool debug = false;            // write preview.log next to the ini
    std::wstring language = L"auto";  // auto | zh | en
    std::wstring theme = L"auto";  // auto | light | dark
    int fontSize = 16;
    bool toc = true;
    bool sync = true;
};

static Config g_cfg;

static int IniReadInt(const wchar_t* section, const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(section, key, def, g_iniPath.c_str());
}

static std::wstring IniReadStr(const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[512] = {};
    GetPrivateProfileStringW(section, key, def, buf, 512, g_iniPath.c_str());
    return std::wstring(buf);
}

static void IniWrite(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), g_iniPath.c_str());
}

static void LoadConfig() {
    g_cfg.autoOpen = IniReadInt(L"general", L"autoOpen", 0) != 0;
    g_cfg.preload = IniReadInt(L"general", L"preload", 0) != 0;
    g_cfg.releaseOnHide = IniReadInt(L"general", L"releaseOnHide", 1) != 0;
    g_cfg.debug = IniReadInt(L"general", L"debug", 0) != 0;
    g_cfg.language = IniReadStr(L"general", L"language", L"auto");
    g_cfg.theme = IniReadStr(L"appearance", L"theme", L"auto");
    if (g_cfg.theme != L"auto" && g_cfg.theme != L"light" && g_cfg.theme != L"dark") g_cfg.theme = L"auto";
    g_cfg.fontSize = IniReadInt(L"appearance", L"fontSize", 16);
    if (g_cfg.fontSize < 11) g_cfg.fontSize = 11;
    if (g_cfg.fontSize > 28) g_cfg.fontSize = 28;
    g_cfg.toc = IniReadInt(L"appearance", L"toc", 1) != 0;
    g_cfg.sync = IniReadInt(L"appearance", L"sync", 1) != 0;
}

static void SaveConfig() {
    IniWrite(L"general", L"autoOpen", g_cfg.autoOpen ? L"1" : L"0");
    IniWrite(L"general", L"preload", g_cfg.preload ? L"1" : L"0");
    IniWrite(L"general", L"releaseOnHide", g_cfg.releaseOnHide ? L"1" : L"0");
    IniWrite(L"general", L"language", g_cfg.language);
    IniWrite(L"appearance", L"theme", g_cfg.theme);
    IniWrite(L"appearance", L"fontSize", std::to_wstring(g_cfg.fontSize));
    IniWrite(L"appearance", L"toc", g_cfg.toc ? L"1" : L"0");
    IniWrite(L"appearance", L"sync", g_cfg.sync ? L"1" : L"0");
}

// ------------------------------------------------------------------ helpers --

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

static std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

static std::wstring DirNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    return path.substr(0, pos);
}

static std::wstring BaseNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

static std::wstring StripExt(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? name : name.substr(0, dot);
}

static std::wstring LowerExtOf(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return std::wstring();
    std::wstring ext = path.substr(dot);
    for (size_t i = 0; i < ext.size(); ++i) ext[i] = (wchar_t)towlower(ext[i]);
    return ext;
}

static bool IsMarkdownPath(const std::wstring& path) {
    std::wstring ext = LowerExtOf(path);
    return ext == L".md" || ext == L".markdown" || ext == L".mdown" || ext == L".mkd" || ext == L".mdx" ||
           ext == L".rmd";
}

static void AppendJsonString(std::wstring& out, const std::wstring& s) {
    out += L'"';
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            case L'\b': out += L"\\b"; break;
            case L'\f': out += L"\\f"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8] = {};
                    swprintf(buf, 8, L"\\u%04x", (unsigned)c);
                    out += buf;
                } else if (c == 0x2028 || c == 0x2029) {
                    wchar_t buf[8] = {};
                    swprintf(buf, 8, L"\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += L'"';
}

// Very small JSON reader, sufficient for the messages produced by our own page.
static bool JsonFindKey(const std::wstring& json, const wchar_t* key, size_t& valuePos) {
    std::wstring needle = L"\"";
    needle += key;
    needle += L"\"";
    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::wstring::npos) {
        size_t p = pos + needle.size();
        while (p < json.size() && iswspace(json[p])) ++p;
        if (p < json.size() && json[p] == L':') {
            ++p;
            while (p < json.size() && iswspace(json[p])) ++p;
            valuePos = p;
            return true;
        }
        pos += needle.size();
    }
    return false;
}

static bool JsonGetString(const std::wstring& json, const wchar_t* key, std::wstring& out) {
    size_t p = 0;
    if (!JsonFindKey(json, key, p)) return false;
    if (p >= json.size() || json[p] != L'"') return false;
    ++p;
    out.clear();
    while (p < json.size()) {
        wchar_t c = json[p++];
        if (c == L'"') return true;
        if (c != L'\\') {
            out += c;
            continue;
        }
        if (p >= json.size()) break;
        wchar_t e = json[p++];
        switch (e) {
            case L'"': out += L'"'; break;
            case L'\\': out += L'\\'; break;
            case L'/': out += L'/'; break;
            case L'b': out += L'\b'; break;
            case L'f': out += L'\f'; break;
            case L'n': out += L'\n'; break;
            case L'r': out += L'\r'; break;
            case L't': out += L'\t'; break;
            case L'u': {
                if (p + 4 <= json.size()) {
                    unsigned v = 0;
                    for (int i = 0; i < 4; ++i) {
                        wchar_t h = json[p + i];
                        unsigned d = 0;
                        if (h >= L'0' && h <= L'9') d = (unsigned)(h - L'0');
                        else if (h >= L'a' && h <= L'f') d = (unsigned)(h - L'a' + 10);
                        else if (h >= L'A' && h <= L'F') d = (unsigned)(h - L'A' + 10);
                        v = v * 16 + d;
                    }
                    out += (wchar_t)v;
                    p += 4;
                }
                break;
            }
            default: out += e; break;
        }
    }
    return true;
}

static bool JsonGetInt(const std::wstring& json, const wchar_t* key, int& out) {
    size_t p = 0;
    if (!JsonFindKey(json, key, p)) return false;
    wchar_t* end = nullptr;
    long v = wcstol(json.c_str() + p, &end, 10);
    if (end == json.c_str() + p) return false;
    out = (int)v;
    return true;
}

static bool JsonGetBool(const std::wstring& json, const wchar_t* key, bool& out) {
    size_t p = 0;
    if (!JsonFindKey(json, key, p)) return false;
    if (json.compare(p, 4, L"true") == 0) {
        out = true;
        return true;
    }
    if (json.compare(p, 5, L"false") == 0) {
        out = false;
        return true;
    }
    return false;
}

static HWND CurrentScintilla() {
    int which = 0;
    SendMessageW(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    return which == 0 ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
}

static std::wstring CurrentFilePath() {
    wchar_t buf[32768] = {};
    LRESULT ok = SendMessageW(nppData._nppHandle, NPPM_GETFULLCURRENTPATH, (WPARAM)(sizeof(buf) / sizeof(buf[0])),
                              (LPARAM)buf);
    return ok ? std::wstring(buf) : std::wstring();
}

static bool IsNppDarkMode() {
    return SendMessageW(nppData._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
}

// The editor may use a dark theme while Notepad++ itself stays in light mode.
// Follow what the user actually reads, not only the chrome.
static bool EditorIsDark() {
    HWND sci = CurrentScintilla();
    if (!sci) return false;
    LRESULT color = SendMessageW(sci, SCI_STYLEGETBACK, STYLE_DEFAULT, 0);
    int r = (int)(color & 0xFF);
    int g = (int)((color >> 8) & 0xFF);
    int b = (int)((color >> 16) & 0xFF);
    int luminance = (r * 299 + g * 587 + b * 114) / 1000;
    return luminance < 110;
}

static bool ResolvedDark() {
    if (g_cfg.theme == L"dark") return true;
    if (g_cfg.theme == L"light") return false;
    if (IsNppDarkMode()) return true;
    return EditorIsDark();
}

static void CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(g_panel ? g_panel : nullptr)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* dst = GlobalLock(mem);
        if (dst) {
            memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
            mem = nullptr;  // ownership transferred
        }
    }
    if (mem) GlobalFree(mem);
    CloseClipboard();
}

static bool WriteTextFile(const std::wstring& path, const std::string& utf8) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = utf8.empty() || (WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr) &&
                               written == utf8.size());
    CloseHandle(h);
    return ok;
}

static std::wstring AskSavePath(const wchar_t* filter, const wchar_t* defaultName, const wchar_t* defExt,
                                const std::wstring& initialDir) {
    wchar_t file[32768] = {};
    if (defaultName) wcsncpy(file, defaultName, 32767);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nppData._nppHandle;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 32768;
    ofn.lpstrDefExt = defExt;
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return std::wstring();
    return std::wstring(file);
}

// -------------------------------------------------------------- panel window --

static void EnsureWebView(bool quiet = false);
static void WarmUpWebView();
static void StartReleaseTimer();
static void ApplyRelease();
static void RenderNow(bool force);
static void ApplyTheme();
static void ShowPanel(bool show, bool userAction);
static void AutoOpenForCurrentFile();
static void UpdateToggleChecks();
static void PostToggleChecks();
static void RegisterToolbarIcon();
void HandleWebMessage(const std::wstring& json);

// NPPM_SETMENUITEMCHECK also refreshes the toolbar button.  Sending it from
// inside a toolbar click would re-enter comctl32 while it is still dispatching
// that click, so the refresh is always deferred to the next message loop pass.
#define WM_MD_UPDATE_CHECKS (WM_APP + 17)
#define WM_MD_RESET_WEB (WM_APP + 18)
#define TIMER_RENDER_DEBOUNCE 1
#define TIMER_RELEASE_ENGINE 2
#define TIMER_FORCE_SHOW 3
static const UINT kReleaseDelayMs = 30000;  // give the memory back after 30 s hidden
static bool g_checkUpdatePending = false;
static ULONGLONG g_webStartTick = 0;

static void PostToggleChecks() {
    if (!g_panel || g_checkUpdatePending) return;
    g_checkUpdatePending = true;
    PostMessageW(g_panel, WM_MD_UPDATE_CHECKS, 0, 0);
}

// The panel is empty until the WebView2 window and its first frame exist.  Draw
// a document-shaped skeleton in that gap so opening the preview never shows a
// blank rectangle (the page itself shows a matching skeleton after that).
static double PanelScale(HWND hwnd) {
    typedef UINT(WINAPI * PfnDpi)(HWND);
    static PfnDpi fn = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) fn = (PfnDpi)GetProcAddress(user32, "GetDpiForWindow");
    }
    if (fn && hwnd) {
        UINT dpi = fn(hwnd);
        if (dpi >= 48) return (double)dpi / 96.0;
    }
    return 1.0;
}

static void PaintSkeleton(HWND hwnd, HDC dc) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const double s = PanelScale(hwnd);
    auto S = [s](int v) { return (int)(v * s + 0.5); };

    const COLORREF bg = g_dark ? RGB(13, 17, 23) : RGB(255, 255, 255);
    const COLORREF bar = g_dark ? RGB(32, 39, 48) : RGB(237, 241, 245);
    const COLORREF divider = g_dark ? RGB(24, 30, 38) : RGB(240, 243, 246);

    HBRUSH bgBrush = CreateSolidBrush(bg);
    FillRect(dc, &rc, bgBrush);
    DeleteObject(bgBrush);

    HBRUSH barBrush = CreateSolidBrush(bar);

    // table of contents column
    const int tocW = S(228);
    int ty = S(58);
    const int tocWidths[7] = {66, 118, 100, 88, 108, 80, 94};
    for (int i = 0; i < 7; ++i) {
        RECT r = {S(12), ty, S(12) + S(tocWidths[i]), ty + S(9)};
        FillRect(dc, &r, barBrush);
        ty += S(9) + S(13);
    }

    // article
    const int x0 = tocW + S(44);
    const int x1 = rc.right - S(44);
    if (x1 > x0 + S(120)) {
        const int w = x1 - x0;
        int y = S(44);
        RECT title = {x0, y, x0 + w * 58 / 100, y + S(26)};
        FillRect(dc, &title, barBrush);
        y += S(26) + S(28);
        const int lines[6] = {94, 86, 78, 90, 66, 82};
        for (int i = 0; i < 6; ++i) {
            RECT r = {x0, y, x0 + w * lines[i] / 100, y + S(10)};
            FillRect(dc, &r, barBrush);
            y += S(10) + S(14);
            if (i == 2) y += S(10);  // paragraph break
        }
    }
    DeleteObject(barBrush);

    HPEN pen = CreatePen(PS_SOLID, 1, divider);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, tocW, 0, nullptr);
    LineTo(dc, tocW, rc.bottom);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            if (g_web) {
                RECT rc = {};
                GetClientRect(hwnd, &rc);
                g_web->Resize(rc.right - rc.left, rc.bottom - rc.top);
            }
            if (IsWindowVisible(hwnd)) EnsureWebView();
            return 0;
        }
        case WM_SHOWWINDOW: {
            if (wParam) {
                KillTimer(hwnd, TIMER_RELEASE_ENGINE);
                if (g_startup && !g_cfg.autoOpen) {
                    // restored dock from the previous session: stay closed
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
                EnsureWebView();
                InvalidateRect(hwnd, nullptr, TRUE);
            } else {
                // the panel is gone, so the Chromium processes are dead weight:
                // free them once the user has not come back for a while
                StartReleaseTimer();
            }
            PostToggleChecks();
            return 0;
        }
        case WM_MD_UPDATE_CHECKS: {
            g_checkUpdatePending = false;
            UpdateToggleChecks();
            return 0;
        }
        case WM_MD_RESET_WEB: {
            // a quiet warm-up failed (e.g. another instance holds the profile):
            // throw the broken host away and allow a retry when the panel opens
            if (g_web) {
                g_web->Destroy();
                delete g_web;
                g_web = nullptr;
            }
            g_webFailed = false;
            return 0;
        }
        case WM_SETFOCUS: {
            if (g_web) g_web->Focus();
            return 0;
        }
        case WM_ERASEBKGND:
            // WM_PAINT draws the background together with the placeholder
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd, &ps);
            if (!g_web || !g_web->IsReady()) PaintSkeleton(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == TIMER_RENDER_DEBOUNCE) {
                KillTimer(hwnd, TIMER_RENDER_DEBOUNCE);
                RenderNow(false);
            } else if (wParam == TIMER_FORCE_SHOW) {
                KillTimer(hwnd, TIMER_FORCE_SHOW);
                if (g_web) g_web->SetVisible(true);
            } else if (wParam == TIMER_RELEASE_ENGINE) {
                KillTimer(hwnd, TIMER_RELEASE_ENGINE);
                ApplyRelease();
            }
            return 0;
        }
        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void EnsurePanel() {
    if (g_panel) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NppMarkdownPreviewPanel";
    RegisterClassExW(&wc);

    g_panel = CreateWindowExW(0, L"NppMarkdownPreviewPanel", kPanelTitle, WS_CHILD | WS_CLIPCHILDREN,
                              0, 0, 520, 640, nppData._nppHandle, nullptr, g_inst, nullptr);
}

static void EnsureWebView(bool quiet) {
    mdlog::Line(L"EnsureWebView quiet=%d web=%d", quiet ? 1 : 0, g_web ? 1 : 0);
    if (g_webFailed || !g_panel) return;
    // Do not spin up the browser for a dock that Notepad++ restored on startup
    // while the panel is meant to stay closed.
    if (g_startup && !g_cfg.autoOpen) return;

    if (g_web) return;

    g_webStartTick = mdlog::Now();
    g_web = new WebViewHost();
    g_web->onReady = []() {
        mdlog::Line(L"page ready after %llu ms", mdlog::Now() - g_webStartTick);
        // the page has painted its first frame: swap the placeholder for it
        if (g_panel) KillTimer(g_panel, TIMER_FORCE_SHOW);
        if (g_web) g_web->SetVisible(true);
        if (g_panel) InvalidateRect(g_panel, nullptr, FALSE);
        RenderNow(true);
    };
    g_web->onMessage = [](const std::wstring& json) { HandleWebMessage(json); };
    g_web->onError = [quiet](const std::wstring& text) {
        mdlog::Line(L"webview error: %s", text.c_str());
        g_webFailed = true;
        if (g_web) g_web->SetVisible(true);
        if (quiet) {
            // background warm-up must never interrupt the user; drop the broken
            // instance so the next real "show" can try again and report properly
            if (g_panel) PostMessageW(g_panel, WM_MD_RESET_WEB, 0, 0);
            return;
        }
        MessageBoxW(nppData._nppHandle, text.c_str(), kPluginName, MB_ICONERROR | MB_OK);
    };
    g_web->onPdfDone = [](bool ok) {
        g_printMode = false;
        if (g_web) g_web->PostJson(L"{\"type\":\"printMode\",\"on\":false}");
        if (!ok) MessageBoxW(nppData._nppHandle, mdtr::T(L"PDF 导出失败。", L"PDF export failed."), kPluginName, MB_ICONWARNING | MB_OK);
    };

    std::wstring userData = g_configDir + L"\\WebView2";
    CreateDirectoryW(userData.c_str(), nullptr);

    RECT rc = {};
    GetClientRect(g_panel, &rc);
    g_web->Resize(rc.right - rc.left, rc.bottom - rc.top);
    g_web->Create(g_panel, g_pluginDir, userData, g_pluginDir + L"\\web");
    // never keep the placeholder up forever if the page fails to hand shake
    SetTimer(g_panel, TIMER_FORCE_SHOW, 5000, nullptr);
    g_web->SetDocFolder(DirNameOf(CurrentFilePath()));
}

// Starting the WebView2 browser process and loading the page is what makes the
// very first "show panel" take a second or two.  When a Markdown buffer is
// active the runtime is prepared in the background, so the panel opens with the
// document already rendered.
static void WarmUpWebView() {
    if (!g_cfg.preload || g_startup || g_webFailed || !g_panel) return;
    if (g_web && g_web->IsReady()) return;
    if (IsWindowVisible(g_panel)) return;

    RECT rc = {};
    GetClientRect(g_panel, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    EnsureWebView(true);
    if (g_web && (w < 240 || h < 240)) {
        // the hidden dock never got a real size: render at a sane viewport and
        // let the WM_SIZE that comes with the real show correct it
        g_web->Resize(900, 800);
    }
}

// The Chromium processes are the expensive part of the preview.  Once the panel
// has been closed for a while, drop them and let the next open build them again.
static void StartReleaseTimer() {
    if (!g_panel || !g_cfg.releaseOnHide) return;
    if (!g_web || !g_web->IsAvailable()) return;
    SetTimer(g_panel, TIMER_RELEASE_ENGINE, kReleaseDelayMs, nullptr);
}

// Closing the last WebView2 page also terminates the browser process, so a
// "page only" release saves nothing measurable; release everything.
static void ApplyRelease() {
    if (!g_web || IsWindowVisible(g_panel)) return;
    mdlog::Line(L"release engine (webview open for %llu ms)", mdlog::Now() - g_webStartTick);
    g_web->Destroy();
    delete g_web;
    g_web = nullptr;
    g_webFailed = false;
}

static void RegisterDock() {
    static DockedWidgetData data = {};
    data.hClient = g_panel;
    data.pszName = kPanelTitle;
    data.dlgID = 0;
    data.uMask = DWS_DF_CONT_RIGHT | DWS_USEOWNDARKMODE;
    data.pszAddInfo = nullptr;
    data.pszModuleName = kDllModuleName;
    SendMessageW(nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&data);
}

static void ShowPanel(bool show, bool userAction) {
    if (!g_panel) return;
    if (show) {
        EnsureWebView();
        SendMessageW(nppData._nppHandle, NPPM_DMMSHOW, 0, (LPARAM)g_panel);
        if (userAction) g_userHidden = false;
        if (g_web && g_web->IsReady()) g_web->SetVisible(true);
        InvalidateRect(g_panel, nullptr, TRUE);
        RenderNow(true);
        if (g_web) g_web->Focus();
    } else {
        SendMessageW(nppData._nppHandle, NPPM_DMMHIDE, 0, (LPARAM)g_panel);
        if (userAction) g_userHidden = true;
    }
    PostToggleChecks();
}

static void ApplyTheme() {
    bool dark = ResolvedDark();
    g_dark = dark;
    if (g_web) {
        g_web->SetTheme(dark);
        std::wstring mode = L"{\"type\":\"themeMode\",\"mode\":";
        AppendJsonString(mode, g_cfg.theme);
        mode += L"}";
        g_web->PostJson(mode);
    }
    if (g_panel) InvalidateRect(g_panel, nullptr, TRUE);
}

// --------------------------------------------------------------- rendering --

static std::wstring BuildContentJson(const std::wstring& text, const std::wstring& path, bool truncated) {
    std::wstring json;
    json.reserve(text.size() + 512);
    json += L"{\"type\":\"content\",\"text\":";
    AppendJsonString(json, text);
    json += L",\"path\":";
    AppendJsonString(json, path);
    json += L",\"dir\":";
    AppendJsonString(json, DirNameOf(path));
    json += L",\"dark\":";
    json += g_dark ? L"true" : L"false";
    json += L",\"mode\":";
    AppendJsonString(json, g_cfg.theme);
    json += L",\"fontSize\":";
    json += std::to_wstring(g_cfg.fontSize);
    json += L",\"toc\":";
    json += g_cfg.toc ? L"true" : L"false";
    json += L",\"sync\":";
    json += g_cfg.sync ? L"true" : L"false";
    json += L",\"truncated\":";
    json += truncated ? L"true" : L"false";
    json += L",\"lang\":";
    json += mdtr::English() ? L"\"en\"" : L"\"zh\"";
    json += L"}";
    return json;
}

static void RenderNow(bool force) {
    if (!g_panel || !g_web || !g_web->IsReady()) return;

    HWND sci = CurrentScintilla();
    if (!sci) return;

    LRESULT length = SendMessageW(sci, SCI_GETLENGTH, 0, 0);
    if (length < 0) length = 0;
    const long long kMaxBytes = 12LL * 1024 * 1024;
    bool truncated = (long long)length > kMaxBytes;
    long take = (long)(truncated ? kMaxBytes : length);

    std::string utf8((size_t)take + 1, '\0');
    SendMessageW(sci, SCI_GETTEXT, (WPARAM)(take + 1), (LPARAM)&utf8[0]);
    utf8.resize((size_t)take);

    std::wstring text = Utf8ToWide(utf8);
    std::wstring path = CurrentFilePath();

    if (!force && text == g_lastText && path == g_lastPath) return;
    g_lastText = text;
    g_lastPath = path;

    g_web->SetDocFolder(DirNameOf(path));
    g_web->PostJson(BuildContentJson(text, path, truncated));
}

static void ScheduleRender() {
    if (!g_panel) return;
    SetTimer(g_panel, TIMER_RENDER_DEBOUNCE, 120, nullptr);
}

static void SendScrollSync(bool force) {
    if (!g_cfg.sync || !g_panel || !g_web || !g_web->IsReady()) return;
    if (!IsWindowVisible(g_panel)) return;
    ULONGLONG now = GetTickCount64();
    if (!force && now - g_lastSyncTick < 45) return;
    g_lastSyncTick = now;

    HWND sci = CurrentScintilla();
    if (!sci) return;

    LRESULT firstVisible = SendMessageW(sci, SCI_GETFIRSTVISIBLELINE, 0, 0);
    LRESULT firstDoc = SendMessageW(sci, SCI_DOCLINEFROMVISIBLE, (WPARAM)firstVisible, 0);
    LRESULT total = SendMessageW(sci, SCI_GETLINECOUNT, 0, 0);
    LRESULT onScreen = SendMessageW(sci, SCI_LINESONSCREEN, 0, 0);

    std::wstring json = L"{\"type\":\"sync\",\"first\":";
    json += std::to_wstring((long long)firstDoc);
    json += L",\"total\":";
    json += std::to_wstring((long long)total);
    json += L",\"visible\":";
    json += std::to_wstring((long long)onScreen);
    json += L"}";
    g_web->PostJson(json);
}

// ------------------------------------------------------------ web messages --

void HandleWebMessage(const std::wstring& json) {
    std::wstring type;
    if (!JsonGetString(json, L"type", type)) return;

    if (type == L"ready") {
        ApplyTheme();
        RenderNow(true);
        return;
    }
    if (type == L"openExternal") {
        std::wstring url;
        if (JsonGetString(json, L"url", url) && !url.empty()) {
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    if (type == L"openFile") {
        std::wstring path;
        if (!JsonGetString(json, L"path", path) || path.empty()) return;
        if (path.rfind(L"file:///", 0) == 0) {
            path = path.substr(8);
            for (size_t i = 0; i < path.size(); ++i)
                if (path[i] == L'/') path[i] = L'\\';
        }
        bool absolute = (path.size() > 2 && path[1] == L':') || (path.size() > 1 && path[0] == L'\\');
        if (!absolute) {
            std::wstring base = DirNameOf(CurrentFilePath());
            if (!base.empty()) path = base + L"\\" + path;
        }
        if (IsMarkdownPath(path) && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            SendMessageW(nppData._nppHandle, NPPM_DOOPEN, 0, (LPARAM)path.c_str());
        } else {
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    if (type == L"timing") {
        std::wstring text;
        if (JsonGetString(json, L"text", text)) mdlog::Line(L"page timing %s", text.c_str());
        return;
    }
    if (type == L"copyText") {
        std::wstring text;
        if (JsonGetString(json, L"text", text)) CopyToClipboard(text);
        return;
    }
    if (type == L"setTheme") {
        std::wstring value;
        if (JsonGetString(json, L"value", value) &&
            (value == L"auto" || value == L"light" || value == L"dark")) {
            g_cfg.theme = value;
            SaveConfig();
            ApplyTheme();
        }
        return;
    }
    if (type == L"setFontSize") {
        int value = 0;
        if (JsonGetInt(json, L"value", value)) {
            if (value < 11) value = 11;
            if (value > 28) value = 28;
            g_cfg.fontSize = value;
            SaveConfig();
        }
        return;
    }
    if (type == L"setToc") {
        bool value = false;
        if (JsonGetBool(json, L"value", value)) {
            g_cfg.toc = value;
            SaveConfig();
        }
        return;
    }
    if (type == L"setSync") {
        bool value = false;
        if (JsonGetBool(json, L"value", value)) {
            g_cfg.sync = value;
            SaveConfig();
        }
        return;
    }
    if (type == L"openDevTools") {
        if (g_web) g_web->OpenDevTools();
        return;
    }
    if (type == L"printReady") {
        if (g_printMode) {
            std::wstring path = AskSavePath(
                mdtr::T(L"PDF 文件 (*.pdf)\0*.pdf\0所有文件 (*.*)\0*.*\0\0", L"PDF files (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0\0"), nullptr, L"pdf", DirNameOf(CurrentFilePath()));
            if (path.empty()) {
                g_printMode = false;
                g_web->PostJson(L"{\"type\":\"printMode\",\"on\":false}");
                return;
            }
            if (!g_web->PrintToPdf(path)) {
                g_printMode = false;
                g_web->PostJson(L"{\"type\":\"printMode\",\"on\":false}");
                MessageBoxW(nppData._nppHandle, mdtr::T(L"当前 WebView2 版本不支持 PDF 导出。", L"This WebView2 version does not support PDF export."), kPluginName,
                            MB_ICONWARNING | MB_OK);
                return;
            }
            std::wstring pdfPath = path;
            g_web->onPdfDone = [pdfPath](bool ok) {
                g_printMode = false;
                if (g_web) g_web->PostJson(L"{\"type\":\"printMode\",\"on\":false}");
                if (ok) ShellExecuteW(nullptr, L"open", pdfPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                else MessageBoxW(nppData._nppHandle, mdtr::T(L"PDF 导出失败。", L"PDF export failed."), kPluginName, MB_ICONWARNING | MB_OK);
            };
        }
        return;
    }
    if (type == L"exportHtmlResult" || type == L"htmlReady") {
        std::wstring html, name, mode;
        JsonGetString(json, L"html", html);
        JsonGetString(json, L"name", name);
        JsonGetString(json, L"mode", mode);
        if (html.empty()) return;
        std::string utf8 = WideToUtf8(html);
        if (mode == L"browser") {
            wchar_t tempDir[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tempDir);
            std::wstring path = std::wstring(tempDir) + (name.empty() ? L"preview" : StripExt(name)) + L".html";
            if (WriteTextFile(path, utf8)) ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            std::wstring path = AskSavePath(mdtr::T(L"网页文件 (*.html)\0*.html\0所有文件 (*.*)\0*.*\0\0", L"HTML files (*.html)\0*.html\0All files (*.*)\0*.*\0\0"),
                                            name.empty() ? L"preview.html" : name.c_str(), L"html",
                                            DirNameOf(CurrentFilePath()));
            if (path.empty()) return;
            if (WriteTextFile(path, utf8)) ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else MessageBoxW(nppData._nppHandle, mdtr::T(L"写入文件失败。", L"Could not write the file."), kPluginName, MB_ICONWARNING | MB_OK);
        }
        return;
    }
    if (type == L"requestPdf") {
        if (g_web && g_web->IsReady()) {
            g_printMode = true;
            if (!IsWindowVisible(g_panel)) ShowPanel(true, false);
            g_web->PostJson(L"{\"type\":\"printMode\",\"on\":true}");
        }
        return;
    }
    if (type == L"refresh") {
        RenderNow(true);
        return;
    }
    if (type == L"requestPrint") {
        if (!g_web || !g_web->IsReady()) return;
        if (!g_web->ShowPrintUi())
            MessageBoxW(nppData._nppHandle, mdtr::T(L"当前 WebView2 运行时不支持打印对话框，请改用“导出为 PDF”。", L"This WebView2 runtime has no print dialog; use Export as PDF instead."),
                        kPluginName, MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (type == L"syncNow") {
        SendScrollSync(true);
        return;
    }
    if (type == L"revealLine") {
        // Clicking a heading in the preview jumps to that line in the editor.
        HWND sci = CurrentScintilla();
        int line = -1;
        if (!JsonGetInt(json, L"line", line)) return;
        mdlog::Line(L"reveal line %d", line);
        if (!sci || line < 0) return;
        LRESULT count = SendMessageW(sci, SCI_GETLINECOUNT, 0, 0);
        if (line >= (int)count) line = (int)count - 1;
        SendMessageW(sci, SCI_GOTOLINE, (WPARAM)line, 0);
        int first = line > 2 ? line - 2 : 0;
        SendMessageW(sci, SCI_SETFIRSTVISIBLELINE, (WPARAM)first, 0);
        SetFocus(sci);
        return;
    }
}

// ------------------------------------------------------------------- menus --

static void CmdToggle() { ShowPanel(!IsWindowVisible(g_panel), true); }

static void CmdRefresh() { RenderNow(true); }

static void CmdAutoOpen();
static void CmdPreload();
static void CmdReleaseOnHide();

static void CmdOpenInBrowser() {
    if (!g_web || !g_web->IsReady()) return;
    g_web->PostJson(L"{\"type\":\"requestExport\",\"mode\":\"browser\"}");
}

static void CmdExportHtml() {
    if (!g_web || !g_web->IsReady()) return;
    g_web->PostJson(L"{\"type\":\"requestExport\",\"mode\":\"save\"}");
}

static void CmdExportPdf() {
    if (!g_web || !g_web->IsReady()) return;
    if (!IsWindowVisible(g_panel)) ShowPanel(true, false);
    g_printMode = true;
    g_web->PostJson(L"{\"type\":\"printMode\",\"on\":true}");
}

static void CmdPrint() {
    if (!g_web || !g_web->IsReady()) return;
    if (!g_web->ShowPrintUi())
        MessageBoxW(nppData._nppHandle, mdtr::T(L"当前 WebView2 运行时不支持打印对话框，请改用“导出为 PDF”。", L"This WebView2 runtime has no print dialog; use Export as PDF instead."), kPluginName,
                    MB_ICONINFORMATION | MB_OK);
}

static void CmdAbout() {
    std::wstring text = mdtr::English()
        ? std::wstring(L"Markdown Preview v1.1.0\n\n"
                       L"A live, dockable Markdown preview for Notepad++:\n"
                       L"\u2022 GitHub-style typography, tables / task lists / strikethrough\n"
                       L"\u2022 Syntax highlighting with one-click copy\n"
                       L"\u2022 Table of contents, light/dark themes, font zoom\n"
                       L"\u2022 Local relative images, HTML / PDF export\n"
                       L"\u2022 Optional \u201cfollow editor\u201d theme, preview while typing\n\n"
                       L"Renderer: Microsoft Edge WebView2 (installed)\n"
                       L"Shortcuts: Ctrl+Alt+P toggles the panel, Ctrl+Alt+R refreshes\n\n"
                       L"Config file:\n")
        : std::wstring(L"Markdown 预览 v1.1.0\n\n"
                       L"为 Notepad++ 提供实时、可停靠的 Markdown 预览：\n"
                       L"• GitHub 风格排版，表格 / 任务列表 / 删除线\n"
                       L"• 代码语法高亮，一键复制\n"
                       L"• 目录导航、深浅色主题、字号缩放\n"
                       L"• 支持本地相对路径图片、导出 HTML / PDF\n"
                       L"• 主题可选“跟随编辑器”，边输入边预览\n\n"
                       L"渲染引擎：Microsoft Edge WebView2（本机已安装）\n"
                       L"快捷键：Ctrl+Alt+P 开关面板，Ctrl+Alt+R 刷新\n\n"
                       L"配置文件：\n");
    text += g_iniPath;
    MessageBoxW(nppData._nppHandle, text.c_str(), mdtr::T(L"关于 Markdown 预览", L"About Markdown Preview"), MB_ICONINFORMATION | MB_OK);
}

// -------------------------------------------------------------- func items --

static ShortcutKey g_skToggle = {true, true, false, (UCHAR)'P'};
static ShortcutKey g_skRefresh = {true, true, false, (UCHAR)'R'};

static FuncItem g_funcs[] = {
    {L"显示 / 隐藏预览面板", CmdToggle, 0, false, &g_skToggle},
    {L"刷新预览", CmdRefresh, 0, false, &g_skRefresh},
    {L"打开 Markdown 时自动预览", CmdAutoOpen, 0, true, nullptr},
    {L"预加载预览（打开更快）", CmdPreload, 0, true, nullptr},
    {L"关闭预览后释放内存", CmdReleaseOnHide, 0, true, nullptr},
    {L"在浏览器中打开预览", CmdOpenInBrowser, 0, false, nullptr},
    {L"导出为 HTML…", CmdExportHtml, 0, false, nullptr},
    {L"导出为 PDF…", CmdExportPdf, 0, false, nullptr},
    {L"打印…", CmdPrint, 0, false, nullptr},
    {L"关于 Markdown 预览", CmdAbout, 0, false, nullptr},
};

// Notepad++ copies the menu labels right after setInfo(), so the language has
// to be decided there.
static void LocalizeMenu() {
    static const wchar_t* const en[] = {
        L"Show / hide preview panel",
        L"Refresh preview",
        L"Auto-open for Markdown files",
        L"Preload preview (opens faster)",
        L"Release memory when hidden",
        L"Open preview in browser",
        L"Export as HTML\u2026",
        L"Export as PDF\u2026",
        L"Print\u2026",
        L"About Markdown Preview",
    };
    static const int n = (int)(sizeof(en) / sizeof(en[0]));
    if (!mdtr::English()) return;
    for (int i = 0; i < n && i < (int)(sizeof(g_funcs) / sizeof(g_funcs[0])); ++i) {
        wcsncpy_s(g_funcs[i]._itemName, menuItemSize, en[i], _TRUNCATE);
    }
}
static const int kCmdCount = (int)(sizeof(g_funcs) / sizeof(g_funcs[0]));

static void AutoOpenForCurrentFile() {
    if (!g_cfg.autoOpen || g_userHidden) return;
    if (IsWindowVisible(g_panel)) return;
    if (IsMarkdownPath(CurrentFilePath())) ShowPanel(true, false);
}

// --------------------------------------------------------------- toolbar ---

static HICON g_iconNormal = nullptr;
static HICON g_iconDark = nullptr;
static HBITMAP g_toolbarBmp = nullptr;
static bool g_toolbarRegistered = false;

// A compact "M" badge: at 16x16 a plain letter is the only thing that stays
// readable, and the checked state supplies the "panel is open" feedback.
static void DrawToggleBadge(HDC dc, bool lightBadge) {
    HBRUSH face = CreateSolidBrush(lightBadge ? RGB(222, 228, 236) : RGB(47, 58, 74));
    HGDIOBJ oldBrush = SelectObject(dc, face);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, 0, 1, 16, 15, 5, 5);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(face);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, lightBadge ? RGB(31, 41, 55) : RGB(255, 255, 255));

    LOGFONTW lf = {};
    lf.lfHeight = -11;
    lf.lfWeight = FW_BOLD;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy(lf.lfFaceName, L"Segoe UI");
    HFONT font = CreateFontIndirectW(&lf);
    HGDIOBJ oldFont = SelectObject(dc, font);
    TextOutW(dc, 3, 1, L"M", 1);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

// Icons are used when a non-classic toolbar icon set is active (the default in
// dark mode).  A magenta key colour becomes full transparency, which keeps the
// rounded corners clean without any alpha blending artefacts.
static HICON CreateToggleIcon(bool forDarkToolbar) {
    const int S = 16;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = S;
    bmi.bmiHeader.biHeight = -S;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        DeleteDC(dc);
        return nullptr;
    }

    HGDIOBJ oldBmp = SelectObject(dc, color);
    RECT rc = {0, 0, S, S};
    HBRUSH key = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(dc, &rc, key);
    DeleteObject(key);
    DrawToggleBadge(dc, forDarkToolbar);

    BYTE* px = (BYTE*)bits;
    for (int i = 0; i < S * S; ++i) {
        BYTE b = px[i * 4 + 0];
        BYTE g = px[i * 4 + 1];
        BYTE r = px[i * 4 + 2];
        px[i * 4 + 3] = (r > 245 && g < 12 && b > 245) ? 0 : 255;
    }

    SelectObject(dc, oldBmp);
    DeleteDC(dc);

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

// Classic toolbar icon sets draw HBITMAPs, with the button face colour acting
// as the transparent colour.
static HBITMAP CreateToggleBitmap() {
    const int S = 16;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, S, S);
    if (bmp) {
        HGDIOBJ oldBmp = SelectObject(dc, bmp);
        RECT rc = {0, 0, S, S};
        HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        DrawToggleBadge(dc, false);
        SelectObject(dc, oldBmp);
    }
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return bmp;
}

static void UpdateToggleChecks() {
    if (!g_panel) return;
    int cmdId = g_funcs[0]._cmdID;
    if (cmdId <= 0) return;
    SendMessageW(nppData._nppHandle, NPPM_SETMENUITEMCHECK, (WPARAM)cmdId,
                 (LPARAM)(IsWindowVisible(g_panel) ? TRUE : FALSE));
}

static void RegisterToolbarIcon() {
    if (g_toolbarRegistered || !g_panel) return;
    int cmdId = g_funcs[0]._cmdID;
    if (cmdId <= 0) return;

    if (!g_toolbarBmp) g_toolbarBmp = CreateToggleBitmap();
    if (!g_iconNormal) g_iconNormal = CreateToggleIcon(false);
    if (!g_iconDark) g_iconDark = CreateToggleIcon(true);
    if (!g_toolbarBmp || !g_iconNormal || !g_iconDark) return;

    toolbarIconsWithDarkMode icons = {};
    icons.hToolbarBmp = g_toolbarBmp;
    icons.hToolbarIcon = g_iconNormal;
    icons.hToolbarIconDarkMode = g_iconDark;
    SendMessageW(nppData._nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE, (WPARAM)cmdId, (LPARAM)&icons);
    g_toolbarRegistered = true;
    UpdateToggleChecks();
}

static void CmdAutoOpen() {
    g_cfg.autoOpen = !g_cfg.autoOpen;
    SaveConfig();
    SendMessageW(nppData._nppHandle, NPPM_SETMENUITEMCHECK, (WPARAM)g_funcs[2]._cmdID,
                 (LPARAM)(g_cfg.autoOpen ? TRUE : FALSE));
    if (g_cfg.autoOpen) AutoOpenForCurrentFile();
}

static void CmdPreload() {
    g_cfg.preload = !g_cfg.preload;
    SaveConfig();
    SendMessageW(nppData._nppHandle, NPPM_SETMENUITEMCHECK, (WPARAM)g_funcs[3]._cmdID,
                 (LPARAM)(g_cfg.preload ? TRUE : FALSE));
    if (g_cfg.preload) {
        if (IsMarkdownPath(CurrentFilePath())) WarmUpWebView();
    } else if (g_web && g_panel && !IsWindowVisible(g_panel)) {
        // give the memory back when the panel is not on screen
        g_web->Destroy();
        delete g_web;
        g_web = nullptr;
    }
}

static void CmdReleaseOnHide() {
    g_cfg.releaseOnHide = !g_cfg.releaseOnHide;
    SaveConfig();
    SendMessageW(nppData._nppHandle, NPPM_SETMENUITEMCHECK, (WPARAM)g_funcs[4]._cmdID,
                 (LPARAM)(g_cfg.releaseOnHide ? TRUE : FALSE));
    if (!g_panel) return;
    if (!g_cfg.releaseOnHide) {
        KillTimer(g_panel, TIMER_RELEASE_ENGINE);
    } else if (!IsWindowVisible(g_panel)) {
        StartReleaseTimer();
    }
}

// ----------------------------------------------------------- plugin exports --

extern "C" __declspec(dllexport) void setInfo(NppData data) {
    nppData = data;

    wchar_t modPath[MAX_PATH * 2] = {};
    GetModuleFileNameW(g_inst, modPath, (DWORD)(sizeof(modPath) / sizeof(modPath[0])));
    std::wstring full(modPath);
    g_pluginDir = DirNameOf(full);

    wchar_t cfg[32768] = {};
    LRESULT got = SendMessageW(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR,
                               (WPARAM)(sizeof(cfg) / sizeof(cfg[0])), (LPARAM)cfg);
    if (got && cfg[0]) {
        g_configDir = std::wstring(cfg) + L"\\MarkdownPreview";
    } else {
        wchar_t appData[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
            g_configDir = std::wstring(appData) + L"\\Notepad++\\plugins\\config\\MarkdownPreview";
        }
    }
    CreateDirectoryW(g_configDir.c_str(), nullptr);
    g_iniPath = g_configDir + L"\\MarkdownPreview.ini";

    LoadConfig();
    mdtr::Detect(g_cfg.language);
    ApplyLanguage();
    mdlog::Init(g_configDir, g_cfg.debug);
    mdlog::Line(L"plugin loaded, dir=%s", g_pluginDir.c_str());
    // materialise the defaults once so the settings file is easy to find and edit
    if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) SaveConfig();
    EnsurePanel();
    if (g_panel) RegisterDock();
    ApplyTheme();
}

extern "C" __declspec(dllexport) const wchar_t* getName() {
    DetectLanguageEarly();
    return PluginDisplayName();
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    *count = kCmdCount;
    return g_funcs;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notification) {
    if (!notification) return;

    switch (notification->nmhdr.code) {
        case NPPN_READY: {
            SendMessageW(nppData._nppHandle, NPPM_SETMENUITEMCHECK, (WPARAM)g_funcs[2]._cmdID,
                         (LPARAM)(g_cfg.autoOpen ? TRUE : FALSE));
            SendMessageW(nppData._nppHandle, NPPM_SETMENUITEMCHECK, (WPARAM)g_funcs[3]._cmdID,
                         (LPARAM)(g_cfg.preload ? TRUE : FALSE));
            SendMessageW(nppData._nppHandle, NPPM_SETMENUITEMCHECK, (WPARAM)g_funcs[4]._cmdID,
                         (LPARAM)(g_cfg.releaseOnHide ? TRUE : FALSE));
            g_startup = false;
            ApplyTheme();
            RegisterToolbarIcon();

            // The preview stays closed at startup unless the user opted into
            // automatic preview for Markdown files.
            bool wantVisible = g_cfg.autoOpen && !g_userHidden &&
                               IsMarkdownPath(CurrentFilePath());
            if (!wantVisible && IsWindowVisible(g_panel)) {
                SendMessageW(nppData._nppHandle, NPPM_DMMHIDE, 0, (LPARAM)g_panel);
            }
            UpdateToggleChecks();
            if (wantVisible) {
                ShowPanel(true, false);
            } else if (IsMarkdownPath(CurrentFilePath())) {
                // warm up in the background so the first click is instant
                WarmUpWebView();
            }
            return;
        }
        case NPPN_TBMODIFICATION: {
            // Notepad++ tells plugins when toolbar icons can be registered.
            RegisterToolbarIcon();
            return;
        }
        case NPPN_BUFFERACTIVATED:
        case NPPN_FILEOPENED:
        case NPPN_FILECLOSED:
        case NPPN_FILERENAMED: {
            g_lastText.clear();
            g_lastPath.clear();
            ApplyTheme();
            ScheduleRender();
            if (IsMarkdownPath(CurrentFilePath())) WarmUpWebView();
            AutoOpenForCurrentFile();
            return;
        }
        case NPPN_DARKMODECHANGED: {
            ApplyTheme();
            return;
        }
        case NPPN_BEFORESHUTDOWN:
        case NPPN_SHUTDOWN: {
            if (g_web) {
                g_web->Destroy();
                delete g_web;
                g_web = nullptr;
            }
            return;
        }
        default:
            break;
    }

    if (!g_panel) return;

    HWND from = (HWND)notification->nmhdr.hwndFrom;
    if (from != nppData._scintillaMainHandle && from != nppData._scintillaSecondHandle) return;

    if (notification->nmhdr.code == SCN_MODIFIED) {
        const int interesting = SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT | SC_PERFORMED_UNDO | SC_PERFORMED_REDO;
        if ((notification->modificationType & interesting) && IsWindowVisible(g_panel)) ScheduleRender();
        return;
    }
    if (notification->nmhdr.code == SCN_UPDATEUI) {
        if (notification->updated & SC_UPDATE_V_SCROLL) SendScrollSync(false);
        return;
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_inst = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
