// Worker body shared by both builds. `Module` is the Emscripten factory (EXPORT_ES6, MODULARIZE).
const IN_DIR = '/input';
const OUT_DIR = '/output';

const mimeType = (name) => {
  if (/\.json$/i.test(name)) return 'application/json';
  if (/\.gz$/i.test(name)) return 'application/gzip';
  if (/\.(bval|bvec|txt|tsv)$/i.test(name)) return 'text/plain';
  return 'application/octet-stream'; // no registered NIfTI/NRRD/MGH type
};

// WORKERFS reads through FileReaderSync, which costs milliseconds a call however few bytes it returns, and dcm2niix
// opens each file three times: twice in order for headers, then in slice order for pixels. Whole files are read in
// batches while dcm2niix walks them in order, and the most recent are kept up to CACHE_BYTES.
const MAX_CACHED_FILE = 64 * 1024 * 1024;
const READAHEAD_BYTES = 16 * 1024 * 1024;
const CACHE_BYTES = 256 * 1024 * 1024;

const cachingReads = (FS) => {
  const cache = new Map();
  let cached = 0;
  let nextInOrder = null;
  const contents = (node) => {
    const hit = cache.get(node);
    if (hit) {
      cache.delete(node); // re-insert: Map order is the LRU order
      cache.set(node, hit);
      return hit;
    }
    const batch = [node];
    let size = node.size;
    if (node === nextInOrder)
      for (let n = node.nextFile; n && !cache.has(n) && n.size <= MAX_CACHED_FILE && size + n.size <= READAHEAD_BYTES; n = n.nextFile) {
        batch.push(n);
        size += n.size;
      }
    const data = new Uint8Array(new FileReaderSync().readAsArrayBuffer(new Blob(batch.map((n) => n.contents))));
    let offset = 0;
    for (const n of batch) {
      cache.set(n, data.slice(offset, offset + n.size));
      offset += n.size;
      cached += n.size;
    }
    for (const [n, bytes] of cache) {
      if (cached <= CACHE_BYTES || n === node) break;
      cache.delete(n);
      cached -= bytes.byteLength;
    }
    nextInOrder = batch[batch.length - 1].nextFile;
    return cache.get(node);
  };
  const ops = FS.filesystems.WORKERFS.stream_ops;
  const uncached = ops.read;
  ops.read = (stream, buffer, offset, length, position) => {
    const node = stream.node;
    if (node.size > MAX_CACHED_FILE) return uncached(stream, buffer, offset, length, position);
    if (position >= node.size) return 0;
    const end = Math.min(node.size, position + length);
    buffer.set(contents(node).subarray(position, end), offset);
    return end - position;
  };
  // Links the files in the order they were given, which is the order dcm2niix first reads them.
  return (nodes) => {
    cache.clear();
    cached = 0;
    nextInOrder = null;
    nodes.forEach((node, i) => (node.nextFile = nodes[i + 1] || null));
  };
};

// Keep folder structure (dcm2niix %f uses it) without letting a name escape the input folder.
const safeRelativePath = (path, fallback) => {
  const parts = String(path || '').split('/').filter((p) => p && p !== '.' && p !== '..');
  return parts.length ? parts.join('/') : fallback;
};

export function serve(Module) {
  let mod = null;
  let log = [];
  const emit = (stream) => (text) => {
    log.push(text);
    self.postMessage({ type: 'log', stream, text });
  };
  const fail = (message) => self.postMessage({ type: 'error', message });

  let resetCache = null;
  Module({ print: emit('stdout'), printErr: emit('stderr') }).then(
    (m) => {
      mod = m;
      resetCache = cachingReads(m.FS);
      self.postMessage({ type: 'ready' });
    },
    (err) => fail(`dcm2niix WASM failed to load: ${(err && err.message) || err}`)
  );
  self.onerror = (message, source, line, col, error) => fail(String(message || 'dcm2niix worker error'));
  self.onunhandledrejection = (event) => fail((event.reason && event.reason.message) || 'dcm2niix worker: unhandled rejection');

  const remove = (path) => {
    const FS = mod.FS;
    if (!FS.analyzePath(path).exists) return;
    if (FS.isDir(FS.stat(path).mode)) {
      for (const name of FS.readdir(path)) if (name !== '.' && name !== '..') remove(`${path}/${name}`);
      FS.rmdir(path);
    } else FS.unlink(path);
  };

  const unmountInput = () => {
    const node = mod.FS.analyzePath(IN_DIR);
    if (node.exists && node.object.mount && node.object.mount.mountpoint === IN_DIR) mod.FS.unmount(IN_DIR);
    remove(IN_DIR);
  };

  const stageInput = (fileList) => {
    const FS = mod.FS;
    const names = fileList.map((item, i) => safeRelativePath(item.webkitRelativePath, item.file.name || `file${i}`));
    const dirs = new Set(names.flatMap((n) => n.split('/').slice(0, -1).map((_, k, parts) => parts.slice(0, k + 1).join('/'))));
    const seen = new Set();
    const blobs = names.map((n, i) => {
      let name = n;
      for (let k = i; seen.has(name) || dirs.has(name); k++) name = `${n}_${k}`; // duplicate drops, or a file named like a folder, would collide
      seen.add(name);
      return { name, data: fileList[i].file };
    });
    FS.mkdir(IN_DIR);
    FS.mount(FS.filesystems.WORKERFS, { blobs }, IN_DIR); // reads lazily from the File, no copy; needs -lworkerfs.js
    resetCache(blobs.map((b) => FS.lookupPath(`${IN_DIR}/${b.name}`).node));
  };

  const collectOutput = (dir, rel, out) => {
    const FS = mod.FS;
    for (const name of FS.readdir(dir)) {
      if (name.startsWith('.')) continue;
      const path = `${dir}/${name}`;
      const relPath = rel ? `${rel}/${name}` : name;
      const { node } = FS.lookupPath(path);
      if (FS.isDir(node.mode)) collectOutput(path, relPath, out);
      else {
        const bytes = FS.filesystems.MEMFS.getFileDataAsTypedArray(node); // view, no copy; File copies it
        out.push({ path: relPath, file: new File([bytes], name, { type: mimeType(name) }) });
        FS.unlink(path);
      }
    }
    return out;
  };

  self.addEventListener('message', async (e) => {
    try {
      const { fileList, cmd } = e.data;
      if (!mod) throw new Error('dcm2niix WASM module not loaded yet');
      stageInput(fileList);
      mod.FS.mkdir(OUT_DIR);
      log = [];
      let exitCode = 0;
      try {
        // -d 9: keep finding deep folders (inputs are no longer flattened); -o last so it wins over a user -o
        exitCode = mod.callMain(['-d', '9', ...cmd, '-o', OUT_DIR, IN_DIR]);
      } catch (err) {
        // Emscripten exit() throws ExitStatus even for exit(0)
        if (err && typeof err === 'object' && 'status' in err) exitCode = err.status;
        else throw err;
      }
      const outputs = collectOutput(OUT_DIR, '', []);
      self.postMessage({ type: 'result', convertedFiles: outputs.map((o) => o.file), paths: outputs.map((o) => o.path), exitCode, log });
    } catch (err) {
      fail((err && err.message) || String(err) || 'dcm2niix worker error');
    } finally {
      if (mod) {
        unmountInput();
        remove(OUT_DIR); // outputs are unlinked as collected; this clears folders and leftovers after an error
      }
    }
  });
}
