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

/** Why a frame could not be decoded (TFrameInfo.status in console/nii_dicom.h). */
export type FrameStatus =
  | 'ok'
  | 'decode-error'
  | 'unsupported-precision'
  | 'unsupported-components'
  | 'unsupported-subsampling'
  | 'unsupported-photometric'
  | 'unsupported-signed'
  | 'too-large';

/** What the codestream held. `mct` 1: the codec inverted ICT/RCT; `isYCbCr` 1: converted here. */
export interface FrameInfo {
  status: FrameStatus;
  /** Why the worker, not the decoder, refused the frame (e.g. a WASM trap, which also retires the worker). Fields other than `status` may then be absent. */
  message?: string;
  width: number;
  height: number;
  components: number;
  bitsStored: number;
  isSigned: number;
  mct: number;
  isYCbCr: number;
}

export interface DecodedFrame {
  width: number;
  height: number;
  /** Interleaved RGBA, alpha 255. Owned by the caller. */
  rgba: Uint8ClampedArray;
  info: FrameInfo;
}

/** A frame's samples (output 'samples'). Owned by the caller. */
export interface DecodedFrameSamples {
  width: number;
  height: number;
  /**
   * Interleaved, unchanged: Uint8Array when the codestream stores at most 8 bits, else Uint16Array.
   * Monochrome has 1 component, polarity not applied (MONOCHROME1 is the caller's to invert);
   * colour has 3, RGB, with 8-bit YCbCr converted as for RGBA output.
   */
  samples: Uint8Array | Uint16Array;
  components: 1 | 3;
  /** The codestream's precision (may exceed DICOM BitsStored, e.g. DCMTK's lossless JPEG-LS). */
  bitsStored: number;
  info: FrameInfo;
}

export interface FrameDecodeOptions {
  /** DICOM PhotometricInterpretation, e.g. 'YBR_FULL_422'. The codestream decides where they disagree. */
  photometric?: string | null;
  /**
   * DICOM BitsStored (0028,0101). RGBA output maps more than 8 bits to 8 by
   * round(v * 255 / (2^bits - 1)), with bits the smaller of this and the codestream's precision.
   * Default 0: the codestream's precision.
   */
  bitsStored?: number | null;
  /** 'rgba' (default) or 'samples'. */
  output?: 'rgba' | 'samples';
  /** Aborting drops the result; the worker still finishes the frame. */
  signal?: AbortSignal;
}

export interface FrameDecoder {
  /**
   * Decode one encapsulated frame (its item payload, no item header) to RGBA8, or with
   * `output: 'samples'` to its samples. JPEG 2000 or JPEG-LS, 8-16-bit unsigned, 1 or 3
   * components, at most 64 MB of input (else 'too-large'). The input is copied. Rejects with FrameDecodeError when dcm2niix
   * cannot decode the frame, with the signal's reason (by default a DOMException named
   * 'AbortError') when aborted, TypeError for an unknown output, or Error when the pool is busy, failed or terminated.
   * An aborted decode keeps counting toward maxPending until its worker finishes it.
   */
  decode(bytes: Uint8Array | ArrayBuffer, options: FrameDecodeOptions & { output: 'samples' }): Promise<DecodedFrameSamples>;
  decode(bytes: Uint8Array | ArrayBuffer, options?: FrameDecodeOptions & { output?: 'rgba' }): Promise<DecodedFrame>;
  /** Output chosen at run time (a FrameDecodeOptions variable): narrow the result with `'rgba' in frame`. */
  decode(bytes: Uint8Array | ArrayBuffer, options?: FrameDecodeOptions): Promise<DecodedFrame | DecodedFrameSamples>;
  /**
   * Bake an ICC profile into a `size`^3 RGBA8 lookup table (alpha 255, red fastest) mapping
   * the profile's RGB to sRGB with LittleCMS (perceptual intent), for a viewer to apply by
   * trilinear lookup. Resolves null when the profile is not an RGB profile LittleCMS can open, or needs more memory than its budget.
   * `size` defaults to 33 (2..129). The profile is copied.
   */
  iccLut(profile: Uint8Array | ArrayBuffer, options?: { size?: number; signal?: AbortSignal }): Promise<Uint8Array | null>;
  /** Stop every worker and reject pending decodes. */
  terminate(): void;
}

export interface FrameDecoderOptions {
  /** Persistent workers, each with its own WASM instance. Default 2. */
  workers?: number;
  /** Decodes in flight before decode() rejects. Default 256. */
  maxPending?: number;
  /** Custom worker factory, e.g. for a non-Vite bundler or a CDN copy of frame-worker.jpeg.js. */
  createWorker?: () => Worker;
}

/** A pool of persistent workers that decode DICOM-WSI tiles to RGBA8 or samples. */
export function createFrameDecoder(options?: FrameDecoderOptions): FrameDecoder;

export class FrameDecodeError extends Error {
  status: FrameStatus;
  info: FrameInfo;
}
