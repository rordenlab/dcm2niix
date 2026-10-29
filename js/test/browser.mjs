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
    const { series, exitCode, log } = await dcm.convert(files, { args: ['-v', '1'] });
    return { exitCode, n: series.length, paired: series.every((s) => s.image && s.sidecar && s.json && s.json.SeriesNumber !== undefined), nested: log.some((l) => l.includes('/input/In/20180918Si/')) && log.some((l) => l.includes('/input/In/20180918GE/')) };
  });
  assert.equal(r.exitCode, 0);
  assert.equal(r.n, 8);
  assert.ok(r.paired, 'every image has a parsed sidecar');
  assert.ok(r.nested, 'inputs keep their folders (no a/b -> a_b flattening)');
});

test('convert: DWI series carries bval/bvec', async () => {
  const r = await run(async () => {
    const { series, exitCode } = await dcm.convert(await load('dcm_qa_uih/In/DTI_134434/dti_tra_dir16_PA_SaveBySlc__135612'));
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
    const { series, files, exitCode } = await dcm.convert([...good, bad]);
    return { exitCode, n: series.length, files: files.length };
  });
  assert.notEqual(r.exitCode, 0);
  assert.equal(r.n, 4);
  assert.equal(r.files, 8);
});

test('listSeries and selection by CRC; no conversion', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In');
    const list = await dcm.listSeries(files);
    const crc = list.find((s) => s.SeriesNumber === 4).CRC;
    const { series } = await dcm.convert(files, { args: ['-n', crc] });
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
    const files = await load('dcm_qa_nih/In/20180918GE');
    const args = ['-z', 'n'];
    await dcm.convert(files, { args, onProgress: (p) => progress.push(p), onLog: () => lines++ });
    const off = [];
    await dcm.convert(files, { args: ['--progress', 'n'], onProgress: (p) => off.push(p) });
    return { progress, lines, args, off: off.length };
  });
  assert.ok(r.progress.length >= 2 && r.progress.at(-1) === 1, `progress ${r.progress}`);
  assert.ok(r.lines > 0);
  assert.deepEqual(r.args, ['-z', 'n'], 'caller args are not mutated');
  assert.equal(r.off, 0, 'an explicit --progress n wins');
});

test('abort and timeout terminate the worker', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_uih/In/DTI_134434/t1_gre_fsp_3d_sag__134917');
    const ac = new AbortController();
    const p = dcm.convert(files, { signal: ac.signal });
    setTimeout(() => ac.abort(), 50);
    const aborted = await p.then(() => 'resolved', (e) => e.name);
    const timedOut = await dcm.convert(files, { timeoutMs: 1 }).then(() => 'resolved', (e) => e.name);
    const pre = new AbortController();
    pre.abort();
    const preAborted = await dcm.convert(files, { signal: pre.signal }).then(() => 'resolved', (e) => e.name);
    return { aborted, timedOut, preAborted };
  });
  assert.deepEqual(r, { aborted: 'AbortError', timedOut: 'TimeoutError', preAborted: 'AbortError' });
});

test('large input via WORKERFS (75 MB, 1 series)', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_uih/In/DTI_134434/t1_gre_fsp_3d_sag__134917');
    const bytes = files.reduce((a, f) => a + f.size, 0);
    const { series, exitCode } = await dcm.convert(files);
    return { exitCode, n: series.length, bytes };
  });
  assert.equal(r.exitCode, 0);
  assert.equal(r.n, 1);
  assert.ok(r.bytes > 70e6);
});

test('worker failures reject with a message', async () => {
  const r = await run(async () => {
    const blobWorker = (src) => () => new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
    const loadFail = await dcm.convert([], { createWorker: blobWorker("throw new Error('wasm fetch failed')") }).then(() => 'resolved', (e) => e.message);
    const crash = await dcm
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
    const d = new dcm.Dcm2niix();
    await d.init();
    const a = await d.input(files).z('n').run();
    const b = await d.input(files).z('n').run();
    const same = a.length === b.length && (await Promise.all(a.map(async (f, i) => (await sha256(f)) === (await sha256(b[i]))))).every(Boolean);
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
    const deepN = (await dcm.convert(deep)).series.length;
    const userOut = (await dcm.convert(files, { args: ['-o', '/elsewhere'] })).series.length;
    const clash = files.map((f, i) => Object.assign(new File([f], f.name), { _webkitRelativePath: i === 0 ? 'x' : `x/${f.name}` }));
    const clashN = (await dcm.convert(clash)).series.length;
    const taken = ['x', 'x_2', 'x']; // the suffix for index 2 is already used
    const dup = files.map((f, i) => Object.assign(new File([f], f.name), { _webkitRelativePath: taken[i] || f._webkitRelativePath }));
    const dupN = (await dcm.convert(dup, { timeoutMs: 20000 })).series.length;
    return { deepN, userOut, clashN, dupN };
  });
  assert.deepEqual(r, { deepN: 4, userOut: 4, clashN: 4, dupN: 4 });
});

