#ifndef _NIFTI_ORTHO_CORE_
#define _NIFTI_ORTHO_CORE_

#ifdef  __cplusplus
extern "C" {
#endif
    
#ifndef USING_R
#include "nifti1.h"
#endif
    
    void mat2sForm (struct nifti_1_header *h, mat44 s);
    bool isMat44Canonical(mat44 R);
	unsigned char *  nii_setOrtho(unsigned char* img, struct nifti_1_header *h);
#ifdef  __cplusplus
}
#endif

#endif
