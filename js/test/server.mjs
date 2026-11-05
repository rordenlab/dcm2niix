// Static server for browser tests: serves the dcm2niix repo root (dist + dcm_qa fixtures) and lists fixture folders.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
// Frame-decoder fixtures live in sibling dcm_validate (validation data stays out of this repo); served at /frames/.
export const FRAMES = path.resolve(process.env.DCM2NIIX_FRAMES || path.join(ROOT, '..', 'dcm_validate', 'wsi_frames', 'fixtures'));
if (!fs.existsSync(path.join(FRAMES, 'expected.json'))) throw new Error(`frame fixtures not found in ${FRAMES}: clone dcm_validate beside dcm2niix or set DCM2NIIX_FRAMES`);
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json' };

const walk = (dir, rel = '') =>
  fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) =>
    e.name.startsWith('.') ? [] : e.isDirectory() ? walk(path.join(dir, e.name), `${rel}${e.name}/`) : [`${rel}${e.name}`]
  );

export function startServer(port = 0) {
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost');
    const p = decodeURIComponent(url.pathname);
    const [dir, rel] = p.startsWith('/frames/') ? [FRAMES, p.slice(7)] : [ROOT, p];
    const target = path.resolve(dir, `.${rel}`);
    if (target !== dir && !target.startsWith(dir + path.sep)) return res.writeHead(403).end();
    if (url.searchParams.has('list')) return res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify(walk(target)));
    fs.readFile(target, (err, data) => {
      if (err) return res.writeHead(404).end();
      res.writeHead(200, { 'Content-Type': MIME[path.extname(target)] || 'application/octet-stream' }).end(data);
    });
  });
  return new Promise((resolve) => server.listen(port, '127.0.0.1', () => resolve(server)));
}
