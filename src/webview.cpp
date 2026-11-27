// Markdown Preview for Notepad++ - WebView2 host implementation
#include "i18n.h"
#include "log.h"
#include "webview.h"

#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>

#include <string>
#include <vector>

#include "WebView2.h"

#ifdef WEBVIEW_TRACE
#define WV_TRACE(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
#else
#define WV_TRACE(...) do {} while (0)
#endif

namespace {

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateEnv)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

const wchar_t* kAppHost = L"nppmd.local";
const wchar_t* kAssetHost = L"mdassets.local";
const wchar_t* kAppUrl = L"https://nppmd.local/index.html";

// ---------------------------------------------------------------- helpers ---

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

int HexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

// Percent-decode an URL component into a wide string (input is UTF-8 percent encoded).
std::wstring PercentDecode(const std::wstring& in) {
    std::string bytes;
    bytes.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        wchar_t c = in[i];
        if (c == L'%' && i + 2 < in.size()) {
            int hi = HexVal(in[i + 1]);
            int lo = HexVal(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                bytes.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (c < 128) {
            bytes.push_back((char)c);
        } else {
            // Should not happen (URLs are encoded), keep best effort.
            bytes.push_back('?');
        }
    }
    return Utf8ToWide(bytes);
}

std::wstring LowerExt(const std::wstring& path) {
    const wchar_t* dot = wcsrchr(path.c_str(), L'.');
    if (!dot) return std::wstring();
    std::wstring ext(dot);
    for (size_t i = 0; i < ext.size(); ++i) ext[i] = (wchar_t)towlower(ext[i]);
    return ext;
}

std::wstring MimeForPath(const std::wstring& path) {
    std::wstring ext = LowerExt(path);
    if (ext == L".png") return L"image/png";
    if (ext == L".jpg" || ext == L".jpeg") return L"image/jpeg";
    if (ext == L".gif") return L"image/gif";
    if (ext == L".webp") return L"image/webp";
    if (ext == L".bmp") return L"image/bmp";
    if (ext == L".ico") return L"image/x-icon";
    if (ext == L".avif") return L"image/avif";
    if (ext == L".svg") return L"image/svg+xml";
    if (ext == L".pdf") return L"application/pdf";
    if (ext == L".mp4") return L"video/mp4";
    if (ext == L".webm") return L"video/webm";
    return std::wstring();
}

bool IsAbsolutePath(const std::wstring& p) {
    if (p.size() >= 2 && p[1] == L':' && iswalpha(p[0])) return true;          // C:\...
    if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\') return true;          // \\server\share
    if (p.size() >= 1 && (p[0] == L'\\' || p[0] == L'/')) return true;         // \rooted
    return false;
}

bool ReadWholeFile(const std::wstring& path, std::vector<BYTE>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 64LL * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    bool ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size());
    CloseHandle(h);
    return ok;
}

IStream* StreamFromBytes(const std::vector<BYTE>& data) {
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, data.empty() ? 1 : data.size());
    if (!mem) return nullptr;
    void* dst = GlobalLock(mem);
    if (!dst) {
        GlobalFree(mem);
        return nullptr;
    }
    if (!data.empty()) memcpy(dst, data.data(), data.size());
    GlobalUnlock(mem);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) {
        GlobalFree(mem);
        return nullptr;
    }
    return stream;
}

// -------------------------------------------------------- environment opts --

