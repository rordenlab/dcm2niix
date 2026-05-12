# ReproIn one-pass naming (`-f H` / `%H`)

dcm2niix's `-f H` (a.k.a. `%H` format specifier) emits filenames that approximate the
[ReproIn](https://github.com/ReproNim/reproin) heuristic shipped with
[heudiconv](https://github.com/nipy/heudiconv). The Python implementation is the
ground truth; dcm2niix re-implements the parts that can be done in a single pass over
DICOM data and leaves cross-series reasoning to a downstream pass.

This document records:
1. What dcm2niix does in one pass, with precedence rules and design choices.
2. Known limitations that **must** be resolved by a second pass (Python helper).
3. Mapping notes that future maintainers will need when extending the parser.

## Command-line usage

```
dcm2niix -f %H [-bi <SUBJECT>] [-bv <SESSION>] [-br <PROJECT|.>] -o <out_dir> <dicom_dir>
```

| Flag | Purpose | Example |
| --- | --- | --- |
| `-f %H` | Activate ReproIn one-pass naming. `%H` is the format specifier; `-f H` (literal `H`) does **not** trigger ReproIn — it produces a literal filename of `H`. | `-f %H` |
| `-bi <id>` | BIDS subject ID (without the `sub-` prefix). Optional; default is derived from `(0010,0020) PatientID` (see "Subject / session defaults"). | `-bi M2022` → `sub-M2022/...` |
| `-bv <visit>` | BIDS session / visit (without the `ses-` prefix). Optional; if absent and no `_ses-<X>` is parsed from ProtocolName, the `ses-` segment is omitted entirely. A `_ses-<X>` token in ProtocolName **wins** over this flag. | `-bv 1222` → `ses-1222/...` |
| `-br <name>` | Override the project subdirectory name appended under `-o`. By default this name comes from `(0008,1030) StudyDescription` (falling back to `PerformedProcedureStepDescription`). Pass `-br .` to suppress the subdirectory entirely so `-o` itself becomes the BIDS root — useful when the caller already created a named dataset directory. | `-br MyStudy`, `-br .` |
| `-o <dir>` | Output root. ReproIn appends a study-derived hierarchy below this directory (override with `-br`). | `-o /data/bids` |

**Worked example.** Given DICOM input where `(0008,1030) StudyDescription = "BrainHealth AgingBrain"` and series 5 has `(0018,1030) ProtocolName = "anat-T1w"`:

```
dcm2niix -f %H -bi M2022 -bv 1222 -o /data/bids /path/to/DICOMs
```

writes:

```
/data/bids/BrainHealth/AgingBrain/sub-M2022/ses-1222/anat/sub-M2022_ses-1222_T1w.{nii,json}
```

**Other relevant flags.** `-z y` (gzip output), `-i y` (skip derived/2D), `-b y|n|o` (BIDS sidecar), `-w 0|1|2` (overwrite policy) all interact with `-f %H` exactly as they do with any other format specifier — see `dcm2niix -h`.

**Defaults when neither `-bi` nor `-bv` is supplied.** Match heudiconv:
- Subject is derived from `(0010,0020) PatientID` via the same `fixup_subjectid` rule heudiconv uses (lowercase; if it matches `sid0*(\d+)$`, reformat as `sid%06d`; otherwise strip `-` and `_`). So a DICOM with `PatientID = "crlab"` becomes `sub-crlab/...`.
- Session is omitted entirely unless a series carries `_ses-<X>` in its ProtocolName. So a single-session study without `_ses-` markers writes `sub-XX/<datatype>/sub-XX_<entities>_<suffix>.nii.gz` — no `ses-` segment in the path or filename.

## Privacy considerations

ReproIn mode preserves more identifying metadata than dcm2niix's defaults
elsewhere, by design — it targets heudiconv parity, which prioritises study
provenance over anonymisation. Specifically:

- **PatientID flows into the BIDS path.** When `-bi` is omitted, the `sub-`
  segment is derived from `(0010,0020) PatientID` (after lowercase + `sid`
  normalisation + `-_` strip). For studies where PatientID is the participant's
  real name or a mapping back to PHI, this is a leak. Pass `-bi <pseudonym>`
  to override.
- **`tools/reproinx.py` runs `dcm2niix` with `-ba n`** so `AcquisitionDateTime`
  survives into every JSON sidecar. Real timestamps end up in
  `_scans.tsv`. This is necessary for the closest-time fmap-matching
  tie-breaker; if you need anonymised timestamps, you have to either run
  `reproinx.py --anonymize` (which suppresses `-ba n` and accepts
  first-compatible fmap matching instead of closest-time) or post-process
  the sidecars with a date-shifter.
- **StudyDescription / PerformedProcedureStepDescription become directory
  names.** They are sanitised against path traversal but otherwise emitted
  verbatim. If a site encodes the PI name or grant number in
  StudyDescription, that is also exposed in the BIDS layout.

For fully anonymised pipelines, prefer the legacy `-f %h` mode or supply
`-bi`/`-bv` explicitly, and run `reproinx.py --anonymize` (or skip the
post-pass entirely).

## ProtocolName grammar

```
[PREFIX:][WIP ]<datatype[-<suffix>]>[_ses-<SESID>][_task-<TASKID>][_acq-<ACQLABEL>][_run-<RUNID>][_dir-<DIR>][<more BIDS>][__<custom>]
```

Datatype must be one of `anat`, `func`, `fmap`, `dwi`, `beh`. The leading `[A-Z]+:`
prefix and `WIP ` prefix are stripped (Philips). Anything after `__` is dropped.
Recognised entities (`ses`, `run`, `task`, `acq`, `rec`, `dir`) are pulled out;
unknown `key-value` tokens flow through into a `bids` leftover string.
Special remaps applied first: `anat_T1w → anat-T1w`, `hardi_64 → dwi_acq-hardi64`,
`AAHead_Scout → anat-scout`, leading `scout → anat-scout`.

## DICOM-tag precedence

| Field | Primary tag | Fallback |
| --- | --- | --- |
| Series spec | `(0018,1030) ProtocolName` | `(0008,103e) SeriesDescription` |
| Study path | `(0008,1030) StudyDescription` | `(0040,0254) PerformedProcedureStepDescription` |

Mirrors the Python `series_spec_fields = ("protocol_name", "series_description")`
and `get_study_description()` which sources `(0008,1030)`.

The study path translates ` ` and `_` into `kPathSeparator`, so
`StudyDescription = "BrainHealth AgingBrain"` becomes `BrainHealth/AgingBrain/`.

## Subject / session defaults

| `-bi <id>` | `-bv <visit>` | `_ses-` in protocol | Output (PatientID = `crlab`) |
| --- | --- | --- | --- |
| omitted | omitted | none | `sub-crlab/...` (no ses-) |
| omitted | omitted | `_ses-pre` | `sub-crlab/ses-pre/...` |
| omitted | `1222` | none | `sub-crlab/ses-1222/...` |
| `M2022` | omitted | none | `sub-M2022/...` (no ses-) |
| `M2022` | `1222` | none | `sub-M2022/ses-1222/...` |
| `M2022` | `1222` | `_ses-pre` | `sub-M2022/ses-pre/...` (spec wins) |

Resolution rules:

**Subject** — `-bi` if supplied; else `(0010,0020) PatientID` after
`fixup_subjectid` (lowercase, `sid0*(\d+)$ → sid%06d`, otherwise strip `-_`).

**Session** — `_ses-<X>` parsed from ProtocolName if present; else `-bv` if
supplied; else omit the `ses-` segment entirely. Two literal markers in
ProtocolName resolve at conversion time:
- `_ses-{date}` (Python-friendly) and `_ses-DATE` (Siemens X60 doesn't allow
  `{}` in protocol names) both expand to `(0008,0020) StudyDate` in `YYYYMMDD`.

## Suffix inference (when ProtocolName omits it)

| datatype | Rule |
| --- | --- |
| `func` | `_pace_` in spec → `pace`; ImageType has `P` → `phase`; ImageType has `M` → `bold`; default `bold` |
| `fmap` | ImageType[2] = `M` + `dir` set → `epi`; `M` no dir → `magnitude` (echo-numbered when multi-echo); `P` → `phasediff`; `DIFFUSION` → `epi` |
| `dwi` | `dwi` |
| `anat` | spec must already provide suffix (e.g. `anat-T1w`) |

`SeriesDescription` ending in `_SBRef` overrides any inferred suffix to `sbref`.

## Multi-echo

- Func dual-echo: `_echo-N` is injected between `_run-` and the suffix.
- GRE fmap (`fmap_acq-ge_run-3` style): magnitude images become `magnitude1` /
  `magnitude2` keyed off `dcm.echoNum`; phase image becomes `phasediff`.

## Derived images

`reproinIsDerived()` returns true for any of:
- `dcm.isDerived` (DICOM `ImageType[0] == DERIVED`)
- SeriesDescription/ProtocolName contains `-scout`, `_ADC`, `_TRACEW`/`_TRACE`, `_FA`
- Parsed datatype-suffix is `anat-scout`

When `opts.isIgnoreDerivedAnd2D` is true, derived data is suppressed by the existing
skip path. Otherwise, derived outputs are routed under
`derivatives/scanner/sub-XX/ses-YY/<datatype>/...` per the heudiconv heuristic's
commented-out branch.

---

## Known one-pass limitations (handled by `tools/reproinx.py`)

The companion script [`tools/reproinx.py`](tools/reproinx.py) wraps dcm2niix
and walks the resulting BIDS tree to address cross-series concerns. It runs
`dcm2niix -f %H -ba n` (so `AcquisitionDateTime` survives into the JSON
sidecar), then per-session:

- Aggregates `_scans.tsv` from each non-fmap JSON's `AcquisitionDateTime`.
- Pairs every non-fmap, non-sbref scan with the fmap group whose
  `ShimSetting` (exact) and NIfTI affine (`np.allclose(rtol=0.05)`) match —
  the same algorithm heudiconv runs via `POPULATE_INTENDED_FOR_OPTS`.
- Writes modern BIDS B0 mapping fields: `B0FieldIdentifier` on every fmap
  JSON in the group, and `B0FieldSource` on each compatible target. (Heudiconv
  emits the legacy `IntendedFor` list instead; we emit the BIDS ≥ 1.7 keys.)

Usage:

```
python3 tools/reproinx.py <indir> [outdir] [subject] [session]
python3 tools/reproinx.py --no-convert <indir> <outdir>   # only re-run post-pass
```

Stdlib-only — no third-party Python dependencies. The NIfTI-1 affine and
shape are read directly from the 348-byte header (works for both `.nii` and
`.nii.gz`); affine compatibility uses a pure-Python `allclose` with
`rtol=0.05`.

The remaining items below are issues `reproinx.py` does **not yet** handle (or
that span beyond its scope). Each is an open concern you may hit when
comparing dcm2niix `-f %H` output against the canonical heudiconv `BIDS/...`
tree.

### 1. `IntendedFor` (legacy)
`reproinx.py` writes the modern `B0FieldIdentifier` / `B0FieldSource` keys.
Pipelines that still consume the legacy `IntendedFor` list (e.g. older
fMRIPrep) will not see them. If needed, a follow-up post-pass could derive
`IntendedFor` from `B0FieldSource` by walking the same maps.

### 2. `_run+` / `_run=` counter semantics
ReproIn allows `_run+` (auto-increment) and `_run=` (reuse last). dcm2niix
treats run identifiers as literal strings. The post-pass can renumber by walking
the tree in acquisition order. Until then, users should write explicit numeric
`_run-NN` in their protocols.

### 3. Canceled-run detection (`__dup0N` suffix)
ReproIn marks duplicate filenames as canceled by adding a `__dup0N` suffix to
older versions. dcm2niix simply overwrites or refuses to overwrite. Post-pass
should rename collisions.

### 4. Motion-corrected `_rec-moco`
Triggered by `is_motion_corrected` in heudiconv; dcm2niix doesn't currently set
that flag. Post-pass can detect from `ImageType` containing `MOCO` and rename
accordingly.

### 5. Study-level `_scans.tsv` and boilerplate
heudiconv emits `sub-XX_scans.tsv`, `participants.tsv`, `dataset_description.json`,
`task-*_bold.json` per task with sensible defaults. dcm2niix only emits a stub
`dataset_description.json` and `task-rest_bold.json` (existing
`createDummyBidsBoilerplate`). Post-pass should:
- Aggregate per-subject `scans.tsv` from JSON sidecars (`AcquisitionDateTime`).
- Produce per-task JSON files for every distinct `_task-X` value seen.
- Write `participants.tsv` from `(0010,0010) PatientName` / `(0010,0040) PatientSex`.

### 6. Session inference from a single localizer
ReproIn allows `_ses-` to be specified in only one series (typically the
localizer) and propagated to every other series in the session. dcm2niix sees
each series in isolation. Workaround: pass `-bv <session>` (`opts.bidsSession`)
or write `_ses-` in every protocol. Post-pass can backfill from the localizer.

### 7. Cross-series `protocols2fix` substitutions
The heuristic supports per-study regex substitutions on
ProtocolName/SeriesDescription. dcm2niix has no equivalent. Post-pass could
load a YAML/JSON of substitutions and rename files on disk.

### 8. Phase encoding direction codes
ReproIn allows `_dir-AP/PA/LR/...` directly in ProtocolName. dcm2niix does not
currently cross-check against `(0018,9089) PhaseEncodingDirection`. Mismatch
silently flows through. Post-pass could verify and warn.

### 9. Physio recordings routed under `derivatives/scanner/`
Siemens XA PhysioLogging and CMRR PMU files arrive as DICOM Raw Data Storage,
and `dcm.isDerived` is true for them. Under `-f H` they consequently land in
`derivatives/scanner/sub-XX/ses-YY/func/<bold-stem>_recording-*_physio.tsv.gz`.
BIDS canonically places `_physio` recordings alongside the corresponding `_bold`
file under `sub-XX/ses-YY/func/`. Post-pass should move physio sidecars next to
their target BOLD or fix the routing here by gating physio-specific writers on
`dcm.isXAPhysio`/`dcm.isCMRRPhysio` rather than `isDerived`.

### 10. Multi-echo phasediff vs phase1/phase2
ReproIn currently emits `phasediff` for any `P` image in a fmap. The BIDS
"Case 2" spec allows `phase1`/`phase2` instead. dcm2niix follows the heuristic's
`phasediff`-only behaviour. Future post-pass could split when two phases exist.

---

## Design choices baked into the C parser

- **Subject and session defaults follow heudiconv**: PatientID-derived
  `sub-` and an omitted `ses-` segment when no source provides one. See the
  Subject / session defaults table.
- **Run zero-padding**: `_run-1` in ProtocolName becomes `run-01` in the output
  filename. heudiconv's `"run-%02d"` formatting.
- **Custom `__suffix` is dropped**: matches `series_spec.split("__", 1)[0]`.
- **Sanitisation**: task/acq values strip `#!@$%^&.,:;_-`.
- **Fall-through**: when ProtocolName **and** SeriesDescription both fail to
  parse, dcm2niix falls back to its legacy `Unknown/<series>_<protocol>`
  filename rather than dropping the data. Conversion never silently loses a
  file.
