# spec2nii ↔ dcm2niix DICOM-MRS parity plan

**Goal.** For every DICOM-format MRS sample shipped in `/Users/chris/src/spec2nii/tests/spec2nii_test_data/`, produce a `dcm2niix` output that matches spec2nii's NIfTI image data byte-for-byte, with the spec2nii-NIfTI-extension JSON metadata re-expressed as a BIDS-MRS-compliant `.json` sidecar.

---

## Close-out scope (2026-06-07 deadline)

This cycle ships a **scoped MRS converter**, not the full corpus-green plan. The
original "every vendor family green" target is documented below as the
long-term vision; the line below is what is in-scope for this release.

### Shipping this cycle (quick finishers)

These items either are landed, or are small enough to close before the
deadline:

- [x] **Phase 0** — `tools/spec2nii_compare.py` + 31-dataset inventory.
- [x] **Phase 1 Siemens SVS core** — VB/VE/XA20/XA30/anon SVS parse with FID +
  sform + dim parity. 2/19 cleanly PASS today (XA20, XA30); 3 more (VB SVS,
  VE SVS, anon) now sit at `pix✓ + JSON Δ1` after F1 — residual is the
  M4 `RxCoil/ReceiveCoilName` precedence (CSA short-name vs public-tag
  long-name), tracked under "Deferred to Phase 6" below.
- [x] **Phase 2.a Philips classic parse** — `nframes × spec_points` payloads
  accepted; integer-multiple gate.
- [x] **Phase 2.c / 4.4 `_mrsref` support** — Philips classic 2× payload now
  writes paired `<stem>_svs.nii(.gz)` + `<stem>_mrsref.nii(.gz)`; Siemens /
  Philips standalone water-reference acquisitions (series-name `wrsoff` /
  `no_Water_Suppression`) relabeled `_svs → _mrsref` with
  `WaterSuppressed: false` sidecar.
- [x] **2026-06-07 round 1 + round 2 external review absorbed** — all 4 HIGH +
  most MED items resolved or deferred with rationale ([audit_response.md](audit_response.md)).
- [x] **2026-06-07 round 3 external review absorbed** —
  H1 comparator spec-side `_ref` pairing (`tools/spec2nii_compare.py:~318`),
  H2 MRSI negative-evidence gate (CSI `Rows`/`Columns` > 1 now rejects `_svs` labeling at `nii_dicom_batch.cpp:~11815`),
  H3 `fidRef` leak on FID-size and header-byte-count error paths (`nii_dicom_batch.cpp:~11606, ~11769`),
  H4 scoreboard arithmetic (refreshed below — 19+9+3 = 31),
  M1 `VoiPosition` `itemsOK >= 3` gate (`nii_dicom.cpp:~1711`),
  M2 `csaMultiFloat` NUL terminator (`nii_dicom.cpp:~1373`),
  M5 explicit `JSON—` sentinel on converter error in comparator (`spec2nii_compare.py:~390`),
  L3 byte-diff fast-path in comparator (`spec2nii_compare.py:~415`).
  Kept-with-rationale: M3 group-order CSA gate (conformant DICOM is ascending-tag-order; defensive only),
  M7 keep `isMrsRef` bool (avoids polarity hazard from BIDS-entity-string surgery — see refactor agent report).
- [x] **F1 Pixdim convention fix** — swapped `pixdim[1]`↔`pixdim[2]` in
  `saveDcm2NiiMRS` so per-axis voxel sizes align with the sform column
  norms (the sform already applied spec2nii's row1/row2 swap, but pixdim
  reported the unswapped `xyzMM[1..2]` and contradicted the affine). VB
  SVS / VE SVS / anon SVS now `pix✓`; remaining `JSON Δ1` is the
  `RxCoil/ReceiveCoilName` CSA-vs-public precedence (M4, Phase 6).
  `AcquisitionVoxelSize` reordered to match the new pixdim convention.
- [x] **F2 UIH SVS sform (P3.a)** — UIH MRS encodes IOP as
  direction × VoxelSize (rows have magnitude == PixelSpacing), so the
  geometry-validity gate now normalises before testing orthogonality and
  the writer normalises the row vectors before the m00/m01 multiplication.
  Added the spec2nii `half_shift=True` half-voxel translation on the UIH
  manufacturer branch. `uih_svs_press_te144_SVS_801` now PASS (+1 → 3/40
  PASS).
- [x] **F3 Comparator hygiene (M7/M8/M9/M10)** — pair-aware suffix glob via
  BidsGuess lookup landed earlier; explicit `JSON—` sentinel landed in
  round-3 (M5); added a `skip_reason` field on `Dataset`, ran it on the 9
  `philips/spar_dcm_orientation_tests/` (`P2.b Philips orientation
  handedness, deferred to Phase 6`) and Philips HYPER
  `philips_converted_dcm` (spec2nii reference errors in this environment).
  `--list` now flags SKIP rows; `--run-skipped` overrides for ad-hoc
  inspection.
- [x] **F4 Phase 5 docs polish** — README feature matrix now names
  "Siemens VB/VE/XA SVS, Philips classic SVS, UIH SVS" plus the
  `_mrsref` companion / standalone-relabel behaviour (line 35).
  CLAUDE.md MRS section was already current as of this cycle's
  audit-response work.

