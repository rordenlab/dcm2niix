// Node checks for output grouping (no browser needed).
import assert from 'node:assert/strict';
import { groupOutputs } from '../src/core.js';

const f = (name, body = '', relativePath) => {
  const file = new File([body], name);
  if (relativePath) Object.defineProperty(file, 'relativePath', { value: relativePath });
  return file;
};

const files = [
  f('t2_e1.nii.gz'), f('t2_e1.json', '{"EchoNumber":1}'),
  f('t2_e2.nii.gz'), f('t2_e2.json', '{"EchoNumber":2}'),
  f('dwi.nii'), f('dwi.json', 'not json'), f('dwi.bval'), f('dwi.bvec'),
  f('bold_physio.tsv.gz'), f('bold_physio.json', '{}'),
  f('a.nii', '', 'sub-1/a.nii'), f('a.nii', '', 'sub-2/a.nii'), f('a.json', '{"x":2}', 'sub-2/a.json'),
];
const series = await groupOutputs(files);
const byKey = Object.fromEntries(series.map((s) => [s.image.relativePath || s.stem, s]));
assert.deepEqual(Object.keys(byKey).sort(), ['dwi', 'sub-1/a.nii', 'sub-2/a.nii', 't2_e1', 't2_e2']);
assert.equal(byKey.t2_e2.json.EchoNumber, 2);
assert.ok(byKey.dwi.bval && byKey.dwi.bvec && byKey.dwi.sidecar && byKey.dwi.json === undefined, 'bad JSON keeps the File');
assert.equal(byKey['sub-2/a.nii'].json.x, 2);
assert.equal(byKey['sub-1/a.nii'].sidecar, undefined, 'same stem in other folders does not pair');
assert.ok(!series.some((s) => s.stem === 'bold_physio'), 'physio has no image so stays in files only');
console.log('ok   groupOutputs');

// createFrameDecoder pool accounting, with a fake worker that replies only when told to.
import { getEventListeners } from 'node:events';
import { createFrameDecoderFactory, FrameDecodeError } from '../src/frames.js';

const fakeWorkers = [];
const createFrameDecoder = createFrameDecoderFactory(() => {
  const w = { posted: [], terminated: false, postMessage(msg) { this.posted.push(msg); }, terminate() { this.terminated = true; } };
  w.reply = (msg, info = { status: 'ok', width: 1, height: 1 }) => w.onmessage({ data: { id: msg.id, info, rgba: info.status === 'ok' ? new Uint8ClampedArray(4) : undefined } });
  fakeWorkers.push(w);
  return w;
});
const settled = (p) => p.then((v) => ['ok', v], (e) => ['err', e]);

// An aborted decode rejects at once but keeps its capacity until the worker replies.
{
  const dec = createFrameDecoder({ workers: 1, maxPending: 1 });
  const w = fakeWorkers.at(-1);
  const ac = new AbortController();
  const first = settled(dec.decode(new Uint8Array(8), { signal: ac.signal }));
  ac.abort();
  assert.equal((await first)[1].name, 'AbortError');
  const blocked = await settled(dec.decode(new Uint8Array(8)));
  assert.match(blocked[1].message, /busy/, 'capacity held until the worker replies');
  assert.equal(w.posted.length, 1, 'nothing more reached the worker');
  w.reply(w.posted[0]);
  const next = dec.decode(new Uint8Array(8));
  assert.equal(w.posted.length, 2, 'the reply released the slot');
  w.reply(w.posted[1]);
  assert.equal((await settled(next))[0], 'ok');
  dec.terminate();
}

// Any abort reason rejects, even a falsy one, and is passed through unchanged.
{
  const dec = createFrameDecoder({ workers: 1, maxPending: 8 });
  for (const reason of [null, false, 0, '']) {
    const ac = new AbortController();
    const p = settled(dec.decode(new Uint8Array(8), { signal: ac.signal }));
    ac.abort(reason);
    assert.deepEqual(await p, ['err', reason], `abort(${JSON.stringify(reason)}) rejects`);
  }
  dec.terminate();
}

// Abort listeners are removed when a decode settles, however it settles.
{
  const dec = createFrameDecoder({ workers: 1 });
  const w = fakeWorkers.at(-1);
  const ac = new AbortController();
  const ok = dec.decode(new Uint8Array(8), { signal: ac.signal });
  const bad = settled(dec.decode(new Uint8Array(8), { signal: ac.signal }));
  const cut = settled(dec.decode(new Uint8Array(8), { signal: ac.signal }));
  assert.equal(getEventListeners(ac.signal, 'abort').length, 3);
  w.reply(w.posted[0]);
  w.reply(w.posted[1], { status: 'decode-error' });
  await ok;
  assert.ok((await bad)[1] instanceof FrameDecodeError);
  dec.terminate();
  assert.equal((await cut)[1].message, 'frame decoder terminated');
  assert.equal(getEventListeners(ac.signal, 'abort').length, 0, 'no listener outlives its decode');
  assert.ok(w.terminated);
}

// The input is copied: a view into a larger buffer posts only its bytes, and the caller keeps them.
{
  const dec = createFrameDecoder({ workers: 1 });
  const w = fakeWorkers.at(-1);
  const big = new Uint8Array(16).map((_, i) => i);
  dec.decode(big.subarray(4, 8)).catch(() => {});
  assert.deepEqual([...new Uint8Array(w.posted[0].bytes)], [4, 5, 6, 7]);
  dec.decode(big, { photometric: null }).catch(() => {}); // a missing DICOM tag
  assert.equal(w.posted[1].photometric, '');
  const huge = await settled(dec.decode(new Uint8Array(64 * 1024 * 1024 + 1)));
  assert.equal(huge[1].status, 'too-large');
  assert.equal(w.posted.length, 2, 'an oversized input never reaches the worker');
  dec.terminate();
}
console.log('ok   createFrameDecoder pool accounting');
