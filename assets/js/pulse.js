'use strict';
(() => {
  const root = document.documentElement;
  root.classList.add('js');
  const $ = id => document.getElementById(id);
  const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const fmt = n => n.toLocaleString('ru-RU', { maximumFractionDigits: 1, minimumFractionDigits: n < 10 && n % 1 ? 1 : 0 });

  /* ---------- окна: рисуются в размере приложения и масштабируются ---------- */
  const stages = [...document.querySelectorAll('.stage')];
  const fit = () => stages.forEach(s => s.style.setProperty('--s', Math.min(1, s.clientWidth / 1164).toFixed(4)));
  if ('ResizeObserver' in window) new ResizeObserver(fit).observe(document.body); else addEventListener('resize', fit);
  fit();

  // видимость блоков: живые части работают, только пока их видно
  const seen = new Map();
  const watch = (el, cb) => {
    if (!el || !('IntersectionObserver' in window)) { if (el) cb(true); return; }
    new IntersectionObserver(es => es.forEach(e => { seen.set(el, e.isIntersecting); cb(e.isIntersecting); }), { threshold: .15 }).observe(el);
  };
  const live = el => seen.get(el) && !document.hidden;

  // число, которое плавно доезжает до цели
  function countTo(el, to, ms = 1200, suffix = '') {
    if (!el) return;
    const from = parseFloat(el.textContent) || 0;
    if (reduce) { el.textContent = to + suffix; return; }
    const t0 = performance.now();
    const step = t => {
      const k = Math.min(1, (t - t0) / ms), e = 1 - Math.pow(1 - k, 3);
      el.textContent = Math.round(from + (to - from) * e) + suffix;
      if (k < 1) setTimeout(() => step(performance.now()), 16);
    };
    step(t0);
  }

  /* ---------- режимы: Стандартный / Игровой / Максимум ---------- */
  const MODES = ['standard', 'game', 'max'];
  const NAMES = { standard: 'Стандартный', game: 'Игровой', max: 'Максимум' };
  const NOTES = {
    standard: 'Освобождает память. Программы не закрывает.',
    game: 'Закрывает всё, кроме игры, Discord, Steam и драйверов.',
    max: 'Закрывает всё, кроме самой Windows.'
  };
  const note = $('modeNote');
  const radios = [...document.querySelectorAll('.mode')];

  function setMode(mode, save) {
    if (!MODES.includes(mode)) return;
    const changed = root.dataset.mode !== mode;
    root.dataset.mode = mode;
    radios.forEach(r => { const on = r.dataset.mode === mode; r.setAttribute('aria-checked', String(on)); r.tabIndex = on ? 0 : -1; });
    if (note && note.textContent !== NOTES[mode]) {
      note.classList.add('swap');
      setTimeout(() => { note.textContent = NOTES[mode]; note.classList.remove('swap'); }, 180);
    }
    if (changed) resetClean();
    if (save) { try { localStorage.setItem('pulse-mode', mode); } catch (e) { /* без хранилища — просто не запоминаем */ } }
  }
  document.querySelectorAll('button[data-mode]').forEach(b => b.addEventListener('click', () => setMode(b.dataset.mode, true)));
  const group = document.querySelector('.modes');
  if (group) group.addEventListener('keydown', e => {
    const step = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[e.key];
    if (!step) return;
    e.preventDefault();
    const i = (MODES.indexOf(root.dataset.mode) + step + 3) % 3;
    setMode(MODES[i], true);
    radios[i].focus();
  });

  const top = $('top');
  const onScroll = () => { if (top) top.classList.toggle('scrolled', scrollY > 12); };
  addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  /* ---------- «Обзор»: график, очистка ---------- */
  const winHero = $('winHero');
  watch(winHero, () => {});
  const N = 60;
  const series = { cpu: [], ram: [], disk: [], net: [] };
  let mem = 87;
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const walk = (prev, base, spread, lo, hi) => clamp(prev + (base - prev) * .25 + (Math.random() - .5) * spread, lo, hi);
  for (let i = 0; i < N; i++) {
    series.cpu.push(28 + Math.sin(i / 4) * 8 + Math.random() * 6);
    series.ram.push(mem + (Math.random() - .5));
    series.disk.push(Math.random() < .12 ? 30 + Math.random() * 50 : 4 + Math.random() * 6);
    series.net.push(62 + Math.sin(i / 3) * 10 + Math.random() * 12);
  }
  const paths = winHero ? {
    cpu: winHero.querySelector('.ln.c-cpu'), ram: winHero.querySelector('.ln.c-ram'),
    disk: winHero.querySelector('.ln.c-disk'), net: winHero.querySelector('.ln.c-net')
  } : {};
  const toPath = arr => {
    const pts = arr.map((v, i) => [i * (600 / (N - 1)), 140 - v * 1.4]);
    let d = `M${pts[0][0].toFixed(1)} ${pts[0][1].toFixed(1)}`;
    for (let i = 1; i < pts.length; i++) {
      const [x0, y0] = pts[i - 1], [x1, y1] = pts[i], mx = (x0 + x1) / 2;
      d += ` C${mx.toFixed(1)} ${y0.toFixed(1)} ${mx.toFixed(1)} ${y1.toFixed(1)} ${x1.toFixed(1)} ${y1.toFixed(1)}`;
    }
    return d;
  };
  const draw = () => Object.keys(paths).forEach(k => paths[k] && paths[k].setAttribute('d', toPath(series[k])));
  const tickChart = () => {
    const last = k => series[k][N - 1];
    series.cpu.push(walk(last('cpu'), 30, 14, 6, 92));
    series.ram.push(walk(last('ram'), mem, 1.2, 10, 99));
    series.disk.push(Math.random() < .1 ? 25 + Math.random() * 55 : walk(last('disk'), 5, 4, 0, 100));
    series.net.push(walk(last('net'), 66, 18, 20, 98));
    Object.values(series).forEach(a => a.shift());
    draw();
    const c = Math.round(last('cpu')), set = (id, t) => { const el = $(id); if (el) el.textContent = t; };
    set('stCpu', c + '%');
    set('stRam', Math.round(last('ram')) + '%');
    set('stDisk', Math.round(last('disk')) + '%');
    set('stNet', fmt(Math.round(last('net') / 5 * 10) / 10) + ' Mbps');
  };
  draw();
  setInterval(() => { if (live(winHero)) tickChart(); }, 1000);

  const gauge = $('gauge'), memPct = $('memPct'), memChip = $('memChip');
  const cleanBtn = $('cleanBtn'), toast = $('toast');
  const procRows = [...document.querySelectorAll('#procs li')];
  const memCells = procRows.map(li => li.querySelectorAll('em')[1]);
  const FREED = { standard: 2.1, game: 4.3, max: 5.6 };
  const AFTER = { standard: 74, game: 56, max: 47 };
  let cleaning = false, cleaned = false;

  function setMem(p) {
    mem = p;
    if (gauge) gauge.style.setProperty('--p', (p / 100).toFixed(3));
    countTo(memPct, p, 1300);
    if (memChip) {
      const [txt, cls] = p >= 85 ? ['Ужасно', 'chip-crit'] : p >= 70 ? ['Плохо', 'chip-warn'] : p >= 55 ? ['Нормально', 'chip-mute'] : ['Хорошо', 'chip-good'];
      memChip.textContent = txt;
      memChip.className = `chip g-chip ${cls}`;
    }
  }
  function memText(gb) { return gb >= 1 ? `${fmt(Math.round(gb * 10) / 10)} GB` : `${Math.round(gb * 1000)} MB`; }
  function resetClean() {
    if (!cleanBtn) return;
    cleaned = false;
    procRows.forEach((li, i) => { li.classList.remove('gone'); const gb = +memCells[i].dataset.mem; memCells[i].textContent = memText(gb); });
    $('lastTitle').textContent = 'Ещё не чистили';
    $('lastSub').textContent = 'Запустите очистку, и здесь появится, сколько освободилось';
    if (mem !== 87) setMem(87);
  }
  function showToast(text) {
    if (!toast) return;
    toast.textContent = text;
    toast.classList.add('show');
    clearTimeout(showToast.t);
    showToast.t = setTimeout(() => toast.classList.remove('show'), 2600);
  }
  if (cleanBtn) cleanBtn.addEventListener('click', () => {
    if (cleaning) return;
    if (cleaned) resetClean();
    cleaning = true;
    cleanBtn.disabled = true;
    cleanBtn.textContent = 'Очистка…';
    const mode = root.dataset.mode;
    setTimeout(() => {
      procRows.forEach((li, i) => {
        if ((li.dataset.close || '').split(' ').includes(mode)) li.classList.add('gone');
        else if (mode === 'standard' || li.dataset.close === '') {
          const gb = +memCells[i].dataset.mem * (mode === 'standard' ? .62 : .85);
          memCells[i].textContent = memText(gb);
        }
      });
      setMem(AFTER[mode]);
      const gb = fmt(FREED[mode]);
      $('lastTitle').textContent = `Освобождено ${gb} ГБ`;
      $('lastSub').textContent = `Только что · режим «${NAMES[mode]}»`;
      showToast(`Готово: освобождено ${gb} ГБ`);
      cleanBtn.disabled = false;
      cleanBtn.textContent = 'Быстрая очистка';
      cleaning = false;
      cleaned = true;
    }, 900);
  });

  /* ---------- «Главное»: вкладки с автопереключением ---------- */
  const tabs = [...document.querySelectorAll('.tab')];
  const winTour = $('winTour');
  const tourStage = $('tourStage');
  const DUR = 7000;
  let tabIdx = 0, auto = true, timer = null;
  const pageHooks = {};

  const tourCount = $('tourCount');
  const PAGES = ['lab', 'opt', 'check', 'cs2', 'irq'];
  function showPage(key, byUser) {
    PAGES.forEach(k => {
      const pane = $('pane-' + k), cap = $('cap-' + k);
      if (pane) pane.hidden = k !== key;
      if (cap) cap.hidden = k !== key;
    });
    let ti = tabs.findIndex(t => t.getAttribute('aria-controls') === 'pane-' + key);
    if (ti < 0) ti = 0; // CS2 живёт внутри Pulse Lab
    tabIdx = ti;
    tabs.forEach((t, k) => { const on = k === ti; t.setAttribute('aria-selected', String(on)); t.tabIndex = on ? 0 : -1; t.classList.remove('run'); });
    if (winTour) winTour.dataset.active = key === 'opt' ? '6' : '5';
    if (tourCount) tourCount.textContent = (ti + 1) + ' / ' + tabs.length;
    const strip = tabs[ti] && tabs[ti].parentElement;
    if (strip && strip.scrollWidth > strip.clientWidth) strip.scrollTo({ left: tabs[ti].offsetLeft - 16, behavior: 'smooth' });
    if (pageHooks[key]) pageHooks[key]();
    if (byUser) { auto = false; clearTimeout(timer); } else schedule();
  }
  const openTab = (i, byUser) => showPage(tabs[i].getAttribute('aria-controls').replace('pane-', ''), byUser);
  document.querySelectorAll('[data-go]').forEach(el => el.addEventListener('click', () => showPage(el.dataset.go, true)));
  const labToast = $('labToast');
  document.querySelectorAll('[data-toast]').forEach(el => el.addEventListener('click', () => {
    if (!labToast) return;
    labToast.textContent = 'Этот экран откроется в самом Pulse. Здесь работают «Проверка ПК», «Мышь и прерывания» и CS2';
    labToast.classList.add('show');
    clearTimeout(labToast.t);
    labToast.t = setTimeout(() => labToast.classList.remove('show'), 2600);
  }));
  function schedule() {
    clearTimeout(timer);
    if (!auto || !live(tourStage)) return;
    const t = tabs[tabIdx];
    void t.offsetWidth;
    t.style.setProperty('--dur', DUR + 'ms');
    t.classList.add('run');
    timer = setTimeout(() => openTab((tabIdx + 1) % tabs.length, false), DUR);
  }
  tabs.forEach((t, i) => {
    t.addEventListener('click', () => openTab(i, true));
    t.addEventListener('keydown', e => {
      const step = { ArrowRight: 1, ArrowLeft: -1 }[e.key];
      if (!step) return;
      e.preventDefault();
      const j = (i + step + tabs.length) % tabs.length;
      openTab(j, true);
      tabs[j].focus();
    });
  });
  // стрелки листают и не выключают автопереключение
  const step = d => { openTab((tabIdx + d + tabs.length) % tabs.length, false); };
  const tp = $('tourPrev'), tn = $('tourNext');
  if (tp) tp.addEventListener('click', () => step(-1));
  if (tn) tn.addEventListener('click', () => step(1));
  // клик внутри окна — человек разбирается сам, автопереключение больше не мешает
  if (winTour) winTour.addEventListener('pointerdown', () => { auto = false; clearTimeout(timer); tabs.forEach(t => t.classList.remove('run')); });
  watch(tourStage, vis => {
    if (vis) { if (auto && !timer) schedule(); }
    else { clearTimeout(timer); timer = null; tabs.forEach(t => t.classList.remove('run')); }
  });

  // «Проверено сегодня в …» — недавнее время, а не застывшее
  const ca = $('checkedAt');
  if (ca) { const d = new Date(Date.now() - 17 * 60000); ca.textContent = `Проверено сегодня в ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`; }

  /* оптимизация */
  const optRun = $('optRun'), optTitle = $('optTitle'), optSub = $('optSub'), optUndo = $('optUndo');
  const chips = [...document.querySelectorAll('.chipx')];
  const segBtns = [...document.querySelectorAll('.seg button')];
  let lvl = 'std', running = false;
  const READY_SUB = { std: 'Pulse всё настроит автоматически', max: '+10 настроек. FACEIT перестанет работать' };
  function optReset() {
    running = false;
    optRun.style.setProperty('--prog', 0);
    optRun.disabled = false;
    optRun.querySelector('span').textContent = 'Запустить оптимизацию';
    optTitle.textContent = 'Система готова';
    optSub.textContent = READY_SUB[lvl];
    optUndo.classList.remove('ready');
    chips.forEach(c => c.classList.remove('lit'));
  }
  segBtns.forEach(b => b.addEventListener('click', () => {
    if (running) return;
    lvl = b.dataset.lvl;
    segBtns.forEach(x => x.classList.toggle('on', x === b));
    optReset();
  }));
  if (optRun) optRun.addEventListener('click', () => {
    if (running) return;
    running = true;
    optRun.disabled = true;
    const total = lvl === 'max' ? 67 : 57, t0 = performance.now(), ms = reduce ? 10 : 2600;
    chips.forEach(c => c.classList.remove('lit'));
    optUndo.classList.remove('ready');
    const step = t => {
      const k = Math.min(1, (t - t0) / ms), n = Math.round(total * k);
      optRun.style.setProperty('--prog', k.toFixed(3));
      optRun.querySelector('span').textContent = `Применено ${n} из ${total}`;
      optTitle.textContent = 'Настраиваю…';
      chips.forEach((c, i) => c.classList.toggle('lit', k >= (i + 1) / 4 - .05));
      if (k < 1) { setTimeout(() => step(performance.now()), 16); return; }
      optTitle.textContent = 'Готово';
      optSub.textContent = `${total} настроек применено. Всё записано в журнал`;
      optRun.querySelector('span').textContent = 'Запустить снова';
      optRun.disabled = false;
      optUndo.classList.add('ready');
      running = false;
    };
    step(t0);
  });
  if (optUndo) optUndo.addEventListener('click', () => { if (!running && optUndo.classList.contains('ready')) optReset(); });

  // чипы на орбите: позиция по кругу, сами всегда ровно (держат горизонт)
  const R = 168, START = -38;
  let ang = START, lastT = 0;
  const place = () => chips.forEach((c, i) => {
    const a = (ang + i * 90) * Math.PI / 180;
    c.style.transform = `translate(${(Math.sin(a) * R).toFixed(1)}px, ${(-Math.cos(a) * R).toFixed(1)}px)`;
  });
  place();
  const orbit = t => {
    const dt = lastT ? Math.min(64, t - lastT) : 0;
    lastT = t;
    if (!reduce && live(tourStage) && !$('pane-opt').hidden) { ang += dt * 360 / 60000; place(); }
    requestAnimationFrame(orbit);
  };
  requestAnimationFrame(orbit);

  /* проверка ПК */
  const score = $('score'), scoreNum = $('scoreNum');
  let fixed = false;
  pageHooks.check = () => {
    if (!score) return;
    if (!fixed) {
      score.style.setProperty('--p', 0);
      scoreNum.textContent = '0';
      setTimeout(() => { if (fixed) return; score.style.setProperty('--p', .86); countTo(scoreNum, 86, 1300); }, 60);
    }
  };
  const doFix = () => {
    if (fixed) return;
    fixed = true;
    const act = $('usbAct');
    act.classList.add('done');
    act.textContent = '✓ Исправлено';
    score.style.setProperty('--p', .91);
    countTo(scoreNum, 91, 1000);
    $('scoreWord').textContent = 'Отлично';
    $('scoreSub').textContent = 'Осталась 1 проблема: диск. Её кнопкой не исправить.';
    $('chipWarn').innerHTML = '<i></i>Можно лучше: 1';
    $('chipGood').innerHTML = '<i></i>В порядке: 21';
    const fa = $('fixAll');
    fa.disabled = true;
    fa.querySelector('span').textContent = 'Исправлено';
  };
  ['fixAll', 'usbAct'].forEach(id => { const el = $(id); if (el) el.addEventListener('click', doFix); });

  /* прерывания */
  const coresEl = $('cores'), barsEl = $('bars');
  const load = [13.4, 0, .5, .2, .5, 1, 0, 0, .5, .2, 1.3, .5];
  if (coresEl) coresEl.innerHTML = load.map((_, i) => `<span data-c="${i}"><b>${i}</b><small>Я${i >> 1}</small></span>`).join('');
  if (barsEl) barsEl.innerHTML = load.map((v, i) => `<li><span>${i}</span><u style="--v:${(v / 14).toFixed(3)}"></u><em>${fmt(v)}%</em></li>`).join('');
  let routed = false;
  const irqTick = () => {
    if (!barsEl) return;
    [...barsEl.children].forEach((li, i) => {
      let base = load[i];
      if (routed) base = i === 0 ? 3.1 : i === 4 ? 4.2 : i === 8 ? 2.6 : load[i];
      const v = Math.max(0, base + (Math.random() - .5) * Math.max(.4, base * .25));
      li.querySelector('u').style.setProperty('--v', Math.min(1, v / 14).toFixed(3));
      li.querySelector('em').textContent = fmt(Math.round(v * 10) / 10) + '%';
    });
  };
  setInterval(() => { if (live(tourStage) && !$('pane-irq').hidden) irqTick(); }, 1000);
  const irqAll = $('irqAll');
  if (irqAll) irqAll.addEventListener('click', () => {
    routed = !routed;
    coresEl.querySelectorAll('span').forEach(s => s.classList.toggle('sel', routed && s.dataset.c === '4'));
    $('gpuCore').textContent = routed ? '4' : 'Windows';
    irqAll.textContent = routed ? 'Готово' : 'Настроить всё';
    irqTick();
  });

  /* ---------- журнал «Вернуть как было» ---------- */
  const jRows = [...document.querySelectorAll('#journal li')];
  const jAll = $('jAll');
  const jSet = (li, on) => {
    li.classList.toggle('off', !on);
    const st = li.querySelector('.j-st');
    st.textContent = on ? 'Применено' : 'Как было';
    st.className = `chip j-st ${on ? 'chip-good' : 'chip-mute'}`;
    li.querySelector('.j-btn').textContent = on ? 'Вернуть' : 'Применить';
  };
  const jSync = () => {
    const n = jRows.filter(li => !li.classList.contains('off')).length;
    $('jCount').textContent = `${n} из ${jRows.length} применено`;
    if (jAll) jAll.querySelector('span').textContent = n ? 'Вернуть как было' : 'Применить снова';
  };
  jRows.forEach(li => li.querySelector('.j-btn').addEventListener('click', () => { jSet(li, li.classList.contains('off')); jSync(); }));
  if (jAll) jAll.addEventListener('click', () => {
    const anyOn = jRows.some(li => !li.classList.contains('off'));
    jRows.forEach((li, i) => setTimeout(() => { jSet(li, !anyOn); if (i === jRows.length - 1) jSync(); }, reduce ? 0 : i * 90));
  });

  /* ---------- появление блоков ---------- */
  const items = document.querySelectorAll('.reveal');
  if ('IntersectionObserver' in window) {
    const io = new IntersectionObserver(es => es.forEach(e => { if (e.isIntersecting) { e.target.classList.add('on'); io.unobserve(e.target); } }), { rootMargin: '0px 0px -8% 0px' });
    items.forEach(el => io.observe(el));
  } else items.forEach(el => el.classList.add('on'));

  /* ---------- начальный режим ---------- */
  let saved = new URLSearchParams(location.search).get('mode');
  if (!saved) { try { saved = localStorage.getItem('pulse-mode'); } catch (e) { /* нет доступа */ } }
  if (saved && saved !== 'standard') setMode(saved, false);
  const qTab = new URLSearchParams(location.search).get('tab');
  if (PAGES.includes(qTab) && qTab !== 'lab') showPage(qTab, true);

  /* ---------- версия, размер и дата из GitHub Releases ---------- */
  const set = (id, text) => { const el = $(id); if (el && text) el.textContent = text; };
  fetch('https://api.github.com/repos/skywhyelo-lab/Plus/releases/latest')
    .then(r => (r.ok ? r.json() : null))
    .then(rel => {
      if (!rel) return;
      const asset = (rel.assets || []).find(a => a.name === 'Pulse.exe');
      const version = String(rel.tag_name || '').replace(/^v/i, '');
      if (!/^[0-9][0-9A-Za-z.+-]{0,39}$/.test(version)) return;
      set('relVersion', version);
      if (!asset) return;
      const d = new Date(asset.updated_at || rel.published_at);
      set('relSize', `${Math.round(asset.size / 1048576)} МБ`);
      if (!Number.isNaN(d.getTime())) set('relDate', d.toLocaleDateString('ru-RU'));
      if (asset.digest) {
        set('relHash', String(asset.digest).replace(/^sha256:/, ''));
        const row = $('relHashRow');
        if (row) row.hidden = false;
      }
    })
    .catch(() => { /* оффлайн или лимит API — остаются значения из разметки */ });
})();
