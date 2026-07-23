#!/usr/bin/env python3
"""Self-check for reproinx.py BIDS-validity fixes:
  1. sbref is a valid fmap target (_is_target_for_fmaps).
  2. TaskName is backfilled into sbref sidecars (_ensure_sbref_tasknames).
  3. R1 reclassification does NOT promote a multi-echo short EPI to pepolar
     `_epi` (BIDS pepolar forbids the `echo` entity), but still promotes a
     single-echo short EPI.
  4. An sbref mirrors its bold sibling's B0FieldSource even when two
     shim-compatible fmaps bracket the pair in acquisition time.
  5. When the bold lacks a usable AcquisitionTime the pair adopts the timed
     sbref's choice; and when neither is timed an incompatible sbref does not
     erase the bold's valid B0FieldSource.
  6. --shift-dates anchors each subject's earliest scan to 1925 across sidecars
     + scans.tsv, preserving time-of-day/intervals, removes provenance (.tsv +
     .bak), and shifts retained derivatives sidecars too.
  7. --shift-dates warns when a timestamp-derived ses-<...> dir would leak the
     date via the path — including the zero-offset re-run case.
  8. --shift-dates resolves the acq_time column from the header (not assumed
     col 2), discovers/shifts dates that live only in scans.tsv or in a
     sidecar's AcquisitionDate, and fails closed (raises) on an unreadable
     sidecar.
  9. Multi-echo BOLD+phase families resolve to _part-<mag|phase>_<bold|sbref>
     (BidsGuess/ImageType precedence, ambiguous or unclassifiable-sibling
     families skipped whole), phase gets Units, _events.tsv is one-per-run
     (echo/part stripped), and the resolver -> __dup recovery leaves no
     base-less or temporary duplicate family after failure.
This list is representative, not exhaustive; see the test functions below.
Run: python3 test_reproinx_sbref.py   (no pytest, no DICOM needed)."""
import gzip, json, struct, tempfile
from pathlib import Path
import reproinx


def _read(path: Path) -> dict:
    """Load a JSON sidecar, closing the handle (no interpreter-shutdown leak)."""
    with open(path) as f:
        return json.load(f)


def _write_nii_gz(path: Path, nvols: int, with_sform: bool = False) -> None:
    """Minimal 348-byte NIfTI-1 header. Only magic + dim are read by most paths;
    with_sform also sets a fixed sform affine so _imaging_volume matches."""
    hdr = bytearray(348)
    struct.pack_into("<i", hdr, 0, 348)
    struct.pack_into("<8h", hdr, 40, 4, 64, 64, 30, nvols, 1, 1, 1)
    if with_sform:
        struct.pack_into("<h", hdr, 254, 1)  # sform_code = 1
        struct.pack_into("<12f", hdr, 280,
                         2, 0, 0, 10, 0, 2, 0, 20, 0, 0, 2, 30)  # srow_x/y/z
    with gzip.open(str(path), "wb") as f:
        f.write(bytes(hdr))


def _mk_series(func: Path, stem: str, nvols: int) -> None:
    _write_nii_gz(func / f"{stem}.nii.gz", nvols)
    (func / f"{stem}.json").write_text(json.dumps({"PhaseEncodingDirection": "j-"}))


# Shared shim — makes every series mutually fmap-compatible in the bracket test.
_SHIM = [1, 2, 3, 4, 5, 6, 7, 8]


def _mk_compat(d: Path, stem: str, acqtime: str, nvols: int = 1) -> None:
    """A series carrying the fixed sform + shim + an AcquisitionTime, so the
    fmap matcher treats all such series as mutually compatible."""
    _write_nii_gz(d / f"{stem}.nii.gz", nvols, with_sform=True)
    (d / f"{stem}.json").write_text(json.dumps(
        {"ShimSetting": _SHIM, "AcquisitionTime": acqtime,
         "PhaseEncodingDirection": "j-"}))


def test_sbref_is_fmap_target():
    # Fix 1: sbref must now pair (was excluded before).
    assert reproinx._is_target_for_fmaps(Path("sub-1_task-x_sbref.json")) is True
    assert reproinx._is_target_for_fmaps(Path("sub-1_task-x_bold.json")) is True
    # fmap files are never their own targets.
    assert reproinx._is_target_for_fmaps(Path("fmap/sub-1_phasediff.json")) is False
    # RF-off noise images nothing, so it must not be distortion-corrected or land
    # in a fieldmap's IntendedFor.
    assert reproinx._is_target_for_fmaps(Path("sub-1_task-x_noRF.json")) is False


