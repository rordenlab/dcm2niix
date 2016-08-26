//#define myNoSave //do not save images to disk
#ifdef _MSC_VER

	#include <direct.h>

	#define getcwd _getcwd

	#define chdir _chrdir

	#include "io.h"

	//#include <math.h>

    #define MiniZ
#else
	#include <unistd.h>
    #ifdef myDisableMiniZ
   		#undef MiniZ
    #else
    	#define MiniZ
    #endif
#endif

#if defined(__APPLE__) && defined(__MACH__)
//#include "nii_foreign.h"
#endif
#ifndef myDisableZLib
    #ifdef MiniZ
        #include "miniz.c"  //single file clone of libz
    #else
        #include <zlib.h>
    #endif

#endif
#ifdef myUseCOut
 #include <iostream>
#endif
#include "nifti1_io_core.h"
#include "nifti1.h"
#include "nii_dicom_batch.h"
#include "nii_dicom.h"
#include "tinydir.h"
#include <ctype.h> //toupper
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef myEnableOtsu
#include "nii_ostu_ml.h" //provide better brain crop, but artificially reduces signal variability in air
#endif
#include <time.h>  // clock_t, clock, CLOCKS_PER_SEC
#include "nii_ortho.h"
#if defined(_WIN64) || defined(_WIN32)
    #include <windows.h> //write to registry
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


//gcc -O3 -o main main.c nii_dicom.c
#if defined(_WIN64) || defined(_WIN32)
const char kPathSeparator ='\\';
const char kFileSep[2] = "\\";
#else
const char kPathSeparator ='/';
const char kFileSep[2] = "/";
#endif

struct TDCMsort {
    uint64_t indx, img;
};

struct TSearchList {
    unsigned long numItems, maxItems;
    char **str;
};

void dropFilenameFromPath(char *path) { //
   const char *dirPath = strrchr(path, '/'); //UNIX
   if (dirPath == 0)
      dirPath = strrchr(path, '\\'); //Windows
    if (dirPath == NULL) {
        strcpy(path,"");
    } else
        path[dirPath - path] = 0; // please make sure there is enough space in TargetDirectory
    if (strlen(path) == 0) { //file name did not specify path, assume relative path and return current working directory
    	//strcat (path,"."); //relative path - use cwd <- not sure if this works on Windows!
    	char cwd[1024];
   		getcwd(cwd, sizeof(cwd));
   		strcat (path,cwd);
    }
}


void getFileName( char *pathParent, const char *path) //if path is c:\d1\d2 then filename is 'd2'
{
    const char *filename = strrchr(path, '/'); //UNIX
    if (filename == 0) {
       filename = strrchr(path, '\\'); //Windows
       if (filename == NULL) filename = strrchr(path, ':'); //Windows
     }
    //const char *filename = strrchr(path, kPathSeparator); //x
    if (filename == NULL) {//no path separator
        strcpy(pathParent,path);
        return;
    }
    filename++;
    strcpy(pathParent,filename);
}

bool is_fileexists(const char * filename) {
    FILE * fp = NULL;
    if ((fp = fopen(filename, "r"))) {
        fclose(fp);
        return true;
    }
    return false;
}

#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif

bool is_fileNotDir(const char* path) { //returns false if path is a folder; requires #include <sys/stat.h>
    struct stat buf;
    stat(path, &buf);
    return S_ISREG(buf.st_mode);
} //is_file()

bool is_exe(const char* path) { //requires #include <sys/stat.h>
    struct stat buf;
    if (stat(path, &buf) != 0) return false; //file does not eist
    if (!S_ISREG(buf.st_mode)) return false; //not regular file, e.g. '..'
    return (buf.st_mode & 0111) ;
    //return (S_ISREG(buf.st_mode) && (buf.st_mode & 0111) );
} //is_exe()

int is_dir(const char *pathname, int follow_link) {
    struct stat s;
    if ((NULL == pathname) || (0 == strlen(pathname)))
        return 0;
    int err = stat(pathname, &s);
    if(-1 == err)
        return 0; /* does not exist */
    else {
        if(S_ISDIR(s.st_mode)) {
           return 1; /* it's a dir */
        } else {
            return 0;/* exists but is no dir */
        }
    }
} //is_dir

void geCorrectBvecs(struct TDICOMdata *d, int sliceDir, struct TDTI *vx){
    //0018,1312 phase encoding is either in row or column direction
    //0043,1039 (or 0043,a039). b value (as the first number in the string).
    //0019,10bb (or 0019,a0bb). phase diffusion direction
    //0019,10bc (or 0019,a0bc). frequency diffusion direction
    //0019,10bd (or 0019,a0bd). slice diffusion direction
    //These directions are relative to freq,phase,slice, so although no
    //transformations are required, you need to check the direction of the
    //phase encoding. This is in DICOM message 0018,1312. If this has value
    //COL then if swap the x and y value and reverse the sign on the z value.
    //If the phase encoding is not COL, then just reverse the sign on the x value.
    if (d->manufacturer != kMANUFACTURER_GE) return;
    if (d->CSA.numDti < 1) return;
    if ((toupper(d->patientOrient[0])== 'H') && (toupper(d->patientOrient[1])== 'F') && (toupper(d->patientOrient[2])== 'S'))
        ; //participant was head first supine
    else {
    	#ifdef myUseCOut
     	std::cout<<"GE DTI directions require head first supine acquisition" <<std::endl;
    	#else
        printf("GE DTI directions require head first supine acquisition\n");
		#endif
        return;
    }
    bool col = false;
    if (d->phaseEncodingRC== 'C')
        col = true;
    else if (d->phaseEncodingRC!= 'R') {
        printf("Error: Unable to determine DTI gradients, 0018,1312 should be either R or C");
        return;
    }
    if (abs(sliceDir) != 3)
        printf("Warning: GE DTI only tested for axial acquisitions (solution: use Xiangrui Li's dicm2nii)\n");
    //GE vectors from Xiangrui Li' dicm2nii, validated with datasets from https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging
	ivec3 flp;
	if (abs(sliceDir) == 1)
		flp = setiVec3(1, 1, 0); //SAGITTAL
	else if (abs(sliceDir) == 2)
		flp = setiVec3(0, 1, 1); //CORONAL
	else if (abs(sliceDir) == 3)
		flp = setiVec3(0, 0, 1); //AXIAL
	else
		printf("Impossible GE slice orientation!");
	if (sliceDir < 0)
    	flp.v[2] = 1 - flp.v[2];
    printf("Reorienting %s : %d GE DTI vectors: please validate. isCol=%d sliceDir=%d flp=%d %d %d\n", d->protocolName, d->CSA.numDti, col, sliceDir, flp.v[0], flp.v[1],flp.v[2]);
	if (!col)
		printf(" reorienting for ROW phase-encoding untested.\n");
    for (int i = 0; i < d->CSA.numDti; i++) {
        float vLen = sqrt( (vx[i].V[1]*vx[i].V[1])
                          + (vx[i].V[2]*vx[i].V[2])
                          + (vx[i].V[3]*vx[i].V[3]));
        if ((vx[i].V[0] <= FLT_EPSILON)|| (vLen <= FLT_EPSILON) ) { //bvalue=0
            for (int v= 0; v < 4; v++)
                vx[i].V[v] =0.0f;
            continue; //do not normalize or reorient b0 vectors
        }
        if (!col) { //rows need to be swizzled
        	//see Stanford dataset Ax_DWI_Tetrahedral_7 unable to resolve between possible solutions
        	// http://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging
            float swap = vx[i].V[1];
            vx[i].V[1] = vx[i].V[2];
            vx[i].V[2] = swap;
            vx[i].V[2] = -vx[i].V[2]; //because of transpose?
        }
		for (int v = 0; v < 3; v++)
			if (flp.v[v] == 1)
				vx[i].V[v+1] = -vx[i].V[v+1];
		vx[i].V[2] = -vx[i].V[2]; //we read out Y-direction opposite order as dicm2nii, see also opts.isFlipY
    }
    //These next lines are only so files appear identical to old versions of dcm2niix:
    //  dicm2nii and dcm2niix generate polar opposite gradient directions.
    //  this does not matter, since intensity is the normal of the gradient vector.
    for (int i = 0; i < d->CSA.numDti; i++)
    	for (int v = 1; v < 4; v++)
    		vx[i].V[v] = -vx[i].V[v];
    //These next lines convert any "-0" values to "0"
    for (int i = 0; i < d->CSA.numDti; i++)
    	for (int v = 1; v < 4; v++)
    		if (isSameFloat(vx[i].V[v],-0))
    			vx[i].V[v] = 0.0f;
} //geCorrectBvecs()

void siemensPhilipsCorrectBvecs(struct TDICOMdata *d, int sliceDir, struct TDTI *vx){
    //see Matthew Robson's  http://users.fmrib.ox.ac.uk/~robson/internal/Dicom2Nifti111.m
    //convert DTI vectors from scanner coordinates to image frame of reference
    //Uses 6 orient values from ImageOrientationPatient  (0020,0037)
    // requires PatientPosition 0018,5100 is HFS (head first supine)
    if ((d->manufacturer != kMANUFACTURER_SIEMENS) && (d->manufacturer != kMANUFACTURER_PHILIPS)) return;
    if (d->CSA.numDti < 1) return;
    if ((toupper(d->patientOrient[0])== 'H') && (toupper(d->patientOrient[1])== 'F') && (toupper(d->patientOrient[2])== 'S'))
        ; //participant was head first supine
    else {
    #ifdef myUseCOut
    std::cout<<"Siemens/Philips DTI directions require head first supine acquisition"<<std::endl;
    #else
        printf("Siemens/Philips DTI directions require head first supine acquisition\n");
        #endif
        return;
    }
    vec3 read_vector = setVec3(d->orient[1],d->orient[2],d->orient[3]);
    vec3 phase_vector = setVec3(d->orient[4],d->orient[5],d->orient[6]);
    vec3 slice_vector = crossProduct(read_vector ,phase_vector);
    read_vector = nifti_vect33_norm(read_vector);
    phase_vector = nifti_vect33_norm(phase_vector);
    slice_vector = nifti_vect33_norm(slice_vector);
    for (int i = 0; i < d->CSA.numDti; i++) {
        float vLen = sqrt( (vx[i].V[1]*vx[i].V[1])
                          + (vx[i].V[2]*vx[i].V[2])
                          + (vx[i].V[3]*vx[i].V[3]));
        if ((vx[i].V[0] <= FLT_EPSILON)|| (vLen <= FLT_EPSILON) ) { //bvalue=0
            if (vx[i].V[0] > FLT_EPSILON)
                printf("Warning: volume %d appears to be an ADC map (non-zero b-value with zero vector length)\n", i);
            //for (int v= 0; v < 4; v++)
            //    vx[i].V[v] =0.0f;
            continue; //do not normalize or reorient b0 vectors
        }//if bvalue=0
        vec3 bvecs_old =setVec3(vx[i].V[1],vx[i].V[2],vx[i].V[3]);
        vec3 bvecs_new =setVec3(dotProduct(bvecs_old,read_vector),dotProduct(bvecs_old,phase_vector),dotProduct(bvecs_old,slice_vector) );
        bvecs_new = nifti_vect33_norm(bvecs_new);
        vx[i].V[1] = bvecs_new.v[0];
        vx[i].V[2] = -bvecs_new.v[1];
        vx[i].V[3] = bvecs_new.v[2];
        if (abs(sliceDir) == kSliceOrientMosaicNegativeDeterminant) vx[i].V[2] = -vx[i].V[2];
        for (int v= 0; v < 4; v++)
            if (vx[i].V[v] == -0.0f) vx[i].V[v] = 0.0f; //remove sign from values that are virtually zero
    } //for each direction

    if (abs(sliceDir) == kSliceOrientMosaicNegativeDeterminant)
       printf("WARNING: please validate DTI vectors (matrix had a negative determinant, perhaps Siemens sagittal).\n");
    else if ( d->sliceOrient == kSliceOrientTra)
        printf("Saving %d DTI gradients. Please validate if you are conducting DTI analyses.\n", d->CSA.numDti);
    else
        printf("WARNING: DTI gradient directions only tested for axial (transverse) acquisitions. Please validate bvec files.\n");
} //siemensPhilipsCorrectBvecs()

bool isNanPosition (struct TDICOMdata d) { //in 2007 some Siemens RGB DICOMs did not include the PatientPosition 0020,0032 tag
    if (isnan(d.patientPosition[1])) return true;
    if (isnan(d.patientPosition[2])) return true;
    if (isnan(d.patientPosition[3])) return true;
    return false;
}

bool isSamePosition (struct TDICOMdata d, struct TDICOMdata d2){
    if ( isNanPosition(d) ||  isNanPosition(d2)) return false;
    if (!isSameFloat(d.patientPosition[1],d2.patientPosition[1])) return false;
    if (!isSameFloat(d.patientPosition[2],d2.patientPosition[2])) return false;
    if (!isSameFloat(d.patientPosition[3],d2.patientPosition[3])) return false;
    return true;
} //isSamePosition()

void nii_SaveText(char pathoutname[], struct TDICOMdata d, struct TDCMopts opts, struct nifti_1_header *h, char * dcmname) {
	if (!opts.isCreateText) return;
	char txtname[2048] = {""};
	strcpy (txtname,pathoutname);
    strcat (txtname,".txt");
    //printf("Saving text %s\n",txtname);
    FILE *fp = fopen(txtname, "w");
    fprintf(fp, "%s\tField Strength:\t%g\tProtocolName:\t%s\tScanningSequence00180020:\t%s\tTE:\t%g\tTR:\t%g\tSeriesNum:\t%ld\tAcquNum:\t%d\tImageNum:\t%d\tImageComments:\t%s\tDateTime:\t%F\tName:\t%s\tConvVers:\t%s\tDoB:\t%s\tGender:\t%s\tAge:\t%s\tDimXYZT:\t%d\t%d\t%d\t%d\tCoil:\t%d\tEchoNum:\t%d\tOrient(6)\t%g\t%g\t%g\t%g\t%g\t%g\tbitsAllocated\t%d\tInputName\t%s\n",
      pathoutname, d.fieldStrength, d.protocolName, d.scanningSequence, d.TE, d.TR, d.seriesNum, d.acquNum, d.imageNum, d.imageComments,
      d.dateTime, d.patientName, kDCMvers, d.birthDate, d.gender, d.age, h->dim[1], h->dim[2], h->dim[3], h->dim[4],
            d.coilNum,d.echoNum, d.orient[1], d.orient[2], d.orient[3], d.orient[4], d.orient[5], d.orient[6],
            d.bitsAllocated, dcmname);
    fclose(fp);
}

