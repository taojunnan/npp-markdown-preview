// Very small UI-language helper: the plugin ships Chinese and English strings
// and picks one at load time (system locale, overridable via the ini file).
#pragma once

#include <windows.h>
#include <string>

namespace mdtr {

inline bool& English() {
    static bool value = false;
    return value;
}

inline const wchar_t* T(const wchar_t* zh, const wchar_t* en) { return English() ? en : zh; }

inline bool IsChineseLangId(LANGID id) { return PRIMARYLANGID(id) == LANG_CHINESE; }

// override: "en" / "zh" from the config file, anything else means "auto".
inline void Detect(const std::wstring& override) {
    if (override == L"en") {
        English() = true;
        return;
    }
    if (override == L"zh") {
        English() = false;
        return;
    }
    English() = !IsChineseLangId(GetUserDefaultUILanguage());
}

}  // namespace mdtr
