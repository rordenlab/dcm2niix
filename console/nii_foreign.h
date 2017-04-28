//Attempt to open non-DICOM image

#ifndef _NII_FOREIGN_
#define _NII_FOREIGN_

#ifdef  __cplusplus
extern "C" {
#endif

#include "nii_dicom_batch.h"

//int  open_foreign (const char *fn);
int  convert_foreign (const char *fn, struct TDCMopts opts);

#ifdef  __cplusplus
}
#endif

#endif