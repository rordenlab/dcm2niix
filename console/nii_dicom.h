#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "nifti1_io_core.h"
#ifndef HAVE_R
#include "nifti1.h"
#endif

#ifndef MRIpro_nii_dcm_h

#define MRIpro_nii_dcm_h

#ifdef  __cplusplus
extern "C" {
#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

 #ifdef myEnableJasper
  #define kDCMsuf " (JasPer build)"
 #else
  #ifdef myDisableOpenJPEG
    #define kDCMsuf ""
  #else
    #define kDCMsuf " (OpenJPEG build)"
  #endif
 #endif
#if defined(__ICC) || defined(__INTEL_COMPILER)
	#define kCCsuf  " IntelCC" STR(__INTEL_COMPILER)
#elif defined(_MSC_VER)
	#define kCCsuf  " MSC" STR(_MSC_VER)
#elif defined(__clang__)
	#define kCCsuf  " Clang" STR(__clang_major__) "." STR(__clang_minor__) "." STR(__clang_patchlevel__)
#elif defined(__GNUC__) || defined(__GNUG__)
    #define kCCsuf  " GCC" STR(__GNUC__) "." STR(__GNUC_MINOR__) "." STR(__GNUC_PATCHLEVEL__)
#else
	#define kCCsuf " CompilerNA" //unknown compiler!
#endif

#define kDCMvers "v1.0.20180404" kDCMsuf kCCsuf

static const int kMaxEPI3D = 1024; //maximum number of EPI images in Siemens Mosaic
static const int kMaxDTI4D = 4096; //maximum number of DTI directions for 4D (Philips) images, also maximum number of 3D slices for Philips 3D and 4D images
static const int kMaxSlice2D = 64000; //maximum number of 2D slices in 4D (Philips) images

#define kDICOMStr 64
#define kDICOMStrLarge 256
#define kMANUFACTURER_UNKNOWN  0
#define kMANUFACTURER_SIEMENS  1
#define kMANUFACTURER_GE  2
#define kMANUFACTURER_PHILIPS  3
#define kMANUFACTURER_TOSHIBA  4

//note: note a complete modality list, e.g. XA,PX, etc
#define kMODALITY_UNKNOWN  0
#define kMODALITY_CR  1
#define kMODALITY_CT  2
#define kMODALITY_MR  3
#define kMODALITY_PT  4
#define kMODALITY_US  5

#define kEXIT_NO_VALID_FILES_FOUND  2
static const int kSliceOrientUnknown = 0;
static const int kSliceOrientTra = 1;
static const int kSliceOrientSag = 2;
static const int kSliceOrientCor = 3;
static const int kSliceOrientMosaicNegativeDeterminant = 4;
static const int kCompressNone = 0;
static const int kCompressYes = 1;
static const int kCompressC3 = 2; //obsolete JPEG lossless
static const int kCompress50 = 3; //obsolete JPEG lossy
static const int kCompressRLE = 4; //run length encoding

// Maximum number of dimensions for .dimensionIndexValues, i.e. possibly the
// number of axes in the output .nii.
static const uint8_t MAX_NUMBER_OF_DIMENSIONS = 8;

    struct TDTI {
        float V[4];
        //int totalSlicesIn4DOrder;
    };
    struct TDTI4D {
        struct TDTI S[kMaxDTI4D];
        int sliceOrder[kMaxSlice2D]; // [7,3,2] means the first slice on disk should be moved to 7th position
        int gradDynVol[kMaxDTI4D]; //used to parse dimensions of Philips data, e.g. file with multiple dynamics, echoes, phase+magnitude
        float TE[kMaxDTI4D], intenScale[kMaxDTI4D], intenIntercept[kMaxDTI4D], intenScalePhilips[kMaxDTI4D];
        bool isPhase[kMaxDTI4D];
    };

#ifdef _MSC_VER //Microsoft nomenclature for packed structures is different...
    #pragma pack(2)
    typedef struct {
        char name[64]; //null-terminated
        int32_t vm;
        char vr[4]; //  possibly nul-term string
        int32_t syngodt;//  ??
        int32_t nitems;// number of items in CSA
        int32_t xx;// maybe == 77 or 205
    } TCSAtag; //Siemens csa tag structure
    typedef struct {
        int32_t xx1, xx2_Len, xx3_77, xx4;
    } TCSAitem; //Siemens csa item structure
    #pragma pack()
#else
    typedef struct __attribute__((packed)) {
        char name[64]; //null-terminated
        int32_t vm;
        char vr[4]; //  possibly nul-term string
        int32_t syngodt;//  ??
        int32_t nitems;// number of items in CSA
        int32_t xx;// maybe == 77 or 205
    } TCSAtag; //Siemens csa tag structure
    typedef struct __attribute__((packed)) {
        int32_t xx1, xx2_Len, xx3_77, xx4;
    } TCSAitem; //Siemens csa item structure
#endif
    struct TCSAdata {
    	float sliceTiming[kMaxEPI3D], dtiV[4], sliceNormV[4], bandwidthPerPixelPhaseEncode, sliceMeasurementDuration;
        int numDti, SeriesHeader_offset, SeriesHeader_length, multiBandFactor, sliceOrder, slice_start, slice_end, mosaicSlices,protocolSliceNumber1,phaseEncodingDirectionPositive;
    	bool isPhaseMap;

    };
    struct TDICOMdata {
        long seriesNum;
        int xyzDim[5];
        //numberOfDynamicScans, patientPositionNumPhilips
        //patientPositionSequentialRepeats,patientPositionRepeats,
        //maxGradDynVol, gradDynVol,
        int protocolBlockStartGE, protocolBlockLengthGE, modality, dwellTime, effectiveEchoSpacingGE, phaseEncodingLines, phaseEncodingSteps, echoTrainLength, coilNum, echoNum, sliceOrient, manufacturer, converted2NII, acquNum, imageNum, imageStart, imageBytes, bitsStored, bitsAllocated, samplesPerPixel,locationsInAcquisition, compressionScheme;
        float patientWeight, zSpacing, zThick, pixelBandwidth, SAR, phaseFieldofView, accelFactPE, flipAngle, fieldStrength, TE, TI, TR, intenScale, intenIntercept, intenScalePhilips, gantryTilt, lastScanLoc, angulation[4];
        float orient[7], patientPosition[4], patientPositionLast[4], xyzMM[4], stackOffcentre[4];
        float radionuclidePositronFraction, radionuclideTotalDose, radionuclideHalfLife, doseCalibrationFactor; //PET ISOTOPE MODULE ATTRIBUTES (C.8-57)
		float ecat_isotope_halflife, ecat_dosage;
        double dateTime, acquisitionTime, acquisitionDate, bandwidthPerPixelPhaseEncode;
        //char mrAcquisitionType[kDICOMStr]
        char scanOptions[kDICOMStr], stationName[kDICOMStr], softwareVersions[kDICOMStr], deviceSerialNumber[kDICOMStr], institutionAddress[kDICOMStr], institutionName[kDICOMStr], referringPhysicianName[kDICOMStr], seriesInstanceUID[kDICOMStr], studyInstanceUID[kDICOMStr], bodyPartExamined[kDICOMStr], procedureStepDescription[kDICOMStr], imageType[kDICOMStr], institutionalDepartmentName[kDICOMStr], manufacturersModelName[kDICOMStr], patientID[kDICOMStr], patientOrient[kDICOMStr], patientName[kDICOMStr],seriesDescription[kDICOMStr], studyID[kDICOMStr], sequenceName[kDICOMStr], protocolName[kDICOMStr],sequenceVariant[kDICOMStr],scanningSequence[kDICOMStr], patientBirthDate[kDICOMStr], patientAge[kDICOMStr],  studyDate[kDICOMStr],studyTime[kDICOMStr];
        char imageComments[kDICOMStrLarge];
        uint32_t dimensionIndexValues[MAX_NUMBER_OF_DIMENSIONS];
        struct TCSAdata CSA;
        //isSlicesSpatiallySequentialPhilips
        bool isSegamiOasis, isScaleOrTEVaries,  isDerived, isXRay, isMultiEcho, isValid, is3DAcq, is2DAcq, isExplicitVR, isLittleEndian, isPlanarRGB, isSigned, isHasPhase,isHasMagnitude,isHasMixed, isFloat, isResampled, isLocalizer;
        char phaseEncodingRC, patientSex;
        //uint32_t *totalSlicesIn4DOrder; //Reordering array for Philips slices
    };

    size_t nii_ImgBytes(struct nifti_1_header hdr);
    int isSameFloatGE (float a, float b);
    struct TDICOMdata readDICOMv(char * fname, int isVerbose, int compressFlag, struct TDTI4D *dti4D);
    struct TDICOMdata readDICOM(char * fname);
    struct TDICOMdata clear_dicom_data();
    struct TDICOMdata  nii_readParRec (char * parname, int isVerbose, struct TDTI4D *dti4D, bool isReadPhase);
    unsigned char * nii_flipY(unsigned char* bImg, struct nifti_1_header *h);
    unsigned char * nii_flipZ(unsigned char* bImg, struct nifti_1_header *h);
    //*unsigned char * nii_reorderSlices(unsigned char* bImg, struct nifti_1_header *h, struct TDTI4D *dti4D);
    void changeExt (char *file_name, const char* ext);
    unsigned char * nii_planar2rgb(unsigned char* bImg, struct nifti_1_header *hdr, int isPlanar);
	int isDICOMfile(const char * fname); //0=not DICOM, 1=DICOM, 2=NOTSURE(not part 10 compliant)
    void setQSForm(struct nifti_1_header *h, mat44 Q44i, bool isVerbose);
    int headerDcm2Nii2(struct TDICOMdata d, struct TDICOMdata d2, struct nifti_1_header *h, int isVerbose);
    int headerDcm2Nii(struct TDICOMdata d, struct nifti_1_header *h, bool isComputeSForm) ;
    unsigned char * nii_loadImgXL(char* imgname, struct nifti_1_header *hdr, struct TDICOMdata dcm, bool iVaries, int compressFlag, int isVerbose, struct TDTI4D *dti4D);
#ifdef  __cplusplus
}
#endif

#endif
