# Siemens BIDS Classification (`setBidsSiemens`)

dcm2niix guesses a BIDS datatype + suffix for each Siemens series so downstream tools (`-f %H`, `tools/reproinx.py`) can name files. This "BidsGuess" is a *heuristic* — a hint, not an authority. It is produced by `setBidsSiemens()` in `console/nii_dicom_batch.cpp` and stored on `TDICOMdata.CSA.bidsDataType` + `.bidsEntitySuffix`. The result looks like:

```
<datatype>/<...entities..._suffix>      e.g.  anat/..._acq-tfl_run-5_T1w
```

A `discard`/`derived` datatype routes the series to `derivatives/scanner/` (see `docs/BIDS_REPROIN.md`), not the raw BIDS tree.

## Inputs

| Symbol | Source |
|--------|--------|
| `seqDetails` | CSA `tSequenceFileName` (e.g. `%SiemensSeq%\gre`), else `sequenceName` (0018,0024), else `pulseSequenceName` (0018,9005) |
| `seqName` | `sequenceName` (0018,0024); falls back to `pulseSequenceName` (0018,9005) on XA |
| ImageType | (0008,0008), underscore-joined; tokens like `DERIVED`, `DIFFUSION`, `FMRI`, `_SWI`, `_UNI`, `T1 MAP` |
| `alTI[0]/alTI[1]` | CSA inversion times → `isDualTI` (MP2RAGE) |
| `lContrasts` / `echoNum` | CSA `lContrasts` / DICOM echo number → multi-echo |
| CSA `numDti` (→dwi classification), `accelFactPE` (→`_acq-…p<N>`), `multiBandFactor` (→`_acq-…m<N>`) | acquisition entities |

## Classification cascade (first match wins)

Evaluated top-to-bottom as an `if / else if` chain; the **first** matching row sets `datatype` + `suffix`. Match tokens are substrings of `seqDetails` unless noted.

