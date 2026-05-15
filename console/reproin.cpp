// ReproIn (DBIC heuristic) one-pass filename emulation for dcm2niix.
// See REPROIN.md for design notes and known limitations. The Python ground
// truth lives in heudiconv's reproin heuristic (parse_series_spec / infotodict).

#include "reproin.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// MSVC names the reentrant tokenizer strtok_s with the same signature as
// POSIX strtok_r. Map one to the other so reproin.cpp builds on Windows.
#ifdef _MSC_VER
#define strtok_r strtok_s
#endif

#if defined(_WIN64) || defined(_WIN32)
static const char kReproinPathSep = '\\';
#else
static const char kReproinPathSep = '/';
#endif

// Known datatypes per ReproIn spec (see KNOWN_DATATYPES in reproin heuristic).
static bool reproinKnownDatatype(const char *s) {
	return (strcmp(s, "anat") == 0) || (strcmp(s, "func") == 0) ||
		   (strcmp(s, "fmap") == 0) || (strcmp(s, "dwi") == 0) ||
		   (strcmp(s, "beh") == 0);
}

// _delete_chars(value, "#!@$%^&.,:;_-") — sanitises task/acq/etc.
// Extended beyond heudiconv's set with path-traversal-relevant characters
// (slashes, backslashes, whitespace, control chars) so that DICOM/CLI values
// cannot inject directory structure into a BIDS filename.
static void reproinSanitize(char *s) {
	static const char kIllegal[] = "#!@$%^&.,:;_-/\\ \t\r\n";
	int j = 0;
	for (int i = 0; s[i] != '\0'; i++) {
		unsigned char c = (unsigned char)s[i];
		bool drop = (c < 0x20); // control characters
		for (int k = 0; !drop && kIllegal[k] != '\0'; k++) {
			if ((char)c == kIllegal[k]) {
				drop = true;
				break;
			}
		}
		if (!drop)
			s[j++] = (char)c;
	}
	s[j] = '\0';
}

// Strip raw path separators and control characters from a study-path source.
// Called before reproinPathify expands underscores/spaces into the *intended*
// path separators, so any '/' or '\\' surviving into the output came from us.
static void reproinDropRawPathChars(char *s) {
	int j = 0;
	for (int i = 0; s[i] != '\0'; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '/' || c == '\\' || c < 0x20)
			continue;
		s[j++] = (char)c;
	}
	s[j] = '\0';
}

// Public path-safety scrub for any BIDS label arriving from outside the
// reproin parser (e.g. -bi/-bv values from the command line). Strips path
// separators, whitespace, control chars, and any '.' segments by collapsing
// dots that would form a traversal. Preserves case and digits.
void reproinSanitizeLabel(char *s) {
	if (s == NULL)
		return;
	int j = 0;
	for (int i = 0; s[i] != '\0'; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '/' || c == '\\' || c == '.' ||
			c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
			c < 0x20)
			continue;
		s[j++] = (char)c;
	}
	s[j] = '\0';
}

// Walk the path-separator-joined output and drop any "." or ".." segments.
// Defends against directory traversal when a DICOM StudyDescription contains
// dots (e.g. "../escape" → after pathify still "../escape"; here it becomes
// "escape").
static void reproinRejectDotSegments(char *s) {
	int n = (int)strlen(s);
	char buf[PATH_MAX];
	int bj = 0;
	int i = 0;
	while (i <= n) {
		int start = i;
		while (i < n && s[i] != kReproinPathSep)
			i++;
		int len = i - start;
		bool isDot = (len == 1 && s[start] == '.');
		bool isDotDot = (len == 2 && s[start] == '.' && s[start + 1] == '.');
		if (len > 0 && !isDot && !isDotDot) {
			if (bj > 0 && bj + 1 < (int)sizeof(buf))
				buf[bj++] = kReproinPathSep;
			if (bj + len < (int)sizeof(buf)) {
				memcpy(buf + bj, s + start, len);
				bj += len;
			}
		}
		i++; // skip separator
	}
	buf[bj] = '\0';
	strcpy(s, buf);
}

