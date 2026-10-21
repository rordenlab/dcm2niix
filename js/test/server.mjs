// Static server for browser tests: serves the dcm2niix repo root (dist + dcm_qa fixtures) and lists fixture folders.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json' };

const walk = (dir, rel = '') =>
  fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) =>
    e.name.startsWith('.') ? [] : e.isDirectory() ? walk(path.join(dir, e.name), `${rel}${e.name}/`) : [`${rel}${e.name}`]
  );

export function startServer(port = 0) {
  const server = http.createServer((req, res) => {
    const url = new URL(req.url, 'http://localhost');
    const target = path.resolve(ROOT, `.${decodeURIComponent(url.pathname)}`);
    if (!target.startsWith(ROOT)) return res.writeHead(403).end();
    if (url.searchParams.has('list')) return res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify(walk(target)));
    fs.readFile(target, (err, data) => {
      if (err) return res.writeHead(404).end();
      res.writeHead(200, { 'Content-Type': MIME[path.extname(target)] || 'application/octet-stream' }).end(data);
    });
  });
  return new Promise((resolve) => server.listen(port, '127.0.0.1', () => resolve(server)));
}