void nii_SaveBIDS(char pathoutname[], struct TDICOMdata d, struct TDCMopts opts, struct TDTI4D *dti4D, struct nifti_1_header *h) {
//https://docs.google.com/document/d/1HFUkAEE-pB-angVcYe6pf_-fVf4sCpOHKesUvfb8Grc/edit#
// Generate Brain Imaging Data Structure (BIDS) info
// sidecar JSON file (with the same  filename as the .nii.gz file, but with .json extension).
// we will use %g for floats since exponents are allowed
// we will not set the locale, so decimal separator is always a period, as required
//  https://www.ietf.org/rfc/rfc4627.txt
	if (!opts.isCreateBIDS) return;
	char txtname[2048] = {""};
    strcpy (txtname,pathoutname);
    strcat (txtname,".json");
    //printf("Saving DTI %s\n",txtname);
    FILE *fp = fopen(txtname, "w");
    fprintf(fp, "{\n");
	switch (d.manufacturer) {
				case kMANUFACTURER_SIEMENS:
						fprintf(fp, "\t\"Manufacturer\": \"Siemens\",\n" );
						break;
				case kMANUFACTURER_GE:
						fprintf(fp, "\t\"Manufacturer\": \"GE\",\n" );
						break;
				case kMANUFACTURER_PHILIPS:
						fprintf(fp, "\t\"Manufacturer\": \"Philips\",\n" );
						break;
				case kMANUFACTURER_TOSHIBA:
						fprintf(fp, "\t\"Manufacturer\": \"Toshiba\",\n" );
						break;
	};
	fprintf(fp, "\t\"ManufacturersModelName\": \"%s\",\n", d.manufacturersModelName );
	//if conditionals: the following values are required for DICOM MRI, but not available for CT
	if (d.fieldStrength > 0.0) fprintf(fp, "\t\"MagneticFieldStrength\": %g,\n", d.fieldStrength );
	if (d.flipAngle > 0.0) fprintf(fp, "\t\"FlipAngle\": %g,\n", d.flipAngle );
	if (d.TE > 0.0) fprintf(fp, "\t\"EchoTime\": %g,\n", d.TE / 1000.0 );
    if (d.TR > 0.0) fprintf(fp, "\t\"RepetitionTime\": %g,\n", d.TR / 1000.0 );
    if ((d.CSA.bandwidthPerPixelPhaseEncode > 0.0) &&  (h->dim[2] > 0) && (h->dim[1] > 0)) {
		float dwellTime = 0;
		if (d.phaseEncodingRC =='C')
			dwellTime =  1.0/d.CSA.bandwidthPerPixelPhaseEncode/h->dim[2];
		else
			dwellTime =  1.0/d.CSA.bandwidthPerPixelPhaseEncode/h->dim[1];
		fprintf(fp, "\t\"EffectiveEchoSpacing\": %g,\n", dwellTime );

    }
	bool first = 1;
	if (dti4D->S[0].sliceTiming >= 0.0) {
   		fprintf(fp, "\t\"SliceTiming\": [\n");
		for (int i = 0; i < kMaxDTI4D; i++) {
			if (dti4D->S[i].sliceTiming >= 0.0){
			  if (!first)
				  fprintf(fp, ",\n");
				else
				  first = 0;
				fprintf(fp, "\t\t%g", dti4D->S[i].sliceTiming / 1000.0 );
			}
		}
		fprintf(fp, "\t],\n");
	}
	if (d.phaseEncodingRC == 'C')
		fprintf(fp, "\t\"PhaseEncodingDirection\": \"j");
	else
		fprintf(fp, "\t\"PhaseEncodingDirection\": \"i");
	//phaseEncodingDirectionPositive has one of three values: UNKNOWN (-1), NEGATIVE (0), POSITIVE (1)
	//However, DICOM and NIfTI are reversed in the j (ROW) direction
	//Equivalent to dicm2nii's "if flp(iPhase), phPos = ~phPos; end"
	if ((d.CSA.phaseEncodingDirectionPositive == 1) && ((opts.isFlipY)))
		fprintf(fp, "-");
	if ((d.CSA.phaseEncodingDirectionPositive == 0) && ((!opts.isFlipY)))
		fprintf(fp, "-");
	fprintf(fp, "\"\n");
    fprintf(fp, "}\n");
    fclose(fp);
}

int nii_SaveDTI(char pathoutname[],int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TDCMopts opts, int sliceDir, struct TDTI4D *dti4D) {
    //reports non-zero if last volumes should be excluded (e.g. philip stores an ADC maps)
    //to do: works with 3D mosaics and 4D files, must remove repeated volumes for 2D sequences....
    uint64_t indx0 = dcmSort[0].indx; //first volume
    int numDti = dcmList[indx0].CSA.numDti;

    if (numDti < 1) return false;
    if ((numDti < 3) && (nConvert < 3)) return false;
    TDTI * vx = NULL;
    if (numDti > 2) {
        vx = (TDTI *)malloc(numDti * sizeof(TDTI));
        for (int i = 0; i < numDti; i++) //for each direction
            for (int v = 0; v < 4; v++) //for each vector+B-value
                    vx[i].V[v] = dti4D->S[i].V[v];
    } else { //if (numDti == 1) {//extract DTI from different slices
        vx = (TDTI *)malloc(nConvert * sizeof(TDTI));
        numDti = 0;
        for (int i = 0; i < nConvert; i++) { //for each image
            if ((dcmList[indx0].CSA.mosaicSlices > 1)  || (isSamePosition(dcmList[indx0],dcmList[dcmSort[i].indx]))) {
                //if (numDti < kMaxDTIv)
                for (int v = 0; v < 4; v++) //for each vector+B-value
                    vx[numDti].V[v] = dcmList[dcmSort[i].indx].CSA.dtiV[v];  //dcmList[indx0].CSA.dtiV[numDti][v] = dcmList[dcmSort[i].indx].CSA.dtiV[0][v];
                numDti++;

            } //for slices with repeats
        }//for each file
        dcmList[indx0].CSA.numDti = numDti;
    }
    bool bValueVaries = false;
    for (int i = 1; i < numDti; i++) //check if all bvalues match first volume
        if (vx[i].V[0] != vx[0].V[0]) bValueVaries = true;
    if (!bValueVaries) {
        bool bVecVaries = false;
        for (int i = 1; i < numDti; i++) {//check if all bvalues match first volume
            if (vx[i].V[1] != vx[0].V[1]) bVecVaries = true;
            if (vx[i].V[2] != vx[0].V[2]) bVecVaries = true;
            if (vx[i].V[3] != vx[0].V[3]) bVecVaries = true;
        }
        if (!bVecVaries) {
                free(vx);
                return false;
        }
        for (int i = 1; i < numDti; i++)
                printf("bxyz %g %g %g %g\n",vx[i].V[0],vx[i].V[1],vx[i].V[2],vx[i].V[3]);
        printf("Error: only one B-value reported for all volumes: %g\n",vx[0].V[0]);
        free(vx);
        return false;
    }
    int firstB0 = -1;
    for (int i = 0; i < numDti; i++) //check if all bvalues match first volume
        if (isSameFloat(vx[i].V[0],0) ) {
            firstB0 = i;
            break;
        }
    #ifdef myUseCOut
    if (firstB0 < 0)
    	std::cout<<"Warning: this diffusion series does not have a B0 (reference) volume"<<std::endl;
	if (firstB0 > 0)
    	std::cout<<"Note: B0 not the first volume in the series (FSL eddy reference volume is "<<firstB0<<")"<<std::endl;

	#else
    if (firstB0 < 0) printf("Warning: this diffusion series does not have a B0 (reference) volume\n");
    if (firstB0 > 0) printf("Note: B0 not the first volume in the series (FSL eddy reference volume is %d)\n", firstB0);
	#endif
    int numFinalADC = 0;
    if (dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_PHILIPS) {
        int i = numDti - 1;
        while ((i > 0) && (!isSameFloat(vx[i].V[0],0.0f)) && //not a B-0 image
                   ((isSameFloat(vx[i].V[1],0.0f)) &&
                   (isSameFloat(vx[i].V[2],0.0f)) &&
                   (isSameFloat(vx[i].V[3],0.0f)) ) ){//yet all vectors are zero!!!! must be ADC
                numFinalADC++; //final volume is ADC map
                numDti --; //remove final volume - it is a computed ADC map!
                dcmList[indx0].CSA.numDti = numDti;
                i --;
        } //
        if (numFinalADC > 0)
            printf("Note: final %d volumes appear to be ADC images that will be removed to allow processing\n", numFinalADC);
        /*for (int i = 0; i < (numDti); i++) {
            if ((!isSameFloat(vx[i].V[0],0.0f)) && //not a B-0 image
                ((isSameFloat(vx[i].V[1],0.0f)) &&
                 (isSameFloat(vx[i].V[2],0.0f)) &&
                 (isSameFloat(vx[i].V[3],0.0f)) ) )
                printf("Warning: volume %d appears to be an ADC volume %g %g %g\n", i+1, vx[i].V[1], vx[i].V[2], vx[i].V[3]);

        }*/
    }
    // philipsCorrectBvecs(&dcmList[indx0]); //<- replaced by unified siemensPhilips solution
    geCorrectBvecs(&dcmList[indx0],sliceDir, vx);
    siemensPhilipsCorrectBvecs(&dcmList[indx0],sliceDir, vx);
    if (!opts.isFlipY ) { //!FLIP_Y&& (dcmList[indx0].CSA.mosaicSlices < 2) mosaics are always flipped in the Y direction
        for (int i = 0; i < (numDti); i++) {
            if (fabs(vx[i].V[2]) > FLT_EPSILON)
                vx[i].V[2] = -vx[i].V[2];
        } //for each direction
    } //if not a mosaic
    if (opts.isVerbose) {
        for (int i = 0; i < (numDti); i++) {
            printf("%d\tB=\t%g\tVec=\t%g\t%g\t%g\n",i, vx[i].V[0],
                   vx[i].V[1],vx[i].V[2],vx[i].V[3]);

        } //for each direction
    }
    //printf("%f\t%f\t%f",dcmList[indx0].CSA.dtiV[1][1],dcmList[indx0].CSA.dtiV[1][2],dcmList[indx0].CSA.dtiV[1][3]);
    char txtname[2048] = {""};
    strcpy (txtname,pathoutname);
    strcat (txtname,".bval");
    //printf("Saving DTI %s\n",txtname);
    FILE *fp = fopen(txtname, "w");
    if (fp == NULL) {
        free(vx);
        return numFinalADC;
    }
    for (int i = 0; i < (numDti-1); i++) {
        if (opts.isCreateBIDS) {
            fprintf(fp, "%g ", vx[i].V[0]);
        } else {
            fprintf(fp, "%g\t", vx[i].V[0]);
        }
	}
    fprintf(fp, "%g\n", vx[numDti-1].V[0]);
    fclose(fp);
    strcpy(txtname,pathoutname);
    strcat (txtname,".bvec");
    //printf("Saving DTI %s\n",txtname);
    fp = fopen(txtname, "w");
    if (fp == NULL) {
        free(vx);
        return numFinalADC;
    }
    for (int v = 1; v < 4; v++) {
        for (int i = 0; i < (numDti-1); i++) {
            if (opts.isCreateBIDS) {
                fprintf(fp, "%g ", vx[i].V[v]);
            } else {
                fprintf(fp, "%g\t", vx[i].V[v]);
            }
        }
        fprintf(fp, "%g\n", vx[numDti-1].V[v]);
    }
    fclose(fp);
    free(vx);
    return numFinalADC;
} //nii_SaveDTI()

float sqr(float v){
    return v*v;
}  //sqr()

float intersliceDistance(struct TDICOMdata d1, struct TDICOMdata d2) {
    //some MRI scans have gaps between slices, some CT have overlapping slices. Comparing adjacent slices provides measure for dx between slices
    if ( isNanPosition(d1) ||  isNanPosition(d2))
        return d1.xyzMM[3];
    float tilt = 1.0;
    if (d1.gantryTilt != 0)
        tilt = (float) cos(d1.gantryTilt  * M_PI/180); //for CT scans with gantry tilt, we need to compute distance between slices, not distance along bed
    return tilt * sqrt( sqr(d1.patientPosition[1]-d2.patientPosition[1])+
                sqr(d1.patientPosition[2]-d2.patientPosition[2])+
                sqr(d1.patientPosition[3]-d2.patientPosition[3]));
} //intersliceDistance()

void swapDim3Dim4(int d3, int d4, struct TDCMsort dcmSort[]) {
    //swap space and time: input A0,A1...An,B0,B1...Bn output A0,B0,A1,B1,...
    int nConvert = d3 * d4;
#ifdef _MSC_VER
	TDCMsort * dcmSortIn = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
#else
    struct TDCMsort dcmSortIn[nConvert];
#endif
    for (int i = 0; i < nConvert; i++) dcmSortIn[i] = dcmSort[i];
    int i = 0;
    for (int b = 0; b < d3; b++)
        for (int a = 0; a < d4; a++) {
            int k = (a *d3) + b;
            //printf("%d -> %d %d ->%d\n",i,a, b, k);
            dcmSort[k] = dcmSortIn[i];
            i++;
        }
#ifdef _MSC_VER
	free(dcmSortIn);
#endif
} //swapDim3Dim4()

bool intensityScaleVaries(int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[]){
    //detect whether some DICOM images report different intensity scaling
    //some Siemens PET scanners generate 16-bit images where slice has its own scaling factor.
    // since NIfTI provides a single scaling factor for each file, these images require special consideration
    if (nConvert < 2) return false;
    bool iVaries = false;
    float iScale = dcmList[dcmSort[0].indx].intenScale;
    float iInter = dcmList[dcmSort[0].indx].intenIntercept;
    for (int i = 1; i < nConvert; i++) { //stack additional images
        uint64_t indx = dcmSort[i].indx;
        if (fabs (dcmList[indx].intenScale - iScale) > FLT_EPSILON) iVaries = true;
        if (fabs (dcmList[indx].intenIntercept- iInter) > FLT_EPSILON) iVaries = true;
    }
    return iVaries;
} //intensityScaleVaries()