**Actual end-of-cycle state:** 3/40 PASS, `_mrsref` works, Siemens SVS
covered (1 `JSON Δ1` M4 RxCoil-alias gap on VB/VE/anon), Philips classic +
`_mrsref` covered (P2.b orient handedness blocks every PASS on the 8 active
Philips rows), UIH SVS PRESS PASS. The remaining 27 FAIL + 10 SKIP rows are
tracked below as deferred — they require non-trivial parser additions
(MRSI dispatch, Philips orientation handedness, sLASER Phoenix-protocol
TE summing) that are not deadline-scoped.

### Deferred to Phase 6 (post-release backlog)

These are real work but out of scope for this release. Tracked by
deliverable + why-deferred so the next cycle has a running start.

- **P1.e Siemens sLASER multi-DICOM** (6 datasets blocked) — Phoenix
  Protocol `alTE` summing (spec2nii `dicomfunctions.py:649`) + multi-DICOM
  stack ordering. Both require deep DICOM-internals work; the Phoenix
  protocol parser is a significant addition.
- **P2.b Philips orientation handedness** (5 classic SVS + 9 orientation_tests
  = 14 datasets blocked) — every Philips SVS has matching FID magnitudes
  but inverted signs on every sform column; spec2nii's
  `_process_philips_svs_new` uses a different DICOM→NIfTI pipeline that
  needs to be ported.
- **P2.d Philips Enhanced multi-dynamic** (3 datasets — `svsWSAntCing`,
  `press_mega`, HYPER `converted_dcm`) — Enhanced DICOM per-frame walking
  + `DIM_DYN` / `DIM_EDIT` axis encoding; `saveDcm2NiiMRS` currently asserts
  single-dynamic.
- **P4.1-P4.3 MRSI generalization** (8 datasets — 5 Siemens MRSI + 2 UIH
  MRSI + 1 voi_in_mrsi) — full spatial-dim packing rewrite + per-vendor
  MRSI dispatch. Single biggest cost item in the original plan.
- **P4.5 `_unloc`** — no corpus sample currently exercises it; defer until a
  driver appears.
- **M4 Coil alias precedence (audit follow-up)** — CSA `ReceivingCoil` vs
  public coil-name precedence on the MRS path. JSON-only diff; doesn't
  block any PASS count, but parity gap with spec2nii on multiple datasets.
- **`saveDcm2NiiMRS` extraction refactor (P4.1)** — pre-condition for clean
  MRSI dispatch; ~250 lines of monolith should be split into
  `mrsValidateMembers / mrsBuildHeader / mrsWriteFID / mrsWriteSidecar`
  helpers before MRSI lands.

### What "done" means for this cycle

- All four `F1`-`F4` items landed and committed to `development`.
- Build clean; `dcm_qa` / `dcm_qa_nih` / `dcm_qa_uih` show only the
  pre-existing stale Ref diffs.
- `tools/spec2nii_compare.py --all` reports **3 pass, 27 fail, 10 skipped
  (total 40)** with all deferred datasets tagged by reason in this file.
- Scoreboard table refreshed from a post-fix run.
- This `## Close-out scope` section unchanged except to flip the F1-F4 boxes.

---

**Boundaries.**
- spec2nii = reference. dcm2niix is the C/C++ port for the DICOM-MRS subset spec2nii supports.
- spec2nii emits NIfTI-2 + `dim_info`-style extension header. dcm2niix emits NIfTI-1 + BIDS-MRS sidecar. Image data (FID payload, dim, pixdim, sform) must match within single-precision; sidecar must be lossless re-expression of what spec2nii embeds.
- spec2nii is permissive-licensed (BSD-3-Clause, William Clarke / U. Oxford). Direct line-for-line porting is fine; the existing SVS path (`saveDcm2NiiMRS`) already attributes spec2nii.
- DICOM-MRS only. Twix (.dat), RDA, SPAR/SDAT, GE P-files, jMRUI, Bruker, Varian are explicitly **out of scope** for this plan even though spec2nii handles them.

---

## Decisions (locked 2026-06-06)

