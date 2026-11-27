/* ============================================================================
   Markdown Preview for Notepad++ — panel application
   - renders GitHub-flavoured Markdown (marked) with syntax highlighting (hljs)
   - syncs the editor scroll position through heading anchors
   - talks to the native plugin over window.chrome.webview
   ========================================================================== */

(function () {
  'use strict';

  // ---------------------------------------------------------------- bridge --

  const host = window.chrome && window.chrome.webview ? window.chrome.webview : null;
  const embedded = !!host;

  function post(msg) {
    if (host) {
      try { host.postMessage(msg); } catch (e) { /* ignore */ }
    }
  }

  function onHostMessage(handler) {
    if (!host) return;
    host.addEventListener('message', function (event) {
      const data = event.data;
      if (!data) return;
      if (typeof data === 'string') {
        try { handler(JSON.parse(data)); } catch (e) { /* ignore */ }
      } else {
        handler(data);
      }
    });
  }

  // ----------------------------------------------------------------- state --

  const state = {
    text: '',
    path: '',
    dir: '',
    name: '',
    dark: false,
    mode: 'auto',
    fontSize: 16,
    toc: true,
    sync: true,
    truncated: false,
    lastSync: null,
    editorLine: null,   // last line reported by the editor (caret or viewport)
    headings: [],      // { line, el }
    blocks: [],        // { line, el } for every top level block (dense scroll sync)
    renderedPath: null
  };

  let lastRenderAt = 0;       // when the last render finished (see dimToolsWhileScrolling)
  let tocOffsets = null;      // cached heading offsets, invalidated on re-render
  let tocLines = [];          // source line of every TOC entry
  let activeTocIndex = -1;
  let tocUpdateFrame = 0;

  function scheduleTocUpdate() {
    if (tocUpdateFrame) return;
    tocUpdateFrame = requestAnimationFrame(function () {
      tocUpdateFrame = 0;
      // After a re-render the heading lines moved; re-apply the editor position
      // instead of guessing from the scroll offset.
      if (state.editorLine !== null && state.editorLine !== undefined && tocLines.length) {
        const idx = tocIndexForLine(state.editorLine);
        if (idx >= 0) {
          highlightTocIndex(idx);
          return;
        }
      }
      updateActiveToc(true);
    });
  }

  const $ = function (id) { return document.getElementById(id); };
  const docEl = $('doc');
  const scrollEl = $('scroll');
  const tocEl = $('toc');
  const tocListEl = $('toc-list');

  // -------------------------------------------------------------- i18n --
  // The host tells us which language to use via the document meta ("lang").
  const I18N = {
    zh: {
      tocTitle: '目录',
      tocToggle: '显示 / 隐藏目录',
      tocClose: '收起目录',
      themeToggle: '切换深浅色',
      zoomOut: '缩小字号 (Ctrl+-)',
      zoomIn: '放大字号 (Ctrl+=)',
      more: '更多操作',
      refresh: '刷新预览 (F5)',
      copyAll: '复制全文 Markdown',
      exportHtml: '导出为 HTML…',
      exportPdf: '导出为 PDF…',
      print: '打印…',
      openBrowser: '在浏览器中打开',
      syncOn: '滚动同步：开',
      syncOff: '滚动同步：关',
      syncNow: '同步到光标位置',
      copy: '复制',
      copied: '已复制',
      selectAll: '全选',
      copyCode: '复制代码块',
      emptyTitle: '开始书写，右侧即时呈现',
      emptyBody: '在 Notepad++ 中输入 Markdown，这里会实时渲染成排版良好的文档。',
      hintHeading: '章节结构',
      hintBold: '强调文字',
      hintList: '清单',
      hintCode: '代码高亮',
      hintTable: '数据表格',
      hintImage: '本地图片',
      errorTitle: '预览未能启动',
      ready: '就绪',
      untitled: '未命名',
      unsaved: '未保存的文档',
      words: '{w} 字 · {l} 行',
      truncated: ' · 已截断',
      noHeadings: '文档中还没有标题。用 # 号开头即可生成章节。',
      section: '章节 {n}',
      gotoSection: '定位到此章节',
      gotoSource: '在编辑器中定位此行',
      imageMissing: '图片未找到：',
      renderFailed: '渲染库未能加载，请重新安装插件。',
      parseFailed: 'Markdown 解析失败：',
      themeAuto: '跟随编辑器',
      themeLight: '浅色',
      themeDark: '深色',
      themeButton: '主题：{name}（点击切换）',
      themeToast: '主题：{name}',
      noContent: '没有可导出的内容',
      generating: '正在生成…',
      genFailed: '生成失败：',
      openedBrowser: '已在浏览器中打开',
      exported: '已导出',
      docFallback: 'Markdown 文档'
    },
    en: {
      tocTitle: 'Contents',
      tocToggle: 'Show / hide contents',
      tocClose: 'Hide contents',
      themeToggle: 'Toggle light / dark',
      zoomOut: 'Smaller text (Ctrl+-)',
      zoomIn: 'Larger text (Ctrl+=)',
      more: 'More actions',
      refresh: 'Refresh preview (F5)',
      copyAll: 'Copy all Markdown',
      exportHtml: 'Export as HTML…',
      exportPdf: 'Export as PDF…',
      print: 'Print…',
      openBrowser: 'Open in browser',
      syncOn: 'Scroll sync: on',
      syncOff: 'Scroll sync: off',
      syncNow: 'Sync to caret',
      copy: 'Copy',
      copied: 'Copied',
      selectAll: 'Select all',
      copyCode: 'Copy code block',
      emptyTitle: 'Start typing — it renders here',
      emptyBody: 'Type Markdown in Notepad++ and it is rendered here as a typeset document.',
      hintHeading: 'Sections',
      hintBold: 'Emphasis',
      hintList: 'Lists',
      hintCode: 'Highlighted code',
      hintTable: 'Tables',
      hintImage: 'Local images',
      errorTitle: 'The preview could not start',
      ready: 'Ready',
      untitled: 'Untitled',
      unsaved: 'Unsaved document',
      words: '{w} words · {l} lines',
      truncated: ' · truncated',
      noHeadings: 'No headings yet. Start a line with # to create one.',
      section: 'Section {n}',
      gotoSection: 'Go to this section',
      gotoSource: 'Show this line in the editor',
      imageMissing: 'Image not found: ',
      renderFailed: 'The rendering library failed to load. Reinstall the plugin.',
      parseFailed: 'Markdown parsing failed: ',
      themeAuto: 'Follow editor',
      themeLight: 'Light',
      themeDark: 'Dark',
      themeButton: 'Theme: {name} (click to switch)',
      themeToast: 'Theme: {name}',
      noContent: 'Nothing to export',
      generating: 'Generating…',
      genFailed: 'Generation failed: ',
      openedBrowser: 'Opened in browser',
      exported: 'Exported',
      docFallback: 'Markdown document'
    }
  };

  let lang = 'zh';

  function t(key, vars) {
    const table = I18N[lang] || I18N.zh;
    let text = table[key];
    if (text === undefined) text = I18N.zh[key] !== undefined ? I18N.zh[key] : key;
    if (vars) {
      Object.keys(vars).forEach(function (k) {
        text = text.split('{' + k + '}').join(String(vars[k]));
      });
    }
    return text;
  }

  function themeName(mode) {
    if (mode === 'light') return t('themeLight');
    if (mode === 'dark') return t('themeDark');
    return t('themeAuto');
  }

  // Applies the language to the static markup (data-i18n / data-i18n-title)
  // and to everything that is built dynamically.
  function applyLang(next) {
    lang = next === 'en' ? 'en' : 'zh';
    document.documentElement.lang = lang === 'en' ? 'en' : 'zh-CN';
    document.title = lang === 'en' ? 'Markdown Preview' : 'Markdown 预览';

    const nodes = document.querySelectorAll('[data-i18n]');
    for (let i = 0; i < nodes.length; i++) {
      const key = nodes[i].getAttribute('data-i18n');
      const text = t(key);
      if (text && text !== key) nodes[i].textContent = text;
    }
    const titled = document.querySelectorAll('[data-i18n-title]');
    for (let i = 0; i < titled.length; i++) {
      const key = titled[i].getAttribute('data-i18n-title');
      const text = t(key);
      if (text && text !== key) titled[i].title = text;
    }

    if (typeof refreshThemeButton === 'function') refreshThemeButton();
    if (typeof refreshSyncMenu === 'function') refreshSyncMenu();
  }

  // --------------------------------------------------------------- helpers --

  function isNarrow() { return window.innerWidth <= 760; }

  function toast(text, ms) {
    const el = $('toast');
    el.textContent = text;
    el.hidden = false;
    clearTimeout(toast._t);
    toast._t = setTimeout(function () { el.hidden = true; }, ms || 1600);
  }

  function basename(p) {
    if (!p) return '';
    const parts = p.split(/[\\/]/);
    return parts[parts.length - 1] || p;
  }

  function stripExt(name) {
    const i = name.lastIndexOf('.');
    return i > 0 ? name.slice(0, i) : name;
  }

  function joinPath(dir, rel) {
    if (!dir) return rel;
    const sep = dir.indexOf('\\') >= 0 ? '\\' : '/';
    return dir.replace(/[\\/]+$/, '') + sep + rel.replace(/^[.][\\/]/, '');
  }

  function decodeMaybe(s) {
    try { return decodeURIComponent(s); } catch (e) { return s; }
  }

  function isAbsolutePath(p) {
    return /^[a-zA-Z]:[\\/]/.test(p) || /^\\\\/.test(p) || /^file:/i.test(p);
  }

  function assetUrl(fullPath) {
    return 'https://mdassets.local/r?p=' + encodeURIComponent(fullPath);
  }

  function resolveLocalPath(src) {
    if (!src) return null;
    const raw = src.trim().replace(/^<|>$/g, '');
    if (/^(https?:|data:|blob:|mailto:)/i.test(raw)) return null;
    if (/^file:\/\//i.test(raw)) {
      let p = decodeMaybe(raw.replace(/^file:\/\//i, ''));
      if (/^\/[a-zA-Z]:/.test(p)) p = p.slice(1);
      return p.replace(/\//g, '\\');
    }
    const p = decodeMaybe(raw.split('#')[0].split('?')[0]);
    if (isAbsolutePath(p)) return p;
    if (!state.dir) return null;
    if (/^[\\/]/.test(p)) return joinPath(state.dir, p);
    return joinPath(state.dir, p);
  }

  // -------------------------------------------------------------- markdown --

  let markedReady = false;

  function setupMarked() {
    if (typeof marked === 'undefined') return false;
    try {
      marked.setOptions({ gfm: true, breaks: false });
    } catch (e) { /* older api */ }

    if (typeof markedKatex === 'function' && typeof katex !== 'undefined') {
      try {
        marked.use(markedKatex({
          throwOnError: false,
          nonStandard: false,
          output: 'htmlAndMathml'
        }));
      } catch (e) {
        // math is optional — never let it break the preview
        console.warn('KaTeX extension unavailable:', e);
      }
    }
    markedReady = true;
    return true;
  }

  const SANITIZE_OPTS = {
    ADD_ATTR: ['target', 'align', 'colspan', 'rowspan'],
    ADD_TAGS: ['math', 'semantics', 'annotation', 'mrow', 'mi', 'mo', 'mn', 'msup', 'msub', 'mfrac', 'msqrt', 'mtext'],
    USE_PROFILES: { html: true, svg: true, mathMl: true }
  };

  function sanitize(html) {
    if (typeof DOMPurify === 'undefined') return html;
    return DOMPurify.sanitize(html, SANITIZE_OPTS);
  }

  // Same sanitising pass, but returns nodes we can insert directly.  On a large
  // document this avoids serialising the cleaned HTML back to a string and
  // parsing it a second time (that round trip used to cost seconds).
  function sanitizeInto(el, html) {
    el.textContent = '';
    if (typeof DOMPurify === 'undefined') {
      el.innerHTML = html;
      return;
    }
    const frag = DOMPurify.sanitize(html, Object.assign({}, SANITIZE_OPTS, { RETURN_DOM_FRAGMENT: true }));
    el.appendChild(frag);
  }

  function slugify(text, index) {
    let s = (text || '').trim().toLowerCase();
    s = s.replace(/[\s]+/g, '-');
    s = s.replace(/[^\w\u00C0-\uFFFF\-]/g, '');
    s = s.replace(/-+/g, '-').replace(/^-|-$/g, '');
    return s || ('section-' + index);
  }

  // Collect source heading lines so the preview can follow the editor.
  // Headings come from the block scan so setext headings (text underlined with
  // === or ---) land in the table of contents as well.
  function collectSourceHeadings(text, blocks) {
    const lines = text.split(/\r\n|\r|\n/);
    const out = [];
    const scan = blocks || collectSourceBlocks(text);
    for (let k = 0; k < scan.length; k++) {
      const b = scan[k];
      if (b.kind !== 'heading') continue;
      const line = lines[b.line] || '';
      const m = /^ {0,3}(#{1,6})\s+(.*?)\s*#*\s*$/.exec(line);
      if (m) { out.push({ line: b.line, level: m[1].length, text: m[2] }); continue; }
      const underline = (lines[b.line + 1] || '').trim();
      out.push({ line: b.line, level: underline.charAt(0) === '=' ? 1 : 2, text: line.trim() });
    }
    return out;
  }

  // Every top level block with the source line it starts on.  Headings alone are
  // too sparse: a single section can hold a table, a code block and three
  // paragraphs, and interpolating over the whole section drifts by screens.
  function collectSourceBlocks(text) {
    const lines = text.split(/\r\n|\r|\n/);
    const n = lines.length;
    const out = [];
    const blank = (v) => /^\s*$/.test(v);
    const atx = /^ {0,3}#{1,6}\s+/;
    const fence = /^ {0,3}(`{3,}|~{3,})/;
    const setext = /^ {0,3}(=+|-+)\s*$/;
    const hr = /^ {0,3}([-*_])(?:\s*\1){2,}\s*$/;
    const quote = /^ {0,3}>/;
    const item = /^ {0,3}([-*+]|\d{1,9}[.)])\s+/;
    const tableSep = /^ {0,3}\|?[\s:|-]*-[\s:|-]*\|?\s*$/;
    const htmlBlock = /^ {0,3}<([a-zA-Z][\w-]*|\/[a-zA-Z][\w-]*|!)/;
    const indentOf = (v) => v.length - v.replace(/^ +/, '').length;

    let i = 0;
    while (i < n) {
      const line = lines[i];
      if (blank(line)) { i++; continue; }

      const fm = fence.exec(line);
      if (fm) {
        out.push({ line: i, kind: 'code' });
        const mark = fm[1][0];
        const len = fm[1].length;
        const close = new RegExp('^ {0,3}' + (mark === '`' ? '`' : '~') + '{' + len + ',}\\s*$');
        i++;
        while (i < n && !close.test(lines[i])) i++;
        i++;
        continue;
      }

      if (atx.test(line)) { out.push({ line: i, kind: 'heading' }); i++; continue; }

      if (i + 1 < n && setext.test(lines[i + 1]) && !hr.test(line) && !item.test(line)) {
        out.push({ line: i, kind: 'heading' });
        i += 2;
        continue;
      }

      if (hr.test(line)) { out.push({ line: i, kind: 'hr' }); i++; continue; }

      if (quote.test(line)) {
        out.push({ line: i, kind: 'quote' });
        i++;
        while (i < n) {
          if (blank(lines[i])) break;
          if (quote.test(lines[i]) || (!atx.test(lines[i]) && !fence.test(lines[i]) && !hr.test(lines[i]))) { i++; continue; }
          break;
        }
        continue;
      }

      if (item.test(line)) {
        out.push({ line: i, kind: 'list' });
        const markerType = function (m) { return /^\d/.test(m) ? 'ol' : m; };
        const startIndent = indentOf(line);
        const type = markerType(item.exec(line)[1]);
        let contentIndent = startIndent + item.exec(line)[1].length + 1;
        let prevBlank = false;
        i++;
        while (i < n) {
          const l = lines[i];
          const nm = item.exec(l);
          if (blank(l)) {
            // a blank line can separate items of a loose list, or end the list
            let j = i;
            while (j < n && blank(lines[j])) j++;
            if (j < n) {
              const nl = lines[j];
              const nn = item.exec(nl);
              if (nn && indentOf(nl) <= startIndent && markerType(nn[1]) === type) { i = j; prevBlank = true; continue; }
              if (indentOf(nl) >= contentIndent) { i = j; prevBlank = true; continue; }
            }
            break;
          }
          if (nm && indentOf(l) <= startIndent) {
            if (markerType(nm[1]) !== type) break;   // a different marker is a new list
            contentIndent = indentOf(l) + nm[1].length + 1;
            prevBlank = false;
            i++;
            continue;
          }
          if (indentOf(l) >= contentIndent) { prevBlank = false; i++; continue; }
          if (!prevBlank) { prevBlank = false; i++; continue; }   // lazy continuation
          break;
        }
        continue;
      }

      // indented code block: four spaces after a blank line
      if (indentOf(line) >= 4 && (i === 0 || blank(lines[i - 1]))) {
        out.push({ line: i, kind: 'code' });
        i++;
        while (i < n && (blank(lines[i]) || indentOf(lines[i]) >= 4)) i++;
        continue;
      }

      if (i + 1 < n && line.indexOf('|') !== -1 && tableSep.test(lines[i + 1])) {
        out.push({ line: i, kind: 'table' });
        i += 2;
        while (i < n && !blank(lines[i])) i++;
        continue;
      }

      // a link reference definition renders to nothing on its own
      if (/^ {0,3}\[[^\]]+\]:/.test(line)) { i++; continue; }

      if (htmlBlock.test(line)) {
        out.push({ line: i, kind: 'html' });
        i++;
        while (i < n && !blank(lines[i])) i++;
        continue;
      }

      out.push({ line: i, kind: 'p' });
      i++;
      while (i < n) {
        const l = lines[i];
        if (blank(l)) break;
        if (atx.test(l) || fence.test(l) || hr.test(l) || quote.test(l) || item.test(l)) break;
        if (i + 1 < n && setext.test(lines[i + 1])) break;
        i++;
      }
    }
    return out;
  }

  function blockKindOf(el) {
    const tag = el.tagName;
    if (/^H[1-6]$/.test(tag)) return 'heading';
    if (tag === 'P') return 'p';
    if (tag === 'UL' || tag === 'OL') return 'list';
    if (tag === 'PRE' || (el.classList && el.classList.contains('codeblock'))) return 'code';
    if (tag === 'TABLE' || (el.classList && el.classList.contains('table-wrap'))) return 'table';
    if (tag === 'BLOCKQUOTE') return 'quote';
    if (tag === 'HR') return 'hr';
    return 'other';
  }

  function kindMatches(srcKind, elKind) {
    if (srcKind === elKind) return true;
    // raw HTML can turn into a paragraph, a div or something else entirely
    if (srcKind === 'html' || elKind === 'other') return true;
    return false;
  }

  function buildBlockMap(blocks) {
    state.blocks = [];
    const children = Array.prototype.slice.call(docEl.children);
    // Any disagreement means the scan does not mirror the renderer for this
    // document, so drop the map and fall back to the heading anchors.
    if (!blocks.length || blocks.length !== children.length) return;
    for (let i = 0; i < blocks.length; i++) {
      if (!kindMatches(blocks[i].kind, blockKindOf(children[i]))) return;
    }
    const out = new Array(blocks.length);
    for (let i = 0; i < blocks.length; i++) out[i] = { line: blocks[i].line, el: children[i] };
    state.blocks = out;
  }

  // --------------------------------------------------------- post-process ---

  function fixHeadings() {
    const heads = Array.prototype.slice.call(docEl.querySelectorAll('h1, h2, h3, h4, h5, h6'));
    const used = Object.create(null);
    heads.forEach(function (h, i) {
      const raw = h.textContent.trim();
      let id = slugify(raw, i);
      let n = 1;
      while (used[id]) { id = slugify(raw, i) + '-' + (n++); }
      used[id] = true;
      h.id = id;

      const a = document.createElement('a');
      a.className = 'h-anchor';
      a.href = '#' + id;
      a.textContent = '#';
      a.title = t('gotoSection');
      a.addEventListener('click', function (ev) {
        ev.preventDefault();
        scrollToId(id);
      });
      h.appendChild(a);
    });
  }

  function fixImages() {
    const imgs = Array.prototype.slice.call(docEl.querySelectorAll('img'));
    imgs.forEach(function (img) {
      img.setAttribute('loading', 'lazy');
      img.setAttribute('decoding', 'async');
      const src = img.getAttribute('src') || '';
      const local = resolveLocalPath(src);
      if (local) {
        img.setAttribute('data-local', '1');
        img.setAttribute('data-src', src);
        img.src = assetUrl(local);
        img.addEventListener('error', function () {
          if (img.dataset.failed) return;
          img.dataset.failed = '1';
          const box = document.createElement('span');
          box.className = 'img-error';
          box.textContent = t('imageMissing') + (src.length > 90 ? src.slice(0, 90) + '…' : src);
          if (img.parentNode) img.parentNode.replaceChild(box, img);
        });
      }
      img.addEventListener('click', function (ev) {
        ev.preventDefault();
        openLightbox(img.currentSrc || img.src, img.alt);
      });
    });
  }

  function fixLinks() {
    const links = Array.prototype.slice.call(docEl.querySelectorAll('a'));
    links.forEach(function (a) {
      if (a.classList.contains('h-anchor')) return;
      const href = (a.getAttribute('href') || '').trim();
      if (!href) return;

      if (href.charAt(0) === '#') {
        a.addEventListener('click', function (ev) {
          ev.preventDefault();
          scrollToId(href.slice(1));
        });
        return;
      }

      a.title = a.title || href;

      if (/^(https?:)?\/\//i.test(href) || /^mailto:/i.test(href) || /^tel:/i.test(href)) {
        a.addEventListener('click', function (ev) {
          ev.preventDefault();
          post({ type: 'openExternal', url: href });
          if (!embedded) window.open(href, '_blank');
        });
        return;
      }

      a.addEventListener('click', function (ev) {
        ev.preventDefault();
        const p = resolveLocalPath(href);
        if (p) post({ type: 'openFile', path: p });
      });
    });
  }

  // Highlighting a large document costs more than it is worth; the code blocks
  // keep their monospace styling, just without colours.
  const BIG_DOC_BYTES = 300 * 1024;

  function fixCodeBlocks() {
    const bigDoc = (state.text || '').length > BIG_DOC_BYTES;
    const codes = Array.prototype.slice.call(docEl.querySelectorAll('pre > code'));
    codes.forEach(function (code) {
      const pre = code.parentNode;
      if (!pre || pre.parentNode === null) return;
      if (pre.parentNode.classList && pre.parentNode.classList.contains('codeblock')) return;

      let lang = '';
      for (let i = 0; i < code.classList.length; i++) {
        if (code.classList[i].indexOf('language-') === 0) { lang = code.classList[i].slice(9); break; }
      }

      if (typeof hljs !== 'undefined' && !bigDoc) {
        try {
          if (lang && hljs.getLanguage(lang)) {
            code.innerHTML = hljs.highlight(code.textContent, { language: lang, ignoreIllegals: true }).value;
            code.classList.add('hljs');
          } else if (!lang) {
            const auto = hljs.highlightAuto(code.textContent);
            if (auto && auto.value && auto.relevance > 5) {
              code.innerHTML = auto.value;
              code.classList.add('hljs');
            }
          }
        } catch (e) { /* keep plain text */ }
      }

      const wrap = document.createElement('div');
      wrap.className = 'codeblock';
      pre.parentNode.insertBefore(wrap, pre);
      wrap.appendChild(pre);

      const head = document.createElement('div');
      head.className = 'cb-head';
      const label = document.createElement('span');
      label.textContent = lang || 'code';
      const btn = document.createElement('button');
      btn.className = 'cb-copy';
      btn.type = 'button';
      btn.textContent = t('copy');
      btn.addEventListener('click', function (ev) {
        ev.preventDefault();
        ev.stopPropagation();
        copyText(code.textContent || '');
        btn.textContent = t('copied');
        setTimeout(function () { btn.textContent = t('copy'); }, 1200);
      });
      head.appendChild(label);
      head.appendChild(btn);
      wrap.insertBefore(head, pre);
    });
  }

  function fixTables() {
    const tables = Array.prototype.slice.call(docEl.querySelectorAll('table'));
    tables.forEach(function (t) {
      if (t.parentNode && t.parentNode.classList && t.parentNode.classList.contains('table-wrap')) return;
      const wrap = document.createElement('div');
      wrap.className = 'table-wrap';
      t.parentNode.insertBefore(wrap, t);
      wrap.appendChild(t);
    });
  }

  function fixTaskLists() {
    const boxes = Array.prototype.slice.call(docEl.querySelectorAll('li input[type="checkbox"]'));
    boxes.forEach(function (box) {
      box.disabled = true;
      const li = box.closest('li');
      if (li) li.classList.add('task-list-item');
    });
  }

  function buildToc() {
    const heads = Array.prototype.slice.call(docEl.querySelectorAll('h1, h2, h3, h4'));
    tocListEl.innerHTML = '';
    if (!heads.length) {
      const empty = document.createElement('div');
      empty.className = 'toc-empty';
      empty.textContent = t('noHeadings');
      tocListEl.appendChild(empty);
      return;
    }
    heads.forEach(function (h, i) {
      const a = document.createElement('a');
      a.className = 'toc-item';
      a.dataset.level = String(Math.min(4, parseInt(h.tagName.slice(1), 10)));
      a.textContent = h.textContent.replace(/#$/, '').trim() || t('section', { n: i + 1 });
      a.title = a.textContent;
      a.href = '#' + h.id;
      a.addEventListener('click', function (ev) {
        ev.preventDefault();
        pinTocItem(i);
        scrollToId(h.id);
        if (isNarrow()) document.body.classList.remove('toc-open');
      });
      tocListEl.appendChild(a);
    });
  }

  function refreshTocLines() {
    tocLines = [];
    const heads = docEl.querySelectorAll('h1, h2, h3, h4');
    for (let i = 0; i < heads.length; i++) {
      const line = parseInt(heads[i].dataset.line, 10);
      tocLines.push(isNaN(line) ? -1 : line);
    }
  }

  function buildHeadingMap(sourceText, blocks) {
    const source = collectSourceHeadings(sourceText, blocks);
    const domHeads = Array.prototype.slice.call(docEl.querySelectorAll('h1, h2, h3, h4, h5, h6'));
    state.headings = [];
    if (!source.length || source.length !== domHeads.length) return; // ratio fallback
    for (let i = 0; i < source.length; i++) {
      state.headings.push({ line: source[i].line, el: domHeads[i] });
      domHeads[i].dataset.line = String(source[i].line);
      domHeads[i].classList.add('jumpable');
      domHeads[i].title = t('gotoSource');
    }
  }

  function updateStatus() {
    const text = state.text || '';
    const lines = text ? text.split(/\r\n|\r|\n/).length : 0;
    const cjk = (text.match(/[\u3400-\u9FFF\uF900-\uFAFF]/g) || []).length;
    const words = text.replace(/[\u3400-\u9FFF\uF900-\uFAFF]/g, ' ').split(/\s+/).filter(Boolean).length;
    const total = cjk + words;

    $('status-left').textContent = state.path || t('unsaved');
    $('status-left').title = state.path || '';
    $('status-right').textContent = t('words', { w: total.toLocaleString(), l: lines.toLocaleString() }) +
      (state.truncated ? t('truncated') : '');
  }

  // ---------------------------------------------------------------- render --

  let renderSeq = 0;

  function render(text, meta) {
    const seq = ++renderSeq;
    const renderStart = performance.now();
    document.documentElement.classList.remove('booting');
    state.text = text || '';
    if (meta) {
      if (meta.path !== undefined) state.path = meta.path || '';
      if (meta.dir !== undefined) state.dir = meta.dir || '';
      if (meta.truncated !== undefined) state.truncated = !!meta.truncated;
      if (typeof meta.dark === 'boolean') applyTheme(meta.dark, false);
      if (typeof meta.fontSize === 'number') applyFontSize(meta.fontSize, false);
      if (typeof meta.toc === 'boolean') { state.toc = meta.toc; applyToc(false); }
      if (typeof meta.sync === 'boolean') state.sync = meta.sync;
      if (typeof meta.mode === 'string') { state.mode = meta.mode; refreshThemeButton(); }
      if (typeof meta.lang === 'string' && meta.lang !== lang) applyLang(meta.lang);
    }
    state.name = state.path ? basename(state.path) : t('untitled');
    // the title bar is gone; the status bar carries the path instead
    const nameEl = $('doc-name');
    if (nameEl) {
      nameEl.textContent = state.name;
      nameEl.title = state.path || '';
    }

    const pathChanged = state.renderedPath !== state.path;
    state.renderedPath = state.path;

    const body = state.text;
    const empty = body.trim().length === 0;

    $('empty').hidden = !empty;
    $('error').hidden = true;
    docEl.style.display = empty ? 'none' : '';

    if (empty) {
      docEl.innerHTML = '';
      tocListEl.innerHTML = '';
      const hint = document.createElement('div');
      hint.className = 'toc-empty';
      hint.textContent = t('noHeadings');
      tocListEl.appendChild(hint);
      state.headings = [];
      state.blocks = [];
      updateStatus();
      return;
    }

    if (!markedReady && !setupMarked()) {
      $('error').hidden = false;
      $('error-text').textContent = t('renderFailed');
      return;
    }

    let html = '';
    const tParse0 = performance.now();
    try {
      html = marked.parse(body, { async: false });
    } catch (err) {
      $('error').hidden = false;
      $('error-text').textContent = t('parseFailed') + (err && err.message ? err.message : String(err));
      return;
    }
    if (seq !== renderSeq) return; // a newer render started
    const tParse = performance.now() - tParse0;

    const prevFirst = state.lastSync ? state.lastSync.first : null;

    tocOffsets = null;
    activeTocIndex = -1;
    const tSan0 = performance.now();
    sanitizeInto(docEl, html);
    const tSan = performance.now() - tSan0;
    tocPinIndex = -1;

    fixHeadings();
    fixImages();
    fixLinks();
    fixCodeBlocks();
    fixTables();
    fixTaskLists();
    const tFix = performance.now() - tSan0 - tSan;
    const tToc0 = performance.now();
    buildToc();
    const srcBlocks = collectSourceBlocks(body);
    buildHeadingMap(body, srcBlocks);
    buildBlockMap(srcBlocks);
    refreshTocLines();
    updateStatus();
    const tToc = performance.now() - tToc0;

    // restore the reading position after re-render
    if (pathChanged) {
      scrollEl.scrollTop = 0;
    } else if (prevFirst !== null) {
      applySync({ first: prevFirst, total: lastSyncTotal() , visible: 0 }, true);
    }
    // The TOC hit-test needs a full layout; keep it out of the render path and
    // let it run once the frame has been produced.
    scheduleTocUpdate();

    lastRenderAt = performance.now();

    if (meta && meta.debug) {
      post({
        type: 'timing',
        text: 'render=' + Math.round(performance.now() - renderStart) + 'ms' +
              ' parse=' + Math.round(tParse) + ' sanitize=' + Math.round(tSan) +
              ' fix=' + Math.round(tFix) + ' toc=' + Math.round(tToc) +
              ' bytes=' + body.length + ' headings=' + state.headings.length +
              ' blocks=' + state.blocks.length + '/' + docEl.children.length
      });
    }
  }

  function lastSyncTotal() {
    return state.lastSync && state.lastSync.total ? state.lastSync.total : 1;
  }

  // ------------------------------------------------------------ scroll sync --

  let syncFrame = null;

  // Scrolls the panel performs on its own (scroll sync, TOC jumps) must not be
  // read back as "the user scrolled", or they overwrite the caret highlight.
  // Cleared by real input, not by a timer, so a slow layout cannot sneak in.
  let programmaticScroll = false;

  function markProgrammaticScroll() {
    programmaticScroll = true;
  }

  function applySync(msg, immediate) {
    if (!state.sync) return;
    markProgrammaticScroll();
    state.lastSync = msg;
    tocPinIndex = -1;

    if (syncFrame) cancelAnimationFrame(syncFrame);
    const run = function () {
      syncFrame = null;
      const first = Math.max(0, msg.first || 0);
      const total = Math.max(1, msg.total || 1);
      const y = targetScrollFor(first, total);
      if (y === null) return;
      const max = Math.max(0, scrollEl.scrollHeight - scrollEl.clientHeight);
      scrollEl.scrollTop = Math.min(max, Math.max(0, y));
      // Deliberately no TOC update here: this scroll came from the editor, and
      // the caret/viewport message that accompanied it already picked the item.
      // Recomputing from the scroll offset is what made the highlight jump to a
      // neighbouring heading after every edit.
    };
    if (immediate) run();
    else syncFrame = requestAnimationFrame(run);
  }

  function offsetInScroll(el) {
    const r = el.getBoundingClientRect();
    const s = scrollEl.getBoundingClientRect();
    return r.top - s.top + scrollEl.scrollTop;
  }

  function targetScrollFor(first, total) {
    const blocks = state.blocks;
    if (blocks && blocks.length) {
      let lo = 0;
      let hi = blocks.length - 1;
      let idx = -1;
      while (lo <= hi) {
        const mid = (lo + hi) >> 1;
        if (blocks[mid].line <= first) { idx = mid; lo = mid + 1; } else hi = mid - 1;
      }
      if (idx < 0) return 0;
      const cur = blocks[idx];
      const yCur = Math.max(0, offsetInScroll(cur.el) - 8);
      const next = blocks[idx + 1];
      if (next) {
        const yNext = Math.max(yCur, offsetInScroll(next.el) - 8);
        const span = Math.max(1, next.line - cur.line);
        const frac = Math.min(1, Math.max(0, (first - cur.line) / span));
        return yCur + frac * (yNext - yCur);
      }
      const span = Math.max(1, total - cur.line);
      const frac = Math.min(1, Math.max(0, (first - cur.line) / span));
      const docEnd = Math.max(yCur, scrollEl.scrollHeight - scrollEl.clientHeight);
      return yCur + frac * (docEnd - yCur);
    }

    const heads = state.headings;
    if (!heads.length) {
      const ratio = Math.min(1, first / Math.max(1, total - 1));
      return ratio * Math.max(0, scrollEl.scrollHeight - scrollEl.clientHeight);
    }

    let idx = -1;
    for (let i = 0; i < heads.length; i++) {
      if (heads[i].line <= first) idx = i;
      else break;
    }
    if (idx < 0) return 0;

    const cur = heads[idx];
    const yCur = Math.max(0, offsetInScroll(cur.el) - 8);

    if (idx + 1 < heads.length) {
      const next = heads[idx + 1];
      const yNext = Math.max(yCur, offsetInScroll(next.el) - 8);
      const span = Math.max(1, next.line - cur.line);
      const frac = Math.min(1, Math.max(0, (first - cur.line) / span));
      return yCur + frac * (yNext - yCur);
    }

    const span = Math.max(1, total - cur.line);
    const frac = Math.min(1, Math.max(0, (first - cur.line) / span));
    const docEnd = Math.max(yCur, scrollEl.scrollHeight - scrollEl.clientHeight);
    return yCur + frac * (docEnd - yCur);
  }

  function scrollToId(id) {
    if (!id) return;
    const el = document.getElementById(id);
    if (!el) return;
    markProgrammaticScroll();
    scrollEl.scrollTo({ top: Math.max(0, offsetInScroll(el) - 10), behavior: 'smooth' });
  }

  let tocTick = 0;
  let tocPinIndex = -1;     // item picked from the TOC, kept until the user scrolls

  function buildTocOffsets() {
    const heads = docEl.querySelectorAll('h1, h2, h3, h4');
    const base = scrollEl.getBoundingClientRect().top - scrollEl.scrollTop;
    const out = new Float64Array(heads.length);
    for (let i = 0; i < heads.length; i++) {
      out[i] = heads[i].getBoundingClientRect().top - base;
    }
    tocOffsets = out;
    return out;
  }

  function updateActiveToc(force) {
    const now = Date.now();
    if (!force && now - tocTick < 60) return;
    tocTick = now;
    const items = tocListEl.querySelectorAll('.toc-item');
    if (!items.length) return;
    const offsets = tocOffsets || buildTocOffsets();
    const heads = { length: offsets.length };
    if (!heads.length) return;

    const maxScroll = Math.max(0, scrollEl.scrollHeight - scrollEl.clientHeight);
    const clientHeight = scrollEl.clientHeight;
    const remaining = maxScroll - scrollEl.scrollTop;

    // Near the end of the document the last headings can never reach the top
    // edge, so the reading line drops towards the middle of the viewport.
    let line = scrollEl.scrollTop + 24;
    if (maxScroll > 4 && remaining < clientHeight * 0.6) {
      line = scrollEl.scrollTop + Math.max(24, Math.min(clientHeight * 0.4, 180));
    }

    // Binary search the cached offsets: the old linear scan read layout for
    // every heading and dominated scroll cost on long documents.
    let active = 0;
    let lo = 0;
    let hi = offsets.length - 1;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1;
      if (offsets[mid] <= line) { active = mid; lo = mid + 1; }
      else hi = mid - 1;
    }

    // Pinned to the very bottom: the last section is what is being read.
    if (maxScroll > 4 && remaining <= 2) active = heads.length - 1;

    // A heading chosen in the TOC stays highlighted until the user scrolls.
    if (tocPinIndex >= 0 && tocPinIndex < heads.length) active = tocPinIndex;

    highlightTocIndex(active);
  }

  function highlightTocIndex(index) {
    const items = tocListEl.querySelectorAll('.toc-item');
    if (!items.length) return;
    if (index >= items.length) index = items.length - 1;
    if (index < 0) index = 0;
    if (index !== activeTocIndex) {
      const prev = items[activeTocIndex];
      if (prev) prev.classList.remove('active');
      const next = items[index];
      if (next) next.classList.add('active');
      activeTocIndex = index;
    }
    const act = items[index];
    if (act && act.scrollIntoView) {
      // keep the active item visible inside the TOC only
      const r = act.getBoundingClientRect();
      const tr = tocEl.getBoundingClientRect();
      if (r.top < tr.top + 8 || r.bottom > tr.bottom - 8) {
        act.scrollIntoView({ block: 'nearest' });
      }
    }
  }

  // Last TOC entry whose heading starts at or above the caret line.
  function tocIndexForLine(line) {
    let idx = -1;
    for (let i = 0; i < tocLines.length; i++) {
      if (tocLines[i] >= 0 && tocLines[i] <= line) idx = i;
      else if (tocLines[i] > line) break;
    }
    return idx;
  }

  function pinTocItem(index) {
    tocPinIndex = index;
    updateActiveToc(true);
  }

  // Fade the floating toolbar out while the document is being scrolled, and
  // bring it back shortly after the scrolling stops.
  const toolsEl = document.querySelector('.tools');
  let toolsIdleTimer = 0;

  function dimToolsWhileScrolling() {
    if (!toolsEl) return;
    // A re-render often drags the preview along by a line or two; that is not
    // the user reading, so keep the toolbar steady right after a render.
    if (performance.now() - lastRenderAt < 500) return;
    toolsEl.classList.add('dim');
    clearTimeout(toolsIdleTimer);
    toolsIdleTimer = setTimeout(function () { toolsEl.classList.remove('dim'); }, 1000);
  }

  scrollEl.addEventListener('scroll', dimToolsWhileScrolling, { passive: true });

  scrollEl.addEventListener('scroll', function () {
    // A pinned item survives the smooth scroll that the click started; it is
    // released by real input (wheel / keys / drag) or by editor-driven sync.
    if (programmaticScroll) return;
    state.editorLine = null;
    updateActiveToc();
  }, { passive: true });

  ['wheel', 'touchstart', 'keydown', 'mousedown'].forEach(function (evt) {
    scrollEl.addEventListener(evt, function () {
      tocPinIndex = -1;
      programmaticScroll = false;
    }, { passive: true });
  });

  // ---------------------------------------------------------------- theme ---

  function applyTheme(dark, persist) {
    state.dark = !!dark;
    document.documentElement.dataset.theme = state.dark ? 'dark' : 'light';
    document.documentElement.dataset.themeMode = state.mode || 'auto';
    const ghLight = $('gh-light'), ghDark = $('gh-dark');
    const hlLight = $('hl-light'), hlDark = $('hl-dark');
    if (ghLight) ghLight.disabled = state.dark;
    if (ghDark) ghDark.disabled = !state.dark;
    if (hlLight) hlLight.disabled = state.dark;
    if (hlDark) hlDark.disabled = !state.dark;
    if (persist) post({ type: 'setTheme', value: state.dark ? 'dark' : 'light' });
  }

  function applyFontSize(size, persist) {
    tocOffsets = null;
    const s = Math.min(28, Math.max(11, size | 0));
    state.fontSize = s;
    document.documentElement.style.setProperty('--doc-font', s + 'px');
    if (persist) post({ type: 'setFontSize', value: s });
  }

  function applyToc(persist) {
    if (isNarrow()) {
      document.body.classList.toggle('no-toc', false);
    } else {
      document.body.classList.toggle('no-toc', !state.toc);
      document.body.classList.remove('toc-open');
    }
    $('btn-toc').classList.toggle('on', state.toc);
    if (persist) post({ type: 'setToc', value: state.toc });
  }

  // ---------------------------------------------------------------- menus ---

  function closeMenus() {
    $('more-menu').hidden = true;
    $('ctx-menu').hidden = true;
    $('btn-more').setAttribute('aria-expanded', 'false');
  }

  function placeMenu(menu, x, y) {
    menu.hidden = false;
    menu.style.left = '0px';
    menu.style.top = '0px';
    const r = menu.getBoundingClientRect();
    const left = Math.min(x, window.innerWidth - r.width - 8);
    const top = Math.min(y, window.innerHeight - r.height - 8);
    menu.style.left = Math.max(6, left) + 'px';
    menu.style.top = Math.max(6, top) + 'px';
  }

  // ---------------------------------------------------------------- copy ----

  function copyText(text) {
    if (!text) return;
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function () { toast(t('copied')); }, fallback);
      return;
    }
    fallback();

    function fallback() {
      try {
        const ta = document.createElement('textarea');
        ta.value = text;
        ta.style.position = 'fixed';
        ta.style.opacity = '0';
        document.body.appendChild(ta);
        ta.select();
        const ok = document.execCommand('copy');
        document.body.removeChild(ta);
        if (ok) { toast(t('copied')); return; }
      } catch (e) { /* ignore */ }
      post({ type: 'copyText', text: text });
      toast(t('copied'));
    }
  }

  // ------------------------------------------------------------- lightbox ---

  let lightboxOpen = false;

  function openLightbox(src, alt) {
    if (!src) return;
    const img = $('lightbox-img');
    img.src = src;
    img.alt = alt || '';
    $('lightbox').hidden = false;
    lightboxOpen = true;
  }

  function closeLightbox() {
    $('lightbox').hidden = true;
    $('lightbox-img').src = '';
    lightboxOpen = false;
  }

  // --------------------------------------------------------------- export ---

  const cssCache = Object.create(null);

  async function fetchText(url) {
    if (cssCache[url]) return cssCache[url];
    const r = await fetch(url, { cache: 'no-store' });
    if (!r.ok) throw new Error('fetch failed: ' + url);
    const t = await r.text();
    cssCache[url] = t;
    return t;
  }

  function blobToDataUrl(blob) {
    return new Promise(function (resolve, reject) {
      const fr = new FileReader();
      fr.onload = function () { resolve(fr.result); };
      fr.onerror = reject;
      fr.readAsDataURL(blob);
    });
  }

  // Turn relative url(...) references inside CSS into data: URIs so the
  // exported document keeps its fonts when opened elsewhere.
  async function inlineCssAssets(cssText, baseUrl, budget) {
    const seen = Object.create(null);
    const urls = [];
    cssText.replace(/url\((?!['"]?(?:data:|https?:|#))['"]?([^'")]+)['"]?\)/g, function (m, p) {
      if (!seen[p]) { seen[p] = true; urls.push(p); }
      return m;
    });

    for (let i = 0; i < urls.length; i++) {
      const rel = urls[i];
      if (budget.left <= 0) break;
      let abs;
      try { abs = new URL(rel, baseUrl).href; } catch (e) { continue; }
      try {
        const r = await fetch(abs, { cache: 'force-cache' });
        if (!r.ok) continue;
        const blob = await r.blob();
        budget.left -= blob.size;
        const dataUrl = await blobToDataUrl(blob);
        cssText = cssText.split(rel).join(dataUrl);
        cssText = cssText.split(abs).join(dataUrl);
      } catch (e) { /* keep original reference */ }
    }
    return cssText;
  }

  async function buildStandaloneHtml() {
    const clone = docEl.cloneNode(true);
    clone.querySelectorAll('.h-anchor, .cb-copy').forEach(function (n) { n.remove(); });
    clone.querySelectorAll('img').forEach(function (img) {
      img.removeAttribute('loading');
      img.removeAttribute('data-local');
    });

    // embed local images so the export is self-contained
    const imgs = Array.prototype.slice.call(clone.querySelectorAll('img'));
    const budget = { left: 14 * 1024 * 1024 };
    for (let i = 0; i < imgs.length; i++) {
      const src = imgs[i].getAttribute('src') || '';
      if (src.indexOf('https://mdassets.local/') !== 0) continue;
      if (budget.left <= 0) break;
      try {
        const r = await fetch(src);
        if (!r.ok) continue;
        const blob = await r.blob();
        budget.left -= blob.size;
        imgs[i].src = await blobToDataUrl(blob);
      } catch (e) { /* leave as is */ }
    }

    const base = new URL('.', location.href).href;
    const sheets = [
      state.dark ? 'vendor/gh-dark.css' : 'vendor/gh-light.css',
      state.dark ? 'vendor/hljs-dark.css' : 'vendor/hljs-light.css',
      'vendor/katex.min.css',
      'style.css'
    ];

    let css = '';
    const fontBudget = { left: 5 * 1024 * 1024 };
    for (let i = 0; i < sheets.length; i++) {
      try {
        let text = await fetchText(base + sheets[i]);
        if (sheets[i].indexOf('katex') >= 0) {
          text = await inlineCssAssets(text, base + 'vendor/katex.min.css', fontBudget);
        }
        css += '\n/* ' + sheets[i] + ' */\n' + text;
      } catch (e) { /* skip missing sheet */ }
    }

    css += `
/* export overrides */
html, body { height: auto; overflow: visible; background: ${state.dark ? '#0d1117' : '#ffffff'}; }
#main { display: block; }
#scroll { overflow: visible; }
#doc { max-width: 860px; margin: 0 auto; padding: 44px 28px 90px; }
`;

    const title = state.name ? stripExt(state.name) : t('docFallback');
    const themeAttr = state.dark ? 'dark' : 'light';
    return '<!DOCTYPE html>\n<html lang="zh-CN" data-theme="' + themeAttr + '">\n<head>\n' +
      '<meta charset="utf-8">\n' +
      '<meta name="viewport" content="width=device-width, initial-scale=1">\n' +
      '<title>' + escapeHtml(title) + '</title>\n' +
      '<style>' + css + '</style>\n</head>\n<body>\n' +
      '<div id="main"><div id="scroll"><article id="doc" class="markdown-body">' + clone.innerHTML +
      '</article></div></div>\n</body>\n</html>';
  }

  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }

  async function exportHtml(mode) {
    if (!state.text.trim()) { toast(t('noContent')); return; }
    toast(t('generating'), 4000);
    let html;
    try {
      html = await buildStandaloneHtml();
    } catch (e) {
      toast(t('genFailed') + (e && e.message ? e.message : e));
      return;
    }
    const name = (state.name ? stripExt(state.name) : 'preview') + '.html';
    if (embedded) {
      post({ type: 'exportHtmlResult', html: html, name: name, mode: mode || 'save' });
    } else {
      const blob = new Blob([html], { type: 'text/html;charset=utf-8' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = name;
      a.click();
    }
    toast(mode === 'browser' ? t('openedBrowser') : t('exported'));
  }

  function enterPrintMode(on) {
    document.body.classList.toggle('print-mode', !!on);
    setTimeout(function () {
      post({ type: 'printReady' });
    }, 80);
  }

  // ----------------------------------------------------------- host events --

  onHostMessage(function (msg) {
    if (!msg || !msg.type) return;

    switch (msg.type) {
      case 'caret': {
        if (typeof msg.line === 'number') {
          state.editorLine = msg.line;
          if (tocLines.length) {
            const idx = tocIndexForLine(msg.line);
            if (idx >= 0) {
              tocPinIndex = -1;
              highlightTocIndex(idx);
            }
          }
        }
        break;
      }
      case 'content': {
        render(msg.text || '', {
          path: msg.path || '',
          dir: msg.dir || '',
          truncated: !!msg.truncated,
          dark: typeof msg.dark === 'boolean' ? msg.dark : undefined,
          fontSize: typeof msg.fontSize === 'number' ? msg.fontSize : undefined,
          toc: typeof msg.toc === 'boolean' ? msg.toc : undefined,
          sync: typeof msg.sync === 'boolean' ? msg.sync : undefined,
          lang: typeof msg.lang === 'string' ? msg.lang : undefined,
          debug: msg.debug === true
        });
        break;
      }
      case 'theme':
        applyTheme(!!msg.dark, false);
        refreshThemeButton();
        break;
      case 'themeMode':
        if (typeof msg.mode === 'string') state.mode = msg.mode;
        refreshThemeButton();
        break;
      case 'sync':
        applySync(msg, false);
        break;
      case 'printMode':
        enterPrintMode(!!msg.on);
        break;
      case 'requestExport':
        exportHtml(msg.mode || 'save');
        break;
      default:
        break;
    }
  });

  // -------------------------------------------------------------- ui wiring --

  const THEME_MODES = ['auto', 'light', 'dark'];

  function refreshThemeButton() {
    document.documentElement.dataset.themeMode = state.mode || 'auto';
    const btn = $('btn-theme');
    if (!btn) return;
    btn.title = t('themeButton', { name: themeName(state.mode) });
  }

  function refreshSyncMenu() {
    const item = document.querySelector('#more-menu [data-act="toggle-sync"]');
    if (item) item.textContent = state.sync ? t('syncOn') : t('syncOff');
  }

  function toggleTheme() {
    const i = THEME_MODES.indexOf(state.mode);
    const next = THEME_MODES[(i + 1) % THEME_MODES.length];
    state.mode = next;
    post({ type: 'setTheme', value: next });
    refreshThemeButton();
    toast(t('themeToast', { name: themeName(next) }), 1100);
  }

  $('btn-theme').addEventListener('click', toggleTheme);

  // Clicking a heading jumps to its source line in the editor.
  docEl.addEventListener('click', function (ev) {
    if (ev.detail !== 1) return;
    const head = ev.target && ev.target.closest ? ev.target.closest('h1, h2, h3, h4, h5, h6') : null;
    if (!head || !head.dataset || !head.dataset.line) return;
    const sel = window.getSelection();
    if (sel && !sel.isCollapsed && String(sel).trim()) return;
    post({ type: 'revealLine', line: parseInt(head.dataset.line, 10) });
  });

  $('btn-toc').addEventListener('click', function () {
    if (isNarrow()) {
      document.body.classList.toggle('toc-open');
      $('btn-toc').classList.toggle('on', document.body.classList.contains('toc-open'));
    } else {
      state.toc = !state.toc;
      applyToc(true);
    }
  });

  $('btn-toc-close').addEventListener('click', function () {
    if (isNarrow()) {
      document.body.classList.remove('toc-open');
      $('btn-toc').classList.remove('on');
    } else {
      state.toc = false;
      applyToc(true);
    }
  });

  $('btn-zoom-in').addEventListener('click', function () { applyFontSize(state.fontSize + 1, true); });
  $('btn-zoom-out').addEventListener('click', function () { applyFontSize(state.fontSize - 1, true); });
  $('btn-refresh').addEventListener('click', function () {
    post({ type: 'refresh' });
    if (!embedded) render(state.text, null);
  });

  $('btn-more').addEventListener('click', function (ev) {
    ev.stopPropagation();
    const menu = $('more-menu');
    if (!menu.hidden) { closeMenus(); return; }
    const r = ev.currentTarget.getBoundingClientRect();
    refreshSyncMenu();
    placeMenu(menu, r.right - 200, r.bottom + 4);
    $('btn-more').setAttribute('aria-expanded', 'true');
  });

  $('more-menu').addEventListener('click', function (ev) {
    const btn = ev.target.closest('button');
    if (!btn) return;
    const act = btn.dataset.act;
    closeMenus();
    switch (act) {
      case 'copy-all': copyText(state.text); break;
      case 'export-html': exportHtml('save'); break;
      case 'export-pdf': post({ type: 'requestPdf' }); break;
      case 'print': post({ type: 'requestPrint' }); break;
      case 'browser': exportHtml('browser'); break;
      case 'toggle-sync':
        state.sync = !state.sync;
        post({ type: 'setSync', value: state.sync });
        refreshSyncMenu();
        toast(state.sync ? t('syncOn') : t('syncOff'), 1200);
        break;
      case 'sync-now': post({ type: 'syncNow' }); break;
      default: break;
    }
  });

  // context menu
  docEl.addEventListener('contextmenu', function (ev) {
    ev.preventDefault();
    const sel = String(window.getSelection() || '');
    const codeEl = ev.target.closest ? ev.target.closest('pre') : null;
    const menu = $('ctx-menu');
    const copyItem = menu.querySelector('[data-act="ctx-copy"]');
    const codeItem = menu.querySelector('[data-act="ctx-copy-code"]');
    copyItem.disabled = !sel;
    codeItem.hidden = !codeEl;

    menu.dataset.code = codeEl ? (codeEl.textContent || '') : '';
    placeMenu(menu, ev.clientX, ev.clientY);
    menu.dataset.sel = sel;
  });

  $('ctx-menu').addEventListener('click', function (ev) {
    const btn = ev.target.closest('button');
    if (!btn) return;
    const act = btn.dataset.act;
    const menu = $('ctx-menu');
    const sel = menu.dataset.sel || '';
    const code = menu.dataset.code || '';
    closeMenus();
    if (act === 'ctx-copy' && sel) copyText(sel);
    else if (act === 'ctx-selectall') {
      const range = document.createRange();
      range.selectNodeContents(docEl);
      const s = window.getSelection();
      s.removeAllRanges();
      s.addRange(range);
    } else if (act === 'ctx-copy-code' && code) copyText(code);
    else if (act === 'ctx-open-browser') exportHtml('browser');
  });

  document.addEventListener('click', function (ev) {
    if (!ev.target.closest || !ev.target.closest('.menu, #btn-more')) closeMenus();
    if (isNarrow() && !ev.target.closest('#toc, #btn-toc')) {
      document.body.classList.remove('toc-open');
    }
  });

  $('lightbox').addEventListener('click', function (ev) {
    if (ev.target.id === 'lightbox') closeLightbox();
  });

  document.addEventListener('keydown', function (ev) {
    if (ev.key === 'Escape') {
      if (lightboxOpen) { closeLightbox(); return; }
      closeMenus();
      document.body.classList.remove('toc-open');
      return;
    }
    if (ev.key === 'F5') {
      ev.preventDefault();
      post({ type: 'refresh' });
      return;
    }
    if ((ev.ctrlKey || ev.metaKey) && (ev.key === '=' || ev.key === '+')) {
      ev.preventDefault();
      applyFontSize(state.fontSize + 1, true);
      return;
    }
    if ((ev.ctrlKey || ev.metaKey) && ev.key === '-') {
      ev.preventDefault();
      applyFontSize(state.fontSize - 1, true);
      return;
    }
    if ((ev.ctrlKey || ev.metaKey) && ev.shiftKey && (ev.key === 'C' || ev.key === 'c')) {
      const sel = String(window.getSelection() || '');
      if (sel) { ev.preventDefault(); copyText(sel); }
    }
  });

  window.addEventListener('resize', function () {
    tocOffsets = null;
    if (!isNarrow()) document.body.classList.remove('toc-open');
    applyToc(false);
  });

  // ------------------------------------------------------------ demo mode ---

  const DEMO = [
    '# Markdown 预览',
    '',
    '> 在 Notepad++ 中输入 Markdown，右侧面板即时呈现排版结果。',
    '',
    '## 排版示例',
    '',
    '这是一段**加粗**、*斜体*、~~删除线~~与 `行内代码` 混排的段落，用来检验中英文混排的阅读节奏与行距。',
    '',
    '### 代码高亮',
    '',
    '```js',
    'function greet(name) {',
    '  const message = `你好，${name}！`;',
    '  console.log(message);',
    '  return message.length > 0;',
    '}',
    '```',
    '',
    '### 表格',
    '',
    '| 功能 | 说明 | 状态 |',
    '| --- | --- | :---: |',
    '| 实时预览 | 输入即渲染 | ✅ |',
    '| 目录导航 | 自动生成大纲 | ✅ |',
    '| 主题跟随 | 深色 / 浅色 | ✅ |',
    '',
    '### 任务清单',
    '',
    '- [x] 停靠面板',
    '- [x] 语法高亮',
    '- [ ] 导出 PDF',
    '',
    '### 数学公式',
    '',
    '行内公式 $E = mc^2$，块级公式：',
    '',
    '$$\\int_a^b f(x)\\,dx = F(b) - F(a)$$',
    '',
    '---',
    '',
    '1. 有序列表第一项',
    '2. 第二项',
    '   - 嵌套无序列表',
    '',
    '> 提示：按 `Ctrl+Alt+P` 可以随时收起或打开这个面板。'
  ].join('\n');

  function startDemo() {
    const params = new URLSearchParams(location.search);
    const dark = params.get('theme') === 'dark';
    applyTheme(dark, false);
    const text = params.get('text') || DEMO;
    render(text, { path: 'D:\\Demo\\演示.md', dir: 'D:\\Demo' });
  }

  // Diagnostics only: the host logs these numbers when debug=1.
  function reportTiming() {
    try {
      const nav = (performance.getEntriesByType('navigation') || [])[0] || {};
      const rb = (performance.getEntriesByType('resource') || []);
      let heaviest = '';
      let worst = 0;
      for (const r of rb) {
        if (r.duration > worst) { worst = r.duration; heaviest = r.name.split('/').pop(); }
      }
      const text = 'dom=' + Math.round(performance.now()) +
        ' req=' + Math.round(nav.requestStart || 0) +
        ' rspStart=' + Math.round(nav.responseStart || 0) +
        ' rspEnd=' + Math.round(nav.responseEnd || 0) +
        ' dcl=' + Math.round(nav.domContentLoadedEventEnd || 0) +
        ' resources=' + rb.length +
        ' slowest=' + heaviest + '(' + Math.round(worst) + 'ms)';
      post({ type: 'timing', text: text });
      window.addEventListener('load', function () {
        post({ type: 'timing', text: 'load=' + Math.round(performance.now()) });
      }, { once: true });
    } catch (e) { }
  }

  // ------------------------------------------------------------------ boot --

  function boot() {
    setupMarked();
    refreshThemeButton();

    const params = new URLSearchParams(location.search);
    if (params.get('font')) applyFontSize(parseInt(params.get('font'), 10) || 16, false);

    if (embedded) {
      post({ type: 'ready' });
      reportTiming();
    } else {
      startDemo();
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }
})();