// Drop leading "[A-Z]+:" (vendor prefix) and "WIP " (Philips). In-place.
static void reproinStripPrefix(char *s) {
	// ^[A-Z]+:
	int n = 0;
	while (s[n] >= 'A' && s[n] <= 'Z')
		n++;
	if (n > 0 && s[n] == ':') {
		memmove(s, s + n + 1, strlen(s + n + 1) + 1);
	}
	// ^WIP\s+
	if (strncmp(s, "WIP ", 4) == 0)
		memmove(s, s + 4, strlen(s + 4) + 1);
}

// Drop "__custom" trailing comment.
static void reproinStripCustom(char *s) {
	char *p = strstr(s, "__");
	if (p != NULL)
		*p = '\0';
}

// trim leading/trailing whitespace
static void reproinTrim(char *s) {
	int n = (int)strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
	int i = 0;
	while (s[i] == ' ' || s[i] == '\t')
		i++;
	if (i > 0)
		memmove(s, s + i, strlen(s + i) + 1);
}

// Apply the same fixups parse_series_spec applies before tokenising.
// In-place. Buffer must be large enough to hold the longest substitution
// (len("dwi_acq-hardi64") - len("hardi_64") = +7 chars).
static void reproinSpecFixups(char *s, size_t cap) {
	// anat_T1w -> anat-T1w (only as a prefix to avoid corrupting other tokens)
	if (strncmp(s, "anat_T1w", 8) == 0)
		s[4] = '-';
	// hardi_64 -> dwi_acq-hardi64
	char *p = strstr(s, "hardi_64");
	if (p != NULL) {
		size_t tailLen = strlen(p + 8); // chars after "hardi_64"
		const char repl[] = "dwi_acq-hardi64";
		size_t replLen = strlen(repl);
		if ((p - s) + replLen + tailLen + 1 < cap) {
			memmove(p + replLen, p + 8, tailLen + 1);
			memcpy(p, repl, replLen);
		}
	}
	// AAHead_Scout -> anat-scout
	p = strstr(s, "AAHead_Scout");
	if (p != NULL) {
		size_t tailLen = strlen(p + 12);
		const char repl[] = "anat-scout";
		size_t replLen = strlen(repl);
		memmove(p + replLen, p + 12, tailLen + 1);
		memcpy(p, repl, replLen);
	}
	// bare leading "scout" (token) -> "anat-scout"
	if (strncmp(s, "scout", 5) == 0 && (s[5] == '\0' || s[5] == '_')) {
		size_t tailLen = strlen(s + 5);
		const char repl[] = "anat-scout";
		size_t replLen = strlen(repl);
		if (replLen + tailLen + 1 < cap) {
			memmove(s + replLen, s + 5, tailLen + 1);
			memcpy(s, repl, replLen);
		}
	}
}

// Split "key-value" once. Returns value pointer (into s) or NULL if no '-'.
// On return, s holds the key (NUL-terminated at the original '-').
static char *reproinSplit2(char *s) {
	char *dash = strchr(s, '-');
	if (dash == NULL)
		return NULL;
	*dash = '\0';
	return dash + 1;
}

