#include "nii_json_meta.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Comprehensive DICOM field mappings
static const struct TDICOMFieldMapping DICOM_FIELD_MAPPINGS[] = {
    // File Meta Information
    {0x00020000, "FileMetaInformationGroupLength", VR_UL, false, false, "File Meta Information Group Length"},
    {0x00020001, "FileMetaInformationVersion", VR_OB, false, false, "File Meta Information Version"},
    {0x00020002, "MediaStorageSOPClassUID", VR_UI, false, false, "Media Storage SOP Class UID"},
    {0x00020003, "MediaStorageSOPInstanceUID", VR_UI, false, false, "Media Storage SOP Instance UID"},
    {0x00020010, "TransferSyntaxUID", VR_UI, false, false, "Transfer Syntax UID"},
    {0x00020012, "ImplementationClassUID", VR_UI, false, false, "Implementation Class UID"},
    {0x00020013, "ImplementationVersionName", VR_SH, false, false, "Implementation Version Name"},
    
    // Data Set Identification
    {0x00080005, "SpecificCharacterSet", VR_CS, false, false, "Specific Character Set"},
    {0x00080012, "InstanceCreationDate", VR_DA, false, false, "Instance Creation Date"},
    {0x00080013, "InstanceCreationTime", VR_TM, false, false, "Instance Creation Time"},
    {0x00080023, "ContentDate", VR_DA, false, false, "Content Date"},
    {0x0008002A, "AcquisitionDateTime", VR_DT, false, false, "Acquisition DateTime"},
    {0x00080033, "ContentTime", VR_TM, false, false, "Content Time"},
    {0x00080070, "Manufacturer", VR_LO, false, false, "Manufacturer"},
    {0x00080080, "InstitutionName", VR_LO, false, false, "Institution Name"},
    {0x00080081, "InstitutionAddress", VR_ST, false, false, "Institution Address"},
    {0x00081010, "StationName", VR_SH, false, false, "Station Name"},
    {0x00081090, "ManufacturersModelName", VR_LO, false, false, "Manufacturer's Model Name"},
    
    // Patient Information Module
    {0x00100010, "PatientName", VR_PN, false, false, "Patient's Name"},
    {0x00100020, "PatientID", VR_LO, false, false, "Patient ID"},
    {0x00100030, "PatientBirthDate", VR_DA, false, false, "Patient's Birth Date"},
    {0x00100040, "PatientSex", VR_CS, false, false, "Patient's Sex"},
    {0x00101010, "PatientAge", VR_AS, false, false, "Patient's Age"},
    {0x00101020, "PatientSize", VR_DS, false, false, "Patient's Size"},
    {0x00101030, "PatientWeight", VR_DS, false, false, "Patient's Weight"},
    
    // General Study Module
    {0x0020000D, "StudyInstanceUID", VR_UI, false, false, "Study Instance UID"},
    {0x00080020, "StudyDate", VR_DA, false, false, "Study Date"},
    {0x00080030, "StudyTime", VR_TM, false, false, "Study Time"},
    {0x00080090, "ReferringPhysicianName", VR_PN, false, false, "Referring Physician's Name"},
    {0x00200010, "StudyID", VR_SH, false, false, "Study ID"},
    {0x00080050, "AccessionNumber", VR_SH, false, false, "Accession Number"},
    {0x00081030, "StudyDescription", VR_LO, false, false, "Study Description"},
    
    // General Series Module
    {0x0020000E, "SeriesInstanceUID", VR_UI, false, false, "Series Instance UID"},
    {0x00200011, "SeriesNumber", VR_IS, false, false, "Series Number"},
    {0x00080021, "SeriesDate", VR_DA, false, false, "Series Date"},
    {0x00080031, "SeriesTime", VR_TM, false, false, "Series Time"},
    {0x00080060, "Modality", VR_CS, false, false, "Modality"},
    {0x0008103E, "SeriesDescription", VR_LO, false, false, "Series Description"},
    {0x00180015, "BodyPartExamined", VR_CS, false, false, "Body Part Examined"},
    {0x00181030, "ProtocolName", VR_LO, false, false, "Protocol Name"},
    
    // General Image Module  
    {0x00200013, "InstanceNumber", VR_IS, false, false, "Instance Number"},
    {0x00080008, "ImageType", VR_CS, true, false, "Image Type"},
    {0x00080016, "SOPClassUID", VR_UI, false, false, "SOP Class UID"},
    {0x00080018, "SOPInstanceUID", VR_UI, false, false, "SOP Instance UID"},
    {0x00200032, "ImagePositionPatient", VR_DS, true, false, "Image Position (Patient)"},
    {0x00200037, "ImageOrientationPatient", VR_DS, true, false, "Image Orientation (Patient)"},
    {0x00200052, "frameOfReferenceUID", VR_UI, false, false, "Frame of Reference UID"},
    {0x00181164, "imagerPixelSpacing", VR_DS, true, false, "Imager Pixel Spacing"},
    
    // General Equipment Module
    {0x00080070, "Manufacturer", VR_LO, false, false, "Manufacturer"},
    {0x00081010, "StationName", VR_SH, false, false, "Station Name"},
    {0x00081090, "ManufacturersModelName", VR_LO, false, false, "Manufacturer's Model Name"},
    {0x00181000, "deviceSerialNumber", VR_LO, false, false, "Device Serial Number"},
    {0x00181020, "softwareVersions", VR_LO, true, false, "Software Versions"},
    
    // MR Image Module
    {0x00180050, "sliceThickness", VR_DS, false, false, "Slice Thickness"},
    {0x00180080, "repetitionTime", VR_DS, false, false, "Repetition Time"},
    {0x00180081, "echoTime", VR_DS, false, false, "Echo Time"},
    {0x00180082, "inversionTime", VR_DS, false, false, "Inversion Time"},
    {0x00180084, "imagingFrequency", VR_DS, false, false, "Imaging Frequency"},
    {0x00180085, "imagedNucleus", VR_SH, false, false, "Imaged Nucleus"},
    {0x00180086, "echoNumbers", VR_IS, true, false, "Echo Number(s)"},
    {0x00180087, "magneticFieldStrength", VR_DS, false, false, "Magnetic Field Strength"},
    {0x00180088, "SpacingBetweenSlices", VR_DS, false, false, "Spacing Between Slices"},
    {0x00180089, "numberOfPhaseEncodingSteps", VR_IS, false, false, "Number of Phase Encoding Steps"},
    {0x00180091, "echoTrainLength", VR_IS, false, false, "Echo Train Length"},
    {0x00180093, "percentSampling", VR_DS, false, false, "Percent Sampling"},
    {0x00180094, "percentPhaseFieldOfView", VR_DS, false, false, "Percent Phase Field of View"},
    {0x00180095, "pixelBandwidth", VR_DS, false, false, "Pixel Bandwidth"},
    {0x00181314, "flipAngle", VR_DS, false, false, "Flip Angle"},
    {0x00181310, "acquisitionMatrix", VR_US, true, false, "Acquisition Matrix"},
    {0x00181312, "inPlanePhaseEncodingDirection", VR_CS, false, false, "In-plane Phase Encoding Direction"},
    
    // Additional MR/Diffusion Fields
    {0x00189087, "b_value", VR_DS, false, false, "B Value"},
    {0x00189089, "diffusionGradientOrientation", VR_DS, true, false, "Diffusion Gradient Orientation"},
    {0x00180083, "numberOfAverages", VR_IS, false, false, "Number of Averages"},
    {0x00189081, "partialFourier", VR_CS, false, false, "Partial Fourier"},
    {0x00189082, "partialFourierDirection", VR_CS, false, false, "Partial Fourier Direction"},
    {0x00189075, "diffusionDirectionality", VR_CS, false, false, "Diffusion Directionality"},
    
    // Image Pixel Module
    {0x00280002, "samplesPerPixel", VR_US, false, false, "Samples per Pixel"},
    {0x00280004, "photometricInterpretation", VR_CS, false, false, "Photometric Interpretation"},
    {0x00280010, "rows", VR_US, false, false, "Rows"},
    {0x00280011, "columns", VR_US, false, false, "Columns"},
    {0x00280030, "pixelSpacing", VR_DS, true, false, "Pixel Spacing"},
    {0x00280100, "bitsAllocated", VR_US, false, false, "Bits Allocated"},
    {0x00280101, "bitsStored", VR_US, false, false, "Bits Stored"},
    {0x00280102, "highBit", VR_US, false, false, "High Bit"},
    {0x00280103, "pixelRepresentation", VR_US, false, false, "Pixel Representation"},
    {0x00281050, "windowCenter", VR_DS, true, false, "Window Center"},
    {0x00281051, "windowWidth", VR_DS, true, false, "Window Width"},
    {0x00281052, "rescaleIntercept", VR_DS, false, false, "Rescale Intercept"},
    {0x00281053, "rescaleSlope", VR_DS, false, false, "Rescale Slope"},
    {0x00281054, "rescaleType", VR_LO, false, false, "Rescale Type"},
    {0x00280008, "numberOfFrames", VR_IS, false, false, "Number of Frames"},
    {0x20500020, "presentationLUTShape", VR_CS, false, false, "Presentation LUT Shape"},
    {0x00282110, "lossyImageCompression", VR_CS, false, false, "Lossy Image Compression"},
    {0x00280301, "burnedInAnnotation", VR_CS, false, false, "Burned In Annotation"},
    {0x00089207, "volumeBasedCalculationTechnique", VR_CS, false, false, "Volume Based Calculation Technique"},
    {0x00089206, "volumetricProperties", VR_CS, false, false, "Volumetric Properties"},
    
    // Pixel Data
    {0x7FE00010, "pixelData", VR_OW, false, false, "Pixel Data"},
    
    // Common Sequence Tags
    {0x00081140, "referencedImageSequence", VR_SQ, false, true, "Referenced Image Sequence"},
    {0x00540220, "viewCodeSequence", VR_SQ, false, true, "View Code Sequence"},
    
    // Sentinel - MUST BE LAST
    {0x00000000, NULL, VR_UNKNOWN, false, false, NULL}
};