/*unsigned char * nii_bgr2rgb(unsigned char* bImg, struct nifti_1_header *hdr) {
 //DICOM planarappears to be BBB..B,GGG..G,RRR..R, NIfTI RGB saved in planes RRR..RGGG..GBBBB..B
 //  see http://www.barre.nom.fr/medical/samples/index.html US-RGB-8-epicard
 if (hdr->datatype != DT_RGB24) return bImg;
 int dim3to7 = 1;
 for (int i = 3; i < 8; i++)
 if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
 int sliceBytes24 = hdr->dim[1]*hdr->dim[2] * hdr->bitpix/8;
 int sliceBytes8 = hdr->dim[1]*hdr->dim[2];
 //Byte bImg[ bSz ];
 //[img getBytes:&bImg length:bSz];
 unsigned char slice24[sliceBytes24];
 int sliceOffsetR = 0;
 for (int sl = 0; sl < dim3to7; sl++) { //for each 2D slice
 memcpy(&slice24, &bImg[sliceOffsetR], sliceBytes24);
 memcpy( &bImg[sliceOffsetR], &slice24[sliceBytes8*2], sliceBytes8);
 sliceOffsetR += sliceBytes8;
 memcpy( &bImg[sliceOffsetR], &slice24[sliceBytes8], sliceBytes8);
 sliceOffsetR += sliceBytes8;
 memcpy( &bImg[sliceOffsetR], &slice24[0], sliceBytes8);
 sliceOffsetR += sliceBytes8;
 } //for each slice
 return bImg;
 } //nii_ImgBytes()*/

bool niiExists(const char*pathoutname) {
    char niiname[2048] = {""};
    strcat (niiname,pathoutname);
    strcat (niiname,".nii");
    if (is_fileexists(niiname)) return true;
    char gzname[2048] = {""};
    strcat (gzname,pathoutname);
    strcat (gzname,".nii.gz");
    if (is_fileexists(gzname)) return true;
    return false;
} //niiExists()

#ifndef W_OK
#define W_OK 2 /* write mode check */
#endif

int nii_createFilename(struct TDICOMdata dcm, char * niiFilename, struct TDCMopts opts) {
    char pth[1024] = {""};
    if (strlen(opts.outdir) > 0) {
        strcpy(pth, opts.outdir);
        int w =access(pth,W_OK);
        if (w != 0) {
            if (getcwd(pth, sizeof(pth)) != NULL) {
            w =access(pth,W_OK);
            if (w != 0) {
            	#ifdef myUseCOut
    			std::cout<<"Error: you do not have write permissions for the directory "<<opts.outdir<<std::endl;
				#else
                printf("Error: you do not have write permissions for the directory %s\n",opts.outdir);
                #endif
                return EXIT_FAILURE;
            }
            #ifdef myUseCOut
    		std::cout<<"Warning: "<<opts.outdir<<" write permission denied. Saving to working directory "<<pth<<std::endl;
			#else
            printf("Warning: %s write permission denied. Saving to working directory %s \n", opts.outdir, pth);
            #endif
            }
        }
     }
    char inname[1024] = {""};//{"test%t_%av"}; //% a = acquisition, %n patient name, %t time
    strcpy(inname, opts.filename);
    char outname[1024] = {""};
    char newstr[256];
    if (strlen(inname) < 1) {
        strcpy(inname, "T%t_N%n_S%s");
    }
    int start = 0;
    int pos = 0;
    bool isCoilReported = false;
    bool isEchoReported = false;
    while (pos < strlen(inname)) {
        if (inname[pos] == '%') {
            if (pos > start) {
                strncpy(&newstr[0], &inname[0] + start, pos - start);
                newstr[pos - start] = '\0';
                strcat (outname,newstr);
            }
            pos++; //extra increment: skip both % and following character
            char f = 'P';
            if (pos < strlen(inname)) f = toupper(inname[pos]);
        	if ((f == 'A') && (dcm.coilNum > 0)) {
        		isCoilReported = true;
                sprintf(newstr, "%02d", dcm.coilNum);
                strcat (outname,newstr);
            }
        	if (f == 'E') {
        		isEchoReported = true;
                sprintf(newstr, "%d", dcm.echoNum);
                strcat (outname,newstr);
            }
            if (f == 'C')
                strcat (outname,dcm.imageComments);
            if (f == 'D')
                strcat (outname,dcm.seriesDescription);
            if (f == 'F')
                strcat (outname,opts.indirParent);
            if (f == 'I')
                strcat (outname,dcm.patientID);
            if (f == 'M') {
                if (dcm.manufacturer == kMANUFACTURER_GE)
                    strcat (outname,"GE");
                else if (dcm.manufacturer == kMANUFACTURER_TOSHIBA)
                    strcat (outname,"To");
                else if (dcm.manufacturer == kMANUFACTURER_PHILIPS)
                    strcat (outname,"Ph");
                else if (dcm.manufacturer == kMANUFACTURER_SIEMENS)
                    strcat (outname,"Si");
                else
                    strcat (outname,"NA"); //manufacturer name not available
            }
            if (f == 'N')
                strcat (outname,dcm.patientName);
            if (f == 'P')
                strcat (outname,dcm.protocolName);
            if (f == 'Q')
                strcat (outname,dcm.scanningSequence);
            if ((f >= '0') && (f <= '9')) {
                if ((pos<strlen(inname)) && (toupper(inname[pos+1]) == 'S')) {
                    char zeroPad[12] = {""};
                    sprintf(zeroPad,"%%0%dd",atoi(&f));
                    sprintf(newstr, zeroPad, dcm.seriesNum);
                    strcat (outname,newstr);
                    pos++; // e.g. %3f requires extra increment: skip both number and following character
                }
            }
            if (f == 'S') {
                sprintf(newstr, "%ld", dcm.seriesNum);
                strcat (outname,newstr);
            }
            if (f == 'T') {
                sprintf(newstr, "%0.0f", dcm.dateTime);
                strcat (outname,newstr);
            }
            if (f == 'Z')
                strcat (outname,dcm.sequenceName);
            start = pos + 1;
        } //found a % character
        pos++;
    } //for each character in input
    if (!isCoilReported && (dcm.coilNum > 1)) {
        sprintf(newstr, "_c%d", dcm.coilNum);
        strcat (outname,newstr);
    }
    if (!isEchoReported && (dcm.echoNum > 1)) {
        sprintf(newstr, "_e%d", dcm.echoNum);
        strcat (outname,newstr);
    }
    if (dcm.isHasPhase)
    	strcat (outname,"_ph"); //manufacturer name not available
    if (pos > start) { //append any trailing characters
        strncpy(&newstr[0], &inname[0] + start, pos - start);
        newstr[pos - start] = '\0';
        strcat (outname,newstr);
    }
    if (strlen(outname) < 1) strcpy(outname, "dcm2nii_invalidName");
    if (outname[0] == '.') outname[0] = '_'; //make sure not a hidden file
    //eliminate illegal characters http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx
    for (int pos = 0; pos<strlen(outname); pos ++)
        if ((outname[pos] == '<') || (outname[pos] == '>') || (outname[pos] == ':')
            || (outname[pos] == '"') || (outname[pos] == '\\') || (outname[pos] == '/')
            || (outname[pos] == '^')
            || (outname[pos] == '*') || (outname[pos] == '|') || (outname[pos] == '?'))
            outname[pos] = '_';
    //printf("outname=*%s* %d %d\n", outname, pos,start);
    char baseoutname[2048] = {""};
    strcat (baseoutname,pth);
    char appendChar[2] = {"a"};
    appendChar[0] = kPathSeparator;
    if (pth[strlen(pth)-1] != kPathSeparator)
        strcat (baseoutname,appendChar);
    strcat (baseoutname,outname);
    char pathoutname[2048] = {""};
    strcat (pathoutname,baseoutname);
    int i = 0;
    while (niiExists(pathoutname) && (i < 26)) {
        strcpy(pathoutname,baseoutname);
        appendChar[0] = 'a'+i;
        strcat (pathoutname,appendChar);
        i++;
    }
    if (i >= 26) {
            #ifdef myUseCOut
    		std::cout<<"Error: too many NIFTI images with the name "<<baseoutname<<std::endl;
			#else
        printf("Error: too many NIFTI images with the name %s\n", baseoutname);
        #endif
        return EXIT_FAILURE;
    }
    //printf("-->%s\n",pathoutname); return EXIT_SUCCESS;
    //printf("outname=%s\n", pathoutname);
    strcpy(niiFilename,pathoutname);
    return EXIT_SUCCESS;
} //nii_createFilename()

void  nii_createDummyFilename(char * niiFilename, struct TDCMopts opts) {
    //generate string that illustrating sample of filename
    struct TDICOMdata dcm = clear_dicom_data();
    strcpy(opts.indirParent,"myFolder");
    char niiFilenameBase[1024] = {"/usr/myFolder/dicom.dcm"};
    nii_createFilename(dcm, niiFilenameBase, opts) ;
    strcpy(niiFilename,"Example output filename: '");
    strcat(niiFilename,niiFilenameBase);
    if (opts.isGz)
        strcat(niiFilename,".nii.gz'");
    else
        strcat(niiFilename,".nii'");
} //nii_createDummyFilename()

#ifndef myDisableZLib


#ifndef MiniZ
unsigned long mz_compressBound(unsigned long source_len) {
	return compressBound(source_len);
}

unsigned long mz_crc32(unsigned long crc, const unsigned char *ptr, size_t buf_len) {
    return crc32(crc, ptr, (uInt) buf_len);
}
#endif

void writeNiiGz (char * baseName, struct nifti_1_header hdr,  unsigned char* src_buffer, unsigned long src_len) {
    //create gz file in RAM, save to disk http://www.zlib.net/zlib_how.html
    // in general this single-threaded approach is slower than PIGZ but is useful for slow (network attached) disk drives
    char fname[2048] = {""};
    strcpy (fname,baseName);
    strcat (fname,".nii.gz");
    unsigned long hdrPadBytes = sizeof(hdr) + 4; //348 byte header + 4 byte pad
    unsigned long cmp_len = mz_compressBound(src_len+hdrPadBytes);
    unsigned char *pCmp = (unsigned char *)malloc(cmp_len);
    z_stream strm;
    strm.total_in = 0;
    strm.total_out = 0;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_out = pCmp; // output char array
    strm.avail_out = (unsigned int)cmp_len; // size of output
    //if ( deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY)!= Z_OK) return;
    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION)!= Z_OK) {
        free(pCmp);
        return;
    }
    //add header
    unsigned char *pHdr = (unsigned char *)malloc(hdrPadBytes);
    pHdr[hdrPadBytes-1] = 0; pHdr[hdrPadBytes-2] = 0; pHdr[hdrPadBytes-3] = 0; pHdr[hdrPadBytes-4] = 0;
    memcpy(pHdr,&hdr, sizeof(hdr));
    strm.avail_in = (unsigned int)hdrPadBytes; // size of input
	strm.next_in = (uint8_t *)pHdr; // input header -- TPX  strm.next_in = (Bytef *)pHdr; uint32_t
    deflate(&strm, Z_NO_FLUSH);
    //add image
    strm.avail_in = (unsigned int)src_len; // size of input
	strm.next_in = (uint8_t *)src_buffer; // input image -- TPX strm.next_in = (Bytef *)src_buffer;
    deflate(&strm, Z_FINISH); //Z_NO_FLUSH;
    //finish up
    deflateEnd(&strm);
    unsigned long file_crc32 = mz_crc32(0L, Z_NULL, 0);
    file_crc32 = mz_crc32(file_crc32, pHdr, (unsigned int)hdrPadBytes);
    file_crc32 = mz_crc32(file_crc32, src_buffer, (unsigned int)src_len);
    cmp_len = strm.total_out;
    if (cmp_len <= 0) {
        free(pCmp);
        free(src_buffer);
        return;
    }
    FILE *fileGz = fopen(fname, "wb");
    if (!fileGz) {
        free(pCmp);
        free(src_buffer);
        return;
    }
    //write header http://www.gzip.org/zlib/rfc-gzip.html
    fputc((char)0x1f, fileGz); //ID1
    fputc((char)0x8b, fileGz); //ID2
    fputc((char)0x08, fileGz); //CM - use deflate compression method
    fputc((char)0x00, fileGz); //FLG - no addition fields
    fputc((char)0x00, fileGz); //MTIME0
    fputc((char)0x00, fileGz); //MTIME1
    fputc((char)0x00, fileGz); //MTIME2
    fputc((char)0x00, fileGz); //MTIME2
    fputc((char)0x00, fileGz); //XFL
    fputc((char)0xff, fileGz); //OS
    //write Z-compressed data
    fwrite (&pCmp[2] , sizeof(char), cmp_len-6, fileGz); //-6 as LZ78 format has 2 bytes header (typically 0x789C) and 4 bytes tail (ADLER 32)
    //write tail: write redundancy check and uncompressed size as bytes to ensure LITTLE-ENDIAN order
    fputc((unsigned char)(file_crc32), fileGz);
    fputc((unsigned char)(file_crc32 >> 8), fileGz);
    fputc((unsigned char)(file_crc32 >> 16), fileGz);
    fputc((unsigned char)(file_crc32 >> 24), fileGz);
    fputc((unsigned char)(strm.total_in), fileGz);
    fputc((unsigned char)(strm.total_in >> 8), fileGz);
    fputc((unsigned char)(strm.total_in >> 16), fileGz);
    fputc((unsigned char)(strm.total_in >> 24), fileGz);
    fclose(fileGz);
    free(pCmp);
    free(pHdr);
} //writeNiiGz()
#endif

