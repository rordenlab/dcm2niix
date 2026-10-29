// Packed-package consumer test: npm pack, install the tarball into a minimal Vite app, then check
// a production build served from a subpath, the Vite dev server and TypeScript, as a downstream app would use them.
// Set KEEP_TARBALL=<dir> to keep the tested tarball.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';
import { build, createServer } from 'vite';

const js = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const fixture = path.join(js, '..', 'dcm_qa_nih', 'In', '20180918GE');
const version = /#define kDCMdate "(v[0-9.]+)"/.exec(fs.readFileSync(path.join(js, '..', 'console', 'nii_dicom.h'), 'utf8'))[1];
const tmp = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'dcm2niix-consumer-'))); // Vite rejects the /var -> /private/var symlink on macOS
const app = path.join(tmp, 'app');
const closers = [];
let failed = 0;

const check = async (name, fn) => {
  const t0 = Date.now();
  try {
    await fn();
    console.log(`ok   ${name} (${Date.now() - t0} ms)`);
  } catch (e) {
    failed++;
    console.log(`FAIL ${name}\n     ${e.message}`);
  }
};

// Converts the fixture and reports the embedded native version; /jpeg and /plain must be the same module.
const convertAt = async (browser, url) => {
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto(url);
  await page.waitForFunction(() => window.ready === true, null, { timeout: 30000 });
  await page.setInputFiles('#dir', fixture);
  const r = await page.evaluate(() => window.run());
  assert.equal(r.n, 4, 'series');
  assert.ok(r.ok, 'exit code');
  assert.ok(r.names.every((n) => n.startsWith('20180918GE_')), `names ${r.names}`);
  assert.ok(r.version.includes(version), `version ${r.version}`);
  assert.ok(r.aliases, '/jpeg and /plain resolve to the root module');
  assert.deepEqual(errors, []);
  await page.close();
};

try {
  const out = execFileSync('npm', ['pack', '--json', '--pack-destination', tmp], { cwd: js, encoding: 'utf8' });
  const tarball = path.join(tmp, JSON.parse(out.slice(out.indexOf('[')))[0].filename);
  fs.mkdirSync(app);
  for (const f of ['index.html', 'main.js', 'main.ts', 'tsconfig.json']) fs.copyFileSync(path.join(js, 'test', 'consumer', f), path.join(app, f));
  fs.writeFileSync(path.join(app, 'package.json'), JSON.stringify({ name: 'consumer', private: true, type: 'module' }));
  execFileSync('npm', ['install', '--no-save', '--no-audit', '--no-fund', '--ignore-scripts', tarball], { cwd: app, stdio: 'ignore' });
  const browser = await chromium.launch();
  closers.push(() => browser.close());

  await check('vite build: worker and WASM emitted, no Node-branch copy', async () => {
    await build({ root: app, base: '/sub/', logLevel: 'warn', build: { outDir: path.join(app, 'dist') } });
    const assets = fs.readdirSync(path.join(app, 'dist', 'assets'));
    assert.equal(assets.filter((a) => a.endsWith('.wasm')).length, 1, `assets ${assets}`);
    assert.equal(assets.filter((a) => a.startsWith('worker.jpeg')).length, 1, `assets ${assets}`);
    assert.equal(assets.filter((a) => a.startsWith('index.jpeg')).length, 0, `dead glue copy in ${assets}`);
  });

  await check('production build served from /sub/ converts; /jpeg and /plain are aliases', async () => {
    const dist = path.join(app, 'dist');
    const mime = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
    const server = http.createServer((req, res) => {
      const rel = decodeURIComponent(new URL(req.url, 'http://x').pathname).replace(/^\/sub\//, '');
      const file = path.join(dist, rel || 'index.html');
      if (!file.startsWith(dist) || !fs.existsSync(file)) return res.writeHead(404).end();
      res.writeHead(200, { 'Content-Type': mime[path.extname(file)] || 'application/octet-stream' }).end(fs.readFileSync(file));
    });
    await new Promise((ok) => server.listen(0, '127.0.0.1', ok));
    closers.push(() => server.close());
    await convertAt(browser, `http://127.0.0.1:${server.address().port}/sub/`);
  });

  await check('vite dev server with optimizeDeps.exclude converts', async () => {
    const server = await createServer({ root: app, base: '/sub/', logLevel: 'warn', optimizeDeps: { exclude: ['@niivue/dcm2niix'] }, server: { port: 0, host: '127.0.0.1' } });
    await server.listen();
    closers.push(() => server.close());
    await convertAt(browser, server.resolvedUrls.local[0]);
  });

  await check('TypeScript: shipped declarations type-check a consumer', async () => {
    execFileSync(path.join(js, 'node_modules', '.bin', 'tsc'), ['-p', app], { encoding: 'utf8' });
  });

  if (process.env.KEEP_TARBALL) {
    fs.copyFileSync(tarball, path.join(process.env.KEEP_TARBALL, path.basename(tarball)));
    console.log(`kept ${path.join(process.env.KEEP_TARBALL, path.basename(tarball))}`);
  }
} catch (e) {
  failed++;
  console.log(`FAIL setup\n     ${e.stdout || ''}${e.message}`);
} finally {
  for (const close of closers.reverse()) await close();
  fs.rmSync(tmp, { recursive: true, force: true });
}
process.exit(failed ? 1 : 0);
