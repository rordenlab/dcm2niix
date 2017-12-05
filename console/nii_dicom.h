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

#define kDCMvers "v1.0.20171204" kDCMsuf kCCsuf

static const int kMaxEPI3D = 1024; //maximum number of EPI images in Siemens Mosaic
static const int kMaxDTI4D = 4096; //maximum number of DTI directions for 4D (Philips) images, also maximum number of 3D slices for Philips 3D and 4D images
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
        int sliceNumberMrPhilips;
    };
    struct TDTI4D {
        struct TDTI S[kMaxDTI4D];
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
    	bool isPhaseMap;
        float sliceTiming[kMaxEPI3D], dtiV[4], sliceNormV[4], bandwidthPerPixelPhaseEncode, sliceMeasurementDuration;
        int numDti, SeriesHeader_offset, SeriesHeader_length, multiBandFactor, sliceOrder, slice_start, slice_end, mosaicSlices,protocolSliceNumber1,phaseEncodingDirectionPositive;
    };
    struct TDICOMdata {
        long seriesNum;
        int xyzDim[5];
        int modality, dwellTime, effectiveEchoSpacingGE, phaseEncodingLines, phaseEncodingSteps, echoTrainLength, patientPositionNumPhilips, coilNum, echoNum, sliceOrient,numberOfDynamicScans, manufacturer, converted2NII, acquNum, imageNum, imageStart, imageBytes, bitsStored, bitsAllocated, samplesPerPixel,patientPositionSequentialRepeats,locationsInAcquisition, compressionScheme;
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
        bool isSegamiOasis, isDerived, isXRay, isMultiEcho, isSlicesSpatiallySequentialPhilips, isValid, is3DAcq, is2DAcq, isExplicitVR, isLittleEndian, isPlanarRGB, isSigned, isHasPhase,isHasMagnitude,isHasMixed, isFloat, isResampled;
        char phaseEncodingRC, patientSex;
    };

  // Gathering spot for all the info needed to get the b value and direction
  // for a volume.
  class TVolumeDiffusion {
  public:
    TVolumeDiffusion(struct TDICOMdata& tdd, struct TDTI4D* dti4D):
      dd(tdd),
      pdti4D(dti4D)
    {clear_volume();}
    ~TVolumeDiffusion() {}
    
    uint8_t manufacturer;            // kMANUFACTURER_UNKNOWN, kMANUFACTURER_SIEMENS, etc.

    // Blank the volume-specific members or set them to impossible values.
    void clear_volume();

    void set_directionality0018_9075(const unsigned char* inbuf);
    //void set_manufacturer(const uint8_t m) {manufacturer = m; update();}
    void set_orientation0018_9089(const int lLength, const unsigned char* inbuf, const bool isLittleEndian);
    void set_isAtFirstPatientPosition(const bool iafpp) {isAtFirstPatientPosition = iafpp; update();}
    void set_bValGE(const int lLength, const unsigned char* inbuf);
    void set_directionGE(const int lLength, const unsigned char* inbuf, const int axis);
    void set_bVal(const float b) {dtiV[0] = b; update();}
    void set_seq0018_9117(const char* inbuf);
  private:
    // Note that most of these data elements are private to force the use of setters,
    // since the design depends on the setters all calling update().
    struct TDICOMdata& dd;  // The multivolume
    struct TDTI4D* pdti4D;  // permanent records.

    bool isAtFirstPatientPosition;   // Limit b vals and vecs to 1 per volume.

    //float bVal0018_9087;      // kDiffusion_b_value, always present in Philips/Siemens.
    //float bVal2001_1003;        // kDiffusionBFactor
    // float dirRL2005_10b0;        // kDiffusionDirectionRL
    // float dirAP2005_10b1;        // kDiffusionDirectionAP
    // float dirFH2005_10b2;        // kDiffusionDirectionFH

    // Philips diffusion scans tend to have a "trace" (average of the diffusion
    // weighted volumes) volume tacked on, usually but not always at the end,
    // so b is > 0, but the direction is meaningless.  Most software versions
    // explicitly set the direction to 0, but version 3 does not, making (0x18,
    // 0x9075) necessary.
    bool isPhilipsNonDirectional;

    char directionality0018_9075[16];       // DiffusionDirectionality, not in Philips 2.6.
    float orientation0018_9089[3];      // kDiffusionOrientation, always
                                        // present in Philips/Siemens for
                                        // volumes with a direction.
    char seq0018_9117[64];      // MRDiffusionSequence, not in Philips 2.6.

    float dtiV[4];
    //uint16_t numDti;

    // Update the diffusion info in dd and *pdti4D for a volume once all the
    // diffusion info for that volume has been read into pvd.
    //
    // Note that depending on the scanner software the diffusion info can arrive in
    // different tags, in different orders (because of enclosing sequence tags),
    // and the values in some tags may be invalid, or may be essential, depending
    // on the presence of other tags.  Thus it is best to gather all the diffusion
    // info for a volume (frame) before taking action on it.
    //
    // On the other hand, dd and *pdti4D need to be updated as soon as the
    // diffusion info is ready, before diffusion info for the next volume
    // is read in.
    void update();
  };

    size_t nii_ImgBytes(struct nifti_1_header hdr);
    struct TDICOMdata readDICOMv(char * fname, int isVerbose, int compressFlag, struct TDTI4D *dti4D);
    struct TDICOMdata readDICOM(char * fname);
    struct TDICOMdata clear_dicom_data();
    struct TDICOMdata  nii_readParRec (char * parname, int isVerbose, struct TDTI4D *dti4D);
    unsigned char * nii_flipY(unsigned char* bImg, struct nifti_1_header *h);
    unsigned char * nii_flipZ(unsigned char* bImg, struct nifti_1_header *h);
    unsigned char * nii_reorderSlices(unsigned char* bImg, struct nifti_1_header *h, struct TDTI4D *dti4D);
    void changeExt (char *file_name, const char* ext);
    unsigned char * nii_planar2rgb(unsigned char* bImg, struct nifti_1_header *hdr, int isPlanar);
	int isDICOMfile(const char * fname); //0=not DICOM, 1=DICOM, 2=NOTSURE(not part 10 compliant)
    void setQSForm(struct nifti_1_header *h, mat44 Q44i, bool isVerbose);
    int headerDcm2Nii2(struct TDICOMdata d, struct TDICOMdata d2, struct nifti_1_header *h, int isVerbose);
    int headerDcm2Nii(struct TDICOMdata d, struct nifti_1_header *h, bool isComputeSForm) ;
    unsigned char * nii_loadImgXL(char* imgname, struct nifti_1_header *hdr, struct TDICOMdata dcm, bool iVaries, int compressFlag, int isVerbose);
#ifdef  __cplusplus
}
#endif

#endif