int nii_saveNII(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts) {
    hdr.vox_offset = 352;
    size_t imgsz = nii_ImgBytes(hdr);
    #ifndef myDisableZLib
    if  ((opts.isGz) &&  (strlen(opts.pigzname)  < 1) &&  ((imgsz+hdr.vox_offset) <  2147483647) ) { //use internal compressor
    //if (true) {//TPX
        writeNiiGz (niiFilename, hdr,  im,imgsz);
        return EXIT_SUCCESS;
    }
    #endif
    char fname[2048] = {""};
    strcpy (fname,niiFilename);
    strcat (fname,".nii");
    FILE *fp = fopen(fname, "wb");
    if (!fp) return EXIT_FAILURE;
    fwrite(&hdr, sizeof(hdr), 1, fp);
    uint32_t pad = 0;
    fwrite(&pad, sizeof( pad), 1, fp);
    fwrite(&im[0], imgsz, 1, fp);
    fclose(fp);
    if ((opts.isGz) &&  (strlen(opts.pigzname)  > 0) ) {
    	char command[768];
    	strcpy(command, "\"" );
        strcat(command, opts.pigzname );
        strcat(command, "\" -n -f \""); //current versions of pigz (2.3) built on Windows can hang if the filename is included, presumably because it is not finding the path characters ':\'
        strcat(command, fname);
        strcat(command, "\""); //add quotes in case spaces in filename 'pigz "c:\my dir\img.nii"'
    	#if defined(_WIN64) || defined(_WIN32) //using CreateProcess instead of system to run in background (avoids screen flicker)
    		DWORD exitCode;
   		PROCESS_INFORMATION ProcessInfo = {0};
   		STARTUPINFO startupInfo= {0};
   		startupInfo.cb = sizeof(startupInfo);
    		//StartupInfo.cb = sizeof StartupInfo ; //Only compulsory field
    		if(CreateProcess(NULL, command, NULL,NULL,FALSE,NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW,NULL, NULL,&startupInfo,&ProcessInfo)) {
                //printf("compression --- %s\n",command);
        		WaitForSingleObject(ProcessInfo.hProcess,INFINITE);
        		CloseHandle(ProcessInfo.hThread);
        		CloseHandle(ProcessInfo.hProcess);
    		} else
    			printf("compression failed %s\n",command);
    	#else //if win else linux
        system(command);
        #endif //else linux
        #ifdef myUseCOut
    	std::cout<<"compress: "<<command<<std::endl;
		#else
        printf("compress: %s\n",command);
        #endif
    }
    return EXIT_SUCCESS;
} //nii_saveNII()


int nii_saveNII3D(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts) {
    //save 4D series as sequence of 3D volumes
    struct nifti_1_header hdr1 = hdr;
    int nVol = 1;
    for (int i = 4; i < 8; i++) {
        if (hdr.dim[i] > 1) nVol = nVol * hdr.dim[i];
        hdr1.dim[i] = 0;
    }
    hdr1.dim[0] = 3; //save as 3D file
    size_t imgsz = nii_ImgBytes(hdr1);
    size_t pos = 0;
    char fname[2048] = {""};
    char zeroPad[1024] = {""};
	double fnVol = nVol;
	int zeroPadLen = (1 + log10( fnVol));
    sprintf(zeroPad,"%%s_%%0%dd",zeroPadLen);
    for (int i = 1; i <= nVol; i++) {
        sprintf(fname,zeroPad,niiFilename,i);
        if (nii_saveNII(fname, hdr1, (unsigned char*)&im[pos], opts) == EXIT_FAILURE) return EXIT_FAILURE;
        pos += imgsz;
    }
    return EXIT_SUCCESS;
} //nii_saveNII3D

void nii_check16bitUnsigned(unsigned char *img, struct nifti_1_header *hdr){
    //default NIfTI 16-bit is signed, set to unusual 16-bit unsigned if required...
    if (hdr->datatype != DT_UINT16) return;
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    int nVox = hdr->dim[1]*hdr->dim[2]* dim3to7;
    if (nVox < 1) return;
    unsigned short * img16 = (unsigned short*) img;
    unsigned short max16 = img16[0];
    //clock_t start = clock();
    for (int i=0; i < nVox; i++)
        if (img16[i] > max16)
            max16 = img16[i];
    //printf("max16= %d vox=%d %fms\n",max16, nVox, ((double)(clock()-start))/1000);
    if (max16 > 32767)
            #ifdef myUseCOut
    	std::cout<<"Note: intensity range requires saving as rare 16-bit UNSIGNED integer. Subsequent tools may require 32-bit conversion"<<std::endl;
		#else
        printf("Note: intensity range requires saving as rare 16-bit UNSIGNED integer. Subsequent tools may require 32-bit conversion\n");
    	#endif
    else
        hdr->datatype = DT_INT16;
} //nii_check16bitUnsigned()

int siemensCtKludge(int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[]) {
    //Siemens CT bug: when a user draws an open object graphics object onto a 2D slice this is appended as an additional image,
    //regardless of slice position. These images do not report number of positions in the volume, so we need tedious leg work to detect
    uint64_t indx0 = dcmSort[0].indx;
    if ((nConvert < 2) ||(dcmList[indx0].manufacturer != kMANUFACTURER_SIEMENS) || (!isSameFloat(dcmList[indx0].TR ,0.0f))) return nConvert;
    float prevDx = 0.0;
    for (int i = 1; i < nConvert; i++) {
        float dx = intersliceDistance(dcmList[indx0],dcmList[dcmSort[i].indx]);
        if ((!isSameFloat(dx,0.0f)) && (dx < prevDx)) {
            #ifdef myUseCOut
            std::cout<<"Slices skipped: image position not sequential, admonish your vendor (Siemens OOG?)"<<std::endl;
            #else
            printf("Slices skipped: image position not sequential, admonish your vendor (Siemens OOG?)\n");
            #endif
            return i;
        }
        prevDx = dx;
    }
    return nConvert; //all images in sequential order
}

int isSameFloatT (float a, float b, float tolerance) {
    return (fabs (a - b) <= tolerance);
}

unsigned char * nii_saveNII3Dtilt(char * niiFilename, struct nifti_1_header * hdr, unsigned char* im, struct TDCMopts opts, float * sliceMMarray, float gantryTiltDeg, int manufacturer ) {
    //correct for gantry tilt - http://www.mathworks.com/matlabcentral/fileexchange/24458-dicom-gantry-tilt-correction
    if (gantryTiltDeg == 0.0) return im;
    struct nifti_1_header hdrIn = *hdr;
    int nVox2DIn = hdrIn.dim[1]*hdrIn.dim[2];
    if ((nVox2DIn < 1) || (hdrIn.dim[0] != 3) || (hdrIn.dim[3] < 3)) return im;
    if (hdrIn.datatype != DT_INT16) {
        printf("Only able to correct gantry tilt for 16-bit integer data with at least 3 slices.");
        return im;
    }
    printf("Gantry Tilt Correction is new: please validate conversions\n");
    float GNTtanPx = tan(gantryTiltDeg / (180/M_PI))/hdrIn.pixdim[2]; //tangent(degrees->radian)
    //unintuitive step: reverse sign for negative gantry tilt, therefore -27deg == +27deg (why @!?#)
    // seen in http://www.mathworks.com/matlabcentral/fileexchange/28141-gantry-detector-tilt-correction/content/gantry2.m
    // also validated with actual data...
    if (manufacturer == kMANUFACTURER_PHILIPS) //see 'Manix' example from Osirix
        GNTtanPx = - GNTtanPx;
    else if ((manufacturer == kMANUFACTURER_SIEMENS) && (gantryTiltDeg > 0.0))
        GNTtanPx = - GNTtanPx;
    else if (manufacturer == kMANUFACTURER_GE)
        ; //do nothing
    else
        if (gantryTiltDeg < 0.0) GNTtanPx = - GNTtanPx; //see Toshiba examples from John Muschelli
    // printf("gantry tilt pixels per mm %g\n",GNTtanPx);
    short * imIn16 = ( short*) im;
	//create new output image: larger due to skew
	// compute how many pixels slice must be extended due to skew
    int s = hdrIn.dim[3] - 1; //top slice
    float maxSliceMM = fabs(s * hdrIn.pixdim[3]);
    if (sliceMMarray != NULL) maxSliceMM = fabs(sliceMMarray[s]);
    int pxOffset = ceil(fabs(GNTtanPx*maxSliceMM));
    // printf("Tilt extends slice by %d pixels", pxOffset);
	hdr->dim[2] = hdr->dim[2] + pxOffset;
	int nVox2D = hdr->dim[1]*hdr->dim[2];
	unsigned char * imOut = (unsigned char *)malloc(nVox2D * hdrIn.dim[3] * 2);// *2 as 16-bits per voxel, sizeof( short) );
	short * imOut16 = ( short*) imOut;
	//set surrounding voxels to darkest observed value
	int minVoxVal = imIn16[0];
	for (int v = 0; v < (nVox2DIn * hdrIn.dim[3]); v++)
		if (imIn16[v] < minVoxVal)
			minVoxVal = imIn16[v];
	for (int v = 0; v < (nVox2D * hdrIn.dim[3]); v++)
		imOut16[v] = minVoxVal;
	//copy skewed voxels
	for (int s = 0; s < hdrIn.dim[3]; s++) { //for each slice
		float sliceMM = s * hdrIn.pixdim[3];
		if (sliceMMarray != NULL) sliceMM = sliceMMarray[s]; //variable slice thicknesses
		//sliceMM -= mmMidZ; //adjust so tilt relative to middle slice
		if (GNTtanPx < 0)
			sliceMM -= maxSliceMM;
		float Offset = GNTtanPx*sliceMM;
		float fracHi =  ceil(Offset) - Offset; //ceil not floor since rI=r-Offset not rI=r+Offset
		float fracLo = 1.0f - fracHi;
		for (int r = 0; r < hdr->dim[2]; r++) { //for each row of output
			float rI = (float)r - Offset; //input row
			if ((rI >= 0.0) && (rI < hdrIn.dim[2])) {
				int rLo = floor(rI);
				int rHi = rLo + 1;
				if (rHi >= hdrIn.dim[2]) rHi = rLo;
				rLo = (rLo * hdrIn.dim[1]) + (s * nVox2DIn); //offset to start of row below
				rHi = (rHi * hdrIn.dim[1]) + (s * nVox2DIn); //offset to start of row above
				int rOut = (r * hdrIn.dim[1]) + (s * nVox2D); //offset to output row
				for (int c = 0; c < hdrIn.dim[1]; c++) { //for each row
					imOut16[rOut+c] = round( ( ((float)imIn16[rLo+c])*fracLo) + ((float)imIn16[rHi+c])*fracHi);
				} //for c (each column)
			} //rI (input row) in range
		} //for r (each row)
	} //for s (each slice)*/
	free(im);
    if (sliceMMarray != NULL) return imOut; //we will save after correcting for variable slice thicknesses
    char niiFilenameTilt[2048] = {""};
    strcat(niiFilenameTilt,niiFilename);
    strcat(niiFilenameTilt,"_Tilt");
    nii_saveNII3D(niiFilenameTilt, *hdr, imOut, opts);
    return imOut;
}// nii_saveNII3Dtilt()

int nii_saveNII3Deq(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts, float * sliceMMarray ) {
    //convert image with unequal slice distances to equal slice distances
    //sliceMMarray = 0.0 3.0 6.0 12.0 22.0 <- ascending distance from first slice
    int nVox2D = hdr.dim[1]*hdr.dim[2];
    if ((nVox2D < 1) || (hdr.dim[0] != 3) ) return EXIT_FAILURE;
    if ((hdr.datatype != DT_UINT8) && (hdr.datatype != DT_RGB24) && (hdr.datatype != DT_INT16)) {
        printf("Only able to make equidistant slices from 3D 8,16,24-bit volumes with at least 3 slices.");
        return EXIT_FAILURE;
    }
    float mn = sliceMMarray[1] - sliceMMarray[0];
    for (int i = 1; i < hdr.dim[3]; i++) {
    	float dx = sliceMMarray[i] - sliceMMarray[i-1];
        //if ((dx < mn) // <- only allow consistent slice direction
        if ((dx < mn) && (dx > 0.0)) // <- allow slice direction to reverse
            mn = sliceMMarray[i] - sliceMMarray[i-1];
    }
    if (mn <= 0.0f) {
    	printf("Unable to equalize slice distances: slice number not consistent with slice position.\n");
    	return EXIT_FAILURE;
    }
    int slices = hdr.dim[3];
    slices = (int)ceil((sliceMMarray[slices-1]-0.5*(sliceMMarray[slices-1]-sliceMMarray[slices-2]))/mn); //-0.5: fence post
    if (slices > (hdr.dim[3] * 2)) {
        slices = 2 * hdr.dim[3];
        mn = (sliceMMarray[hdr.dim[3]-1]) / (slices-1);
    }
    //printf("-->%g mn slices %d orig %d\n", mn, slices, hdr.dim[3]);
    if (slices < 3) return EXIT_FAILURE;
    struct nifti_1_header hdrX = hdr;
    hdrX.dim[3] = slices;
    hdrX.pixdim[3] = mn;
    if ((hdr.pixdim[3] != 0.0) && (hdr.pixdim[3] != hdrX.pixdim[3])) {
        float Scale = hdrX.pixdim[3] / hdr.pixdim[3];
        //to do: do I change srow_z or srow_x[2], srow_y[2], srow_z[2],
        hdrX.srow_z[0] = hdr.srow_z[0] * Scale;
        hdrX.srow_z[1] = hdr.srow_z[1] * Scale;
        hdrX.srow_z[2] = hdr.srow_z[2] * Scale;
    }
    unsigned char *imX;
    if (hdr.datatype == DT_INT16) {
        short * im16 = ( short*) im;
        imX = (unsigned char *)malloc( (nVox2D * slices)  *  2);//sizeof( short) );
        short * imX16 = ( short*) imX;
        for (int s=0; s < slices; s++) {
            float sliceXmm = s * mn; //distance from first slice
            int sliceXi = (s * nVox2D);//offset for this slice
            int sHi = 0;
            while ((sHi < (hdr.dim[3] - 1) ) && (sliceMMarray[sHi] < sliceXmm))
                sHi += 1;
            int sLo = sHi - 1;
            if (sLo < 0) sLo = 0;
            float mmHi = sliceMMarray[sHi];
            float mmLo = sliceMMarray[sLo];
            sLo = sLo * nVox2D;
            sHi = sHi * nVox2D;
            if ((mmHi == mmLo) || (sliceXmm > mmHi)) { //select only from upper slice TPX
                //for (int v=0; v < nVox2D; v++)
                //    imX16[sliceXi+v] = im16[sHi+v];
                memcpy(&imX16[sliceXi], &im16[sHi], nVox2D* sizeof(unsigned short)); //memcpy( dest, src, bytes)

            } else {
                float fracHi = (sliceXmm-mmLo)/ (mmHi-mmLo);
                float fracLo = 1.0 - fracHi;
                //weight between two slices
                for (int v=0; v < nVox2D; v++)
                    imX16[sliceXi+v] = round( ( (float)im16[sLo+v]*fracLo) + (float)im16[sHi+v]*fracHi);
            }
        }
    } else {
        if (hdr.datatype == DT_RGB24) nVox2D = nVox2D * 3;
        imX = (unsigned char *)malloc( (nVox2D * slices)  *  2);//sizeof( short) );
        for (int s=0; s < slices; s++) {
            float sliceXmm = s * mn; //distance from first slice
            int sliceXi = (s * nVox2D);//offset for this slice
            int sHi = 0;
            while ((sHi < (hdr.dim[3] - 1) ) && (sliceMMarray[sHi] < sliceXmm))
                sHi += 1;
            int sLo = sHi - 1;
            if (sLo < 0) sLo = 0;
            float mmHi = sliceMMarray[sHi];
            float mmLo = sliceMMarray[sLo];
            sLo = sLo * nVox2D;
            sHi = sHi * nVox2D;
            if ((mmHi == mmLo) || (sliceXmm > mmHi)) { //select only from upper slice TPX
                memcpy(&imX[sliceXi], &im[sHi], nVox2D); //memcpy( dest, src, bytes)
            } else {
                float fracHi = (sliceXmm-mmLo)/ (mmHi-mmLo);
                float fracLo = 1.0 - fracHi; //weight between two slices
                for (int v=0; v < nVox2D; v++)
                    imX[sliceXi+v] = round( ( (float)im[sLo+v]*fracLo) + (float)im[sHi+v]*fracHi);
            }
        }
    }
    char niiFilenameEq[2048] = {""};
    strcat(niiFilenameEq,niiFilename);
    strcat(niiFilenameEq,"_Eq");
    nii_saveNII3D(niiFilenameEq, hdrX, imX, opts);
    free(imX);
    return EXIT_SUCCESS;
}

