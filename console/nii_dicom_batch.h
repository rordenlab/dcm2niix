//
//

#ifndef MRIpro_nii_batch_h
#define MRIpro_nii_batch_h

#ifdef USING_DCM2NIIXFSWRAPPER
#include "nifti1.h"
#include "nii_dicom.h"
#include <vector>

struct MRIFSSTRUCT {
	struct nifti_1_header hdr0;

	size_t imgsz;
	unsigned char *imgM;

	char pulseSequenceDetails[kDICOMStr];
	struct TDICOMdata tdicomData;
	char namePostFixes[256];
	char *dicomfile;

	int nDcm;
	char **dicomlst;

	struct TDTI *tdti;
	int numDti;
};

std::vector<std::vector<float>> *nii_getAutoScaleFactorVector();

MRIFSSTRUCT *nii_getMrifsStruct();
void nii_clrMrifsStruct();

std::vector<MRIFSSTRUCT> *nii_getMrifsStructVector();
void nii_clrMrifsStructVector();

void dcmListDump(int nConvert, struct TDCMsort dcmSort[], struct TDICOMdata dcmList[], struct TSearchList *nameList, struct TDCMopts opts);
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h> //requires VS 2015 or later
#include <string.h>
#ifndef USING_R
#include "nifti1.h"
#endif
#include "nii_dicom.h"

#ifdef USING_R
struct TDicomSeries {
	std::string name;
	TDICOMdata representativeData;
	std::vector<std::string> files;
};
#endif

#define kNAME_CONFLICT_SKIP 0		// 0 = write nothing for a file that exists with desired name
#define kNAME_CONFLICT_OVERWRITE 1	// 1 = overwrite existing file with same name
#define kNAME_CONFLICT_ADD_SUFFIX 2 // default 2 = write with new suffix as a new file

#define kMaximize16BitRange_False 0 // e.g. raw UINT16 values 0..4095 saved as INT16 (e.g. AFNI preserves INT16 "short", converts UINT16 to float32)
#define kMaximize16BitRange_True 1	// e.g. raw UINT16 values 0..4095 saved as 0..61425 UINT16 (SPM free precision)
#define kMaximize16BitRange_Raw 2	// e.g. raw UINT16 values 0..4095 saved as UINT16 (retains raw data type, AFNI would convert to float32)

#define kSaveFormatNIfTI 0
#define kSaveFormatNRRD 1
#define kSaveFormatMGH 2
#define kSaveFormatJNII 3
#define kSaveFormatBNII 4

// kMRWeighting* enum moved to nii_dicom.h so TDICOMdata.acquisitionContrast
// (parsed from DICOM (0008,9209)) and MRWeightingGuess() share the same value
// space.
int MRWeightingGuess(struct TDICOMdata *d, bool isSpinEcho, bool isVariableFlipAngle);

#define MAX_NUM_SERIES 16
#define kOptsStr 512

struct TDCMopts {
	bool isDumpNotConvert;
	// isAnonymizeBIDS (`-ba y/n/o`): true => strip BOTH dates and patient PII
	// (heavy anonymisation, default). isOmitPiiBIDS: set by `-ba o`; strips
	// patient PII (PatientName/ID/BirthDate/Sex/Age/Size/Weight,
	// AccessionNumber, ReferringPhysicianName) but keeps
	// AcquisitionDateTime/Time so downstream tools (e.g. reproinx.py's
	// scans.tsv) can still aggregate timestamps. The two flags are
	// independent — `o` sets isAnonymizeBIDS=false, isOmitPiiBIDS=true.
	bool isKeepDirectionVaries, isIgnoreTriggerTimes, isTestx0021x105E, isAddNamePostFixes, isSaveNativeEndian, isOneDirAtATime, isRenameNotConvert, isSave3D, isZStd, isGz, isPipedGz, isFlipY, isCreateBIDS, isSortDTIbyBVal, isAnonymizeBIDS, isOmitPiiBIDS, isOnlyBIDS, isCreateText, isForceOnsetTimes, isIgnoreDerivedAnd2D, isPhilipsFloatNotDisplayScaling, isIgnoreIntensityScaling, isTiltCorrect, isRGBplanar, isOnlySingleFile, isForceStackDCE, isIgnoreSeriesInstanceUID, isRotate3DAcq, isCrop, isGuessBidsFilename, isBidsRoot;
	int saveFormat, isMaximize16BitRange, isForceStackSameSeries, nameConflictBehavior, isVerbose, isProgress, compressFlag, dirSearchDepth, onlySearchDirForDICOM, gzLevel, diffCyclingModeGE; // support for compressed data 0=none,
	int numRenameSkip, numRenameConflict; // -r rename mode: byte-identical duplicates skipped, distinct-file naming conflicts
	char filename[kOptsStr], outdir[kOptsStr], indir[kOptsStr], pigzname[kOptsStr], optsname[kOptsStr], indirParent[kOptsStr], imageComments[24], bidsSubject[kOptsStr], bidsSession[kOptsStr], bidsRoot[kOptsStr];
	double seriesNumber[MAX_NUM_SERIES]; // requires double must store -1 (report but do not convert) as well as seriesUidCrc (uint32)
	long numSeries;
#ifdef USING_R
	bool isScanOnly, isImageInMemory;
	void *imageList;
	std::vector<TDicomSeries> series;

	// Used when sorting a directory
	std::vector<std::string> sourcePaths;
	std::vector<std::string> targetPaths;
	std::vector<std::string> ignoredPaths;
#endif
};
void saveIniFile(struct TDCMopts opts);
void setDefaultOpts(struct TDCMopts *opts, const char *argv[]); // either "setDefaultOpts(opts,NULL)" or "setDefaultOpts(opts,argv)" where argv[0] is path to search
void readIniFile(struct TDCMopts *opts, const char *argv[]);
int nii_saveNIIx(char *niiFilename, struct nifti_1_header hdr, unsigned char *im, struct TDCMopts opts);
int nii_loadDir(struct TDCMopts *opts);
int nii_loadDirCore(char *indir, struct TDCMopts *opts);
int singleDICOM(struct TDCMopts *opts, char *fname);
void nii_SaveBIDS(char pathoutname[], struct TDICOMdata d, struct TDCMopts opts, struct nifti_1_header *h, const char *filename);
int nii_createFilename(struct TDICOMdata dcm, char *niiFilename, struct TDCMopts opts);
void nii_createDummyFilename(char *niiFilename, struct TDCMopts opts);
#ifdef __cplusplus
}
#endif

#endif