// Core function implementations
struct TDICOMMetadataCollector* initMetadataCollector(uint32_t memoryLimitMB) {
    struct TDICOMMetadataCollector* collector = 
        (struct TDICOMMetadataCollector*)calloc(1, sizeof(struct TDICOMMetadataCollector));
    
    if (!collector) return NULL;
    
    collector->memoryLimitMB = memoryLimitMB;
    collector->bufferSize = memoryLimitMB * 1024 * 1024;
    collector->dataBuffer = (char*)malloc(collector->bufferSize);
    collector->tagCapacity = 1000; // Initial capacity
    collector->tags = (struct TDICOMTagLite*)calloc(collector->tagCapacity, sizeof(struct TDICOMTagLite));
    
    if (!collector->dataBuffer || !collector->tags) {
        freeMetadataCollector(collector);
        return NULL;
    }
    
    return collector;
}

void freeMetadataCollector(struct TDICOMMetadataCollector* collector) {
    if (!collector) return;
    
    if (collector->dataBuffer) free(collector->dataBuffer);
    if (collector->tags) free(collector->tags);
    free(collector);
}

bool addTagToCollector(struct TDICOMMetadataCollector* collector, 
                       uint32_t tag, TVRCode vr, const char* data, uint32_t length) {
    if (!collector || !data || length == 0) return false;
    
    // Check memory limit
    if (collector->bufferUsed + length > collector->bufferSize) {
        collector->memoryExceeded = true;
        return false;
    }
    
    // Expand tag array if needed
    if (collector->tagCount >= collector->tagCapacity) {
        collector->tagCapacity *= 2;
        collector->tags = (struct TDICOMTagLite*)realloc(
            collector->tags, 
            collector->tagCapacity * sizeof(struct TDICOMTagLite)
        );
        if (!collector->tags) return false;
    }
    
    // Add tag data to buffer
    uint32_t dataOffset = collector->bufferUsed;
    memcpy(collector->dataBuffer + dataOffset, data, length);
    collector->bufferUsed += length;
    
    // Add tag info
    struct TDICOMTagLite* newTag = &collector->tags[collector->tagCount];
    newTag->tag = tag;
    newTag->vrCode = vr;
    newTag->length = length;
    newTag->dataOffset = dataOffset;
    newTag->isPrivate = (getGroupFromTag(tag) % 2 == 1);
    
    collector->tagCount++;
    return true;
}