void smooth1D(int num, double * im) {
	if (num < 3) return;
	double * src = (double *) malloc(sizeof(double)*num);
	memcpy(&src[0], &im[0], num * sizeof(double)); //memcpy( dest, src, bytes)
	double frac = 0.25;
	for (int i = 1; i < (num-1); i++)
		im[i] = (src[i-1]*frac) + (src[i]*frac*2) + (src[i+1]*frac);
	free(src);
}

void nii_saveCrop(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts) {
    //remove excess neck slices - assumes output of nii_setOrtho()
    int nVox2D = hdr.dim[1]*hdr.dim[2];
    if ((nVox2D < 1) || (fabs(hdr.pixdim[3]) < 0.001) || (hdr.dim[0] != 3) || (hdr.dim[3] < 128)) return;
    if ((hdr.datatype != DT_INT16) && (hdr.datatype != DT_UINT16)) {
        printf("Only able to crop 16-bit volumes.");
        return;
    }
	short * im16 = ( short*) im;
	unsigned short * imu16 = (unsigned short*) im;
	float kThresh = 0.09; //more than 9% of max brightness
	#ifdef myEnableOtsu
	kThresh = 0.0001;
	if (hdr.datatype == DT_UINT16)
		maskBackgroundU16 (imu16, hdr.dim[1],hdr.dim[2],hdr.dim[3], 5,2, true);
	else
		maskBackground16 (im16, hdr.dim[1],hdr.dim[2],hdr.dim[3], 5,2, true);
	#endif
    int ventralCrop = 0;
    //find max value for each slice
    int slices = hdr.dim[3];
    double * sliceSums = (double *) malloc(sizeof(double)*slices);
    double maxSliceVal = 0.0;
    for (int i = (slices-1); i  >= 0; i--) {
    	sliceSums[i] = 0;
    	int sliceStart = i * nVox2D;
    	if (hdr.datatype == DT_UINT16)
			for (int j = 0; j < nVox2D; j++)
				sliceSums[i] += imu16[j+sliceStart];
    	else
			for (int j = 0; j < nVox2D; j++)
				sliceSums[i] += im16[j+sliceStart];
		if (sliceSums[i] > maxSliceVal)
    		maxSliceVal = sliceSums[i];
    }
    if (maxSliceVal <= 0) {
    	free(sliceSums);
    	return;
    }
    smooth1D(slices, sliceSums);
    for (int i = 0; i  < slices; i++)
    	sliceSums[i] = sliceSums[i] / maxSliceVal; //so brightest slice has value 1
	//dorsal crop: eliminate slices with more than 5% brightness
	int dorsalCrop;
	for (dorsalCrop = (slices-1); dorsalCrop >= 1; dorsalCrop--)
		if (sliceSums[dorsalCrop-1] > kThresh) break;
	if (dorsalCrop <= 1) {
		free(sliceSums);
		return;
	}
	/*
	//find brightest band within 90mm of top of head
	int ventralMaxSlice = dorsalCrop - round(90 /fabs(hdr.pixdim[3])); //brightest stripe within 90mm of apex
    if (ventralMaxSlice < 0) ventralMaxSlice = 0;
    int maxSlice = dorsalCrop;
    for (int i = ventralMaxSlice; i  < dorsalCrop; i++)
    	if (sliceSums[i] > sliceSums[maxSlice])
    		maxSlice = i;
	//now find
    ventralMaxSlice = maxSlice - round(45 /fabs(hdr.pixdim[3])); //gap at least 60mm
    if (ventralMaxSlice < 0) {
    	free(sliceSums);
    	return;
    }
    int ventralMinSlice = maxSlice - round(90/fabs(hdr.pixdim[3])); //gap no more than 120mm
    if (ventralMinSlice < 0) ventralMinSlice = 0;
	for (int i = (ventralMaxSlice-1); i >= ventralMinSlice; i--)
		if (sliceSums[i] > sliceSums[ventralMaxSlice])
			ventralMaxSlice = i;
	//finally: find minima between these two points...
    int minSlice = ventralMaxSlice;
    for (int i = ventralMaxSlice; i  < maxSlice; i++)
    	if (sliceSums[i] < sliceSums[minSlice])
    		minSlice = i;
    //printf("%d %d %d\n", ventralMaxSlice, minSlice, maxSlice);
	int gap = round((maxSlice-minSlice)*0.8);//add 40% for cerebellum
	if ((minSlice-gap) > 1)
        ventralCrop = minSlice-gap;
	free(sliceSums);
	if (ventralCrop > dorsalCrop) return;
	//FindDVCrop2
	const double kMaxDVmm = 180.0;
    double sliceMM = hdr.pixdim[3] * (dorsalCrop-ventralCrop);
    if (sliceMM > kMaxDVmm) { //decide how many more ventral slices to remove
        sliceMM = sliceMM - kMaxDVmm;
        sliceMM = sliceMM / hdr.pixdim[3];
        ventralCrop = ventralCrop + round(sliceMM);
    }*/
    const double kMaxDVmm = 169.0;
    ventralCrop = dorsalCrop - round( kMaxDVmm / hdr.pixdim[3]);
    if (ventralCrop < 0) ventralCrop = 0;
	//apply crop

	printf(" Cropping from slice %d to %d (of %d)\n", ventralCrop, dorsalCrop, slices);
    struct nifti_1_header hdrX = hdr;
    slices = dorsalCrop - ventralCrop + 1;
    hdrX.dim[3] = slices;
    //translate origin to account for missing slices
    hdrX.srow_x[3] += hdr.srow_x[2]*ventralCrop;
    hdrX.srow_y[3] += hdr.srow_y[2]*ventralCrop;
    hdrX.srow_z[3] += hdr.srow_z[2]*ventralCrop;
	//convert data
    unsigned char *imX;
	imX = (unsigned char *)malloc( (nVox2D * slices)  *  2);//sizeof( short) );
	short * imX16 = ( short*) imX;
	for (int s=0; s < slices; s++) {
		int sIn = s+ventralCrop;
		int sOut = s;
		sOut = sOut * nVox2D;
		sIn = sIn * nVox2D;
		memcpy(&imX16[sOut], &im16[sIn], nVox2D* sizeof(unsigned short)); //memcpy( dest, src, bytes)
    }
    char niiFilenameCrop[2048] = {""};
    strcat(niiFilenameCrop,niiFilename);
    strcat(niiFilenameCrop,"_Crop");
    nii_saveNII3D(niiFilenameCrop, hdrX, imX, opts);
    free(imX);
    return;
} //nii_saveCrop()

int saveDcm2Nii(int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TSearchList *nameList, struct TDCMopts opts, struct TDTI4D *dti4D) {
    bool iVaries = intensityScaleVaries(nConvert,dcmSort,dcmList);
    float *sliceMMarray = NULL; //only used if slices are not equidistant
    uint64_t indx = dcmSort[0].indx;
    uint64_t indx0 = dcmSort[0].indx;
    bool saveAs3D = dcmList[indx].isHasPhase;
    struct nifti_1_header hdr0;
    unsigned char * img = nii_loadImgXL(nameList->str[indx], &hdr0,dcmList[indx], iVaries, opts.compressFlag);
    if ( (dcmList[indx0].compressionScheme != kCompressNone) && (opts.compressFlag != kCompressNone))
        printf("Image Decompression is new: please validate conversions\n");
    if (opts.isVerbose)
    #ifdef myUseCOut
    	std::cout<<"Converting "<<nameList->str[indx]<<std::endl;
	#else
        printf("Converting %s\n",nameList->str[indx]);
    #endif
    if (img == NULL) return EXIT_FAILURE;
    //if (iVaries) img = nii_iVaries(img, &hdr0);
    size_t imgsz = nii_ImgBytes(hdr0);
    unsigned char *imgM = (unsigned char *)malloc(imgsz* (uint64_t)nConvert);
    memcpy(&imgM[0], &img[0], imgsz);
    free(img);
    //printf(" %d %d %d %d %lu\n", hdr0.dim[1], hdr0.dim[2], hdr0.dim[3], hdr0.dim[4], (unsigned long)[imgM length]);
    if (nConvert > 1) {
        if (dcmList[indx0].gantryTilt != 0.0f)
            printf(" Warning: note these images have gantry tilt of %g degrees (manufacturer ID = %d)\n", dcmList[indx0].gantryTilt, dcmList[indx0].manufacturer);
        if (hdr0.dim[3] < 2) {
            //stack volumes with multiple acquisitions
            int nAcq = 1;
            //Next line works in theory, but fails with Siemens CT that saves pairs of slices as acquisitions, see example "testSiemensStackAcq"
            //  nAcq = 1+abs( dcmList[dcmSort[nConvert-1].indx].acquNum-dcmList[indx0].acquNum);
            //therefore, the 'same position' is the most robust solution in the real world.
            if ((dcmList[indx0].manufacturer == kMANUFACTURER_SIEMENS) && (isSameFloat(dcmList[indx0].TR ,0.0f))) {
                nConvert = siemensCtKludge(nConvert, dcmSort,dcmList);
            }
            if ((nAcq == 1 ) && (dcmList[indx0].locationsInAcquisition > 0)) nAcq = nConvert/dcmList[indx0].locationsInAcquisition;

            if (nAcq < 2 ) {
                nAcq = 0;
                for (int i = 0; i < nConvert; i++)
                    if (isSamePosition(dcmList[dcmSort[0].indx],dcmList[dcmSort[i].indx])) nAcq++;
            }

            /*int nImg = 1+abs( dcmList[dcmSort[nConvert-1].indx].imageNum-dcmList[dcmSort[0].indx].imageNum);
            if (((nConvert/nAcq) > 1) && ((nConvert%nAcq)==0) && (nImg == nConvert) && (dcmList[dcmSort[0].indx].locationsInAcquisition == 0) ) {
                printf(" stacking %d acquisitions as a single volume\n", nAcq);
                //some Siemens CT scans use multiple acquisitions for a single volume, perhaps also check that slice position does not repeat?
                hdr0.dim[3] = nConvert;
            } else*/ if ( (nAcq > 1) && ((nConvert/nAcq) > 1) && ((nConvert%nAcq)==0) ) {

                hdr0.dim[3] = nConvert/nAcq;
                hdr0.dim[4] = nAcq;
                hdr0.dim[0] = 4;
            } else {
                hdr0.dim[3] = nConvert;
                if (nAcq > 1) {
                    printf("Slice positions repeated, but number of slices (%d) not divisible by number of repeats (%d): missing images?\n", nConvert, nAcq);
                }
            }
            float dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
            bool dxVaries = false;
            for (int i = 1; i < nConvert; i++)
                if (!isSameFloatT(dx,intersliceDistance(dcmList[dcmSort[i-1].indx],dcmList[dcmSort[i].indx]),0.2))
                    dxVaries = true;
            if (hdr0.dim[4] < 2) {

                if (dxVaries) {
                    sliceMMarray = (float *) malloc(sizeof(float)*nConvert);
                    sliceMMarray[0] = 0.0f;
                    printf("Dims %d %d %d %d %d\n", hdr0.dim[1], hdr0.dim[2], hdr0.dim[3], hdr0.dim[4], nAcq);
                    printf("Warning: interslice distance varies in this volume (incompatible with NIfTI format).\n");
                    printf(" Distance from first slice:\n");
                    printf("dx=[0");
                    for (int i = 1; i < nConvert; i++) {
                        float dx0 = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[i].indx]);
                        printf(" %g", dx0);
                        sliceMMarray[i] = dx0;
                    }
                    printf("]\n");
                }
            }
            if ((hdr0.dim[4] > 0) && (dxVaries) && (dx == 0.0) && (dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_PHILIPS)) {
                swapDim3Dim4(hdr0.dim[3],hdr0.dim[4],dcmSort);
                dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
                //printf("swizzling 3rd and 4th dimensions (XYTZ -> XYZT), assuming interslice distance is %f\n",dx);
            }
            if ((dx == 0.0 ) && (!dxVaries)) { //all images are the same slice - 16 Dec 2014
                printf(" Warning: all images appear to be a single slice - please check slice/vector orientation\n");
                hdr0.dim[3] = 1;
                hdr0.dim[4] = nConvert;
                hdr0.dim[0] = 4;
            }
            dcmList[dcmSort[0].indx].xyzMM[3] = dx; //16Sept2014 : correct DICOM for true distance between slice centers:
            // e.g. MCBI Siemens ToF 0018:0088 reports 16mm SpacingBetweenSlices, but actually 0.5mm
            if (dx > 0) hdr0.pixdim[3] = dx;
        } else if (hdr0.dim[4] < 2) {
            hdr0.dim[4] = nConvert;
            hdr0.dim[0] = 4;
        } else {
            hdr0.dim[5] = nConvert;
            hdr0.dim[0] = 5;
        }
        //printf(" %d %d %d %d %lu\n", hdr0.dim[1], hdr0.dim[2], hdr0.dim[3], hdr0.dim[4], (unsigned long)[imgM length]);
        struct nifti_1_header hdrI;
        for (int i = 1; i < nConvert; i++) { //stack additional images
            indx = dcmSort[i].indx;
            //if (headerDcm2Nii(dcmList[indx], &hdrI) == EXIT_FAILURE) return EXIT_FAILURE;
            img = nii_loadImgXL(nameList->str[indx], &hdrI, dcmList[indx],iVaries, opts.compressFlag);
            if (img == NULL) return EXIT_FAILURE;
            if ((hdr0.dim[1] != hdrI.dim[1]) || (hdr0.dim[2] != hdrI.dim[2]) || (hdr0.bitpix != hdrI.bitpix)) {
                    #ifdef myUseCOut
    	std::cout<<"Error: image dimensions differ "<<nameList->str[dcmSort[0].indx]<<"  "<<nameList->str[indx]<<std::endl;
		#else
                printf("Error: image dimensions differ %s %s",nameList->str[dcmSort[0].indx], nameList->str[indx]);
                #endif
                free(imgM);
                free(img);
                return EXIT_FAILURE;
            }
            memcpy(&imgM[(uint64_t)i*imgsz], &img[0], imgsz);
            free(img);
        }
    }
    char pathoutname[2048] = {""};
    if (nii_createFilename(dcmList[dcmSort[0].indx], pathoutname, opts) == EXIT_FAILURE) {
        free(imgM);
        return EXIT_FAILURE;
    }
    if (strlen(pathoutname) <1) {
        free(imgM);
        return EXIT_FAILURE;
    }
    int sliceDir = 0;
    if (hdr0.dim[3] > 1)
        sliceDir = headerDcm2Nii2(dcmList[dcmSort[0].indx],dcmList[dcmSort[nConvert-1].indx] , &hdr0);
	//UNCOMMENT NEXT TWO LINES TO RE-ORDER MOSAIC WHERE CSA's protocolSliceNumber does not start with 1
	if (dcmList[dcmSort[0].indx].CSA.protocolSliceNumber1 > 1) {
		printf("WARNING: WEIRD CSA 'ProtocolSliceNumber': SPATIAL, SLICE-ORDER AND DTI TRANSFORMS UNTESTED\n");
		//see https://github.com/neurolabusc/dcm2niix/issues/40
		sliceDir = -1; //not sure how to handle negative determinants?

	}
	if (sliceDir < 0) {
        imgM = nii_flipZ(imgM, &hdr0);
        sliceDir = abs(sliceDir); //change this, we have flipped the image so GE DTI bvecs no longer need to be flipped!
    }

    nii_SaveBIDS(pathoutname, dcmList[dcmSort[0].indx], opts, dti4D, &hdr0);
	nii_SaveText(pathoutname, dcmList[dcmSort[0].indx], opts, &hdr0, nameList->str[indx]);
    int numFinalADC = nii_SaveDTI(pathoutname,nConvert, dcmSort, dcmList, opts, sliceDir, dti4D);
    numFinalADC = numFinalADC; //simply to silence compiler warning when myNoSave defined

    if ((hdr0.datatype == DT_UINT16) &&  (!dcmList[dcmSort[0].indx].isSigned)) nii_check16bitUnsigned(imgM, &hdr0);
    #ifdef myUseCOut
     std::cout<<"Convert "<<nConvert<<" DICOM as "<<pathoutname<<
     	" ("<<hdr0.dim[1]<<"x"<<hdr0.dim[2]<<"x"<<hdr0.dim[3]<<"x"<<hdr0.dim[4]<<")" <<std::endl;
    #else
    printf( "Convert %d DICOM as %s (%dx%dx%dx%d)\n",  nConvert, pathoutname, hdr0.dim[1],hdr0.dim[2],hdr0.dim[3],hdr0.dim[4]);
    #endif
    if (hdr0.dim[3] < 2)
    #ifdef myUseCOut
    	std::cout<<"WARNING: check that 2D images are not mirrored"<<std::endl;
		#else
        printf("WARNING: check that 2D images are not mirrored.\n");
        #endif
    else
        fflush(stdout); //GUI buffers printf, display all results
    if ((dcmList[dcmSort[0].indx].is3DAcq) && (hdr0.dim[3] > 1) && (hdr0.dim[0] < 4))
        imgM = nii_setOrtho(imgM, &hdr0); //printf("ortho %d\n", echoInt (33));
    else if (opts.isFlipY)//(FLIP_Y) //(dcmList[indx0].CSA.mosaicSlices < 2) &&
        imgM = nii_flipY(imgM, &hdr0);
    else
    #ifdef myUseCOut
    	std::cout<<"DICOM row order preserved: may appear upside down in tools that ignore spatial transforms"<<std::endl;
		#else
        printf("DICOM row order preserved: may appear upside down in tools that ignore spatial transforms\n");
        #endif