// Slot known entities, accumulate everything else into bids leftovers.
static void reproinAssignToken(struct TReproinSpec *spec, const char *raw,
							   char *bidsBuf, size_t bidsCap) {
	char tok[kReproinStr];
	snprintf(tok, sizeof(tok), "%s", raw);
	char *value = reproinSplit2(tok);
	if (value == NULL) {
		// Unknown bareword: append to bids leftovers.
		if (strlen(bidsBuf) > 0) {
			if (strlen(bidsBuf) + 1 < bidsCap)
				strcat(bidsBuf, "_");
		}
		if (strlen(bidsBuf) + strlen(raw) < bidsCap)
			strcat(bidsBuf, raw);
		return;
	}
	char clean[kReproinStr];
	snprintf(clean, sizeof(clean), "%s", value);
	if (strcmp(tok, "ses") == 0) {
		reproinSanitize(clean);
		snprintf(spec->ses, sizeof(spec->ses), "%s", clean);
	} else if (strcmp(tok, "run") == 0) {
		// Don't sanitise digits; preserve literal value (we zero-pad later).
		// But still scrub path separators/control chars in case of garbage.
		snprintf(spec->run, sizeof(spec->run), "%s", value);
		reproinDropRawPathChars(spec->run);
	} else if (strcmp(tok, "task") == 0) {
		reproinSanitize(clean);
		snprintf(spec->task, sizeof(spec->task), "%s", clean);
	} else if (strcmp(tok, "acq") == 0) {
		reproinSanitize(clean);
		snprintf(spec->acq, sizeof(spec->acq), "%s", clean);
	} else if (strcmp(tok, "rec") == 0) {
		reproinSanitize(clean);
		snprintf(spec->rec, sizeof(spec->rec), "%s", clean);
	} else if (strcmp(tok, "dir") == 0) {
		reproinSanitize(clean);
		snprintf(spec->dir, sizeof(spec->dir), "%s", clean);
	} else {
		// Unknown key-value: keep verbatim in bids leftovers, but scrub path
		// separators/control chars to keep the segment safe.
		char safe[kReproinStr];
		snprintf(safe, sizeof(safe), "%s", raw);
		reproinDropRawPathChars(safe);
		if (strlen(bidsBuf) > 0 && strlen(bidsBuf) + 1 < bidsCap)
			strcat(bidsBuf, "_");
		if (strlen(bidsBuf) + strlen(safe) < bidsCap)
			strcat(bidsBuf, safe);
	}
}

// Core parser. Returns true if `text` looks like a valid ReproIn spec.
static bool reproinParseString(const char *text, struct TReproinSpec *spec) {
	if (text == NULL || text[0] == '\0')
		return false;
	char buf[kReproinStr];
	snprintf(buf, sizeof(buf), "%s", text);
	reproinTrim(buf);
	reproinStripPrefix(buf);
	reproinStripCustom(buf);
	reproinSpecFixups(buf, sizeof(buf));
	reproinTrim(buf);
	if (buf[0] == '\0')
		return false;
	// Tokenise on '_'
	char *saveptr = NULL;
	char *tok = strtok_r(buf, "_", &saveptr);
	if (tok == NULL)
		return false;
	// First token: <datatype>[-<suffix>]
	char *suffix = strchr(tok, '-');
	if (suffix != NULL) {
		*suffix = '\0';
		suffix++;
	}
	if (!reproinKnownDatatype(tok))
		return false;
	snprintf(spec->datatype, sizeof(spec->datatype), "%s", tok);
	if (suffix != NULL && suffix[0] != '\0')
		snprintf(spec->suffix, sizeof(spec->suffix), "%s", suffix);
	char bidsBuf[kReproinStr] = {""};
	while ((tok = strtok_r(NULL, "_", &saveptr)) != NULL) {
		reproinAssignToken(spec, tok, bidsBuf, sizeof(bidsBuf));
	}
	snprintf(spec->bids, sizeof(spec->bids), "%s", bidsBuf);
	spec->valid = true;
	return true;
}

// Pull ImageType[idx] (the IOD-specific specialisation slot is index 2:
// M/P/FMRI/MPR/DIFFUSION). dcm->imageType is "_"-joined in dcm2niix
// (the original backslash separators are normalised in nii_dicom.cpp).
static void reproinImageTypeSlot(const char *imageType, int idx, char *out, size_t cap) {
	out[0] = '\0';
	if (imageType == NULL || imageType[0] == '\0')
		return;
	int slot = 0;
	size_t start = 0;
	size_t len = strlen(imageType);
	for (size_t i = 0; i <= len; i++) {
		if (imageType[i] == '_' || imageType[i] == '\0') {
			if (slot == idx) {
				size_t n = i - start;
				if (n >= cap)
					n = cap - 1;
				memcpy(out, imageType + start, n);
				out[n] = '\0';
				return;
			}
			slot++;
			start = i + 1;
		}
	}
}

