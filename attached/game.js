/* ============================================================================
   拼9 —— 游戏逻辑
   依赖 puzzles.js（window.PUZZLE_DATA）

   关键设计：
   · 玩家点数字/运算符拼算式，按「=」检查当前这一组（写错会抖动+闪红）
   · 判定"是否凑出 9"完全在前端算：对玩家输入的这串符号，尝试所有加括号方式，
     只要有一种结果正好等于 9 就算对（用分数精确计算，避免 0.1 误差）
   · 因此不带括号的输入也能解出所有题目（例如 1+1÷8×8 会被识别成 (1+1/8)*8=9）
   · 前三关鼓励多解：拼完一组解可以按「再来一种解法」，给出不同划分/不同算式加分
   ==========================================================================*/
(function () {
  'use strict';

  var DATA = window.PUZZLE_DATA;
  if (!DATA || !DATA.puzzles || !DATA.puzzles.length) {
    document.body.innerHTML =
      '<div style="padding:40px;color:#fff;font:16px/1.8 sans-serif">' +
      'puzzles.js 没加载成功（或者里面没有题目）。<br>' +
      '请确认 puzzles.js 和 index.html 在同一个文件夹里。</div>';
    return;
  }

  /* 所有关卡都不计时了：time: 0 = 不限时（HUD 只显示"不限时"，永远不会超时失败）。
     hidden = 隐藏时间：只用来算彩蛋什么时候冒出来（时间过半 ~ 四分之三之间）。
     想把某一关的限时加回来，把它的 time 改成正数即可（界面会自动显示倒计时）。 */
  var LEVELS = [
    { no: 1, tier: 'normal',   cn: '第一关', time: 0, hidden: 20, rounds: 1 },
    { no: 2, tier: 'hard',     cn: '第二关', time: 0, hidden: 30, rounds: 1 },
    { no: 3, tier: 'veryhard', cn: '第三关', time: 0, hidden: 40, rounds: 1 },
    { no: 4, tier: 'hardcore', cn: '第四关', time: 0, hidden: 50, rounds: 1 },
    { no: 5, tier: 'extreme',  cn: '第五关', time: 0, hidden: 60, rounds: 1 },
    { no: 6, tier: 'insane',   cn: '第六关', time: 0, hidden: 70, rounds: 1 },
    { no: 7, tier: 'torment',  cn: '第七关', time: 0, hidden: 65, rounds: 1 }
  ];

  var TIERS = {};
  DATA.tiers.forEach(function (t) { TIERS[t.key] = t; });
  var SCORING = DATA.scoring || {};
  var TARGET = DATA.target || 9;

  /* 每档的 score 范围（用来算难度系数；后四档没存 score，就按 1.0 算） */
  var SCORE_RANGE = {};
  DATA.puzzles.forEach(function (p) {
    if (typeof p.score !== 'number') return;
    var r = SCORE_RANGE[p.tier] || (SCORE_RANGE[p.tier] = { min: p.score, max: p.score });
    if (p.score < r.min) r.min = p.score;
    if (p.score > r.max) r.max = p.score;
  });

  /* ================================ 小工具 ================================ */
  function $(id) { return document.getElementById(id); }
  function el(tag, cls, text) {
    var d = document.createElement(tag);
    if (cls) d.className = cls;
    if (text !== undefined && text !== null) d.textContent = text;
    return d;
  }
  function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }
  function shuffle(a) {
    for (var i = a.length - 1; i > 0; i--) {
      var j = Math.floor(Math.random() * (i + 1));
      var t = a[i]; a[i] = a[j]; a[j] = t;
    }
    return a;
  }
  function opText(op) { return op === '*' ? '×' : (op === '/' ? '÷' : op); }

  /* 分数（有理数）运算：用 [分子,分母] 表示，避免浮点误差 */
  function gcd(a, b) { a = Math.abs(a); b = Math.abs(b); while (b) { var t = a % b; a = b; b = t; } return a || 1; }
  function fr(n, d) { if (d < 0) { n = -n; d = -d; } var g = gcd(n, d); return [n / g, d / g]; }
  function frAdd(a, b) { return fr(a[0] * b[1] + b[0] * a[1], a[1] * b[1]); }
  function frSub(a, b) { return fr(a[0] * b[1] - b[0] * a[1], a[1] * b[1]); }
  function frMul(a, b) { return fr(a[0] * b[0], a[1] * b[1]); }
  function frDiv(a, b) { return b[0] === 0 ? null : fr(a[0] * b[1], a[1] * b[0]); }
  function frKey(f) { return f[0] + '/' + f[1]; }

  /* 核心判定：给一串数字 + 一串运算符（顺序固定，括号随便加），能不能得到 target */
  function canMake(nums, ops, target) {
    var n = nums.length;
    if (n === 0) return false;
    if (n === 1) return nums[0] === target;

    var dp = [], i, j, k, len;
    for (i = 0; i < n; i++) { dp.push([]); for (j = 0; j < n; j++) dp[i].push(null); }
    for (i = 0; i < n; i++) {
      var m0 = new Map(); var f0 = fr(nums[i], 1); m0.set(frKey(f0), f0); dp[i][i] = m0;
    }
    for (len = 2; len <= n; len++) {
      for (i = 0; i + len - 1 < n; i++) {
        j = i + len - 1;
        var set = new Map();
        for (k = i; k < j; k++) {
          var A = dp[i][k], B = dp[k + 1][j];
          if (!A || !B) continue;
          var op = ops[k];
          A.forEach(function (a) {
            B.forEach(function (b) {
              var r = op === '+' ? frAdd(a, b) : op === '-' ? frSub(a, b)
                    : op === '*' ? frMul(a, b) : frDiv(a, b);
              if (r) set.set(frKey(r), r);
            });
          });
        }
        dp[i][j] = set;
      }
    }
    var all = dp[0][n - 1], ok = false;
    all.forEach(function (v) { if (v[0] === target * v[1]) ok = true; });
    return ok;
  }

  /* 玩家自己写了小括号时，按常规优先级精确求值；不合法返回 null */
  function evalTokens(tokens) {
    var pos = 0;
    function parseExpr() {
      var v = parseTerm();
      if (v === null) return null;
      while (pos < tokens.length) {
        var t = tokens[pos];
        if (t.k === 'op' && (t.op === '+' || t.op === '-')) {
          pos++;
          var r = parseTerm();
          if (r === null) return null;
          v = (t.op === '+') ? frAdd(v, r) : frSub(v, r);
        } else break;
      }
      return v;
    }
    function parseTerm() {
      var v = parseFactor();
      if (v === null) return null;
      while (pos < tokens.length) {
        var t = tokens[pos];
        if (t.k === 'op' && (t.op === '*' || t.op === '/')) {
          pos++;
          var r = parseFactor();
          if (r === null) return null;
          v = (t.op === '*') ? frMul(v, r) : frDiv(v, r);
          if (v === null) return null;              /* 除以 0 */
        } else break;
      }
      return v;
    }
    function parseFactor() {
      if (pos >= tokens.length) return null;
      var t = tokens[pos];
      if (t.k === 'num') { pos++; return fr(t.v, 1); }
      if (t.k === 'op' && t.op === '(') {
        pos++;
        var v = parseExpr();
        if (v === null) return null;
        if (pos >= tokens.length || tokens[pos].k !== 'op' || tokens[pos].op !== ')') return null;
        pos++;
        return v;
      }
      return null;
    }
    var val = parseExpr();
    if (val === null || pos !== tokens.length) return null;
    return val;
  }

  /* ============================== 像素小画 ==============================
     用 box-shadow 一格一格拼出来，不需要任何图片文件。
     想换成真图：把 SPR_CIRNO 部分换成 <img src="assets/cirno.png"> 即可。 */
  var PAL = {
    H: '#9de6ff', h: '#5cc0ea', B: '#2f7fd0', S: '#ffe3cd', s: '#f2c4a2',
    E: '#20375c', M: '#c85a5a', R: '#ff7b8a', W: '#eaf9ff'
  };
  var SPR_CIRNO = [
    '................',
    '.....hhhhhh.....',
    '...hhHHHHHHhh...',
    '..hHHHHHHHHHHh..',
    '.hHHBBHHHHHHHHh.',
    '.hHBBBBHHHHHHHh.',
    '.hHBBHHHHHHHHHh.',
    '.hHHHHHHHHHHHHh.',
    '.hHSSSSSSSSHHHh.',
    '.hSSSSSSSSSSSHh.',
    '.hSSEESSEESSSHh.',
    '.hSSEESSEESSSHh.',
    '.hSSSSSSSSSSSHh.',
    '..hSSSMMSSSSh...',
    '...hhSSSSSSh....',
    '.....hhhhhh.....'
  ];
  var SPR_BAKA = [
    '................',
    '.....hhhhhh.....',
    '...hhHHHHHHhh...',
    '..hHHHHHHHHHHh..',
    '.hHHBBHHHHHHHHh.',
    '.hHBBBBHHHHHHHh.',
    '.hHBBHHHHHHHHHh.',
    '.hHHHHHHHHHHHHh.',
    '.hHSSSSSSSSHHHh.',
    '.hSSSSSSSSSSSHh.',
    '.hSREESSSSEESRh.',
    '.hSSEESSEESSSHh.',
    '.hSSSSSSSSSSSHh.',
    '..hSSMMMMMMSSh..',
    '...hhSSSSSSh....',
    '.....hhhhhh.....'
  ];
  function pixelSprite(rows, px, pal) {
    pal = pal || PAL;
    var w = rows[0].length, h = rows.length;
    var box = el('div', 'pixel-sprite');
    box.style.width = (w * px) + 'px';
    box.style.height = (h * px) + 'px';
    var dot = el('i');
    dot.style.display = 'block';
    dot.style.width = px + 'px';
    dot.style.height = px + 'px';
    dot.style.background = 'transparent';
    var sh = [];
    for (var y = 0; y < h; y++) {
      for (var x = 0; x < w; x++) {
        var c = rows[y].charAt(x);
        var col = pal[c];
        if (!col) continue;
        sh.push((x * px) + 'px ' + (y * px) + 'px 0 0 ' + col);
      }
    }
    dot.style.boxShadow = sh.join(',');
    box.appendChild(dot);
    return box;
  }

  /* ================================ 音乐 ================================
     素材放在 ex/music/true music/ 里（4 首），点装饰播的是 ex/music/baka/baka.mp3。
     如果某首不响，多半是浏览器不支持 .aac —— 转成 .mp3 再改这里的路径即可。 */
  var TRACKS = [
    { name: 'A · 琪露诺的完美算术教室',
      src: 'ex/music/true music/A【东方PV】琪露诺的完美算术教室【中日歌词_4K_2160p】_849881108.MP3' },
    { name: 'B · ⑨啊哦啦噜噜啦哩哩…',
      src: 'ex/music/true music/B ⑨ P1 ⑨啊哦啦噜噜啦哩哩啦噜噜啦哩哩啦噜噜啦哩哩啦噜噜啦哩哩⑨_3087919043B6.mp3' },
    { name: 'C · 芙兰朵露和她的小fumo',
      src: 'ex/music/true music/C【东方动画】热情高唱～芙兰朵露和她的小fumo P1 【东方动画】芙兰朵露和她的小傻子_41670543354.mp3' },
    { name: 'D · Baka 激唱冬之花',
      src: 'ex/music/true music/D baka P1 Baka激唱冬之花_42536403996.mp3' }
  ];
  var TRACKS_URL = TRACKS.map(function (t) { return encodeURI(t.src); });

  /* 音乐菜单：无音乐 + 4 首 */
  function buildMusicMenu() {
    var menu = $('musicMenu');
    menu.innerHTML = '';
    var opts = [{ name: '无音乐', idx: -1 }];
    TRACKS.forEach(function (t, i) { opts.push({ name: t.name, idx: i }); });
    opts.forEach(function (o) {
      var b = el('button', 'menu-item', o.name);
      b.setAttribute('data-track', o.idx);
      b.onclick = function (e) {
        e.stopPropagation();
        selectTrack(o.idx);
        menu.classList.add('hidden');
        if (G && G.state) setHint(o.idx < 0 ? '已关闭音乐' : '正在播放：' + TRACKS[o.idx].name, 'good');
      };
      menu.appendChild(b);
    });
  }
  var musicIndex = -1;      /* -1 = 无音乐 */
  var audioEl = null;
  var bgmBroken = false;    /* 选了歌但浏览器放不出来（多半是 .aac 不被支持） */

  /* ============ 雪花特效：只在放「D · Baka 激唱冬之花」时出现 ============
     切歌/暂停/恢复都会走 paintMusicUI()，所以雪花在那里同步挂载/移除。 */
  var SPR_FLAKE = [
    '...W...',
    '.W.W.W.',
    '..WWW..',
    'WWWWWWW',
    '..WWW..',
    '.W.W.W.',
    '...W...'
  ];
  var SNOW_TRACK = 3;                    /* 第 4 首（下标 3）= 冬之花 */
  var snowBox = null;

  function setSnow(on) {
    if (on) {
      if (snowBox) return;
      snowBox = el('div', 'snowfield');
      snowBox.setAttribute('aria-hidden', 'true');
      for (var i = 0; i < 24; i++) {
        var s = pixelSprite(SPR_FLAKE, Math.random() < 0.5 ? 2 : 3);
        s.className = 'pixel-sprite snowflake';
        s.style.left = (Math.random() * 100).toFixed(2) + '%';
        s.style.animationDuration = (9 + Math.random() * 12).toFixed(1) + 's';
        s.style.animationDelay = (-Math.random() * 14).toFixed(1) + 's';
        s.style.opacity = (0.35 + Math.random() * 0.5).toFixed(2);
        snowBox.appendChild(s);
      }
      document.body.appendChild(snowBox);
    } else if (snowBox) {
      if (snowBox.parentNode) snowBox.parentNode.removeChild(snowBox);
      snowBox = null;
    }
  }
  function syncSnow() {
    setSnow(bgmOn() && musicIndex === SNOW_TRACK);
  }

  function paintMusicUI() {
    var w = document.querySelector('.music-widget');
    var playing = !!(audioEl && !audioEl.paused && musicIndex >= 0);
    w.classList.toggle('playing', playing);
    w.classList.toggle('muted', musicIndex < 0);
    var items = document.querySelectorAll('#musicMenu .menu-item');
    for (var i = 0; i < items.length; i++) {
      items[i].classList.toggle('active', parseInt(items[i].getAttribute('data-track'), 10) === musicIndex);
    }
    syncSnow();                          /* 只有冬之花那首会下雪 */
  }
  function selectTrack(i) {
    musicIndex = i;
    if (audioEl) { audioEl.pause(); audioEl = null; }
    if (i >= 0) {
      audioEl = new Audio(TRACKS_URL[i]);
      audioEl.loop = true;
      audioEl.volume = 0.55;
      bgmBroken = false;
      hookBgmSurround();                          /* 背景音乐也走环绕（慢慢左右晃） */
      var p = audioEl.play();
      if (p && p.catch) {
        p.catch(function (err) {
          /* 这首放不出来（多半是浏览器不吃 .aac）：记下来，让 baka 音效至少还有声 */
          bgmBroken = true;
          setHint('这首放不出来（浏览器可能不支持 .aac）：' + TRACKS[i].name, 'warn');
          try { console.warn('[BGM] 播放失败：', TRACKS_URL[i], err); } catch (e) {}
          musicIndex = -1;
          audioEl = null;
          paintMusicUI();
          if (G && G.state) setHint('这首 .aac 放不出来 —— 换成 .mp3 就能播；音效不受影响', 'warn');
        });
      }
    }
    paintMusicUI();
  }
  function toggleMusic() {
    if (musicIndex < 0) { selectTrack(0); return; }     /* 无音乐状态下点音符 = 播放第一首 */
    if (!audioEl) { selectTrack(musicIndex); return; }
    if (audioEl.paused) {
      var p = audioEl.play();
      if (p && p.catch) p.catch(function () {});
    } else {
      audioEl.pause();
    }
    paintMusicUI();
  }

  /* ================================ 弹窗 ================================ */
  var modalOkHandler = null, modalCancelHandler = null;
  function showModal(o) {
    var box = $('modalSprite');
    box.innerHTML = '';
    if (o.img) {                                  /* 用真实图片（gif/jpg） */
      var im = el('img', 'modal-img');
      im.src = encodeURI(o.img);
      box.appendChild(im);
    } else if (o.sprite) {                        /* 用内置像素画 */
      box.appendChild(pixelSprite(o.sprite, o.px || 6, o.pal));
    }
    $('modalTitle').textContent = o.title || '';
    var txt = $('modalText');
    if (o.html) { txt.textContent = ''; txt.innerHTML = o.html; }
    else { txt.innerHTML = ''; txt.textContent = o.text || ''; }
    txt.className = 'modal-text' + (o.credit ? ' credit' : '');
    $('modalOk').textContent = o.okText || '确定';
    modalOkHandler = o.onOk || null;
    modalCancelHandler = o.onCancel || null;
    $('modal').classList.toggle('soft', !!o.soft);   /* soft:true → 提示框做虚化（胜利用） */
    $('modal').classList.toggle('failbg', !!o.failbg);/* failbg:true → 遮罩调淡（失败用，露出后面的动画） */
    resetMini('modalMini');                          /* 每次弹窗都从"不透明"开始 */
    $('modalCancel').classList.toggle('hidden', !o.cancelText);
    if (o.cancelText) $('modalCancel').textContent = o.cancelText;
    $('modal').classList.remove('hidden');
  }
  function hideModal() { $('modal').classList.add('hidden'); }

  /* 提示框右上角的 − / + ：切"几乎透明"，方便看清后面的胜利视频 / 失败鬼畜动画 */
  function bindMini(btnId) {
    var b = $(btnId);
    if (!b) return;
    b.onclick = function (e) {
      e.stopPropagation();
      var box = b.parentNode;                        /* .modal-box */
      var ghost = box.classList.toggle('ghost');
      if (box.parentNode) box.parentNode.classList.toggle('ghostbg', ghost);
      b.textContent = ghost ? '+' : '−';
      b.title = ghost ? '还原' : '变透明';
    };
  }
  function resetMini(btnId) {
    var b = $(btnId);
    if (!b) return;
    var box = b.parentNode;
    box.classList.remove('ghost');
    if (box.parentNode) box.parentNode.classList.remove('ghostbg');
    b.textContent = '−';
    b.title = '变透明';
  }

  /* ============================== 屏幕切换 ============================== */
  function showScreen(id) {
    ['screenTitle', 'screenSelect', 'screenGame'].forEach(function (s) {
      $(s).classList.toggle('active', s === id);
    });
    /* 两只弹球在所有界面都一直飘，不会跟着切屏消失 */
  }

  /* ============================== 选关界面 ============================== */
  function puzzlesOfTier(tier) {
    return DATA.puzzles.filter(function (p) { return p.tier === tier; });
  }
  function buildLevelSelect() {
    var top = $('levelRowTop'), bottom = $('levelRowBottom');
    top.innerHTML = ''; bottom.innerHTML = '';
    LEVELS.forEach(function (L, idx) {
      var t = TIERS[L.tier] || {};
      var card = el('button', 'level-card');
      card.appendChild(el('span', 'level-cn', L.cn));
      card.appendChild(el('span', 'level-en', L.tier));
      card.appendChild(el('span', 'level-info',
        (t.size || '?') + ' 个数 · ' + (L.time > 0 ? ('限时 ' + L.time + ' 秒') : '不限时')));
      var cnt = puzzlesOfTier(L.tier).length;
      if (!cnt) {
        card.disabled = true;
        card.appendChild(el('span', 'level-info', '（题库里没有这一档的题）'));
      }
      card.onclick = function () { startLevel(L.no); };
      (idx < 3 ? top : bottom).appendChild(card);
    });
  }

  /* ============================== 游戏主体 ============================== */
  var G = null;   /* 当前这一关的进度 */

  function setHint(text, kind) {
    var h = $('hintBar');
    h.textContent = text || '';
    h.className = 'hint-bar' + (kind ? ' ' + kind : '');
  }

  function startLevel(no) {
    var L = LEVELS[no - 1];
    var pool = shuffle(puzzlesOfTier(L.tier).slice());
    if (!pool.length) { setHint('这一档还没有题目', 'warn'); return; }
    stopWinMedia();                       /* 保险：把上一轮的胜利视频/音轨收干净 */
    failStop();                           /* 保险：把失败动画也收干净 */
    G = {
      level: L, tier: TIERS[L.tier] || {},
      queue: pool, qi: 0,
      round: 0, score: 0, roundScores: [], timeUsed: 0,
      streak: 0, state: null
    };
    showScreen('screenGame');
    updateHud();
    nextPuzzle();
  }

  function nextPuzzle() {
    if (G.round >= G.level.rounds) { winLevel(); return; }
    if (G.qi >= G.queue.length) { G.queue = shuffle(G.queue); G.qi = 0; }
    var src = G.queue[G.qi++];
    var p = src;
    /* 第 2 关起：题目出现时把数字打乱顺序。
       原始数据是按小到大、而且同一组的 4 个数挨在一起 —— 直接显示等于把分组提示给玩家了。
       只打乱"显示用的副本"，题目本身和各组答案不受影响。 */
    if (G.level.no >= 2) {
      p = {};
      for (var kk in src) if (Object.prototype.hasOwnProperty.call(src, kk)) p[kk] = src[kk];
      p.nums = shuffle(src.nums.slice());
    }
    G.round++;
    var st = {
      puzzle: p,
      slots: [], locked: [], used: {},
      active: 0, keys: [], mistakes: 0,
      remain: G.level.time, startAt: 0, timerId: 0,
      pausedAt: 0, pausedTotal: 0, done: false
    };
    for (var i = 0; i < p.groups; i++) { st.slots.push([]); st.locked.push(false); }
    G.state = st;
    setHint('');
    renderPuzzle(); renderPalette(); renderSlots(); updateHud();
    clearEgg();
    startTimer();
    scheduleEgg();
  }

  /* ------------------------------ 计时 ------------------------------
     time = 0 表示不限时（第 4~7 关）；
     彩蛋弹窗时用 pauseTimer()/resumeTimer() 把计时暂停掉。 */
  function elapsedSec() {
    if (!G || !G.state) return 0;
    var paused = G.state.pausedTotal || 0;
    if (G.state.pausedAt) paused += performance.now() - G.state.pausedAt;
    return (performance.now() - G.state.startAt - paused) / 1000;
  }
  function pauseTimer() {
    if (G && G.state && G.level.time > 0 && !G.state.pausedAt) G.state.pausedAt = performance.now();
  }
  function resumeTimer() {
    if (G && G.state && G.state.pausedAt) {
      G.state.pausedTotal = (G.state.pausedTotal || 0) + (performance.now() - G.state.pausedAt);
      G.state.pausedAt = 0;
    }
  }
  function startTimer() {
    stopTimer();
    G.state.startAt = performance.now();
    if (G.level.time <= 0) {                       /* 不限时 */
      paintTimer(-1, 0);
      return;
    }
    paintTimer(G.level.time, G.level.time);
    G.state.timerId = setInterval(function () {
      if (!G || !G.state) return;
      var left = Math.max(0, G.level.time - elapsedSec());
      G.state.remain = left;
      paintTimer(left, G.level.time);
      if (left <= 0) { stopTimer(); failLevel(); }
    }, 100);
  }
  function stopTimer() {
    if (G && G.state && G.state.timerId) { clearInterval(G.state.timerId); G.state.timerId = 0; }
  }
  function paintTimer(left, limit) {
    var f = $('timerFill');
    var t = $('timerText');
    if (limit <= 0) {                              /* 不限时 */
      f.style.width = '100%';
      f.classList.remove('low');
      t.textContent = '不限时';
      t.classList.remove('low');
      return;
    }
    f.style.width = clamp(left / limit * 100, 0, 100) + '%';
    f.classList.toggle('low', left / limit < 0.3);
    t.textContent = left.toFixed(1) + 's';
    t.classList.toggle('low', left <= 3.2);
  }

  function updateHud() {
    $('levelName').textContent = G.level.cn + ' · ' + G.level.tier;
    $('roundInfo').textContent = (G.level.rounds > 1)
      ? ('第 ' + Math.min(G.round, G.level.rounds) + ' / ' + G.level.rounds + ' 次')
      : ('要拼出 ' + (G.tier.groups || 1) + ' 个 9');
    $('scoreText').textContent = G.score + ' 分';
  }

  /* ---------------------------- 渲染 ---------------------------- */
  function renderPuzzle() {
    var box = $('puzzleNums');
    box.innerHTML = '';
    G.state.puzzle.nums.forEach(function (v, i) {
      var d = el('div', 'pnum' + (G.state.used[i] ? ' used' : ''), v);
      box.appendChild(d);
    });
    var meta = '要拼出 ' + G.state.puzzle.groups + ' 个 9';
    if (G.state.puzzle.unused) meta += '（可以剩 ' + G.state.puzzle.unused + ' 个数不用）';
    meta += '　·　每组正好 4 个数（括号可加可不加）';
    /* 有两位数就提示一下键盘怎么打（两位数没法直接键入，用字母代替） */
    var two = [], seen = {};
    G.state.puzzle.nums.forEach(function (v) { if (v >= 10 && !seen[v]) { seen[v] = 1; two.push(v); } });
    if (two.length) {
      two.sort(function (a, b) { return a - b; });
      meta += '　·　两位数用字母：' + two.map(function (v) {
        return String.fromCharCode(97 + v - 10) + '=' + v;
      }).join(' ');
    }
    $('puzzleMeta').textContent = meta;
  }

  function numCount(si) {
    var t = G.state.slots[si], n = 0;
    for (var i = 0; i < t.length; i++) if (t[i].k === 'num') n++;
    return n;
  }
  function openParens(si) {
    var t = G.state.slots[si], n = 0;
    for (var i = 0; i < t.length; i++) {
      if (t[i].k !== 'op') continue;
      if (t[i].op === '(') n++;
      else if (t[i].op === ')') n--;
    }
    return n;
  }
  function canAddNumber(si) {
    if (G.state.locked[si]) return false;
    if (numCount(si) >= 4) return false;
    var t = G.state.slots[si], last = t[t.length - 1];
    if (!last) return true;
    return last.k === 'op' && last.op !== ')';      /* 运算符或 ( 后面才能接数字 */
  }
  function canAddOp(si, op) {
    if (G.state.locked[si]) return false;
    var t = G.state.slots[si], last = t[t.length - 1];
    if (op === '(') return !last || (last.k === 'op' && last.op !== ')');
    if (op === ')') {
      if (!last) return false;
      var prevOk = (last.k === 'num') || (last.k === 'op' && last.op === ')');
      return prevOk && openParens(si) > 0;
    }
    /* + - * / ：左边必须是一个数或者右括号 */
    return !!last && (last.k === 'num' || (last.k === 'op' && last.op === ')'));
  }
  function allLocked() {
    for (var i = 0; i < G.state.locked.length; i++) if (!G.state.locked[i]) return false;
    return true;
  }
  function unlockedCount() {
    var n = 0;
    for (var i = 0; i < G.state.locked.length; i++) if (!G.state.locked[i]) n++;
    return n;
  }

  function renderPalette() {
    var np = $('numPad');
    np.innerHTML = '';
    G.state.puzzle.nums.forEach(function (v, i) {
      var b = el('button', 'pad-btn num', v);
      b.disabled = !!G.state.used[i] || !canAddNumber(G.state.active);
      b.onclick = function () { addNumber(i); };
      np.appendChild(b);
    });

    var op = $('opPad');
    op.innerHTML = '';
    ['+', '-', '*', '/', '(', ')', '='].forEach(function (o) {
      var isParen = (o === '(' || o === ')');
      var b = el('button', 'pad-btn op' + (isParen ? ' paren' : ''), opText(o));
      if (o === '=') {
        b.onclick = function () { verifySlot(G.state.active); };
      } else {
        b.disabled = !canAddOp(G.state.active, o);
        b.onclick = function () { addOp(o); };
      }
      op.appendChild(b);
    });

    var ex = $('extraBtn');
    var canExtra = allLocked() && extraLeft() > 0;
    ex.disabled = !canExtra;
    var pct = Math.round(extraRate() * 100);
    ex.textContent = '再来一种解法 +' + pct + '%';
    ex.classList.toggle('hidden', extraMax() <= 0);
  }

  function renderSlots() {
    var box = $('slots');
    box.innerHTML = '';
    G.state.slots.forEach(function (t, i) {
      var s = el('div', 'slot' + (i === G.state.active ? ' active' : '') +
                        (G.state.locked[i] ? ' locked' : ''));
      s.appendChild(el('span', 'slot-tag', '第' + (i + 1) + '组'));
      if (!t.length) s.appendChild(el('span', 'slot-hint', '点下面的数字和运算符…'));
      t.forEach(function (x) {
        var cls = (x.k === 'num') ? 'tok num'
                : ('tok op' + ((x.op === '(' || x.op === ')') ? ' paren' : ''));
        s.appendChild(el('div', cls, x.k === 'num' ? x.v : opText(x.op)));
      });
      if (G.state.locked[i]) s.appendChild(el('span', 'slot-ok', '✔'));
      s.onclick = function () { selectSlot(i); };
      box.appendChild(s);
    });
  }

  function flashSlot(i) {
    var nodes = document.querySelectorAll('#slots .slot');
    var s = nodes[i];
    if (!s) return;
    s.classList.add('shake', 'bad');
    setTimeout(function () { s.classList.remove('shake', 'bad'); }, 460);
  }

  /* ---------------------------- 交互 ---------------------------- */
  function selectSlot(i) {
    if (G.state.locked[i]) {
      /* 点已锁定的组 = 解锁重做 */
      G.state.locked[i] = false;
      setHint('第 ' + (i + 1) + ' 组已解锁，可以重做', '');
    }
    G.state.active = i;
    renderSlots(); renderPalette();
  }

  function addNumber(pi) {
    if (!G || !G.state) return;
    var s = G.state.active;
    if (G.state.used[pi]) return;
    if (!canAddNumber(s)) {
      flashSlot(s);
      setHint('这一组已经有 4 个数了，或者该点运算符了', 'warn');
      return;
    }
    G.state.slots[s].push({ k: 'num', i: pi, v: G.state.puzzle.nums[pi] });
    G.state.used[pi] = true;
    setHint('');
    renderPuzzle(); renderSlots(); renderPalette();
  }

  function addOp(op) {
    if (!G || !G.state) return;
    var s = G.state.active;
    if (!canAddOp(s, op)) { flashSlot(s); return; }
    G.state.slots[s].push({ k: 'op', op: op });
    setHint('');
    renderSlots(); renderPalette();
  }

  function delOne() {
    if (!G || !G.state) return;
    var s = G.state.active;
    if (G.state.locked[s]) { G.state.locked[s] = false; renderSlots(); renderPalette(); return; }
    var t = G.state.slots[s];
    var x = t.pop();
    if (x && x.k === 'num') delete G.state.used[x.i];
    renderPuzzle(); renderSlots(); renderPalette();
  }

  function clearSlot() {
    if (!G || !G.state) return;
    var s = G.state.active;
    G.state.slots[s].forEach(function (x) { if (x.k === 'num') delete G.state.used[x.i]; });
    G.state.slots[s] = [];
    G.state.locked[s] = false;
    renderPuzzle(); renderSlots(); renderPalette();
  }

  /* 按「=」检查当前这一组 */
  function verifySlot(si) {
    if (!G || !G.state) return;
    var t = G.state.slots[si];
    if (!t.length) { flashSlot(si); setHint('先点数字和运算符组一个算式', 'warn'); return; }
    if (G.state.locked[si]) { setHint('第 ' + (si + 1) + ' 组已经对了', 'good'); return; }

    var nums = [], ops = [];
    t.forEach(function (x) { if (x.k === 'num') nums.push(x.v); else ops.push(x.op); });
    if (nums.length !== 4) {
      flashSlot(si);
      setHint('这一组要正好用 4 个数（现在 ' + nums.length + ' 个）', 'warn');
      return;
    }

    var hasParen = t.some(function (x) { return x.k === 'op' && (x.op === '(' || x.op === ')'); });
    var ok;
    if (hasParen) {
      if (openParens(si) !== 0) { flashSlot(si); setHint('小括号没有配对～', 'warn'); return; }
      var val = evalTokens(t);
      ok = (val !== null) && (val[0] === TARGET * val[1]);
    } else {
      /* 没写括号时宽容判定：自动尝试所有加括号的方式 */
      ok = canMake(nums, ops, TARGET);
    }
    if (!ok) {
      flashSlot(si);
      G.state.mistakes++;
      setHint('这 4 个数这样凑不出 9，再想想～（括号可加可不加）', 'warn');
      return;
    }

    G.state.locked[si] = true;
    setHint('第 ' + (si + 1) + ' 组：正确！', 'good');
    var n = G.state.slots.length;
    for (var i = 1; i <= n; i++) {
      var j = (si + i) % n;
      if (!G.state.locked[j]) { G.state.active = j; break; }
    }
    renderSlots(); renderPalette();
    if (allLocked()) setHint('全部拼好了！点右下角「完成」收下这一分', 'good');
  }

  /* ------------------------- 多解 / 加成 ------------------------- */
  function extraMax() {
    var ms = (SCORING.multiSolve || {})[G.level.tier];
    if (ms && ms.maxExtra) return ms.maxExtra;
    var ss = SCORING.secondSolution || {};
    if ((ss.tiers || []).indexOf(G.level.tier) >= 0) return 1;
    return 0;
  }
  function extraRate() {
    var ms = (SCORING.multiSolve || {})[G.level.tier];
    if (ms && ms.extraRate) return ms.extraRate;
    var ss = SCORING.secondSolution || {};
    if ((ss.tiers || []).indexOf(G.level.tier) >= 0) return ss.rate || 0.15;
    return 0;
  }
  function extraLeft() { return extraMax() - Math.max(0, G.state.keys.length - 1); }

  /* 解法指纹：多组题看"划分"（每组数字排序后比较），单组题看算式字符串 */
  function solutionKey() {
    if (G.state.slots.length === 1) {
      return G.state.slots[0].map(function (x) { return x.k === 'num' ? x.v : x.op; }).join('');
    }
    return G.state.slots.map(function (t) {
      return t.filter(function (x) { return x.k === 'num'; })
              .map(function (x) { return x.v; })
              .sort(function (a, b) { return a - b; })
              .join('-');
    }).sort().join('|');
  }
  function recordKey() {
    var k = solutionKey();
    if (G.state.keys.indexOf(k) < 0) G.state.keys.push(k);
    return k;
  }

  function useExtra() {
    if (!allLocked()) { setHint('先把这一组解完整（每组按 = 检查）', 'warn'); return; }
    recordKey();
    var used = G.state.keys.length - 1;
    if (used >= extraMax()) { setHint('这一题的多解加成已经拿满了', 'warn'); renderPalette(); return; }
    /* 清空重来，用另一种解法再解一次 */
    G.state.slots = G.state.slots.map(function () { return []; });
    G.state.locked = G.state.locked.map(function () { return false; });
    G.state.used = {};
    G.state.active = 0;
    setHint('再给一种不同的解法（+ ' + Math.round(extraRate() * 100) + '%）', 'good');
    renderPuzzle(); renderSlots(); renderPalette();
  }

  /* ---------------------------- 计分 ---------------------------- */
  function diffFactor(p) {
    var r = SCORE_RANGE[p.tier];
    var mm = SCORING.diffMult || [0.85, 1.15];
    if (!r || r.max <= r.min || typeof p.score !== 'number') return (mm[0] + mm[1]) / 2;
    var t = clamp((p.score - r.min) / (r.max - r.min), 0, 1);
    return mm[0] + (mm[1] - mm[0]) * t;
  }
  function timeFactor() {
    /* 不再按时间扣分：做出题本身就不容易，快慢都给一样的系数。
       （想恢复"解得快加分"，把下面这行删掉，就会用回 SCORING.time 那套 0.85~1.15） */
    return 1.0;
  }
  function roundScore(extras) {
    var base = G.tier.base || 100;
    var bonus = extras * extraRate();
    var comboStep = (SCORING.combo || {}).step || 0.05;
    var comboMax = (SCORING.combo || {}).max || 0.25;
    var combo = Math.min(G.streak * comboStep, comboMax);
    var sc = base * diffFactor(G.state.puzzle) * timeFactor() * (1 + bonus + combo);
    return { score: Math.round(sc), bonus: bonus, combo: combo };
  }

  /* ---------------------------- 完成一题 ---------------------------- */
  function completeRound() {
    if (!G || !G.state) return;
    /* 先自动检查那些"填满 4 个数但还没按 = "的组：
       这样玩家写好算式直接点「完成」也能提交，不会白写一次 */
    var failedSlot = -1;
    for (var i = 0; i < G.state.slots.length; i++) {
      if (!G.state.locked[i] && numCount(i) === 4) {
        verifySlot(i);
        if (!G.state.locked[i] && failedSlot < 0) failedSlot = i;
      }
    }
    if (!allLocked()) {
      if (failedSlot >= 0) {                        /* 有组算错了：把它的报错留住 */
        flashSlot(failedSlot);
        setHint('第 ' + (failedSlot + 1) + ' 组的算式凑不出 9，检查一下～', 'warn');
      } else {
        flashSlot(G.state.active);
        setHint('还有 ' + unlockedCount() + ' 组没做完（每组要正好用 4 个数）', 'warn');
      }
      return;
    }
    if (G.state.done) return;                       /* 防止双击「完成」算两遍分 */
    G.state.done = true;
    recordKey();
    stopTimer();
    var extras = Math.min(G.state.keys.length - 1, extraMax());
    var r = roundScore(extras);
    G.score += r.score;
    G.roundScores.push(r.score);
    G.timeUsed += elapsedSec();
    if (G.state.mistakes === 0) G.streak++; else G.streak = 0;
    updateHud();

    var msg = '完成！+' + r.score + ' 分';
    if (extras > 0) msg += '（多解 ×' + extras + ' +' + Math.round(r.bonus * 100) + '%）';
    setHint(msg, 'good');

    setTimeout(function () {
      if (!G) return;
      if (G.round >= G.level.rounds) winLevel(); else nextPuzzle();
    }, 700);
  }

  /* --------------------------- 胜 / 负 --------------------------- */
  /* ============ 胜利画面的 baka 视频（音画同步） ============
     做法：视频本身静音播放当画面，声音用单独给的那条音轨；
     每 0.5 秒对一次表，偏差超过 0.12 秒就把音频拉到视频的时间点 —— 音画不会越飘越远。
     文件名里有 🧊 和空格，所以统一用 encodeURI 之后再交给 <video>/Audio。 */
  var WIN_VIDEO_SRC = 'ex/victory conclusion/baka🧊_29591406094.mp4';
  var WIN_AUDIO_SRC = 'ex/victory conclusion/baka🧊_29591406094.aac';
  var winVideo = null, winAudio = null, winSyncTimer = 0, bgmWasPlaying = false;
  /* 赢的这一刻到底有没有在放背景音乐：
     没有（选了「无音乐」或音乐被暂停）→ 整个世界静音，胜利视频也只有画面没有声。 */
  var winSoundOn = false;

  function playWinAudio() {
    if (!winSoundOn) return;                 /* 无音乐 = 全静音 */
    if (!winAudio) return;
    var p = winAudio.play();
    if (p && p.catch) p.catch(function () {
      if (G && G.state) setHint('浏览器拦了自动播放：点一下「确定」就会响', 'warn');
    });
  }
  function startWinSync() {
    if (winSyncTimer) clearInterval(winSyncTimer);
    winSyncTimer = setInterval(function () {
      if (!winVideo || !winAudio) return;
      if (winVideo.paused || winAudio.paused) return;
      var d = winVideo.currentTime - winAudio.currentTime;
      if (d > 0.12 || d < -0.12) { try { winAudio.currentTime = winVideo.currentTime; } catch (e) {} }
    }, 500);
  }
  function startWinMedia() {
    var layer = $('winLayer');
    if (!layer) return;
    layer.classList.remove('hidden');

    if (!winVideo) {
      winVideo = $('winVideo');
      winVideo.src = encodeURI(WIN_VIDEO_SRC);
      winVideo.muted = true;                 /* 画面静音，声音走下面这条音轨 */
      winVideo.loop = true;
      winVideo.load();
      winAudio = new Audio(encodeURI(WIN_AUDIO_SRC));
      winAudio.loop = true;
      winAudio.volume = 0.95;
    }
    /* 先记住"赢的这一刻有没有在放音乐"，再让背景音乐让位 */
    winSoundOn = bgmOn();
    if (audioEl && !audioEl.paused) { bgmWasPlaying = true; audioEl.pause(); paintMusicUI(); }

    try { winVideo.currentTime = 0; } catch (e) {}
    try { winAudio.currentTime = 0; } catch (e) {}
    var pv = winVideo.play(); if (pv && pv.catch) pv.catch(function () {});
    playWinAudio();                            /* winSoundOn 为 false 时这里什么也不会发生 */
    startWinSync();
  }
  function stopWinMedia() {
    if (winSyncTimer) { clearInterval(winSyncTimer); winSyncTimer = 0; }
    if (winVideo) { try { winVideo.pause(); } catch (e) {} }
    if (winAudio) { try { winAudio.pause(); } catch (e) {} }
    var layer = $('winLayer');
    if (layer) layer.classList.add('hidden');
    if (bgmWasPlaying && audioEl) {
      var p = audioEl.play(); if (p && p.catch) p.catch(function () {});
      bgmWasPlaying = false;
      paintMusicUI();
    }
  }

  /* 放弃：先确认，再按失败处理（确认框开着的时候把计时停住） */
  function askGiveUp() {
    if (!G || !G.state) return;
    pauseTimer();
    showModal({
      img: 'ex/picture/BA1.jpg',
      title: '要放弃这道题吗？',
      text: '放弃的话这一关就按失败算哦~',
      okText: '放弃', cancelText: '再想想',
      onOk: function () { hideModal(); giveUp(); },
      onCancel: function () { hideModal(); resumeTimer(); }
    });
  }
  function giveUp() {
    clearEgg();
    stopTimer();
    showModal({
      img: 'ex/picture/BA.gif',
      title: '你放弃了 ' + G.level.cn + ' 的这一题…',
      text: '你是杂鱼还是baka呢，这是一个问题~',
      okText: '确定',
      onOk: function () { hideModal(); showResult(false); }
    });
  }

  /* ===================== 失败结算动画 =====================
     用 B* / F* / single 这些素材做一场"鬼畜 baka 大乱斗"。一轮的时间轴：
       0 ~ 3 秒 ：每喊一声 baka，就在随机位置丢出一张 picture 素材（声音和素材同拍）
       3 ~ 7 秒 ：baka 仍然照原来的节奏喊，但素材出现得越来越快（700ms → 90ms 线性缩短）
       ≈7 秒   ：single.jpg 旋转着放大"突脸"，最后定格
       定格之后 ：等最后一声 baka 放完，再等 0.5 秒 → 重来（再跑一轮）
     图层：z-index 20 —— 在提示框/结算框（z-index 60）之下，但盖得住封面那些装饰素材。
     静音规则照旧：没在放背景音乐时只有画面、没有声音。 */
  /* 只用静态图（jpg/png）：动图 GIF 数量一多就要一直解码，最费资源。
     反正一轮只留 3 张不同的图反复丢，鬼畜感反而更统一。 */
  var FAIL_PICS = ['B.jpg', 'BA1.jpg', 'single.jpg'];
  var FAIL_JUMP_SRC = 'ex/picture/bo.jpg';      /* 压轴突脸那张（不放进随机池，留个惊喜） */
  var FAIL_BAKA_EVERY = 220;      /* 每轮 baka 的间隔（毫秒）；每轮再叠 4~6 声，密密麻麻 */
  var FAIL_PIC_START  = 260;      /* 开局素材间隔（比原来快 2.7 倍；嫌卡就再往大调） */
  var FAIL_PIC_END    = 32;       /* 7 秒时素材间隔（比原来快 2.8 倍） */
  var FAIL_ACCEL_FROM = 3000;     /* 从第 3 秒开始加速 */
  var FAIL_ACCEL_TO   = 7000;     /* 第 7 秒突脸 */

  var failLayer = null, failTick = 0, failSoundOn = false, failBgmWasPlaying = false;

  function failStop(resumeBgm) {
    if (failTick) { clearInterval(failTick); failTick = 0; }
    if (failLayer && failLayer.parentNode) failLayer.parentNode.removeChild(failLayer);
    failLayer = null;
    if (resumeBgm !== false && failBgmWasPlaying && audioEl) {
      var p = audioEl.play(); if (p && p.catch) p.catch(function () {});
      failBgmWasPlaying = false;
      paintMusicUI();
    }
  }
  function failPic() {
    if (!failLayer) return;
    var im = el('img', 'fail-pic');
    im.src = encodeURI('ex/picture/' + FAIL_PICS[Math.floor(Math.random() * FAIL_PICS.length)]);
    var W = window.innerWidth, H = window.innerHeight;
    var size = 90 + Math.random() * 170;
    im.style.width = size.toFixed(0) + 'px';
    im.style.left = (Math.random() * Math.max(0, W - size)).toFixed(0) + 'px';
    im.style.top = (Math.random() * Math.max(0, H - size)).toFixed(0) + 'px';
    im.style.setProperty('--r', (Math.random() * 60 - 30).toFixed(1) + 'deg');
    im.style.opacity = (0.75 + Math.random() * 0.25).toFixed(2);
    failLayer.appendChild(im);
  }
  function failJump() {
    if (!failLayer) return;
    var im = el('img', 'fail-jump');
    im.src = encodeURI(FAIL_JUMP_SRC);
    failLayer.appendChild(im);
  }
  /* 一次"爆发"：错开几十毫秒连叠 n 声 baka，声音更厚 */
  function bakaBurst(n, vol) {
    for (var i = 0; i < n; i++) {
      (function (k) {
        setTimeout(function () {
          if (failLayer && failSoundOn) playBakaRaw(Math.max(0.4, vol - k * 0.05), false);
        }, k * (40 + Math.random() * 110));
      })(i);
    }
  }
  function failCycle() {
    if (!failLayer) return;
    failLayer.innerHTML = '';                       /* 清掉上一轮丢出来的素材 */
    var t0 = performance.now();
    var lastBaka = -1e9, lastPic = -1e9, jumped = false, jumpAt = 0;
    failTick = setInterval(function () {
      if (!failLayer) { clearInterval(failTick); failTick = 0; return; }
      var t = performance.now() - t0;
      if (t - lastBaka >= FAIL_BAKA_EVERY) {        /* baka：固定节奏，每次叠 4~6 声 */
        lastBaka = t;
        if (failSoundOn) bakaBurst(4 + Math.floor(Math.random() * 3), 0.85);
      }
      var every = FAIL_PIC_START;                   /* 素材：3 秒后越来越快 */
      if (t > FAIL_ACCEL_FROM) {
        var k = Math.min(1, (t - FAIL_ACCEL_FROM) / (FAIL_ACCEL_TO - FAIL_ACCEL_FROM));
        every = FAIL_PIC_START + (FAIL_PIC_END - FAIL_PIC_START) * k;
      }
      /* 一帧里可能该补好几张（最快时 16ms 一张），最多补 8 张免得卡死。
         注意：一轮之内素材**只增不减**，全部留到这一轮结束再一起清掉。 */
      var n = 0;
      while (t - lastPic >= every && n < 8) { lastPic += every; failPic(); n++; }
      if (!jumped && t >= FAIL_ACCEL_TO) { jumped = true; jumpAt = t; failJump(); }
      /* 定格后：定格画面留 1.5 秒，等最后一声 baka 放完再等 0.5 秒 → 重来 */
      if (jumped && t - jumpAt >= 1500 + bakaDur * 1000 + 500) {
        clearInterval(failTick); failTick = 0;
        failCycle();
      }
    }, 16);
  }
  function startFailShow() {
    failStop(false);                                /* 先收干净，但别急着恢复音乐 */
    failSoundOn = bgmOn();                          /* 没放音乐 → 这场也只有画面 */
    if (failSoundOn) {
      failBgmWasPlaying = true;
      audioEl.pause();
      paintMusicUI();
      bakaBurst(8, 0.9);                            /* 开场直接叠 8 声 */
    }
    failLayer = el('div', 'fail-layer');
    document.body.appendChild(failLayer);
    failCycle();
  }

  function failLevel() {
    clearEgg();
    stopTimer();
    startFailShow();                                 /* 提示框一出现，后面的鬼畜动画就开跑 */
    showModal({
      img: 'ex/picture/BA.gif',                      /* BAKA 图 */
      failbg: true,                                  /* 遮罩调淡，看得见后面乱飞的素材 */
      title: '琪露诺说你是baka！！',
      text: G.level.cn + '（' + G.level.tier + '）'
            + (G.level.time > 0 ? ('没能在 ' + G.level.time + ' 秒内拼出来…') : '这一题没拼出来…'),
      okText: '确定',
      onOk: function () { hideModal(); showResult(false); }
    });
  }
  function winLevel() {
    clearEgg();
    stopTimer();
    startWinMedia();                               /* 立马开 baka 视频（音画同步） */
    showModal({
      img: 'ex/picture/F1.gif',                      /* 可爱的 fumo 特写 */
      soft: true,                                    /* 压在 baka 视频上，做虚化 */
      title: '恭喜通关 ' + G.level.cn + '（' + G.level.tier + '）！',
      text: '看来你不是baka呢~有点可惜……',
      okText: '确定',
      onOk: function () {
        hideModal();
        playWinAudio();                              /* 万一被浏览器拦了，点确定时补一次 */
        showResult(true);
      }
    });
  }

  function showResult(win) {
    var byEgg = !!(win && G && G.eggWin);                   /* 这次是不是靠彩蛋赢的 */
    $('resultTitle').textContent = win
      ? (byEgg ? '恭喜你发现彩蛋啦' : '胜利结算')
      : '失败结算';
    var avg = G.roundScores.length
      ? Math.round(G.roundScores.reduce(function (a, b) { return a + b; }, 0) / G.roundScores.length) : 0;
    var html = '';
    if (byEgg) {
      html += '<div class="egg-strike" style="margin:0 0 12px">（虽然这是作者答不对题时遗留的测试通道）</div>';
    }
    html += '<div>关卡：<b>' + G.level.cn + ' · ' + G.level.tier + '</b></div>';
    if (G.level.rounds > 1) {
      html += '<div>完成次数：<b>' + (win ? G.level.rounds : G.roundScores.length) +
              ' / ' + G.level.rounds + '</b></div>';
    }
    html += '<div>本关得分：<b>' + G.score + '</b>（每次平均 ' + avg + '）</div>';
    $('resultBody').innerHTML = html;
    resetMini('resultMini');                                /* 结算框每次打开都从"不透明"开始 */
    $('resultScreen').classList.remove('hidden');
    $('resultScreen').classList.toggle('failmode', !win);   /* 失败时把遮罩调淡，看得清后面的乱飞素材 */
    if (win) failStop();                                    /* 胜利：收掉失败动画 */
    else if (!failLayer) startFailShow();                   /* 失败：还没跑就补开（正常在提示框那步就开了） */
  }

  /* ============================== 事件绑定 ============================== */
  function bind() {
    /* 首页 */
    $('startBtn').onclick = function () { showScreen('screenSelect'); };
    $('backToTitle').onclick = function () { showScreen('screenTitle'); };
    $('quitBtn').onclick = function () {
      stopTimer();
      showModal({
        img: 'ex/picture/BA1.jpg', title: '要退出这一关吗？',
        text: '当前进度不会保存哦。', okText: '退出', cancelText: '继续玩',
        onOk: function () {
          hideModal();
          clearEgg();                                /* 把还没点的彩蛋收掉，免得跟到选关界面 */
          stopWinMedia();
          G = null;
          showScreen('screenSelect');
        }
      });
    };

    /* 顶栏 */
    $('musicToggle').onclick = toggleMusic;
    $('musicArrow').onclick = function (e) {
      e.stopPropagation();
      $('musicMenu').classList.toggle('hidden');
    };
    document.addEventListener('click', function () { $('musicMenu').classList.add('hidden'); });
    $('infoBtn').onclick = function () {
      showModal({
        img: 'ex/picture/T.gif',                     /* 打鼓的 fumo 当封面吉祥物 */
        title: '关于 · 致谢',
        credit: true,
        html:
          '<p><b>琪露诺の挑战</b></p>' +
          '<p>本小游戏灵感来源于网友 aliao（QQ：815*****50）。</p>' +
          '<p>相关素材均来源于互联网，对以下视频作者表示感谢：</p>' +
          '<ul class="credit-list">' +
            '<li><a href="https://b23.tv/T3J1hg5" target="_blank" rel="noreferrer">b23.tv/T3J1hg5</a>' +
              '　Baka激唱冬之花　<span class="credit-up">up：点击此处查看更多信息</span></li>' +
            '<li><a href="https://b23.tv/7KAsHBP" target="_blank" rel="noreferrer">b23.tv/7KAsHBP</a>' +
              '　⑨啊哦啦噜噜啦哩哩啦噜噜啦哩哩啦噜噜啦哩哩啦噜噜啦哩哩⑨　<span class="credit-up">up：布洛妮码a</span></li>' +
            '<li><a href="https://b23.tv/P6EjaYV" target="_blank" rel="noreferrer">b23.tv/P6EjaYV</a>' +
              '　【东方动画】热情高唱～芙兰朵露和她的小fumo　<span class="credit-up">up：幻想乡的新月</span></li>' +
            '<li><a href="https://b23.tv/eVw3wHv" target="_blank" rel="noreferrer">b23.tv/eVw3wHv</a>' +
              '　转发你认识的笨蛋然后什么也不说　<span class="credit-up">up：逐風Caly</span></li>' +
            '<li><a href="https://b23.tv/I1MXq9K" target="_blank" rel="noreferrer">b23.tv/I1MXq9K</a>' +
              '　没事，你继续刷　<span class="credit-up">up：桑德枫迪亚</span></li>' +
            '<li><a href="https://b23.tv/iSZ9ryY" target="_blank" rel="noreferrer">b23.tv/iSZ9ryY</a>' +
              '　fumo们的可爱动静　<span class="credit-up">up：暖圆圆Idyliss</span></li>' +
          '</ul>' +
          '<p class="credit-date">26.10.18</p>',
        okText: '好的'
      });
    };

    /* 游玩区 */
    $('delBtn').onclick = delOne;
    $('clearBtn').onclick = clearSlot;
    $('doneBtn').onclick = completeRound;
    $('extraBtn').onclick = useExtra;
    $('giveUpBtn').onclick = askGiveUp;
    bindMini('modalMini');                          /* 提示框右上角 − / + */
    bindMini('resultMini');                         /* 结算框右上角 − / + */

    /* 弹窗 */
    $('modalOk').onclick = function () {
      var h = modalOkHandler; modalOkHandler = null;
      if (h) h(); else hideModal();
    };
    $('modalCancel').onclick = function () {
      var h = modalCancelHandler;
      modalOkHandler = null; modalCancelHandler = null;
      if (h) h(); else hideModal();
    };

    /* 结算 */
    $('resultRetry').onclick = function () {
      $('resultScreen').classList.add('hidden');
      failStop();                                    /* 收掉失败动画，恢复背景音乐 */
      stopWinMedia();                                /* 收掉视频/音轨，恢复背景音乐 */
      startLevel(G.level.no);
    };
    $('resultBack').onclick = function () {
      $('resultScreen').classList.add('hidden');
      failStop();
      stopWinMedia();
      G = null;
      showScreen('screenSelect');
    };

    /* 键盘（方便测试）：数字键选数、+ - * / 选运算符、回车/= 检查、退格删除 */
    document.addEventListener('keydown', function (e) {
      var k = e.key;
      /* 什么算"正在做题"：游戏界面开着 + 有题目 + 没弹窗 + 没在结算画面 */
      var solving = $('screenGame').classList.contains('active') &&
                    !!(G && G.state) &&
                    $('modal').classList.contains('hidden') &&
                    $('resultScreen').classList.contains('hidden');

      /* 不在做题的时候：按键盘上的 9 就是一声 baka（⑨），连按会叠成 bababababaka。
         做题时 9 交给下面的逻辑当数字用，不出声。 */
      if (!solving) {
        if (k === '9') playBaka(0.9, 'click');
        return;
      }
      /* 数字：1~9 直接用数字键；两位数（10~18）用字母代替
         a=10 b=11 c=12 d=13 e=14 f=15 g=16 h=17 i=18（大小写都行） */
      var want = 0;
      if (/^[0-9]$/.test(k)) want = parseInt(k, 10);
      else if (/^[a-iA-I]$/.test(k)) want = 10 + (k.toLowerCase().charCodeAt(0) - 97);
      if (want > 0) {
        var nums = G.state.puzzle.nums;
        for (var i = 0; i < nums.length; i++) {
          if (nums[i] === want && !G.state.used[i]) {
            /* 只有真的把 9 填进去的时候才 baka 一声（这题没有可用 9 就完全没反应） */
            if (k === '9') playBaka(0.9, 'click');
            addNumber(i); e.preventDefault(); return;
          }
        }
      } else if (k === '+') { addOp('+'); e.preventDefault(); }
      else if (k === '-') { addOp('-'); e.preventDefault(); }
      else if (k === '*') { addOp('*'); e.preventDefault(); }
      else if (k === 'x' || k === 'X') { addOp('*'); e.preventDefault(); }
      else if (k === '/') { addOp('/'); e.preventDefault(); }
      else if (k === '(') { addOp('('); e.preventDefault(); }
      else if (k === ')') { addOp(')'); e.preventDefault(); }
      else if (k === '=' || k === 'Enter') { verifySlot(G.state.active); e.preventDefault(); }
      else if (k === 'Backspace') { delOne(); e.preventDefault(); }
      else if (k === 'Escape') { $('quitBtn').click(); }
    });
  }

  /* ============================ 装饰层小玩意 ============================
     · 点固定装饰图 = 播 baka.mp3（带 class="deco-click" 的都算）
     · B5.gif 当慢速弹球在屏幕里碰来碰去（图层在 UI 下面，不挡按钮） */
  /* ---- baka 音效：用一个"播放器池"实现鬼畜叠音 ----
     点得快的时候，每一次点击都会占用一个新的播放器，于是声音会一层层叠起来
     —— 就是 bababababababaka 的效果。
     随机掺入（mix）则相反：只要还有一声在响就跳过，避免和音乐打架。 */
  var BAKA_VOICES = 8;                    /* 同时最多叠 8 层（想更鬼畜就加大） */
  var BAKA_SRC = encodeURI('ex/music/baka/baka.mp3');
  var bakaPool = [];
  var bakaIdx = 0;
  var bakaDur = 1.0;                      /* baka.mp3 有多长（秒），从元数据读，失败动画要用 */

  function initBakaPool() {
    for (var i = 0; i < BAKA_VOICES; i++) {
      var a = new Audio(BAKA_SRC);
      a.preload = 'auto';
      if (i === 0) {
        a.addEventListener('loadedmetadata', function () {      /* 记下 baka 有多长 */
          if (isFinite(a.duration) && a.duration > 0) bakaDur = a.duration;
        });
      }
      bakaPool.push(a);
    }
  }
  function bakaBusy() {
    for (var i = 0; i < bakaPool.length; i++) if (!bakaPool[i].paused) return true;
    return false;
  }
  function bgmOn() { return !!(audioEl && !audioEl.paused && musicIndex >= 0); }
  /* baka 音效到底允不允许出声：
     1) 音乐真的在放 → 允许（原规则）；
     2) 选了歌但浏览器放不出来（多半 .aac 不吃）→ 也允许，否则整局会一声都没有。 */
  function bakaAllowed() { return bgmOn() || bgmBroken; }

  /* ---- 环绕音（Web Audio 立体声定位） ----
     ⚠️ 关键坑：给 <audio> 接线（createMediaElementSource）之后，这个元素就**只能**通过音频图出声，
        一旦 AudioContext 没跑起来（自动播放策略把 resume() 拒了），那一路就彻底静音而且不可逆。
     所以这里的规矩是：**只在确认 AudioContext 是 running 的时候才接线**，
        否则保持元素的普通播放 —— 宁可没有环绕，也绝不能让声音消失。 */
  var audioCtx = null, bakaPanners = [], panPhase = 0;
  var bgmPanner = null, bgmPanTimer = 0;
  /* 环绕音总开关。默认关闭 —— 先保证有声音；
     等确认基础播放正常后，想再试环绕就把它改成 true。 */
  var SURROUND_ON = false;

  function ensureAudioCtx() {
    if (audioCtx) return audioCtx;
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return null;
    try { audioCtx = new AC(); } catch (e) { audioCtx = null; }
    return audioCtx;
  }
  function ctxRunning() { return !!(audioCtx && audioCtx.state === 'running'); }

  /* 把第 i 个 baka 播放器接到 panner 上（每路只接一次，且只在 ctx 跑起来时接） */
  function hookVoice(i) {
    var a = bakaPool[i];
    if (!a) return null;
    if (a.__routed) return bakaPanners[i] || null;
    if (!SURROUND_ON) return null;
    if (!ctxRunning()) return null;
    try {
      var src = audioCtx.createMediaElementSource(a);
      var pan = audioCtx.createStereoPanner ? audioCtx.createStereoPanner() : null;
      if (pan) { src.connect(pan); pan.connect(audioCtx.destination); }
      else src.connect(audioCtx.destination);
      a.__routed = true;
      bakaPanners[i] = pan;
      return pan;
    } catch (e) { return null; }                 /* 接不上就保持普通播放 */
  }

  /* 背景音乐也加环绕：幅度小、周期慢，像在耳边缓慢绕圈 */
  function hookBgmSurround() {
    if (!SURROUND_ON) return;
    if (!audioEl || audioEl.__routed) return;
    ensureAudioCtx();
    if (!audioCtx) return;
    var go = function () {
      if (!ctxRunning() || !audioEl || audioEl.__routed) return;
      try {
        var src = audioCtx.createMediaElementSource(audioEl);
        var pan = audioCtx.createStereoPanner ? audioCtx.createStereoPanner() : null;
        if (pan) { src.connect(pan); pan.connect(audioCtx.destination); }
        else src.connect(audioCtx.destination);
        audioEl.__routed = true;
        bgmPanner = pan;
        if (!bgmPanTimer) {
          bgmPanTimer = setInterval(function () {          /* 每 120ms 挪一点声像 */
            if (!bgmPanner || !bgmPanner.pan) return;
            if (!audioEl || audioEl.paused) return;
            bgmPanner.pan.value = 0.45 * Math.sin(performance.now() / 2600);
          }, 120);
        }
      } catch (e) { /* 接不上就保持普通居中播放 */ }
    };
    if (ctxRunning()) go();
    else if (audioCtx.resume) {
      try { audioCtx.resume().then(go).catch(function () {}); } catch (e) {}
    }
  }

  /* 不管有没有音乐，直接放（失败结算动画自己控制静音与否） */
  function playBakaRaw(vol, noOverlap) {
    if (!bakaPool.length) return;
    if (noOverlap && bakaBusy()) return;

    var idx = bakaIdx % bakaPool.length;          /* 轮流用下一个播放器 → 叠音 */
    var a = bakaPool[idx];
    bakaIdx++;

    if (SURROUND_ON) {                            /* 环绕关掉时完全不碰 Web Audio */
      ensureAudioCtx();
      if (ctxRunning()) {
        panPhase += 1.15;
        var pan = hookVoice(idx);
        if (pan && pan.pan) { try { pan.pan.value = Math.sin(panPhase); } catch (e) {} }
      }
    }

    a.volume = (typeof vol === 'number') ? vol : 0.9;
    try { a.currentTime = 0; } catch (e) {}
    var p = a.play();
    if (p && p.catch) p.catch(function () {});
  }
  function playBaka(vol, mode) {
    /* 没在放背景音乐时不出声（防止突然的 baka 吓到人）；
       但如果音乐因为格式问题根本放不出来，就放行，免得整局静音。 */
    if (!bakaAllowed()) return;
    playBakaRaw(vol, mode === 'mix');             /* 掺入：有声音在响就不掺 */
  }

  /* 背景音乐在放的时候随机掺 baka —— 想更密/更稀就改这四个数 */
  var BAKA_GAP_MIN = 2500;      /* 最短间隔（毫秒） */
  var BAKA_GAP_MAX = 7000;      /* 最长间隔（毫秒） */
  var BAKA_CHANCE  = 0.8;       /* 每次到点时掺进去的概率 */
  var BAKA_VOLUME  = 0.55;      /* 掺进去时的音量（0~1） */

  function startBakaMixer() {
    (function loop() {
      var bgmPlaying = !!(audioEl && !audioEl.paused && musicIndex >= 0);
      if (bgmPlaying && Math.random() < BAKA_CHANCE) playBaka(BAKA_VOLUME, 'mix');
      setTimeout(loop, BAKA_GAP_MIN + Math.random() * (BAKA_GAP_MAX - BAKA_GAP_MIN));
    })();
  }

  /* ============== 彩蛋：小火把 / 小青蛙 / 冰青蛙 ==============
     虚化、不起眼、鼠标指上去连光标都不变（不暴露）。
     答题中途随机出现在屏幕外周一圈，出现后不再移动，一道题只会出现一种。
     点中：小火把 / 小青蛙 → 直接胜利；冰青蛙 → 被发现了（暂停计时，点确定继续答题）。 */
  var PAL_TORCH = { y:'#fff3b0', Y:'#ffb437', O:'#ff6a24', B:'#8b5a2b' };
  var PAL_FROG  = { G:'#67c04a', E:'#ffffff', p:'#1d3b22', M:'#2b6b2a' };
  var PAL_ICEFROG = { G:'#a8e6ff', E:'#ffffff', p:'#1b3b6f', M:'#4f9fd0' };

  var SPR_TORCH = [
    '................',
    '.......y........',
    '......yYY.......',
    '.....yYYOY......',
    '.....YYOOY......',
    '....YYOOOOY.....',
    '....YYOOOY......',
    '.....YYOY.......',
    '......yy........',
    '....BBBBBBB.....',
    '....BBBBBBB.....',
    '.....BBBBB......',
    '.....BBBBB......',
    '.....BBBBB......',
    '......BBB.......',
    '................'
  ];
  var SPR_FROG = [
    '................',
    '................',
    '...EE......EE...',
    '..EppE....EppE..',
    '..EppE....EppE..',
    '..GGGGGGGGGGGG..',
    '.GGGGGGGGGGGGGG.',
    '.GGGGGGGGGGGGGG.',
    '.GGGGGMMMMGGGGG.',
    '.GGGGGGGGGGGGGG.',
    '.GGGGGGGGGGGGGG.',
    '..GGGGGGGGGGGG..',
    '...GG......GG...',
    '..GGG......GGG..',
    '................',
    '................'
  ];

  var EGGS = [
    { key: 'torch',   spr: SPR_TORCH, pal: PAL_TORCH,   win: true,
      title: '你发现了克制琪露诺的小火把了，你赢了', sub: '冰之妖精怕火与热' },
    { key: 'frog',    spr: SPR_FROG,  pal: PAL_FROG,    win: true,
      title: '琪露诺被你丢出的小青蛙吸引了，你赢了', sub: '冰之妖精真的很喜欢青蛙呢……' },
    { key: 'icefrog', spr: SPR_FROG,  pal: PAL_ICEFROG, win: false,
      title: '琪露诺发现青蛙冻住了，你被发现了！', sub: '点「确定」回去继续答题' }
  ];

  var eggTimer = 0, eggEl = null;

  function clearEgg() {
    if (eggTimer) { clearTimeout(eggTimer); eggTimer = 0; }
    if (eggEl && eggEl.parentNode) eggEl.parentNode.removeChild(eggEl);
    eggEl = null;
  }
  function scheduleEgg() {
    clearEgg();
    if (!G || !G.state) return;
    /* 出现时机：时间过半 ~ 四分之三之间。
       前三关用可见的限时；第 4~7 关没有可见限时，就用 hidden（原来那套 50/60/70/65 秒）。 */
    var base = G.level.time > 0 ? G.level.time : (G.level.hidden || 40);
    var lo = base * 0.5, hi = base * 0.75;
    var delay = (lo + Math.random() * (hi - lo)) * 1000;
    eggTimer = setTimeout(spawnEgg, delay);
  }
  /* 当前界面上"会挡东西"的方块（HUD、题目区、答题区、调色板、提示条…） */
  function eggBlockers() {
    var out = [];
    var scr = document.querySelector('.screen.active');
    if (!scr) return out;
    var kids = scr.children;
    for (var i = 0; i < kids.length; i++) {
      var r = kids[i].getBoundingClientRect();
      if (r.width > 1 && r.height > 1) out.push(r);
    }
    return out;
  }
  function eggBlocked(x, y, w, h, list) {
    var pad = 8;
    for (var i = 0; i < list.length; i++) {
      var r = list[i];
      if (x < r.right + pad && x + w > r.left - pad &&
          y < r.bottom + pad && y + h > r.top - pad) return true;
    }
    return false;
  }
  function spawnEgg() {
    eggTimer = 0;
    if (!G || !G.state) return;
    var e = EGGS[Math.floor(Math.random() * EGGS.length)];
    var box = el('div', 'egg');
    box.title = '';                                  /* 不要 tooltip 暴露自己 */
    box.appendChild(pixelSprite(e.spr, 4, e.pal));

    /* 外周一圈找位置：贴着边随机挑点，但**不能和题目/面板重叠**，
       否则彩蛋会被挡在面板后面根本看不见。试 60 次，实在找不到才退让。 */
    var SIZE = 64;                                   /* 16 × 4px */
    var W = window.innerWidth, H = window.innerHeight;
    var blocked = eggBlockers();
    var spot = null;
    for (var t = 0; t < 60 && !spot; t++) {
      var side = Math.floor(Math.random() * 4);
      var off = 4 + Math.random() * 30;              /* 离边 4~34px */
      var along = (Math.random() < 0.5)
        ? (2 + Math.random() * 16)                   /* 2%~18% */
        : (82 + Math.random() * 16);                 /* 82%~98% */
      var px, py;
      if (side === 0) { px = along / 100 * W; py = off; }
      else if (side === 1) { px = W - off - SIZE; py = (8 + Math.random() * 78) / 100 * H; }
      else if (side === 2) { px = along / 100 * W; py = H - off - SIZE; }
      else { px = off; py = (8 + Math.random() * 78) / 100 * H; }
      px = Math.max(2, Math.min(W - SIZE - 2, px));
      py = Math.max(2, Math.min(H - SIZE - 2, py));
      if (!eggBlocked(px, py, SIZE, SIZE, blocked)) spot = { x: px, y: py };
    }
    if (spot) {
      box.style.left = Math.round(spot.x) + 'px';
      box.style.top = Math.round(spot.y) + 'px';
    } else {
      /* 屏幕太小、到处都被面板占满：至少让它浮在面板之上，保证看得见、点得到 */
      box.classList.add('front');
      box.style.left = (4 + Math.random() * 30) + 'px';
      box.style.bottom = (4 + Math.random() * 30) + 'px';
    }
    box.onclick = function () { onEggClick(e); };
    document.body.appendChild(box);
    eggEl = box;
  }
  function onEggClick(e) {
    clearEgg();
    if (e.win) {
      pauseTimer();                                  /* 弹窗期间也把计时停住，免得超时把胜利顶掉 */
      showModal({
        sprite: e.spr, pal: e.pal, px: 6,
        title: e.title, text: e.sub, okText: '确定',
        onOk: function () { hideModal(); eggWin(); }
      });
    } else {
      pauseTimer();                                  /* 被发现了：暂停计时 */
      var sub = (G && G.level.time > 0)
        ? '计时已经暂停 —— 点「确定」回去继续答题'
        : '点「确定」回去继续答题';
      showModal({
        sprite: e.spr, pal: e.pal, px: 6,
        title: e.title, text: sub, okText: '确定',
        onOk: function () { hideModal(); resumeTimer(); }
      });
    }
  }
  function eggWin() {
    clearEgg();
    if (!G) return;                                  /* 保险：已经退回选关界面了 */
    stopTimer();
    G.eggWin = true;                                 /* 记一下：这次是彩蛋赢的，结算页要换文案 */
    startWinMedia();                                 /* 一样放 baka 视频 */
    showResult(true);
  }

  function bindDecos() {
    var nodes = document.querySelectorAll('.deco-click');
    for (var i = 0; i < nodes.length; i++) {
      nodes[i].addEventListener('click', function () {
        var me = this;
        me.classList.remove('pop');
        void me.offsetWidth;                   /* 强制重排，让效果能重复触发 */
        me.classList.add('pop');
        setTimeout(function () { me.classList.remove('pop'); }, 320);
        playBaka(0.9, 'click');                /* 点一下 = 一声 baka（可叠成鬼畜） */
      });
    }
  }

  /* ==================== 装饰弹球（慢速移动 + 碰墙反弹） ====================
     两只都走这一套代码：
       'tl' = 原来那只（左上角出发，固定方向）
       'bl' = 主页那只（同一张 gif，左下角出发，初始方向每次刷新都随机；
              碰墙时速度还会轻微抖动一下，所以两次轨迹不会一模一样） */
  function clampV(v) {
    var a = Math.abs(v);
    if (a < 26) a = 26;
    if (a > 95) a = 95;
    return v < 0 ? -a : a;
  }
  function runBouncer(id, corner) {
    var b = $(id);
    if (!b) return;
    var w = 220, h = 90, x = 0, y = 0, vx = 62, vy = 46, started = false;
    var last = performance.now();

    function place() {
      var H = window.innerHeight;
      if (corner === 'bl') {                    /* 左下角出发，方向随机 */
        x = 16 + Math.random() * 70;
        y = Math.max(0, H - h - 16 - Math.random() * 70);
        vx = 40 + Math.random() * 35;           /* 向右 */
        vy = -(35 + Math.random() * 42);        /* 向上 */
      } else {
        x = 60; y = 160; vx = 62; vy = 46;
      }
      started = true;
    }
    function fit() {
      w = b.offsetWidth || 220;
      h = b.offsetHeight || 90;
      if (!started) place();
      else y = Math.min(y, Math.max(0, window.innerHeight - h));
    }
    fit();
    if (!b.complete) b.addEventListener('load', fit);
    window.addEventListener('resize', fit);

    function bounce(axis) {                     /* 反弹 + 一点随机抖动 */
      var j = 0.92 + Math.random() * 0.16;
      if (axis === 'x') { vx = clampV(-vx * j); vy = clampV(vy * (0.96 + Math.random() * 0.08)); }
      else { vy = clampV(-vy * j); vx = clampV(vx * (0.96 + Math.random() * 0.08)); }
    }
    function step(now) {
      var dt = Math.min(0.06, (now - last) / 1000);
      last = now;
      var W = window.innerWidth, H = window.innerHeight;
      x += vx * dt; y += vy * dt;
      if (x <= 0) { x = 0; bounce('x'); }
      if (y <= 0) { y = 0; bounce('y'); }
      if (x + w >= W) { x = W - w; bounce('x'); }
      if (y + h >= H) { y = H - h; bounce('y'); }
      b.style.transform = 'translate(' + x.toFixed(1) + 'px,' + y.toFixed(1) + 'px)';
      requestAnimationFrame(step);
    }
    requestAnimationFrame(step);
  }
  function startBouncers() {
    runBouncer('bouncer', 'tl');
    runBouncer('bouncer2', 'bl');
  }

  /* ================================ 启动 ================================ */
  function init() {
    buildMusicMenu();
    buildLevelSelect();
    initBakaPool();
    bindDecos();
    startBouncers();
    startBakaMixer();
    paintMusicUI();
    bind();
    showScreen('screenTitle');
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();

})();
