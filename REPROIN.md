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
- **`tools/reproinx.py` runs `dcm2niix` with `-ba o`** — strips the
  patient PII block (PatientName/ID/BirthDate/Sex/Age/Size/Weight,
  AccessionNumber, ReferringPhysicianName) but keeps `AcquisitionDateTime`
  so real timestamps survive into every JSON sidecar and into
  `_scans.tsv`. The dates are needed for the closest-time fmap-matching
  tie-breaker. Two privacy options: `reproinx.py --anonymize` upgrades to
  `-ba y` (strips dates entirely; fmap matching then falls back to
  first-compatible, and `_scans.tsv` loses timestamps), or — the
  BIDS-RECOMMENDED middle ground — `reproinx.py --shift-dates` keeps the
  relative timing but de-identifies the absolute date: per subject it shifts
  every `acq_time` / `AcquisitionDateTime` by a whole-day offset so the
  earliest scan lands on 1925-01-01, preserving time-of-day and all
  intra-subject intervals (sessions, runs). The shift happens after fmap
  matching, so pairing is unaffected. (It does not rename timestamp-derived
  `ses-<YYYYMMDDThhmmss>` labels — those still leak the date via the path, so
  the pass warns loudly when any are present; use named sessions to fully
  de-identify.)
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
`dcm2niix -f %H -ba o` (strips patient PII but keeps `AcquisitionDateTime`
in the JSON sidecar), then per-subject and per-session:

- **Session backfill.** Heudiconv lets a single series (typically the
  scout) carry `_ses-<X>` and propagates it across every other series in
  the same `StudyInstanceUID`. dcm2niix's one-pass parser emits `_ses-X`
  only on series that name it explicitly. The post-pass consults the
  C-side `.reproin_provenance.tsv` (which records the original
  ProtocolName/SeriesDescription even when the carrying series was
  dropped by `-i y`), falling back to scanning filenames under the
  subject tree and the parallel `derivatives/scanner/<sub>/...`
  branch. When a single unambiguous label is found it renames and moves
  every non-derivative file under `sub-X/<datatype>/` into
  `sub-X/ses-X/<datatype>/`.
- **`__dup-NN` collision renaming.** dcm2niix's default name-conflict
  mode appends `a`/`b`/`c`/… to colliding series in write order; the
  post-pass reads the provenance TSV and renumbers them as heudiconv-style
  `__dup-NN`, with the lowest `SeriesNumber` owning the unsuffixed base.
- **Per-session `_scans.tsv`** with columns `filename, acq_time,
  operator, randstr` — matches heudiconv's reproin layout. `operator`
  is always `n/a` (PerformingPhysicianName/OperatorsName carry human
  names and are intentionally not threaded through the provenance TSV);
  `randstr` is an 8-hex md5 of the sorted `*UID` fields available in the
  sidecar. CRLF line endings to match Python's `csv.writer`
  `excel-tab` default, which is what heudiconv emits.
- **Per-task `task-<X>[_acq-<Y>]_bold.json`** at the BIDS root for every
  task/acq tuple seen under any subject. When a task has both bare
  `task-X` and `task-X_acq-Y` runs, the post-pass writes `TaskName` into
  each per-series BOLD sidecar and removes only generated root-level
  `task-X_bold.json` stubs, avoiding BIDS validator
  `MULTIPLE_INHERITABLE_FILES` while preserving root files with curated
  extra metadata.
- **Per-task empty `_events.tsv`** placeholders next to each `_bold.nii*`.
- **BIDS root scaffolding**: `CHANGES`, `README` (no `.md`),
  `participants.tsv` (one row per `sub-*`, columns
  `participant_id, age, sex, group` with `age`/`sex` pulled from the
  provenance TSV and `group` defaulting to `control`; rows ordered
  chronologically by StudyDate then alphabetical),
  `participants.json`, `scans.json`, and upgrade of dcm2niix's stub
  `dataset_description.json` to the heudiconv reproin template.
  Written at the BIDS root, which is the common parent of every
  `sub-*` directory — *not* necessarily the user's `-o` directory,
  since dcm2niix may append a `<StudyDescription>` hierarchy below it.