bool reproinParseSpec(const struct TDICOMdata *dcm, struct TReproinSpec *spec) {
	memset(spec, 0, sizeof(*spec));
	bool ok = reproinParseString(dcm->protocolName, spec);
	if (!ok)
		ok = reproinParseString(dcm->seriesDescription, spec);
	if (!ok)
		return false;
	// SBRef override (matches heuristic: series_description.endswith("_SBRef")).
	const char *sd = dcm->seriesDescription;
	size_t sdLen = sd ? strlen(sd) : 0;
	if (sdLen >= 6 && strcmp(sd + sdLen - 6, "_SBRef") == 0) {
		snprintf(spec->suffix, sizeof(spec->suffix), "sbref");
		spec->isSbref = true;
	}
	// Image-type-driven suffix inference (only if ProtocolName lacked one).
	char iod[16];
	reproinImageTypeSlot(dcm->imageType, 2, iod, sizeof(iod));
	if (spec->suffix[0] == '\0') {
		if (strcmp(spec->datatype, "func") == 0) {
			// "_pace_" anywhere in ProtocolName/SeriesDescription wins over
			// the imageType-driven default (heuristic.py:510-518).
			bool isPace = (dcm->protocolName[0] != '\0' && strstr(dcm->protocolName, "_pace_") != NULL) ||
						  (dcm->seriesDescription[0] != '\0' && strstr(dcm->seriesDescription, "_pace_") != NULL);
			if (isPace)
				snprintf(spec->suffix, sizeof(spec->suffix), "pace");
			else if (strcmp(iod, "P") == 0)
				snprintf(spec->suffix, sizeof(spec->suffix), "phase");
			else
				snprintf(spec->suffix, sizeof(spec->suffix), "bold");
		} else if (strcmp(spec->datatype, "fmap") == 0) {
			if (strcmp(iod, "P") == 0)
				snprintf(spec->suffix, sizeof(spec->suffix), "phasediff");
			else if (strcmp(iod, "DIFFUSION") == 0)
				snprintf(spec->suffix, sizeof(spec->suffix), "epi");
			else if (strcmp(iod, "M") == 0) {
				if (spec->dir[0] != '\0')
					snprintf(spec->suffix, sizeof(spec->suffix), "epi");
				else
					snprintf(spec->suffix, sizeof(spec->suffix), "magnitude");
			} else {
				// Unknown IOD: assume magnitude as a reasonable default.
				snprintf(spec->suffix, sizeof(spec->suffix), "magnitude");
			}
		} else if (strcmp(spec->datatype, "dwi") == 0) {
			snprintf(spec->suffix, sizeof(spec->suffix), "dwi");
		}
	}
	spec->isDerived = reproinIsDerived(dcm, spec);
	return true;
}

bool reproinIsDerived(const struct TDICOMdata *dcm, const struct TReproinSpec *spec) {
	if (dcm->isDerived)
		return true;
	if (spec != NULL && strcmp(spec->datatype, "anat") == 0 &&
		strcmp(spec->suffix, "scout") == 0)
		return true;
	// Inspect both SeriesDescription and ProtocolName so a marker present in
	// either field routes to derivatives/scanner. dcm2niix already prefers
	// ProtocolName over SeriesDescription at parse time, so a scanner that
	// only labels derived data via ProtocolName would otherwise slip through.
	const char *sources[2] = {dcm->seriesDescription, dcm->protocolName};
	for (int i = 0; i < 2; i++) {
		const char *s = sources[i];
		if (s != NULL && s[0] != '\0') {
			if (strstr(s, "-scout") || strstr(s, "_ADC") ||
				strstr(s, "_TRACEW") || strstr(s, "_TRACE") ||
				strstr(s, "_FA") || strstr(s, "AAHead_Scout"))
				return true;
		}
	}
	return false;
}

