#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>

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
void dcm2niix_fswrapper::setOpts(const char* dcmindir, const char* dcm2niixopts)
{
  memset(&tdcmOpts, 0, sizeof(tdcmOpts));
  setDefaultOpts(&tdcmOpts, NULL);

  if (dcmindir != NULL)
    strcpy(tdcmOpts.indir, dcmindir);

  // dcmunpack actually uses seriesDescription, set FName = `printf %04d.$descr $series`
  // change it from "%4s.%p" to "%4s.%d"
  strcpy(tdcmOpts.filename, "%4s.%d");

  // set the options for freesurfer mgz orientation
  tdcmOpts.isRotate3DAcq = false;
  tdcmOpts.isFlipY = false;
  tdcmOpts.isIgnoreSeriesInstanceUID = true;
  tdcmOpts.isCreateBIDS = false;
  tdcmOpts.isGz = false;
  tdcmOpts.isForceStackSameSeries = 1; // merge 2D slice '-m y', tdcmOpts.isForceStackSameSeries = 1
  tdcmOpts.isForceStackDCE = false;
  //tdcmOpts.isForceOnsetTimes = false;

  if (dcm2niixopts != NULL)
    __setDcm2niixOpts(dcm2niixopts);
}

/* set user dcm2niix conversion options in struct TDCMopts tdcmOpts
 * only subset of dcm2niix command line options are recognized: (https://manpages.ubuntu.com/manpages/jammy/en/man1/dcm2niix.1.html)
 *     -b <y/i/n>
 *            Save  additional  BIDS  metadata  to  a  side-car  .json  file  (default  y).
 *            The "i"nput-only option reads DICOMs but saves neither BIDS nor NIfTI.
 *
 *     -ba <y/n> anonymize BIDS (default y).
 *            If "n"o, side-car may report patient name, age and weight.
 *
 *     -f <format>
 *            Format string for the output filename(s). 
 *
 *     -i <y/n>
 *            Ignore derived, localizer and 2D images (default n)
 *
 *     -m <y/n/2>
 *            Merge slices from the same series regardless of study time, echo, coil, orientation, etc. (default 2).
 *            If "2", automatic based on image modality.
 *
 *     -v <2/y/n>
 *            Enable verbose output. "n" for succinct, "y" for verbose, "2" for high verbosity
 *
 *     -o <path>
 *            Output directory where the converted files should be saved.
 *
 *     -t <y/n>
 *            Save patient details as text notes.
 *
 *     -p <y/n>
 *            Use Philips precise float (rather than display) scaling.
 *
 *     -x <y/n/i>
 *            Crop images. This will attempt to remove excess neck from 3D acquisitions.
 *            If "i", images are neither cropped nor rotated to canonical space.
 */