test('builder rejects a concurrent run', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In/20180918GE');
    const d = new dcm.Dcm2niix();
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

test('runDcm2niix compatibility and option forwarding', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In/20180918GE');
    const names = (await dcm.runDcm2niix(files)).map((f) => f.name);
    const named = (await dcm.runDcm2niix(files, { args: ['-f', 'series_%s'] })).map((f) => f.name).sort();
    const ac = new AbortController();
    ac.abort();
    const aborted = await dcm.runDcm2niix(files, { signal: ac.signal }).then(() => 'resolved', (e) => e.name);
    const empty = await dcm.runDcm2niix([]);
    return { names, named, aborted, empty: empty.length };
  });
  assert.equal(r.names.length, 4);
  assert.ok(r.names.every((n) => n.endsWith('.nii')));
  assert.deepEqual(r.named, ['series_4.nii', 'series_5.nii', 'series_6.nii', 'series_7.nii']);
  assert.equal(r.aborted, 'AbortError');
  assert.equal(r.empty, 0);
});

test('exitCodes', async () => {
  const r = await run(async () => ({ codes: { ...dcm.exitCodes }, frozen: Object.isFrozen(dcm.exitCodes), empty: (await dcm.convert([])).exitCode }));
  assert.equal(r.frozen, true);
  assert.deepEqual(Object.values(r.codes), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]);
  assert.equal(r.empty, r.codes.noValidFiles);
});

test('JPEG 2000 series decodes to the native result', async () => {
  const r = await run(async () => {
    const { series, exitCode } = await dcm.convert(await load('dcm_qa/In/Orientation/axmb/AxInt36mb'));
    const image = series[0].image;
    const view = new DataView(await image.arrayBuffer());
    const dims = [1, 2, 3, 4].map((i) => view.getInt16(40 + 2 * i, true));
    const hash = await sha256(image.slice(view.getFloat32(108, true))); // voxels only
    return { exitCode, n: series.length, name: series[0].image.name, dims, hash };
  });
  assert.equal(r.exitCode, 0);
  assert.equal(r.n, 1);
  assert.equal(r.name, 'AxInt36mb_fMRI_MB_int_20140310133834_26.nii'); // native CLI name for that folder
  assert.deepEqual(r.dims, [86, 86, 36, 2]);
  assert.equal(r.hash, 'bfa0f6e4ecc8dcc517595935e7a760c817b2d8582217ef659d9e4658a6907bf9', 'voxels match the native build');
});

test('output names follow the shared top folder, as the CLI does', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In/20180918GE');
    const top = (await dcm.convert(files)).files.map((f) => f.name).sort();
    const roots = files.map((f, i) => Object.assign(new File([f], f.name), { _webkitRelativePath: `${i % 2 ? 'a' : 'b'}/${f.name}` }));
    const multi = (await dcm.convert(roots)).series.map((s) => s.image.name);
    return { top, multi };
  });
  const native = [4, 5, 6, 7].flatMap((n) => [`20180918GE_DCM2NIIX_regression_test_20180918114023_${n}.json`, `20180918GE_DCM2NIIX_regression_test_20180918114023_${n}.nii`]);
  assert.deepEqual(r.top, native, 'same names as dcm2niix -o out 20180918GE');
  assert.ok(r.multi.length === 4 && r.multi.every((n) => n.startsWith('input_')), `several roots keep input_: ${r.multi}`);
});

test('a 9 MB input (between the 8 MB batch and 64 MB whole-file limits) leaves outputs unchanged', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In/20180918GE');
    const filler = Object.assign(new File([new Uint8Array(9 * 1024 * 1024)], 'filler.bin'), { _webkitRelativePath: '20180918GE/filler.bin' }); // dcm2niix probes it; checks correctness, not which cache branch served it
    const [ref, big] = [await dcm.convert(files), await dcm.convert([filler, ...files])];
    const hashes = async (r) => Promise.all(r.series.map((s) => sha256(s.image)));
    return { n: big.series.length, same: JSON.stringify(await hashes(ref)) === JSON.stringify(await hashes(big)) };
  });
  assert.equal(r.n, 4);
  assert.ok(r.same, 'same images with the filler present');
});

test('timing (many-file input)', async () => {
  const r = await run(async () => {
    const files = await load('dcm_qa_nih/In');
    const time = async (fn) => {
      await fn(); // warm-up
      const t = [];
      for (let i = 0; i < 3; i++) {
        const t0 = performance.now();
        await fn();
        t.push(performance.now() - t0);
      }
      return Math.round(t.sort((a, b) => a - b)[1]);
    };
    return { files: files.length, convert: await time(() => dcm.convert(files)), listSeries: await time(() => dcm.listSeries(files)) };
  });
  console.log(`     timing ${r.files} files, median of 3: convert ${r.convert} ms, listSeries ${r.listSeries} ms`);
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