// Replace ' ', '_' with kReproinPathSep; drop '-' (matches existing
// heudiconvStrPth behaviour, extended for spaces). Caret '^' also becomes a
// path separator (PatientName style "Last^First").
static void reproinPathify(char *s) {
	int n = (int)strlen(s);
	int j = 0;
	bool hasCaret = false;
	for (int i = 0; i < n; i++) {
		char c = s[i];
		if (c == '^') {
			s[j++] = kReproinPathSep;
			hasCaret = true;
		} else if (!hasCaret && (c == '_' || c == ' ')) {
			s[j++] = kReproinPathSep;
		} else if (c == '-') {
			// drop
		} else {
			s[j++] = c;
		}
	}
	s[j] = '\0';
}

// Port of heudiconv's fixup_subjectid (heuristic.py:975):
//   subjectid = subjectid.lower()
//   reg = re.match(r"sid0*(\d+)$", subjectid)
//   if not reg:
//       return re.sub("[-_]", "", subjectid)
//   return "sid%06d" % int(reg.groups()[0])
void reproinFixupSubjectId(const char *in, char *out, size_t cap) {
	if (cap == 0)
		return;
	out[0] = '\0';
	if (in == NULL || in[0] == '\0')
		return;
	char buf[kReproinStr];
	snprintf(buf, sizeof(buf), "%s", in);
	for (int i = 0; buf[i] != '\0'; i++) {
		if (buf[i] >= 'A' && buf[i] <= 'Z')
			buf[i] = (char)(buf[i] + ('a' - 'A'));
	}
	// ^sid<digits>$ — Python regex `sid0*(\d+)$` accepts any non-empty digit
	// run after "sid", including all-zero strings (regex backtracking).
	if (strncmp(buf, "sid", 3) == 0 && buf[3] != '\0') {
		bool allDigits = true;
		for (const char *p = buf + 3; *p != '\0'; p++) {
			if (*p < '0' || *p > '9') {
				allDigits = false;
				break;
			}
		}
		if (allDigits) {
			long n = strtol(buf + 3, NULL, 10);
			snprintf(out, cap, "sid%06ld", n);
			return;
		}
	}
	// Otherwise strip '-' and '_' (heudiconv); reproinSanitizeLabel below
	// also drops path separators, dots, whitespace, controls.
	size_t j = 0;
	for (size_t i = 0; buf[i] != '\0' && j + 1 < cap; i++) {
		if (buf[i] != '-' && buf[i] != '_')
			out[j++] = buf[i];
	}
	out[j] = '\0';
	reproinSanitizeLabel(out);
}

// Resolve effective session value. Returns empty string when no session is
// determined from any source — caller should omit the ses- segment.
void reproinResolveSession(const struct TReproinSpec *spec,
						   const struct TDICOMdata *dcm,
						   const char *cliSession,
						   char *out, size_t cap) {
	if (cap == 0)
		return;
	out[0] = '\0';
	const char *src = NULL;
	if (spec != NULL && spec->ses[0] != '\0')
		src = spec->ses;
	else if (cliSession != NULL && cliSession[0] != '\0')
		src = cliSession;
	if (src == NULL)
		return;
	// Resolve heudiconv literal date markers: "{date}" (Python-friendly) or
	// "DATE" (Siemens X60 doesn't allow {} in protocol names). Either resolves
	// to the study date (YYYYMMDD), matching heuristic.py:800-802.
	if (dcm != NULL && (strcmp(src, "{date}") == 0 || strcmp(src, "DATE") == 0)) {
		if (dcm->studyDate[0] != '\0') {
			snprintf(out, cap, "%s", dcm->studyDate);
			return;
		}
		// Fall through: no date available, treat as no session.
		return;
	}
	snprintf(out, cap, "%s", src);
	// Sanitise: -bv from CLI bypasses reproinAssignToken's reproinSanitize.
	// Use the public Label scrub (drops path separators, dots, whitespace,
	// controls) so '.' segments cannot reach the filename composer.
	reproinSanitizeLabel(out);
}

