// Public API; the entry file supplies the worker factory so bundlers see a literal worker URL.

const IMAGE_EXT = /\.(nii\.gz|nii|nrrd|nhdr|mgz|mgh|jnii|bnii)$/i;
const STEM_EXT = /\.(nii\.gz|nii|nrrd|nhdr|mgz|mgh|jnii|bnii|json|bval|bvec)$/i;

// Safari drops webkitRelativePath when a File crosses into a worker, so send it alongside.
// _webkitRelativePath is set by filesFromDataTransfer (drop events).
const conformFileList = (files) =>
  Array.from(files || []).map((file) => ({ file, webkitRelativePath: file.webkitRelativePath || file._webkitRelativePath || '' }));

const errorFrom = (eventOrError, fallback) => new Error((eventOrError && eventOrError.message) || fallback);

const abortError = (signal) => (signal && signal.reason !== undefined ? signal.reason : new DOMException('dcm2niix conversion aborted', 'AbortError'));

/** dcm2niix exit codes; mirrors kEXIT_* in console/nii_dicom.h. */
export const exitCodes = Object.freeze({
  ok: 0,
  error: 1,
  noValidFiles: 2,
  version: 3,
  corruptFile: 4,
  inputFolderInvalid: 5,
  outputFolderInvalid: 6,
  outputFolderReadOnly: 7,
  someOkSomeBad: 8,
  renameError: 9,
  incompleteVolumes: 10,
  nominal: 11,
  invalidParam: 12,
});

const tail = (log, n = 5) => (log && log.length ? `: ${log.slice(-n).join(' | ')}` : '');

// Resolves when the worker's WASM is ready; rejects if it cannot load.
const whenReady = (worker) =>
  new Promise((resolve, reject) => {
    worker.onmessage = (e) => {
      if (e.data && e.data.type === 'ready') resolve(worker);
      else if (e.data && e.data.type === 'error') reject(new Error(e.data.message || 'dcm2niix worker failed to load'));
    };
    worker.onerror = (e) => {
      if (e.preventDefault) e.preventDefault();
      // a 404 worker gives an empty message; the usual cause is dev pre-bundling moving the package
      reject(errorFrom(e, "dcm2niix worker failed to load (Vite dev: set optimizeDeps.exclude: ['@niivue/dcm2niix'], or pass createWorker)"));
    };
    worker.onmessageerror = () => reject(new Error('dcm2niix worker message could not be deserialized'));
  });

// One run on a ready worker; resolves { files, exitCode, log } for any dcm2niix exit code.
const runOnWorker = (worker, fileList, cmd, { onLog, onProgress } = {}) =>
  new Promise((resolve, reject) => {
    worker.onmessage = (e) => {
      const data = e.data || {};
      if (data.type === 'log') {
        if (onLog) onLog(data.text, data.stream);
        const m = onProgress && /^Progress: ([0-9.eE+-]+)/.exec(data.text);
        if (m) onProgress(Number(m[1]));
      } else if (data.type === 'error') {
        reject(new Error(data.message || 'dcm2niix worker error'));
      } else if (data.type === 'result') {
        const files = data.convertedFiles || [];
        (data.paths || []).forEach((path, i) => Object.defineProperty(files[i], 'relativePath', { value: path, enumerable: true }));
        resolve({ files, exitCode: data.exitCode, log: data.log || [] });
      }
    };
    worker.onerror = (e) => {
      if (e.preventDefault) e.preventDefault();
      reject(errorFrom(e, 'dcm2niix worker crashed'));
    };
    worker.onmessageerror = () => reject(new Error('dcm2niix worker message could not be deserialized'));
    worker.postMessage({ fileList, cmd: cmd.map(String) });
  });

/**
 * Group dcm2niix outputs by stem: one entry per image with its sidecar, parsed JSON and DWI gradients.
 * Files without an image (e.g. physio) stay only in the flat file list.
 */
export async function groupOutputs(files) {
  const byStem = new Map();
  for (const file of files) {
    if (!STEM_EXT.test(file.name)) continue;
    const key = (file.relativePath || file.name).replace(STEM_EXT, '');
    if (!byStem.has(key)) byStem.set(key, { stem: file.name.replace(STEM_EXT, '') });
    const entry = byStem.get(key);
    if (IMAGE_EXT.test(file.name)) entry.image = file;
    else if (/\.json$/i.test(file.name)) entry.sidecar = file;
    else if (/\.bval$/i.test(file.name)) entry.bval = file;
    else if (/\.bvec$/i.test(file.name)) entry.bvec = file;
  }
  const series = [...byStem.values()].filter((s) => s.image);
  await Promise.all(
    series.map(async (s) => {
      if (!s.sidecar) return;
      try {
        s.json = JSON.parse(await s.sidecar.text());
      } catch {
        // leave json undefined; the raw sidecar File is still returned
      }
    })
  );
  return series;
}

