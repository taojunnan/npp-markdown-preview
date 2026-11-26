// Markdown Preview for Notepad++ - WebView2 host
// A thin, dependency-light wrapper around the WebView2 HWND controller.
#pragma once

#include <windows.h>
#include <functional>
#include <string>

class WebViewHost {
public:
    WebViewHost();
    ~WebViewHost();

    // pluginDir: folder that contains WebView2Loader.dll (the plugin folder)
    // userDataDir: writable folder for the WebView2 profile
    // webRoot: folder that is served as https://nppmd.local/
    bool Create(HWND parent,
                const std::wstring& pluginDir,
                const std::wstring& userDataDir,
                const std::wstring& webRoot);

    void Destroy();
    void Resize(int widthPixels, int heightPixels);
    // Keep the browser window hidden until the first render is ready, so the
    // host can paint its own placeholder in the meantime.
    void SetVisible(bool visible);
    void Focus();

    bool IsReady() const;
    bool IsAvailable() const;

    void PostJson(const std::wstring& json);
    void SetTheme(bool dark);
    void SetDocFolder(const std::wstring& folder);

    bool PrintToPdf(const std::wstring& path);
    bool ShowPrintUi();
    void OpenDevTools();

    // Renders the current viewport to a PNG (used by diagnostics/test host).
    bool CapturePng(const std::wstring& path);

    // Callbacks (invoked on the UI thread)
    std::function<void(const std::wstring&)> onMessage;      // JSON message from the page
    std::function<void()> onReady;                            // page finished handshake
    std::function<void(const std::wstring&)> onError;         // fatal setup error, human text
    std::function<void(bool)> onPdfDone;                      // PDF export finished
    std::function<void(bool)> onCaptureDone;                  // PNG capture finished

    struct Impl;

private:
    Impl* impl_;
    WebViewHost(const WebViewHost&);
    WebViewHost& operator=(const WebViewHost&);
};