void reproinSanitizeProjectPath(char *s) {
	if (s == NULL)
		return;
	// Same path-safety pipeline as reproinBuildStudyPath, applied to a
	// CLI-supplied string. Strips raw '/' and '\\' (so '-br /tmp/x' can't
	// climb out of -o), expands '_' and ' ' into our own kPathSeparator, then
	// drops "." / ".." segments so '-br ..' or '-br ../escape' resolves to
	// either empty or a path safely contained below -o.
	reproinDropRawPathChars(s);
	reproinPathify(s);
	reproinRejectDotSegments(s);
}

void reproinBuildStudyPath(const struct TDICOMdata *dcm, char *pthOut, size_t cap) {
	pthOut[0] = '\0';
	const char *src = NULL;
	if (dcm->studyDescription[0] != '\0')
		src = dcm->studyDescription;
	else if (dcm->procedureStepDescription[0] != '\0')
		src = dcm->procedureStepDescription;
	if (src == NULL)
		return;
	snprintf(pthOut, cap, "%s", src);
	reproinSanitizeProjectPath(pthOut);
}

// Decide concrete suffix when emitting fmap GRE multi-echo magnitudes.
// echoNum is 1-based DICOM EchoNumbers; isMultiEcho indicates the series has
// >1 echo. For phasediff (P) we always emit a single phasediff regardless of
// echo. For magnitude we emit "magnitude<N>" when multi-echo, else "magnitude".
static void reproinResolveSuffixForFmap(const struct TReproinSpec *spec,
										int echoNum, bool isMultiEcho,
										char *out, size_t cap) {
	if (strcmp(spec->suffix, "magnitude") == 0 && isMultiEcho && echoNum >= 1) {
		snprintf(out, cap, "magnitude%d", echoNum);
	} else {
		snprintf(out, cap, "%s", spec->suffix);
	}
}