/**
 * Collect files from a drop event, recursing into folders and stamping `_webkitRelativePath`.
 * Call it inside the drop handler: DataTransferItems are cleared once the handler returns.
 * Adapted from @niivue/nv-ext-dcm2niix 1.0.0-rc.24 traverseDataTransferItems.
 */
export async function filesFromDataTransfer(items) {
  const files = [];
  const entries = [];
  for (let i = 0; i < items.length; i++) {
    const entry = items[i].webkitGetAsEntry ? items[i].webkitGetAsEntry() : null;
    if (entry) entries.push(entry);
  }
  await Promise.all(entries.map((entry) => walkEntry(entry, '', files)));
  return files;
}

function walkEntry(entry, path, out) {
  return new Promise((resolve, reject) => {
    if (entry.isFile) {
      entry.file((file) => {
        file._webkitRelativePath = path + file.name;
        out.push(file);
        resolve();
      }, reject);
      return;
    }
    if (entry.isDirectory) {
      const reader = entry.createReader();
      const childPath = `${path}${entry.name}/`;
      const readBatch = () => {
        reader.readEntries((batch) => {
          if (batch.length === 0) return resolve();
          Promise.all(batch.map((child) => walkEntry(child, childPath, out)))
            .then(readBatch)
            .catch(reject);
        }, reject);
      };
      readBatch();
      return;
    }
    resolve();
  });
}

/** Alias kept for @niivue/nv-ext-dcm2niix users. */
export const traverseDataTransferItems = filesFromDataTransfer;

export function createApi(defaultCreateWorker) {
  /**
   * Convert DICOM files in a fresh worker that is terminated afterwards.
   * Resolves for any dcm2niix exit code (partial output is kept); rejects on worker/WASM failure, abort or timeout.
   */
  async function convert(files, { args = [], signal, timeoutMs, onLog, onProgress, createWorker = defaultCreateWorker } = {}) {
    if (signal && signal.aborted) throw abortError(signal);
    if (onProgress && !args.includes('--progress')) args = [...args, '--progress', 'y']; // an explicit --progress wins
    const worker = createWorker();
    let timer;
    let onAbort;
    const stopped = new Promise((_, reject) => {
      if (signal) {
        onAbort = () => reject(abortError(signal));
        signal.addEventListener('abort', onAbort, { once: true });
      }
      if (timeoutMs > 0) timer = setTimeout(() => reject(new DOMException(`dcm2niix timed out after ${timeoutMs} ms`, 'TimeoutError')), timeoutMs);
    });
    try {
      await Promise.race([whenReady(worker), stopped]);
      const result = await Promise.race([runOnWorker(worker, conformFileList(files), args, { onLog, onProgress }), stopped]);
      return { series: await groupOutputs(result.files), ...result };
    } finally {
      worker.terminate();
      clearTimeout(timer);
      if (signal && onAbort) signal.removeEventListener('abort', onAbort);
    }
  }

  /** List series without converting (dcm2niix -n -2). Pass a series `CRC` to `convert(files, { args: ['-n', crc] })`. */
  async function listSeries(files, options = {}) {
    const result = await convert(files, { ...options, args: [...(options.args || []), '-n', '-2'] });
    const listing = result.files.find((f) => f.name === 'dcm2niix_series.json');
    // the listing starts as "[]", so a failed scan could still leave valid but incomplete JSON
    if (!listing || result.exitCode !== 0) throw new Error(`dcm2niix series listing failed (exit code ${result.exitCode})${tail(result.log)}`);
    return JSON.parse(await listing.text());
  }

  /** @niivue/nv-ext-dcm2niix compatible: NIfTI files only by default; rejects unless dcm2niix succeeds. */
  async function runDcm2niix(files, { niftiOnly = true, ...options } = {}) {
    if (!files || files.length === 0) return [];
    const { files: outputs, exitCode, log } = await convert(files, options);
    if (exitCode !== 0 && exitCode !== 3) throw new Error(`dcm2niix processing failed with exit code ${exitCode}${tail(log)}`);
    return niftiOnly ? outputs.filter((f) => /\.nii(\.gz)?$/i.test(f.name)) : outputs;
  }

  class Dcm2niix {
    constructor() {
      this.worker = null;
    }

    init() {
      this.worker = defaultCreateWorker();
      return whenReady(this.worker).then(() => true);
    }

    conformFileList(fileObjectOrArray) {
      return conformFileList(fileObjectOrArray);
    }

    input(fileListObject) {
      return new Processor({ worker: this.worker, fileList: conformFileList(fileListObject) });
    }

    inputFromWebkitDirectory(fileListObject) {
      return this.input(fileListObject);
    }

    inputFromDropItems(dataTransferItemArray) {
      return this.input(dataTransferItemArray);
    }
  }

  return { Dcm2niix, convert, listSeries, runDcm2niix };
}

class Processor {
  constructor({ worker, fileList }) {
    this.worker = worker;
    this.fileList = fileList;
    this.commands = [];
  }

