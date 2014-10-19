#ifndef _NIFTI_ORTHO_CORE_
#define _NIFTI_ORTHO_CORE_

#ifdef  __cplusplus
extern "C" {
#endif
    
#include "nifti1.h"

unsigned char *  nii_setOrtho(unsigned char* img, struct nifti_1_header *h);

#ifdef  __cplusplus
}
#endif

#endif