def test_sbref_taskname_backfill():
    # Fix 2: TaskName backfilled from the task- entity, idempotent, derivatives skipped.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        func = root / "sub-1" / "func"; func.mkdir(parents=True)
        missing = func / "sub-1_task-reward_sbref.json"; missing.write_text("{}")
        preset = func / "sub-1_task-stop_sbref.json"
        preset.write_text(json.dumps({"TaskName": "Stop task"}))
        deriv = root / "derivatives" / "scanner" / "sub-1_task-z_sbref.json"
        deriv.parent.mkdir(parents=True); deriv.write_text("{}")
        notask = func / "sub-1_acq-y_sbref.json"; notask.write_text("{}")  # no task- entity

        n = reproinx._ensure_sbref_tasknames(root)

        assert json.loads(missing.read_text())["TaskName"] == "reward"
        assert json.loads(preset.read_text())["TaskName"] == "Stop task"  # not overwritten
        assert "TaskName" not in json.loads(deriv.read_text())           # derivatives skipped
        assert "TaskName" not in json.loads(notask.read_text())          # no task- entity
        assert n == 1  # only the one genuinely-missing func sbref counted


def test_reclassify_skips_multiecho():
    # Fix 3: a multi-echo short EPI must NOT become a pepolar _epi (no echo
    # entity allowed); a single-echo short EPI still must.
    with tempfile.TemporaryDirectory() as d:
        ses = Path(d) / "sub-x" / "ses-1"
        func = ses / "func"; func.mkdir(parents=True)
        _mk_series(func, "sub-x_ses-1_task-a_bold", 10)               # long sibling
        _mk_series(func, "sub-x_ses-1_task-b_acq-de_echo-1_bold", 4)  # multi-echo short
        _mk_series(func, "sub-x_ses-1_task-c_bold", 4)                # single-echo short

        moved = reproinx._reclassify_session(ses, min_volumes=5)

        # multi-echo stays in func/ as bold
        assert (func / "sub-x_ses-1_task-b_acq-de_echo-1_bold.nii.gz").is_file()
        assert not list((ses / "fmap").glob("*echo*")) if (ses / "fmap").is_dir() else True
        # single-echo short promoted out of func/ into fmap/_epi
        assert not (func / "sub-x_ses-1_task-c_bold.nii.gz").is_file()
        assert list((ses / "fmap").glob("sub-x_ses-1_dir-*_epi.nii.gz"))
        # long sibling untouched
        assert (func / "sub-x_ses-1_task-a_bold.nii.gz").is_file()
        assert moved == 1


def test_sbref_mirrors_bold_b0field():
    # Fix 4: two shim-compatible fmaps bracket a bold (t=10:01) + sbref (t=10:09)
    # in time. Independent closest-time selection would give bold->A, sbref->B;
    # the sibling-mirror forces sbref to adopt bold's source (A).
    with tempfile.TemporaryDirectory() as d:
        ses = Path(d) / "sub-x" / "ses-1"
        fmap = ses / "fmap"; func = ses / "func"
        fmap.mkdir(parents=True); func.mkdir(parents=True)
        _mk_compat(fmap, "sub-x_ses-1_acq-A_dir-AP_epi", "10:00:00")
        _mk_compat(fmap, "sub-x_ses-1_acq-B_dir-AP_epi", "10:10:00")
        _mk_compat(func, "sub-x_ses-1_task-t_bold", "10:01:00", nvols=20)
        _mk_compat(func, "sub-x_ses-1_task-t_sbref", "10:09:00", nvols=1)

        reproinx._populate_b0_fields(ses)

        bold_src = _read(func / "sub-x_ses-1_task-t_bold.json").get("B0FieldSource")
        sbref_src = _read(func / "sub-x_ses-1_task-t_sbref.json").get("B0FieldSource")
        assert bold_src.startswith("acq-A"), bold_src     # closest in time -> A
        assert sbref_src == bold_src, (sbref_src, bold_src)  # mirrors sibling, not B
        # Group B was never selected -> no identifier written on it.
        bid = _read(fmap / "sub-x_ses-1_acq-B_dir-AP_epi.json")
        assert "B0FieldIdentifier" not in bid