- **fmap pairing.** Every non-fmap scan (including `sbref`) is paired with
  the fmap group whose `ShimSetting` (exact) and NIfTI affine
  (`np.allclose(rtol=0.05)`) match — the same algorithm heudiconv runs
  via `POPULATE_INTENDED_FOR_OPTS`. Modern BIDS B0 mapping fields are
  written: `B0FieldIdentifier` on every fmap JSON in the group, and
  `B0FieldSource` on each compatible target. (Heudiconv emits the legacy
  `IntendedFor` list instead; we emit the BIDS ≥ 1.7 keys.) An `sbref`
  shares its `bold` sibling's readout, so it is forced to the same
  `B0FieldSource` as that sibling rather than re-paired independently —
  a closest-in-time tie between two compatible fmaps can never split the
  pair (the shared choice is taken from whichever sibling has a usable
  `AcquisitionTime`).
- **Unknown-rescue of non-leading-datatype protocols.** Protocols that put
  entities first and the suffix last with no leading `<datatype>` token
  (e.g. `ses-pre_task-bernd_bold`, `acq-space_T2w`) are not canonical
  reproin — heudiconv's `parse_series_spec` returns `{}` when `token[0]`
  isn't a known datatype, so the one-pass parser drops them to `Unknown/`.
  `_rescue_unknown_dir` recovers the reproin entities second-hand from the
  `ProtocolName`: a study-scoped `ses-<X>` (one label per
  `StudyInstanceUID`, conflicts fall back to the `StudyDate`/`StudyTime`
  timestamp session), and a numeric `run-<N>` placed in canonical BIDS
  order. The BidsGuess `_run-<SeriesNumber>` (a raw acquisition counter,
  not a BIDS run index) is stripped; a non-disambiguating collision run
  left after reclassification is dropped unless the protocol states it.
- **Stranded physio rescue.** When a BOLD's ProtocolName fails to parse it
  lands in `Unknown/` and dcm2niix writes its `_recording-*_physio` files
  there too (not under `derivatives/scanner/`), so the normal physio pass
  never sees them. `_rescue_unknown_physio` pairs each by
  `(session, task, run)` to its rescued BOLD (unique match required),
  adopting the bold's full stem with `_echo-N` dropped.
- **Cross-series reclassification** (`_reclassify_session`, mirrors
  reproin-namer; gated by `--min-volumes`/`-N`, default 5). dcm2niix
  classifies each series in isolation; the post-pass uses the whole
  session to resolve intent: a short reverse-PE EPI (`< N` volumes) beside
  a long-EPI sibling becomes a PEPOLAR distortion map `fmap/…_dir-<L>_epi`
  (existing anatomical `dir-AP`/`dir-PA` preserved, else the voxel-axis
  label), and an `anat/…_T2w` co-planar with a session `_bold` becomes
  `_inplaneT2`. A half-sessioned subject (a sessionless ReproIn-parsed
  series beside a single existing `ses-*/`) is consolidated into that
  session.

Usage:

```
python3 tools/reproinx.py <indir> [outdir] [subject] [session]
python3 tools/reproinx.py --no-convert <indir> <outdir>     # skip dcm2niix; re-run post-pass only
python3 tools/reproinx.py --anonymize <indir> <outdir>      # upgrade inner -ba o to -ba y
python3 tools/reproinx.py --strict <indir> <outdir>         # fail-fast on per-session errors
python3 tools/reproinx.py --keep-derivatives <indir> <out>  # retain derivatives/scanner/
python3 tools/reproinx.py --shift-dates <indir> <outdir>    # de-identify dates: per-subject shift to 1925, intervals preserved
python3 tools/reproinx.py -N 5 <indir> <outdir>             # min-volumes threshold for short-EPI->fmap/_epi reclassification
```

`derivatives/scanner/` is the scratch tree dcm2niix `-f %H` writes scouts and
DERIVED-flagged images (FA, ColFA, TENSOR_B0, physio) into. The post-pass
reads it once for session detection, then deletes it by default so the output
matches heudiconv's layout (heudiconv produces no `derivatives/` folder).
Pass `--keep-derivatives` if you want to inspect these scratch files.

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

