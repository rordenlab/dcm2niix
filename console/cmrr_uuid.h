#ifndef CMRR_UUID_H
#define CMRR_UUID_H

#include "cJSON.h"
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#define kCMRRMeasurementUuidLength 36
#define kCMRRMeasurementUuidBufferLength (kCMRRMeasurementUuidLength + 1)
#define kCMRRMeasurementUuidJsonKey "CMRRMeasurementUUID"

typedef enum {
	kCmrrUuidNone = 0,
	kCmrrUuidResolved,
	kCmrrUuidConflict,
} TCmrrUuidResolution;

// Parse a canonical 8-4-4-4-12 UUID at the start of `text`. CMRR writes
// lowercase UUIDs, but accept uppercase hexadecimal and normalize it so the
// same measurement always receives the same JSON value. Reject a nil UUID and
// require a token boundary so arbitrary WIP text is never truncated into an ID.
static inline bool cmrrParseCanonicalUuid(const char *text, char out[kCMRRMeasurementUuidBufferLength]) {
	if (out == NULL)
		return false;
	out[0] = '\0';
	if (text == NULL)
		return false;
	bool nonzero = false;
	for (int i = 0; i < kCMRRMeasurementUuidLength; i++) {
		bool hyphen = (i == 8) || (i == 13) || (i == 18) || (i == 23);
		unsigned char c = (unsigned char)text[i];
		if (c == '\0')
			return false;
		if (hyphen) {
			if (c != '-')
				return false;
			out[i] = '-';
			continue;
		}
		if (!isxdigit(c))
			return false;
		out[i] = (char)tolower(c);
		if (c != '0')
			nonzero = true;
	}
	char boundary = text[kCMRRMeasurementUuidLength];
	if (!((boundary == '\0') || isspace((unsigned char)boundary) || ((boundary == '|') && (text[kCMRRMeasurementUuidLength + 1] == '|')))) {
		out[0] = '\0';
		return false;
	}
	if (!nonzero) {
		out[0] = '\0';
		return false;
	}
	out[kCMRRMeasurementUuidLength] = '\0';
	return true;
}

static inline bool cmrrPulseSequenceDetails(const char *text) {
	if (text == NULL)
		return false;
	for (const char *p = text; *p != '\0'; p++) {
		if ((tolower((unsigned char)p[0]) == 'c') &&
			(tolower((unsigned char)p[1]) == 'm') &&
			(tolower((unsigned char)p[2]) == 'r') &&
			(tolower((unsigned char)p[3]) == 'r') &&
			((p == text) || !isalnum((unsigned char)p[-1])) &&
			((p[4] == '\0') || !isalnum((unsigned char)p[4])))
			return true;
	}
	return false;
}

// The leading UUID in sWipMemBlock.tFree is CMRR measurement provenance only
// when the Phoenix protocol identifies a CMRR sequence. Other WIP sequences may
// use tFree for unrelated text and must not receive this field.
static inline bool cmrrUuidFromWipMemBlock(const char *wipMemBlock, const char *pulseSequenceDetails, char out[kCMRRMeasurementUuidBufferLength]) {
	if (!cmrrPulseSequenceDetails(pulseSequenceDetails)) {
		if (out != NULL)
			out[0] = '\0';
		return false;
	}
	return cmrrParseCanonicalUuid(wipMemBlock, out);
}

// CMRR PULS/RESP/Info logs identify the measurement with an exact
// `UUID = <uuid>` header. The caller tokenizes the line and supplies the key,
// separator, and value separately so similarly named headers are not accepted.
static inline bool cmrrUuidFromPayloadHeader(const char *key, const char *separator, const char *value, char out[kCMRRMeasurementUuidBufferLength]) {
	if ((key == NULL) || (separator == NULL) || (strcmp(key, "UUID") != 0) || (strcmp(separator, "=") != 0)) {
		if (out != NULL)
			out[0] = '\0';
		return false;
	}
	return cmrrParseCanonicalUuid(value, out);
}

// Resolve the Phoenix and embedded-log identities. One source is sufficient;
// when both are present they must agree. payloadConflict records disagreement
// among PULS/RESP/Info log headers before this final comparison.
static inline TCmrrUuidResolution cmrrResolveMeasurementUuid(const char *phoenixUuid, const char *payloadUuid, bool payloadConflict, char out[kCMRRMeasurementUuidBufferLength]) {
	if (out == NULL)
		return kCmrrUuidNone;
	out[0] = '\0';
	if (payloadConflict)
		return kCmrrUuidConflict;
	bool hasPhoenix = (phoenixUuid != NULL) && (phoenixUuid[0] != '\0');
	bool hasPayload = (payloadUuid != NULL) && (payloadUuid[0] != '\0');
	if (hasPhoenix && hasPayload && (strcmp(phoenixUuid, payloadUuid) != 0))
		return kCmrrUuidConflict;
	if (hasPhoenix)
		strcpy(out, phoenixUuid);
	else if (hasPayload)
		strcpy(out, payloadUuid);
	else
		return kCmrrUuidNone;
	return kCmrrUuidResolved;
}

static inline void cmrrMergePayloadUuid(const char *candidate, char payloadUuid[kCMRRMeasurementUuidBufferLength], bool *payloadConflict) {
	if ((candidate == NULL) || (candidate[0] == '\0') || (payloadUuid == NULL) || (payloadConflict == NULL))
		return;
	if (payloadUuid[0] == '\0') {
		strcpy(payloadUuid, candidate);
		return;
	}
	if (strcmp(payloadUuid, candidate) != 0)
		*payloadConflict = true;
}

// The measurement UUID can provide a persistent link to source DICOM data.
// Emit it only under `-ba n`; both `-ba y` and `-ba o` suppress it.
static inline const char *cmrrUuidForOutput(const char *uuid, bool isAnonymizeBIDS, bool isOmitPiiBIDS) {
	if (isAnonymizeBIDS || isOmitPiiBIDS || (uuid == NULL) || (uuid[0] == '\0'))
		return NULL;
	return uuid;
}

static inline bool cmrrAddMeasurementUuidToJson(cJSON *root, const char *uuid) {
	if ((uuid == NULL) || (uuid[0] == '\0'))
		return true;
	return cJSON_AddStringToObject(root, kCMRRMeasurementUuidJsonKey, uuid) != NULL;
}

#endif