def test_sbref_mirror_prefers_timed_sibling():
    # Fix 5: when the bold sibling lacks a usable AcquisitionTime, the shared
    # selection must come from the timed sbref (its closest-time choice), and the
    # bold must adopt it too — not the bold's "first compatible" fallback.
    with tempfile.TemporaryDirectory() as d:
        ses = Path(d) / "sub-x" / "ses-1"
        fmap = ses / "fmap"; func = ses / "func"
        fmap.mkdir(parents=True); func.mkdir(parents=True)
        _mk_compat(fmap, "sub-x_ses-1_acq-A_dir-AP_epi", "10:00:00")
        _mk_compat(fmap, "sub-x_ses-1_acq-B_dir-AP_epi", "10:10:00")
        # bold has NO AcquisitionTime -> falls back to first compatible (A);
        # sbref is timed at 10:09 -> closest is B. Pair must converge on B.
        _mk_compat(func, "sub-x_ses-1_task-t_bold", "10:01:00", nvols=20)
        with open(func / "sub-x_ses-1_task-t_bold.json", "w") as f:
            json.dump({"ShimSetting": _SHIM, "PhaseEncodingDirection": "j-"}, f)
        _mk_compat(func, "sub-x_ses-1_task-t_sbref", "10:09:00", nvols=1)

        reproinx._populate_b0_fields(ses)

        bold_src = _read(func / "sub-x_ses-1_task-t_bold.json").get("B0FieldSource")
        sbref_src = _read(func / "sub-x_ses-1_task-t_sbref.json").get("B0FieldSource")
        assert sbref_src.startswith("acq-B"), sbref_src   # sbref's timed choice
        assert bold_src == sbref_src, (bold_src, sbref_src)  # bold adopts it


def test_sbref_mirror_untimed_keeps_bold_match():
    # Fix 6: neither sibling timed, bold matches an fmap, sbref matches none.
    # The shared selection must keep the bold's valid choice, not be erased to
    # None by the incompatible sbref.
    with tempfile.TemporaryDirectory() as d:
        ses = Path(d) / "sub-x" / "ses-1"
        fmap = ses / "fmap"; func = ses / "func"
        fmap.mkdir(parents=True); func.mkdir(parents=True)
        _mk_compat(fmap, "sub-x_ses-1_acq-A_dir-AP_epi", "10:00:00")
        # bold: compatible (sform+shim), but NO AcquisitionTime.
        _write_nii_gz(func / "sub-x_ses-1_task-t_bold.nii.gz", 20, with_sform=True)
        with open(func / "sub-x_ses-1_task-t_bold.json", "w") as f:
            json.dump({"ShimSetting": _SHIM, "PhaseEncodingDirection": "j-"}, f)
        # sbref: NO time AND incompatible (different shim) -> selects None.
        _write_nii_gz(func / "sub-x_ses-1_task-t_sbref.nii.gz", 1, with_sform=True)
        with open(func / "sub-x_ses-1_task-t_sbref.json", "w") as f:
            json.dump({"ShimSetting": [9, 9, 9], "PhaseEncodingDirection": "j-"}, f)

        reproinx._populate_b0_fields(ses)

        bold_src = _read(func / "sub-x_ses-1_task-t_bold.json").get("B0FieldSource")
        sbref_src = _read(func / "sub-x_ses-1_task-t_sbref.json").get("B0FieldSource")
        assert bold_src is not None and bold_src.startswith("acq-A"), bold_src
        assert sbref_src == bold_src, (sbref_src, bold_src)  # sbref adopts bold's match


