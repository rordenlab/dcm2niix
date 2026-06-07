# spec2nii ↔ dcm2niix DICOM-MRS parity plan

**Goal.** For every DICOM-format MRS sample shipped in `/Users/chris/src/spec2nii/tests/spec2nii_test_data/`, produce a `dcm2niix` output that matches spec2nii's NIfTI image data byte-for-byte, with the spec2nii-NIfTI-extension JSON metadata re-expressed as a BIDS-MRS-compliant `.json` sidecar.

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
- [ ] Q6 decision: `dcm_qa_mrs` submodule or path-only
- [ ] `tools/spec2nii_inventory.md` written
- [ ] `tools/spec2nii_compare.py` written and self-tested
- [ ] Existing XA60 SVS still green on the diff helper

### Phase 1 Siemens SVS
- [ ] P1.1 VBData svs_se_C>T15>S10
- [ ] P1.2 VEData svs_se_c>t15>s10_R10
- [ ] P1.3 XA20 single-DICOM SVS
- [ ] P1.4 anon_dcm.IMA
- [ ] P1.5 sLASER (8 datasets)
- [ ] P1.6 HERCULES / hyper_isthmus / fid

### Phase 2 Philips
- [ ] P2.1 Classic SVS (5 orientation + 1 no-WS)
- [ ] P2.2 Enhanced multi-dynamic (2 datasets)
- [ ] P2.3 spar_dcm orientation regression (9 datasets)
- [ ] P2.4 HYPER

### Phase 3 UIH
- [ ] P3.1 SVS PRESS
- [ ] P3.2 2D MRSI
- [ ] P3.3 3D MRSI

### Phase 4 MRSI / Unloc / mrsref
- [ ] P4.1 `saveDcm2NiiMRS` refactor
- [ ] P4.2 `kMRSAcqMRSI` wiring
- [ ] P4.3 Per-vendor MRSI parity
- [ ] P4.4 `_mrsref` pairing
- [ ] P4.5 `_unloc` (if applicable)

### Phase 5 Hardening
- [ ] `dcm_qa_mrs` Ref refresh + `/regressiontest` integration
- [ ] CLAUDE.md rewrite
- [ ] README feature matrix update
- [ ] Final diff report archived

---

## What I'll do without asking again

Once you answer Q1-Q10:
- I'll start Phase 0 the same session.
- I'll commit per Q8 cadence to `development`.
- I'll only ping you when I hit a blocker that needs your call (spec ambiguity, source-data gap, scope expansion).
- The plan file is the working document — I'll update its checklist + add discoveries / pivots inline as we go.
- I'll re-run `/regressiontest` at each phase boundary and call it out in the commit message.
