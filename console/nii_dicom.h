#include <stdbool.h> //requires VS 2015 or later
#include <string.h>
#include <stdint.h>
#include "nifti1_io_core.h"
#ifndef USING_R
#include "nifti1.h"
#endif

#ifndef MRIpro_nii_dcm_h

#define MRIpro_nii_dcm_h

#ifdef  __cplusplus
extern "C" {
#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

 #if defined(myEnableJPEGLS) || defined(myEnableJPEGLS1)
   #define kLSsuf " (JP-LS:CharLS)"
 #else
   #define kLSsuf ""
 #endif
 #ifdef myEnableJasper
  #define kJP2suf " (JP2:JasPer)"
 #else
  #ifdef myDisableOpenJPEG
    #define kJP2suf ""
  #else
    #define kJP2suf " (JP2:OpenJPEG)"
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

#define kDCMdate "v1.0.20190523"
#define kDCMvers kDCMdate " " kJP2suf kLSsuf kCCsuf

static const int kMaxEPI3D = 1024; //maximum number of EPI images in Siemens Mosaic
static const int kMaxDTI4D = 18000; //maximum number of DTI directions for 4D (Philips) images, also maximum number of 3D slices for Philips 3D and 4D images
static const int kMaxSlice2D = 64000; //maximum number of 2D slices in 4D (Philips) images

#define kDICOMStr 66 //64 characters plus NULL https://github.com/rordenlab/dcm2niix/issues/268
#define kDICOMStrLarge 256
#define kMANUFACTURER_UNKNOWN  0
#define kMANUFACTURER_SIEMENS  1
#define kMANUFACTURER_GE  2
#define kMANUFACTURER_PHILIPS  3
#define kMANUFACTURER_TOSHIBA  4
#define kMANUFACTURER_UIH  5
#define kMANUFACTURER_BRUKER  6
#define kMANUFACTURER_HITACHI 7

//note: note a complete modality list, e.g. XA,PX, etc
#define kMODALITY_UNKNOWN  0
#define kMODALITY_CR  1
#define kMODALITY_CT  2
#define kMODALITY_MR  3
#define kMODALITY_PT  4
#define kMODALITY_US  5

//GE phase encoding
#define kGE_PHASE_ENCODING_POLARITY_UNKNOWN  -1
#define kGE_PHASE_ENCODING_POLARITY_UNFLIPPED  0
#define kGE_PHASE_ENCODING_POLARITY_FLIPPED  4
#define kGE_SLICE_ORDER_UNKNOWN -1
#define kGE_SLICE_ORDER_TOP_DOWN  0
#define kGE_SLICE_ORDER_BOTTOM_UP  2


//#define kGE_PHASE_DIRECTION_CENTER_OUT_REV  3
//#define kGE_PHASE_DIRECTION_CENTER_OUT  4

//EXIT_SUCCESS 0
//EXIT_FAILURE 1
#define kEXIT_NO_VALID_FILES_FOUND  2
#define kEXIT_REPORT_VERSION  3
#define kEXIT_CORRUPT_FILE_FOUND  4
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
static const int kCompressPMSCT_RLE1 = 5; //see rle2img: Philips/ELSCINT1 run-length compression 07a1,1011= PMSCT_RLE1
static const int kCompressJPEGLS = 5; //LoCo JPEG-LS
#ifdef myEnableJasper
    static const int kCompressSupport = kCompressYes; //JASPER for JPEG2000
#else
    #ifdef myDisableOpenJPEG
        static const int kCompressSupport = kCompressNone; //no decompressor
    #else
        static const int kCompressSupport = kCompressYes; //OPENJPEG for JPEG2000
    #endif
#endif

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
        float triggerDelayTime[kMaxDTI4D], TE[kMaxDTI4D], RWVScale[kMaxDTI4D], RWVIntercept[kMaxDTI4D], intenScale[kMaxDTI4D], intenIntercept[kMaxDTI4D], intenScalePhilips[kMaxDTI4D];
        bool isReal[kMaxDTI4D];
        bool isImaginary[kMaxDTI4D];
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
        int numDti, SeriesHeader_offset, SeriesHeader_length, multiBandFactor, sliceOrder, slice_start, slice_end, mosaicSlices, protocolSliceNumber1, phaseEncodingDirectionPositive;
    	bool isPhaseMap;

    };
    struct TDICOMdata {
        long seriesNum;
        int xyzDim[5];
        uint32_t coilCrc, seriesUidCrc, instanceUidCrc;
        int numberOfImagesInGridUIH, numberOfDiffusionDirectionGE, phaseEncodingGE, protocolBlockStartGE, protocolBlockLengthGE, modality, dwellTime, effectiveEchoSpacingGE, phaseEncodingLines, phaseEncodingSteps, echoTrainLength, echoNum, sliceOrient, manufacturer, converted2NII, acquNum, imageNum, imageStart, imageBytes, bitsStored, bitsAllocated, samplesPerPixel,locationsInAcquisition, compressionScheme;
        float numberOfAverages, imagingFrequency, patientWeight, zSpacing, zThick, pixelBandwidth, SAR, phaseFieldofView, accelFactPE, flipAngle, fieldStrength, TE, TI, TR, intenScale, intenIntercept, intenScalePhilips, gantryTilt, lastScanLoc, angulation[4];
        float orient[7], patientPosition[4], patientPositionLast[4], xyzMM[4], stackOffcentre[4];
        float rtia_timerGE, radionuclidePositronFraction, radionuclideTotalDose, radionuclideHalfLife, doseCalibrationFactor; //PET ISOTOPE MODULE ATTRIBUTES (C.8-57)
		float ecat_isotope_halflife, ecat_dosage;
		float pixelPaddingValue;  // used for both FloatPixelPaddingValue (0028, 0122) and PixelPaddingValue (0028, 0120); NaN if not present.
        double acquisitionDuration, triggerDelayTime, RWVScale, RWVIntercept, dateTime, acquisitionTime, acquisitionDate, bandwidthPerPixelPhaseEncode;
        char coilElements[kDICOMStr], coilName[kDICOMStr], phaseEncodingDirectionDisplayedUIH[kDICOMStr], imageBaseName[kDICOMStr], scanOptions[kDICOMStr], stationName[kDICOMStr], softwareVersions[kDICOMStr], deviceSerialNumber[kDICOMStr], institutionName[kDICOMStr], referringPhysicianName[kDICOMStr], seriesInstanceUID[kDICOMStr], studyInstanceUID[kDICOMStr], bodyPartExamined[kDICOMStr], procedureStepDescription[kDICOMStr], imageType[kDICOMStr], institutionalDepartmentName[kDICOMStr], manufacturersModelName[kDICOMStr], patientID[kDICOMStr], patientOrient[kDICOMStr], patientName[kDICOMStr], accessionNumber[kDICOMStr], seriesDescription[kDICOMStr], studyID[kDICOMStr], sequenceName[kDICOMStr], protocolName[kDICOMStr],sequenceVariant[kDICOMStr],scanningSequence[kDICOMStr], patientBirthDate[kDICOMStr], patientAge[kDICOMStr],  studyDate[kDICOMStr],studyTime[kDICOMStr];
        char institutionAddress[kDICOMStrLarge], imageComments[kDICOMStrLarge];
        uint32_t dimensionIndexValues[MAX_NUMBER_OF_DIMENSIONS];
        struct TCSAdata CSA;
        bool isDiffusion, isVectorFromBMatrix, isRawDataStorage, isGrayscaleSoftcopyPresentationState, isStackableSeries, isCoilVaries, isNonParallelSlices, isSegamiOasis, isXA10A, isScaleOrTEVaries,  isDerived, isXRay, isMultiEcho, isValid, is3DAcq, is2DAcq, isExplicitVR, isLittleEndian, isPlanarRGB, isSigned, isHasPhase, isHasImaginary, isHasReal, isHasMagnitude,isHasMixed, isFloat, isResampled, isLocalizer;
        char phaseEncodingRC, patientSex;
    };

    size_t nii_ImgBytes(struct nifti_1_header hdr);
    int isSameFloatGE (float a, float b);
    void getFileNameX( char *pathParent, const char *path, int maxLen);
    struct TDICOMdata readDICOMv(char * fname, int isVerbose, int compressFlag, struct TDTI4D *dti4D);
    struct TDICOMdata readDICOM(char * fname);
    struct TDICOMdata clear_dicom_data(void);
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
