#include <stdbool.h>
#include <string.h>
#ifndef HAVE_R
#include "nifti1.h"
#endif

#ifndef MRIpro_nii_dcm_h

#define MRIpro_nii_dcm_h

#ifdef  __cplusplus
extern "C" {
#endif

 #ifdef myEnableJasper
  #define kDCMsuf " (jasper build)"
 #else
  #ifdef myDisableOpenJPEG
    #define kDCMsuf ""
  #else
    #define kDCMsuf " (openJPEG build)"
  #endif
 #endif
 #define kDCMvers "v1.0.20170207" kDCMsuf

static const int kMaxDTI4D = 4000; //#define kMaxDTIv  4000
#define kDICOMStr 40
#define kMANUFACTURER_UNKNOWN  0
#define kMANUFACTURER_SIEMENS  1
#define kMANUFACTURER_GE  2
#define kMANUFACTURER_PHILIPS  3
#define kMANUFACTURER_TOSHIBA  4
static const int kSliceOrientUnknown = 0;
static const int kSliceOrientTra = 1;
static const int kSliceOrientSag = 2;
static const int kSliceOrientCor = 3;
static const int kSliceOrientMosaicNegativeDeterminant = 4;
static const int kCompressNone = 0;
static const int kCompressYes = 1;
static const int kCompressC3 = 2; //obsolete JPEG lossless
static const int kCompress50 = 3; //obsolete JPEG lossy
    struct TDTI {
        float V[4];
        float sliceTiming;
        int sliceNumberMrPhilips;
    };
    struct TDTI4D {
        struct TDTI S[kMaxDTI4D];
    };

    struct TCSAdata {
    	bool isPhaseMap;
        float dtiV[4], sliceNormV[4], bandwidthPerPixelPhaseEncode, sliceMeasurementDuration;
        int numDti, multiBandFactor, sliceOrder, slice_start, slice_end, mosaicSlices,protocolSliceNumber1,phaseEncodingDirectionPositive;
    };
    struct TDICOMdata {
        long seriesNum;
        int xyzDim[5];//, xyzOri[4];
        int patientPositionNumPhilips, coilNum, echoNum, sliceOrient,numberOfDynamicScans, manufacturer, converted2NII, acquNum, imageNum, imageStart, imageBytes, bitsStored, bitsAllocated, samplesPerPixel,patientPositionSequentialRepeats,locationsInAcquisition, compressionScheme; //
        float flipAngle, fieldStrength, TE, TR,intenScale,intenIntercept,intenScalePhilips, gantryTilt, lastScanLoc, angulation[4];
        float orient[7], patientPosition[4], patientPositionLast[4], xyzMM[4], stackOffcentre[4]; //patientPosition2nd[4],
        double dateTime, acquisitionTime, acquisitionDate;
        bool isXRay, isSlicesSpatiallySequentialPhilips, isNonImage, isValid, is3DAcq, isExplicitVR, isLittleEndian, isPlanarRGB, isSigned, isHasPhase,isHasMagnitude,isHasMixed, isFloat, isResampled;
        char phaseEncodingRC;
        char  procedureStepDescription[kDICOMStr], imageType[kDICOMStr], manufacturersModelName[kDICOMStr], patientID[kDICOMStr], patientOrient[kDICOMStr], patientName[kDICOMStr],seriesDescription[kDICOMStr], sequenceName[kDICOMStr], protocolName[kDICOMStr],scanningSequence[kDICOMStr], birthDate[kDICOMStr], gender[kDICOMStr], age[kDICOMStr],  studyDate[kDICOMStr],studyTime[kDICOMStr], imageComments[kDICOMStr];
        struct TCSAdata CSA;
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
    int headerDcm2Nii2(struct TDICOMdata d, struct TDICOMdata d2, struct nifti_1_header *h);
    //unsigned char * nii_loadImgX(char* imgname, struct nifti_1_header *hdr, struct TDICOMdata dcm, bool iVaries);
    unsigned char * nii_loadImgXL(char* imgname, struct nifti_1_header *hdr, struct TDICOMdata dcm, bool iVaries, int compressFlag, int isVerbose);
    //int foo (float vx);
#ifdef  __cplusplus
}
#endif

#endif