def test_shift_dates():
    # --shift-dates: per-subject whole-day offset anchors the earliest scan to
    # 1925-01-01, preserving time-of-day and intra-subject intervals, across
    # sidecars and scans.tsv.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        a1 = root / "sub-x" / "ses-01" / "anat"; a1.mkdir(parents=True)
        a2 = root / "sub-x" / "ses-02" / "anat"; a2.mkdir(parents=True)
        (a1 / "sub-x_ses-01_T1w.json").write_text(
            json.dumps({"AcquisitionDateTime": "2023-09-14T10:00:00"}))
        (a2 / "sub-x_ses-02_T1w.json").write_text(           # +7 days, later time
            json.dumps({"AcquisitionDateTime": "2023-09-21T11:30:00"}))
        (root / "sub-x" / "ses-01" / "sub-x_ses-01_scans.tsv").write_text(
            "filename\tacq_time\toperator\trandstr\r\n"
            "anat/sub-x_ses-01_T1w.nii.gz\t2023-09-14T10:00:00\tn/a\tabc\r\n")
        # provenance (.tsv AND rotated .bak) carry raw StudyDate/PatientID
        prov = root / reproinx._PROVENANCE_TSV
        bak = root / (reproinx._PROVENANCE_TSV + ".bak")
        prov.write_text("StudyInstanceUID\tStudyDate\tPatientID\n1.2.3\t20230914\tREALID\n")
        bak.write_text("StudyInstanceUID\tStudyDate\tPatientID\n1.2.3\t20230914\tREALID\n")
        # a RETAINED derivatives sidecar (e.g. --keep-derivatives) must shift too
        deriv = root / "derivatives" / "scanner" / "sub-x" / "anat"
        deriv.mkdir(parents=True)
        (deriv / "sub-x_scout.json").write_text(
            json.dumps({"AcquisitionDateTime": "2023-09-14T09:00:00"}))

        assert reproinx._shift_dates(root) == 1
        assert _read(a1 / "sub-x_ses-01_T1w.json")["AcquisitionDateTime"] == "1925-01-01T10:00:00"
        assert _read(a2 / "sub-x_ses-02_T1w.json")["AcquisitionDateTime"] == "1925-01-08T11:30:00"
        tsv = (root / "sub-x" / "ses-01" / "sub-x_ses-01_scans.tsv").read_text()
        assert "1925-01-01T10:00:00" in tsv and "2023" not in tsv
        assert not prov.exists() and not bak.exists()  # provenance + .bak scrubbed
        # derivatives sidecar shifted by the subject's offset (same as raw tree)
        assert _read(deriv / "sub-x_scout.json")["AcquisitionDateTime"] == "1925-01-01T09:00:00"


def test_shift_dates_warns_on_timestamp_session():
    # A timestamp-derived session dir (ses-YYYYMMDDThhmmss) still leaks the real
    # date via the path; --shift-dates must warn loudly (it can't rename it).
    import io, contextlib
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        s = root / "sub-x" / "ses-20230914T103000" / "anat"; s.mkdir(parents=True)
        (s / "sub-x_ses-20230914T103000_T1w.json").write_text(
            json.dumps({"AcquisitionDateTime": "2023-09-14T10:30:00"}))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            reproinx._shift_dates(root)
        msg = err.getvalue()
        assert "WARNING" in msg and "leak" in msg, msg


def test_shift_dates_warns_zero_offset():
    # Re-run / already-1925 tree: offset is 0 (nothing to shift) but a timestamp
    # ses-<...> dir still leaks the date via the path, so the warning must still
    # fire (regression for the offset-zero early-continue gap).
    import io, contextlib
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        s = root / "sub-x" / "ses-19250101T100000" / "anat"; s.mkdir(parents=True)
        (s / "sub-x_ses-19250101T100000_T1w.json").write_text(
            json.dumps({"AcquisitionDateTime": "1925-01-01T10:00:00"}))  # offset 0
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            reproinx._shift_dates(root)
        assert "WARNING" in err.getvalue() and "leak" in err.getvalue()


def test_shift_dates_resolves_acq_time_column():
    # acq_time resolved from the header, not assumed column 2: a reordered TSV
    # still gets the right column shifted.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        a = root / "sub-x" / "ses-01" / "anat"; a.mkdir(parents=True)
        (a / "sub-x_ses-01_T1w.json").write_text(
            json.dumps({"AcquisitionDateTime": "2023-09-14T10:00:00"}))
        scans = root / "sub-x" / "ses-01" / "sub-x_ses-01_scans.tsv"
        scans.write_text(  # acq_time is column 1, not 2
            "acq_time\tfilename\r\n2023-09-14T10:00:00\tanat/sub-x_ses-01_T1w.nii.gz\r\n")
        reproinx._shift_dates(root)
        assert "1925-01-01T10:00:00" in scans.read_text() and "2023" not in scans.read_text()


