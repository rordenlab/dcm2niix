// Browser checks for the built package (run `npm run build` first). Fixtures are the dcm_qa, dcm_qa_nih and dcm_qa_uih submodules.
import assert from 'node:assert/strict';
import { chromium } from 'playwright';
import { startServer } from './server.mjs';

const server = await startServer();
const base = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch();
const page = await browser.newPage();
page.on('pageerror', (e) => console.error('pageerror:', e.message));
await page.goto(`${base}/js/test/harness.html`);
await page.waitForFunction(() => window.ready === true);

const tests = [];
const test = (name, fn) => tests.push({ name, fn });
const run = (fn, arg) => page.evaluate(fn, arg);

test('convert: grouping, folder structure reaches dcm2niix', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In');
    const { series, exitCode, log } = await plain.convert(files, { args: ['-v', '1'] });
    return { exitCode, n: series.length, paired: series.every((s) => s.image && s.sidecar && s.json && s.json.SeriesNumber !== undefined), nested: log.some((l) => l.includes('/input/In/20180918Si/')) && log.some((l) => l.includes('/input/In/20180918GE/')) };
  });
  assert.equal(r.exitCode, 0);
  assert.equal(r.n, 8);
  assert.ok(r.paired, 'every image has a parsed sidecar');
  assert.ok(r.nested, 'inputs keep their folders (no a/b -> a_b flattening)');
});

test('convert: DWI series carries bval/bvec', async () => {
  const r = await run(async () => {
    const { series, exitCode } = await plain.convert(await load('dcm_qa_uih/In/DTI_134434/dti_tra_dir16_PA_SaveBySlc__135612'));
    return { exitCode, s: series.map((x) => ({ bval: !!x.bval, bvec: !!x.bvec })) };
  });
  assert.equal(r.exitCode, 0);
  assert.deepEqual(r.s, [{ bval: true, bvec: true }]);
});

test('convert: nonzero exit keeps partial output', async () => {
  const r = await run(async () => {
    const good = await load('dcm_qa_nih/In/20180918Si');
    const j2k = await (await fetch('/dcm_qa/In/Orientation/axmb/AxInt36mb/jp2k1.dcm')).blob();
    const bad = new File([j2k.slice(0, 200000)], 'trunc.dcm');
    bad._webkitRelativePath = 'bad/trunc.dcm';
    const { series, files, exitCode } = await jpeg.convert([...good, bad]);
    return { exitCode, n: series.length, files: files.length };
  });
  assert.notEqual(r.exitCode, 0);
  assert.equal(r.n, 4);
  assert.equal(r.files, 8);
});

test('listSeries and selection by CRC; no conversion', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In');
    const list = await plain.listSeries(files);
    const crc = list.find((s) => s.SeriesNumber === 4).CRC;
    const { series } = await plain.convert(files, { args: ['-n', crc] });
    return { n: list.length, anon: list.every((s) => !s.PatientID && !s.SeriesInstanceUID), picked: series.map((s) => s.json.SeriesNumber) };
  });
  assert.equal(r.n, 8);
  assert.ok(r.anon, '-ba y default omits identifiers');
  assert.deepEqual(r.picked, [4], 'GE and Siemens series 4 have different CRCs; -n picks one');
});

test('onProgress and onLog', async () => {
  const r = await run(async () => {
    const progress = [];
    let lines = 0;
    await plain.convert(await load('dcm_qa_nih/In/20180918GE'), { args: ['--progress', 'y'], onProgress: (p) => progress.push(p), onLog: () => lines++ });
    return { progress, lines };
  });
  assert.ok(r.progress.length >= 2 && r.progress.at(-1) === 1, `progress ${r.progress}`);
  assert.ok(r.lines > 0);
});

test('abort and timeout terminate the worker', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_uih/In/DTI_134434/t1_gre_fsp_3d_sag__134917');
    const ac = new AbortController();
    const p = plain.convert(files, { signal: ac.signal });
    setTimeout(() => ac.abort(), 50);
    const aborted = await p.then(() => 'resolved', (e) => e.name);
    const timedOut = await plain.convert(files, { timeoutMs: 1 }).then(() => 'resolved', (e) => e.name);
    const pre = new AbortController();
    pre.abort();
    const preAborted = await plain.convert(files, { signal: pre.signal }).then(() => 'resolved', (e) => e.name);
    return { aborted, timedOut, preAborted };
  });
  assert.deepEqual(r, { aborted: 'AbortError', timedOut: 'TimeoutError', preAborted: 'AbortError' });
});

