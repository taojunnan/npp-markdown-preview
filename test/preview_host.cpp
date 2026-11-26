// Standalone harness: hosts the real WebView2 preview page with sample content,
// optionally captures a PNG. Used for visual QA outside Notepad++.
//
//   preview_host.exe <distDir> <sample.md> <out.png> [light|dark]

#include <windows.h>
#include <string>
#include <vector>

#include "webview.h"

static WebViewHost* g_web = nullptr;
static std::wstring g_theme = L"light";
static std::wstring g_out;
static std::wstring g_samplePath;
static std::wstring g_sampleText;
static std::wstring g_pdfPath;
static bool g_captured = false;
static HWND g_hwnd = nullptr;
static bool g_armed = false;

static void ArmCapture() {
    if (g_armed || !g_hwnd) return;
    g_armed = true;
    SetTimer(g_hwnd, 1, 1500, nullptr);
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

static std::wstring ReadFileUtf8(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::wstring();
    LARGE_INTEGER size = {};
    GetFileSizeEx(h, &size);
    std::string data((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    ReadFile(h, data.data(), (DWORD)data.size(), &read, nullptr);
    CloseHandle(h);
    data.resize(read);
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        data = data.substr(3);
    }
    return Utf8ToWide(data);
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
            default:
                if (c < 0x20) {
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

static std::wstring DirNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

static void SendContent() {
    std::wstring json = L"{\"type\":\"content\",\"text\":";
    AppendJsonString(json, g_sampleText);
    json += L",\"path\":";
    AppendJsonString(json, g_samplePath);
    json += L",\"dir\":";
    AppendJsonString(json, DirNameOf(g_samplePath));
    json += L",\"dark\":";
    json += (g_theme == L"dark") ? L"true" : L"false";
    json += L",\"mode\":\"auto\",\"fontSize\":16,\"toc\":true,\"sync\":true,\"truncated\":false}";
    if (g_web) g_web->PostJson(json);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_TIMER && w == 1) {
        KillTimer(hwnd, 1);
        if (g_web) g_web->CapturePng(g_out);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_SIZE && g_web) {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        g_web->Resize(rc.right - rc.left, rc.bottom - rc.top);
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("usage: preview_host <distDir> <sample.md> <out.png> [light|dark]\n");
        return 2;
    }
    wchar_t buf[32768];
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, buf, 32768);
    std::wstring distDir(buf);
    MultiByteToWideChar(CP_ACP, 0, argv[2], -1, buf, 32768);
    g_samplePath = buf;
    MultiByteToWideChar(CP_ACP, 0, argv[3], -1, buf, 32768);
    g_out = buf;
    if (argc >= 5) {
        MultiByteToWideChar(CP_ACP, 0, argv[4], -1, buf, 32768);
        g_theme = buf;
    }
    if (argc >= 6) {
        MultiByteToWideChar(CP_ACP, 0, argv[5], -1, buf, 32768);
        g_pdfPath = buf;
    }

    g_sampleText = ReadFileUtf8(g_samplePath);
    if (g_sampleText.empty()) {
        printf("cannot read sample\n");
        return 1;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    wc.lpszClassName = L"MdPreviewHostWnd";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"MdPreviewHostWnd", L"Markdown Preview Host",
                                WS_OVERLAPPEDWINDOW, 40, 40, 1080, 1480, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    g_hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    g_web = new WebViewHost();
    g_web->onMessage = [](const std::wstring& json) {
        if (json.find(L"\"ready\"") != std::wstring::npos) {
            SendContent();
            ArmCapture();
        }
    };
    g_web->onReady = []() {
        SendContent();
        ArmCapture();
    };
    g_web->onError = [](const std::wstring& text) {
        wprintf(L"WebView error: %s\n", text.c_str());
        fflush(stdout);
        PostQuitMessage(1);
    };
    g_web->onCaptureDone = [](bool ok) {
        printf("capture %s\n", ok ? "ok" : "failed");
        fflush(stdout);
        if (!ok) { PostQuitMessage(3); return; }
        if (!g_pdfPath.empty()) {
            if (!g_web->PrintToPdf(g_pdfPath)) { printf("pdf start failed\n"); fflush(stdout); PostQuitMessage(4); }
            return;
        }
        PostQuitMessage(0);
    };
    g_web->onPdfDone = [](bool ok) {
        printf("pdf %s\n", ok ? "ok" : "failed");
        fflush(stdout);
        PostQuitMessage(ok ? 0 : 5);
    };

    std::wstring userData = distDir + L"\\hostprofile";
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    g_web->Resize(rc.right - rc.left, rc.bottom - rc.top);
    if (!g_web->Create(hwnd, distDir, userData, distDir + L"\\web")) {
        printf("Create() failed\n");
        return 1;
    }

    MSG msg;
    DWORD start = GetTickCount();
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (GetTickCount() - start > 15000) {
            printf("timeout\n");
            break;
        }
    }

    g_web->Destroy();
    delete g_web;
    CoUninitialize();
    return 0;
}