- **Q1 Suffixes:** support every BIDS-MRS suffix where spec2nii has a DICOM sample in its corpus. Triage as datasets are inventoried; today that means `_svs` ✓, `_mrsi` (CSI under several vendors), `_mrsref` (Philips no-WS, Siemens water-ref variants), and `_unloc` only if a corpus path actually exercises it.
- **Q2 GE:** skip entirely. spec2nii has no GE DICOM-MRS source data; the GE corpus is P-files only.
- **Q3 Validation tolerance:** byte-identical FID + sform float32-precise — the existing XA60 SVS standard.
- **Q4 Sidecar field set:** today's behaviour — BIDS-MRS required + recommended, plus dcm2niix's existing non-BIDS fields (Siemens CSA, vendor sequence name, etc.).
- **Q5 Anonymisation:** follow the existing `-ba` convention (`-ba y` default strips dates + PII; `-ba o` keeps timestamps; `-ba n` keeps both). Treat the `Siemens/anon/` and `philips/spar_sdat_anon/` samples as equal-priority test cases, matched under whatever `-ba` mode spec2nii's own anon path produces.
- **Q6 Validation data:** the existing `/Users/chris/src/dcm_qa_spec` covers the SVS XA60 round-trip; for the rest, validate live against spec2nii's output on the same source DICOMs (no new tracked test data this phase). Building our own validation corpus stays for later.
- **Q7 Multi-nucleus / 2D-spectral:** scope-creep is fine — if spec2nii can convert it from DICOM, so should we. The single CSA-derived scalar collapse from CLAUDE.md gets fixed when the first corpus dataset needs array-valued fields (HERCULES / HERMES / MEGA-PRESS, etc.).
- **Q8 Commit cadence:** one commit per **vendor family** (Phase 1 ships in one PR per vendor: Siemens, then Philips, then UIH).
- **Q9 Ref refresh:** I'll trust judgement. Working rule:
  - For the in-tree `dcm_qa_spec` SVS Ref, refresh in lockstep with any C-side BIDS-MRS rename (the existing Ref at HEAD is already stale for `ScanningSequence: "SVS"` and `NumberOfTransients: 64`; will refresh as part of Phase 0).
  - For drift checks against spec2nii's live output, the diff helper will carry an ignore list (versioned in `tools/spec2nii_compare.py`) covering `ConversionSoftwareVersion`, `BidsGuess`, `OriginalFile`, and anything that's genuinely informational rather than a parity claim.
  - If we ever hit a case where the field semantics differ in non-trivial ways (e.g. spec2nii names something one way, BIDS-MRS spec another), I'll surface it before changing the C side.
- **Q10 Refactor latitude:** full. `saveDcm2NiiMRS` will be split into shared helpers (validate-stack, build-header, write-FID, write-sidecar) before MRSI lands.

---

## Current state (audit, as of 2026-06-06)

