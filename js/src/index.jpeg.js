import { createApi } from './core.js';

export { filesFromDataTransfer, traverseDataTransferItems, groupOutputs, exitCodes } from './core.js';

// Literal URL so bundlers emit the worker and its WASM sibling.
export const { Dcm2niix, convert, listSeries, runDcm2niix } = createApi(
  () => new Worker(new URL('./worker.jpeg.js', import.meta.url), { type: 'module' })
);
