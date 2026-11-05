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
// One JPEG 2000 frame through the frame worker, plus a typed failure.
window.runFrame = async (bytes) => {
  const decoder = root.createFrameDecoder({ workers: 1 });
  try {
    const f = await decoder.decode(bytes, { photometric: 'YBR_RCT' });
    const sha256 = [...new Uint8Array(await crypto.subtle.digest('SHA-256', f.rgba))].map((b) => b.toString(16).padStart(2, '0')).join('');
    const garbage = await decoder.decode(new Uint8Array([0xff, 0x4f, 0xff, 0x51, 1, 2, 3, 4])).then(() => 'resolved', (e) => `${e.name}:${e.status}`);
    return { sha256, garbage };
  } finally {
    decoder.terminate();
  }
};
window.ready = true;