const struct TDICOMFieldMapping* findFieldMapping(uint32_t tag) {
    for (int i = 0; DICOM_FIELD_MAPPINGS[i].fieldName != NULL; i++) {
        if (DICOM_FIELD_MAPPINGS[i].tag == tag) {
            return &DICOM_FIELD_MAPPINGS[i];
        }
    }
    return NULL;
}

TVRCode stringToVRCode(const char* vr) {
    if (!vr || strlen(vr) != 2) return VR_UNKNOWN;
    
    // Convert string VR to enum
    if (strcmp(vr, "AE") == 0) return VR_AE;
    if (strcmp(vr, "AS") == 0) return VR_AS;
    if (strcmp(vr, "AT") == 0) return VR_AT;
    if (strcmp(vr, "CS") == 0) return VR_CS;
    if (strcmp(vr, "DA") == 0) return VR_DA;
    if (strcmp(vr, "DS") == 0) return VR_DS;
    if (strcmp(vr, "DT") == 0) return VR_DT;
    if (strcmp(vr, "FL") == 0) return VR_FL;
    if (strcmp(vr, "FD") == 0) return VR_FD;
    if (strcmp(vr, "IS") == 0) return VR_IS;
    if (strcmp(vr, "LO") == 0) return VR_LO;
    if (strcmp(vr, "LT") == 0) return VR_LT;
    if (strcmp(vr, "OB") == 0) return VR_OB;
    if (strcmp(vr, "OD") == 0) return VR_OD;
    if (strcmp(vr, "OF") == 0) return VR_OF;
    if (strcmp(vr, "OL") == 0) return VR_OL;
    if (strcmp(vr, "OV") == 0) return VR_OV;
    if (strcmp(vr, "OW") == 0) return VR_OW;
    if (strcmp(vr, "PN") == 0) return VR_PN;
    if (strcmp(vr, "SH") == 0) return VR_SH;
    if (strcmp(vr, "SL") == 0) return VR_SL;
    if (strcmp(vr, "SQ") == 0) return VR_SQ;
    if (strcmp(vr, "SS") == 0) return VR_SS;
    if (strcmp(vr, "ST") == 0) return VR_ST;
    if (strcmp(vr, "TM") == 0) return VR_TM;
    if (strcmp(vr, "UC") == 0) return VR_UC;
    if (strcmp(vr, "UI") == 0) return VR_UI;
    if (strcmp(vr, "UL") == 0) return VR_UL;
    if (strcmp(vr, "UN") == 0) return VR_UN;
    if (strcmp(vr, "UR") == 0) return VR_UR;
    if (strcmp(vr, "US") == 0) return VR_US;
    if (strcmp(vr, "UT") == 0) return VR_UT;
    
    return VR_UNKNOWN;
}