void dcm2niix_fswrapper::__setDcm2niixOpts(const char *dcm2niixopts)
{
  //printf("[DEBUG] dcm2niix_fswrapper::__setDcm2niixOpts(%s)\n", dcm2niixopts);

  char *restOpts = (char*)malloc(strlen(dcm2niixopts)+1);
  memset(restOpts, 0, strlen(dcm2niixopts)+1);
  memcpy(restOpts, dcm2niixopts, strlen(dcm2niixopts));

  char *nextOpt = strtok_r((char*)dcm2niixopts, ",", &restOpts);
  while (nextOpt != NULL)
  {
    char *k = nextOpt;
    char *v = strchr(nextOpt, '=');
    if (v != NULL)
      *v = '\0';
    v++;  // move past '='

    // skip leading white spaces
    while (*k == ' ')
      k++;
    
    if (strcmp(k, "b") == 0)
    {
      if (*v == 'n' || *v == 'N' || *v == '0')
	tdcmOpts.isCreateBIDS = false;
      else if (*v == 'i' || *v == 'I')
      {
	tdcmOpts.isCreateBIDS = false;
        tdcmOpts.isOnlyBIDS = true;
      }
      else if (*v == 'y' || *v == 'Y')
	tdcmOpts.isCreateBIDS = true;
    }
    else if (strcmp(k, "ba") == 0)
      tdcmOpts.isAnonymizeBIDS = (*v == 'n' || *v == 'N') ? false : true;
    else if (strcmp(k, "f") == 0)
      strcpy(tdcmOpts.filename, v);
    else if (strcmp(k, "i") == 0)
      tdcmOpts.isIgnoreDerivedAnd2D = (*v == 'y' || *v == 'Y') ? true : false;
    else if (strcmp(k, "m") == 0)
    {
      if (*v == 'n' || *v == 'N' || *v == '0')
	tdcmOpts.isForceStackSameSeries = 0;
      else if (*v == 'y' || *v == 'Y' || *v == '1')
	tdcmOpts.isForceStackSameSeries = 1;
      else if (*v == '2')
	tdcmOpts.isForceStackSameSeries = 2;
      else if (*v == 'o' || *v == 'O')
	tdcmOpts.isForceStackDCE = false;
    }
    else if (strcmp(k, "v") == 0)
    {
      if (*v == 'n' || *v == 'N' || *v == '0')
	tdcmOpts.isVerbose = 0;
      else if (*v == 'h' || *v == 'H' || *v == '2')
	tdcmOpts.isVerbose = 2;
      else
	tdcmOpts.isVerbose = 1;
    }
    else if (strcmp(k, "o") == 0)
      strcpy(tdcmOpts.outdir, v);
    else if (strcmp(k, "t") == 0)
      tdcmOpts.isCreateText = (*v == 'y' || *v == 'Y') ? true : false;
    else if (strcmp(k, "p") == 0)
    {
      if (*v == 'n' || *v == 'N' || *v == '0')
        tdcmOpts.isPhilipsFloatNotDisplayScaling = false;
    }
    else if (strcmp(k, "x") ==0)
    {
      if (*v == 'y' || *v == 'Y')
	tdcmOpts.isCrop = true;
      else if (*v == 'i' || *v == 'I')
      {
	tdcmOpts.isRotate3DAcq = false;
	tdcmOpts.isCrop = false;
      }
    }
    else
    {
      printf("[WARN] dcm2niix option %s=%s skipped\n", k, v);
    }
    
    nextOpt = strtok_r(NULL, ",", &restOpts);
  }
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

// interface to nii_getMrifsStructVector()
std::vector<MRIFSSTRUCT>* dcm2niix_fswrapper::getMrifsStructVector(void)
{
  return nii_getMrifsStructVector();
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

void dcm2niix_fswrapper::dicomDump(const char* dicomdir, const char *series_info, bool max, bool extrainfo)
{
  strcpy(tdcmOpts.indir, dicomdir);
  tdcmOpts.isDumpNotConvert = true;
  nii_loadDirCore(tdcmOpts.indir, &tdcmOpts);

  char fnamecopy[2048] = {'\0'};
  memcpy(fnamecopy, series_info, strlen(series_info));
  char *logdir = dirname(fnamecopy);
  
  FILE *fpout = fopen(series_info, "w");

  std::vector<MRIFSSTRUCT> *mrifsStruct_vector = dcm2niix_fswrapper::getMrifsStructVector();
  int nitems = (*mrifsStruct_vector).size();
  for (int n = 0; n < nitems; n++)
  {
    struct TDICOMdata *tdicomData = &(*mrifsStruct_vector)[n].tdicomData;

    // output the dicom list for the series into seriesNum-dicomflst.txt
    char dicomflst[2048] = {'\0'};
    sprintf(dicomflst, "%s/%ld-dicomflst.txt", logdir, tdicomData->seriesNum);
    FILE *fp_dcmLst = fopen(dicomflst, "w");
    for (int nDcm = 0; nDcm < (*mrifsStruct_vector)[n].nDcm; nDcm++)
      fprintf(fp_dcmLst, "%s\n", (*mrifsStruct_vector)[n].dicomlst[nDcm]);
    fclose(fp_dcmLst);

    // output series_info
    fprintf(fpout, "%ld %s %f %f %f %f\\%f %c %f %s %s %s", 
                   tdicomData->seriesNum, tdicomData->seriesDescription,
                   tdicomData->TE, tdicomData->TR, tdicomData->flipAngle, tdicomData->xyzMM[1], tdicomData->xyzMM[2],
	           tdicomData->phaseEncodingRC, tdicomData->pixelBandwidth, (*mrifsStruct_vector)[n].dicomfile, tdicomData->imageType, (*mrifsStruct_vector)[n].pulseSequenceDetails);
#if 0
    if (max)
    {
      //$max kImageStart 0x7FE0 + (0x0010 << 16)
      fprintf(fpout, " max-value");
    }
#endif
    if (extrainfo)
      fprintf(fpout, " %s %s %s %s %f %s", tdicomData->patientName, tdicomData->studyDate, mfrCode2Str(tdicomData->manufacturer), tdicomData->manufacturersModelName, tdicomData->fieldStrength, tdicomData->deviceSerialNumber);

    fprintf(fpout, "\n");
  }
  fclose(fpout);

  return;
}

const char *dcm2niix_fswrapper::mfrCode2Str(int code)
{
  if (code == kMANUFACTURER_SIEMENS)
    return "SIEMENS";
  else if (code == kMANUFACTURER_GE)
    return "GE";
  else if (code == kMANUFACTURER_PHILIPS)
    return "PHILIPS";
  else if (code == kMANUFACTURER_TOSHIBA)
    return "TOSHIBA";
  else if (code == kMANUFACTURER_UIH)
    return "UIH";
  else if (code == kMANUFACTURER_BRUKER)
    return "BRUKER";
  else if (code == kMANUFACTURER_HITACHI)
    return "HITACHI";
  else if (code == kMANUFACTURER_CANON)
    return "CANON";
  else if (code == kMANUFACTURER_MEDISO)
    return "MEDISO";
  else
    return "UNKNOWN";
}


void dcm2niix_fswrapper::seriesInfoDump(FILE *fpdump, const MRIFSSTRUCT *pmrifsStruct)
{
  // print dicom-info for the series converted using (*mrifsStruct_vector)[0]
  // see output from mri_probedicom --i $f --no-siemens-ascii

  fprintf(fpdump, "###### %s ######\n", pmrifsStruct->dicomfile);

  const struct TDICOMdata *tdicomData = &(pmrifsStruct->tdicomData);

  // kManufacturer 0x0008 + (0x0070 << 16)
  fprintf(fpdump, "Manufacturer %s\n", dcm2niix_fswrapper::mfrCode2Str(tdicomData->manufacturer));

  // kManufacturersModelName 0x0008 + (0x1090 << 16)
  fprintf(fpdump, "ScannerModel %s\n", tdicomData->manufacturersModelName);

  // kSoftwareVersions 0x0018 + (0x1020 << 16) //LO
  fprintf(fpdump, "SoftwareVersion %s\n", tdicomData->softwareVersions);

  // kDeviceSerialNumber 0x0018 + (0x1000 << 16) //LO
  fprintf(fpdump, "ScannerSerialNo %s\n", tdicomData->deviceSerialNumber);

  // kInstitutionName 0x0008 + (0x0080 << 16)
  fprintf(fpdump, "Institution %s\n", tdicomData->institutionName);

  // kSeriesDescription 0x0008 + (0x103E << 16) // '0008' '103E' 'LO' 'SeriesDescription'
  fprintf(fpdump, "SeriesDescription %s\n", tdicomData->seriesDescription);

  // kStudyInstanceUID 0x0020 + (0x000D << 16)
  fprintf(fpdump, "StudyUID %s\n", tdicomData->studyInstanceUID);

  // kStudyDate 0x0008 + (0x0020 << 16)
  fprintf(fpdump, "StudyDate %s\n", tdicomData->studyDate);

  // kStudyTime 0x0008 + (0x0030 << 16)
  fprintf(fpdump, "StudyTime %s\n", tdicomData->studyTime);

  // kPatientName 0x0010 + (0x0010 << 16)
  fprintf(fpdump, "PatientName %s\n", tdicomData->patientName);

  // kSeriesNum 0x0020 + (0x0011 << 16)
  fprintf(fpdump, "SeriesNo %ld\n", tdicomData->seriesNum);

  // kImageNum 0x0020 + (0x0013 << 16)
  fprintf(fpdump, "ImageNo %d\n", tdicomData->imageNum);

  // kMRAcquisitionType 0x0018 + (0x0023 << 16)
  fprintf(fpdump, "AcquisitionType %s\n", (tdicomData->is3DAcq) ? "3D" : ((tdicomData->is3DAcq) ? "2D" : "unknown")); //dcm2niix has two values: d.is2DAcq, d.is3DAcq

  // kImageTypeTag 0x0008 + (0x0008 << 16)
  fprintf(fpdump, "ImageType %s\n", tdicomData->imageType);

  // kImagingFrequency 0x0018 + (0x0084 << 16) //DS
  fprintf(fpdump, "ImagingFrequency %f\n", tdicomData->imagingFrequency);

  // kPixelBandwidth 0x0018 + (0x0095 << 16) //'DS' 'PixelBandwidth'
  fprintf(fpdump, "PixelFrequency %f\n", tdicomData->pixelBandwidth);

  // dcm2niix doesn't seem to retrieve this  0x18, 0x85
  //fprintf(fpdump, "ImagedNucleus %s\n",e->d.string);

  // kEchoNum 0x0018 + (0x0086 << 16) //IS
  fprintf(fpdump, "EchoNumber %d\n", tdicomData->echoNum);

  // kMagneticFieldStrength 0x0018 + (0x0087 << 16) //DS
  fprintf(fpdump, "FieldStrength %f\n", tdicomData->fieldStrength);

  // kSequenceName 0x0018 + (0x0024 << 16)
  fprintf(fpdump, "PulseSequence %s\n", tdicomData->sequenceName);

  // kProtocolName 0x0018 + (0x1030 << 16)
  fprintf(fpdump, "ProtocolName %s\n", tdicomData->protocolName);

  // kScanningSequence 0x0018 + (0x0020 << 16)
  fprintf(fpdump, "ScanningSequence %s\n", tdicomData->scanningSequence);

  // dcm2niix doesn't seem to retrieve this
  // kTransmitCoilName 0x0018 + (0x1251 << 16) // SH issue527
  //fprintf(fpdump, "TransmittingCoil %s\n",e->d.string);

  // kPatientOrient 0x0018 + (0x5100 << 16) //0018,5100. patient orientation - 'HFS'
  fprintf(fpdump, "PatientPosition %s\n", tdicomData->patientOrient);

  // kFlipAngle 0x0018 + (0x1314 << 16)
  fprintf(fpdump, "FlipAngle %f\n", tdicomData->flipAngle);

  // kTE 0x0018 + (0x0081 << 16)
  fprintf(fpdump, "EchoTime %f\n", tdicomData->TE);

  // kTR 0x0018 + (0x0080 << 16)
  fprintf(fpdump, "RepetitionTime %f\n", tdicomData->TR);

  // kTI 0x0018 + (0x0082 << 16) // Inversion time
  fprintf(fpdump, "InversionTime %f\n", tdicomData->TI);

  // kPhaseEncodingSteps 0x0018 + (0x0089 << 16) //'IS'
  fprintf(fpdump, "NPhaseEnc %d\n", tdicomData->phaseEncodingSteps);

  // kInPlanePhaseEncodingDirection 0x0018 + (0x1312 << 16) //CS
  fprintf(fpdump, "PhaseEncDir %c\n", tdicomData->phaseEncodingRC);

  // kZSpacing 0x0018 + (0x0088 << 16) //'DS' 'SpacingBetweenSlices'
  fprintf(fpdump, "SliceDistance %f\n", tdicomData->zSpacing);

  // kZThick 0x0018 + (0x0050 << 16)
  fprintf(fpdump, "SliceThickness %f\n", tdicomData->zThick);  // d.zThick=d.xyzMM[3]

  // kXYSpacing 0x0028 + (0x0030 << 16) //DS 'PixelSpacing'
  fprintf(fpdump, "PixelSpacing %f\\%f\n", tdicomData->xyzMM[1], tdicomData->xyzMM[2]);

  // kDim2 0x0028 + (0x0010 << 16)
  fprintf(fpdump, "NRows %d\n", tdicomData->xyzDim[2]);

  // kDim1 0x0028 + (0x0011 << 16)
  fprintf(fpdump, "NCols %d\n", tdicomData->xyzDim[1]);

  // kBitsAllocated 0x0028 + (0x0100 << 16)
  fprintf(fpdump, "BitsPerPixel %d\n", tdicomData->bitsAllocated);

  // dcm2niix doesn't seem to retrieve this
  //fprintf(fpdump, "HighBit %d\n",*(e->d.us));

  // dcm2niix doesn't seem to retrieve this
  //fprintf(fpdump, "SmallestValue %d\n",*(e->d.us));

  // dcm2niix doesn't seem to retrieve this
  //fprintf(fpdump, "LargestValue %d\n",*(e->d.us));

  // kOrientation 0x0020 + (0x0037 << 16)
  //fprintf(fpdump, "ImageOrientation %f\\%f\\%f\\%f\\%f\\%f\n", 
  fprintf(fpdump, "ImageOrientation %g\\%g\\%g\\%g\\%g\\%g\n",
              tdicomData->orient[1], tdicomData->orient[2], tdicomData->orient[3], 
              tdicomData->orient[4], tdicomData->orient[5], tdicomData->orient[6]);  // orient[7] ???

  // kImagePositionPatient 0x0020 + (0x0032 << 16) // Actually !  patientPosition[4] ???
  fprintf(fpdump, "ImagePosition %f\\%f\\%f\n", 
              tdicomData->patientPosition[1], tdicomData->patientPosition[2], tdicomData->patientPosition[3]); //two values: d.patientPosition, d.patientPositionLast

  // dcm2niix doesn't seem to retrieve this
  // kSliceLocation 0x0020+(0x1041 << 16 ) //DS would be useful if not optional type 3
  //case kSliceLocation : //optional so useless, infer from image position patient (0020,0032) and image orientation (0020,0037)
  //	sliceLocation = dcmStrFloat(lLength, &buffer[lPos]);
  //	break;
  //fprintf(fpdump, "SliceLocation %s\n",e->d.string);

  // kTransferSyntax 0x0002 + (0x0010 << 16)
  fprintf(fpdump, "TransferSyntax %s\n", tdicomData->transferSyntax);

  // dcm2niix doesn't seem to retrieve this  0x51, 0x1016
  //fprintf(fpdump, "SiemensCrit %s\n",e->d.string);
}
