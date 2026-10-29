#!/usr/bin/env python3
"""Quick-look PCASL perfusion + CBF from a BIDS ASL NIfTI + JSON sidecar.

A Python port of fun_PCASL_process_dicom.m (LOFT), generalized to read the
acquisition structure (M0 / control / label / per-volume PostLabelingDelay)
from the BIDS sidecar instead of hard-coding it. This is a fast sanity-check,
NOT a replacement for BASIL / oxford_asl: the multi-PLD combination is a naive
per-PLD average that ignores arterial transit time, and the TFL/tgse readout
T1 correction from the MATLAB is omitted (sequence-specific).

Usage:
    python pcasl_process_bids.py <input.nii[.gz]> <output_prefix>

<input> is a BIDS ASL image; its `.json` sidecar (same stem) is required and
supplies M0Type and the per-volume PostLabelingDelay array. An adjacent
`_aslcontext.tsv` is used for volume roles when present; otherwise roles are
derived from PostLabelingDelay (0 -> m0scan, remaining volumes alternate
label/control, label first).

Writes <prefix>_m0.nii.gz, <prefix>_perfusion.nii.gz (4D, one per PLD),
<prefix>_cbf.nii.gz (4D per PLD) and <prefix>_summary.png; prints per-PLD mean
gray-matter CBF.

CBF uses the single-PLD Alsop 2015 model (mL/100g/min):
    CBF = 6000 * lambda * dM * exp(PLD/T1b) / (2 * alpha * T1b * M0 * (1 - exp(-tau/T1b)))
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import nibabel as nib


def _find_sidecar(nii: Path) -> Path:
    stem = nii.name[:-7] if nii.name.endswith(".nii.gz") else nii.stem
    j = nii.with_name(stem + ".json")
    if not j.is_file():
        sys.exit(f"error: required sidecar not found: {j}")
    return j


def _roles(meta: dict, nii: Path, nvol: int) -> tuple[list[str], list[float]]:
    """Return (per-volume role, per-volume PLD). Prefer _aslcontext.tsv for the
    role; PLD comes from the sidecar PostLabelingDelay array."""
    pld = meta.get("PostLabelingDelay")
    if not isinstance(pld, list) or len(pld) != nvol:
        sys.exit(f"error: PostLabelingDelay must be an array of {nvol} values "
                 f"(got {pld!r}); this tool needs the per-volume BIDS array.")
    stem = nii.name[:-7] if nii.name.endswith(".nii.gz") else nii.stem
    base = stem[:-4] if stem.endswith("_asl") else stem
    ctx = nii.with_name(base + "_aslcontext.tsv")
    if ctx.is_file():
        rows = [r.strip() for r in ctx.read_text().splitlines() if r.strip()]
        roles = rows[1:] if rows and rows[0].lower() == "volume_type" else rows
        if len(roles) != nvol:
            sys.exit(f"error: {ctx.name} has {len(roles)} rows, expected {nvol}")
    else:  # derive from PLD: 0 -> m0scan; remaining alternate label/control
        roles, k = [], 0
        for v in pld:
            if v == 0:
                roles.append("m0scan")
            else:
                roles.append("label" if k % 2 == 0 else "control")
                k += 1
    if "m0scan" not in roles:
        sys.exit("error: no m0scan volume found (M0 is required for CBF).")
    return roles, [float(x) for x in pld]


def main() -> None:
    ap = argparse.ArgumentParser(description="Quick-look PCASL perfusion + CBF.")
    ap.add_argument("input", help="BIDS ASL NIfTI (.nii/.nii.gz)")
    ap.add_argument("output", help="output prefix (files <prefix>_*.nii.gz/.png)")
    ap.add_argument("--lambda-", dest="lam", type=float, default=0.9,
                    help="blood-brain partition coefficient (default 0.9)")
    ap.add_argument("--alpha", type=float, default=0.85,
                    help="labeling efficiency (default 0.85 PCASL)")
    ap.add_argument("--t1blood", type=float, default=1.65,
                    help="blood T1 in s (default 1.65 at 3T)")
    args = ap.parse_args()

    nii = Path(args.input)
    if not nii.is_file():
        sys.exit(f"error: input not found: {nii}")
    meta = json.loads(_find_sidecar(nii).read_text())
    img = nib.load(str(nii))
    if img.ndim != 4:
        sys.exit(f"error: expected a 4D ASL series, got shape {img.shape}")
    data = np.asarray(img.dataobj, dtype=np.float64)
    nvol = data.shape[3]
    roles, pld = _roles(meta, nii, nvol)

    tau = float(meta.get("LabelingDuration", 0) or 0)
    if tau <= 0:
        sys.exit("error: sidecar LabelingDuration (tau) missing or <= 0.")
    lam, alpha, t1b = args.lam, args.alpha, args.t1blood

    print(f"volumes: {nvol}  (m0scan={roles.count('m0scan')}, "
          f"control={roles.count('control')}, label={roles.count('label')})")
    print(f"tau (LabelingDuration) = {tau}s   lambda={lam}  alpha={alpha}  "
          f"T1blood={t1b}s   ASLType={meta.get('ArterialSpinLabelingType')}")

    # M0: mean of m0scan volumes (calibration)
    m0_idx = [i for i, r in enumerate(roles) if r == "m0scan"]
    m0 = data[..., m0_idx].mean(axis=3)

    # group control/label by their PLD
    plds = sorted({pld[i] for i, r in enumerate(roles) if r in ("control", "label")})
    perf_stack, cbf_stack = [], []
    # brain mask from M0 (crude): above a fraction of the robust max
    thr = 0.20 * np.percentile(m0, 98)
    brain = m0 > thr
    denom = 2.0 * alpha * t1b * m0 * (1.0 - np.exp(-tau / t1b))
    print(f"\n{'PLD(s)':>7} {'pairs':>6} {'meanGM dM':>10} {'meanGM CBF':>11}")
    for p in plds:
        ci = [i for i, r in enumerate(roles) if r == "control" and pld[i] == p]
        li = [i for i, r in enumerate(roles) if r == "label" and pld[i] == p]
        if not ci or not li:
            continue
        dM = data[..., ci].mean(axis=3) - data[..., li].mean(axis=3)
        with np.errstate(divide="ignore", invalid="ignore"):
            cbf = 6000.0 * lam * dM * np.exp(p / t1b) / denom
        cbf = np.where(brain & (denom > 0), cbf, 0.0)
        cbf = np.clip(cbf, -50, 300)  # sane display bounds for a quick look
        perf_stack.append(dM)
        cbf_stack.append(cbf)
        n_pairs = min(len(ci), len(li))
        print(f"{p:>7.2f} {n_pairs:>6} {dM[brain].mean():>10.2f} "
              f"{cbf[brain].mean():>11.1f}")

    perf4d = np.stack(perf_stack, axis=3)
    cbf4d = np.stack(cbf_stack, axis=3)
    cbf_mean = cbf4d.mean(axis=3)
    print(f"\noverall mean gray-matter CBF (avg over PLDs) = "
          f"{cbf_mean[brain].mean():.1f} mL/100g/min")

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    aff = img.affine
    nib.save(nib.Nifti1Image(m0.astype(np.float32), aff), f"{out}_m0.nii.gz")
    nib.save(nib.Nifti1Image(perf4d.astype(np.float32), aff), f"{out}_perfusion.nii.gz")
    nib.save(nib.Nifti1Image(cbf4d.astype(np.float32), aff), f"{out}_cbf.nii.gz")

    # montage: M0, mean perfusion, mean CBF at a mid axial slice
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    z = m0.shape[2] // 2
    perf_mean = perf4d.mean(axis=3)
    pmax = np.percentile(perf_mean[brain], 95) if brain.any() else 1.0  # robust window
    fig, ax = plt.subplots(1, 3, figsize=(11, 4))
    for a, im, ttl, kw in (
        (ax[0], m0[..., z], "M0", {}),
        (ax[1], perf_mean[..., z], "Perfusion (control-label)", dict(vmin=0, vmax=pmax)),
        (ax[2], cbf_mean[..., z], "CBF (mL/100g/min)", dict(vmin=0, vmax=80)),
    ):
        a.imshow(np.rot90(im), cmap="gray", **kw)
        a.set_title(ttl, fontsize=10)
        a.axis("off")
    fig.tight_layout()
    fig.savefig(f"{out}_summary.png", dpi=110)
    print(f"\nwrote: {out}_m0.nii.gz, {out}_perfusion.nii.gz, "
          f"{out}_cbf.nii.gz, {out}_summary.png")


if __name__ == "__main__":
    main()