def test_shift_dates_scans_only_and_acqdate():
    # A collated tree where the date lives ONLY in scans.tsv (sidecar lacks
    # AcquisitionDateTime) must still be de-identified; and a sidecar carrying
    # AcquisitionDate+AcquisitionTime (no AcquisitionDateTime) must have the
    # date field shifted. Regression for the fail-open gap in offset discovery.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        a = root / "sub-x" / "ses-01" / "anat"; a.mkdir(parents=True)
        # sidecar with only date-only AcquisitionDate + time-only AcquisitionTime
        (a / "sub-x_ses-01_T1w.json").write_text(
            json.dumps({"AcquisitionDate": "20230914", "AcquisitionTime": "10:00:00"}))
        # a second sidecar with NO date at all, but scans.tsv carries the real date
        (a / "sub-x_ses-01_T2w.json").write_text("{}")
        scans = root / "sub-x" / "ses-01" / "sub-x_ses-01_scans.tsv"
        scans.write_text(
            "filename\tacq_time\toperator\trandstr\r\n"
            "anat/sub-x_ses-01_T2w.nii.gz\t2023-09-14T08:00:00\tn/a\tz\r\n")

        assert reproinx._shift_dates(root) == 1
        # earliest across sources is the scans.tsv 08:00 -> anchors 09-14 to 1925
        assert "1925-01-01T08:00:00" in scans.read_text() and "2023" not in scans.read_text()
        # the date-only AcquisitionDate field was shifted (no real date left)
        assert _read(a / "sub-x_ses-01_T1w.json")["AcquisitionDate"] == "19250101"
        assert _read(a / "sub-x_ses-01_T1w.json")["AcquisitionTime"] == "10:00:00"  # time untouched

    # bare AcquisitionDate ONLY (no time, no scans.tsv) is still discovered+shifted
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        a = root / "sub-y" / "ses-01" / "anat"; a.mkdir(parents=True)
        (a / "sub-y_ses-01_T1w.json").write_text(json.dumps({"AcquisitionDate": "20230914"}))
        assert reproinx._shift_dates(root) == 1
        assert _read(a / "sub-y_ses-01_T1w.json")["AcquisitionDate"] == "19250101"


def test_shift_dates_fatal_on_unreadable():
    # Fail-closed: an unreadable/malformed date-bearing sidecar must abort
    # --shift-dates (not silently skip, leaving a real date), so the caller
    # exits non-zero rather than claiming de-identification.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        a = root / "sub-x" / "ses-01" / "anat"; a.mkdir(parents=True)
        (a / "sub-x_ses-01_T1w.json").write_text("{ this is not valid json")
        raised = False
        try:
            reproinx._shift_dates(root)
        except Exception:
            raised = True
        assert raised, "unreadable sidecar must make --shift-dates fail closed"


def test_shift_dates_subjectless_unknown():
    # MED/LOW-MED regression: a subjectless, all-Unknown/ study (e.g. CT left in
    # Unknown/, or a wholly unrescued study) must still be de-identified. Root-level
    # Unknown/*.json and stray *_scans.tsv are outside the per-subject walk; the
    # residual sweep anchors each to the reference year, and provenance is scrubbed.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        unk = root / "Unknown"; unk.mkdir()
        (unk / "1_ct.json").write_text(
            json.dumps({"AcquisitionDateTime": "2023-09-14T10:00:00"}))
        (unk / "1_ct_scans.tsv").write_text(
            "filename\tacq_time\r\nct/1_ct.nii.gz\t2023-09-14T10:00:00\r\n")
        prov = root / reproinx._PROVENANCE_TSV
        prov.write_text("StudyInstanceUID\tStudyDate\tPatientID\n1.2.3\t20230914\tREALID\n")
        # no sub-* anywhere: _shift_dates returns 0 subjects, but must still scrub
        assert reproinx._shift_dates(root) == 0
        assert _read(unk / "1_ct.json")["AcquisitionDateTime"] == "1925-01-01T10:00:00"
        tsv = (unk / "1_ct_scans.tsv").read_text()
        assert "1925-01-01T10:00:00" in tsv and "2023" not in tsv
        assert not prov.exists()  # provenance (raw StudyDate/PatientID) scrubbed


