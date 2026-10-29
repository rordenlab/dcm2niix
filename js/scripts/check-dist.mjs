// Refuse to pack a stale or incomplete dist: wrong embedded dcm2niix version, out-of-date bundle or declarations, stray files.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const js = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const at = (...p) => path.join(js, ...p);
const expected = ['dcm2niix.jpeg.js', 'dcm2niix.jpeg.wasm', 'index.d.ts', 'index.jpeg.js', 'worker.jpeg.js'];
const errors = [];

const dist = fs.existsSync(at('dist')) ? fs.readdirSync(at('dist')).filter((f) => !f.startsWith('.')).sort() : [];
for (const f of expected) if (!dist.includes(f)) errors.push(`dist/${f} missing`);
for (const f of dist) if (!expected.includes(f)) errors.push(`dist/${f} is not part of the package (stale build?)`);
for (const f of ['LICENSE', 'NOTICES']) if (!fs.existsSync(at(f))) errors.push(`${f} missing`);

const header = fs.readFileSync(at('..', 'console', 'nii_dicom.h'), 'utf8');
const version = /#define kDCMdate "(v[0-9.]+)"/.exec(header)?.[1];
if (!version) errors.push('kDCMdate not found in console/nii_dicom.h');
else if (dist.includes('dcm2niix.jpeg.wasm') && !fs.readFileSync(at('dist', 'dcm2niix.jpeg.wasm')).includes(Buffer.from(version)))
  errors.push(`dist/dcm2niix.jpeg.wasm does not embed ${version} (rebuild with pixi run wasm)`);

if (dist.includes('index.d.ts') && fs.readFileSync(at('dist', 'index.d.ts'), 'utf8') !== fs.readFileSync(at('src', 'index.d.ts'), 'utf8'))
  errors.push('dist/index.d.ts differs from src/index.d.ts');

// Bundles must be newer than every source they include.
const built = Math.min(...['index.jpeg.js', 'worker.jpeg.js'].filter((f) => dist.includes(f)).map((f) => fs.statSync(at('dist', f)).mtimeMs));
for (const f of ['core.js', 'worker-core.js', 'index.jpeg.js', 'worker.jpeg.js', 'dcm2niix.jpeg.js', 'dcm2niix.jpeg.wasm'])
  if (fs.statSync(at('src', f)).mtimeMs > built) errors.push(`src/${f} is newer than the dist bundles`);

if (errors.length) {
  console.error(`check-dist failed:\n  ${errors.join('\n  ')}`);
  process.exit(1);
}
console.log(`check-dist ok: ${version}, ${dist.length} dist files`);