const char* vrCodeToString(TVRCode vr) {
    switch (vr) {
        case VR_AE: return "AE";
        case VR_AS: return "AS";
        case VR_AT: return "AT";
        case VR_CS: return "CS";
        case VR_DA: return "DA";
        case VR_DS: return "DS";
        case VR_DT: return "DT";
        case VR_FL: return "FL";
        case VR_FD: return "FD";
        case VR_IS: return "IS";
        case VR_LO: return "LO";
        case VR_LT: return "LT";
        case VR_OB: return "OB";
        case VR_OD: return "OD";
        case VR_OF: return "OF";
        case VR_OL: return "OL";
        case VR_OV: return "OV";
        case VR_OW: return "OW";
        case VR_PN: return "PN";
        case VR_SH: return "SH";
        case VR_SL: return "SL";
        case VR_SQ: return "SQ";
        case VR_SS: return "SS";
        case VR_ST: return "ST";
        case VR_TM: return "TM";
        case VR_UC: return "UC";
        case VR_UI: return "UI";
        case VR_UL: return "UL";
        case VR_UN: return "UN";
        case VR_UR: return "UR";
        case VR_US: return "US";
        case VR_UT: return "UT";
        default: return "UN";
    }
}

// Utility functions
uint16_t getGroupFromTag(uint32_t tag) {
    return (uint16_t)(tag >> 16);
}

