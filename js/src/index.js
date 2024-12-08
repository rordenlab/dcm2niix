export class Dcm2niix {
  constructor() {
    this.worker = null;
  }

  init() {
    this.worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
    return new Promise((resolve, reject) => {
      // handle worker ready message.
      // This gets reassigned in the run() method, 
      // but we need to handle the ready message before that.
      // Maybe there is a less hacky way to do this?
      this.worker.onmessage = (event) => {
        if (event.data && event.data.type === 'ready') {
          resolve(true); // Resolve the promise when the worker is ready
        }
      }

      // Handle worker init errors.
      this.worker.onerror = (error) => {
        reject(new Error(`Worker failed to load: ${error.message}`));
      }
    });
  }

  conformFileList(fileObjectOrArray) {
    // prepare files with their relative paths.
      // annoyingly, safari strips the webkitRelativePath property when sending files to the worker.
      // fileList is a FileList object, not an array, so we need to convert it to an array.
      // filesWithRelativePaths is an array of objects with the file and webkitRelativePath properties.
      // Now we can use the webkitRelativePath property in the worker.
      // This is important for dcm2niix to work with nested dicom directories in safari.
      const filesWithRelativePaths = Array.from(fileObjectOrArray).map((file) => ({
        file,
        // need to check for both webkitRelativePath and _webkitRelativePath.
        // _webkitRelativePath is used in case the file was not from a webkitdirectory file input element (e.g. from a drop event).
        // IMPORTANT: it is up to other developers to ensure that the special _webkitRelativePath property is set correctly when using drop events.
        webkitRelativePath: file.webkitRelativePath || file._webkitRelativePath || ''
      }));
      return filesWithRelativePaths
    }

  input(fileListObject) {
    const conformedFileList = this.conformFileList(fileListObject);
    return new Processor({ worker: this.worker, fileList: conformedFileList });
  }

  inputFromWebkitDirectory(fileListObject) {
    const conformedFileList = this.conformFileList(fileListObject);
    return new Processor({ worker: this.worker, fileList: conformedFileList });
  }

  inputFromDropItems(dataTransferItemArray) {
    const conformedFileList = this.conformFileList(dataTransferItemArray);
    return new Processor({ worker: this.worker, fileList: conformedFileList });
  }
}

class Processor {
  constructor({ worker, fileList }) {
    this.worker = worker;
    this.fileList = fileList;
    this.commands = []; // default to verbose output for testing
  }

  _addCommand(cmd, ...args) {
    this.commands.push(cmd, ...args.map(String));
    return this;
  }

  // --version
  version() {
    return this._addCommand('--version');
  }

  // -1..-9 gz compression level (1=fastest..9=smallest, default 6)
  compressionLevel(level) {
    return this._addCommand(`-${level}`);
  }

  // -a adjacent DICOMs (images from same series always in same folder) for faster conversion (n/y, default n)
  a(value) {
    return this._addCommand('-a', value);
  }
  // alias for -a
  adjacent(value) {
    return this.a(value);
  }

  // -b BIDS sidecar (y/n/o [o=only: no NIfTI], default y)
  b(value) {
    return this._addCommand('-b', value);
  }
  // alias for -b
  bids(value) {
    return this.b(value);
  }

  // -ba BIDS anonymize (y/n, default y)
  ba(value) {
    return this._addCommand('-ba', value);
  }
  // alias for -ba
  bidsAnonymize(value) {
    return this.ba(value);
  }

  // -c comment stored as NIfTI aux_file (up to 24 characters)
  c(value) {
    return this._addCommand('-c', value);
  }
  // alias for -c
  comment(value) {
    return this.c(value);
  }

  // -d directory search depth (0..9, default 5)
  // Note: not used in browser/wasm since file list is a flat list
  d(value) {
    return this._addCommand('-d', value);
  }
  // alias for -d
  directorySearchDepth(value) {
    return this.d(value);
  }

  // export as NRRD (y) or MGH (o) or JSON/JNIfTI (j) or BJNIfTI (b) instead of NIfTI (y/n/o/j/b, default n)
  e(value) {
    return this._addCommand('-e', value);
  }
  // alias for -e
  exportFormat(value) {
    return this.e(value);
  }

  // -f : filename (%a=antenna (coil) name, %b=basename, %c=comments, %d=description, 
  // %e=echo number, %f=folder name, %g=accession number, %i=ID of patient, %j=seriesInstanceUID, 
  // %k=studyInstanceUID, %m=manufacturer, %n=name of patient, %o=mediaObjectInstanceUID, 
  // %p=protocol, %r=instance number, %s=series number, %t=time, %u=acquisition number, 
  // %v=vendor, %x=study ID; %z=sequence name; 
  // 
  // default '%f_%p_%t_%s')
  f(value) {
    return this._addCommand('-f', value);
  }
  // alias for -f
  filenameformat(value) {
    return this.f(value);
  }