class EnvOptions : public ICoreWebView2EnvironmentOptions {
public:
    explicit EnvOptions(const std::wstring& args) : args_(args) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2EnvironmentOptions)) {
            *ppv = static_cast<ICoreWebView2EnvironmentOptions*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    HRESULT STDMETHODCALLTYPE get_AdditionalBrowserArguments(LPWSTR* value) override {
        return CopyOut(args_, value);
    }
    HRESULT STDMETHODCALLTYPE put_AdditionalBrowserArguments(LPCWSTR value) override {
        args_ = value ? value : L"";
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Language(LPWSTR* value) override { return CopyOut(L"", value); }
    HRESULT STDMETHODCALLTYPE put_Language(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_TargetCompatibleBrowserVersion(LPWSTR* value) override {
        return CopyOut(L"", value);
    }
    HRESULT STDMETHODCALLTYPE put_TargetCompatibleBrowserVersion(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AllowSingleSignOnUsingOSPrimaryAccount(BOOL* allow) override {
        if (!allow) return E_POINTER;
        *allow = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AllowSingleSignOnUsingOSPrimaryAccount(BOOL) override { return S_OK; }

private:
    static HRESULT CopyOut(const std::wstring& s, LPWSTR* out) {
        if (!out) return E_POINTER;
        size_t bytes = (s.size() + 1) * sizeof(wchar_t);
        LPWSTR mem = (LPWSTR)CoTaskMemAlloc(bytes);
        if (!mem) return E_OUTOFMEMORY;
        memcpy(mem, s.c_str(), bytes);
        *out = mem;
        return S_OK;
    }

    LONG ref_ = 1;
    std::wstring args_;
};

// ------------------------------------------------------------ COM handlers --
// One small class per WebView2 event/callback: a std::function plus standard
// IUnknown plumbing. Written by hand so that no WRL is required.

#define NPP_COM_BODY(IFACE, IIDNAME)                                                             \
    LONG ref_ = 1;                                                                               \
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }     \
    ULONG STDMETHODCALLTYPE Release() override {                                                 \
        LONG r = InterlockedDecrement(&ref_);                                                    \
        if (r == 0) delete this;                                                                 \
        return (ULONG)r;                                                                         \
    }                                                                                            \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {                 \
        if (!ppv) return E_POINTER;                                                              \
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IIDNAME)) {                       \
            *ppv = static_cast<IFACE*>(this);                                                    \
            AddRef();                                                                            \
            return S_OK;                                                                         \
        }                                                                                        \
        *ppv = nullptr;                                                                          \
        return E_NOINTERFACE;                                                                    \
    }

struct EnvHandler : ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    typedef std::function<HRESULT(HRESULT, ICoreWebView2Environment*)> Fn;
    explicit EnvHandler(Fn fn) : fn_(std::move(fn)) {}
    NPP_COM_BODY(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                 IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        return fn_(hr, env);
    }
    Fn fn_;
};

struct ControllerHandler : ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    typedef std::function<HRESULT(HRESULT, ICoreWebView2Controller*)> Fn;
    explicit ControllerHandler(Fn fn) : fn_(std::move(fn)) {}
    NPP_COM_BODY(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                 IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* controller) override {
        return fn_(hr, controller);
    }
    Fn fn_;
};

struct MessageHandler : ICoreWebView2WebMessageReceivedEventHandler {
    typedef std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)> Fn;
    explicit MessageHandler(Fn fn) : fn_(std::move(fn)) {}
    NPP_COM_BODY(ICoreWebView2WebMessageReceivedEventHandler,
                 IID_ICoreWebView2WebMessageReceivedEventHandler)
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2WebMessageReceivedEventArgs* args) override {
        return fn_(sender, args);
    }
    Fn fn_;
};

struct NavigationStartingHandler : ICoreWebView2NavigationStartingEventHandler {
    typedef std::function<HRESULT(ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*)> Fn;
    explicit NavigationStartingHandler(Fn fn) : fn_(std::move(fn)) {}
    NPP_COM_BODY(ICoreWebView2NavigationStartingEventHandler,
                 IID_ICoreWebView2NavigationStartingEventHandler)
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2NavigationStartingEventArgs* args) override {
        return fn_(sender, args);
    }
    Fn fn_;
};

struct NavigationCompletedHandler : ICoreWebView2NavigationCompletedEventHandler {
    typedef std::function<HRESULT(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*)> Fn;
    explicit NavigationCompletedHandler(Fn fn) : fn_(std::move(fn)) {}
    NPP_COM_BODY(ICoreWebView2NavigationCompletedEventHandler,
                 IID_ICoreWebView2NavigationCompletedEventHandler)
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2NavigationCompletedEventArgs* args) override {
        return fn_(sender, args);
    }
    Fn fn_;
};

struct NewWindowHandler : ICoreWebView2NewWindowRequestedEventHandler {
    typedef std::function<HRESULT(ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*)> Fn;
    explicit NewWindowHandler(Fn fn) : fn_(std::move(fn)) {}
    NPP_COM_BODY(ICoreWebView2NewWindowRequestedEventHandler,
                 IID_ICoreWebView2NewWindowRequestedEventHandler)
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2NewWindowRequestedEventArgs* args) override {
        return fn_(sender, args);
    }
    Fn fn_;
};

