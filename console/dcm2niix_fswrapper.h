#ifndef DCM2NIIX_FSWRAPPER_H
#define DCM2NIIX_FSWRAPPER_H

#include "nii_dicom_batch.h"
#include "nii_dicom.h"

/*
 * This is a wrapper class to interface with dcm2niix functions.
 * 1. The wrapper class provides interface to convert dicom in mgz orientation.
 * 2. The wrapper class and dcm2niix functions are compiled into libdcm2niixfs.a 
 *    with -DUSING_DCM2NIIXFSWRAPPER -DUSING_MGH_NIFTI_IO.
 * 3. When using libdcm2niixfs.a, instead of outputting .nii, *.bval, *.bvec to disk, 
 *    nifti header, image data, TDICOMdata, & TDTI information are saved in MRIFSSTRUCT struct.
 * 4. If libdcm2niixfs.a is compiled with -DUSING_MGH_NIFTI_IO, the application needs to link with nifti library.
 */
class dcm2niix_fswrapper
{
public:
  // set TDCMopts defaults, overwrite settings to output in mgz orientation.
  static void setOpts(const char* dcmindir, const char* niioutdir=NULL, bool createBIDS=false);

  // interface to isDICOMfile() in nii_dicom.cpp
  static bool isDICOM(const char* file);

  // interface to nii_loadDirCore() to search all dicom files from the directory input file is in,
  // and convert dicom files with the same series as given file.
  static int dcm2NiiOneSeries(const char* dcmfile);

  // interface to nii_getMrifsStruct()
  static MRIFSSTRUCT* getMrifsStruct(void);

  // return nifti header saved in MRIFSSTRUCT 
  static nifti_1_header* getNiiHeader(void);

  // return image data saved in MRIFSSTRUCT
  static const unsigned char* getMRIimg(void);

  static void dicomDump(const char* dicomdir);

private:
  static struct TDCMopts tdcmOpts;
};

#endif
