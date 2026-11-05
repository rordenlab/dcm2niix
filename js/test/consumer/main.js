// A typical app: imports the package by name, so the bundler resolves exports, the worker URL and the WASM.
import * as root from '@niivue/dcm2niix';
import * as jpeg from '@niivue/dcm2niix/jpeg';
import * as plain from '@niivue/dcm2niix/plain';

window.run = async () => {
  const { convert, exitCodes } = root;
  const { series, exitCode } = await convert(document.getElementById('dir').files);
  const version = (await convert([], { args: ['--version'] })).log.join(' ');
  const aliases = [jpeg, plain].every((m) => m.convert === convert);
  return { n: series.length, ok: exitCode === exitCodes.ok, names: series.map((s) => s.image.name), version, aliases };
};
// One JPEG 2000 frame through the frame worker, one JPEG-LS frame's samples, plus a typed failure.
window.runFrame = async (bytes, jls) => {
  const decoder = root.createFrameDecoder({ workers: 1 });
  const hex = async (a) => [...new Uint8Array(await crypto.subtle.digest('SHA-256', a))].map((b) => b.toString(16).padStart(2, '0')).join('');
  try {
    const f = await decoder.decode(bytes, { photometric: 'YBR_RCT' });
    const sha256 = await hex(f.rgba);
    const s = await decoder.decode(jls, { photometric: 'MONOCHROME2', output: 'samples' });
    const samplesSha256 = await hex(s.samples);
    const garbage = await decoder.decode(new Uint8Array([0xff, 0x4f, 0xff, 0x51, 1, 2, 3, 4])).then(() => 'resolved', (e) => `${e.name}:${e.status}`);
    return { sha256, samplesSha256, garbage };
  } finally {
    decoder.terminate();
  }
};
window.ready = true;
