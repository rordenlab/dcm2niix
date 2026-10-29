const esbuild = require('esbuild');
const fs = require('fs');

// index.jpeg.js bundles core.js and worker.jpeg.js bundles worker-core.js.
// The Emscripten glue stays external so it keeps resolving its .wasm via import.meta.url.
const common = { bundle: true, format: 'esm', target: ['es2020'], outdir: './dist' };

fs.rmSync('./dist', { recursive: true, force: true }); // no stale files from earlier builds reach the package
Promise.all([
  esbuild.build({ ...common, entryPoints: ['./src/index.jpeg.js'] }),
  esbuild.build({ ...common, entryPoints: ['./src/worker.jpeg.js'], external: ['./dcm2niix.jpeg.js'] }),
])
  .then(() => {
    for (const f of ['dcm2niix.jpeg.js', 'dcm2niix.jpeg.wasm', 'index.d.ts']) fs.copyFileSync(`./src/${f}`, `./dist/${f}`);
    console.log('Build completed!');
  })
  .catch(() => process.exit(1));
