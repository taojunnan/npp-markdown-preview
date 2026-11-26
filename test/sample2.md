## 代码与高亮

```bash
# 安装插件后重启 Notepad++
cp MarkdownPreview.dll "$NPP_HOME/plugins/MarkdownPreview/"
```

```json
{
  "name": "markdown-preview",
  "version": "1.0.0",
  "docked": true,
  "theme": "auto"
}
```

## 本地图片

![渲染示意图](assets/chart.png)

## 任务清单

- [x] 停靠面板
- [x] 实时刷新
- [ ] 更多主题
- [ ] 导出 Word

## 数学公式

行内 $E = mc^2$，块级：

$$
\int_a^b f(x)\,dx = F(b) - F(a)
$$

## 引用

> 排版的目的不是装饰，而是让阅读不需要费力。

## 表格

| 项目 | 参数 | 备注 |
| --- | --- | --- |
| 渲染引擎 | WebView2 | 系统自带 |
| 刷新延迟 | 120 ms | 输入防抖 |
| 面板宽度 | 可拖拽 | 记忆位置 |