  // -i : ignore derived, localizer and 2D images (y/n, default n)
  i(value) {
    return this._addCommand('-i', value);
  }
  // alias for -i
  ignoreDerived(value) {
    return this.i(value);
  }

  // -l : losslessly scale 16-bit integers to use dynamic range (y/n/o [yes=scale, no=no, but uint16->int16, o=original], default o)
  l(value) {
    return this._addCommand('-l', value);
  }
  // alias for -l
  losslessScale(value) {
    return this.l(value);
  }

  // -m : merge 2D slices from same series regardless of echo, exposure, etc. (n/y or 0/1/2, default 2) [no, yes, auto]
  m(value) {
    return this._addCommand('-m', value);
  }
  // alias for -m
  merge2DSlices(value) {
    return this.m(value);
  }

  // -n : only convert this series CRC number - can be used up to 16 times (default convert all)
  n(value) {
    return this._addCommand('-n', value);
  }
  // alias for -n
  seriesCRC(value) {
    return this.n(value);
  }

  // -o : output directory (omit to save to input folder)
  // o(value){
  //   return this._addCommand('-o', value);
  // }
  // alias for -o
  // outputDirectory(value){
  //   return this.o(value);
  // }

  // -p : Philips precise float (not display) scaling (y/n, default y)
  p(value) {
    return this._addCommand('-p', value);
  }
  // alias for -p
  philipsPreciseFloat(value) {
    return this.p(value);
  }

  // -q : only search directory for DICOMs (y/l/n, default y) [y=show number of DICOMs found, l=additionally list DICOMs found, n=no]
  q(value) {
    return this._addCommand('-q', value);
  }
  // alias for -q
  searchDirectory(value) {
    return this.q(value);
  }

  // -r : rename instead of convert DICOMs (y/n, default n)
  r(value) {
    return this._addCommand('-r', value);
  }
  // alias for -r
  renameOnly(value) {
    return this.r(value);
  }

  // -s : single file mode, do not convert other images in folder (y/n, default n)
  s(value) {
    return this._addCommand('-s', value);
  }
  // alias for -s
  singleFileMode(value) {
    return this.s(value);
  }

  // -v : verbose (n/y or 0/1/2, default 0) [no, yes, logorrheic]
  v(value) {
    return this._addCommand('-v', value);
  }
  // alias for -v
  verbose(value) {
    return this.v(value);
  }

  // -w : write behavior for name conflicts (0,1,2, default 2: 0=skip duplicates, 1=overwrite, 2=add suffix)
  w(value) {
    return this._addCommand('-w', value);
  }
  // alias for -w
  writeBehavior(value) {
    return this.w(value);
  }

  // -x : crop 3D acquisitions (y/n/i, default n, use 'i'gnore to neither crop nor rotate 3D acquisitions)
  x(value) {
    return this._addCommand('-x', value);
  }
  // alias for -x
  crop(value) {
    return this.x(value);
  }

  // -z : gz compress images (y/o/i/n/3, default n) [y=pigz, o=optimal pigz, i=internal:miniz, n=no, 3=no,3D]
  z(value) {
    return this._addCommand('-z', value);
  }
  // alias for -z
  gzip(value) {
    return this.z(value);
  }

  // --big-endian : byte order (y/n/o, default o) [y=big-end, n=little-end, o=optimal/native]
  bigEndian(value) {
    return this._addCommand('--big-endian', value);
  }

  // --ignore_trigger_times : disregard values in 0018,1060 and 0020,9153
  ignoreTriggerTimes() {
    return this._addCommand('--ignore_trigger_times');
  }

  // --terse : omit filename post-fixes (can cause overwrites)
  terse() {
    return this._addCommand('--terse');
  }

  // --xml : Slicer format features
  xml() {
    return this._addCommand('--xml');
  }

  async run() {
    return new Promise((resolve, reject) => {
      this.worker.onmessage = (e) => {
        if (e.data.type === 'error') {
          reject(new Error(e.data.message));
        } else {
          // get the output file and the exit code from niimath wasm
          const { convertedFiles, exitCode } = e.data;
          // --version gives exit code 3 in dcm2niix CLI and wasm
          if (exitCode === 0 || exitCode === 3) {
            // success
            resolve(convertedFiles);
          } else {
            // error
            reject(new Error(`dcm2niix processing failed with exit code ${exitCode}`));
          }
        }
      };

      const args = [...this.commands];
      if (this.worker === null) {
        reject(new Error('Worker not initialized. Did you await the init() method?'));
      }
      // send files and commands to the worker
      this.worker.postMessage({ fileList: this.fileList, cmd: args });
    });
  }
}