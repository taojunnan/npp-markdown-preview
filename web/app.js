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
    headings: [],      // { line, el }
    renderedPath: null
  };

  const $ = function (id) { return document.getElementById(id); };
  const docEl = $('doc');
  const scrollEl = $('scroll');
  const tocEl = $('toc');
  const tocListEl = $('toc-list');

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

  function sanitize(html) {
    if (typeof DOMPurify === 'undefined') return html;
    return DOMPurify.sanitize(html, {
      ADD_ATTR: ['target', 'align', 'colspan', 'rowspan'],
      ADD_TAGS: ['math', 'semantics', 'annotation', 'mrow', 'mi', 'mo', 'mn', 'msup', 'msub', 'mfrac', 'msqrt', 'mtext'],
      USE_PROFILES: { html: true, svg: true, mathMl: true }
    });
  }

  function slugify(text, index) {
    let s = (text || '').trim().toLowerCase();
    s = s.replace(/[\s]+/g, '-');
    s = s.replace(/[^\w\u00C0-\uFFFF\-]/g, '');
    s = s.replace(/-+/g, '-').replace(/^-|-$/g, '');
    return s || ('section-' + index);
  }

  // Collect source heading lines so the preview can follow the editor.
  function collectSourceHeadings(text) {
    const out = [];
    let inFence = false;
    const lines = text.split(/\r\n|\r|\n/);
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      if (/^\s{0,3}(```|~~~)/.test(line)) { inFence = !inFence; continue; }
      if (inFence) continue;
      const m = /^\s{0,3}(#{1,6})\s+(\S.*?)\s*#*\s*$/.exec(line);
      if (m) out.push({ line: i, level: m[1].length, text: m[2] });
    }
    return out;
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
      a.title = '定位到此章节';
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
          box.textContent = '图片未找到：' + (src.length > 90 ? src.slice(0, 90) + '…' : src);
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

  function fixCodeBlocks() {
    const codes = Array.prototype.slice.call(docEl.querySelectorAll('pre > code'));
    codes.forEach(function (code) {
      const pre = code.parentNode;
      if (!pre || pre.parentNode === null) return;
      if (pre.parentNode.classList && pre.parentNode.classList.contains('codeblock')) return;

      let lang = '';
      for (let i = 0; i < code.classList.length; i++) {
        if (code.classList[i].indexOf('language-') === 0) { lang = code.classList[i].slice(9); break; }
      }

      if (typeof hljs !== 'undefined') {
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
      btn.textContent = '复制';
      btn.addEventListener('click', function (ev) {
        ev.preventDefault();
        ev.stopPropagation();
        copyText(code.textContent || '');
        btn.textContent = '已复制';
        setTimeout(function () { btn.textContent = '复制'; }, 1200);
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
      empty.textContent = '文档中还没有标题。用 # 号开头即可生成章节。';
      tocListEl.appendChild(empty);
      return;
    }
    heads.forEach(function (h, i) {
      const a = document.createElement('a');
      a.className = 'toc-item';
      a.dataset.level = String(Math.min(4, parseInt(h.tagName.slice(1), 10)));
      a.textContent = h.textContent.replace(/#$/, '').trim() || ('章节 ' + (i + 1));
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

  function buildHeadingMap(sourceText) {
    const source = collectSourceHeadings(sourceText);
    const domHeads = Array.prototype.slice.call(docEl.querySelectorAll('h1, h2, h3, h4, h5, h6'));
    state.headings = [];
    if (!source.length || source.length !== domHeads.length) return; // ratio fallback
    for (let i = 0; i < source.length; i++) {
      state.headings.push({ line: source[i].line, el: domHeads[i] });
    }
  }

  function updateStatus() {
    const text = state.text || '';
    const lines = text ? text.split(/\r\n|\r|\n/).length : 0;
    const cjk = (text.match(/[\u3400-\u9FFF\uF900-\uFAFF]/g) || []).length;
    const words = text.replace(/[\u3400-\u9FFF\uF900-\uFAFF]/g, ' ').split(/\s+/).filter(Boolean).length;
    const total = cjk + words;

    $('status-left').textContent = state.path || '未保存的文档';
    $('status-left').title = state.path || '';
    $('status-right').textContent = total.toLocaleString() + ' 字 · ' + lines.toLocaleString() + ' 行' +
      (state.truncated ? ' · 已截断' : '');
  }

  // ---------------------------------------------------------------- render --

  let renderSeq = 0;

  function render(text, meta) {
    const seq = ++renderSeq;
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
    }
    state.name = state.path ? basename(state.path) : '未命名';
    $('doc-name').textContent = state.name;
    $('doc-name').title = state.path || '';

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
      hint.textContent = '文档中还没有标题。用 # 号开头即可生成章节。';
      tocListEl.appendChild(hint);
      state.headings = [];
      updateStatus();
      return;
    }

    if (!markedReady && !setupMarked()) {
      $('error').hidden = false;
      $('error-text').textContent = '渲染库未能加载，请重新安装插件。';
      return;
    }

    let html = '';
    try {
      html = marked.parse(body, { async: false });
    } catch (err) {
      $('error').hidden = false;
      $('error-text').textContent = 'Markdown 解析失败：' + (err && err.message ? err.message : String(err));
      return;
    }
    if (seq !== renderSeq) return; // a newer render started

    const prevFirst = state.lastSync ? state.lastSync.first : null;

    docEl.innerHTML = sanitize(html);
    tocPinIndex = -1;

    fixHeadings();
    fixImages();
    fixLinks();
    fixCodeBlocks();
    fixTables();
    fixTaskLists();
    buildToc();
    buildHeadingMap(body);
    updateStatus();

    // restore the reading position after re-render
    if (pathChanged) {
      scrollEl.scrollTop = 0;
    } else if (prevFirst !== null) {
      applySync({ first: prevFirst, total: lastSyncTotal() , visible: 0 }, true);
    }
    updateActiveToc();
  }

  function lastSyncTotal() {
    return state.lastSync && state.lastSync.total ? state.lastSync.total : 1;
  }

  // ------------------------------------------------------------ scroll sync --

  let syncFrame = null;

  function applySync(msg, immediate) {
    if (!state.sync) return;
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
      updateActiveToc();
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
    scrollEl.scrollTo({ top: Math.max(0, offsetInScroll(el) - 10), behavior: 'smooth' });
  }

  let tocTick = 0;
  let tocPinIndex = -1;     // item picked from the TOC, kept until the user scrolls

  function updateActiveToc(force) {
    const now = Date.now();
    if (!force && now - tocTick < 60) return;
    tocTick = now;
    const items = tocListEl.querySelectorAll('.toc-item');
    if (!items.length) return;
    const heads = Array.prototype.slice.call(docEl.querySelectorAll('h1, h2, h3, h4'));
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

    let active = 0;
    for (let i = 0; i < heads.length; i++) {
      if (offsetInScroll(heads[i]) <= line) active = i;
      else break;
    }

    // Pinned to the very bottom: the last section is what is being read.
    if (maxScroll > 4 && remaining <= 2) active = heads.length - 1;

    // A heading chosen in the TOC stays highlighted until the user scrolls.
    if (tocPinIndex >= 0 && tocPinIndex < heads.length) active = tocPinIndex;

    items.forEach(function (it, i) { it.classList.toggle('active', i === active); });
    const act = items[active];
    if (act && act.scrollIntoView) {
      // keep the active item visible inside the TOC only
      const r = act.getBoundingClientRect();
      const tr = tocEl.getBoundingClientRect();
      if (r.top < tr.top + 8 || r.bottom > tr.bottom - 8) {
        act.scrollIntoView({ block: 'nearest' });
      }
    }
  }

  function pinTocItem(index) {
    tocPinIndex = index;
    updateActiveToc(true);
  }

  scrollEl.addEventListener('scroll', function () {
    // A pinned item survives the smooth scroll that the click started; it is
    // released by real input (wheel / keys / drag) or by editor-driven sync.
    updateActiveToc();
  }, { passive: true });

  ['wheel', 'touchstart', 'keydown', 'mousedown'].forEach(function (evt) {
    scrollEl.addEventListener(evt, function () {
      tocPinIndex = -1;
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
      navigator.clipboard.writeText(text).then(function () { toast('已复制'); }, fallback);
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
        if (ok) { toast('已复制'); return; }
      } catch (e) { /* ignore */ }
      post({ type: 'copyText', text: text });
      toast('已复制');
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

    const title = state.name ? stripExt(state.name) : 'Markdown 文档';
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
    if (!state.text.trim()) { toast('没有可导出的内容'); return; }
    toast('正在生成…', 4000);
    let html;
    try {
      html = await buildStandaloneHtml();
    } catch (e) {
      toast('生成失败：' + (e && e.message ? e.message : e));
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
    toast(mode === 'browser' ? '已在浏览器中打开' : '已导出');
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
      case 'content': {
        render(msg.text || '', {
          path: msg.path || '',
          dir: msg.dir || '',
          truncated: !!msg.truncated,
          dark: typeof msg.dark === 'boolean' ? msg.dark : undefined,
          fontSize: typeof msg.fontSize === 'number' ? msg.fontSize : undefined,
          toc: typeof msg.toc === 'boolean' ? msg.toc : undefined,
          sync: typeof msg.sync === 'boolean' ? msg.sync : undefined
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
  const THEME_LABEL = { auto: '跟随编辑器', light: '浅色', dark: '深色' };

  function refreshThemeButton() {
    document.documentElement.dataset.themeMode = state.mode || 'auto';
    const btn = $('btn-theme');
    if (!btn) return;
    btn.title = '主题：' + (THEME_LABEL[state.mode] || state.mode) + '（点击切换）';
  }

  function toggleTheme() {
    const i = THEME_MODES.indexOf(state.mode);
    const next = THEME_MODES[(i + 1) % THEME_MODES.length];
    state.mode = next;
    post({ type: 'setTheme', value: next });
    refreshThemeButton();
    toast('主题：' + THEME_LABEL[next], 1100);
  }

  $('btn-theme').addEventListener('click', toggleTheme);

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
    $('more-menu').querySelector('[data-act="toggle-sync"]').textContent =
      '滚动同步：' + (state.sync ? '开' : '关');
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
        toast('滚动同步：' + (state.sync ? '开' : '关'), 1200);
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
