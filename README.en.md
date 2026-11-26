# Markdown Preview for Notepad++

English | [中文](README.md)

A Markdown preview panel for Notepad++ that looks like Typora / GitHub: docked on the right, live preview while you type — real typography, syntax highlighting, tables, math and local images.

Notepad++'s built-in Markdown highlighting is fine for editing; this panel is for reading.

![Light preview](docs/shots/preview-light.png)

Dark:

![Dark preview](docs/shots/preview-dark.png)

## Features

- **Typography** — built on [github-markdown-css](https://github.com/sindresorhus/github-markdown-css): an ~840 px reading column, 1.75 line height, close to GitHub / Typora
- **Syntax highlighting** — [highlight.js](https://highlightjs.org/) with automatic language detection, GitHub light/dark themes
- **Math** — [KaTeX](https://katex.org/): inline `$...$` and display `$$...$$`
- **Tables, task lists, footnotes, strikethrough** — fully supported; wide tables scroll horizontally
- **Local images** — relative paths resolve against the Markdown file's folder; absolute paths, SVG and emoji work too
- **Table of contents** — auto-generated on the left, highlights the current section while scrolling, click to jump
- **Scroll sync** — editor → preview is interpolated by heading lines; preview → editor works too
- **Themes** — light / dark / follow editor, one click from the panel toolbar
- **Export** — HTML (single file, images inlined), PDF, print
- **Light on resources** — the panel starts closed and idle; nothing runs until you open it. The browser processes are released 30 s after the panel is hidden (~180 MB back)

The panel is toggled from the `M` button at the far right of the toolbar:

![Toolbar button](docs/shots/toolbar-button.png)

## Requirements

- Windows 10 / 11 x64
- Notepad++ 8.x (tested on 8.8.5)
- **WebView2 Runtime** — bundled with Windows 11; on Windows 10 it is usually installed with Edge. If missing, the panel tells you and you can install the [Evergreen Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)

## Install

### Option 1: prebuilt package (recommended)

1. Download [npp-markdown-preview-v1.0.0.zip](https://github.com/taojunnan/npp-markdown-preview/raw/main/release/npp-markdown-preview-v1.0.0.zip) (~1.4 MB)
2. Extract it into the `plugins\` folder of your Notepad++ installation, so that you end up with:

   ```
   <Notepad++>\plugins\MarkdownPreview\MarkdownPreview.dll
   <Notepad++>\plugins\MarkdownPreview\WebView2Loader.dll
   <Notepad++>\plugins\MarkdownPreview\web\...
   ```

3. Restart Notepad++, open any `.md` file and click the `M` button at the far right of the toolbar (or `Plugins → Markdown 预览 → 显示 / 隐藏预览面板`, shortcut `Ctrl+Alt+P`)

> Portable installs work the same way — extract into `Notepad++\plugins\`.

### Option 2: let an AI agent install it

With an agent that can read and write files on your machine (Claude Code, Codex CLI, Cursor, Windsurf…), paste the prompt below as-is. It will locate Notepad++, check the runtime, install the plugin and verify it:

```text
Please install the "Markdown Preview" plugin for Notepad++ on this machine.
Work through the steps in order and report the result of each one briefly:

1. Locate Notepad++: run `reg query "HKLM\SOFTWARE\Notepad++" /ve` — the default value
   is the install directory. If it is missing, ask me (typical paths:
   C:\Program Files\Notepad++ or D:\ProgrameFiles\Notepad++).
2. Check the WebView2 runtime:
   `reg query "HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v pv`
   A pv value means it is installed; otherwise tell me to install the Evergreen Runtime
   from https://developer.microsoft.com/microsoft-edge/webview2/ first.
3. If notepad++.exe is running, ask me to save my work and close it before continuing.
4. Download the package to a temp folder:
   https://github.com/taojunnan/npp-markdown-preview/raw/main/release/npp-markdown-preview-v1.0.0.zip
   If GitHub is unreachable, retry through a mirror or proxy; if that also fails,
   stop and tell me instead of installing something from an unknown source.
5. Extract it and copy the whole MarkdownPreview folder into <Notepad++ install dir>\plugins\.
   You should end up with: plugins\MarkdownPreview\MarkdownPreview.dll, WebView2Loader.dll, web\.
6. Start notepad++.exe, open any .md file (create one if needed), press Ctrl+Alt+P and
   verify that the Markdown preview panel appears on the right and renders correctly.
7. Report back: install path, WebView2 version, whether the panel works. On failure,
   send me the exact error message.
```

### Option 3: build from source

> Every `v*` tag is built by GitHub Actions and published under [Releases](https://github.com/taojunnan/npp-markdown-preview/releases), so you can simply grab the zip from there.

You need [Zig](https://ziglang.org/download/) 0.13+ (used as clang + MinGW-w64 headers, **no Visual Studio required**) and Node.js (only to fetch dependencies once).

```powershell
git clone git@github.com:taojunnan/npp-markdown-preview.git
cd npp-markdown-preview

# 1. fetch build dependencies: WebView2 SDK (NuGet) + front-end libraries (npm), ~30 MB, once
powershell -ExecutionPolicy Bypass -File .\scripts\fetch-deps.ps1

# 2. build and install into Notepad++
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Zig "C:\path\to\zig.exe"
```

`build.ps1` options:

```powershell
.\build.ps1 -NoInstall                       # build only, output in dist\MarkdownPreview\
.\build.ps1 -NppDir "D:\Portable\Notepad++"  # install elsewhere
.\build.ps1 -Zig "C:\tools\zig\zig.exe"      # Zig not on the default path
```

## Usage

- Toggle the panel with the `M` toolbar button, `Plugins → Markdown 预览 → 显示 / 隐藏预览面板`, or `Ctrl+Alt+P`
- Image paths are resolved relative to the **current Markdown file**
- The panel's open state lasts for the current session only; it starts closed after a restart
- Edits re-render after a 120 ms debounce, so typing is not interrupted
- See the panel-buttons table under [Configuration](#configuration) for what each toolbar control does

### Keyboard shortcuts

| Shortcut | Action | Scope |
| --- | --- | --- |
| `Ctrl+Alt+P` | show / hide the preview panel | Notepad++ |
| `Ctrl+Alt+R` | refresh the preview | Notepad++ |
| `F5` | refresh the preview | panel |
| `Ctrl+=` / `Ctrl+-` | zoom in / out | panel |
| `Ctrl+Shift+C` | copy the current selection | panel |
| `Esc` | close menus / dismiss the image lightbox | panel |

## Configuration

There are three layers, highest priority first: **panel buttons / menu** (applied immediately and written back) → **config file** (read at startup).

### 1. Plugin menu: `Plugins → Markdown 预览`

| Menu item | Effect | Config key |
| --- | --- | --- |
| 显示 / 隐藏预览面板 | show or hide the panel (`Ctrl+Alt+P`) | — |
| 刷新预览 | re-render the current document (`Ctrl+Alt+R`) | — |
| 打开 Markdown 时自动预览 | show the panel automatically when a `.md` file is opened | `autoOpen` |
| 预加载预览（打开更快） | warm up WebView2 in the background for instant opening (~180 MB resident) | `preload` |
| 关闭预览后释放内存 | release the browser processes 30 s after the panel is hidden | `releaseOnHide` |
| 在浏览器中打开预览 | open the rendered document in the default browser | — |
| 导出为 HTML… / 导出为 PDF… | export the current document | — |
| 打印… | open the system print dialog | — |
| 关于 Markdown 预览 | version information | — |

The three checkable items — auto-preview, preload, release-on-hide — write to the config file as soon as you toggle them.

### 2. Panel buttons: top right of the panel

| Control | Effect | Config key |
| --- | --- | --- |
| TOC icon | show / hide the contents column | `toc` |
| sun / moon / half-circle | light / dark / follow editor | `theme` |
| `A−` / `A+` | body font size ±1 px (`Ctrl+-` / `Ctrl+=`) | `fontSize` |
| `⋯ → 滚动同步：开 / 关` | link editor and preview scrolling | `sync` |
| `⋯ → 同步到光标位置` | scroll the preview to the caret's section | — |

Font size, theme, TOC and scroll sync are remembered across sessions.

### 3. Config file

Path: `%APPDATA%\Notepad++\plugins\config\MarkdownPreview\MarkdownPreview.ini`

```ini
[general]
autoOpen=0
preload=0
releaseOnHide=1
[appearance]
theme=auto
fontSize=16
toc=1
sync=1
```

| Key | Default | Values | Meaning |
| --- | --- | --- | --- |
| `autoOpen` | `0` | `0` / `1` | show the panel automatically when a Markdown file is opened |
| `preload` | `0` | `0` / `1` | `1` = warm up WebView2 in the background (instant open, ~180 MB resident); `0` = saves memory, ~1.5 s to first content |
| `releaseOnHide` | `1` | `0` / `1` | `1` = destroy the WebView2 processes 30 s after the panel is hidden; `0` = keep them for a faster reopen |
| `theme` | `auto` | `auto` / `light` / `dark` | panel theme; `auto` follows Notepad++'s light/dark appearance |
| `fontSize` | `16` | `12` – `24` | body font size in px; the `A−` / `A+` buttons change this |
| `toc` | `1` | `0` / `1` | show the contents column |
| `sync` | `1` | `0` / `1` | synchronised scrolling between editor and preview |
| `debug` | `0` | `0` / `1` | write startup, navigation and render timings to `preview.log` |

Hand-edited values take effect after restarting Notepad++; changes made from the menu or panel buttons apply immediately and are written back to the file.

### Troubleshooting: turn the log on

Set `debug` to `1` and restart Notepad++. The plugin then writes:

```
%APPDATA%\Notepad++\plugins\config\MarkdownPreview\preview.log
```

It records the environment/controller HRESULTs, navigation result, time to first frame, when the engine was released,
and the timings the page reports (`dom=` / `rspEnd=` / `load=`). Attach that file when filing an issue. The log is
cleared automatically once it passes 512 KB.

## Uninstall

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1
```

Or simply delete `<Notepad++>\plugins\MarkdownPreview\` and `%APPDATA%\Notepad++\plugins\config\MarkdownPreview\`.

## Repository layout

```
src\plugin.cpp        plugin entry points, docked panel, editor sync, menu, toolbar button, settings
src\webview.cpp       WebView2 host (COM callbacks, resource channel, print / PDF)
src\webview.h
src\sdk\              official PluginInterface.h / Docking.h / Scintilla.h
web\index.html        panel skeleton markup
web\style.css         typography and themes
web\app.js            rendering, TOC, scroll sync, export
scripts\fetch-deps.ps1 downloads build dependencies (WebView2 SDK + npm packages)
test\                 sample documents and a standalone WebView2 host for screenshots / PDF checks
docs\shots\           README screenshots
release\              prebuilt package
```

Front-end libraries (marked, DOMPurify, highlight.js, KaTeX, github-markdown-css) are copied from
`_build\js` into `dist\...\web\vendor\` at build time, so the plugin runs fully offline.

## How it works

- Native C++ plugin; the panel is rendered by WebView2 (Chromium): `marked` parses → `DOMPurify` sanitizes → `highlight.js` highlights → `KaTeX` typesets math
- Panel assets are served through `SetVirtualHostNameToFolderMapping`; images referenced by the document are intercepted with `WebResourceRequested` and read from the file system — no HTTP involved
- Editor → preview sync interpolates by heading lines instead of a naive percentage; preview → editor uses `SCI_SETFIRSTVISIBLELINE`
- PDF export uses `ICoreWebView2_7::PrintToPdf`; HTML export inlines images as data URIs
- On a cold start the WebView2 window stays hidden until its first frame is ready; the host window paints a skeleton in the meantime, so there is no white flash

## Known limitations

- One WebView2 profile per config directory: a second Notepad++ instance cannot share it (the panel explains this; close the other window and retry)
- ~180 MB while the panel is open (inherent to a Chromium engine); released 30 s after it is hidden
- x64 only

## License

[MIT](LICENSE)

## Credits

[marked](https://github.com/markedjs/marked) · [DOMPurify](https://github.com/cure53/DOMPurify) ·
[highlight.js](https://highlightjs.org/) · [KaTeX](https://katex.org/) ·
[github-markdown-css](https://github.com/sindresorhus/github-markdown-css)