struct ResourceHandler : ICoreWebView2WebResourceRequestedEventHandler {
    typedef std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*)> Fn;
    explicit ResourceHandler(Fn fn) : fn_(std::move(fn)) {}
    NPP_COM_BODY(ICoreWebView2WebResourceRequestedEventHandler,
                 IID_ICoreWebView2WebResourceRequestedEventHandler)
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2WebResourceRequestedEventArgs* args) override {
        return fn_(sender, args);
    }
    Fn fn_;
};

struct PdfHandler : ICoreWebView2PrintToPdfCompletedHandler {
    typedef std::function<HRESULT(HRESULT, BOOL)> Fn;
    explicit PdfHandler(Fn fn) : fn_(std::move(fn)) {}
    NPP_COM_BODY(ICoreWebView2PrintToPdfCompletedHandler, IID_ICoreWebView2PrintToPdfCompletedHandler)
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, BOOL ok) override { return fn_(hr, ok); }
    Fn fn_;
};

struct CaptureHandler : ICoreWebView2CapturePreviewCompletedHandler {
    typedef std::function<HRESULT(HRESULT)> Fn;
    explicit CaptureHandler(Fn fn) : fn_(std::move(fn)) {}
    NPP_COM_BODY(ICoreWebView2CapturePreviewCompletedHandler,
                 IID_ICoreWebView2CapturePreviewCompletedHandler)
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr) override { return fn_(hr); }
    Fn fn_;
};

}  // namespace

// ------------------------------------------------------------------- Impl --

struct WebViewHost::Impl {
    WebViewHost* owner = nullptr;
    HWND parent = nullptr;
    HMODULE loader = nullptr;
    PFN_CreateEnv createEnv = nullptr;

    ICoreWebView2Environment* env = nullptr;
    ICoreWebView2Controller* controller = nullptr;
    ICoreWebView2Controller2* controller2 = nullptr;
    ICoreWebView2* webview = nullptr;

    bool ready = false;
    bool creating = false;   // controller creation in flight
    bool dark = false;
    bool failed = false;
    int width = 0;
    int height = 0;
    std::wstring docFolder;
    std::wstring webRoot;

    void Fail(const std::wstring& text) {
        failed = true;
        if (owner->onError) owner->onError(text);
    }

    // WebView2 bounds are DIPs, while our client rect is in physical pixels.
    // On a scaled display (125% etc.) feeding pixels straight in makes the page
    // render at scale x and get clipped at the right/bottom edges.
    static double ScaleFor(HWND hwnd) {
        typedef UINT(WINAPI * PfnGetDpiForWindow)(HWND);
        static PfnGetDpiForWindow fn = nullptr;
        static bool looked = false;
        if (!looked) {
            looked = true;
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32) fn = (PfnGetDpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
        }
        if (fn && hwnd) {
            UINT dpi = fn(hwnd);
            if (dpi >= 48) return (double)dpi / 96.0;
        }
        return 1.0;
    }

    void ApplyBounds() {
        if (!controller) return;
        double scale = ScaleFor(parent);
        // The controller window is sized in device pixels; the page inside is
        // laid out in DIPs, so the rasterization scale has to match the monitor
        // scale or the content ends up smaller than the dock.
        RECT rc = {0, 0, width, height};
        controller->put_Bounds(rc);

        ICoreWebView2Controller3* c3 = nullptr;
        if (SUCCEEDED(controller->QueryInterface(IID_ICoreWebView2Controller3, (void**)&c3)) && c3) {
            c3->put_ShouldDetectMonitorScaleChanges(FALSE);
            c3->put_RasterizationScale(scale);
            c3->Release();
        }
    }

    void ApplyBackground() {
        if (!controller2) return;
        COREWEBVIEW2_COLOR color = {};
        color.A = 255;
        if (dark) {
            color.R = 13;
            color.G = 17;
            color.B = 23;
        } else {
            color.R = 255;
            color.G = 255;
            color.B = 255;
        }
        controller2->put_DefaultBackgroundColor(color);
    }

