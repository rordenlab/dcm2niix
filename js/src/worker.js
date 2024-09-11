// Load the Emscripten-generated JavaScript, which will handle the WASM binary loading.
// The worker is of type "module" so that it can use ES6 module syntax.
// Importantly, the emscripten code must be compiled with: -s EXPORT_ES6=1 -s MODULARIZE=1.
// This allows proper module bundlers to import the worker and wasm properly with code splitting. 
import Module from './dcm2niix.js';

// initialise an instance of the Emscripten Module so that
// it is ready when the worker receives a message.
// We keep a reference to the module so that the worker can reuse it 
// for all subsequent calls without having to reinitialise it (which could be slow due to the WASM loading)
let mod = null
Module().then((initializedMod) => {
  mod = initializedMod
  // Send a ready message once initialization is complete
  // so we can signal to the main thread that the worker is ready.
  // The Niimath.init() method will wait for this message before resolving the promise.
  self.postMessage({ type: 'ready' });
})

// error handler in the worker
self.onerror = (message, error) => {
  self.postMessage({ type: 'error', message: message, error: error ? error.stack : null });
};
// unhandled promise rejection handler in the worker
self.onunhandledrejection = (event) => {
  self.postMessage({ type: 'error', message: event.reason ? event.reason.message : 'Unhandled rejection', error: event.reason ? event.reason.stack : null });
};

// copy the files to the emscripten filesystem
const copyFilesToFS = async (fileList, inDir, outDir) => {
  // create a directory for dcm2niix to use as its input
  mod.FS.mkdir(inDir);

  // create a directory for dcm2niix to use as its output
  mod.FS.mkdir(outDir);

  // an array to hold all the promises for copying files
  const promises = [];
  for (let fileItem of fileList) {
    const file = fileItem.file;
    // Note: Safari strips webkitRelativePath in the worker,
    // so we use the name property of the file object instead.
    const webkitRelativePath = fileItem.webkitRelativePath || file.name;
    const promise = new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = (e) => {
        try {
          const data = new Uint8Array(e.target.result);
          // webkitRelativePath has the file directory and filename separated by '/',
          // such as 'some_dir/some_file.dcm'.
          // We need to replace the '/' with '_' to create a valid name for the WASM filesystem
          // since some_dir does not exist at out mount point. That directory stub, 
          // doesn't provide any useful information to dcm2niix anyway. 
          const fileName = `${webkitRelativePath.split('/').join('_')}`;
          mod.FS.createDataFile(inDir, fileName, data, true, true);
          resolve(); // Resolve the promise when the file is successfully written
        } catch (error) {
          console.error(error);
          reject(error); // Reject the promise if there's an error
        }
      };
      reader.onerror = () => {
        console.error(reader.error);
        reject(reader.error); // Reject the promise if there's an error reading the file
      };
      reader.readAsArrayBuffer(file);
    });

    promises.push(promise);
  }

  // return a promise that resolves when all files are written
  return Promise.all(promises);

}

const typeFromExtension = (fileName) => {
  const ext = fileName.split('.').pop();
  switch (ext) {
    case 'nii':
      return 'application/sla';
    case 'json':
      return 'application/json';
    case 'txt':
      return 'text/plain';
    case 'gz':
      return 'application/gzip';
    case 'bvec':
      return 'text/plain';
    case 'bval':
      return 'text/plain';
    case 'nrrd':
      return 'application/octet-stream';
    default:
      return 'application/octet-stream';
  }
}

const handleMessage = async (e) => {
  try {
    // name the input and output directories that will get created.
    const inDir = '/input';
    const outDir = '/output';
    const fileList = e.data.fileList;
    const args = e.data.cmd;
    // always put ['-o', outDir] at the beginning of the args array.
    // The user does not need to specify the output directory since it 
    // will be a temporary directory that gets created by the worker in the emscripten filesystem.
    args.unshift('-o', outDir);

    if (!fileList || args.length < 1) {
      throw new Error("Expected a flat file list and at least one command");
    }

    if (!Array.isArray(args)) {
      throw new Error("Expected args to be an array");
    }

    if (!mod) {
      throw new Error("WASM module not loaded yet!");
    }


    // copy the files to the emscripten filesystem
    await copyFilesToFS(fileList, inDir, outDir);
   
    // then add the input directory at the end of the args array
    args.push(inDir);
    // call the main function of the WASM module with the args
    const exitCode = mod.callMain(args);

    // read all files from outDir and return them
    const files = mod.FS.readdir(outDir);
    // filter out any file from the files array that starts 
    // with a dot. FS.readdir returns '.' and '..' which is not useful.
    const filteredFiles = files.filter(file => !file.startsWith('.'));
    
    const convertedFiles = [];
    for (let file of filteredFiles) {
      const filePath = outDir + '/' + file;
      // const blob = new Blob([mod.FS.readFile(filePath)], { type: 'application/sla' });
      // make a file Object from the return value of readFile
      const fileData = mod.FS.readFile(filePath);
      const f = new File([fileData], file, { type: typeFromExtension(file) });
      convertedFiles.push(f);
    }

    // send a message back to the main thread with the output file, exit code and output file name
    self.postMessage({ convertedFiles: convertedFiles, exitCode: exitCode });

    // --------- only for version test
    // const exitCode = mod.callMain(args);
    // // send a message back to the main thread with the output file, exit code and output file name
    // // self.postMessage({ blob: outputFile, outName: outName, exitCode: exitCode });
    // self.postMessage({ blob: "blob", exitCode: exitCode });
  } catch (err) {
    // Send error details back to the main thread
    self.postMessage({ type: 'error', message: err.message, error: err.stack });
  }
}

// Handle messages from the main thread
self.addEventListener('message', handleMessage, false);