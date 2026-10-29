const esbuild = require('esbuild');
const fs = require('fs');

// Both builds come from one source: index*.js bundle core.js, worker*.js bundle worker-core.js.
// The Emscripten glue stays external so it keeps resolving its .wasm via import.meta.url.
const common = { bundle: true, format: 'esm', target: ['es2020'], outdir: './dist' };

Promise.all([
  esbuild.build({ ...common, entryPoints: ['./src/index.js', './src/index.jpeg.js'] }),
  esbuild.build({ ...common, entryPoints: ['./src/worker.js', './src/worker.jpeg.js'], external: ['./dcm2niix.js', './dcm2niix.jpeg.js'] }),
])
  .then(() => {
    for (const f of ['dcm2niix.js', 'dcm2niix.wasm', 'dcm2niix.jpeg.js', 'dcm2niix.jpeg.wasm', 'index.d.ts']) fs.copyFileSync(`./src/${f}`, `./dist/${f}`);
    console.log('Build completed!');
  })
  .catch(() => process.exit(1));