    HRESULT CreateController() {
        if (!env) return E_FAIL;
        if (creating) return S_FALSE;
        creating = true;
        ControllerHandler::Fn fn = [this](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
            creating = false;
            if (FAILED(hr) || !c) {
                Fail(mdtr::T(L"WebView2 控制器创建失败。", L"Could not create the WebView2 controller."));
                return hr;
            }
            WV_TRACE("[wv] controller created\n");
            c->AddRef();
            controller = c;
            controller->QueryInterface(IID_ICoreWebView2Controller2, (void**)&controller2);
            controller->get_CoreWebView2(&webview);
            if (!webview) {
                Fail(mdtr::T(L"WebView2 初始化失败。", L"WebView2 initialisation failed."));
                return E_FAIL;
            }

            controller->put_IsVisible(FALSE);  // shown once the first render is ready
            ApplyBackground();

            ICoreWebView2Settings* settings = nullptr;
            if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
                settings->put_AreDefaultContextMenusEnabled(FALSE);
                settings->put_AreDevToolsEnabled(TRUE);
                settings->put_IsStatusBarEnabled(FALSE);
                settings->put_IsZoomControlEnabled(TRUE);
                ICoreWebView2Settings3* settings3 = nullptr;
                if (SUCCEEDED(settings->QueryInterface(IID_ICoreWebView2Settings3, (void**)&settings3)) &&
                    settings3) {
                    settings3->put_AreBrowserAcceleratorKeysEnabled(TRUE);
                    settings3->Release();
                }
                settings->Release();
            }

            EventRegistrationToken token = {};

            MessageHandler::Fn onMsg = [this](ICoreWebView2*,
                                              ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR json = nullptr;
                if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                    std::wstring msg(json);
                    CoTaskMemFree(json);
                    HandleMessage(msg);
                }
                return S_OK;
            };
            MessageHandler* mh = new MessageHandler(onMsg);
            webview->add_WebMessageReceived(mh, &token);
            mh->Release();

