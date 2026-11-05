import { createApi } from './core.js';
import { createFrameDecoderFactory } from './frames.js';

export { filesFromDataTransfer, traverseDataTransferItems, groupOutputs, exitCodes } from './core.js';
export { FrameDecodeError } from './frames.js';

// Literal URL so bundlers emit the worker and its WASM sibling.
export const { Dcm2niix, convert, listSeries, runDcm2niix } = createApi(
  () => new Worker(new URL('./worker.jpeg.js', import.meta.url), { type: 'module' })
);

/** Pool of persistent workers decoding DICOM-WSI tiles (one frame's codestream) to RGBA8. */
export const createFrameDecoder = createFrameDecoderFactory(
  () => new Worker(new URL('./frame-worker.jpeg.js', import.meta.url), { type: 'module' })
);