| # | Match condition | datatype | suffix | Notes |
|---|-----------------|----------|--------|-------|
| 1 | single slice & no volume, or `isLocalizer` | `discard` | `localizer` | |
| 2 | `DERIVED` + `DIFFUSION` in ImageType | `discard` | `derivedDWI` | console FA/ADC/colFA/trace/TENSOR |
| 3 | non-spatial (IOP 0020,0037 absent → `orient[]` all 0) | `discard` | `nonspatial` | e.g. Siemens color-FA |
| 4 | `b1map` | `fmap` | `TB1TFL` | `_acq-famp` if ImageType `FLIP ANGLE MAP`, else `_acq-anat` |
| 5 | `tfl` \| `mp2rage` \| `wip925` | `anat` | `MP2RAGE` if `isDualTI` else `T1w` | ImageType `T1 MAP`→`T1map`; `_UNI`→`UNIT1` (+`_rec-denoise` if "DENOISED IMAGE") |
| 6 | CSA `numDti>0` \| `_diff` \| `resolve` \| `PtkSms…` \| `ep2d_stejskal_386` | `dwi` | `dwi` (or `sbref` if SeriesDescription `_SBRef`) | `_dir-` label added |
| 7 | `fairest` \| `_asl` \| `_pasl` \| `pcasl`/`PCASL` | `perf` | `asl` (or `m0scan` if `_m0`) | bare `AC=PERFUSION` is **not** enough → Unknown |
| 8 | `pulseSequenceName` has `spcR` | `anat` | `T2w` | |
| 9 | `tse_vfl` \| `\space` (SPACE) | `anat` | `FLAIR` if `spcir` (in any of seq/pulse/details) else `T2w` | `\space` backslash-anchored (avoids "spaceship") |
| 10 | `tse` | `anat` | `tir`→`FLAIR`; then **independently** `tse2d`→**`MRWeightingGuess`(SE)** overwrites: `T2`→`T2w` else `PDw` | The two sub-checks are sequential, not exclusive: a `tse2d` that also matched `tir` ends as `T2w`/`PDw` (the `tse2d` block runs last and overwrites `FLAIR`). `_echo` suppressed (PD/T2 pair) |
| 11 | `ep2d_ase` | *(none)* | `oef_ase` | ASE OEF; BIDS suffix not yet standardized |
| 12 | `ep2d_se` | `fmap` | `epi` | `_dir-` label |
| 13 | `gre_field_mapping` | `fmap` | `phasediff` (phase) / `magnitude<echo>` (mag) | echo in suffix; series not added to run |
| 14 | `\trufi` \| `fl3d1_ns` \| `fl2d1` \| `tfl2d1` | `discard` | `localizer` | |
| 15 | `fl3d1r` | `anat` | `angio` | ToF. (The branch source also lists `fl2d1`/`tfl2d1`, but row 14 already catches those → they only reach here via `fl3d1r`.) |
| 16 | `fl3d_vibe` | `anat` (if resolved) | **`MRWeightingGuess`(GRE)**: `T2starw` / `T1w` / else `PDw` | GRE; `_part-`. If `MRWeightingGuess`→Unknown, datatype is left empty here → the post-cascade `isDerived` clobber (override #2 below) makes it `derived` only when the image is derived, else it lands in `Unknown/` |
| 17 | `ep_seg_fid` | `anat` | `T2starw` | `_part-`; SeriesDescription `mIP`→discard/`mIP`, `SWI_Images`→discard/`SWI_Images` |
| 18 | `gre` | `anat` | `T2starw`; `MEGRE` if `echoNum>1` or `lContrasts>1` | `_part-` |
| 19 | `AALScout` \| `haste` | `discard` | `localizer` | |
| 20 | `_bold` \| `_pace` \| ImageType `FMRI` \| `ep2d_fid` | `func` | `bold` (or `sbref` if `_SBRef`) | `_dir-` label; `_pace` anchored (not "Space") |
| 21 | `sequenceName` has `*epse2d` | `fmap` | `epi` | pepolar; `_dir-` label |

### Post-cascade overrides (run after the chain)

1. **SWI override** — if ImageType contains substring `_SWI`: force `anat`/`T2starw`, `_part-`, `isDerived=false`. Fires regardless of which branch matched — covers EPI-based SWI and derived MINIMUM/`SWI_Images` projections the user wants surfaced as anat. (This is a plain `strstr` substring test, not a bounded-token match — a hypothetical `_SWIRL` ImageType token would also match; no such token occurs in practice.)
2. **Derived clobber** — if `isDerived` and datatype is not already `discard`: set datatype to `derived`. (An explicit `discard` is preserved.)

## Entity assembly (suffix order)

Built in canonical BIDS order and appended to `bidsEntitySuffix`:

| Entity | Rule |
|--------|------|
| `_acq-` | `<preAcqStr>` + alphanumerics of `seqName` up to the first digit (leading `*` skipped); then `p<accelFactPE>` if PE accel > 1, `m<multiBandFactor>` if MB > 1 |
| `_rec-` | `recBIDS` when set (e.g. `denoise` for a denoised UNIT1) |
| `_dir-` | when `isDirLabel`: from `phaseEncodingRC` + CSA polarity — COL→`AP`/`PA`, ROW→`RL`/`LR` |
| `_run-` | `_run-<SeriesNumber>` unless `isAddSeriesToRun` is false (e.g. gre_field_mapping) |
| `_echo-` | `_echo-<echoNum>` when `isReportEcho` and multi-echo (suppressed for PD/T2 pairs, fieldmap magnitude) |
| `_inv-` | `_inv-1`/`_inv-2` when `isDualTI` (MP2RAGE); the two inversion times are CSA `alTI[0]`/`alTI[1]` — index is `2` when `d->TI` matches `alTI[1]`, else `1` (`alTI[2]` is ASL, not used here) |
| `_part-` | `_part-phase` / `_part-mag` when `isPart` (GRE/SWI/VIBE, or any phase image) |
| suffix | trailing `_<modality>` (e.g. `_T1w`, `_bold`) |

## Weighting heuristic — `MRWeightingGuess()`

The **shared** classifier (Siemens `tse2d`/`fl3d_vibe`, plus Philips/GE) that teases apart T1w / PDw / T2w / T2starw / FLAIR / STIR. Returns one of `kMRWeighting{Unknown,T1,T2,PD,T2starw,FLAIR,STIR}`.

### Step 1 — DICOM `AcquisitionContrast` (0008,9209) short-circuit

If the tag carries a weighting-class value, trust it outright (vendor-agnostic, authoritative):

| (0008,9209) value | Result |
|-------------------|--------|
| `T1` | T1w |
| `T2` | T2w |
| `PROTON_DENSITY` | PDw |
| `T2_STAR` | T2starw |
| `FLUID_ATTENUATED` | FLAIR |
| `STIR` | STIR |

Values like `DIFFUSION`/`PERFUSION`/`TOF` are **not** weightings — they fall through to the physics below (handled as acquisition classes elsewhere).

### Step 2 — Bottomley + Ernst-angle physics fallback

Runs only when Step 1 does not apply. Guard rails first:

- `TE <= 0` → **Unknown**
- `isVariableFlipAngle` (SPACE/tse_vfl/FLAIR — nominal FA doesn't predict contrast) → **Unknown**. Reserved/dormant: every current `setBidsSiemens` caller passes `false`, and those VFL sequences are already classified by sequence name (rows 9–10) before any physics fallback, so this guard does not fire today.

**Spin-echo path** (`isSpinEcho == true`, TE in seconds):

| Condition | Result |
|-----------|--------|
| `TE ≥ 0.045 s` (45 ms) | T2w |
| else | PDw |

**Gradient-echo path** (needs `TR`, `fieldStrength` B0, `flipAngle` all > 0; else Unknown):

Field-scaled tissue estimates (gray/white matter):

```
T2*_est = 0.050 / B0            (seconds)
T1_est  = 0.8 · B0^0.38         (seconds)   # Bottomley
ErnstAngle = acos(exp(−TR / T1_est))  · 180/π   (degrees)
```

| Condition | Result |
|-----------|--------|
| `TE ≥ 0.5 · T2*_est` | T2starw |
| `flipAngle ≥ 1.3 · ErnstAngle` | T1w |
| `flipAngle ≤ 0.7 · ErnstAngle` | PDw |
| otherwise (mixed) | PDw (default) |

The `T2*` test wins over the flip-angle tests, so a long-TE GRE is T2starw even at a T1-ish flip angle. `1.3×`/`0.7×` are the fl3d_vibe classifier thresholds.

## Caveats

- This is a *guess*: the sequence-name tokens are Siemens marketing/product names observed in the wild; a re-badged or WIP sequence can miss its branch and land in `Unknown/` (recovered later from `ProtocolName` by reproinx).
- The cascade order is **load-bearing** — earlier branches shadow later ones (e.g. `\space` must precede the `_pace`/`bold` branch; `_SWI` is a post-pass override so it can rescue any branch). Do not reorder without understanding the real datasets each row protects.
- Physics thresholds assume brain tissue at the reported `B0`; they are deliberately coarse (structural triage, not quantitative contrast).