            struct TitleHandler : ICoreWebView2DocumentTitleChangedEventHandler {
                NPP_COM_BODY(ICoreWebView2DocumentTitleChangedEventHandler, IID_ICoreWebView2DocumentTitleChangedEventHandler)
                HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, IUnknown*) override {
                    LPWSTR title = nullptr;
                    if (sender && SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                        WV_TRACE("[wv] title: %ls\n", title);
                        CoTaskMemFree(title);
                    }
                    return S_OK;
                }
            };
            TitleHandler* th = new TitleHandler();
            webview->add_DocumentTitleChanged(th, &token);
            th->Release();

            NavigationCompletedHandler::Fn onNav = [this](ICoreWebView2*,
                                                          ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL ok = FALSE;
                args->get_IsSuccess(&ok);
                LONG status = 0;
                args->get_WebErrorStatus((COREWEBVIEW2_WEB_ERROR_STATUS*)&status);
                WV_TRACE("[wv] navigation completed ok=%d status=%ld\n", (int)ok, status);
                mdlog::Line(L"navigation completed ok=%d status=%ld", (int)ok, (long)status);
                if (ok) {
                    ready = true;
                    ApplyBounds();
                    if (owner->onReady) owner->onReady();
                }
                return S_OK;
            };
            NavigationCompletedHandler* nh = new NavigationCompletedHandler(onNav);
            webview->add_NavigationCompleted(nh, &token);
            nh->Release();

            NavigationStartingHandler::Fn onNavStart = [this](ICoreWebView2*,
                                                              ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                std::wstring url = uri ? uri : L"";
                if (uri) CoTaskMemFree(uri);
                WV_TRACE("[wv] navstarting url=%ls\n", url.c_str());
                if (url.compare(0, 13, L"https://nppmd") == 0) return S_OK;
                if (url.compare(0, 18, L"https://mdassets") == 0) return S_OK;
                args->put_Cancel(TRUE);
                if (url.compare(0, 4, L"http") == 0 && owner->onMessage) {
                    std::wstring msg = L"{\"type\":\"openExternal\",\"url\":\"" + url + L"\"}";
                    owner->onMessage(msg);
                }
                return S_OK;
            };
            NavigationStartingHandler* nsh = new NavigationStartingHandler(onNavStart);
            webview->add_NavigationStarting(nsh, &token);
            nsh->Release();

            NewWindowHandler::Fn onNewWindow = [this](ICoreWebView2*,
                                                      ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                std::wstring url = uri ? uri : L"";
                if (uri) CoTaskMemFree(uri);
                args->put_Handled(TRUE);
                if (owner->onMessage && !url.empty()) {
                    std::wstring msg = L"{\"type\":\"openExternal\",\"url\":\"" + url + L"\"}";
                    owner->onMessage(msg);
                }
                return S_OK;
            };
            NewWindowHandler* nwh = new NewWindowHandler(onNewWindow);
            webview->add_NewWindowRequested(nwh, &token);
            nwh->Release();

            ResourceHandler::Fn onResource =
                [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                ServeResource(args);
                return S_OK;
            };
            ResourceHandler* rh = new ResourceHandler(onResource);
            webview->add_WebResourceRequested(rh, &token);
            rh->Release();
            webview->AddWebResourceRequestedFilter(L"https://mdassets.local/*",
                                                   COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

            ICoreWebView2_3* wv3 = nullptr;
            if (SUCCEEDED(webview->QueryInterface(IID_ICoreWebView2_3, (void**)&wv3)) && wv3) {
                HRESULT mhr = wv3->SetVirtualHostNameToFolderMapping(kAppHost, webRoot.c_str(),
                                                       COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                WV_TRACE("[wv] vhost mapping hr=0x%08lx folder=%ls\n", (unsigned long)mhr, webRoot.c_str());
                wv3->Release();
            }

            ApplyBounds();
            // Bust the cache for index.html itself: assets carry a ?v= token, but the entry
            // document keeps a stable URL and would otherwise survive upgrades in the profile cache.
            std::wstring startUrl = kAppUrl;
            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            std::wstring indexPath = webRoot + L"\\index.html";
            if (GetFileAttributesExW(indexPath.c_str(), GetFileExInfoStandard, &fad)) {
                ULARGE_INTEGER ft = {};
                ft.LowPart = fad.ftLastWriteTime.dwLowDateTime;
                ft.HighPart = fad.ftLastWriteTime.dwHighDateTime;
                startUrl += L"?v=" + std::to_wstring((unsigned long long)ft.QuadPart);
            }
#ifdef WEBVIEW_TRACE
            wchar_t overrideUrl[2048] = {};
            if (GetEnvironmentVariableW(L"NPP_MD_START_URL", overrideUrl, 2048) > 0) startUrl = overrideUrl;
#endif
            HRESULT nhr = webview->Navigate(startUrl.c_str());
            WV_TRACE("[wv] navigate hr=0x%08lx\n", (unsigned long)nhr);
            mdlog::Line(L"navigate hr=0x%08lx", (unsigned long)nhr);
            return S_OK;
        };

        ControllerHandler* handler = new ControllerHandler(fn);
        HRESULT hr = env->CreateCoreWebView2Controller(parent, handler);
        handler->Release();
        if (FAILED(hr)) creating = false;
        mdlog::Line(L"create controller hr=0x%08lx", (unsigned long)hr);
        return hr;
    }

    void HandleMessage(const std::wstring& json) {
        WV_TRACE("[wv] message: %.200ls\n", json.c_str());
        if (!owner->onMessage) return;
        owner->onMessage(json);
    }

    bool LoadAsset(const std::wstring& url, std::vector<BYTE>& data, std::wstring& mime) {
        const std::wstring prefix = std::wstring(L"https://") + kAssetHost + L"/r?p=";
        if (url.compare(0, prefix.size(), prefix) != 0) return false;
        std::wstring path = PercentDecode(url.substr(prefix.size()));
        if (path.empty()) return false;
        // strip fragment
        size_t hash = path.find(L'#');
        if (hash != std::wstring::npos) path = path.substr(0, hash);

        if (!IsAbsolutePath(path)) {
            if (docFolder.empty()) return false;
            if (!docFolder.empty() && docFolder.back() != L'\\' && docFolder.back() != L'/') path = docFolder + L"\\" + path;
            else path = docFolder + path;
        }
        wchar_t full[MAX_PATH * 4] = {};
        if (!GetFullPathNameW(path.c_str(), (DWORD)(sizeof(full) / sizeof(full[0])), full, nullptr)) return false;
        std::wstring fullPath(full);
        mime = MimeForPath(fullPath);
        if (mime.empty()) return false;
        return ReadWholeFile(fullPath, data);
    }

    void ServeResource(ICoreWebView2WebResourceRequestedEventArgs* args) {
        ICoreWebView2WebResourceRequest* request = nullptr;
        if (FAILED(args->get_Request(&request)) || !request) return;
        LPWSTR uri = nullptr;
        request->get_Uri(&uri);
        std::wstring url = uri ? uri : L"";
        if (uri) CoTaskMemFree(uri);
        request->Release();

        std::vector<BYTE> data;
        std::wstring mime;
        bool ok = LoadAsset(url, data, mime);

        std::wstring headers;
        HRESULT status = 200;
        const wchar_t* reason = L"OK";
        IStream* stream = nullptr;

        if (ok) {
            headers = L"Content-Type: " + mime +
                      L"\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n";
            stream = StreamFromBytes(data);
        } else {
            headers = L"Content-Type: text/plain; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\n";
            status = 404;
            reason = L"Not Found";
        }
        if (!env) {
            if (stream) stream->Release();
            return;
        }
        ICoreWebView2WebResourceResponse* response = nullptr;
        if (SUCCEEDED(env->CreateWebResourceResponse(stream, status, reason, headers.c_str(), &response)) &&
            response) {
            args->put_Response(response);
            response->Release();
        }
        if (stream) stream->Release();
    }
};

