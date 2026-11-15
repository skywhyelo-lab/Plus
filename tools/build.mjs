import { readFile, writeFile, readdir, mkdir, cp } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
const root = fileURLToPath(new URL('../', import.meta.url));
const sections = path.join(root, 'src/sections');
const files = (await readdir(sections)).filter(f => f.endsWith('.html')).sort();
const html = (await Promise.all(files.map(f => readFile(path.join(sections, f), 'utf8')))).join('\n');
await writeFile(path.join(root, 'index.html'), html);
await mkdir(path.join(root, 'public'), { recursive: true });
for (const file of ['index.html', 'privacy.html', 'eula.html', 'refund.html', 'robots.txt', 'sitemap.xml', 'llms.txt', 'google2622f687cf2f8caf.html', 'assets']) {
  await cp(path.join(root, file), path.join(root, 'public', file), { recursive: true });
}
console.log(`Built ${files.length} HTML sections into index.html and public/`);