uint16_t getElementFromTag(uint32_t tag) {
    return (uint16_t)(tag & 0xFFFF);
}

uint32_t makeTag(uint16_t group, uint16_t element) {
    return ((uint32_t)group << 16) | element;
}

char* escapeJSONString(const char* input) {
    if (!input) return NULL;
    
    size_t inputLen = strlen(input);
    size_t outputLen = inputLen * 2 + 1; // Worst case: every char needs escaping
    char* output = (char*)malloc(outputLen);
    if (!output) return NULL;
    
    size_t outputPos = 0;
    for (size_t i = 0; i < inputLen && outputPos < outputLen - 1; i++) {
        switch (input[i]) {
            case '"':
                output[outputPos++] = '\\';
                output[outputPos++] = '"';
                break;
            case '\\':
                output[outputPos++] = '\\';
                output[outputPos++] = '\\';
                break;
            case '\b':
                output[outputPos++] = '\\';
                output[outputPos++] = 'b';
                break;
            case '\f':
                output[outputPos++] = '\\';
                output[outputPos++] = 'f';
                break;
            case '\n':
                output[outputPos++] = '\\';
                output[outputPos++] = 'n';
                break;
            case '\r':
                output[outputPos++] = '\\';
                output[outputPos++] = 'r';
                break;
            case '\t':
                output[outputPos++] = '\\';
                output[outputPos++] = 't';
                break;
            default:
                if (input[i] < 0x20) {
                    // Control character - use unicode escape
                    snprintf(&output[outputPos], outputLen - outputPos, "\\u%04x", (unsigned char)input[i]);
                    outputPos += 6;
                } else {
                    output[outputPos++] = input[i];
                }
                break;
        }
    }
    output[outputPos] = '\0';
    return output;
}

// Value formatting functions
void writeStringValue(FILE* output, const char* value) {
    char* escaped = escapeJSONString(value);
    if (escaped) {
        fprintf(output, "\"%s\"", escaped);
        free(escaped);
    } else {
        fprintf(output, "\"\"");
    }
}

void writeNumericValue(FILE* output, const char* value, TVRCode vr, bool isArray) {
    if (!value) {
        fprintf(output, "null");
        return;
    }
    
    if (isArray) {
        fprintf(output, "[");
        char* valueCopy = strdup(value);
        if (valueCopy) {
            char* token = strtok(valueCopy, "\\");
            bool first = true;
            while (token) {
                if (!first) fprintf(output, ", ");
                long num = strtol(token, NULL, 10);
                fprintf(output, "%ld", num);
                first = false;
                token = strtok(NULL, "\\");
            }
            free(valueCopy);
        }
        fprintf(output, "]");
    } else {
        long num = strtol(value, NULL, 10);
        fprintf(output, "%ld", num);
    }
}

void writeFloatValue(FILE* output, const char* value, TVRCode vr, bool isArray) {
    if (!value) {
        fprintf(output, "null");
        return;
    }
    
    if (isArray) {
        fprintf(output, "[");
        char* valueCopy = strdup(value);
        if (valueCopy) {
            char* token = strtok(valueCopy, "\\");
            bool first = true;
            while (token) {
                if (!first) fprintf(output, ", ");
                double num = strtod(token, NULL);
                fprintf(output, "%.6g", num);
                first = false;
                token = strtok(NULL, "\\");
            }
            free(valueCopy);
        }
        fprintf(output, "]");
    } else {
        double num = strtod(value, NULL);
        fprintf(output, "%.6g", num);
    }
}

void writeDateValue(FILE* output, const char* value) {
    writeStringValue(output, value);
}

void writeTimeValue(FILE* output, const char* value) {
    writeStringValue(output, value);
}

void writePersonNameValue(FILE* output, const char* value) {
    writeStringValue(output, value);
}

void writeStringArray(FILE* output, const char* value) {
    if (!value) {
        fprintf(output, "[]");
        return;
    }
    
    fprintf(output, "[");
    char* valueCopy = strdup(value);
    if (valueCopy) {
        char* token = strtok(valueCopy, "\\");
        bool first = true;
        while (token) {
            if (!first) fprintf(output, ", ");
            writeStringValue(output, token);
            first = false;
            token = strtok(NULL, "\\");
        }
        free(valueCopy);
    }
    fprintf(output, "]");
}

