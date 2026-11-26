// Minimal shim for the MIDL-generated EventToken.h that the WebView2 NuGet
// package omits.  The SDK headers include it as "EventToken.h"; shipping it in
// src\sdk makes a clean checkout buildable (build.ps1 puts src on the include
// path), without patching files inside the downloaded SDK.
#pragma once
#ifndef NPP_WEBVIEW2_EVENTTOKEN_SHIM
#define NPP_WEBVIEW2_EVENTTOKEN_SHIM
typedef struct EventRegistrationToken { __int64 value; } EventRegistrationToken;
#endif
