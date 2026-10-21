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