void writeBinaryDataInfo(FILE* output, const char* value, uint32_t length) {
    fprintf(output, "\"[Binary data: %u bytes]\"", length);
}

// JSON generation functions
int generateHumanReadableJSON(const struct TDICOMMetadataCollector* collector,
                             const struct TJSONMetadataOptions* opts,
                             const char* outputPath) {
    if (!collector || !opts || !outputPath) return -1;
    
    FILE* output = fopen(outputPath, "w");
    if (!output) return -1;
    
    fprintf(output, "{");
    if (opts->prettyPrint) fprintf(output, "\n");
    
    bool isFirstField = true;
    int indent = 1;
    
    // Process all collected tags
    for (uint32_t i = 0; i < collector->tagCount; i++) {
        const struct TDICOMTagLite* tag = &collector->tags[i];
        const struct TDICOMFieldMapping* mapping = findFieldMapping(tag->tag);
        
        // Skip if no mapping found and unknown tags not requested
        if (!mapping && !opts->includeUnknown) continue;
        
        // Skip private tags if not requested
        if (tag->isPrivate && !opts->includePrivate) continue;
        
        // Skip sequences if not requested
        if (tag->vrCode == VR_SQ && !opts->includeSequences) continue;
        
        if (!isFirstField) {
            fprintf(output, ",");
            if (opts->prettyPrint) fprintf(output, "\n");
        }
        
        // Create null-terminated string from data
        char* dataStr = (char*)malloc(tag->length + 1);
        if (dataStr) {
            memcpy(dataStr, collector->dataBuffer + tag->dataOffset, tag->length);
            dataStr[tag->length] = '\0';
            
            if (mapping) {
                writeJSONField(output, mapping->fieldName, dataStr, tag->vrCode, 
                              mapping->isArray, opts->prettyPrint, indent);
            } else {
                // Unknown tag - use hex representation
                char hexName[16];
                snprintf(hexName, sizeof(hexName), "%04X%04X", 
                        getGroupFromTag(tag->tag), getElementFromTag(tag->tag));
                writeJSONField(output, hexName, dataStr, tag->vrCode, 
                              false, opts->prettyPrint, indent);
            }
            
            free(dataStr);
        }
        
        isFirstField = false;
    }
    
    if (opts->prettyPrint) fprintf(output, "\n");
    fprintf(output, "}");
    
    fclose(output);
    return 0;
}

void writeJSONField(FILE* output, const char* fieldName, const char* value,
                   TVRCode vr, bool isArray, bool prettyPrint, int indent) {
    if (prettyPrint) {
        for (int i = 0; i < indent; i++) fprintf(output, "  ");
    }
    
    fprintf(output, "\"%s\": ", fieldName);
    
    // Handle different VR types
    switch (vr) {
        case VR_IS:
        case VR_US:
        case VR_SS:
        case VR_UL:
        case VR_SL:
            writeNumericValue(output, value, vr, isArray);
            break;
            
        case VR_FL:
        case VR_FD:
        case VR_DS:
            writeFloatValue(output, value, vr, isArray);
            break;
            
        case VR_DA:
            writeDateValue(output, value);
            break;
            
        case VR_TM:
            writeTimeValue(output, value);
            break;
            
        case VR_PN:
            writePersonNameValue(output, value);
            break;
            
        case VR_CS:
            if (isArray) {
                writeStringArray(output, value);
            } else {
                writeStringValue(output, value);
            }
            break;
            
        case VR_OW:
        case VR_OB:
            writeBinaryDataInfo(output, value, strlen(value));
            break;
            
        case VR_SQ:
            fprintf(output, "\"[Sequence]\"");
            break;
            
        default:
            if (isArray) {
                writeStringArray(output, value);
            } else {
                writeStringValue(output, value);
            }
            break;
    }
}