#ifndef myNoSave
    //printf(" x--> %d ----\n", nConvert);
    if (! opts.isRGBplanar) //save RGB as packed RGBRGBRGB... instead of planar RRR..RGGG..GBBB..B
        imgM = nii_planar2rgb(imgM, &hdr0, true);
    if ((hdr0.dim[4] > 1) && (saveAs3D))
        nii_saveNII3D(pathoutname, hdr0, imgM,opts);
    else {
        if ((numFinalADC > 0) && (hdr0.dim[4] > (numFinalADC+1))) { //ADC maps can disrupt analysis: save a copy with the ADC map, and another without
            char pathoutnameADC[2048] = {""};
            strcat(pathoutnameADC,pathoutname);
            strcat(pathoutnameADC,"_ADC");
            nii_saveNII(pathoutnameADC, hdr0, imgM, opts);
            hdr0.dim[4] = hdr0.dim[4]-numFinalADC;
        };
        nii_saveNII(pathoutname, hdr0, imgM, opts);
    }
#endif
    if (dcmList[indx0].gantryTilt != 0.0) {
        if (dcmList[indx0].isResampled)
            printf("Tilt correction skipped: 0008,2111 reports RESAMPLED\n");
        else if (opts.isTiltCorrect)
            imgM = nii_saveNII3Dtilt(pathoutname, &hdr0, imgM,opts, sliceMMarray, dcmList[indx0].gantryTilt, dcmList[indx0].manufacturer);
        else
            printf("Tilt correction skipped\n");
    }
    if (sliceMMarray != NULL) {
        if (dcmList[indx0].isResampled)
            printf("Slice thickness correction skipped: 0008,2111 reports RESAMPLED\n");
        else
            nii_saveNII3Deq(pathoutname, hdr0, imgM,opts, sliceMMarray);
        free(sliceMMarray);
    }
    if ((opts.isCrop) && (dcmList[indx0].is3DAcq)   && (hdr0.dim[3] > 1) && (hdr0.dim[0] < 4))//for T1 scan: && (dcmList[indx0].TE < 25)
    	nii_saveCrop(pathoutname, hdr0, imgM,opts); //n.b. must be run AFTER nii_setOrtho()!
    free(imgM);
    return EXIT_SUCCESS;
} //saveDcm2Nii()

int compareTDCMsort(void const *item1, void const *item2) {
    //for quicksort http://blog.ablepear.com/2011/11/objective-c-tuesdays-sorting-arrays.html
    struct TDCMsort const *dcm1 = (const struct TDCMsort *)item1;
    struct TDCMsort const *dcm2 = (const struct TDCMsort *)item2;
    if (dcm1->img < dcm2->img)
        return -1;
    else if (dcm1->img > dcm2->img)
        return 1;
    return 0; //tie
} //compareTDCMsort()

int isSameFloatGE (float a, float b) {
//Kludge for bug in 0002,0016="DIGITAL_JACKET", 0008,0070="GE MEDICAL SYSTEMS" DICOM data: Orient field (0020:0037) can vary 0.00604261 == 0.00604273 !!!
    return (fabs (a - b) <= 0.0001);
}

int isSameFloatDouble (double a, double b) {
    //Kludge for bug in 0002,0016="DIGITAL_JACKET", 0008,0070="GE MEDICAL SYSTEMS" DICOM data: Orient field (0020:0037) can vary 0.00604261 == 0.00604273 !!!
    return (fabs (a - b) <= 0.0001);
}

struct TWarnings { //generate a warning only once per set
        bool bitDepthVaries, dateTimeVaries, echoVaries, coilVaries, nameVaries, orientVaries;
};

TWarnings setWarnings() {
	TWarnings r;
	r.bitDepthVaries = false;
	r.dateTimeVaries = false;
	r.echoVaries = false;
	r.coilVaries = false;
	r.nameVaries = false;
	r.orientVaries = false;
	return r;
}

bool isSameSet (struct TDICOMdata d1, struct TDICOMdata d2, bool isForceStackSameSeries,struct TWarnings* warnings) {
    //returns true if d1 and d2 should be stacked together as a signle output
    if (!d1.isValid) return false;
    if (!d2.isValid) return false;
    if  (d1.seriesNum != d2.seriesNum) return false;
    if ((d1.bitsAllocated != d2.bitsAllocated) || (d1.xyzDim[1] != d2.xyzDim[1]) || (d1.xyzDim[2] != d2.xyzDim[2]) || (d1.xyzDim[3] != d2.xyzDim[3]) ) {
        if (!warnings->bitDepthVaries)
        	printf("slices not stacked: dimensions or bit-depth varies\n");
        warnings->bitDepthVaries = true;
        return false;
    }
    if (isForceStackSameSeries) return true; //we will stack these images, even if they differ in the following attributes
    if (!isSameFloatDouble(d1.dateTime, d2.dateTime)) { //beware, some vendors incorrectly store Image Time (0008,0033) as Study Time (0008,0030).
    	if (!warnings->dateTimeVaries)
    		printf("slices not stacked: Study Data/Time (0008,0020 / 0008,0030) varies %12.12f ~= %12.12f\n", d1.dateTime, d2.dateTime);
    	warnings->dateTimeVaries = true;
    	return false;
    }
    if ((d1.TE != d2.TE) || (d1.echoNum != d2.echoNum)) {
        if (!warnings->echoVaries)
        	printf("slices not stacked: echo varies (TE %g, %g; echo %d, %d)\n", d1.TE, d2.TE,d1.echoNum, d2.echoNum );
        warnings->echoVaries = true;
        return false;
    }
    if (d1.coilNum != d2.coilNum) {
        if (!warnings->coilVaries)
        	printf("slices not stacked: coil varies\n");
        warnings->coilVaries = true;
        return false;
    }
    if ((strcmp(d1.protocolName, d2.protocolName) != 0)) {
        if ((!warnings->nameVaries))
        	printf("slices not stacked: protocol name varies\n");
        warnings->nameVaries = true;
        return false;
    }
    if ((!isSameFloatGE(d1.orient[1], d2.orient[1]) || !isSameFloatGE(d1.orient[2], d2.orient[2]) ||  !isSameFloatGE(d1.orient[3], d2.orient[3]) ||
    		!isSameFloatGE(d1.orient[4], d2.orient[4]) || !isSameFloatGE(d1.orient[5], d2.orient[5]) ||  !isSameFloatGE(d1.orient[6], d2.orient[6]) ) ) {
        if (!warnings->orientVaries)
        	printf("slices not stacked: orientation varies (localizer?) [%g %g %g %g %g %g] != [%g %g %g %g %g %g]\n",
               d1.orient[1], d1.orient[2], d1.orient[3],d1.orient[4], d1.orient[5], d1.orient[6],
               d2.orient[1], d2.orient[2], d2.orient[3],d2.orient[4], d2.orient[5], d2.orient[6]);
        warnings->orientVaries = true;
        return false;
    }
    return true;
} //isSameSet()

/*
#if defined(__APPLE__) && defined(__MACH__)
void  convertForeign2Nii(char * fname, struct TDCMopts* opts) {//, struct TDCMopts opts) {

    struct nifti_1_header niiHdr;
    unsigned char * img =  nii_readForeignC(fname, &niiHdr, 0, 65535);
    if (img == NULL) return;
    char pth[1024] = {""};
    if (strlen(opts->outdir) > 0) {
        strcpy(pth, opts->outdir);
        int w =access(pth,W_OK);
        if (w != 0) {
            if (getcwd(pth, sizeof(pth)) != NULL) {
                w =access(pth,W_OK);
                if (w != 0) {
                    printf("Error: you do not have write permissions for the directory %s\n",opts->outdir);
                    return;
                }
                printf("Warning: %s write permission denied. Saving to working directory %s \n", opts->outdir, pth);

            }
        }
        char appendChar[2] = {"a"};
        appendChar[0] = kPathSeparator;
        if (pth[strlen(pth)-1] != kPathSeparator)
            strcat (pth,appendChar);
        char fn[1024] = {""};
        getFileName(fn, fname);
        strcat(pth,fn);
    } else {
        strcat(pth, fname);
    }
    printf("Converted foreign image '%s'\n",fname);
    nii_saveNII(pth, niiHdr, img, *opts);
    free(img);
} //convertForeign2Nii()
#endif */

int singleDICOM(struct TDCMopts* opts, char *fname) {
    char filename[768] ="";
    strcat(filename, fname);
    if (isDICOMfile(filename) == 0) {
        printf("Error: not a DICOM image : %s\n", filename);
        return 0;
    }
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc( sizeof(struct  TDICOMdata));
    struct TDTI4D dti4D;
    struct TSearchList nameList;
    nameList.maxItems = 1; // larger requires more memory, smaller more passes
    nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //reserve one pointer (32 or 64 bits) per potential file
    nameList.numItems = 0;
    nameList.str[nameList.numItems]  = (char *)malloc(strlen(filename)+1);
    strcpy(nameList.str[nameList.numItems],filename);
    nameList.numItems++;
    struct TDCMsort dcmSort[1];
    dcmSort[0].indx = 0;
    dcmSort[0].img = ((uint64_t)dcmList[0].seriesNum << 32) + dcmList[0].imageNum;
    dcmList[0].converted2NII = 1;
    dcmList[0] = readDICOMv(nameList.str[0], opts->isVerbose, opts->compressFlag, &dti4D); //ignore compile warning - memory only freed on first of 2 passes
    return saveDcm2Nii(1, dcmSort, dcmList, &nameList, *opts, &dti4D);
}