  _addCommand(cmd, ...args) {
    this.commands.push(cmd, ...args.map(String));
    return this;
  }

  // --version
  version() { return this._addCommand('--version'); }
  // -1..-9 gz compression level (1=fastest..9=smallest, default 6)
  compressionLevel(level) { return this._addCommand(`-${level}`); }
  // -a adjacent DICOMs (images from same series always in same folder) for faster conversion (n/y, default n)
  a(value) { return this._addCommand('-a', value); }
  adjacent(value) { return this.a(value); }
  // -b BIDS sidecar (y/n/o [o=only: no NIfTI], default y)
  b(value) { return this._addCommand('-b', value); }
  bids(value) { return this.b(value); }
  // -ba BIDS anonymize (y/n/o, default y)
  ba(value) { return this._addCommand('-ba', value); }
  bidsAnonymize(value) { return this.ba(value); }
  // -c comment stored as NIfTI aux_file (up to 24 characters)
  c(value) { return this._addCommand('-c', value); }
  comment(value) { return this.c(value); }
  // -d directory search depth (0..9, default 5)
  d(value) { return this._addCommand('-d', value); }
  directorySearchDepth(value) { return this.d(value); }
  // -e export as NRRD (y) or MGH (o) or JSON/JNIfTI (j) or BJNIfTI (b) instead of NIfTI (y/n/o/j/b, default n)
  e(value) { return this._addCommand('-e', value); }
  exportFormat(value) { return this.e(value); }
  // -f filename format (default '%f_%p_%t_%s'; see dcm2niix -h)
  f(value) { return this._addCommand('-f', value); }
  filenameformat(value) { return this.f(value); }
  // -i ignore derived, localizer and 2D images (y/n, default n)
  i(value) { return this._addCommand('-i', value); }
  ignoreDerived(value) { return this.i(value); }
  // -l losslessly scale 16-bit integers to use dynamic range (y/n/o, default o)
  l(value) { return this._addCommand('-l', value); }
  losslessScale(value) { return this.l(value); }
  // -m merge 2D slices from same series regardless of echo, exposure, etc. (n/y or 0/1/2, default 2)
  m(value) { return this._addCommand('-m', value); }
  merge2DSlices(value) { return this.m(value); }
  // -n only convert this series CRC number (-1 = report series, -2 = JSON series listing)
  n(value) { return this._addCommand('-n', value); }
  seriesCRC(value) { return this.n(value); }
  // -p Philips precise float (not display) scaling (y/n, default y)
  p(value) { return this._addCommand('-p', value); }
  philipsPreciseFloat(value) { return this.p(value); }
  // -q only search directory for DICOMs (y/l/n, default n)
  q(value) { return this._addCommand('-q', value); }
  searchDirectory(value) { return this.q(value); }
  // -r rename instead of convert DICOMs (y/n, default n)
  r(value) { return this._addCommand('-r', value); }
  renameOnly(value) { return this.r(value); }
  // -s single file mode, do not convert other images in folder (y/n, default n)
  s(value) { return this._addCommand('-s', value); }
  singleFileMode(value) { return this.s(value); }
  // -v verbose (n/y or 0/1/2, default 0)
  v(value) { return this._addCommand('-v', value); }
  verbose(value) { return this.v(value); }
  // -w write behavior for name conflicts (0=skip, 1=overwrite, 2=add suffix; default 2)
  w(value) { return this._addCommand('-w', value); }
  writeBehavior(value) { return this.w(value); }
  // -x crop 3D acquisitions (y/n/i, default n)
  x(value) { return this._addCommand('-x', value); }
  crop(value) { return this.x(value); }
  // -z gz compress images (y/o/i/n/3, default n)
  z(value) { return this._addCommand('-z', value); }
  gzip(value) { return this.z(value); }
  // --big-endian byte order (y/n/o, default o)
  bigEndian(value) { return this._addCommand('--big-endian', value); }
  // --ignore_trigger_times disregard values in 0018,1060 and 0020,9153
  ignoreTriggerTimes() { return this._addCommand('--ignore_trigger_times'); }
  // --terse omit filename post-fixes (can cause overwrites)
  terse() { return this._addCommand('--terse'); }
  // --xml Slicer format features
  xml() { return this._addCommand('--xml'); }

  // Legacy contract: resolves File[] for exit code 0 or 3 (--version), rejects otherwise.
  async run() {
    if (this.worker === null) throw new Error('Worker not initialized. Did you await the init() method?');
    if (this.worker.dcm2niixBusy) throw new Error('dcm2niix is already running on this worker; await the previous run()');
    this.worker.dcm2niixBusy = true;
    let result;
    try {
      result = await runOnWorker(this.worker, this.fileList, [...this.commands]);
    } finally {
      this.worker.dcm2niixBusy = false;
    }
    const { files, exitCode, log } = result;
    if (exitCode === 0 || exitCode === 3) return files;
    throw new Error(`dcm2niix processing failed with exit code ${exitCode}${tail(log)}`);
  }
}
