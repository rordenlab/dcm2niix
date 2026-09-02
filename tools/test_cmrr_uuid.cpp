#include "cmrr_uuid.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool condition, const char *message) {
	if (condition)
		return;
	fprintf(stderr, "FAIL: %s\n", message);
	exit(EXIT_FAILURE);
}

static cJSON *makePhysioJson(const char *label, const char *uuid) {
	cJSON *root = cJSON_CreateObject();
	require(root != NULL, "create physio JSON");
	cJSON *columns = cJSON_AddArrayToObject(root, "Columns");
	require(columns != NULL, "create Columns");
	cJSON_AddItemToArray(columns, cJSON_CreateString(label));
	cJSON_AddItemToArray(columns, cJSON_CreateString("trigger"));
	require(cJSON_GetArraySize(columns) == 2, "add physio columns");
	require(cJSON_AddStringToObject(root, "PhysioType", "generic") != NULL, "add PhysioType");
	require(cJSON_AddNumberToObject(root, "SamplingFrequency", 200.0) != NULL, "add SamplingFrequency");
	require(cJSON_AddNumberToObject(root, "StartTime", -22.917) != NULL, "add StartTime");
	require(cmrrAddMeasurementUuidToJson(root, uuid), "add CMRRMeasurementUUID");
	return root;
}

static cJSON *makeImagingJson(const char *wipMemBlock, const char *pulseSequenceDetails, bool anonymize, bool omitPii) {
	cJSON *root = cJSON_CreateObject();
	require(root != NULL, "create imaging JSON");
	require(cJSON_AddStringToObject(root, "WipMemBlock", wipMemBlock) != NULL, "preserve WipMemBlock");
	char uuid[kCMRRMeasurementUuidBufferLength];
	if (cmrrUuidFromWipMemBlock(wipMemBlock, pulseSequenceDetails, uuid))
		require(cmrrAddMeasurementUuidToJson(root, cmrrUuidForOutput(uuid, anonymize, omitPii)), "add imaging CMRRMeasurementUUID");
	return root;
}

