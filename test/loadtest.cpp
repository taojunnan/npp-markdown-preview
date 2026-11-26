// Minimal native smoke test: load the plugin DLL the way Notepad++ does.
#include <windows.h>
#include <cstdio>

typedef const wchar_t* (__cdecl* PFN_NAME)();
typedef BOOL(__cdecl* PFN_UNICODE)();
typedef void* (__cdecl* PFN_FUNCS)(int*);

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: loadtest <dll>\n");
        return 2;
    }
    wchar_t path[MAX_PATH * 2] = {};
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH * 2);

    HMODULE h = LoadLibraryW(path);
    if (!h) {
        printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }
    printf("loaded\n");

    PFN_NAME getName = (PFN_NAME)GetProcAddress(h, "getName");
    PFN_UNICODE isUnicode = (PFN_UNICODE)GetProcAddress(h, "isUnicode");
    PFN_FUNCS getFuncs = (PFN_FUNCS)GetProcAddress(h, "getFuncsArray");
    if (!getName || !isUnicode || !getFuncs) {
        printf("missing exports\n");
        return 1;
    }
    wprintf(L"getName: %s\n", getName());
    wprintf(L"isUnicode: %d\n", (int)isUnicode());
    int n = 0;
    void* items = getFuncs(&n);
    wprintf(L"funcs: %d at %p\n", n, items);

    fflush(stdout);
    FreeLibrary(h);
    printf("unloaded\n");
    return 0;
}
