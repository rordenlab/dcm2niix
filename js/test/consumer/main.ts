// Type check only: the shipped declarations, without any local `declare module` shim.
import { convert, exitCodes, Dcm2niix, type Dcm2niixProcessor, type ConvertResult } from '@niivue/dcm2niix';
import { listSeries } from '@niivue/dcm2niix/plain';

export async function check(files: FileList): Promise<number> {
  const result: ConvertResult = await convert(files, { signal: new AbortController().signal, timeoutMs: 1000, onProgress: (f: number) => void f });
  const builder: Dcm2niixProcessor = new Dcm2niix().input(files).bids('y');
  void builder;
  const listing = await listSeries(files);
  return result.exitCode === exitCodes.ok ? listing.length : -1;
}