bool reproinBuildFilename(const struct TReproinSpec *spec,
						  const char *bidsSubject,
						  const char *bidsSession,
						  int echoNum,
						  bool isMultiEcho,
						  char *outname,
						  size_t cap) {
	if (!spec->valid || spec->datatype[0] == '\0')
		return false;
	if (bidsSubject == NULL || bidsSubject[0] == '\0')
		return false; // caller must resolve subject (PatientID fallback handled there)
	const char *sub = bidsSubject;
	// Empty bidsSession means: omit ses- segment entirely (heudiconv behaviour
	// for studies without an explicit _ses- marker).
	bool hasSes = (bidsSession != NULL && bidsSession[0] != '\0');

	char sep[2] = {kReproinPathSep, '\0'};
	char subSeg[kReproinStr];
	snprintf(subSeg, sizeof(subSeg), "sub-%s", sub);
	char sesSeg[kReproinStr] = "";
	if (hasSes)
		snprintf(sesSeg, sizeof(sesSeg), "ses-%s", bidsSession);

	// <subDir>[/ses-Y]/<datatype>/<sub>[_<ses>]...
	char head[PATH_MAX];
	if (spec->isDerived) {
		if (hasSes)
			snprintf(head, sizeof(head), "derivatives%sscanner%s%s%s%s%s%s%s%s_%s",
					 sep, sep, subSeg, sep, sesSeg, sep, spec->datatype, sep,
					 subSeg, sesSeg);
		else
			snprintf(head, sizeof(head), "derivatives%sscanner%s%s%s%s%s%s",
					 sep, sep, subSeg, sep, spec->datatype, sep, subSeg);
	} else {
		if (hasSes)
			snprintf(head, sizeof(head), "%s%s%s%s%s%s%s_%s",
					 subSeg, sep, sesSeg, sep, spec->datatype, sep, subSeg, sesSeg);
		else
			snprintf(head, sizeof(head), "%s%s%s%s%s",
					 subSeg, sep, spec->datatype, sep, subSeg);
	}

	char tail[PATH_MAX] = {""};
	// BIDS only allows _task- on func/sbref. Order: task, acq, rec, dir, bids, run, echo, suffix.
	bool isFunc = (strcmp(spec->datatype, "func") == 0);
	if (isFunc || spec->isSbref) {
		if (spec->task[0] != '\0') {
			char buf[kReproinStr];
			snprintf(buf, sizeof(buf), "_task-%s", spec->task);
			strncat(tail, buf, sizeof(tail) - strlen(tail) - 1);
		} else if (isFunc) {
			// BIDS requires _task- on func; default to "rest".
			strncat(tail, "_task-rest", sizeof(tail) - strlen(tail) - 1);
		}
	}
	if (spec->acq[0] != '\0') {
		char buf[kReproinStr];
		snprintf(buf, sizeof(buf), "_acq-%s", spec->acq);
		strncat(tail, buf, sizeof(tail) - strlen(tail) - 1);
	}
	if (spec->rec[0] != '\0') {
		char buf[kReproinStr];
		snprintf(buf, sizeof(buf), "_rec-%s", spec->rec);
		strncat(tail, buf, sizeof(tail) - strlen(tail) - 1);
	}
	if (spec->dir[0] != '\0') {
		char buf[kReproinStr];
		snprintf(buf, sizeof(buf), "_dir-%s", spec->dir);
		strncat(tail, buf, sizeof(tail) - strlen(tail) - 1);
	}
	if (spec->bids[0] != '\0') {
		char buf[kReproinStr + 4];
		snprintf(buf, sizeof(buf), "_%s", spec->bids);
		strncat(tail, buf, sizeof(tail) - strlen(tail) - 1);
	}
	if (spec->run[0] != '\0') {
		char buf[32];
		// Zero-pad numeric run to 2 digits; fall back to literal otherwise.
		int runI = 0;
		char *endp = NULL;
		runI = (int)strtol(spec->run, &endp, 10);
		if (endp != NULL && *endp == '\0' && runI > 0)
			snprintf(buf, sizeof(buf), "_run-%02d", runI);
		else
			snprintf(buf, sizeof(buf), "_run-%s", spec->run);
		strncat(tail, buf, sizeof(tail) - strlen(tail) - 1);
	}
	// Echo handling for func / dwi multi-echo. For fmap GRE multi-echo we
	// fold the echo into the suffix (magnitude1/magnitude2) rather than
	// emitting a separate _echo-N entity.
	bool foldEchoIntoSuffix = (strcmp(spec->datatype, "fmap") == 0);
	if (isMultiEcho && echoNum >= 1 && !foldEchoIntoSuffix) {
		char buf[16];
		snprintf(buf, sizeof(buf), "_echo-%d", echoNum);
		strncat(tail, buf, sizeof(tail) - strlen(tail) - 1);
	}
	// Resolve the suffix (handles fmap magnitude/magnitudeN selection).
	char suffix[32];
	if (foldEchoIntoSuffix)
		reproinResolveSuffixForFmap(spec, echoNum, isMultiEcho, suffix, sizeof(suffix));
	else
		snprintf(suffix, sizeof(suffix), "%s", spec->suffix);
	if (suffix[0] != '\0') {
		char buf[64];
		snprintf(buf, sizeof(buf), "_%s", suffix);
		strncat(tail, buf, sizeof(tail) - strlen(tail) - 1);
	}
	snprintf(outname, cap, "%s%s", head, tail);
	return true;
}