// ------------------------------------------------------------------- API ---

WebViewHost::WebViewHost() : impl_(new Impl()) { impl_->owner = this; }

WebViewHost::~WebViewHost() {
    Destroy();
    delete impl_;
}

bool WebViewHost::Create(HWND parent, const std::wstring& pluginDir, const std::wstring& userDataDir,
                         const std::wstring& webRoot) {
    impl_->parent = parent;
    impl_->webRoot = webRoot;

    std::wstring loaderPath = pluginDir;
    if (!loaderPath.empty() && loaderPath.back() != L'\\' && loaderPath.back() != L'/') loaderPath += L'\\';
    loaderPath += L"WebView2Loader.dll";

    impl_->loader = LoadLibraryW(loaderPath.c_str());
    if (!impl_->loader) impl_->loader = LoadLibraryW(L"WebView2Loader.dll");
    if (!impl_->loader) {
        impl_->Fail(
            mdtr::T(L"未找到 WebView2Loader.dll。\n请重新安装插件（该文件应与 MarkdownPreview.dll 位于同一目录）。", L"WebView2Loader.dll was not found.\nReinstall the plugin (the file must sit next to MarkdownPreview.dll)."));
        return false;
    }
    impl_->createEnv = (PFN_CreateEnv)GetProcAddress(impl_->loader, "CreateCoreWebView2EnvironmentWithOptions");
    if (!impl_->createEnv) {
        impl_->Fail(mdtr::T(L"WebView2Loader.dll 不完整：缺少 CreateCoreWebView2EnvironmentWithOptions。", L"WebView2Loader.dll is incomplete: CreateCoreWebView2EnvironmentWithOptions is missing."));
        return false;
    }

    // Keep the embedded browser quiet and predictable.
    EnvOptions* options = new EnvOptions(
        L"--disable-features=msWebOOUI,msPdfOOUI --disable-background-networking --no-first-run");

    EnvHandler::Fn fn = [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        WV_TRACE("[wv] env hr=0x%08lx\n", (unsigned long)hr);
        mdlog::Line(L"environment hr=0x%08lx", (unsigned long)hr);
        if (FAILED(hr) || !env) {
            impl_->Fail(mdtr::English()
                ? L"Could not create a WebView2 environment.\n\n"
                  L"Two common causes:\n"
                  L"\u2022 The \u201cMicrosoft Edge WebView2 Runtime\u201d is missing (usually preinstalled on Windows 10/11; otherwise install the Evergreen runtime from Microsoft);\n"
                  L"\u2022 Another Notepad++ window is already using the same preview profile — close it and retry."
                : L"无法创建 WebView2 环境。\n\n"
                  L"常见原因有两个：\n"
                  L"• 系统缺少 “Microsoft Edge WebView2 运行时”（Windows 10/11 通常已自带，"
                  L"可从微软官网下载 Evergreen 版本）；\n"
                  L"• 已经有一个 Notepad++ 窗口在使用同一个预览配置目录——关掉其它窗口后重试即可。");
            return hr;
        }
        env->AddRef();
        impl_->env = env;
        return impl_->CreateController();
    };
    EnvHandler* handler = new EnvHandler(fn);
    HRESULT hr = impl_->createEnv(nullptr, userDataDir.c_str(), nullptr, handler);
    handler->Release();
    options->Release();

    if (FAILED(hr)) {
        impl_->Fail((mdtr::English() ? L"Creating the WebView2 environment failed (HRESULT 0x" : L"WebView2 环境创建调用失败（HRESULT 0x") + std::to_wstring((unsigned)hr) + L").");
        return false;
    }
    return true;
}

void WebViewHost::Destroy() {
    if (!impl_) return;
    ReleasePage();
    if (impl_->env) {
        impl_->env->Release();
        impl_->env = nullptr;
    }
    if (impl_->loader) {
        FreeLibrary(impl_->loader);
        impl_->loader = nullptr;
    }
}