test('large input via WORKERFS (75 MB, 1 series)', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_uih/In/DTI_134434/t1_gre_fsp_3d_sag__134917');
    const bytes = files.reduce((a, f) => a + f.size, 0);
    const { series, exitCode } = await plain.convert(files);
    return { exitCode, n: series.length, bytes };
  });
  assert.equal(r.exitCode, 0);
  assert.equal(r.n, 1);
  assert.ok(r.bytes > 70e6);
});

test('worker failures reject with a message', async () => {
  const r = await run(async () => {
    const blobWorker = (src) => () => new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
    const loadFail = await plain.convert([], { createWorker: blobWorker("throw new Error('wasm fetch failed')") }).then(() => 'resolved', (e) => e.message);
    const crash = await plain
      .convert([], { createWorker: blobWorker("postMessage({type:'ready'}); onmessage = () => { throw new Error('boom') }") })
      .then(() => 'resolved', (e) => e.message);
    return { loadFail, crash };
  });
  assert.match(r.loadFail, /wasm fetch failed/);
  assert.match(r.crash, /boom/);
});

test('builder: two runs on one worker, legacy results and rejection', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In/20180918GE');
    const d = new plain.Dcm2niix();
    await d.init();
    const a = await d.input(files).z('n').run();
    const b = await d.input(files).z('n').run();
    const same = a.length === b.length && (await Promise.all(a.map(async (f, i) => (await f.text()) === (await b[i].text())))).every(Boolean);
    const empty = await d.input([]).run().then(() => 'resolved', (e) => e.message);
    const c = await d.input(files).bids('n').run();
    d.worker.terminate();
    return { a: a.length, same, empty, c: c.map((f) => f.name) };
  });
  assert.equal(r.a, 8);
  assert.ok(r.same, 'second run on the same worker gives identical outputs');
  assert.match(r.empty, /exit code \d+/);
  assert.ok(r.c.every((n) => !n.endsWith('.json')), 'options from an earlier run do not leak');
});

test('deep folders, user -o, file/folder name clash', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In/20180918GE');
    const deep = files.map((f) => Object.assign(new File([f], f.name), { _webkitRelativePath: `r/a/b/c/d/e/${f._webkitRelativePath}` }));
    const deepN = (await plain.convert(deep)).series.length;
    const userOut = (await plain.convert(files, { args: ['-o', '/elsewhere'] })).series.length;
    const clash = files.map((f, i) => Object.assign(new File([f], f.name), { _webkitRelativePath: i === 0 ? 'x' : `x/${f.name}` }));
    const clashN = (await plain.convert(clash)).series.length;
    const taken = ['x', 'x_2', 'x']; // the suffix for index 2 is already used
    const dup = files.map((f, i) => Object.assign(new File([f], f.name), { _webkitRelativePath: taken[i] || f._webkitRelativePath }));
    const dupN = (await plain.convert(dup, { timeoutMs: 20000 })).series.length;
    return { deepN, userOut, clashN, dupN };
  });
  assert.deepEqual(r, { deepN: 4, userOut: 4, clashN: 4, dupN: 4 });
});

test('builder rejects a concurrent run', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In/20180918GE');
    const d = new plain.Dcm2niix();
    await d.init();
    const first = d.input(files).bids('n').run();
    const second = await d.input(files).bids('y').run().then(() => 'resolved', (e) => e.message);
    const a = await first;
    d.worker.terminate();
    return { second, firstJson: a.some((f) => f.name.endsWith('.json')) };
  });
  assert.match(r.second, /already running/);
  assert.equal(r.firstJson, false);
});

test('runDcm2niix compatibility', async () => {
  const r = await run(async () => (await plain.runDcm2niix(await load('dcm_qa_nih/In/20180918GE'))).map((f) => f.name));
  assert.equal(r.length, 4);
  assert.ok(r.every((n) => n.endsWith('.nii')));
});

let failed = 0;
for (const { name, fn } of tests) {
  const t0 = Date.now();
  try {
    await fn();
    console.log(`ok   ${name} (${Date.now() - t0} ms)`);
  } catch (e) {
    failed++;
    console.log(`FAIL ${name}\n     ${e.message}`);
  }
}
await browser.close();
server.close();
process.exit(failed ? 1 : 0);
