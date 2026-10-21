# @niivue/dcm2niix

`@niivue/dcm2niix` runs [dcm2niix](https://github.com/rordenlab/dcm2niix) (WebAssembly) in a Web Worker, so web pages can convert DICOM to NIfTI with no upload. It is intended for the **browser**, not Node.js.

## Quick start

```javascript
import { convert, filesFromDataTransfer } from '@niivue/dcm2niix';

// <input type="file" webkitdirectory multiple> or a drop event
input.onchange = async () => show(await convert(input.files));
target.ondrop = async (e) => {
  e.preventDefault();
  show(await convert(await filesFromDataTransfer(e.dataTransfer.items))); // read items inside the handler
};

function show({ series, files, exitCode, log }) {
  for (const s of series) console.log(s.stem, s.image, s.json?.SeriesDescription, s.bval, s.bvec);
}
```

`convert(files, options)` starts a worker, converts, and terminates the worker. It resolves for any dcm2niix exit code, so a run where some series fail still returns the ones that converted; it rejects only if the worker or WASM fails, or on abort/timeout.

| Result | |
|---|---|
| `series` | one entry per image: `{ stem, image, sidecar, json, bval, bvec }` |
| `files` | every output `File`, including unpaired ones such as physio; `file.relativePath` is its path in the output folder |
| `exitCode` | dcm2niix exit code (0 = success) |
| `log` | dcm2niix console lines |

| Option | |
|---|---|
| `args` | dcm2niix arguments, e.g. `['-z', 'y', '-ba', 'n']`; input and output folders are set for you, and `-d 9` is the default search depth |
| `signal` | `AbortSignal`; aborting terminates the worker |
| `timeoutMs` | terminate and reject with `TimeoutError` |
| `onLog(text, stream)` | each console line |
| `onProgress(fraction)` | 0..1; add `'--progress', 'y'` to `args` |
| `createWorker` | advanced: supply your own `Worker` |

Privacy: dcm2niix defaults to `-ba y`, which omits names, dates and source UIDs from sidecars; free-text ImageComments is kept. Filenames follow `-f` (default `%f_%p_%t_%s`, which includes the acquisition time).

## Pick series before converting

```javascript
import { listSeries, convert } from '@niivue/dcm2niix';

const list = await listSeries(files); // no conversion, no pixel decoding; rejects unless dcm2niix exits 0
// [{ CRC, SeriesNumber, SeriesDescription, ProtocolName, Modality, Manufacturer, ImageType, Files, Rows, Columns, BidsGuess, ... }]
const pick = list.filter((s) => s.Modality === 'MR');
const { series } = await convert(files, { args: pick.flatMap((s) => ['-n', s.CRC]) }); // -n repeats up to 16 times
```

`StudyInstanceUID`/`SeriesInstanceUID` appear unless `-ba y`; `PatientID` only with `-ba n`. Use them, for example, to refuse mixed examinations. The listing does not apply `-i y` skips.

## Builds

| Import | Contents |
|---|---|
| `@niivue/dcm2niix` or `@niivue/dcm2niix/jpeg` | JPEG-LS and JPEG 2000 support (~1 MB WASM) |
| `@niivue/dcm2niix/plain` | smaller (~0.6 MB WASM), no JPEG-LS/JPEG 2000 |

TypeScript declarations are included.

## Bundlers

The package creates its worker with `new Worker(new URL('./worker.js', import.meta.url))`, which Vite, webpack 5 and esbuild-based builds handle. **Vite's dev server** pre-bundles dependencies and loses the worker, so exclude the package:

```javascript
// vite.config.js
export default { optimizeDeps: { exclude: ['@niivue/dcm2niix'] } };
```

If your setup cannot serve the packaged worker, host `worker.js`, `dcm2niix.js` and `dcm2niix.wasm` yourself and pass `createWorker: () => new Worker(url, { type: 'module' })`.

## Compatibility API

- `runDcm2niix(files, { niftiOnly = true })` and `traverseDataTransferItems(items)` match `@niivue/nv-ext-dcm2niix`.
- `groupOutputs(files)` builds the `series` array from output files (used by `convert`).
- The chainable builder remains for full control over one long-lived worker:

```javascript
import { Dcm2niix } from '@niivue/dcm2niix';
const dcm2niix = new Dcm2niix();
await dcm2niix.init();
const outputFiles = await dcm2niix.input(files).bids('y').z('y').run(); // rejects unless exit code is 0 (or 3 for --version)
dcm2niix.worker.terminate();
```

## Development

You will need Node/npm, [Pixi](https://prefix.dev/) and Make. Pixi provides Emscripten.

```bash
pixi run wasm      # download/build OpenJPEG for WASM, then build both dcm2niix variants and the package
pixi run clean     # remove the OpenJPEG build
```

Manually, from `js/` (inside `pixi shell` so `emcc` is the pinned version):

```bash
npm install
npm run build      # prebuild compiles both WASM variants via ../console/makefile
npm test           # Node grouping tests, then Playwright browser tests on the dcm_qa/dcm_qa_nih/dcm_qa_uih submodules
npm run demo       # build and serve index.html
```

Browser tests use the built `dist` and need the submodules (`git submodule update --init`) and a Playwright Chromium (`npx playwright install chromium`).
