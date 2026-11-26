# Notepad++ Markdown 预览

> 一个**实时**、可停靠的 Markdown 预览面板，排版接近 GitHub 与 Typora 的阅读体验。

## 为什么重新做一个？

常见的 Markdown 插件的预览往往存在这些问题：

- 字体与行距拥挤，长文阅读容易疲劳
- 表格、任务列表、代码高亮支持不完整
- 本地图片无法显示
- 深色模式下刺眼

这个版本针对上述问题逐项处理，同时保持**轻量**：只在需要时加载，输入停止 120ms 后才重新渲染。

### 渲染能力

| 能力 | 说明 | 状态 |
| :--- | :--- | :---: |
| 表格 | 对齐、斑马纹、横向滚动 | ✅ |
| 代码高亮 | 常见语言自动识别 | ✅ |
| 任务列表 | 真实复选框样式 | ✅ |
| 数学公式 | KaTeX 渲染 | ✅ |
| 本地图片 | 相对路径 / 绝对路径 | ✅ |
| 导出 | HTML / PDF | ✅ |

### 代码

```javascript
// 滚动同步：按标题锚点线性插值
function targetScrollFor(firstLine, totalLines) {
  const heads = state.headings;
  let idx = -1;
  for (let i = 0; i < heads.length; i++) {
    if (heads[i].line <= firstLine) idx = i;
  }
  if (idx < 0) return 0;
  const cur = heads[idx];
  const next = heads[idx + 1];
  const span = Math.max(1, (next ? next.line : totalLines) - cur.line);
  const frac = Math.min(1, (firstLine - cur.line) / span);
  return offsetInScroll(cur.el) + frac * (next ? offsetInScroll(next.el) - offsetInScroll(cur.el) : 0);
}
```

```python
def word_count(text: str) -> int:
    """中英文混排的粗略字数统计"""
    cjk = sum(1 for ch in text if '\u4e00' <= ch <= '\u9fff')
    words = len([w for w in text.split() if w])
    return cjk + words
```

### 本地图片

下面的图片使用相对路径引用的本地文件，插件通过内部资源通道读取：

![示意图](assets/chart.png)

### 数学

行内公式 $a^2 + b^2 = c^2$，以及块级公式：

$$
\sum_{i=1}^{n} i = \frac{n(n+1)}{2}
$$

### 任务清单

- [x] 停靠面板与实时刷新
- [x] 跟随 Notepad++ 深色模式
- [x] 目录与滚动同步
- [ ] 更多导出格式

### 引用与分隔

> 好的工具应当让人专注于内容本身，而不是工具。
>
> —— 某位 Markdown 用户

---

1. 有序列表
2. 第二项
   - 嵌套项
   - 另一个嵌套项

最后一段：**粗体**、*斜体*、~~删除线~~、`inline code`、[链接](https://notepad-plus-plus.org/)、<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>P</kbd>。