def test_shift_dates_runs_without_sessions():
    # HIGH regression: _post_process previously returned before the shift pass when
    # no sub-* tree existed, silently shipping real dates + provenance at exit 0.
    # Verify the subjectless early-return branch still invokes --shift-dates.
    import io, contextlib
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "Unknown").mkdir()
        (root / "Unknown" / "1_ct.json").write_text(
            json.dumps({"AcquisitionDateTime": "2023-09-14T10:00:00", "BidsGuess": []}))
        called = []
        orig = reproinx._shift_dates
        reproinx._shift_dates = lambda r: (called.append(r), 0)[1]
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                reproinx._post_process(root, strict=False, shift_dates=True)
        finally:
            reproinx._shift_dates = orig
        assert called, "subjectless _post_process must still run --shift-dates"


def test_shift_dates_subjectless_subname():
    # Regression (audit 2026-07-07): a subjectless survivor whose FILENAME starts
    # with "sub-" (subject known but classification failed -> Unknown/sub-x_ct.json)
    # must still be shifted. The first _already_shifted tested path *components*
    # incl. the filename, so it wrongly treated these as already-handled, leaving
    # a real date. Must key off actual subject DIRS, not name prefixes.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        unk = root / "Unknown"; unk.mkdir()
        (unk / "sub-real_ct.json").write_text(
            json.dumps({"AcquisitionDateTime": "2023-09-14T10:00:00"}))
        (unk / "sub-real_ct_scans.tsv").write_text(
            "filename\tacq_time\r\nct/sub-real_ct.nii.gz\t2023-09-14T10:00:00\r\n")
        assert reproinx._shift_dates(root) == 0  # no real subject dir
        assert _read(unk / "sub-real_ct.json")["AcquisitionDateTime"] == "1925-01-01T10:00:00"
        tsv = (unk / "sub-real_ct_scans.tsv").read_text()
        assert "1925-01-01T10:00:00" in tsv and "2023" not in tsv


def test_resolve_part_entities():
    # Multi-echo BOLD with phase: magnitude/phase series collide onto one %H stem
    # (dcm2niix appends a/b letters, and a phase SBRef can land in a _bold name).
    # _resolve_part_entities must rename to _part-mag/_part-phase with the true
    # bold/sbref suffix (from BidsGuess), add Units to Siemens phase sidecars,
    # take part from BidsGuess even when ImageType is absent, and be idempotent.
    with tempfile.TemporaryDirectory() as d:
        ses = Path(d) / "sub-01" / "ses-1"
        func = ses / "func"; func.mkdir(parents=True)
        base = "sub-01_ses-1_task-rest_acq-2d2echo_run-02_echo-1"

        def mk(stem, imgtype, guess_suffix, nvols=1):
            _write_nii_gz(func / f"{stem}.nii.gz", nvols)
            j = {"Manufacturer": "Siemens",
                 "BidsGuess": ["func", f"_echo-1_{guess_suffix}"]}
            if imgtype is not None:  # imgtype None -> exercise BidsGuess-part precedence
                j["ImageType"] = ["ORIGINAL", "PRIMARY", "FMRI", "NONE", imgtype]
            (func / f"{stem}.json").write_text(json.dumps(j))
        mk(f"{base}_bold", "MAGNITUDE", "bold", nvols=10)
        mk(f"{base}_bolda", None, "part-phase_sbref")          # phase SBRef, no ImageType
        mk(f"{base}_boldb", "PHASE", "part-phase_bold", nvols=10)
        mk(f"{base}_sbref", "MAGNITUDE", "sbref")

        n1 = reproinx._resolve_part_entities(ses)
        names = {p.name for p in func.glob("*.nii.gz")}
        for want in ("part-mag_bold", "part-phase_bold",
                     "part-mag_sbref", "part-phase_sbref"):
            assert f"{base}_{want}.nii.gz" in names, (want, names)
        assert not any("bolda" in n or "boldb" in n for n in names)  # collisions resolved
        assert _read(func / f"{base}_part-phase_bold.json").get("Units") == "arbitrary"
        assert _read(func / f"{base}_part-mag_bold.json").get("Units") is None  # mag: no Units
        # Idempotent: a second pass renames nothing and preserves names.
        n2 = reproinx._resolve_part_entities(ses)
        assert n2 == 0, n2
        assert {p.name for p in func.glob("*.nii.gz")} == names


