#include <stdio.h>

#include "nii_dicom.h"
#include "dcm2niix_fswrapper.h"

struct TDCMopts dcm2niix_fswrapper::tdcmOpts;

/* These are the TDCMopts defaults set in dcm2niix
isIgnoreTriggerTimes = false
isTestx0021x105E = false
isAddNamePostFixes = true
isSaveNativeEndian = true
isOneDirAtATime = false
isRenameNotConvert = false
isSave3D = false
isGz = false
isPipedGz = false
isFlipY = true
isCreateBIDS = true
isSortDTIbyBVal = false
isAnonymizeBIDS = true
isOnlyBIDS = false
isCreateText = false
isForceOnsetTimes = true
isIgnoreDerivedAnd2D = false
isPhilipsFloatNotDisplayScaling = true
isTiltCorrect = true
isRGBplanar = false
isOnlySingleFile = false
isForceStackDCE = true
isIgnoreSeriesInstanceUID = false    // if true, d.seriesUidCrc = d.seriesNum;
isRotate3DAcq = true
isCrop = false
saveFormat = 0
isMaximize16BitRange = 2
isForceStackSameSeries = 2
nameConflictBehavior = 2
isVerbose = 0
isProgress = 0
compressFlag = 0
dirSearchDepth = 5
gzLevel = 6
filename = "%s_%p"    // seriesNum_protocol -f "%s_%p"
outdir = "..."
indir = "..."
pigzname = '\000'
optsname = "~/.dcm2nii.ini"
indirParent = "..."
imageComments = ""
seriesNumber = nnn 
numSeries = 0
 */

// set TDCMopts defaults, overwrite settings to output in mgz orientation
void dcm2niix_fswrapper::setOpts(const char* dcmindir, const char* niioutdir)
{
  memset(&tdcmOpts, 0, sizeof(tdcmOpts));
  setDefaultOpts(&tdcmOpts, NULL);

  if (dcmindir != NULL)
    strcpy(tdcmOpts.indir, dcmindir);
  if (niioutdir != NULL)
    strcpy(tdcmOpts.outdir, niioutdir);

  // set the options for freesurfer mgz orientation
  tdcmOpts.isRotate3DAcq = false;
  tdcmOpts.isFlipY = false;
  tdcmOpts.isIgnoreSeriesInstanceUID = true;
  tdcmOpts.isCreateBIDS = false;
  tdcmOpts.isGz = false;
  //tdcmOpts.isForceStackSameSeries = 1; // merge 2D slice '-m y'
  tdcmOpts.isForceStackDCE = false;
  //tdcmOpts.isForceOnsetTimes = false;
}

// interface to isDICOMfile() in nii_dicom.cpp
bool dcm2niix_fswrapper::isDICOM(const char* file)
{
  return isDICOMfile(file);
}

/*
 * interface to nii_loadDirCore() to search all dicom files from the directory input file is in,
 * and convert dicom files with the same series as given file.
 */
int dcm2niix_fswrapper::dcm2NiiOneSeries(const char* dcmfile)
{
  // get seriesNo for given dicom file
  struct TDICOMdata tdicomData = readDICOM((char*)dcmfile);

  double seriesNo = (double)tdicomData.seriesUidCrc;
  if (tdcmOpts.isIgnoreSeriesInstanceUID)
    seriesNo = (double)tdicomData.seriesNum;

  // set TDCMopts to convert just one series
  tdcmOpts.seriesNumber[0] = seriesNo;
  tdcmOpts.numSeries = 1;

  return nii_loadDirCore(tdcmOpts.indir, &tdcmOpts);
}

// interface to nii_getMrifsStruct()
MRIFSSTRUCT* dcm2niix_fswrapper::getMrifsStruct(void)
{
  return nii_getMrifsStruct();
}

// return nifti header saved in MRIFSSTRUCT
nifti_1_header* dcm2niix_fswrapper::getNiiHeader(void)
{
  MRIFSSTRUCT* mrifsStruct = getMrifsStruct();
  return &mrifsStruct->hdr0;
}

// return image data saved in MRIFSSTRUCT
const unsigned char* dcm2niix_fswrapper::getMRIimg(void)
{
  MRIFSSTRUCT* mrifsStruct = getMrifsStruct();
  return mrifsStruct->imgM;
}