void searchDirForDICOM(char *path, struct TSearchList *nameList, int maxDepth, int depth) {
    tinydir_dir dir;
    tinydir_open(&dir, path);
    while (dir.has_next) {
        tinydir_file file;
        file.is_dir = 0; //avoids compiler warning: this is set by tinydir_readfile
        tinydir_readfile(&dir, &file);
        //printf("%s\n", file.name);
        char filename[768] ="";
        strcat(filename, path);
        strcat(filename,kFileSep);
        strcat(filename, file.name);
        if ((file.is_dir) && (depth < maxDepth) && (file.name[0] != '.'))
            searchDirForDICOM(filename, nameList, maxDepth, depth+1);
        else if (!file.is_reg) //ignore hidden files...
            ;
        else if (isDICOMfile(filename) > 0) {
            if (nameList->numItems < nameList->maxItems) {
                nameList->str[nameList->numItems]  = (char *)malloc(strlen(filename)+1);
                strcpy(nameList->str[nameList->numItems],filename);
                //printf("OK\n");
            }
            nameList->numItems++;
            //printf("dcm %lu %s \n",nameList->numItems, filename);
        } else {
            #if defined(__APPLE__) && defined(__MACH__)
            //convertForeign2Nii(filename, opts);
            #endif
        #ifdef MY_DEBUG
            #ifdef myUseCOut
                std::cout<<"Not a dicom"<< filename <<std::endl;
            #else
                printf("Not a dicom:\t%s\n", filename);
            #endif
        #endif
        }
        tinydir_next(&dir);
    }
    tinydir_close(&dir);
} //searchDirForDICOM()

int removeDuplicates(int nConvert, struct TDCMsort dcmSort[]){
    //done AFTER sorting, so duplicates will be sequential
    if (nConvert < 2) return nConvert;
    int nDuplicates = 0;
    for (int i = 1; i < nConvert; i++) {
        if (dcmSort[i].img == dcmSort[i-1].img) {
            nDuplicates ++;
        } else {
            dcmSort[i-nDuplicates].img = dcmSort[i].img;
            dcmSort[i-nDuplicates].indx = dcmSort[i].indx;
        }
    }
    if (nDuplicates > 0)
        #ifdef myUseCOut
    	std::cout<<"Some images have identical time, series, acquisition and image values. DUPLICATES REMOVED."<<std::endl;
		#else
    	printf("Some images have identical time, series, acquisition and image values. DUPLICATES REMOVED.\n");
    	#endif
    return nConvert - nDuplicates;
} //removeDuplicates()

int removeDuplicatesVerbose(int nConvert, struct TDCMsort dcmSort[], struct TSearchList *nameList){
    //done AFTER sorting, so duplicates will be sequential
    if (nConvert < 2) return nConvert;
    int nDuplicates = 0;
    for (int i = 1; i < nConvert; i++) {
        if (dcmSort[i].img == dcmSort[i-1].img) {
                #ifdef myUseCOut
    	std::cout<<"\t"<<nameList->str[dcmSort[i-1].indx]<<"\t=\t"<<nameList->str[dcmSort[i].indx] <<std::endl;
		#else
            printf("\t%s\t=\t%s\n",nameList->str[dcmSort[i-1].indx],nameList->str[dcmSort[i].indx]);
            #endif
            nDuplicates ++;
        }else {
            dcmSort[i-nDuplicates].img = dcmSort[i].img;
            dcmSort[i-nDuplicates].indx = dcmSort[i].indx;
        }
    }
    if (nDuplicates > 0)
            #ifdef myUseCOut
    	std::cout<<"Some images have identical time, series, acquisition and image values. Duplicates removed."<<std::endl;
		#else
    	printf("Some images have identical time, series, acquisition and image values. Duplicates removed.\n");
    	#endif
    	return nConvert - nDuplicates;
} //removeDuplicates()

int strcicmp(char const *a, char const *b) //case insensitive compare
{
    for (;; a++, b++) {
        int d = tolower(*a) - tolower(*b);
        if (d != 0 || !*a)
            return d;
    }
} //strcicmp()

bool isExt (char *file_name, const char* ext) {
    char *p_extension;
    if((p_extension = strrchr(file_name,'.')) != NULL )
        if(strcicmp(p_extension,ext) == 0) return true;
    //if(strcmp(p_extension,ext) == 0) return true;
    return false;
} //isExt()

/*int nii_readpic(char * fname, struct nifti_1_header *nhdr) {
    //https://github.com/jefferis/pic2nifti/blob/master/libpic2nifti.c
#define BIORAD_HEADER_SIZE 76
#define BIORAD_NOTE_HEADER_SIZE 16
#define BIORAD_NOTE_SIZE 80
    typedef struct
    {
        unsigned short nx, ny;    //  0   2*2     image width and height in pixels
        short npic;               //  4   2       number of images in file
        short ramp1_min;          //  6   2*2     LUT1 ramp min. and max.
        short ramp1_max;
        int32_t notes;                // 10   4       no notes=0; has notes=non zero
        short byte_format;        // 14   2       bytes=TRUE(1); words=FALSE(0)
        unsigned short n;         // 16   2       image number within file
        char name[32];            // 18   32      file name
        short merged;             // 50   2       merged format
        unsigned short color1;    // 52   2       LUT1 color status
        unsigned short file_id;   // 54   2       valid .PIC file=12345
        short ramp2_min;          // 56   2*2     LUT2 ramp min. and max.
        short ramp2_max;
        unsigned short color2;    // 60   2       LUT2 color status
        short edited;             // 62   2       image has been edited=TRUE(1)
        short lens;               // 64   2       Integer part of lens magnification
        float mag_factor;         // 66   4       4 byte real mag. factor (old ver.)
        unsigned short dummy[3];  // 70   6       NOT USED (old ver.=real lens mag.)
    } biorad_header;
    typedef struct
    {
        short blank;		// 0	2
        int note_flag;		// 2	4
        int blank2;			// 6	4
        short note_type;	// 10	2
        int blank3;			// 12	4
    } biorad_note_header;
    size_t n;
    unsigned char buffer[BIORAD_HEADER_SIZE];
    FILE *f = fopen(fname, "rb");
    if (f)
        n = fread(&buffer, BIORAD_HEADER_SIZE, 1, f);
    if(!f || n!=1) {
        printf("Problem reading biorad file!\n");
        fclose(f);
        return EXIT_FAILURE;
    }
    biorad_header bhdr;
    memcpy( &bhdr.nx, buffer+0, sizeof( bhdr.nx ) );
    memcpy( &bhdr.ny, buffer+2, sizeof( bhdr.ny ) );
    memcpy( &bhdr.npic, buffer+4, sizeof( bhdr.npic ) );
    memcpy( &bhdr.byte_format, buffer+14, sizeof( bhdr.byte_format ) );
    memcpy( &bhdr.file_id, buffer+54, sizeof( bhdr.file_id ) );
    if (bhdr.file_id != 12345) {
        fclose(f);
        return EXIT_FAILURE;
    }
    nhdr->dim[0]=3;//3D
    nhdr->dim[1]=bhdr.nx;
    nhdr->dim[2]=bhdr.ny;
    nhdr->dim[3]=bhdr.npic;
    nhdr->dim[4]=0;
    nhdr->pixdim[1]=1.0;
    nhdr->pixdim[2]=1.0;
    nhdr->pixdim[3]=1.0;
    if (bhdr.byte_format == 1)
        nhdr->datatype = DT_UINT8; // 2
    else
        nhdr->datatype = DT_UINT16;
    nhdr->vox_offset = BIORAD_HEADER_SIZE;
    if(fseek(f, bhdr.nx*bhdr.ny*bhdr.npic*bhdr.byte_format, SEEK_CUR)==0) {
        biorad_note_header nh;
        char noteheaderbuf[BIORAD_NOTE_HEADER_SIZE];
        char note[BIORAD_NOTE_SIZE];
        while (!feof(f)) {
            fread(&noteheaderbuf, BIORAD_NOTE_HEADER_SIZE, 1, f);
            fread(&note, BIORAD_NOTE_SIZE, 1, f);
            memcpy(&nh.note_flag, noteheaderbuf+2, sizeof(nh.note_flag));
            memcpy(&nh.note_type, noteheaderbuf+10, sizeof(nh.note_type));
            //		printf("regular note line %s\n",note);
            //		printf("note flag = %d, note type = %d\n",nh.note_flag,nh.note_type);
            // These are not interesting notes
            if(nh.note_type==1) continue;

            // Look for calibration information
            double d1, d2, d3;
            if ( 3 == sscanf( note, "AXIS_2 %lf %lf %lf", &d1, &d2, &d3 ) )
                nhdr->pixdim[1] = d3;
            if ( 3 == sscanf( note, "AXIS_3 %lf %lf %lf", &d1, &d2, &d3 ) )
                nhdr->pixdim[2] = d3;
            if ( 3 == sscanf( note, "AXIS_4 %lf %lf %lf", &d1, &d2, &d3 ) )
                nhdr->pixdim[3] = d3;
            if(nh.note_flag==0) break;
        }
    }
    nhdr->sform_code = 1;
    nhdr->srow_x[0]=nhdr->pixdim[1];nhdr->srow_x[1]=0.0f;nhdr->srow_x[2]=0.0f;nhdr->srow_x[3]=0.0f;
    nhdr->srow_y[0]=0.0f;nhdr->srow_y[1]=nhdr->pixdim[2];nhdr->srow_y[2]=0.0f;nhdr->srow_y[3]=0.0f;
    nhdr->srow_z[0]=0.0f;nhdr->srow_z[1]=0.0f;nhdr->srow_z[2]=nhdr->pixdim[3];nhdr->srow_z[3]=0.0f;
    fclose(f);
    convertForeignToNifti(nhdr);
    return EXIT_SUCCESS;
}


int convert_foreign(struct TDCMopts opts) {
    nifti_1_header nhdr ;
    int OK = EXIT_FAILURE;
    OK = nii_readpic(opts.indir, &nhdr);
    return OK;
}*/

int convert_parRec(struct TDCMopts opts) {
    //sample dataset from Ed Gronenschild <ed.gronenschild@maastrichtuniversity.nl>
    struct TSearchList nameList;
    nameList.numItems = 1;
    nameList.maxItems = 1;
    nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //we reserve one pointer (32 or 64 bits) per potential file
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nameList.numItems * sizeof(struct  TDICOMdata));
    nameList.str[0]  = (char *)malloc(strlen(opts.indir)+1);
    strcpy(nameList.str[0],opts.indir);
    TDTI4D dti4D;
    dcmList[0] = nii_readParRec(nameList.str[0], opts.isVerbose, &dti4D);
    struct TDCMsort dcmSort[1];
    dcmSort[0].indx = 0;
    saveDcm2Nii(1, dcmSort, dcmList, &nameList, opts, &dti4D);
    free(dcmList);//if (nConvertTotal == 0)
    if (nameList.numItems < 1) {
     #ifdef myUseCOut
    	std::cout<<"No valid PAR/REC files were found"<<std::endl;
		#else
		printf("No valid PAR/REC files were found\n");
		#endif
    }
    if (nameList.numItems > 0)
        for (int i = 0; i < nameList.numItems; i++)
            free(nameList.str[i]);
    free(nameList.str);

    return EXIT_SUCCESS;
} //convert_parRec()

void freeNameList(struct TSearchList nameList) {
    if (nameList.numItems > 0) {
        unsigned long n = nameList.numItems;
        if (n > nameList.maxItems) n = nameList.maxItems; //assigned if (nameList->numItems < nameList->maxItems)
        for (unsigned long i = 0; i < n; i++)
            free(nameList.str[i]);
    }
    free(nameList.str);
}

