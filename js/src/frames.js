// createFrameDecoder: a pool of persistent tile-decoding workers (see frame-worker-core.js).
// The entry file supplies the worker factory so bundlers see a literal worker URL.

/** Thrown by decode() when dcm2niix cannot decode a frame; `status` names the reason. */
export class FrameDecodeError extends Error {
  constructor(info) {
    super(`dcm2niix could not decode the frame: ${info.status}`);
    this.name = 'FrameDecodeError';
    this.status = info.status;
    this.info = info;
  }
}

// WSI tiles are well under 1 MB; OpenJPEG buffers the whole input again, and WASM memory never shrinks.
const MAX_INPUT_BYTES = 64 * 1024 * 1024;

const abortError = (signal) => (signal.reason !== undefined ? signal.reason : new DOMException('frame decode aborted', 'AbortError'));

export function createFrameDecoderFactory(defaultCreateWorker) {
  return function createFrameDecoder({ workers = 2, maxPending = 256, createWorker = defaultCreateWorker } = {}) {
    let nextId = 1;
    let closed = false;
    // A job stays here, and on its worker's busy count, until the worker replies, even after
    // its caller aborted: the worker still decodes it, so it still holds capacity.
    const pending = new Map(); // id -> job { resolve, reject, slot, signal, onAbort }
    const pool = [];
    // `ok` is explicit: an abort reason may be any value, including null or 0. Settling twice
    // (abort, then the reply) is harmless: a promise settles once.
    const settle = (job, ok, value) => {
      if (job.signal) job.signal.removeEventListener('abort', job.onAbort);
      if (ok) job.resolve(value);
      else job.reject(value);
    };
    const fail = (slot, error) => {
      slot.dead = error;
      slot.worker.terminate();
      for (const [id, job] of pending)
        if (job.slot === slot) {
          pending.delete(id);
          settle(job, false, error);
        }
    };
    for (let i = 0; i < Math.max(1, workers); i++) {
      const slot = { worker: createWorker(), busy: 0, dead: null };
      slot.worker.onmessage = (e) => {
        const data = e.data;
        if (data.type === 'error') return fail(slot, new Error(data.message || 'dcm2niix frame worker failed to load'));
        const job = pending.get(data.id);
        if (!job) return; // terminated or failed meanwhile
        pending.delete(data.id);
        slot.busy--;
        if (data.rgba) settle(job, true, { width: data.info.width, height: data.info.height, rgba: data.rgba, info: data.info });
        else settle(job, false, new FrameDecodeError(data.info)); // no-op if the caller aborted
      };
      slot.worker.onerror = (e) => {
        if (e.preventDefault) e.preventDefault();
        fail(slot, new Error(e.message || "dcm2niix frame worker failed (Vite dev: set optimizeDeps.exclude: ['@niivue/dcm2niix'], or pass createWorker)"));
      };
      pool.push(slot);
    }

    /**
     * Decode one frame's codestream (item payload only) to RGBA8.
     * `bytes` is copied, so the caller keeps its buffer.
     */
    const decode = (bytes, { photometric = '', signal } = {}) =>
      new Promise((resolve, reject) => {
        if (closed) return reject(new Error('frame decoder terminated'));
        if (signal && signal.aborted) return reject(abortError(signal));
        if (pending.size >= maxPending) return reject(new Error(`frame decoder busy: ${maxPending} decodes pending`));
        let copy; // first: a bad input must fail before it holds any capacity
        try {
          const view = bytes instanceof ArrayBuffer ? new Uint8Array(bytes) : new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
          if (view.byteLength > MAX_INPUT_BYTES) return reject(new FrameDecodeError({ status: 'too-large' }));
          copy = view.slice().buffer;
        } catch (err) {
          return reject(err);
        }
        const live = pool.filter((s) => !s.dead);
        if (!live.length) return reject(pool[0].dead);
        const slot = live.reduce((a, b) => (b.busy < a.busy ? b : a));
        const id = nextId++;
        const job = { resolve, reject, slot, signal, onAbort: null };
        pending.set(id, job);
        slot.busy++;
        if (signal) {
          job.onAbort = () => settle(job, false, abortError(signal)); // capacity is released by the reply
          signal.addEventListener('abort', job.onAbort, { once: true });
        }
        slot.worker.postMessage({ id, bytes: copy, photometric: String(photometric ?? '') }, [copy]); // DICOM parsers give null for a missing tag
      });

    const terminate = () => {
      if (closed) return;
      closed = true;
      const error = new Error('frame decoder terminated');
      for (const job of pending.values()) settle(job, false, error);
      pending.clear();
      for (const slot of pool) slot.worker.terminate();
    };

    return { decode, terminate };
  };
}
