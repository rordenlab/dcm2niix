// Type check only: the shipped declarations, without any local `declare module` shim.
import { convert, exitCodes, Dcm2niix, type Dcm2niixProcessor, type ConvertResult } from '@niivue/dcm2niix';
import { listSeries } from '@niivue/dcm2niix/plain';
import { createFrameDecoder, FrameDecodeError, type DecodedFrame, type DecodedFrameSamples, type FrameDecodeOptions } from '@niivue/dcm2niix';

export async function check(files: FileList): Promise<number> {
  const result: ConvertResult = await convert(files, { signal: new AbortController().signal, timeoutMs: 1000, onProgress: (f: number) => void f });
  const builder: Dcm2niixProcessor = new Dcm2niix().input(files).bids('y');
  void builder;
  const listing = await listSeries(files);
  return result.exitCode === exitCodes.ok ? listing.length : -1;
}

export async function frame(bytes: Uint8Array): Promise<number> {
  const decoder = createFrameDecoder({ workers: 1, maxPending: 4 });
  try {
    const f: DecodedFrame = await decoder.decode(bytes, { photometric: null, signal: new AbortController().signal });
    const s: DecodedFrameSamples = await decoder.decode(bytes, { output: 'samples', bitsStored: 12 });
    const opts: FrameDecodeOptions = { photometric: 'RGB' }; // a typed variable matches neither literal overload
    const d = await decoder.decode(bytes, opts);
    return f.rgba.length + s.samples.length + ('rgba' in d ? d.rgba.length : d.samples.length);
  } catch (e) {
    return e instanceof FrameDecodeError && e.status === 'too-large' ? -2 : -1;
  } finally {
    decoder.terminate();
  }
}
