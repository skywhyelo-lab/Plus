const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    const isMobileDevice = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent) || window.innerWidth < 640;
    // "Ведро": слабый CPU (<=4 логических ядер) или мало памяти (<=4ГБ, если браузер это раскрывает).
    // На таких устройствах отключаем самые дорогие эффекты (Lenis, созвездия частиц, тяжёлый blur) вместо
    // урезания их интенсивности — половинчатые меры всё равно лагали на реальном слабом железе.
    const isLowPower = (navigator.hardwareConcurrency && navigator.hardwareConcurrency <= 4) ||
                        (navigator.deviceMemory && navigator.deviceMemory <= 4);

    // Add is-mobile class to body dynamically if mobile
    if (isMobileDevice) {
      document.body.classList.add('is-mobile');
    }
    if (isLowPower) {
      document.documentElement.classList.add('low-power');
    }

    // ---- Inertial ("ice-smooth") scroll via Lenis ----
    // Skipped on reduced-motion (respects the OS setting) and on touch/mobile
    // (native momentum scroll already feels right there, and Lenis mainly
    // targets wheel input). Falls back silently if the CDN script failed.
    let lenis = null;
    if (!reducedMotion && !isMobileDevice && !isLowPower && window.Lenis) {
      lenis = new Lenis({
        duration: 0.45,        // was 0.7 — user reported scroll felt sluggish/icy; shorter glide reacts faster to wheel input
        easing: (t) => 1 - Math.pow(1 - t, 2.2), // slightly sharper ease-out — settles quicker, still not abrupt
        smoothWheel: true,
        wheelMultiplier: 1.1,  // wheel input carries a bit more weight, so it doesn't feel like it's fighting the mouse
      });
      function raf(time) {
        lenis.raf(time);
        requestAnimationFrame(raf);
      }
      requestAnimationFrame(raf);
    }

    /* ---------- BRAND ACCENT (blue, mirrors the app chrome) — read by the hero canvas ---------- */
    window.pulseAccentRGB = '34, 211, 238';

    /* ---------- HERO: entrance choreography + 3D mouse-tilt + magnetic CTAs ---------- */
    (function() {
      // Trigger the staggered entrance once the DOM is ready
      const hero = document.querySelector('.hero');
      requestAnimationFrame(() => document.body.classList.add('hero-loaded'));

      const canHover = window.matchMedia('(hover: hover)').matches;
      const stage = document.querySelector('.hero-stage');
      const win = document.getElementById('heroWindow');
      const floats = document.querySelectorAll('[data-tilt-float]');

      // 3D tilt of the app window following the pointer (desktop only, respects reduced-motion)
      if (hero && stage && win && canHover && !reducedMotion) {
        let tx = 0, ty = 0, raf = null;
        const apply = () => {
          win.style.transform = `rotateY(${tx}deg) rotateX(${ty}deg)`;
          floats.forEach((f, i) => {
            const depth = (i + 1) * 6;
            f.style.transform = `translate3d(${tx * depth * 0.4}px, ${ty * -depth * 0.4}px, 0)`;
          });
          raf = null;
        };
        hero.addEventListener('pointermove', (e) => {
          const r = hero.getBoundingClientRect();
          const px = (e.clientX - r.left) / r.width - 0.5;
          const py = (e.clientY - r.top) / r.height - 0.5;
          tx = px * 10;
          ty = -py * 7;
          if (!raf) raf = requestAnimationFrame(apply);
        }, { passive: true });
        hero.addEventListener('pointerleave', () => {
          tx = 0; ty = 0;
          if (!raf) raf = requestAnimationFrame(apply);
        });
      }

      // Magnetic pull on primary CTAs — throttled via rAF (was firing a forced
      // layout read + style write on every raw pointermove, causing visible jank).
      if (canHover && !reducedMotion && !isLowPower) {
        document.querySelectorAll('.magnetic').forEach(btn => {
          let mx = 0, my = 0, raf = null;
          const apply = () => {
            btn.style.transform = `translate(${mx}px, ${my}px)`;
            raf = null;
          };
          btn.addEventListener('pointermove', (e) => {
            const r = btn.getBoundingClientRect();
            mx = (e.clientX - r.left - r.width / 2) * 0.25;
            my = (e.clientY - r.top - r.height / 2) * 0.35;
            if (!raf) raf = requestAnimationFrame(apply);
          }, { passive: true });
          btn.addEventListener('pointerleave', () => {
            mx = 0; my = 0;
            if (!raf) raf = requestAnimationFrame(apply);
          });
        });
      }

      // Profile switcher — retints the hero (glow, canvas, app-window) like the real app
      const profileRGB = { standard: '34, 224, 107', gaming: '34, 211, 238', maximum: '255, 106, 26' };
      const profileIdx = { standard: 0, gaming: 1, maximum: 2 };
      const pills = document.querySelectorAll('.profile-pill');
      const pillsTrack = document.querySelector('.profile-pills');
      const psThumb = document.querySelector('.ps-thumb');
      const awProfileIcons = document.querySelectorAll('.aw-profiles .awp');

      function moveThumb(pill) {
        if (!psThumb || !pillsTrack || !pill) return;
        const trackRect = pillsTrack.getBoundingClientRect();
        const pillRect = pill.getBoundingClientRect();
        psThumb.style.left = (pillRect.left - trackRect.left) + 'px';
        psThumb.style.width = pillRect.width + 'px';
      }

      function selectProfile(p, { animateThumb = true } = {}) {
        document.documentElement.setAttribute('data-profile', p);
        window.pulseAccentRGB = profileRGB[p] || profileRGB.standard;
        pills.forEach(x => { x.classList.remove('active'); x.removeAttribute('aria-selected'); });
        const targetPill = document.querySelector(`.profile-pill[data-profile="${p}"]`);
        if (targetPill) {
          targetPill.classList.add('active');
          targetPill.setAttribute('aria-selected', 'true');
          if (!animateThumb) psThumb && (psThumb.style.transition = 'none');
          moveThumb(targetPill);
          if (!animateThumb) requestAnimationFrame(() => { psThumb && (psThumb.style.transition = ''); });
        }
        // Mirror the selection on the app-window's top toggle — always re-synced, never left stale
        awProfileIcons.forEach((ic, i) => ic.classList.toggle('active', i === profileIdx[p]));
        // Broadcast so any other mockup on the page (e.g. the #demo panel below) stays in sync too
        window.dispatchEvent(new CustomEvent('pulse-profile-change', { detail: { profile: p, idx: profileIdx[p] } }));
      }
      // Exposed so other mockups can drive the hero's own switcher (two-way sync)
      window.pulseSelectProfile = selectProfile;

      pills.forEach(pill => {
        pill.addEventListener('click', () => selectProfile(pill.dataset.profile));
      });

      // Initial sync: pill, thumb and the app-window icon must always agree on load
      const initialPill = document.querySelector('.profile-pill.active') || pills[0];
      if (initialPill) selectProfile(initialPill.dataset.profile, { animateThumb: false });
      window.addEventListener('resize', () => {
        const active = document.querySelector('.profile-pill.active');
        if (active) moveThumb(active);
      });
    })();

    /* ---------- HERO CANVAS BACKGROUND (SUBTLE ECG WAVE + PARTICLES) ---------- */
    (function() {
      const canvas = document.getElementById('heroCanvas');
      if (!canvas) return;
      const ctx = canvas.getContext('2d');
      const dpr = Math.min(window.devicePixelRatio || 1, isLowPower ? 1 : 1.5);
      let width, height;

      // 1. Declare and initialize data arrays first (avoids TDZ reference errors on initial resize)
      const particles = [];
      const particleCount = isLowPower ? 10 : (isMobileDevice ? 14 : 38);
      // The O(n²) constellation-line pass (~700 pair checks at 38 particles) is the
      // single costliest part of this loop — skip it entirely on weak hardware.
      let drawConstellation = !isLowPower;
      // Adaptive downgrade: some visitors pass the hardwareConcurrency/deviceMemory
      // check but their PC is loaded with other stuff (background apps, weak GPU,
      // many tabs) and still drops frames. Measure real frame time and cut the
      // expensive parts if the page is actually struggling, not just guessing upfront.
      let perfLastT = 0, perfBadFrames = 0, perfDowngraded = false;
      function perfWatch(now) {
        if (perfLastT) {
          const dt = now - perfLastT;
          perfBadFrames = dt > 33 ? perfBadFrames + 1 : Math.max(0, perfBadFrames - 2);
          if (!perfDowngraded && perfBadFrames > 45) {
            perfDowngraded = true;
            drawConstellation = false;
            if (particles.length > 12) particles.length = 12;
          }
        }
        perfLastT = now;
      }
      for (let i = 0; i < particleCount; i++) {
        particles.push({
          x: Math.random(),
          y: Math.random(),
          r: Math.random() * 1.6 + 0.5,
          speedY: Math.random() * 0.0003 + 0.0001,
          alpha: Math.random() * 0.32 + 0.06
        });
      }

      // Track mouse position
      let mouse = { x: -1000, y: -1000, active: false };
      if (!isMobileDevice) {
        window.addEventListener('mousemove', (e) => {
          const rect = canvas.getBoundingClientRect();
          mouse.x = (e.clientX - rect.left) * dpr;
          mouse.y = (e.clientY - rect.top) * dpr;
          mouse.active = true;
        });
        window.addEventListener('mouseleave', () => {
          mouse.active = false;
        });
      }

      // ECG waveform — a real PQRST heartbeat (P wave · QRS complex · T wave).
      // Negative = upward on canvas. One normalized beat, scaled per-beat below.
      let wave = null;   // ring buffer of sample heights, one slot per screen column
      let head = 0;      // write-head column index — sweeps left→right like a monitor pen
      const PQRST = [0, 0, -3, -5, -6, -4, -1, 0, 0, 5, -34, 14, -3, 0, 0, -5, -9, -11, -9, -5, -1, 0, 0, 0];
      let beatQueue = [];
      let restBeats = 12;

      function getNextWaveHeight() {
        if (beatQueue.length > 0) return beatQueue.shift();
        if (restBeats > 0) {
          restBeats--;
          return (Math.random() - 0.5) * 1.1; // isoelectric baseline shimmer between beats
        }
        // Queue the next beat with a randomized height and R-R interval → every beat differs
        let amp = 0.55 + Math.random() * 0.85;           // 0.55–1.40× → clearly varying beat size
        if (Math.random() < 0.12) amp += 0.35;           // occasional taller beat
        beatQueue = PQRST.map(v => v * amp);
        restBeats = 70 + Math.floor(Math.random() * 90); // long calm gap between beats
        return beatQueue.shift();
      }

      // 2. Define sizing functions
      function resize() {
        const rect = canvas.getBoundingClientRect();
        width = canvas.width = (rect.width || canvas.offsetWidth) * dpr;
        height = canvas.height = (rect.height || canvas.offsetHeight) * dpr;
        if (reducedMotion && width > 0 && height > 0) {
          draw();
        }
      }

      // 3. Define rendering loop
      function draw() {
        if (!width || !height) {
          resize();
          if (!width || !height) {
            if (!reducedMotion) requestAnimationFrame(draw);
            return;
          }
        }
        perfWatch(performance.now());
        ctx.clearRect(0, 0, width, height);

        // (cursor glow trail removed — it was drawn on the canvas and clipped at the hero edge)

        // Draw particles
        for (const p of particles) {
          p.y -= p.speedY;
          if (p.y < -0.01) {
            p.y = 1.01;
            p.x = Math.random();
          }

          let px = p.x * width;
          let py = p.y * height;

          // Gentle mouse repulsion force
          if (mouse.active && !reducedMotion) {
            const dx = px - mouse.x;
            const dy = py - mouse.y;
            const dist = Math.hypot(dx, dy);
            if (dist > 0 && dist < 90 * dpr) {
              const force = (90 * dpr - dist) / (90 * dpr);
              p.x += (dx / dist) * force * 0.006;
              p.x = Math.max(0.001, Math.min(0.999, p.x));
            }
          }

          ctx.globalAlpha = p.alpha;
          ctx.fillStyle = 'rgb(' + (window.pulseAccentRGB || '34, 211, 238') + ')';
          ctx.beginPath();
          ctx.arc(p.x * width, p.y * height, p.r * dpr, 0, Math.PI * 2);
          ctx.fill();
        }

        // Draw dynamic constellation connection lines between close particles
        if (!reducedMotion && drawConstellation) {
          ctx.strokeStyle = 'rgba(' + (window.pulseAccentRGB || '34, 211, 238') + ', 0.18)';
          ctx.lineWidth = 0.75 * dpr;
          const maxD = 105 * dpr;
          const maxD2 = maxD * maxD;
          for (let i = 0; i < particles.length; i++) {
            const pix = particles[i].x * width, piy = particles[i].y * height;
            for (let j = i + 1; j < particles.length; j++) {
              const dx = pix - particles[j].x * width;
              if (dx > maxD || dx < -maxD) continue;      // cheap reject before the sqrt path
              const dy = piy - particles[j].y * height;
              const dist2 = dx * dx + dy * dy;
              if (dist2 < maxD2) {
                ctx.globalAlpha = (1 - Math.sqrt(dist2) / maxD) * 0.22;
                ctx.beginPath();
                ctx.moveTo(pix, piy);
                ctx.lineTo(particles[j].x * width, particles[j].y * height);
                ctx.stroke();
              }
            }
          }
        }

        // ECG monitor sweep — a pen head writes left→right, erasing a gap ahead of itself
        ctx.globalAlpha = 1;
        const step = 3 * dpr;
        const cols = Math.max(2, Math.ceil(width / step) + 1);

        if (!wave || wave.length !== cols) {
          wave = new Array(cols).fill(null);
          head = 0;
        }

        if (!reducedMotion) {
          // write one new sample at the head, then blank a small window just ahead of it
          wave[head] = getNextWaveHeight();
          const gap = Math.max(2, Math.round(16 * dpr / step));
          for (let g = 1; g <= gap; g++) wave[(head + g) % cols] = null;
          head = (head + 1) % cols;
        }

        const accent = window.pulseAccentRGB || '34, 211, 238';
        const baseline = height * 0.56;
        const ampY = 2.7 * dpr;
        ctx.lineJoin = 'round';
        ctx.lineCap = 'round';
        ctx.strokeStyle = 'rgba(' + accent + ', 0.15)';
        ctx.lineWidth = 1.4 * dpr;
        ctx.beginPath();
        let penDown = false;
        for (let i = 0; i < cols; i++) {
          const v = wave[i];
          if (v === null || v === undefined) { penDown = false; continue; }
          const x = i * step;
          const y = baseline + v * ampY;
          if (!penDown) { ctx.moveTo(x, y); penDown = true; }
          else ctx.lineTo(x, y);
        }
        ctx.stroke();

        // glowing pen head at the last written column
        const hi = (head - 1 + cols) % cols;
        const hv = wave[hi];
        if (hv !== null && hv !== undefined) {
          const hx = hi * step;
          const hy = baseline + hv * ampY;
          ctx.shadowBlur = 8 * dpr;
          ctx.fillStyle = 'rgba(' + accent + ', 0.55)';
          ctx.beginPath();
          ctx.arc(hx, hy, 1.8 * dpr, 0, Math.PI * 2);
          ctx.fill();
        }
        ctx.shadowBlur = 0;

        if (!reducedMotion && running) requestAnimationFrame(draw);
      }

      // 4. Initialize size, bind events, and start rendering loop only after full scope setup
      // Pause the loop when the hero is off-screen or the tab is hidden — no wasted frames.
      let running = false;
      let heroVisible = true;
      function setRunning(on) {
        if (on && !running && !reducedMotion) { running = true; requestAnimationFrame(draw); }
        else if (!on) { running = false; }
      }
      function updateRunning() {
        setRunning(heroVisible && !document.hidden);
      }
      const heroEl = document.querySelector('.hero');
      if (heroEl && 'IntersectionObserver' in window) {
        new IntersectionObserver((entries) => {
          heroVisible = entries[0].isIntersecting;
          updateRunning();
        }, { threshold: 0 }).observe(heroEl);
      }
      document.addEventListener('visibilitychange', updateRunning);

      resize();
      window.addEventListener('resize', resize);
      window.addEventListener('load', resize);
      updateRunning();
    })();

    /* ---------- RESULTS PROGRESS BARS ANIMATION ---------- */
    const resultsObserver = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          document.querySelectorAll('.chart-bar-fill').forEach(fill => {
            fill.style.width = fill.dataset.w;
          });
          resultsObserver.disconnect();
        }
      });
    }, { threshold: 0.2 });

    const barsBlock = document.getElementById('barsBlock');
    if (barsBlock) resultsObserver.observe(barsBlock);



    /* (cursor-spotlight JS removed — effect deleted; native cursor only) */

    /* ---------- REVEAL ON SCROLL ---------- */
    const revealObserver = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          entry.target.classList.add('reveal-active');
          revealObserver.unobserve(entry.target);
        }
      });
    }, { threshold: 0.08 });

    document.querySelectorAll('.reveal').forEach(el => {
      revealObserver.observe(el);
    });

    /* ---------- DYNAMIC NUMBER COUNTERS ON SCROLL ---------- */
    const countersObserver = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          const counter = entry.target;
          const target = parseFloat(counter.dataset.target);
          const decimals = parseInt(counter.dataset.decimals || '0');
          const prefix = counter.dataset.prefix || ''; // e.g. "−" / "+" for hero metrics
          const duration = reducedMotion ? 0 : 1200; // ms
          const startTime = performance.now();

          function update(currentTime) {
            const progress = (duration === 0 ? 1 : Math.min((currentTime - startTime) / duration, 1));
            // Ease out quad
            const easeProgress = progress * (2 - progress);
            const value = easeProgress * target;

            counter.textContent = prefix + value.toFixed(decimals);

            if (progress < 1) {
              requestAnimationFrame(update);
            } else {
              counter.textContent = prefix + target.toFixed(decimals);
            }
          }
          requestAnimationFrame(update);
          countersObserver.unobserve(counter);
        }
      });
    }, { threshold: 0.1 });

    document.querySelectorAll('.band-card .count').forEach(counter => {
      countersObserver.observe(counter);
    });
    // Hero metrics + gauge value count-up (above the fold → fires on mount).
    // Skip under reduced-motion so the markup's final values stay put.
    if (!reducedMotion) {
      document.querySelectorAll('.hm-n').forEach(counter => countersObserver.observe(counter));
    }

    // CPU assembly: play once it scrolls into view on ALL devices (hover just
    // replays it on desktop). Toggling on isIntersecting lets it re-assemble
    // each time it re-enters the viewport.
    const assemblyEl = document.querySelector('.assembly');
    if (assemblyEl) {
      const assemblyObserver = new IntersectionObserver((entries) => {
        entries.forEach(entry => {
          assemblyEl.classList.toggle('assembly-active', entry.isIntersecting);
        });
      }, { threshold: 0.4 });
      assemblyObserver.observe(assemblyEl);
    }