### 3. Canceled-run detection (`__dup-NN` suffix, handled by reproinx.py)
ReproIn marks duplicate filenames as canceled by adding a `__dup-NN`
suffix; dcm2niix's default name-conflict mode appends `a`/`b`/`c`/… in
write order. `reproinx.py` reads `.reproin_provenance.tsv` and rewrites
the suffixes to `__dup-NN` with the lowest `SeriesNumber` owning the
unsuffixed base. Ordering across the whole study is not matched to
heudiconv's `infotodict` iteration — only within a collision group.

### 4. Motion-corrected `_rec-moco`
Triggered by `is_motion_corrected` in heudiconv; dcm2niix doesn't currently set
that flag. Post-pass can detect from `ImageType` containing `MOCO` and rename
accordingly.

### 5. Study-level `_scans.tsv` and boilerplate (handled by reproinx.py)
heudiconv emits `sub-XX_scans.tsv`, `participants.tsv`,
`dataset_description.json`, `task-*_bold.json` per task with sensible
defaults. dcm2niix emits a stub `dataset_description.json` and a
`task-<X>[_acq-<Y>]_bold.json` matching the entity set of the bold
file (legacy `%h` still falls back to `task-rest_bold.json`); the rest
is filled in by `reproinx.py` (see the post-pass list above). Direct
`dcm2niix -f %H` users without the post-pass will see only the
dcm2niix stubs.

`participants.tsv` is generated with `participant_id, age, sex, group`
(matching heudiconv reproin). `age` and `sex` are pulled from the
`.reproin_provenance.tsv` written by the C side (PatientAge in years,
PatientSex restricted to `M`/`F`/`O`, otherwise `n/a`); `group`
defaults to `control`. `PatientName` is intentionally not propagated —
it carries identifying information and is withheld from the provenance
TSV by design. Hand-edited rows survive re-runs; only new subjects are
appended.

### 6. Session inference from a single localizer (handled by reproinx.py)
ReproIn allows `_ses-` to be specified on a single series (typically the
scout) and propagated to every series in the same `StudyInstanceUID`.
dcm2niix's one-pass parser sees each series in isolation, so `_ses-X` flows
only into the series that name it. `reproinx.py` walks each subject after
conversion (including the parallel `derivatives/scanner/<sub>/...` tree
where scout outputs live) and, when exactly one `_ses-X` label is found,
renames and moves every non-derivative file into a `ses-X/` subtree.
Direct `dcm2niix -f %H` users without the post-pass should pass
`-bv <session>` or annotate every protocol with `_ses-`.

### 7. Cross-series `protocols2fix` substitutions
The heuristic supports per-study regex substitutions on
ProtocolName/SeriesDescription. dcm2niix has no equivalent. Post-pass could
load a YAML/JSON of substitutions and rename files on disk.

### 8. Phase encoding direction codes
ReproIn allows `_dir-AP/PA/LR/...` directly in ProtocolName. dcm2niix does not
currently cross-check against `(0018,9089) PhaseEncodingDirection`. Mismatch
silently flows through. Post-pass could verify and warn. (When `reproinx.py`
promotes a short reverse-PE EPI to `fmap/_epi` it does preserve an existing
anatomical `dir-AP`/`dir-PA` and only synthesises an orientation-agnostic
voxel-axis label when the source carries none — see the reclassification bullet
above — but it still does not validate a stated `dir-` against the sidecar.)

### 9. Physio recordings routed under `derivatives/scanner/` (handled by reproinx.py)
Siemens XA PhysioLogging and CMRR PMU files arrive as DICOM Raw Data Storage,
and `dcm.isDerived` is true for them, so under `-f H` they initially land in
`derivatives/scanner/sub-XX/ses-YY/func/<bold-stem>_recording-*_physio.tsv.gz`.
BIDS canonically places `_physio` recordings alongside the corresponding `_bold`
file under `sub-XX/ses-YY/func/`, and `reproinx.py:_rescue_physio_recordings`
(run in `_post_process`) moves them there. (Physio whose parent BOLD itself
failed to parse lands in `Unknown/` next to that BOLD instead, and is recovered
separately by `_rescue_unknown_physio` — see the Unknown-rescue bullets above.)
Direct `dcm2niix -f %H` users without the post-pass still see physio under
`derivatives/scanner/`.

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
