// Hand-written declarations for @niivue/dcm2niix (all entry points share this API).

/** Output File; `relativePath` is its path in the output folder (differs from `name` when `-f` contains `/`). */
export type OutputFile = File & { readonly relativePath: string };

/** One converted image and its companions, grouped by filename stem. */
export interface Series {
  stem: string;
  image: OutputFile;
  sidecar?: OutputFile;
  /** Parsed BIDS sidecar, when present and valid JSON. */
  json?: Record<string, unknown>;
  bval?: OutputFile;
  bvec?: OutputFile;
}

export interface ConvertResult {
  /** Images grouped with sidecars and gradients. */
  series: Series[];
  /** Every output file, including unpaired ones (e.g. physio). */
  files: OutputFile[];
  /** dcm2niix exit code; nonzero results still carry any partial output. */
  exitCode: number;
  /** dcm2niix console output, one entry per line. */
  log: string[];
}

export interface ConvertOptions {
  /** dcm2niix command-line arguments, e.g. ['-z', 'y', '-ba', 'n']. Output folder and input are set for you. */
  args?: Array<string | number>;
  /** Abort terminates the worker and rejects with the signal's reason (default AbortError). */
  signal?: AbortSignal;
  /** Terminate and reject with a TimeoutError after this many milliseconds. */
  timeoutMs?: number;
  /** Called with each console line. */
  onLog?: (text: string, stream: 'stdout' | 'stderr') => void;
  /** Called with a 0..1 fraction; adds '--progress', 'y' unless args already set --progress. */
  onProgress?: (fraction: number) => void;
  /** Advanced: supply your own Worker (e.g. when a bundler cannot resolve the packaged worker URL). */
  createWorker?: () => Worker;
}

/** Entry of `listSeries()` (dcm2niix -n -2). Identifying fields follow the -ba setting; `-i y` skips are not applied. */
export interface SeriesListing {
  /** Value for `-n` to convert just this series (may be `CRC.N` for split series). */
  CRC: string;
  SeriesNumber: number;
  SeriesDescription: string;
  ProtocolName: string;
  Modality?: string;
  Manufacturer: string;
  ImageType: string;
  Files: number;
  Rows: number;
  Columns: number;
  /** Omitted under -ba y. */
  StudyInstanceUID?: string;
  /** Omitted under -ba y. */
  SeriesInstanceUID?: string;
  /** Only under -ba n. */
  PatientID?: string;
  BidsGuess?: [string, string];
}

/** Input files: a FileList from `<input webkitdirectory>`, or Files from `filesFromDataTransfer()`. */
export type InputFiles = FileList | ArrayLike<File> | File[];

/** Convert in a fresh worker that is terminated afterwards. Rejects only on worker/WASM failure, abort or timeout. */
export function convert(files: InputFiles, options?: ConvertOptions): Promise<ConvertResult>;

/** List series without converting or decoding pixels. Rejects unless dcm2niix exits 0. */
export function listSeries(files: InputFiles, options?: ConvertOptions): Promise<SeriesListing[]>;

/** Group dcm2niix outputs by stem. */
export function groupOutputs(files: OutputFile[]): Promise<Series[]>;

/** Collect files from a drop event, recursing into folders. Call inside the drop handler. */
export function filesFromDataTransfer(items: DataTransferItemList): Promise<File[]>;

/** Alias of `filesFromDataTransfer` (@niivue/nv-ext-dcm2niix name). */
export function traverseDataTransferItems(items: DataTransferItemList): Promise<File[]>;

/** @niivue/nv-ext-dcm2niix compatible: NIfTI outputs only by default; rejects unless dcm2niix succeeds. Other options go to `convert`. */
export function runDcm2niix(files: InputFiles | null | undefined, options?: { niftiOnly?: boolean } & ConvertOptions): Promise<File[]>;

/** dcm2niix exit codes (`ConvertResult.exitCode`); mirrors kEXIT_* in console/nii_dicom.h. */
export const exitCodes: Readonly<{
  ok: 0;
  error: 1;
  noValidFiles: 2;
  version: 3;
  corruptFile: 4;
  inputFolderInvalid: 5;
  outputFolderInvalid: 6;
  outputFolderReadOnly: 7;
  someOkSomeBad: 8;
  renameError: 9;
  incompleteVolumes: 10;
  nominal: 11;
  invalidParam: 12;
}>;

/** Chainable dcm2niix command-line builder (advanced). */
export interface Processor {
  /** Resolves output files for exit code 0 (or 3 for --version); rejects otherwise. */
  run(): Promise<OutputFile[]>;
  version(): Processor;
  compressionLevel(level: number): Processor;
  a(value: string): Processor;
  adjacent(value: string): Processor;
  b(value: string): Processor;
  bids(value: string): Processor;
  ba(value: string): Processor;
  bidsAnonymize(value: string): Processor;
  c(value: string): Processor;
  comment(value: string): Processor;
  d(value: string | number): Processor;
  directorySearchDepth(value: string | number): Processor;
  e(value: string): Processor;
  exportFormat(value: string): Processor;
  f(value: string): Processor;
  filenameformat(value: string): Processor;
  i(value: string): Processor;
  ignoreDerived(value: string): Processor;
  l(value: string): Processor;
  losslessScale(value: string): Processor;
  m(value: string | number): Processor;
  merge2DSlices(value: string | number): Processor;
  n(value: string | number): Processor;
  seriesCRC(value: string | number): Processor;
  p(value: string): Processor;
  philipsPreciseFloat(value: string): Processor;
  q(value: string): Processor;
  searchDirectory(value: string): Processor;
  r(value: string): Processor;
  renameOnly(value: string): Processor;
  s(value: string): Processor;
  singleFileMode(value: string): Processor;
  v(value: string | number): Processor;
  verbose(value: string | number): Processor;
  w(value: string | number): Processor;
  writeBehavior(value: string | number): Processor;
  x(value: string): Processor;
  crop(value: string): Processor;
  z(value: string): Processor;
  gzip(value: string): Processor;
  bigEndian(value: string): Processor;
  ignoreTriggerTimes(): Processor;
  terse(): Processor;
  xml(): Processor;
}

/** Name used by downstream type shims written before declarations shipped. */
export type Dcm2niixProcessor = Processor;

export class Dcm2niix {
  /** Underlying worker; null before init(). Call worker.terminate() when done. */
  worker: Worker | null;
  /** Boot the WASM worker; rejects if it cannot load. */
  init(): Promise<true>;
  conformFileList(files: InputFiles): Array<{ file: File; webkitRelativePath: string }>;
  input(files: InputFiles): Processor;
  inputFromWebkitDirectory(files: InputFiles): Processor;
  inputFromDropItems(files: InputFiles): Processor;
}
