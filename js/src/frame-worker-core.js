// Tile decoding worker: one dcm2niix WASM instance, kept for the worker's life and
// reused for every frame (convert() instead runs main() once per call). Each message
// decodes one encapsulated frame's codestream to RGBA8 with nii_decodeFrameRGBA.

// Index = TFrameInfo.status (kFrame* in console/nii_dicom.h).
const frameStatus = ['ok', 'decode-error', 'unsupported-precision', 'unsupported-components', 'unsupported-subsampling', 'unsupported-photometric', 'unsupported-signed', 'too-large'];

const INFO_FIELDS = ['status', 'width', 'height', 'components', 'bitsStored', 'isSigned', 'mct', 'isYCbCr'];
const PHOTOMETRIC_BYTES = 64;

export function serveFrames(Module) {
  let mod = null;
  let infoPtr = 0;
  let photometricPtr = 0;
  const ready = Module({ print() {}, printErr() {} }).then((m) => {
    mod = m;
    infoPtr = m._malloc(INFO_FIELDS.length * 4);
    photometricPtr = m._malloc(PHOTOMETRIC_BYTES);
  });
  ready.catch((err) => self.postMessage({ type: 'error', message: `dcm2niix WASM failed to load: ${(err && err.message) || err}` }));

  const decode = ({ id, bytes, photometric }) => {
    const input = new Uint8Array(bytes);
    const inPtr = mod._malloc(input.length);
    if (!inPtr) return self.postMessage({ id, info: { status: 'too-large' } }); // heap could not grow
    let outPtr;
    try {
      mod.HEAPU8.set(input, inPtr);
      mod.stringToUTF8(photometric, photometricPtr, PHOTOMETRIC_BYTES);
      outPtr = mod._nii_decodeFrameRGBA(inPtr, input.length, photometricPtr, infoPtr);
    } catch (err) {
      const message = String((err && err.message) || err);
      self.postMessage({ id, info: { status: 'decode-error', message } });
      // A WASM trap may leave this instance's stack and heap inconsistent: retire the worker.
      if (err instanceof WebAssembly.RuntimeError) self.postMessage({ type: 'error', message: `dcm2niix frame worker trapped: ${message}` });
      return;
    } finally {
      mod._free(inPtr);
    }
    // Views are read only now: the decode may have grown (and so replaced) WASM memory.
    const i32 = mod.HEAP32.subarray(infoPtr >> 2, (infoPtr >> 2) + INFO_FIELDS.length);
    const info = Object.fromEntries(INFO_FIELDS.map((k, i) => [k, i32[i]]));
    info.status = frameStatus[info.status];
    if (!outPtr) {
      self.postMessage({ id, info });
      return;
    }
    const rgba = new Uint8ClampedArray(mod.HEAPU8.subarray(outPtr, outPtr + info.width * info.height * 4)); // a copy, then the heap is freed
    mod._free(outPtr);
    self.postMessage({ id, info, rgba }, [rgba.buffer]);
  };

  // A load failure posts 'error' above, which fails this worker's jobs in frames.js.
  self.onmessage = (e) => ready.then(() => decode(e.data));
}
