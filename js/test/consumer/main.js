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
window.ready = true;