int nii_loadDir (struct TDCMopts* opts) {
    //Identifies all the DICOM files in a folder and its subfolders
    if (strlen(opts->indir) < 1) {
         #ifdef myUseCOut
    	std::cout<<"No input"<<std::endl;
		#else
        printf("No input\n");
        #endif
        return EXIT_FAILURE;
    }
    char indir[512];
    strcpy(indir,opts->indir);
    bool isFile = is_fileNotDir(opts->indir);
    if (isFile) {//if user passes ~/dicom/mr1.dcm we will look at all files in ~/dicom
        dropFilenameFromPath(opts->indir);//getParentFolder(opts.indir, opts.indir);
    }
    if (strlen(opts->outdir) < 1)
        strcpy(opts->outdir,opts->indir);
    else if (!is_dir(opts->outdir,true)) {
        #ifdef myUseCOut
    	std::cout << "Warning: output folder invalid "<< opts->outdir<<" will try %s\n"<< opts->indir <<std::endl;
    	#else
     	printf("Warning: output folder invalid %s will try %s\n",opts->outdir,opts->indir);
        #endif
        strcpy(opts->outdir,opts->indir);
    }
    /*if (isFile && ((isExt(indir, ".gz")) || (isExt(indir, ".tgz"))) ) {
        #ifndef myDisableTarGz
         #ifndef myDisableZLib
          untargz( indir, opts->outdir);
         #endif
        #endif
    }*/
    /*if (isFile && ((isExt(indir, ".mha")) || (isExt(indir, ".mhd"))) ) {
        strcpy(opts->indir, indir); //set to original file name, not path
        return convert_foreign(*opts);
    }*/
    getFileName(opts->indirParent, opts->indir);
    if (isFile && ((isExt(indir, ".par")) || (isExt(indir, ".rec"))) ) {
        char pname[512], rname[512];
        strcpy(pname,indir);
        strcpy(rname,indir);
        changeExt (pname, "PAR");
        changeExt (rname, "REC");
        if (is_fileNotDir(rname)  &&  is_fileNotDir(pname) ) {
            strcpy(opts->indir, pname); //set to original file name, not path
            return convert_parRec(*opts);
        } else if (isExt(indir, ".par")) //Linux is case sensitive...
            return convert_parRec(*opts);
    }
    if ((isFile) && (opts->isOnlySingleFile))
        return singleDICOM(opts, indir);
    struct TSearchList nameList;
	nameList.maxItems = 32000; // larger requires more memory, smaller more passes
    //1: find filenames of dicom files: up to two passes if we found more files than we allocated memory
    for (int i = 0; i < 2; i++ ) {
        nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //reserve one pointer (32 or 64 bits) per potential file
        nameList.numItems = 0;
        searchDirForDICOM(opts->indir, &nameList,  5,1);
        if (nameList.numItems <= nameList.maxItems)
            break;
        freeNameList(nameList);
        nameList.maxItems = nameList.numItems+1;
        //printf("Second pass required, found %ld images\n", nameList.numItems);
    }
    if (nameList.numItems < 1) {
        #ifdef myUseCOut
    	std::cout << "Error: unable to find any DICOM images in "<< opts->indir <<std::endl;
    	#else
        printf("Error: unable to find any DICOM images in %s\n", opts->indir);
        #endif
        free(nameList.str); //ignore compile warning - memory only freed on first of 2 passes
        return EXIT_FAILURE;
    }
    long long nDcm = nameList.numItems;
    #ifdef myUseCOut
    //stdout is piped in XCode just like printf, if this works with QT then we could replace these duplicate commands...
    //try this out the next QT build:
    fprintf(stdout, "STDOUT PIPE TEST\n");
    std::cout << "Found "<< nameList.numItems <<" DICOM images" <<std::endl;
    #else
    printf( "Found %lu DICOM image(s)\n", nameList.numItems);
    #endif
    // struct TDICOMdata dcmList [nameList.numItems]; //<- this exhausts the stack for large arrays
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nameList.numItems * sizeof(struct  TDICOMdata));
    struct TDTI4D dti4D;
    int nConvertTotal = 0;
    for (int i = 0; i < nDcm; i++ ) {
        dcmList[i] = readDICOMv(nameList.str[i], opts->isVerbose, opts->compressFlag, &dti4D); //ignore compile warning - memory only freed on first of 2 passes
        if (dcmList[i].CSA.numDti > 1) { //4D dataset: dti4D arrays require huge amounts of RAM - write this immediately
            struct TDCMsort dcmSort[1];
            dcmSort[0].indx = i;
            dcmSort[0].img = ((uint64_t)dcmList[i].seriesNum << 32) + dcmList[i].imageNum;
            dcmList[i].converted2NII = 1;
            saveDcm2Nii(1, dcmSort, dcmList, &nameList, *opts, &dti4D);
            nConvertTotal++;
        }
    }
    //3: stack DICOMs with the same Series
    for (int i = 0; i < nDcm; i++ ) {
		if ((dcmList[i].converted2NII == 0) && (dcmList[i].isValid)) {
			int nConvert = 0;
			struct TWarnings warnings = setWarnings();
			for (int j = i; j < nDcm; j++)
				if (isSameSet(dcmList[i], dcmList[j], opts->isForceStackSameSeries, &warnings) )
					nConvert++;
			if (nConvert < 1) nConvert = 1; //prevents compiler warning for next line: never executed since j=i always causes nConvert ++

#ifdef _MSC_VER
			TDCMsort * dcmSort = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
#else
			struct TDCMsort dcmSort[nConvert];
#endif
			nConvert = 0;
			//warnings = setWarnings();
			for (int j = i; j < nDcm; j++)
				if (isSameSet(dcmList[i], dcmList[j], opts->isForceStackSameSeries, &warnings)) {
					dcmSort[nConvert].indx = j;
					dcmSort[nConvert].img = ((uint64_t)dcmList[j].seriesNum << 32) + dcmList[j].imageNum;
					dcmList[j].converted2NII = 1;
					nConvert++;
				}
			qsort(dcmSort, nConvert, sizeof(struct TDCMsort), compareTDCMsort); //sort based on series and image numbers....
			if (opts->isVerbose)
				nConvert = removeDuplicatesVerbose(nConvert, dcmSort, &nameList);
			else
				nConvert = removeDuplicates(nConvert, dcmSort);
			nConvertTotal += nConvert;
			saveDcm2Nii(nConvert, dcmSort, dcmList, &nameList, *opts, &dti4D);
#ifdef _MSC_VER
			free(dcmSort);
#endif
		}//convert all images of this series
    }
    free(dcmList);
    if (nConvertTotal == 0) {
        #ifdef myUseCOut
    	std::cout << "No valid DICOM files were found\n" <<std::endl;
    	#else
    	printf("No valid DICOM files were found\n");
    	#endif
    }
    freeNameList(nameList);
    //if (nameList.numItems > 0)
    //    for (int i = 0; i < nameList.numItems; i++)
    //        free(nameList.str[i]);
    //free(nameList.str);
    return EXIT_SUCCESS;
} //nii_loadDir()


/* cleaner than findPigz - perhaps validate someday
 void findExe(char name[512], const char * argv[]) {
    if (is_exe(name)) return; //name exists as provided
    char basename[1024];
    strcpy(basename, name); //basename = source
    //check executable folder
    strcpy(name,argv[0]);
    dropFilenameFromPath(name);
    char appendChar[2] = {"a"};
    appendChar[0] = kPathSeparator;
    if (name[strlen(name)-1] != kPathSeparator) strcat (name,appendChar);
    strcat(name,basename);
    if (is_exe(name)) return; //name exists as provided
    //check /opt
    strcpy (name,"/opt/local/bin/" );
    strcat (name, basename);
    if (is_exe(name)) return; //name exists as provided
    //check /usr
    strcpy (name,"/usr/local/bin/" );
    strcat (name, basename);
    if (is_exe(name)) return;
    strcpy(name,""); //not found!
}*/

void readFindPigz (struct TDCMopts *opts, const char * argv[]) {
    #if defined(_WIN64) || defined(_WIN32)
    strcpy(opts->pigzname,"pigz.exe");
    if (!is_exe(opts->pigzname)) {
    #ifdef myUseCOut
        #ifdef myDisableZLib
        std::cout << "Compression requires "<<opts->pigzname<<" in the same folder as the executable"<<std::endl;
		#else //myUseZLib
 		std::cout << "Compression will be faster with "<<opts->pigzname<<" in the same folder as the executable "<<std::endl;
		#endif
    #else
        #ifdef myDisableZLib
        printf("Compression requires %s in the same folder as the executable\n",opts->pigzname);
		#else //myUseZLib
 		printf("Compression will be faster with %s in the same folder as the executable\n",opts->pigzname);
		#endif
	#endif
        strcpy(opts->pigzname,"");
    } else
    	strcpy(opts->pigzname,".\\pigz"); //drop
    #else
    strcpy(opts->pigzname,"/usr/local/bin/pigz");
    char pigz[1024];
    strcpy(pigz, opts->pigzname);
    if (!is_exe(opts->pigzname)) {
        strcpy(opts->pigzname,"/usr/bin/pigz");
        if (!is_exe(opts->pigzname)) {
        strcpy(opts->pigzname,"/usr/local/bin/pigz_mricron");
        if (!is_exe(opts->pigzname)) {
            strcpy(opts->pigzname,argv[0]);
            dropFilenameFromPath(opts->pigzname);//, opts.pigzname);
            char appendChar[2] = {"a"};
            appendChar[0] = kPathSeparator;
            if (opts->pigzname[strlen(opts->pigzname)-1] != kPathSeparator) strcat (opts->pigzname,appendChar);
            strcat(opts->pigzname,"pigz_mricron");
            #if defined(_WIN64) || defined(_WIN32)
            strcat(opts->pigzname,".exe");
            #endif
            if (!is_exe(opts->pigzname)) {
             #ifdef myUseCOut
              #ifdef myDisableZLib
                std::cout << "Compression requires "<<pigz<<std::endl;
                #else //myUseZLib
                std::cout << "Compression will be faster with "<<pigz<<std::endl;
            	#endif
    		#else
            	#ifdef myDisableZLib
                printf("Compression requires %s\n",pigz);
            	#else //myUseZLib
                printf("Compression will be faster with %s\n",pigz);
            	#endif
            #endif
                strcpy(opts->pigzname,"");
            } //no pigz_mricron in exe's folder
        } //no /usr/local/pigz_mricron
       }//no /usr/bin/pigz
    } //no /usr/local/pigz
    #endif
} //readFindPigz()


#if defined(_WIN64) || defined(_WIN32)
//windows has unusual file permissions for many users - lets save preferences to the registry
void saveIniFile (struct TDCMopts opts) {
HKEY hKey;
if(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\dcm2nii",0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
	RegCloseKey(hKey);
	return;
}
DWORD dwValue    = opts.isGz;
  //RegSetValueEx(hKey,"isGZ", 0, REG_DWORD,reinterpret_cast<BYTE *>(&dwValue),sizeof(dwValue));
  //RegSetValueExA(hKey, "isGZ", 0, REG_DWORD, (LPDWORD)&dwValue, sizeof(dwValue));
  RegSetValueExA(hKey, "isGZ", 0, REG_DWORD, reinterpret_cast<BYTE *>(&dwValue), sizeof(dwValue));
  RegSetValueExA(hKey,"filename",0, REG_SZ,(LPBYTE)opts.filename, strlen(opts.filename)+1);
  RegCloseKey(hKey);
} //saveIniFile()

void readIniFile (struct TDCMopts *opts, const char * argv[]) {
    readFindPigz(opts, argv);
    #ifdef myEnableJasper
    opts->compressFlag = kCompressYes; //JASPER for JPEG2000
	#else
		#ifdef myDisableOpenJPEG
		opts->compressFlag = kCompressNone; //no decompressor
		#else
		opts->compressFlag = kCompressYes; //OPENJPEG for JPEG2000
		#endif
	#endif
    strcpy(opts->indir,"");
    strcpy(opts->outdir,"");
    opts->isOnlySingleFile = false; //convert all files in a directory, not just a single file
    opts->isForceStackSameSeries = false;
    opts->isCrop = false;
    opts->isGz = false;
    opts->isFlipY = true;
    opts->isRGBplanar = false;
    opts->isCreateBIDS =  false;
    opts->isCreateText = false;
#ifdef myDebug
    opts->isVerbose =   true;
#else
    opts->isVerbose = false;
#endif
    opts->isTiltCorrect = true;
    strcpy(opts->filename,"%f_%p_%t_%s");
     HKEY  hKey;
    DWORD vSize     = 0;
    DWORD dwDataType = 0;
    DWORD dwValue    = 0;
    //RegOpenKeyEx(RegOpenKeyEx, key, 0, accessRights, keyHandle);
    //if(RegOpenKeyEx(HKEY_CURRENT_USER,(WCHAR)"Software\\dcm2nii", 0, KEY_QUERY_VALUE,&hKey) != ERROR_SUCCESS) {
    if(RegOpenKeyExA(HKEY_CURRENT_USER,"Software\\dcm2nii", 0, KEY_QUERY_VALUE,&hKey) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return;
    }
    vSize = sizeof(dwValue);
    //if(RegQueryValueExA(hKey,"isGZ", 0, (LPDWORD )&dwDataType, (&dwValue), &vSize) == ERROR_SUCCESS)
    if(RegQueryValueExA(hKey,"isGZ", 0, (LPDWORD )&dwDataType, reinterpret_cast<BYTE *>(&dwValue), &vSize) == ERROR_SUCCESS)
    	opts->isGz = dwValue;
    vSize = 512;
    char buffer[512];
    if(RegQueryValueExA(hKey,"filename", 0,NULL,(LPBYTE)buffer,&vSize ) == ERROR_SUCCESS )
 	strcpy(opts->filename,buffer);
 RegCloseKey(hKey);
} //readIniFile()

#else
//for Unix we will save preferences in a hidden text file in the home directory
#define STATUSFILENAME "/.dcm2nii.ini"

void readIniFile (struct TDCMopts *opts, const char * argv[]) {
    readFindPigz(opts, argv);
    #ifdef myEnableJasper
    opts->compressFlag = kCompressYes; //JASPER for JPEG2000
	#else
		#ifdef myDisableOpenJPEG
		opts->compressFlag = kCompressNone; //no decompressor
		#else
		opts->compressFlag = kCompressYes; //OPENJPEG for JPEG2000
		#endif
	#endif
    //printf("%d %s\n",opts->compressFlag, opts->compressname);

    sprintf(opts->optsname, "%s%s", getenv("HOME"), STATUSFILENAME);
    strcpy(opts->indir,"");
    strcpy(opts->outdir,"");
    opts->isOnlySingleFile = false; //convert all files in a directory, not just a single file
    opts->isForceStackSameSeries = false;
    opts->isCrop = false;
    opts->isGz = false;
    opts->isFlipY = true; //false: images in raw DICOM orientation, true: image rows flipped to cartesian coordinates
    opts->isRGBplanar = false;
    opts->isCreateBIDS =  false;
    opts->isCreateText = false;
#ifdef myDebug
        opts->isVerbose =   true;
#else
        opts->isVerbose = false;
#endif
    opts->isTiltCorrect = true;
    strcpy(opts->filename,"%f_%p_%t_%s");
    FILE *fp = fopen(opts->optsname, "r");
    if (fp == NULL) return;
    char Setting[20],Value[255];
    //while ( fscanf(fp, "%[^=]=%s\n", Setting, Value) == 2 ) {
    //while ( fscanf(fp, "%[^=]=%s\n", Setting, Value) == 2 ) {
    while ( fscanf(fp, "%[^=]=%[^\n]\n", Setting, Value) == 2 ) {
        //printf(">%s<->'%s'\n",Setting,Value);
        if ( strcmp(Setting,"isGZ") == 0 )
            opts->isGz = atoi(Value);
        else if ( strcmp(Setting,"isBIDS") == 0 )
            opts->isCreateBIDS = atoi(Value);
        else if ( strcmp(Setting,"filename") == 0 )
            strcpy(opts->filename,Value);
    }
    fclose(fp);
} //readIniFile()


void saveIniFile (struct TDCMopts opts) {
    FILE *fp = fopen(opts.optsname, "w");
    //printf("%s\n",localfilename);
    if (fp == NULL) return;
    fprintf(fp, "isGZ=%d\n", opts.isGz);
    fprintf(fp, "isBIDS=%d\n", opts.isCreateBIDS);
    fprintf(fp, "filename=%s\n", opts.filename);
    fclose(fp);
} //saveIniFile()

#endif