// Parse DICOM buffer and extract metadata tags
int parseDicomBuffer(const unsigned char* buffer, size_t bufferSize, struct TDICOMMetadataCollector* collector) {
    if (!buffer || !collector || bufferSize < 132) return -1;
    
    // Skip DICOM preamble (128 bytes) and DICM prefix (4 bytes)
    size_t pos = 128;
    if (memcmp(buffer + pos, "DICM", 4) != 0) {
        // No DICOM prefix, start from beginning (implicit VR)
        pos = 0;
    } else {
        pos += 4; // Skip DICM
    }
    
    bool isExplicitVR = true;
    bool isLittleEndian = true;
    
    // Parse data elements
    while (pos + 8 <= bufferSize) {
        // Read tag (group, element)
        uint16_t group = *(uint16_t*)(buffer + pos);
        uint16_t element = *(uint16_t*)(buffer + pos + 2);
        uint32_t tag = makeTag(group, element);
        pos += 4;
        
        // Check for sequence delimiter
        if (tag == 0xFFFEE0DD || tag == 0xFFFEE00D) {
            pos += 4; // Skip length
            continue;
        }
        
        // Handle Transfer Syntax UID to determine VR format
        if (tag == 0x00020010) {
            // This determines explicit/implicit VR - for now assume explicit
        }
        
        char vrStr[3] = {0, 0, 0};
        uint32_t length = 0;
        
        if (isExplicitVR && group != 0xFFFE) {
            // Explicit VR - read VR (2 bytes)
            if (pos + 2 > bufferSize) break;
            vrStr[0] = buffer[pos];
            vrStr[1] = buffer[pos + 1];
            pos += 2;
            
            // Determine length field size based on VR
            bool isLongVR = (strcmp(vrStr, "OB") == 0 || strcmp(vrStr, "OW") == 0 ||
                           strcmp(vrStr, "OF") == 0 || strcmp(vrStr, "SQ") == 0 ||
                           strcmp(vrStr, "UT") == 0 || strcmp(vrStr, "UN") == 0);
            
            if (isLongVR) {
                if (pos + 6 > bufferSize) break;
                pos += 2; // Skip reserved bytes
                length = *(uint32_t*)(buffer + pos);
                pos += 4;
            } else {
                if (pos + 2 > bufferSize) break;
                length = *(uint16_t*)(buffer + pos);
                pos += 2;
            }
        } else {
            // Implicit VR or sequence item
            if (pos + 4 > bufferSize) break;
            length = *(uint32_t*)(buffer + pos);
            pos += 4;
            
            // Guess VR based on tag
            strcpy(vrStr, "UN"); // Default to unknown
            const struct TDICOMFieldMapping* mapping = findFieldMapping(tag);
            if (mapping) {
                strcpy(vrStr, vrCodeToString(mapping->vrCode));
            }
        }
        
        // Handle undefined length
        if (length == 0xFFFFFFFF) {
            // Skip to next tag or handle sequences properly
            continue;
        }
        
        // Ensure we don't read beyond buffer
        if (pos + length > bufferSize) {
            length = bufferSize - pos;
        }
        
        // Extract tag data
        if (length > 0 && pos + length <= bufferSize) {
            TVRCode vr = stringToVRCode(vrStr);
            
            // Check if we should include this tag
            bool isPrivate = (group % 2 == 1);
            bool isSequence = (vr == VR_SQ);
            
            if ((!isPrivate || collector->includePrivate) &&
                (!isSequence || collector->includeSequences)) {
                
                // Add tag to collector
                bool success = addTagToCollector(collector, tag, vr, (const char*)(buffer + pos), length);
                if (!success && collector->tagCount >= collector->tagCapacity) {
                    // Collection limit reached
                    break;
                }
            }
        }
        
        // Move to next tag
        pos += length;
        
        // Ensure even position (DICOM requires even byte boundaries)
        if (pos % 2 != 0) pos++;
    }
    
    return 0;
}