void WebViewHost::ReleasePage() {
    if (!impl_) return;
    if (impl_->controller) {
        impl_->controller->Close();
    }
    if (impl_->webview) {
        impl_->webview->Release();
        impl_->webview = nullptr;
    }
    if (impl_->controller2) {
        impl_->controller2->Release();
        impl_->controller2 = nullptr;
    }
    if (impl_->controller) {
        impl_->controller->Release();
        impl_->controller = nullptr;
    }
    impl_->ready = false;
    impl_->creating = false;
}



void WebViewHost::Resize(int widthPixels, int heightPixels) {
    impl_->width = widthPixels > 0 ? widthPixels : 0;
    impl_->height = heightPixels > 0 ? heightPixels : 0;
    impl_->ApplyBounds();
}

void WebViewHost::Focus() {
    if (impl_->controller) impl_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

bool WebViewHost::IsReady() const { return impl_->ready; }

bool WebViewHost::IsAvailable() const { return impl_->webview != nullptr; }

void WebViewHost::PostJson(const std::wstring& json) {
    if (impl_->webview) impl_->webview->PostWebMessageAsJson(json.c_str());
}

void WebViewHost::SetVisible(bool visible) {
    if (impl_->controller) impl_->controller->put_IsVisible(visible ? TRUE : FALSE);
}

void WebViewHost::SetTheme(bool dark) {
    impl_->dark = dark;
    impl_->ApplyBackground();
    PostJson(std::wstring(L"{\"type\":\"theme\",\"dark\":") + (dark ? L"true" : L"false") + L"}");
}

void WebViewHost::SetDocFolder(const std::wstring& folder) { impl_->docFolder = folder; }

bool WebViewHost::PrintToPdf(const std::wstring& path) {
    ICoreWebView2_7* wv7 = nullptr;
    if (!impl_->webview || FAILED(impl_->webview->QueryInterface(IID_ICoreWebView2_7, (void**)&wv7)) || !wv7)
        return false;
    PdfHandler::Fn fn = [this](HRESULT, BOOL ok) -> HRESULT {
        if (onPdfDone) onPdfDone(ok != FALSE);
        return S_OK;
    };
    PdfHandler* handler = new PdfHandler(fn);
    HRESULT hr = wv7->PrintToPdf(path.c_str(), nullptr, handler);
    handler->Release();
    wv7->Release();
    return SUCCEEDED(hr);
}

bool WebViewHost::ShowPrintUi() {
    ICoreWebView2_16* wv16 = nullptr;
    if (!impl_->webview || FAILED(impl_->webview->QueryInterface(IID_ICoreWebView2_16, (void**)&wv16)) || !wv16)
        return false;
    HRESULT hr = wv16->ShowPrintUI(COREWEBVIEW2_PRINT_DIALOG_KIND_SYSTEM);
    wv16->Release();
    return SUCCEEDED(hr);
}

void WebViewHost::OpenDevTools() {
    if (impl_->webview) impl_->webview->OpenDevToolsWindow();
}

bool WebViewHost::CapturePng(const std::wstring& path) {
    if (!impl_->webview) return false;

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, 0);
    if (!mem) return false;
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream)) || !stream) {
        GlobalFree(mem);
        return false;
    }

    CaptureHandler::Fn fn = [this, stream, path](HRESULT hr) -> HRESULT {
        bool ok = SUCCEEDED(hr);
        if (ok) {
            HGLOBAL h = nullptr;
            if (SUCCEEDED(GetHGlobalFromStream(stream, &h)) && h) {
                SIZE_T size = GlobalSize(h);
                void* data = GlobalLock(h);
                if (data && size > 0) {
                    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (file != INVALID_HANDLE_VALUE) {
                        DWORD written = 0;
                        ok = WriteFile(file, data, (DWORD)size, &written, nullptr) && written == size;
                        CloseHandle(file);
                    } else {
                        ok = false;
                    }
                }
                if (data) GlobalUnlock(h);
            }
        }
        stream->Release();
        if (onCaptureDone) onCaptureDone(ok);
        return S_OK;
    };

    CaptureHandler* handler = new CaptureHandler(fn);
    HRESULT hr = impl_->webview->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream, handler);
    handler->Release();
    return SUCCEEDED(hr);
}
