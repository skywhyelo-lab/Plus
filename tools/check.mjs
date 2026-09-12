import assert from 'node:assert/strict';
import { readFile, access, readdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import './build.mjs';
const root = fileURLToPath(new URL('../', import.meta.url));
for (const file of ['index.html', 'privacy.html', 'eula.html', 'refund.html']) {
  const html = await readFile(path.join(root, file), 'utf8');
  const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);
  assert.equal(new Set(ids).size, ids.length, `Duplicate IDs in ${file}`);
  for (const [,url] of html.matchAll(/(?:href|src)="([^"]+)"/g)) {
    if (/^(https?:|data:|mailto:)/.test(url)) continue;
    if (url.startsWith('#')) { assert(ids.includes(url.slice(1)), `${file}: missing ${url}`); continue; }
    await access(path.join(root,url.split(/[?#]/)[0]));
  }
  assert(!/\/api\/|cryptocloud|paddle|trybit/i.test(html), `Legacy checkout in ${file}`);
}
const html = await readFile(path.join(root,'index.html'),'utf8');
assert(!/<script>/.test(html), 'Executable inline script remains');
assert(!/<style>/.test(html), 'Inline stylesheet remains');
for (const file of await readdir(path.join(root,'assets/js'))) {
  const result=spawnSync(process.execPath,['--check',path.join(root,'assets/js',file)],{encoding:'utf8'});
  assert.equal(result.status,0,result.stderr);
}
for (const file of ['eula.html','refund.html']) assert((await readFile(path.join(root,file),'utf8')).includes('14 дней'));
const image=await readFile(path.join(root,'assets/images/pulse-preview-20260912.png'));
assert.equal(image.readUInt32BE(16),1200);
assert.equal(image.readUInt32BE(20),630);
console.log('PASS: local links, anchors, duplicate IDs, JS syntax, no checkout, refund consistency, OG dimensions');
