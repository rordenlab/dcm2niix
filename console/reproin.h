// ReproIn (DBIC heuristic) one-pass filename emulation for dcm2niix.
// See REPROIN.md for design notes and known limitations.
#ifndef REPROIN_H
#define REPROIN_H

#include "nii_dicom.h"
#include "nii_dicom_batch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define kReproinStr 128

struct TReproinSpec {
	char datatype[16]; // anat | func | fmap | dwi | beh
	char suffix[32];   // T1w, bold, epi, magnitude{,1,2}, phasediff, sbref, scout, ...
	char ses[kReproinStr];
	char task[kReproinStr];
	char acq[kReproinStr];
	char rec[32];
	char dir[16];
	char run[16];			// numeric token (literal); zero-padded at format time
	char bids[kReproinStr]; // unrecognised key-value tokens, joined with '_'
	bool isSbref;
	bool isDerived;
	bool valid;
};

// Parse ProtocolName (preferred) or SeriesDescription (fallback) into spec.
// Sets spec->valid = true on success. Suffix inference (when ProtocolName lacks
// it) consults dcm->imageType / dcm->isHasPhase / dcm->seriesDescription.
bool reproinParseSpec(const struct TDICOMdata *dcm, struct TReproinSpec *spec);

// Build study path component (e.g. "BrainHealth/AgingBrain") from
// StudyDescription, falling back to PerformedProcedureStepDescription. Spaces
// and underscores become path separators. Output is empty if no source field
// is set.
void reproinBuildStudyPath(const struct TDICOMdata *dcm, char *pthOut, size_t cap);

// Construct the BIDS-style basename portion (everything after `pth`):
//   <subDir>/<datatype>/<sub>_<ses>[_task-..][_acq-..][_dir-..][_run-NN][_echo-N]_<suffix>
// `bidsSubject` / `bidsSession` are bare values without the "sub-"/"ses-" prefix.
// `echoNum` is dcm.echoNum; pass <=1 for single-echo. `isMultiEcho` toggles
// _echo-N injection and magnitude1/magnitude2 selection for GRE fmaps.
// Returns false if the spec is invalid.
bool reproinBuildFilename(const struct TReproinSpec *spec,
						  const char *bidsSubject,
						  const char *bidsSession,
						  int echoNum,
						  bool isMultiEcho,
						  char *outname,
						  size_t cap);

// Combined classifier: DICOM ImageType + textual heuristics on
// SeriesDescription/ProtocolName + parsed datatype-suffix == anat-scout.
bool reproinIsDerived(const struct TDICOMdata *dcm, const struct TReproinSpec *spec);

// Port of heudiconv's fixup_subjectid: lowercase, reformat sid<digits> to
// sid%06d, otherwise strip '-' and '_'. `in` is treated as raw PatientID.
void reproinFixupSubjectId(const char *in, char *out, size_t cap);

// In-place path-safety scrub for any BIDS label that arrived from outside the
// reproin parser (e.g. -bi/-bv values from the command line). Strips '/', '\\',
// whitespace, and control characters but preserves case and digits. Does NOT
// apply heudiconv subject normalisation; use reproinFixupSubjectId for that.
void reproinSanitizeLabel(char *s);

// Resolve session value for use as `ses-<X>` in BIDS path/filename.
// Precedence (matches REPROIN.md and the heudiconv heuristic):
//   1. `spec->ses` (parsed `_ses-<X>` from ProtocolName) — if present, wins.
//   2. `cliSession` (from `-bv`) — used only when the spec has no `_ses-`.
//   3. Otherwise output is empty and the BIDS layout omits the ses- segment.
// The literal markers `{date}` (Python-friendly) and `DATE` (Siemens X60)
// are resolved against `dcm->studyDate` (YYYYMMDD).
void reproinResolveSession(const struct TReproinSpec *spec,
						   const struct TDICOMdata *dcm,
						   const char *cliSession,
						   char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif // REPROIN_H