def test_emit_events_tsv_one_per_run():
    # A multi-echo, multi-part run must yield ONE run-level _events.tsv (no echo,
    # no part entity) shared across all echoes and magnitude/phase.
    with tempfile.TemporaryDirectory() as d:
        ses = Path(d) / "sub-01"
        func = ses / "func"; func.mkdir(parents=True)
        base = "sub-01_task-rest_run-01"
        for e in (1, 2):
            for part in ("mag", "phase"):
                _write_nii_gz(func / f"{base}_echo-{e}_part-{part}_bold.nii.gz", 10)
        reproinx._emit_events_tsv(ses)
        ev = sorted(p.name for p in func.glob("*_events.tsv"))
        assert ev == [f"{base}_events.tsv"], ev


def test_resolve_part_units_canonical():
    # Idempotence gap: an already-canonical Siemens phase file lacking Units
    # (e.g. a prior run moved it but failed the Units write) must still get Units
    # on a rerun, though no rename occurs.
    with tempfile.TemporaryDirectory() as d:
        ses = Path(d) / "sub-01"; func = ses / "func"; func.mkdir(parents=True)
        base = "sub-01_task-rest_run-01_echo-1"

        def mk(stem, imgtype):
            _write_nii_gz(func / f"{stem}.nii.gz", 10)
            (func / f"{stem}.json").write_text(json.dumps({
                "Manufacturer": "Siemens",
                "ImageType": ["ORIGINAL", "PRIMARY", "FMRI", "NONE", imgtype]}))
        mk(f"{base}_part-mag_bold", "MAGNITUDE")
        mk(f"{base}_part-phase_bold", "PHASE")            # canonical, no Units
        assert reproinx._resolve_part_entities(ses) == 0  # nothing to move
        assert _read(func / f"{base}_part-phase_bold.json").get("Units") == "arbitrary"


def test_resolve_part_ambiguous_skipped():
    # A BidsGuess with conflicting components (Philips _part-phase_part-mag) is
    # ambiguous; leave the file untouched rather than invent a single part.
    with tempfile.TemporaryDirectory() as d:
        ses = Path(d) / "sub-01"; func = ses / "func"; func.mkdir(parents=True)
        stem = "sub-01_task-rest_run-01_bold"
        _write_nii_gz(func / f"{stem}.nii.gz", 10)
        (func / f"{stem}.json").write_text(json.dumps({
            "Manufacturer": "Philips",
            "BidsGuess": ["func", "_part-phase_part-mag_bold"]}))
        reproinx._resolve_part_entities(ses)
        assert (func / f"{stem}.nii.gz").exists()               # untouched
        assert not list(func.glob("*part-phase_part-phase*"))  # no corruption


def test_resolve_part_unclassifiable_sibling_skips_family():
    # A recognized phase member plus a same-prefix sibling whose part cannot be
    # inferred (no ImageType, bare BidsGuess) must NOT be partially resolved: the
    # whole prefix family is left at its collision names rather than shipping an
    # inconsistent _bold + _part-phase_bold pair.
    with tempfile.TemporaryDirectory() as d:
        ses = Path(d) / "sub-01"; func = ses / "func"; func.mkdir(parents=True)
        base = "sub-01_task-rest_run-01"
        _write_nii_gz(func / f"{base}_bold.nii.gz", 10)          # unclassifiable mag
        (func / f"{base}_bold.json").write_text(json.dumps({
            "Manufacturer": "Siemens", "BidsGuess": ["func", "_bold"]}))
        _write_nii_gz(func / f"{base}_bolda.nii.gz", 10)         # recognized phase
        (func / f"{base}_bolda.json").write_text(json.dumps({
            "Manufacturer": "Siemens", "ImageType": ["ORIGINAL", "PRIMARY", "PHASE"],
            "BidsGuess": ["func", "_part-phase_bold"]}))
        n = reproinx._resolve_part_entities(ses)
        assert n == 0, n
        assert (func / f"{base}_bold.nii.gz").exists()           # untouched
        assert (func / f"{base}_bolda.nii.gz").exists()          # phase NOT partial-renamed
        assert not list(func.glob("*_part-phase_*"))             # no partial family


