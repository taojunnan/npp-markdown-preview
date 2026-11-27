# Markdown 预览 · Notepad++ 插件

[English](README.en.md) | 中文

给 Notepad++ 加一个**像 Typora / GitHub 那样的 Markdown 预览面板**：右侧停靠，编辑即预览，排版、代码高亮、表格、数学公式、本地图片全都对得上。

Notepad++ 自带的 Markdown 高亮只够看源码，这个面板负责「读起来舒服」。

![浅色预览](docs/shots/preview-light.png)

深色：

![深色预览](docs/shots/preview-dark.png)

## 功能

- **排版**：基于 [github-markdown-css](https://github.com/sindresorhus/github-markdown-css)，正文约 840 px 阅读栏宽、行距 1.75，接近 GitHub / Typora 的阅读体验
- **代码高亮**：[highlight.js](https://highlightjs.org/) 自动识别语言，浅色/深色两套 GitHub 配色
- **数学公式**：[KaTeX](https://katex.org/) 行内 `$...$` 与块级 `$$...$$`
- **表格 / 任务列表 / 脚注 / 删除线**：完整支持，表格可横向滚动
- **本地图片**：相对路径按 Markdown 文件所在目录解析，支持绝对路径、SVG、emoji
- **目录（TOC）**：左侧自动生成，随滚动高亮当前章节，点击跳转
- **点标题回源**：点预览里的任意标题，编辑器光标直接跳到对应的源码行；反过来，编辑器里移动光标，左侧目录会跟着高亮当前章节
- **滚动同步**：按块级锚点定位（标题、段落、列表、代码块、表格各自记录源文件行号），预览 → 编辑器反向同步
- **主题**：浅色 / 深色 / 跟随编辑器，面板右上角一键切换
- **中英双语界面**：按系统语言自动切换，也可在配置里锁定
- **导出**：HTML（单文件，图片内联）、PDF、打印
- **轻量**：启动时面板默认关闭，不预热时零常驻内存；关闭面板 30 秒后自动释放浏览器进程（约 180 MB）
- **大文档可用**：渲染路径按文档大小自适应，几百 KB 的文档输入仍然跟手（见下面的实测数据）

打开面板的入口是工具栏最右边的小按钮：

![工具栏按钮](docs/shots/toolbar-button.png)

## 环境要求

- Windows 10 / 11 x64
- Notepad++ 8.x（在 8.8.5 上验证）
- **WebView2 运行时**：Windows 11 自带；Windows 10 一般随 Edge 已安装。缺失时面板会提示，去 [Microsoft Edge WebView2](https://developer.microsoft.com/microsoft-edge/webview2/) 下载 Evergreen Runtime 即可

## 安装

### 方式一：直接下载安装包（推荐）

1. 下载 [npp-markdown-preview.zip](https://github.com/taojunnan/npp-markdown-preview/releases/latest/download/npp-markdown-preview.zip)（约 1.4 MB）
2. 解压到 Notepad++ 安装目录下的 `plugins\`，确保解压后是：

   ```
   <Notepad++>\plugins\MarkdownPreview\MarkdownPreview.dll
   <Notepad++>\plugins\MarkdownPreview\WebView2Loader.dll
   <Notepad++>\plugins\MarkdownPreview\web\...
   ```

3. 重启 Notepad++，打开任意 `.md` 文件，点工具栏最右边的 `M` 按钮（或菜单 `插件 → Markdown 预览 → 显示 / 隐藏预览面板`，快捷键 `Ctrl+Alt+P`）

> 便携版（portable）同理，解压到 `Notepad++\plugins\` 下即可。

### 方式二：让 AI Agent 帮你装

用 Claude Code、Codex CLI、Cursor、Windsurf 这类能读写本机文件的 Agent，把下面这段提示词整段复制给它，它会自己找路径、检查运行时、装好并验证：

```text
请帮我在本机安装 Notepad++ 的「Markdown 预览」插件。按顺序执行，每步简短报告结果：

1. 定位 Notepad++：执行 reg query "HKLM\SOFTWARE\Notepad++" /ve，默认值就是安装目录；
   读不到就问我（常见路径 C:\Program Files\Notepad++ 或 D:\ProgrameFiles\Notepad++）。
2. 检查 WebView2 运行时：reg query "HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" /v pv
   有 pv 输出即已安装；没有则提醒我先去 https://developer.microsoft.com/microsoft-edge/webview2/
   安装 Evergreen Runtime，装完再继续。
3. 如果 notepad++.exe 正在运行，提醒我先保存并关闭；我确认后再退出 Notepad++。
4. 下载安装包到临时目录：
   https://github.com/taojunnan/npp-markdown-preview/releases/latest/download/npp-markdown-preview.zip
   若直连 GitHub 失败，换镜像或代理重试；仍然失败就停下来告诉我，不要安装来路不明的文件。
5. 解压后把里面的 MarkdownPreview 文件夹整体复制到 <Notepad++ 安装目录>\plugins\，
   最终应存在：plugins\MarkdownPreview\MarkdownPreview.dll、WebView2Loader.dll、web\ 目录。
6. 启动 notepad++.exe，打开任意 .md 文件（没有就新建一个），按 Ctrl+Alt+P，
   确认右侧出现 Markdown 预览面板并且内容渲染正常。
7. 最后汇报：安装路径、WebView2 版本、面板是否正常；失败就把报错原文发我。
```

### 方式三：从源码构建

> 每次推 `v*` 标签，GitHub Actions 会自动编译并发布到 [Releases](https://github.com/taojunnan/npp-markdown-preview/releases)，也可以直接下载那里的压缩包。

需要 [Zig](https://ziglang.org/download/) 0.13+（当 clang + MinGW-w64 头文件用，**不需要 Visual Studio**）和 Node.js（仅首次拉依赖）。

```powershell
git clone git@github.com:taojunnan/npp-markdown-preview.git
cd npp-markdown-preview

# 1. 拉取构建依赖：WebView2 SDK（NuGet）+ 前端库（npm），约 30 MB，只需一次
powershell -ExecutionPolicy Bypass -File .\scripts\fetch-deps.ps1

# 2. 编译并安装到 Notepad++
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Zig "C:\path\to\zig.exe"
```

`build.ps1` 支持的参数：

```powershell
.\build.ps1 -NoInstall                       # 只编译，产物在 dist\MarkdownPreview\
.\build.ps1 -NppDir "D:\Portable\Notepad++"  # 安装到别的 Notepad++
.\build.ps1 -Zig "C:\tools\zig\zig.exe"      # Zig 不在默认路径
```

## 使用

- 打开 / 关闭面板：工具栏 `M` 按钮，或菜单 `插件 → Markdown 预览 → 显示 / 隐藏预览面板`（`Ctrl+Alt+P`）
- 图片路径相对于**当前 Markdown 文件**所在目录解析
- 面板打开状态只在本次会话内保持，重启 Notepad++ 后默认关闭
- 修改文档后 120 ms 防抖自动重新渲染，不打断输入
- 面板右上角各按钮（目录、主题、字号、更多菜单、刷新）的用途见下方 [配置](#配置) 一节的「面板按钮」

### 快捷键

| 快捷键 | 作用 | 生效范围 |
| --- | --- | --- |
| `Ctrl+Alt+P` | 显示 / 隐藏预览面板 | Notepad++ 全局 |
| `Ctrl+Alt+R` | 刷新预览 | Notepad++ 全局 |
| `F5` | 刷新预览 | 面板内 |
| `Ctrl+=` / `Ctrl+-` | 放大 / 缩小字号 | 面板内 |
| `Ctrl+Shift+C` | 复制选中的内容 | 面板内 |
| `Esc` | 关闭菜单 / 关闭图片大图 | 面板内 |

在预览里**单击任意标题**，编辑器光标会跳到那行源码（拖动选中文字时不会触发）。

## 配置

设置分三层，优先级从高到低：**面板按钮 / 菜单**（即时生效并写回配置）→ **配置文件**（下次启动生效）。

### 1. 插件菜单：`插件 → Markdown 预览`

| 菜单项 | 作用 | 对应配置 |
| --- | --- | --- |
| 显示 / 隐藏预览面板 | 打开或关闭面板（`Ctrl+Alt+P`） | — |
| 刷新预览 | 重新渲染当前文档（`Ctrl+Alt+R`） | — |
| 打开 Markdown 时自动预览 | 打开 `.md` 文件时自动显示面板 | `autoOpen` |
| 预加载预览（打开更快） | 后台预热 WebView2，打开几乎无延迟（常驻约 180 MB） | `preload` |
| 关闭预览后释放内存 | 面板隐藏 30 秒后释放浏览器进程，把约 180 MB 还回去 | `releaseOnHide` |
| 在浏览器中打开预览 | 用默认浏览器打开当前渲染结果 | — |
| 导出为 HTML… / 导出为 PDF… | 导出当前文档 | — |
| 打印… | 调起系统打印对话框 | — |
| 关于 Markdown 预览 | 版本信息 | — |

带勾选状态的三项：`自动预览`、`预加载`、`关闭后释放内存`——勾选即开启，改完立即写入配置文件。

### 2. 面板按钮：面板右上角

| 控件 | 作用 | 对应配置 |
| --- | --- | --- |
| 目录图标 | 显示 / 收起目录栏 | `toc` |
| 太阳 / 月亮 / 半圆 | 浅色 / 深色 / 跟随编辑器 | `theme` |
| `A−` / `A+` | 正文字号 ±1 px（`Ctrl+-` / `Ctrl+=`） | `fontSize` |
| `⋯ → 滚动同步：开 / 关` | 编辑器与预览是否联动滚动 | `sync` |
| `⋯ → 同步到光标位置` | 手动把预览滚到光标所在章节 | — |

字号、主题、目录、同步这四项点一下就会记住，下次打开沿用。

### 3. 配置文件

路径：`%APPDATA%\Notepad++\plugins\config\MarkdownPreview\MarkdownPreview.ini`

```ini
[general]
autoOpen=0
preload=0
releaseOnHide=1
language=auto
[appearance]
theme=auto
fontSize=16
toc=1
sync=1
```

| 键 | 默认 | 取值 | 说明 |
| --- | --- | --- | --- |
| `autoOpen` | `0` | `0` / `1` | 打开 Markdown 文件时自动显示面板 |
| `preload` | `0` | `0` / `1` | 后台预热 WebView2：`1` 几乎秒开（常驻约 180 MB），`0` 省内存、打开时约 1.5 秒出内容 |
| `releaseOnHide` | `1` | `0` / `1` | 面板隐藏 30 秒后销毁 WebView2 进程；`0` 保留进程，二次打开更快 |
| `theme` | `auto` | `auto` / `light` / `dark` | 面板主题；`auto` 跟随 Notepad++ 当前主题的明暗 |
| `fontSize` | `16` | `12` – `24` | 正文字号（px），面板内 `A−` / `A+` 会改这个值 |
| `toc` | `1` | `0` / `1` | 是否显示左侧目录栏 |
| `sync` | `1` | `0` / `1` | 编辑器与预览的滚动同步 |
| `language` | `auto` | `auto` / `zh` / `en` | 界面语言；`auto` 跟随系统语言 |
| `debug` | `0` | `0` / `1` | 把启动、导航、渲染耗时写进 `preview.log`，排错用 |

手工编辑 ini 后需要重启 Notepad++；用菜单或面板按钮改的会立即生效并写回文件。

### 排错：打开日志

把 `debug` 设为 `1` 后重启 Notepad++，插件会把关键事件写进：

```
%APPDATA%\Notepad++\plugins\config\MarkdownPreview\preview.log
```

内容包括环境创建/控制器创建的 HRESULT、导航结果、页面首帧耗时、释放引擎的时间点，以及页面自己上报的加载耗时
（`dom=` / `rspEnd=` / `load=`）。反馈问题时把这份日志贴上来即可。日志超过 512 KB 会自动清空。

## 卸载

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1
```

或直接删除 `<Notepad++>\plugins\MarkdownPreview\` 与 `%APPDATA%\Notepad++\plugins\config\MarkdownPreview\`。

## 目录结构

```
src\plugin.cpp         Notepad++ 插件接口、停靠面板、编辑器同步、菜单、工具栏按钮、设置
src\webview.cpp        WebView2 宿主（COM 回调、资源通道、打印 / PDF）
src\webview.h
src\sdk\               官方 PluginInterface.h / Docking.h / Scintilla.h
web\index.html         面板骨架
web\style.css          排版与主题
web\app.js             渲染、目录、滚动同步、导出
scripts\fetch-deps.ps1 拉取构建依赖（WebView2 SDK + npm 包）
test\                  样例文档与独立 WebView2 宿主（截图 / PDF 验证用）
docs\shots\            README 截图
release\               预编译安装包
```

前端依赖（marked、DOMPurify、highlight.js、KaTeX、github-markdown-css）在构建时从 `_build\js`
复制进 `dist\...\web\vendor\`，运行期完全离线，不访问网络。

## 实现说明

- 原生 C++ 插件，界面用 WebView2（Chromium）渲染：`marked` 解析 → `DOMPurify` 过滤 → `highlight.js` 高亮 → `KaTeX` 排公式
- 面板资源通过 `SetVirtualHostNameToFolderMapping` 映射到本地目录；文档引用的图片走 `WebResourceRequested` 拦截，按文件目录读取，不经过 HTTP
- 渲染后用一个与 marked 对齐的轻量扫描器，把每个顶层块（标题/段落/列表/代码块/表格/引用/分隔线）的起止行号记下来；再用块首文字与渲染结果做内容比对，把行号挂到对应元素上（不靠元素计数，所以 front matter、HTML 注释、`<details>` 这类"扫描器猜不到、渲染器却会多出或少掉节点"的情况都不会连锁错位）
- 代码块和表格按"一行源码 = 一行渲染"的网格直接换算；其余块在自身高度内按行比例插值。锚点覆盖率不足时退回标题锚点
- 预览 → 编辑器用 `SCI_SETFIRSTVISIBLELINE`，落点与正向一致（视口顶部对齐视口顶部）
- 导出 PDF 用 `ICoreWebView2_7::PrintToPdf`；导出 HTML 把图片转成 data URI
- 冷启动时先把 WebView2 窗口隐藏，页面首帧就绪后再显示，中间由宿主窗口绘制骨架图，避免白屏

## 性能

渲染是「全量重绘」：每次输入停顿后重新解析整篇文档。为了让大文档也能用，插件做了四件事：

- 防抖随文档大小变化（< 256 KB 120 ms，< 2 MB 250 ms，更大 400 ms）
- 超过 300 KB 时跳过语法高亮（代码块保留等宽样式，只是不上色）
- DOMPurify 直接产出 DOM 片段再插入，省掉「净化成字符串 → 再解析一遍」
- 目录命中检测用缓存的标题偏移 + 二分查找，滚动时不再遍历所有标题

在 125% 缩放的 i5 机器上实测（同一份生成的 Markdown，第二轮即「输入后再渲染」）：

| 文档大小 | 首次渲染 | 输入后重渲染 |
| --- | --- | --- |
| 1.6 KB（示例文档） | 0.33 s | 13 ms |
| 380 KB | 1.18 s | 0.44 s |
| 950 KB | 2.28 s | 0.99 s |
| 1.7 MB | 4.56 s | 2.53 s |

打开 `debug=1` 后，`preview.log` 里会记录每一轮的 `render=` / `parse=` / `sanitize=` 分解耗时，方便定位。
## 已知限制

- 面板打开时占用约 180 MB 内存（Chromium 内核的固有成本）；关闭面板 30 秒后自动释放
- 多个 Notepad++ 实例可以各开一个预览；若系统里 WebView2 版本较旧、共享配置目录被占用，插件会自动改用本进程专属的目录重试
- 仅提供 x64 版本

## 许可证

[MIT](LICENSE)

## 致谢

[marked](https://github.com/markedjs/marked) · [DOMPurify](https://github.com/cure53/DOMPurify) ·
[highlight.js](https://highlightjs.org/) · [KaTeX](https://katex.org/) ·
[github-markdown-css](https://github.com/sindresorhus/github-markdown-css)