### dcm2niix
- `saveDcm2NiiMRS` ([nii_dicom_batch.cpp:11324](console/nii_dicom_batch.cpp#L11324)) — SVS only. Rejects `mrsAcqType != kMRSAcqNone && != kMRSAcqSingleVoxel`.
- SOP-class detection ([nii_dicom.cpp:~5587](console/nii_dicom.cpp#L5587)): MR Spectroscopy Storage UID `1.2.840.10008.5.1.4.1.1.4.2` → `d.isMRS = true`.
- FID payload offset captured at `(5600,0020)` SpectroscopyData handler ([nii_dicom.cpp:~7691](console/nii_dicom.cpp#L7691)).
- `isValid` admits `1×1×1` MRS at [nii_dicom.cpp:~8424](console/nii_dicom.cpp#L8424).
- BIDS-MRS sidecar fields currently emitted: required (`ResonantNucleus`, `SpectrometerFrequency`, `SpectralWidth`, `EchoTime`); recommended (`NumberOfSpectralPoints`, `AcquisitionVoxelSize`, `NumberOfTransients`, `ScanningSequence`, `DwellTime`); BidsGuess `["mrs", "_svs"]`.
- Validated against `/Users/chris/src/spec2nii/XA60/Ref/series{30,31}.nii.gz` (1-DICOM and 64-DICOM SVS). Image-data byte-identical; sform float32-precise.

### spec2nii DICOM-MRS parsers (the reference implementations to mimic)
- `spec2nii/Siemens/dicomfunctions.py` — Siemens VB / VE / XA / enhanced DICOM CSI / anon
- `spec2nii/Philips/philips_dcm.py` — Philips Classic + Enhanced DICOM
- `spec2nii/uih.py` — UIH SVS + CSI DICOM
- `spec2nii/dcm2niiOrientation/orientationFuncs.py` — affine derivation (already ported for SVS)

### Test corpus by vendor

| Vendor | Path (relative to spec2nii_test_data/) | Files | Variant | spec2nii routes via |
|---|---|---|---|---|
| Siemens | `Siemens/VBData/DICOM/svs_se_*` | 1 dir | SVS | `dicomfunctions.py` |
| Siemens | `Siemens/VBData/DICOM/csi_se_*` | 7 dirs | MRSI (CSI) | `dicomfunctions.py` |
| Siemens | `Siemens/VEData/DICOM/svs_se_*` | 1 dir | SVS | `dicomfunctions.py` |
| Siemens | `Siemens/VEData/DICOM/csi_se_*` | 7 dirs | MRSI (CSI) | `dicomfunctions.py` |
| Siemens | `Siemens/XAData/XA20/DICOM/26516628.dcm` | 1 file | SVS (XA20) | `dicomfunctions.py` |
| Siemens | `Siemens/XAData/XA30/meas_MID00479_*.dcm` | 1 file | SVS (XA30) | `dicomfunctions.py` |
| Siemens | `Siemens/enhanced_dcm_csi/sm_classic` | dir | MRSI Classic | `dicomfunctions.py` |
| Siemens | `Siemens/enhanced_dcm_csi/sm_enhanced` | dir | MRSI Enhanced | `dicomfunctions.py` |
| Siemens | `Siemens/enhanced_dcm_csi/rk_enhanced` | dir | MRSI Enhanced | `dicomfunctions.py` |
| Siemens | `Siemens/anon/anon_dcm.IMA` | 1 file | SVS anon | `dicomfunctions.py` |
| Siemens | `Siemens/special_cases_slaser_dkd/svs_slaser*` | 8 dirs | SVS sLASER | `dicomfunctions.py` |
| Siemens | `Siemens/hyper_isthmus/...` | ? | SVS (special) | `dicomfunctions.py` |
| Siemens | `Siemens/voi_in_mrsi/...` | ? | MRSI + VOI mask | `dicomfunctions.py` |
| Siemens | `Siemens/HERCULES/...` | ? | SVS edit-on/off | `dicomfunctions.py` |
| Siemens | `Siemens/fid/...` | ? | SVS FID | `dicomfunctions.py` |
| Philips | `philips/DICOM/SV_phantom_*` | 7 dirs | Classic SVS | `philips_dcm.py` |
| Philips | `philips/DICOM_enhanced_multi_dynamic/{press_mega,svsWSAntCing_S002}` | 2 dirs | Enhanced multi-dynamic | `philips_dcm.py` |
| Philips | `philips/spar_dcm_orientation_tests/4002-4802 + _cor_*.nii` | 9 dirs | Orientation regression | `philips_dcm.py` (+ SPAR companion) |
| Philips | `philips/hyper/converted_dcm.dcm` | 1 file | SVS HYPER | `philips_dcm.py` |
| UIH | `UIH/mrs_3d/dicom/csi_hise_3d_te144_CSI_1301` | dir | MRSI 3D | `uih.py` |
| UIH | `UIH/mrs_data/dicom/csi_hise_te144_CSI_1201` | dir | MRSI 2D | `uih.py` |
| UIH | `UIH/mrs_data/dicom/svs_press_te144_SVS_801` | dir | SVS | `uih.py` |
| GE | (none — `from_dicom/` ships NIfTI references, no source DICOMs) | 0 | — | (P-file only) |

Roughly: **~26 DICOM-MRS dataset families across 3 vendors**. SVS subset is ~13 datasets; MRSI subset is ~13. Each "dir" is typically 1-128 DICOM files (single-file enhanced, multi-file classic).

---

## Phased deliverable

### Phase 0 — preconditions and gates (week 0, ~½ day)

Acceptance criteria: every subsequent phase starts from a known-clean state.

**P0.1** `dcm_qa_mrs` decision (Q6 above). Submodule or path-only.
**P0.2** Inventory script `tools/spec2nii_inventory.py` (or just markdown table): for every dataset family in the corpus, record:
- absolute path to source DICOM(s)
- spec2nii subcommand + flags used to convert it
- absolute path to spec2nii-produced NIfTI + extension JSON
- expected dcm2niix output path under `-f %H` (or whatever default; see Q9)
- expected BIDS-MRS suffix (`_svs` / `_mrsi` / `_unloc` / `_mrsref`)
- variance flags: anon? multi-nucleus? 2D-spectral? VOI mask?
**P0.3** `tools/spec2nii_compare.py` — diff helper that:
- runs both converters
- diffs FID payload byte-for-byte (after endian normalisation if needed)
- diffs sform under float32 tolerance (or whatever Q3 picks)
- diffs sidecar JSON under a vetted ignore-list (`ConversionSoftwareVersion`, `BidsGuess`, anything else Q9 picks)
- reports a green/red per dataset
**P0.4** Make sure `saveDcm2NiiMRS`'s current SVS path still passes the diff helper against `Siemens/XAData/XA30/` and `Siemens/VEData/DICOM/svs_se_*`. (Sanity check: we're starting from green.)

Deliverable: `tools/spec2nii_inventory.md` + `tools/spec2nii_compare.py` committed; Phase 0 PR closes.

### Phase 1 — Siemens SVS variants beyond XA60 (~1 week)

`saveDcm2NiiMRS` works for XA60 SVS today. Phase 1 widens it to every other Siemens SVS subtype already in the test corpus.

**P1.1** `Siemens/VBData/DICOM/svs_se_C>T15>S10_10_12_1`
- VB-line (NumarisX, pre-XA). Phase convention is `real + 1j·imag` (NOT negated). Already covered by the existing `isXA` gate in `saveDcm2NiiMRS`; verify byte-identical FID.
- Sidecar fields specific to VB: confirm `SpectrometerFrequency` reads from CSA, not the XA-line tag.
- Acceptance: diff helper green; commit message references spec2nii line numbers cited.

**P1.2** `Siemens/VEData/DICOM/svs_se_c>t15>s10_R10_12_1`
- VE-line. Same phase convention as VB. Verify with `R10` (reverse readout direction) sample — affine sign handling is the risk.

**P1.3** `Siemens/XAData/XA20/DICOM/26516628.dcm`
- XA20 single-DICOM SVS. XA-line phase convention (negate imag). Likely already works but confirm.

**P1.4** `Siemens/anon/anon_dcm.IMA`
- Anonymized SVS. Sidecar must match spec2nii's anon output exactly under `-ba o`. Handle the case where `PatientName`/`PatientBirthDate` are stripped but `SpectrometerFrequency` etc. survive.

**P1.5** `Siemens/special_cases_slaser_dkd/*` (8 datasets)
- sLASER sequence. Test corpus has `wrs_off`, `wrs1`, `wrs2`, `wrs_w1pw3`, `wrs_w4` water-suppression variants. Check that `ScanningSequence` and sequence-specific sidecar fields propagate, and the FID still matches.

**P1.6** `Siemens/HERCULES`, `Siemens/hyper_isthmus`, `Siemens/fid`
- HERCULES = edit-on/edit-off MEGA-PRESS variant. Edit-on/off dimension lives in dim[6] (BIDS-MRS `DIM_EDIT`).
- Triggers Q7 (multi-dim sidecar fields). If Q7=(a), this is where we land the array-valued `ResonantNucleus`/`SpectrometerFrequency` fix. If Q7=(b), document the limitation and produce a single-dim approximation.

Deliverable: every Siemens SVS family green in the diff helper.

### Phase 2 progress (2026-06-06 mid-session)

- [x] **P2.a Philips classic SVS / multi-dynamic enhanced now parse.** saveDcm2NiiMRS's `(size_t)d->imageBytes != bytes_per_dicom` strict gate widened to "integer multiple of expected": Philips packs `nframes × spec_points` complex points in `(5600,0020)`, and a frequent classic-SVS variant carries a water-reference FID immediately after the main FID (payload is therefore 2× expected for a 1-frame single-dynamic scan). Read only the first `bytes_per_dicom` (main FID); emit a one-shot warning naming the dropped multiplier. The water-reference companion file (`<stem>_ref.nii.gz` matched by spec2nii) is Phase 2.b work.
- [ ] **P2.b Philips orientation sign convention.** Test corpus survey shows every Philips SVS has matching FID magnitudes but inverted signs on every column of the sform. spec2nii's `_process_philips_svs_new` uses a slightly different DICOM→NIfTI orientation pipeline; needs targeted comparison and fix.
- [ ] **P2.c Philips `<stem>_ref.nii.gz` water-reference companion.** When the (5600,0020) payload is N× expected, the trailing chunks are water-reference / edit-off / dynamic copies. spec2nii emits them as paired NIfTI files; dcm2niix needs an extended writer to do the same.
- [ ] **P2.d Philips classic vs enhanced detection + multi-dynamic dim_5/dim_6 emission.** MEGA-PRESS (press_mega) and HYPER are edit-on/edit-off variants that should land with `dim_5: DIM_DYN, dim_6: DIM_EDIT` per BIDS-MRS spec.
- [ ] **P2.e Philips orientation_tests** — the 9-dataset `philips/spar_dcm_orientation_tests/` family is the torture test for the orientation fix; bring all 9 PASS before declaring Phase 2 done.

### Phase 2 — Philips DICOM-MRS (~1 week)

spec2nii routes both Classic and Enhanced Philips DICOM-MRS through `philips_dcm.py`. We've never exercised Philips DICOM-MRS in dcm2niix.

**P2.1** Classic Philips SVS: `philips/DICOM/SV_phantom_{center,H15mm,R15mm,45deg_AP,45deg_RL}`
- Five orientation cases (centered + 4 rotations) — same as our existing Philips QSM orientation tests. Affine derivation has to handle Philips's column-major orientation tag ordering. Risk: cross-product sign for the slice axis.
- `SV_phantom_center_no_Water_Suppression` is the water-reference (`_mrsref`) variant — needs Q1=(a) or (c).

**P2.2** Enhanced multi-dynamic: `philips/DICOM_enhanced_multi_dynamic/{press_mega,svsWSAntCing_S002}`
- Single Enhanced DICOM file with multiple dynamics → dim[5] > 1 (`DIM_DYN` per BIDS-MRS). `saveDcm2NiiMRS` currently asserts single-dynamic SVS; need to extend.
- `press_mega` is an edit-on/off MEGA variant. Same multi-dim concerns as HERCULES.

**P2.3** Orientation regression: `philips/spar_dcm_orientation_tests/4002–4802`
- 9 datasets with combinatoric rotation parameters. The companion `.nii` reference files in the same folder are spec2nii's own outputs — compare directly. This is the affine torture test.

**P2.4** HYPER: `philips/hyper/converted_dcm.dcm`
- HYPER edit sequence. Same multi-dim story as HERCULES + edit-on/off.

Deliverable: every Philips DICOM-MRS family green.

### Phase 3 — UIH DICOM-MRS (~3-5 days)

spec2nii routes UIH via `uih.py`. UIH MRS is newer than the rest and the corpus is small (3 series).

**P3.1** `UIH/mrs_data/dicom/svs_press_te144_SVS_801` — SVS PRESS.
**P3.2** `UIH/mrs_data/dicom/csi_hise_te144_CSI_1201` — 2D MRSI (CSI). First non-SVS variant in dcm2niix.
**P3.3** `UIH/mrs_3d/dicom/csi_hise_3d_te144_CSI_1301` — 3D MRSI. Same code path as P3.2 + extra spatial dim.

Note: the `gre_scout_*` and `t2_fse_*` folders are anatomical reference scans, not MRS. They're shipped with the corpus so spec2nii can co-register the MRS voxel for visualisation. Out of scope for the converter — they go through the normal dcm2niix image path. Verify they convert clean and don't break.

Deliverable: every UIH MRS family green.

### Phase 4 — MRSI / Unloc / mrsref generalisation (~1-2 weeks)

If Q1 = (a) (all four suffixes), this is the heavy lift.

**P4.1** Refactor `saveDcm2NiiMRS` per Q10. Extract:
- `mrsValidateMembers()` — already inlined; pull out as a helper
- `mrsBuildHeader()` — dim/pixdim/affine construction
- `mrsWriteFID()` — endian-aware FID write
- `mrsWriteSidecar()` — BIDS-MRS sidecar emission

**P4.2** Add `kMRSAcqMRSI` to `mrsAcqType` and wire SOP-class / sequence-name detection. spec2nii's `dicomfunctions.py` is the reference: which DICOM tags distinguish SVS from MRSI in each vendor.

**P4.3** Per-vendor MRSI ports:
- Siemens VB/VE CSI (P1 corpus already lists them)
- Siemens enhanced DICOM CSI (sm_classic / sm_enhanced / rk_enhanced)
- Philips MRSI (any in their DICOM/?)
- UIH MRSI (P3.2 / P3.3)

**P4.4** `_mrsref` water-reference handling. Most Philips test data has a paired `_no_Water_Suppression` companion — emit it with the `_mrsref` suffix and pair with the `_svs` partner via BIDS naming conventions.

**P4.5** `_unloc` (unlocalized) handling. Rare; only adds value if the corpus has samples — TBD which datasets.

Deliverable: all four BIDS-MRS suffixes round-trip green for every applicable corpus family.

### Phase 5 — Hardening + docs (~3 days)

**P5.1** `dcm_qa_mrs` Ref refresh (per Q6/Q9) + `/regressiontest` integration if Q6=(a).
**P5.2** CLAUDE.md MRS section rewrite: replace the "SVS-only" notes with the full variant table, document the per-vendor phase convention table, document `_mrsref` pairing rules, etc.
**P5.3** README user-facing notes: list the supported MRS variants in the feature matrix.
**P5.4** Run the full diff helper across the 26 dataset families; archive the report as a per-release artifact.

---

## Working method

For each dataset:
1. Run spec2nii on the source → record output paths + extension JSON.
2. Read the relevant spec2nii python function (cite line numbers in the commit).
3. Add / extend the dcm2niix C code, preserving the existing prime-directive constraints (no refactor adjacent code, surgical changes).
4. Run `tools/spec2nii_compare.py <dataset>` until green.
5. Update `spec_plan.md` checklist + commit per Q8 cadence.
6. Push to `development`. No master pushes.

For each phase:
- Run in-tree `/regressiontest` (`dcm_qa`, `dcm_qa_nih`, `dcm_qa_uih`) — should remain at standing baseline, no new diffs.
- If a phase touches shared code (the refactor of `saveDcm2NiiMRS`), re-validate the existing XA60 SVS golden output before moving on.

If I get blocked:
- Spec ambiguity → ask before guessing.
- Source data missing for a corpus entry → document in spec_plan.md, mark deferred, continue.
- spec2nii produces something inconsistent with BIDS-MRS spec → document discrepancy, ship dcm2niix matching BIDS spec (not spec2nii), bring to your attention.

---

## Checklist (filled in as we go)

### Phase 0
- [x] Q6 decision: use `/Users/chris/src/dcm_qa_spec` for SVS Ref refresh; live spec2nii output as source-of-truth otherwise (locked answer to Q6)
- [x] `tools/spec2nii_compare.py` written, inventory encoded inline (31 datasets across 3 vendors), alias resolution, parity gates on FID/sform/dim/sidecar
- [x] Phase 0 baseline captured in commit `05815ae`

### Phase 1 Siemens
- [x] **P1.a (7FE1,1010) FID capture for VB/VE/sLASER/anon/voi_in_mrsi classic Siemens MRS DICOMs.** Sniff: when the gzip-XML and CMRR PMU detections fail AND the SOP class is Siemens CSA Non-Image (1.3.12.2.1107.5.9.1) AND lLength is a multiple of 8, treat as the FID payload; set isMRS, clear isRawDataStorage, store imageStart+imageBytes, infer dataPointColumns from lLength.
- [x] **P1.b BIDS-MRS sidecar shape fixes** (apply to all SVS): SpectrometerFrequency as length-1 array at `%.9g` precision (was %g scalar → lost precision); ResonantNucleus as length-1 array (was bare string); dim_5 always emits as `DIM_DYN`; RepetitionTime now emits on the MRS path (fixed by initializing `dti4D_local.frameDuration[0] = -1.0f` in `saveDcm2NiiMRS` so the general-path gate fires); TransmitCoilName parsed from (0018,1251) inside (0018,9049) and emitted on the MRS path; comparator's alias-resolution checks raw keys so spec2nii's `RxCoil`/`TxCoil`/`ExcitationFlipAngle` ↔ dcm2niix's `ReceiveCoilName`/`TransmitCoilName`/`FlipAngle` are parity-positive.
- [x] **P1.c Siemens corpus survey (post P1.a + P1.b)**:
  - XA20 single-DICOM SVS — **PASS** (FID/sform/dim/JSON all green)
  - XA30 single-DICOM SVS — **PASS**
  - VB-line SVS, VE-line SVS, anon, sLASER single-DICOM (wrsoff_13, wrsoff_19) — FID ✓, dim ✓, JSON Δ8-9, **sform ✗ all-zero** (Phase 1.d work)
  - sLASER multi-DICOM (wrs1, wrs2, w1pw3_1/2, w4_1/2) — FID size mismatch (currently captures whole payload per file; needs per-DICOM stack ordering matching spec2nii)
  - voi_in_mrsi, VB/VE 3D CSI, sm_classic, sm_enhanced, rk_enhanced — Phase 4 MRSI work
- [x] **P1.d CSA SeriesHeader parsing**: extract ImageOrientationPatient, VoiPosition, VoiPhaseFoV, VoiReadoutFoV, VoiThickness, RealDwellTime, ImagingFrequency, ImagedNucleus, SpectroscopyAcquisitionDataColumns, RepetitionTime, EchoTime, InversionTime, FlipAngle, NumberOfAverages, TransmittingCoil, ReceivingCoil from (0029,1010)/(0029,1020). New `readCSAforMRS()` at `nii_dicom.cpp:~1610`, gated on `isRawDataStorage || isMRS || mrsAcqType` so non-MRS files are untouched. Called from both `case kCSAImageHeaderInfo` and `case kCSASeriesHeaderInfo`.
- [x] **Final precision pass:** SpectralWidth/DwellTime now derive from `d.dwellTime` (int nanoseconds, Siemens private 0021,1142) when available — preserves full float64 precision vs the float32 CSA RealDwellTime. Sidecar uses `%.17g` for spectral fields and `%.9g` for SpectrometerFrequency to round-trip spec2nii's emission. Comparator gains 5-ULP / 1e-5-relative float tolerance so float32-stage noise no longer flags parity bugs.

Phase 1 final corpus state (historical snapshot at end of Phase 1 — 5/19
"PASS" reflects the count before F1's pixdim swap shipped, which moved
VB/VE/anon SVS from `sform✗ all-zero` to `pix✓ + JSON Δ1` and exposed
the M4 RxCoil-alias gap that now keeps them out of PASS. See current
end-of-cycle scoreboard above for the authoritative state):

| Status | Datasets | Notes |
|---|---|---|
| ✓ **PASS** | XA20 SVS, XA30 SVS | full FID + sform + dim + JSON parity |
| FID/sform/pix ✓, Δ1 JSON | VB-line SVS, VE-line SVS, anon | RxCoil/ReceiveCoilName M4 alias (Phase 6) |
| FID/sform ✓, Δ2 JSON | sLASER WRSoff_13, WRSoff_19 (single-DICOM) | EchoTime + RxCoil — Phase 1.e needs alTE summing |
| FID size ✗ | sLASER WRS1, WRS2, w1pw3_1/2, w4_1/2 (multi-DICOM) | multi-DICOM stack ordering — Phase 1.e |
| Parser reject | voi_in_mrsi, VB/VE 3D CSI, sm_classic, sm_enhanced, rk_enhanced | MRSI — Phase 4 |

- [ ] **P1.e sLASER (multi-echo + multi-DICOM) — DEFERRED to Phase 4 cycle.** Requires Phoenix Protocol `alTE` summing (cf. spec2nii `dicomfunctions.py:649` `parse_buffer(fullcsa['tags']['MrPhoenixProtocol']['items'][0])`) and multi-DICOM stack ordering. Both are non-trivial parsing additions adjacent to the MRSI refactor.
- [x] **P1.f Siemens checkpoint commit + push** (commit `5682953` + this one)

### Phase 2 Philips
- [ ] P2.1 Classic SVS (5 orientation + 1 no-WS)
- [ ] P2.2 Enhanced multi-dynamic (2 datasets)
- [ ] P2.3 spar_dcm orientation regression (9 datasets)
- [ ] P2.4 HYPER

### Phase 3 UIH

Baseline survey (post Phase 1+2, no UIH-specific code yet):
- P3.1 **SVS PRESS**: **FID ✓ dim ✓ JSON ✓** ← almost-parity, only sform missing (UIH writes orientation through a private path the public (0020,0037) doesn't carry)
- P3.2 2D MRSI HISE: parser rejects (Phase 4 work)
- P3.3 3D MRSI HISE: parser rejects (Phase 4 work)

P3.1 is one CSA-equivalent extractor away from PASS — UIH has its own (0065,xxxx) private tag for VoiPosition/VoiThickness/etc. Phase 3.a deliverable.

- [ ] P3.a UIH SVS sform via private-tag orientation extractor
- [ ] P3.b UIH 2D + 3D MRSI parsing (Phase 4 dispatch)

### Current session checkpoint (2026-06-07 F1-F4 close-out)

Cumulative scoreboard against the 40-dataset corpus
(`spec2nii_compare.py --all` reports `3 pass, 27 fail, 10 skipped`;
inventory grew from 31 → 40 with the 9 `spar_dcm_orientation_tests/`
added per F3):

| Vendor | Total | PASS | suf✓ with parity Δ | suf✗ (MRSI in Unknown) | SKIP |
|---|---|---|---|---|---|
| Siemens | 19 | 2 (XA20, XA30) | 11 (3 VB/VE/anon SVS JSON Δ1 RxCoil — M4 Phase 6; 6 sLASER FID/dim/TE; 2 wrsoff `_mrsref` JSON Δ2) | 6 (5 MRSI + voi_in_mrsi — Phase 6) | 0 |
| Philips | 18 | 0 | 8 (5 classic SVS + center no-WS `_mrsref` + svsWSAntCing + press_mega — all orient handedness P2.b) | 0 | 10 (1 HYPER `philips_converted_dcm` spec2nii-ref errors locally + 9 `spar_dcm_orientation_tests/` P2.b) |
| UIH | 3 | 1 (SVS PRESS — F2) | 0 | 2 (CSI 2D + 3D — MRSI Phase 6) | 0 |
| **TOTAL** | **40** | **3** | **19** | **8** | **10** |

Delta vs round-3 scoreboard:
- PASS 2 → 3 (UIH SVS PRESS landed via F2).
- VB/VE/anon SVS moved from `pix✗ + JSON Δ1` to `pix✓ + JSON Δ1`; residual
  Δ is the RxCoil/ReceiveCoilName alias (M4, deferred to Phase 6).
- Corpus grew 31 → 40 by inventorying the 9 `spar_dcm_orientation_tests/`
  (all skipped pending P2.b).
- Philips HYPER `converted_dcm` switched from `FAIL (spec2nii ERR)` to
  `SKIP (spec2nii ERR documented)` so the noise no longer hides real
  parity failures.

`JSON—` sentinel still distinguishes "converter did not produce a
sidecar" from `JSON✓` (parity-clean) and `JSON Δn` (parity diff). The
4 `JSON—` rows are `sm_enhanced`, `rk_enhanced`,
`uih_csi_hise_te144_*`, `uih_csi_hise_3d_te144_*` — all MRSI parser
rejects pending Phase 6.

`_mrsref` deliverable (P2.c + P4.4) — DONE this session:
- Philips classic 2× payload now emits paired `<stem>_svs.nii(.gz)` + `<stem>_mrsref.nii(.gz)` (`philips_SV_phantom_center` writes both files; verified manually).
- Siemens / Philips standalone water-reference acquisitions (series-name `wrsoff` / `no_Water_Suppression`) relabeled `_svs → _mrsref` with `WaterSuppressed: false` sidecar (3 datasets newly `suf✓`).
- New `TDICOMdata.isMrsRef` flag; `WaterSuppressed` BIDS-MRS-required field emitted from the MRS sidecar block as `!isMrsRef`.
- Comparator now reads `BidsGuess[1]` from the dcm2niix JSON sidecar to validate the suffix (was a fixed `"_svs"` sentinel before).

Commits in this session:
- `05815ae` Phase 0 — `tools/spec2nii_compare.py` + 31-dataset inventory
- `5682953` Phase 1 — (7FE1,1010) FID capture + BIDS-MRS sidecar shape
- `a9d3dd1` Phase 1 cont'd — CSA SeriesHeader parsing + precision (commit-time scoreboard: 5/19 Siemens "PASS"; F1 later moved 3 of those to `pix✓ + JSON Δ1` instead)
- `55884e8` Phase 2.a — relaxed Philips FID size check (9/9 Philips parses)
- `cb9fc19` Audit follow-ups (2026-06-07 external review)
- `6dabc01` Scoreboard refresh + Phase 3 UIH baseline
- `00a680b` CLAUDE.md right-size (559 → 168 lines)
- `04676f9` Phase 2.c / 4.4 `_mrsref` companion + F1-F4 close-out (3/40 PASS)
- (this) Audit response — H1 UIH-only normalization, H2/M5 shallow-copy reset, M1 comment fix, M2 csaICEdims NUL, M3 size_t pre-walk, R1 `mrsIsStandaloneWaterRef` helper, R2 `_SKIP_P2B_ORIENTATION` constant + F1 pixdim-mirror anchors, docs sweep (CLAUDE.md F2 + parser gotcha, spec2nii URL fix)

### Phase 4 MRSI / Unloc / mrsref
- [ ] P4.1 `saveDcm2NiiMRS` refactor — **DEFERRED to Phase 6** (precondition for MRSI; ~250-line monolith split)
- [ ] P4.2 `kMRSAcqMRSI` wiring — **DEFERRED to Phase 6**
- [ ] P4.3 Per-vendor MRSI parity — **DEFERRED to Phase 6** (5 Siemens + 2 UIH + 1 voi_in_mrsi)
- [x] P4.4 `_mrsref` pairing — done (Philips 2× companion + Siemens / Philips standalone water-ref relabeling)
- [ ] P4.5 `_unloc` — **DEFERRED to Phase 6** (no corpus driver yet)

### Phase 5 Hardening
- [ ] `dcm_qa_mrs` Ref refresh + `/regressiontest` integration — **DEFERRED to Phase 6** (depends on stable MRS corpus + MRSI landing first)
- [x] CLAUDE.md MRS section updated as part of this cycle's audit-response work
- [x] **F4 (close-out)** README feature matrix updated — line 35 names "Siemens VB/VE/XA SVS, Philips classic SVS, UIH SVS" plus the `_mrsref` companion / standalone-relabel behaviour
- [ ] Final diff report archived — **DEFERRED to Phase 6**

### Phase 6 follow-up (post-release backlog)

See the "Deferred to Phase 6" list under **Close-out scope** at the top of
this file for the running backlog. Phase 6 starts with the
`saveDcm2NiiMRS` extraction refactor (P4.1) as the precondition for MRSI
dispatch.

---

## What I'll do without asking again

Once you answer Q1-Q10:
- I'll start Phase 0 the same session.
- I'll commit per Q8 cadence to `development`.
- I'll only ping you when I hit a blocker that needs your call (spec ambiguity, source-data gap, scope expansion).
- The plan file is the working document — I'll update its checklist + add discoveries / pivots inline as we go.
- I'll re-run `/regressiontest` at each phase boundary and call it out in the commit message.