// Merge metadata with existing BIDS JSON
int mergeMetadataWithBIDSJSON(const struct TDICOMMetadataCollector* collector,
                             const struct TJSONMetadataOptions* opts,
                             const char* bidsJsonPath) {
    if (!collector || !opts || !bidsJsonPath) return -1;
    
    // Read existing BIDS JSON file
    FILE* bidsFile = fopen(bidsJsonPath, "r");
    if (!bidsFile) return -1;
    
    // Get file size
    fseek(bidsFile, 0, SEEK_END);
    long fileSize = ftell(bidsFile);
    fseek(bidsFile, 0, SEEK_SET);
    
    // Read entire file
    char* bidsContent = (char*)malloc(fileSize + 1);
    if (!bidsContent) {
        fclose(bidsFile);
        return -1;
    }
    
    size_t bytesRead = fread(bidsContent, 1, fileSize, bidsFile);
    bidsContent[bytesRead] = '\0';
    fclose(bidsFile);
    
    // Find the last closing brace and remove it
    char* lastBrace = strrchr(bidsContent, '}');
    if (!lastBrace) {
        free(bidsContent);
        return -1;
    }
    
    // Backup original file
    char backupPath[2048];
    snprintf(backupPath, sizeof(backupPath), "%s.bak", bidsJsonPath);
    rename(bidsJsonPath, backupPath);
    
    // Create new merged file
    FILE* output = fopen(bidsJsonPath, "w");
    if (!output) {
        free(bidsContent);
        return -1;
    }
    
    // Write BIDS content up to the last brace (without the closing brace)
    size_t contentLength = lastBrace - bidsContent;
    fwrite(bidsContent, 1, contentLength, output);
    
    // Add comma if there was existing content
    bool needsComma = false;
    for (size_t i = contentLength - 1; i > 0; i--) {
        if (bidsContent[i] == '"' || bidsContent[i] == ']' || bidsContent[i] == '}' || 
            isdigit(bidsContent[i]) || bidsContent[i] == 'e' || bidsContent[i] == 'f') {
            needsComma = true;
            break;
        }
        if (bidsContent[i] == '{') {
            needsComma = false;
            break;
        }
    }
    
    // Add metadata fields
    for (uint32_t i = 0; i < collector->tagCount; i++) {
        const struct TDICOMTagLite* tag = &collector->tags[i];
        const struct TDICOMFieldMapping* mapping = findFieldMapping(tag->tag);
        
        // Skip if no mapping found and unknown tags not requested
        if (!mapping && !opts->includeUnknown) continue;
        
        // Skip private tags if not requested
        if (tag->isPrivate && !opts->includePrivate) continue;
        
        // Skip sequences if not requested
        if (tag->vrCode == VR_SQ && !opts->includeSequences) continue;
        
        // Skip fields that already exist in BIDS to avoid duplicate keys (invalid JSON)
        // Since both BIDS and extended metadata now use the same PascalCase naming,
        // this will properly prevent duplicates while preserving BIDS field values
        if (mapping) {
            char searchField[512];
            snprintf(searchField, sizeof(searchField), "\"%s\":", mapping->fieldName);
            if (strstr(bidsContent, searchField)) {
                continue; // Skip - field already exists in BIDS
            }
        }
        
        // Add comma and newline
        if (needsComma) {
            fprintf(output, ",");
            if (opts->prettyPrint) fprintf(output, "\n");
        }
        
        // Create null-terminated string from data
        char* dataStr = (char*)malloc(tag->length + 1);
        if (dataStr) {
            memcpy(dataStr, collector->dataBuffer + tag->dataOffset, tag->length);
            dataStr[tag->length] = '\0';
            
            if (mapping) {
                writeJSONField(output, mapping->fieldName, dataStr, tag->vrCode, 
                              mapping->isArray, opts->prettyPrint, 1);
            } else {
                // Unknown tag - use hex representation
                char hexName[16];
                snprintf(hexName, sizeof(hexName), "%04X%04X", 
                        getGroupFromTag(tag->tag), getElementFromTag(tag->tag));
                writeJSONField(output, hexName, dataStr, tag->vrCode, 
                              false, opts->prettyPrint, 1);
            }
            
            free(dataStr);
        }
        
        needsComma = true;
    }
    
    // Close the JSON object
    if (opts->prettyPrint) fprintf(output, "\n");
    fprintf(output, "}");
    
    fclose(output);
    free(bidsContent);
    
    // Remove backup file on success
    remove(backupPath);
    
    return 0;
}