def test_apply_dup_naming_missing_base():
    # Pass-order coupling: if an earlier pass removed the unsuffixed base while a
    # suffixed sibling survives, _apply_dup_naming must NOT rename the survivor to
    # __dup-01 (a base-less duplicate family); it skips the group.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        func = root / "sub-01" / "func"; func.mkdir(parents=True)
        base = "sub-01/func/sub-01_task-rest_bold"
        _write_nii_gz(root / f"{base}a.nii.gz", 10)              # only the 'a' sibling exists
        hdr = "OutputStem\tSeriesNumber\tStudyInstanceUID\tProtocolName\tSeriesDescription\n"
        rows = f"{base}\t1\t1.2.3\trest\trest\n{base}a\t2\t1.2.3\trest\trest\n"
        (root / reproinx._PROVENANCE_TSV).write_text(hdr + rows)
        n = reproinx._apply_dup_naming(root)
        assert n == 0, n
        assert (root / f"{base}a.nii.gz").exists()               # survivor untouched
        assert not (root / f"{base}__dup-01.nii.gz").exists()    # no dup-only family


def test_apply_dup_naming_rolls_back():
    # Both phases form one transaction. A failure after every source moved to a
    # temporary stem must restore the as-written group, and a rerun must finish.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        func = root / "sub-01" / "func"; func.mkdir(parents=True)
        base = "sub-01/func/sub-01_task-rest_bold"
        _write_nii_gz(root / f"{base}.nii.gz", 10)
        _write_nii_gz(root / f"{base}a.nii.gz", 10)
        hdr = "OutputStem\tSeriesNumber\tStudyInstanceUID\tProtocolName\tSeriesDescription\n"
        # The suffixed file has the lower SeriesNumber, so both stems must move.
        rows = f"{base}\t2\t1.2.3\trest\trest\n{base}a\t1\t1.2.3\trest\trest\n"
        (root / reproinx._PROVENANCE_TSV).write_text(hdr + rows)
        move = reproinx._move_stem_files
        calls = 0

        def fail_second_phase(src, dst):
            nonlocal calls
            calls += 1
            if calls == 4:  # both staged and one finalized; fail the next target
                raise OSError("injected duplicate finalization failure")
            return move(src, dst)

        reproinx._move_stem_files = fail_second_phase
        try:
            try:
                reproinx._apply_dup_naming(root)
                assert False, "injected failure did not propagate"
            except OSError:
                pass
        finally:
            reproinx._move_stem_files = move
        assert (root / f"{base}.nii.gz").exists()
        assert (root / f"{base}a.nii.gz").exists()
        assert not list(func.glob("*__reproinx-tmp-*"))
        assert reproinx._apply_dup_naming(root) == 2
        assert (root / f"{base}.nii.gz").exists()
        assert (root / f"{base}__dup-01.nii.gz").exists()
        assert not list(func.glob("*__reproinx-tmp-*"))


if __name__ == "__main__":
    test_sbref_is_fmap_target()
    test_sbref_taskname_backfill()
    test_reclassify_skips_multiecho()
    test_sbref_mirrors_bold_b0field()
    test_sbref_mirror_prefers_timed_sibling()
    test_sbref_mirror_untimed_keeps_bold_match()
    test_shift_dates()
    test_shift_dates_warns_on_timestamp_session()
    test_shift_dates_warns_zero_offset()
    test_shift_dates_resolves_acq_time_column()
    test_shift_dates_scans_only_and_acqdate()
    test_shift_dates_fatal_on_unreadable()
    test_shift_dates_subjectless_unknown()
    test_shift_dates_runs_without_sessions()
    test_shift_dates_subjectless_subname()
    test_resolve_part_entities()
    test_emit_events_tsv_one_per_run()
    test_resolve_part_units_canonical()
    test_resolve_part_ambiguous_skipped()
    test_resolve_part_unclassifiable_sibling_skips_family()
    test_apply_dup_naming_missing_base()
    test_apply_dup_naming_rolls_back()
    print("OK: reproinx fixes pass (fmap-target, TaskName, multiecho-R1, part-entities, b0-mirror x3, shift-dates x9)")