int main() {
	const char *uuidA = "12345678-1234-4abc-8def-1234567890ab";
	const char *uuidB = "abcdef01-2345-4678-9abc-def012345678";
	char parsed[kCMRRMeasurementUuidBufferLength];
	require(cmrrParseCanonicalUuid(uuidA, parsed) && strcmp(parsed, uuidA) == 0, "valid UUID extraction");
	require(cmrrParseCanonicalUuid("12345678-1234-4ABC-8DEF-1234567890AB", parsed) && strcmp(parsed, uuidA) == 0, "UUID lowercase normalization");
	require(!cmrrParseCanonicalUuid("not-a-uuid", parsed), "malformed UUID rejected");
	require(!cmrrParseCanonicalUuid("00000000-0000-0000-0000-000000000000", parsed), "nil UUID rejected");
	require(!cmrrParseCanonicalUuid(NULL, parsed), "missing UUID rejected");

	const char *wipA = "12345678-1234-4abc-8def-1234567890ab||Sequence: R017";
	require(cmrrUuidFromWipMemBlock(wipA, "%CustomerSeq%\\cmrr_mbep2d_bold", parsed) && strcmp(parsed, uuidA) == 0, "CMRR imaging WIP extraction");
	require(!cmrrUuidFromWipMemBlock(wipA, "ep2d_bold", parsed), "non-CMRR WIP excluded");
	require(!cmrrUuidFromWipMemBlock(wipA, "notcmrr_sequence", parsed), "CMRR substring excluded");
	require(cmrrUuidFromPayloadHeader("UUID", "=", uuidA, parsed) && strcmp(parsed, uuidA) == 0, "CMRR payload UUID extraction");
	require(!cmrrUuidFromPayloadHeader("OtherUUID", "=", uuidA, parsed), "non-authoritative payload header excluded");
	require(!cmrrUuidFromPayloadHeader("UUID", ":", uuidA, parsed), "malformed payload header excluded");

	// Three echoes from one measurement resolve identically; a repeated
	// acquisition keeps its distinct scanner UUID.
	for (int echo = 1; echo <= 3; echo++) {
		(void)echo;
		require(cmrrUuidFromWipMemBlock(wipA, "cmrr_mbep2d_bold", parsed) && strcmp(parsed, uuidA) == 0, "multi-echo UUID stability");
	}
	char parsedB[kCMRRMeasurementUuidBufferLength];
	require(cmrrParseCanonicalUuid(uuidB, parsedB) && strcmp(parsed, parsedB) != 0, "different measurements remain distinct");

	char resolved[kCMRRMeasurementUuidBufferLength];
	require(cmrrResolveMeasurementUuid(uuidA, uuidA, false, resolved) == kCmrrUuidResolved && strcmp(resolved, uuidA) == 0, "matching Phoenix/log UUIDs accepted");
	require(cmrrResolveMeasurementUuid(uuidA, NULL, false, resolved) == kCmrrUuidResolved && strcmp(resolved, uuidA) == 0, "Phoenix-only UUID accepted");
	require(cmrrResolveMeasurementUuid(NULL, uuidA, false, resolved) == kCmrrUuidResolved && strcmp(resolved, uuidA) == 0, "payload-only UUID accepted");
	require(cmrrResolveMeasurementUuid(uuidA, uuidB, false, resolved) == kCmrrUuidConflict && resolved[0] == '\0', "Phoenix/log conflict rejected");
	require(cmrrResolveMeasurementUuid(uuidA, uuidA, true, resolved) == kCmrrUuidConflict && resolved[0] == '\0', "payload-internal conflict rejected");
	require(cmrrResolveMeasurementUuid(NULL, NULL, false, resolved) == kCmrrUuidNone, "absent UUID stays absent");

	require(cmrrUuidForOutput(uuidA, false, false) == uuidA, "-ba n retains UUID");
	require(cmrrUuidForOutput(uuidA, false, true) == NULL, "-ba o suppresses UUID");
	require(cmrrUuidForOutput(uuidA, true, false) == NULL, "-ba y suppresses UUID");
	cJSON *imaging = makeImagingJson(wipA, "%CustomerSeq%\\cmrr_mbep2d_bold", false, false);
	require(cJSON_IsString(cJSON_GetObjectItem(imaging, "WipMemBlock")), "imaging WipMemBlock preserved");
	require(strcmp(cJSON_GetObjectItem(imaging, kCMRRMeasurementUuidJsonKey)->valuestring, uuidA) == 0, "imaging JSON UUID");
	cJSON_Delete(imaging);
	cJSON *omitPiiImaging = makeImagingJson(wipA, "%CustomerSeq%\\cmrr_mbep2d_bold", false, true);
	require(cJSON_GetObjectItem(omitPiiImaging, kCMRRMeasurementUuidJsonKey) == NULL, "-ba o imaging UUID absent");
	cJSON_Delete(omitPiiImaging);
	cJSON *anonymizedImaging = makeImagingJson(wipA, "%CustomerSeq%\\cmrr_mbep2d_bold", true, false);
	require(cJSON_GetObjectItem(anonymizedImaging, kCMRRMeasurementUuidJsonKey) == NULL, "-ba y imaging UUID absent");
	cJSON_Delete(anonymizedImaging);
	cJSON *nonCmrrImaging = makeImagingJson(wipA, "ep2d_bold", false, false);
	require(cJSON_GetObjectItem(nonCmrrImaging, kCMRRMeasurementUuidJsonKey) == NULL, "non-CMRR imaging field absent");
	cJSON_Delete(nonCmrrImaging);

	cJSON *cardiac = makePhysioJson("cardiac", uuidA);
	cJSON *respiratory = makePhysioJson("respiratory", uuidA);
	cJSON *physioRoots[2] = {cardiac, respiratory};
	for (cJSON *root : physioRoots) {
		cJSON *identity = cJSON_GetObjectItem(root, kCMRRMeasurementUuidJsonKey);
		require(cJSON_IsString(identity) && strcmp(identity->valuestring, uuidA) == 0, "physio JSON UUID");
		require(cJSON_IsString(cJSON_GetObjectItem(root, "PhysioType")), "PhysioType preserved");
		require(cJSON_IsArray(cJSON_GetObjectItem(root, "Columns")), "Columns preserved");
		require(cJSON_IsNumber(cJSON_GetObjectItem(root, "SamplingFrequency")), "SamplingFrequency preserved");
		require(cJSON_IsNumber(cJSON_GetObjectItem(root, "StartTime")), "StartTime preserved");
	}
	cJSON_Delete(cardiac);
	cJSON_Delete(respiratory);

	cJSON *withoutUuid = makePhysioJson("cardiac", NULL);
	require(cJSON_GetObjectItem(withoutUuid, kCMRRMeasurementUuidJsonKey) == NULL, "field absent without UUID");
	cJSON_Delete(withoutUuid);

	printf("CMRR UUID tests passed\n");
	return EXIT_SUCCESS;
}
