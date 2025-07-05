#ifndef NII_JSON_META_H
#define NII_JSON_META_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// VR type enumeration for efficient storage
typedef enum {
    VR_UNKNOWN = 0,
    VR_AE, VR_AS, VR_AT, VR_CS, VR_DA, VR_DS, VR_DT,
    VR_FL, VR_FD, VR_IS, VR_LO, VR_LT, VR_OB, VR_OD,
    VR_OF, VR_OL, VR_OV, VR_OW, VR_PN, VR_SH, VR_SL,
    VR_SQ, VR_SS, VR_ST, VR_SV, VR_TM, VR_UC, VR_UI,
    VR_UL, VR_UN, VR_UR, VR_US, VR_UT, VR_UV
} TVRCode;

// DICOM field mapping structure
struct TDICOMFieldMapping {
    uint32_t tag;           // Combined group/element (group << 16 | element)
    const char* fieldName;  // Human-readable JSON field name
    TVRCode vrCode;         // Expected VR type
    bool isArray;           // Multiple values (DS with backslash separator)
    bool isSequence;        // SQ type - nested structure
    const char* description; // Human description for documentation
};

// Memory-efficient tag storage
struct TDICOMTagLite {
    uint32_t tag;           // Combined group/element
    TVRCode vrCode;         // VR type as enum
    uint32_t length;        // Data length
    uint32_t dataOffset;    // Offset in shared data buffer
    bool isPrivate;         // Private tag flag
};

// Metadata collection container
struct TDICOMMetadataCollector {
    struct TDICOMTagLite* tags;
    char* dataBuffer;       // Shared buffer for all tag data
    uint32_t tagCount;
    uint32_t tagCapacity;
    uint32_t bufferSize;
    uint32_t bufferUsed;
    uint32_t memoryLimitMB; // Configurable memory limit
    bool memoryExceeded;    // Flag when limit hit
    bool includePrivate;
    bool includeSequences;
    bool includeUnknown;
};

// JSON generation options
struct TJSONMetadataOptions {
    bool prettyPrint;
    bool includePrivate;
    bool includeSequences;
    bool includeUnknown;
    bool separateFile;      // Write to separate .json file
    uint32_t memoryLimitMB;
    char filterPattern[256]; // Tag filter regex
};

// Core function declarations
struct TDICOMMetadataCollector* initMetadataCollector(uint32_t memoryLimitMB);
void freeMetadataCollector(struct TDICOMMetadataCollector* collector);
bool addTagToCollector(struct TDICOMMetadataCollector* collector, 
                       uint32_t tag, TVRCode vr, const char* data, uint32_t length);
const struct TDICOMFieldMapping* findFieldMapping(uint32_t tag);
TVRCode stringToVRCode(const char* vr);
const char* vrCodeToString(TVRCode vr);

// JSON generation functions
int generateHumanReadableJSON(const struct TDICOMMetadataCollector* collector,
                             const struct TJSONMetadataOptions* opts,
                             const char* outputPath);
int mergeMetadataWithBIDSJSON(const struct TDICOMMetadataCollector* collector,
                             const struct TJSONMetadataOptions* opts,
                             const char* bidsJsonPath);
void writeJSONField(FILE* output, const char* fieldName, const char* value,
                   TVRCode vr, bool isArray, bool prettyPrint, int indent);

// Value formatting functions
void writeNumericValue(FILE* output, const char* value, TVRCode vr, bool isArray);
void writeFloatValue(FILE* output, const char* value, TVRCode vr, bool isArray);
void writeDateValue(FILE* output, const char* value);
void writeTimeValue(FILE* output, const char* value);
void writePersonNameValue(FILE* output, const char* value);
void writeStringValue(FILE* output, const char* value);
void writeStringArray(FILE* output, const char* value);
void writeBinaryDataInfo(FILE* output, const char* value, uint32_t length);

// DICOM parsing functions
int parseDicomBuffer(const unsigned char* buffer, size_t bufferSize, struct TDICOMMetadataCollector* collector);

// Utility functions
char* escapeJSONString(const char* input);
uint16_t getGroupFromTag(uint32_t tag);
uint16_t getElementFromTag(uint32_t tag);
uint32_t makeTag(uint16_t group, uint16_t element);

#ifdef __cplusplus
}
#endif

#endif // NII_JSON_META_H