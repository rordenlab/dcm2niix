# @niivue/dcm2niix

`@niivue/dcm2niix` runs [dcm2niix](https://github.com/rordenlab/dcm2niix) (WebAssembly) in a Web Worker, so web pages can convert DICOM to NIfTI with no upload. It is intended for the **browser**, not Node.js. One build is shipped, with JPEG-LS and JPEG 2000 support (~1 MB WASM).

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
| `series` | one entry per image: `{ stem, image, sidecar, json, bval, bvec }`; `json` is the parsed sidecar, `sidecar` the raw `File` |
| `files` | every output `File`, including unpaired ones such as physio; `file.relativePath` is its path in the output folder |
| `exitCode` | dcm2niix exit code; compare with `exitCodes` |
| `log` | dcm2niix console lines |

| Option | |
|---|---|
| `args` | dcm2niix arguments, e.g. `['-z', 'y', '-ba', 'n']`; input and output folders are set for you, and `-d 9` is the default search depth |
| `signal` | `AbortSignal`; aborting terminates the worker |
| `timeoutMs` | terminate and reject with `TimeoutError` |
| `onLog(text, stream)` | each console line |
| `onProgress(fraction)` | 0..1; turns on `--progress y` unless `args` sets `--progress` |
| `createWorker` | advanced: supply your own `Worker` |

Exit codes (`exitCodes`, mirroring `console/nii_dicom.h`): `ok` 0, `error` 1, `noValidFiles` 2, `version` 3, `corruptFile` 4, `inputFolderInvalid` 5, `outputFolderInvalid` 6, `outputFolderReadOnly` 7, `someOkSomeBad` 8, `renameError` 9, `incompleteVolumes` 10, `nominal` 11, `invalidParam` 12. Whether a partial result is acceptable is your application's decision; the last log line is not necessarily an error.

Privacy: dcm2niix defaults to `-ba y`, which omits names, dates and source UIDs from sidecars; free-text ImageComments is kept. Pass `-ba n` only when your application needs identifying fields, and keep such sidecars in memory.

Output names follow `-f` (default `%f_%p_%t_%s`, which includes the acquisition time). `%f` is the top folder of the input, as with the command line: a folder chosen with `webkitdirectory`, or a single dropped folder, gives `<folder>_…`; loose files or several top-level folders give `input_…`. Folder names can carry identifiers (e.g. a patient name); use an `-f` without `%f` if outputs leave the browser.

## Pick series before converting

```javascript
import { listSeries, convert } from '@niivue/dcm2niix';

const list = await listSeries(files); // no conversion, no pixel decoding; rejects unless dcm2niix exits 0
// [{ CRC, SeriesNumber, SeriesDescription, ProtocolName, Modality, Manufacturer, ImageType, Files, Rows, Columns, BidsGuess, ... }]
const pick = list.filter((s) => s.Modality === 'MR');
const { series } = await convert(files, { args: pick.flatMap((s) => ['-n', s.CRC]) }); // -n repeats up to 16 times
```

The listing is not a one-to-one list of outputs: entries such as Siemens physio logs (`Rows`/`Columns` 1) produce no image, and one entry can produce several images (e.g. `_e2` echoes). Pass `CRC` back to `-n` and use the `series` that conversion returns; do not build filenames from the listing. It has no voxel size or volume count. `StudyInstanceUID`/`SeriesInstanceUID` appear unless `-ba y`; `PatientID` only with `-ba n`. The listing does not apply `-i y` skips.

## Bundlers

The package creates its worker with `new Worker(new URL('./worker.jpeg.js', import.meta.url), { type: 'module' })`. Tested with Vite 6 (production build under a subpath, and the dev server). **Vite's dev server** pre-bundles dependencies and loses the worker, so exclude the package:

```javascript
// vite.config.js
export default { optimizeDeps: { exclude: ['@niivue/dcm2niix'] } };
```

webpack 5 supports this worker pattern but is not tested here. esbuild does not emit the worker or WASM: copy them and pass `createWorker`.

To host the worker yourself, serve `worker.jpeg.js`, `dcm2niix.jpeg.js` and `dcm2niix.jpeg.wasm` from `dist/` side by side and pass `createWorker: () => new Worker(url, { type: 'module' })`.

## Upgrading from 1.2/1.3 or @niivue/nv-ext-dcm2niix

- Delete any local `declare module '@niivue/dcm2niix'` shim: it hides the shipped types (including `convert`). The old shim name `Dcm2niixProcessor` is exported.
- `@niivue/dcm2niix/plain` and `/jpeg` now load the same JPEG-capable build.
- Output names start with the input folder instead of `input_`.
- `runDcm2niix(files, { niftiOnly, ...options })` and `traverseDataTransferItems(items)` match `@niivue/nv-ext-dcm2niix`; other options (`signal`, `timeoutMs`, `args`, …) go to `convert`. A wrapper that adds timeouts, cancellation, worker cleanup and sidecar pairing reduces to:

```javascript
import { convert, exitCodes } from '@niivue/dcm2niix';

const { series, exitCode, log } = await convert(files, { signal, timeoutMs: 120000 }); // add args: ['-ba', 'n'] only if you need identifying sidecars
if (exitCode !== exitCodes.ok) throw new Error(`dcm2niix exit code ${exitCode}: ${log.slice(-3).join(' ')}`); // or keep the partial series
const images = series.map((s) => ({ image: s.image, sidecar: s.json ?? null }));
```

Mixed NIfTI/DICOM drops, requiring a single series, and empty-output messages stay in your application.

## Builder API

`groupOutputs(files)` builds the `series` array from output files (used by `convert`). The chainable builder remains for full control over one long-lived worker:

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
pixi run wasm      # download/build OpenJPEG for WASM, then build dcm2niix and the package
pixi run pack-npm  # wasm, then npm test, npm run test:consumer and npm pack
pixi run clean     # remove the OpenJPEG build
```

Manually, from `js/` (inside `pixi shell` so `emcc` is the pinned version):

```bash
npm install
npm run build          # prebuild compiles the WASM via ../console/makefile
npm test               # Node grouping tests, then Playwright browser tests on the dcm_qa/dcm_qa_nih/dcm_qa_uih submodules
npm run test:consumer  # pack, install into a Vite app, build/dev/type-check
npm run demo           # build and serve index.html
```

Browser tests use the built `dist` and need the submodules (`git submodule update --init`) and a Playwright Chromium (`npx playwright install chromium`). `npm pack` runs `scripts/check-dist.mjs`, which refuses a stale or incomplete `dist`. Publish the tested tarball: `npm publish <tgz> --tag rc` (prerelease) or `--tag latest`.
