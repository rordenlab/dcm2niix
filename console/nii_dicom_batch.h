//
//

#ifndef MRIpro_nii_batch_h
#define MRIpro_nii_batch_h

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <string.h>
#include "nifti1.h"
#include "nifti1.h"
#include "nii_dicom.h"

    struct TDCMopts {
        bool isGz, isFlipY,isVerbose;
        char filename[512], outdir[512], indir[512], pigzname[512], optsname[512], indirParent[512];
    };
    void saveIniFile (struct TDCMopts opts);
    void readIniFile (struct TDCMopts *opts, const char * argv[]);
    //void readIniFile (struct TDCMopts *opts);
    int nii_loadDir (struct TDCMopts *opts) ;
    //int nii_createFilename(struct TDICOMdata dcm, char * niiFilename, struct TDCMopts opts);
    void  nii_createDummyFilename(char * niiFilename, struct TDCMopts opts);
#ifdef  __cplusplus
}
#endif

#endif
