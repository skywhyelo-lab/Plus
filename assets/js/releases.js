/* ---------- GITHUB RELEASE CHECKER (last update date for Pulse.exe) ---------- */
    async function checkPulseUpdates() {
      try {
        const res = await fetch('https://api.github.com/repos/skywhyelo-lab/Plus/releases/latest');
        if (!res.ok) return;
        const release = await res.json();
        const assets = release.assets || [];
        const asset = assets.find(a => a.name === 'Pulse.exe');

        // Lite публикуется тем же релизом отдельным файлом. Размер и контрольную сумму
        // берём из ответа API (поле digest), чтобы на сайте не осталось цифр, которые
        // надо править руками на каждой сборке.
        const liteAsset = assets.find(a => a.name === 'PulseLite.exe');
        if (liteAsset) {
          if (liteAsset.size) {
            const liteMB = (liteAsset.size / (1024 * 1024)).toFixed(1);
            ['specsSizeLite', 'specsSizeSide'].forEach(id => {
              const el = document.getElementById(id);
              if (el) el.textContent = `${liteMB} МБ`;
            });
          }
          const liteHash = document.getElementById('hashLite');
          if (liteHash && liteAsset.digest) liteHash.textContent = String(liteAsset.digest).replace(/^sha256:/, '');
        }

        // Pulse Remote (Android) — тот же приём: размер подтягивается из релиза, руками
        // ничего не правим.
        const remoteAsset = assets.find(a => a.name === 'PulseRemote.apk');
        if (remoteAsset && remoteAsset.size) {
          const remoteMB = (remoteAsset.size / (1024 * 1024)).toFixed(0);
          const remoteEl = document.getElementById('specsSizeRemote');
          if (remoteEl) remoteEl.textContent = `${remoteMB} МБ`;
        }

        if (!asset || !asset.updated_at) return;

        const fullHash = document.getElementById('hashFull');
        if (fullHash && asset.digest) fullHash.textContent = String(asset.digest).replace(/^sha256:/, '');

        const d = new Date(asset.updated_at);
        const day = String(d.getDate()).padStart(2, '0');
        const month = String(d.getMonth() + 1).padStart(2, '0');
        const year = d.getFullYear();
        const formatted = `${day}.${month}.${year}`;
        const rawVersion = String(release.tag_name || '').replace(/^v/i, '');
        const version = /^[0-9][0-9A-Za-z.+-]{0,39}$/.test(rawVersion) ? rawVersion : '';
        if (Number.isNaN(d.getTime())) return;

        const heroNote = document.getElementById('heroNote');
        if (heroNote) {
          const versionText = version ? `<span class="hn-tag">Версия ${version}</span>` : '';
          heroNote.innerHTML = `${versionText}<span class="hn-tag">Windows 10 / 11</span><span class="hn-tag">Платная лицензия</span><span class="hn-tag hn-date">Обновлено ${formatted}</span>`;
        }

        const updatedRow = document.getElementById('lastUpdateRow');
        const specsUpdated = document.getElementById('specsUpdated');
        if (updatedRow && specsUpdated) {
          specsUpdated.textContent = formatted;
          updatedRow.style.display = '';
        }

        if (asset.size) {
          const sizeMB = (asset.size / (1024 * 1024)).toFixed(1);
          ['specsSize', 'specsSizeInline'].forEach(id => {
            const el = document.getElementById(id);
            if (el) el.textContent = `${sizeMB} МБ`;
          });
          const bandCountFile = document.querySelector('.band-card .count[data-target="99.3"]');
          if (bandCountFile) {
            bandCountFile.dataset.target = sizeMB;
            if (bandCountFile.textContent !== '0') bandCountFile.textContent = sizeMB;
          }
        }
      } catch (e) {
        console.error('Ошибка при получении обновлений с GitHub:', e);
      }
    }
    window.addEventListener('DOMContentLoaded', checkPulseUpdates);
