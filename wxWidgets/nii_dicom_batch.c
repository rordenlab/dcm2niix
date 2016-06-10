//#define myNoSave //do not save images to disk

#ifndef myDisableZLib
 #include <zlib.h>
 #ifndef myDisableTarGz 
 // #include "untgz.h"
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
#include <unistd.h>
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
}

void getFileName( char *pathParent, const char *path) //if path is c:\d1\d2 then filename is 'd2'
{
    const char *filename = strrchr(path, '/'); //UNIX
    if (filename == 0)
       filename = strrchr(path, '\\'); //Windows
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

bool is_fileNotDir(const char* path) { //returns false if path is a folder; requires #include <sys/stat.h>
    struct stat buf;
    stat(path, &buf);
    return S_ISREG(buf.st_mode);
} //is_file()

bool is_exe(const char* path) { //requires #include <sys/stat.h>
    struct stat buf;
    stat(path, &buf);
    return (S_ISREG(buf.st_mode) && (buf.st_mode & 0111) );
} //is_exe()

int is_dir(const char *pathname, int follow_link) {
struct stat s;
if ((NULL == pathname) || (0 == strlen(pathname)))
	return 0;
int err = stat(pathname, &s);
if(-1 == err) {
        return 0; /* does not exist */
} else {
    if(S_ISDIR(s.st_mode)) {
       return 1; /* it's a dir */
    } else {
        return 0;/* exists but is no dir */
    }
}
} //is_dir
/*int is_dir(const char *pathname, int follow_link) {
    //http://sources.gentoo.org/cgi-bin/viewvc.cgi/path-sandbox/trunk/libsbutil/get_tmp_dir.c?revision=260
	struct stat buf;
	int retval;
	if ((NULL == pathname) || (0 == strlen(pathname)))
		return 0;
	retval = follow_link ? stat(pathname, &buf) : lstat(pathname, &buf);
	if ((-1 != retval) && (S_ISDIR(buf.st_mode)))
		return 1;
	if ((-1 == retval) && (ENOENT != errno)) // Some or other error occurred
		return -1;
	return 0;
} //is_dir()
*/

bool isDICOMfile(const char * fname) {
    FILE *fp = fopen(fname, "rb");
	if (!fp)  return false;
	fseek(fp, 0, SEEK_END);
	long long fileLen=ftell(fp);
    if (fileLen < 256) return false;
	fseek(fp, 0, SEEK_SET);
	unsigned char buffer[256];
	fread(buffer, 256, 1, fp);
	fclose(fp);
    if ((buffer[128] != 'D') || (buffer[129] != 'I')  || (buffer[130] != 'C') || (buffer[131] != 'M'))
        return false;
    return true;
} //isDICOMfile()

void geCorrectBvecs(struct TDICOMdata *d, int sliceDir){
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
        #ifdef myUseCOut
     	std::cout<<"Error: Unable to determine DTI gradients, 0018,1312 should be either R or C" <<std::endl;
    	#else
        printf("Error: Unable to determine DTI gradients, 0018,1312 should be either R or C");
        #endif
        return;
    }
    if (abs(sliceDir) != 3)
            #ifdef myUseCOut
     	std::cout<<"GE DTI gradients only tested for axial acquisitions" <<std::endl;
    	#else
        printf("GE DTI gradients only tested for axial acquisitions");
        #endif
    //printf("GE row(0) or column(1) = %d",col);
    #ifdef myUseCOut
    std::cout<<"Reorienting "<< d->CSA.numDti << "GE DTI gradients. Please validate if you are conducting DTI analyses. isCol="<<col <<std::endl;
    #else
    printf("Reorienting %d GE DTI gradients. Please validate if you are conducting DTI analyses. isCol=%d\n", d->CSA.numDti, col);
    #endif
    for (int i = 0; i < d->CSA.numDti; i++) {
        float vLen = sqrt( (d->CSA.dtiV[i][1]*d->CSA.dtiV[i][1])
                          + (d->CSA.dtiV[i][2]*d->CSA.dtiV[i][2])
                          + (d->CSA.dtiV[i][3]*d->CSA.dtiV[i][3]));
        if ((d->CSA.dtiV[i][0] <= FLT_EPSILON)|| (vLen <= FLT_EPSILON) ) { //bvalue=0
            for (int v= 0; v < 4; v++)
                d->CSA.dtiV[i][v] =0.0f;
            continue; //do not normalize or reorient b0 vectors
        }
        d->CSA.dtiV[i][1] = -d->CSA.dtiV[i][1];
        if (!col) { //rows need to be swizzled
            float swap = d->CSA.dtiV[i][1];
            d->CSA.dtiV[i][1] = -d->CSA.dtiV[i][2];
            d->CSA.dtiV[i][2] = swap;
        }
        if (sliceDir < 0)
                d->CSA.dtiV[i][3] = -d->CSA.dtiV[i][3];
        if (isSameFloat(d->CSA.dtiV[i][1],-0)) d->CSA.dtiV[i][1] = 0.0f;
        if (isSameFloat(d->CSA.dtiV[i][2],-0)) d->CSA.dtiV[i][2] = 0.0f;
        if (isSameFloat(d->CSA.dtiV[i][3],-0)) d->CSA.dtiV[i][3] = 0.0f;
        
    }
} //geCorrectBvecs()

/*void philipsCorrectBvecs(struct TDICOMdata *d){
    //Philips DICOM data stored in patient (LPH) space, regardless of settings in Philips user interface
    //algorithm from PARtoNRRD/CATNAP (with July 20, 2007 patch) http://godzilla.kennedykrieger.org/~jfarrell/software_web.htm
    //0018,5100. patient orientation - 'HFS'
    //2001,100B Philips slice orientation (TRANSVERSAL, AXIAL, SAGITTAL)
    //2005,1071 MRStackAngulationAP
    //2005,1072 MRStackAngulationFH
    //2005,1073 MRStackAngulationRL
    if (d->manufacturer != kMANUFACTURER_PHILIPS) return;
    if (d->CSA.numDti < 1) return;
    mat33 tpp,tpo;
    if ((toupper(d->patientOrient[0])== 'F') && (toupper(d->patientOrient[1])== 'F'))
        LOAD_MAT33(tpp, 0,-1,0, -1,0,0, 0,0,1); //feet first
    else if ((toupper(d->patientOrient[0])== 'H') && (toupper(d->patientOrient[1])== 'F'))
        LOAD_MAT33(tpp, 0,1,0,-1,0,0, 0,0,-1); //head first
    else {
        printf("Unable to correct Philips DTI vectors: patient position must be head or feet first\n");
        return;
    }
    //unused? mat33 rev_tpp =nifti_mat33_transpose(tpp);
    if (toupper(d->patientOrient[2])== 'S')//supine
        LOAD_MAT33 (tpo, 1,0,0, 0,1,0, 0,0,1);
    else if (toupper(d->patientOrient[2])== 'P')//prone
        LOAD_MAT33 (tpo,-1,0,0, 0,-1,0, 0,0,1);
    else if ((toupper(d->patientOrient[2])== 'D') && (toupper(d->patientOrient[3])== 'R'))   //DR
        LOAD_MAT33 (tpo,0,-1,0, 1,0,0, 0,0,1);
    else if ((toupper(d->patientOrient[2])== 'D') && (toupper(d->patientOrient[3])== 'L'))//DL
        LOAD_MAT33 (tpo,0,1,0, -1,0,0, 0,0,1);
    else {
        printf("DTI vector error: Position is not HFS,HFP,HFDR,HFDL,FFS,FFP,FFDR, or FFDL: %s\n",d->patientOrient);
        return;
    }
    //unused? mat33 rev_tpo =nifti_mat33_transpose(tpo);
    //unused? mat33 tpom = nifti_mat33_mul( tpo, tpp);
    //unused? mat33 rev_tpom = nifti_mat33_mul( rev_tpp,rev_tpo  );
    printf("Reorienting %d Philip DTI gradients with angulations %f %f %f. Please validate if you are conducting DTI analyses.\n", d->CSA.numDti, d->angulation[1], d->angulation[2], d->angulation[3]);
    
    float rl = d->angulation[1]  * M_PI /180; //as radian
    float ap = d->angulation[2]  * M_PI /180;
    float fh = d->angulation[3]  * M_PI /180;
    //printf(" %f %f %f \n",ap,fh,rl);
    mat33 trl, tap, tfh, rev_tsom, dtiextra;
    LOAD_MAT33 (trl,1,0,0,  0, cos(rl),- sin(rl),  0, sin(rl),cos(rl));
    LOAD_MAT33 (tap, cos(ap),0, sin(ap),  0,1,0,                 - sin(ap),0, cos(ap));
    LOAD_MAT33 (tfh,cos(fh),- sin(fh),0, sin(fh), cos(fh),0,    0,0,1);
    mat33 rev_trl =nifti_mat33_transpose(trl);
    mat33 rev_tap =nifti_mat33_transpose(tap);
    mat33 rev_tfh =nifti_mat33_transpose(tfh);
    mat33 mtemp1 = nifti_mat33_mul( trl, tap);
    //unused? mat33 tang = nifti_mat33_mul( mtemp1, tfh);
    mtemp1 = nifti_mat33_mul( rev_tfh, rev_tap );
    mat33 rev_tang = nifti_mat33_mul( mtemp1, rev_trl);
    //kSliceOrientSag
    if (d->sliceOrient == kSliceOrientSag)//SAGITTAL
        LOAD_MAT33 (rev_tsom, 0,0,1,  0,-1,0, -1,0,0 );
    else if (d->sliceOrient == 2)//CORONAL
        LOAD_MAT33 (rev_tsom, 0,0,1,  -1,0,0, 0,1,0 );
    else
        LOAD_MAT33 (rev_tsom, 0,-1,0,  -1,0,0, 0,0,1 );
    LOAD_MAT33 (dtiextra, 0,-1,0,  -1,0,0, 0,0,1 );
    mat33 mtemp2 = nifti_mat33_mul( dtiextra, rev_tsom);
    mtemp1 = nifti_mat33_mul( mtemp2, rev_tang);
    for (int i = 0; i < d->CSA.numDti; i++) {
        //printf("%d\tvin=[\t%f\t%f\t%f\t]; bval=\t%f\n",i,d->CSA.dtiV[i][1],d->CSA.dtiV[i][2],d->CSA.dtiV[i][3],d->CSA.dtiV[i][0]);
        float vLen = sqrt( (d->CSA.dtiV[i][1]*d->CSA.dtiV[i][1])
                          + (d->CSA.dtiV[i][2]*d->CSA.dtiV[i][2])
                          + (d->CSA.dtiV[i][3]*d->CSA.dtiV[i][3]));
        if ((d->CSA.dtiV[i][0] <= FLT_EPSILON)|| (vLen <= FLT_EPSILON) ) { //bvalue=0
            for (int v= 0; v < 4; v++)
                d->CSA.dtiV[i][v] =0.0f;
            continue; //do not normalize or reorient b0 vectors
        }
        vec3 v3;
        
        for (int v= 1; v < 4; v++) //normalize and reverse vector directions
            v3.v[v-1] =-d->CSA.dtiV[i][v]/vLen;
        v3 = nifti_vect33mat33_mul(v3,mtemp1);
        v3 = nifti_vect33_norm(v3);
        d->CSA.dtiV[i][1] = v3.v[0];
        d->CSA.dtiV[i][2] = v3.v[1]; //NIfTI Y reversed relative to DICOM
        d->CSA.dtiV[i][3] = v3.v[2];
        //printf("%d\tvoutt=[\t%f\t%f\t%f\t]; bval=\t%f\n",i,d->CSA.dtiV[i][1],d->CSA.dtiV[i][2],d->CSA.dtiV[i][3],d->CSA.dtiV[i][0]);
        
    }
    if ( d->sliceOrient != kSliceOrientTra)
        printf("Warning: Philips DTI gradients only evaluated for axial (transverse) acquisitions. Please verify sign and direction\n");
} //philipsCorrectBvecs()
*/

void siemensPhilipsCorrectBvecs(struct TDICOMdata *d, int sliceDir){
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
        float vLen = sqrt( (d->CSA.dtiV[i][1]*d->CSA.dtiV[i][1])
                          + (d->CSA.dtiV[i][2]*d->CSA.dtiV[i][2])
                          + (d->CSA.dtiV[i][3]*d->CSA.dtiV[i][3]));
        if ((d->CSA.dtiV[i][0] <= FLT_EPSILON)|| (vLen <= FLT_EPSILON) ) { //bvalue=0
            for (int v= 0; v < 4; v++)
                d->CSA.dtiV[i][v] =0.0f;
            continue; //do not normalize or reorient b0 vectors
        }//if bvalue=0
        vec3 bvecs_old =setVec3(d->CSA.dtiV[i][1],d->CSA.dtiV[i][2],d->CSA.dtiV[i][3]);
        vec3 bvecs_new =setVec3(dotProduct(bvecs_old,read_vector),dotProduct(bvecs_old,phase_vector),dotProduct(bvecs_old,slice_vector) );
        bvecs_new = nifti_vect33_norm(bvecs_new);
        d->CSA.dtiV[i][1] = bvecs_new.v[0];
        d->CSA.dtiV[i][2] = -bvecs_new.v[1];
        d->CSA.dtiV[i][3] = bvecs_new.v[2];
        if (sliceDir == kSliceOrientMosaicNegativeDeterminant) d->CSA.dtiV[i][2] = -d->CSA.dtiV[i][2];
        for (int v= 0; v < 4; v++)
            if (d->CSA.dtiV[i][v] == -0.0f) d->CSA.dtiV[i][v] = 0.0f; //remove sign from values that are virtually zero
    } //for each direction
    #ifdef myUseCOut
    if (sliceDir == kSliceOrientMosaicNegativeDeterminant)
        std::cout<<"WARNING: please validate DTI vectors (matrix had a negative determinant, perhaps Siemens sagittal)."<<std::endl;
    else if ( d->sliceOrient == kSliceOrientTra)
    	std::cout<<"Saving "<<d->CSA.numDti<<" DTI gradients. Please validate if you are conducting DTI analyses."<<std::endl;
    else
    	std::cout<<"WARNING: DTI gradient directions only tested for axial (transverse) acquisitions. Please validate bvec files."<<std::endl;
    #else
    if (sliceDir == kSliceOrientMosaicNegativeDeterminant)
       printf("WARNING: please validate DTI vectors (matrix had a negative determinant, perhaps Siemens sagittal).\n"); 
    else if ( d->sliceOrient == kSliceOrientTra)
        printf("Saving %d DTI gradients. Please validate if you are conducting DTI analyses.\n", d->CSA.numDti);
    else
        printf("WARNING: DTI gradient directions only tested for axial (transverse) acquisitions. Please validate bvec files.\n");
	#endif
} //siemensPhilipsCorrectBvecs()

bool isSamePosition (struct TDICOMdata d, struct TDICOMdata d2){
    if (!isSameFloat(d.patientPosition[1],d2.patientPosition[1])) return false;
    if (!isSameFloat(d.patientPosition[2],d2.patientPosition[2])) return false;
    if (!isSameFloat(d.patientPosition[3],d2.patientPosition[3])) return false;
    return true;
} //isSamePosition()

bool nii_SaveDTI(char pathoutname[],int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TDCMopts opts, int sliceDir) {
    //reports true if last volume is excluded (e.g. philip stores an ADC map)
    //to do: works with 3D mosaics and 4D files, must remove repeated volumes for 2D sequences....

    uint64_t indx0 = dcmSort[0].indx; //first volume
    int numDti = dcmList[indx0].CSA.numDti;

    if (numDti < 1) return false;
    if ((numDti < 3) && (nConvert < 3)) return false;
    if (numDti == 1) {//extract DTI from different slices
        numDti = 0;
        for (int i = 0; i < nConvert; i++) { //for each image
            if ((dcmList[indx0].CSA.mosaicSlices > 1)  || (isSamePosition(dcmList[indx0],dcmList[dcmSort[i].indx]))) {
                if (numDti < kMaxDTIv) 
                    for (int v = 0; v < 4; v++) //for each vector+B-value
                        dcmList[indx0].CSA.dtiV[numDti][v] = dcmList[dcmSort[i].indx].CSA.dtiV[0][v];
                numDti++;
                
            } //for slices with repeats
        }//for each file
        dcmList[indx0].CSA.numDti = numDti;
    }
    if (numDti < 3) return false;
    if (numDti > kMaxDTIv) {
    	#ifdef myUseCOut
    	std::cout<<"Error: more than "<<kMaxDTIv<<" DTI directions detected (check for a new software)"<<std::endl;
		#else
        printf("Error: more than %d DTI directions detected (check for a new software)", kMaxDTIv);
        #endif
        return false;
    }
    bool bValueVaries = false;
    for (int i = 1; i < numDti; i++) //check if all bvalues match first volume
        if (dcmList[indx0].CSA.dtiV[i][0] != dcmList[indx0].CSA.dtiV[0][0]) bValueVaries = true;
    if (!bValueVaries) {
        for (int i = 1; i < numDti; i++)
                printf("bxyz %g %g %g %g\n",dcmList[indx0].CSA.dtiV[i][0],dcmList[indx0].CSA.dtiV[i][1],dcmList[indx0].CSA.dtiV[i][2],dcmList[indx0].CSA.dtiV[i][3]);
        printf("Error: only one B-value reported for all volumes: %g\n",dcmList[indx0].CSA.dtiV[0][0]);
        return false;
    }
        
    int firstB0 = -1;
    for (int i = 0; i < numDti; i++) //check if all bvalues match first volume
        if (isSameFloat(dcmList[indx0].CSA.dtiV[i][0],0) ) {
            firstB0 = i;
            break;
        }
    //printf("2015ALPHA %d -> %d\n",numDti, nConvert);
    #ifdef myUseCOut
    if (firstB0 < 0) 
    	std::cout<<"Warning: this diffusion series does not have a B0 (reference) volume"<<std::endl;
	if (firstB0 > 0) 
    	std::cout<<"Note: B0 not the first volume in the series (FSL eddy reference volume is "<<firstB0<<")"<<std::endl;
	
	#else
    if (firstB0 < 0) printf("Warning: this diffusion series does not have a B0 (reference) volume\n");
    if (firstB0 > 0) printf("Note: B0 not the first volume in the series (FSL eddy reference volume is %d)\n", firstB0);
	#endif
    bool isFinalADC = false;
    /*if ((dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_PHILIPS)
     && (!isSameFloat(dcmList[indx0].CSA.dtiV[numDti-1][0],0.0f))
     )
     printf("xxx-->%f\n", dcmList[indx0].CSA.dtiV[numDti-1][3]);*/
    if ((dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_PHILIPS)
        && (!isSameFloat(dcmList[indx0].CSA.dtiV[numDti-1][0],0.0f))  //not a B-0 image
        && ((isSameFloat(dcmList[indx0].CSA.dtiV[numDti-1][1],0.0f)) ||
            (isSameFloat(dcmList[indx0].CSA.dtiV[numDti-1][2],0.0f)) ||
            (isSameFloat(dcmList[indx0].CSA.dtiV[numDti-1][3],0.0f)) )) {//yet all vectors are zero!!!! must be ADC
            isFinalADC = true; //final volume is ADC map
            numDti --; //remove final volume - it is a computed ADC map!
            dcmList[indx0].CSA.numDti = numDti;
        }
    // philipsCorrectBvecs(&dcmList[indx0]); //<- replaced by unified siemensPhilips solution
    geCorrectBvecs(&dcmList[indx0],sliceDir);
    siemensPhilipsCorrectBvecs(&dcmList[indx0],sliceDir);
    if (opts.isVerbose) {
        for (int i = 0; i < (numDti-1); i++) {
        	#ifdef myUseCOut
    		std::cout<<i<<"\tB=\t"<<dcmList[indx0].CSA.dtiV[i][0]<<"\tVec=\t"<<
                   dcmList[indx0].CSA.dtiV[i][1]<<"\t"<<dcmList[indx0].CSA.dtiV[i][2]
                   <<"\t"<<dcmList[indx0].CSA.dtiV[i][3]<<std::endl;
			#else
            printf("%d\tB=\t%g\tVec=\t%g\t%g\t%g\n",i, dcmList[indx0].CSA.dtiV[i][0],
                   dcmList[indx0].CSA.dtiV[i][1],dcmList[indx0].CSA.dtiV[i][2],dcmList[indx0].CSA.dtiV[i][3]);
            
        	#endif
        } //for each direction
    }
    if (!opts.isFlipY ) { //!FLIP_Y&& (dcmList[indx0].CSA.mosaicSlices < 2) mosaics are always flipped in the Y direction
        for (int i = 0; i < (numDti-1); i++) {
            if (fabs(dcmList[indx0].CSA.dtiV[i][2]) > FLT_EPSILON)
                dcmList[indx0].CSA.dtiV[i][2] = -dcmList[indx0].CSA.dtiV[i][2];
        } //for each direction
    } //if not a mosaic
    //printf("%f\t%f\t%f",dcmList[indx0].CSA.dtiV[1][1],dcmList[indx0].CSA.dtiV[1][2],dcmList[indx0].CSA.dtiV[1][3]);
    char txtname[2048] = {""};
    strcpy (txtname,pathoutname);
    strcat (txtname,".bval");
    //printf("Saving DTI %s\n",txtname);
    FILE *fp = fopen(txtname, "w");
    if (fp == NULL) return isFinalADC;
    for (int i = 0; i < (numDti-1); i++)
        fprintf(fp, "%g\t", dcmList[indx0].CSA.dtiV[i][0]);
    fprintf(fp, "%g\n", dcmList[indx0].CSA.dtiV[numDti-1][0]);
    fclose(fp);
    strcpy(txtname,pathoutname);
    strcat (txtname,".bvec");
    //printf("Saving DTI %s\n",txtname);
    fp = fopen(txtname, "w");
    if (fp == NULL) return isFinalADC;
    for (int v = 1; v < 4; v++) {
        for (int i = 0; i < (numDti-1); i++)
            fprintf(fp, "%g\t", dcmList[indx0].CSA.dtiV[i][v]);
        fprintf(fp, "%g\n", dcmList[indx0].CSA.dtiV[numDti-1][v]);
    }
    fclose(fp);
    return isFinalADC;
} //nii_SaveDTI()

float sqr(float v){
    return v*v;
}  //sqr()

float intersliceDistance(struct TDICOMdata d1, struct TDICOMdata d2) {
    //some MRI scans have gaps between slices, some CT have overlapping slices. Comparing adjacent slices provides measure for dx between slices
    return sqrt( sqr(d1.patientPosition[1]-d2.patientPosition[1])+
                sqr(d1.patientPosition[2]-d2.patientPosition[2])+
                sqr(d1.patientPosition[3]-d2.patientPosition[3]));
} //intersliceDistance()

void swapDim3Dim4(int d3, int d4, struct TDCMsort dcmSort[]) {
    //swap space and time: input A0,A1...An,B0,B1...Bn output A0,B0,A1,B1,...
    int nConvert = d3 * d4;
    struct TDCMsort dcmSortIn[nConvert];
    for (int i = 0; i < nConvert; i++) dcmSortIn[i] = dcmSort[i];
    int i = 0;
    for (int b = 0; b < d3; b++)
        for (int a = 0; a < d4; a++) {
            int k = (a *d3) + b;
            //printf("%d -> %d %d ->%d\n",i,a, b, k);
            dcmSort[k] = dcmSortIn[i];
            i++;
        }
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
    while (pos<strlen(inname)) {
        if (inname[pos] == '%') {
            if (pos > start) {
                strncpy(&newstr[0], &inname[0] + start, pos - start);
                newstr[pos - start] = '\0';
                strcat (outname,newstr);
            }
            pos++; //extra increment: skip both % and following character
            char f = 'P';
            if (pos<strlen(inname)) f = toupper(inname[pos]);
            if (f == 'C')
                strcat (outname,dcm.imageComments);
            if (f == 'F')
                strcat (outname,opts.indirParent);
            if (f == 'I')
                strcat (outname,dcm.patientID);
            if (f == 'N')
                strcat (outname,dcm.patientName);
            if (f == 'P')
                strcat (outname,dcm.protocolName);
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
            start = pos + 1;
        } //found a % character
        pos++;
    } //for each character in input
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
             
            || (outname[pos] == '*') || (outname[pos] == '|') || (outname[pos] == '?'))
            outname[pos] = '_';
    //printf("name=*%s* %d %d\n", outname, pos,start);
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
void writeNiiGz (char * baseName, struct nifti_1_header hdr,  unsigned char* src_buffer, unsigned long src_len) {
    //create gz file in RAM, save to disk http://www.zlib.net/zlib_how.html
    // in general this single-threaded approach is slower than PIGZ but is useful for slow (network attached) disk drives
    char fname[2048] = {""};
    strcpy (fname,baseName);
    strcat (fname,".nii.gz");
    unsigned long hdrPadBytes = sizeof(hdr) + 4; //348 byte header + 4 byte pad
    unsigned long cmp_len = compressBound(src_len+hdrPadBytes);
    unsigned char *pCmp = (unsigned char *)malloc(cmp_len);
    z_stream strm;
    uLong file_crc32 = crc32(0L, Z_NULL, 0);
    //strm.adler = crc32(0L, Z_NULL, 0);
    strm.total_in = 0;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_out = pCmp; // output char array
    strm.avail_out = (unsigned int)cmp_len; // size of output
    //if ( deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY)!= Z_OK) return;
    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION)!= Z_OK) return;
    //add header
    unsigned char *pHdr = (unsigned char *)malloc(hdrPadBytes);
    memcpy(pHdr,&hdr, sizeof(hdr));
    strm.avail_in = (unsigned int)hdrPadBytes; // size of input
    strm.next_in = (Bytef *)pHdr; // input header
    deflate(&strm, Z_NO_FLUSH);
    file_crc32 = crc32(file_crc32, pHdr, (unsigned int)hdrPadBytes);
    //add image
    strm.avail_in = (unsigned int)src_len; // size of input
    strm.next_in = (Bytef *)src_buffer; // input image
    deflate(&strm, Z_FINISH); //Z_NO_FLUSH;
    //finish up
    deflateEnd(&strm);
    file_crc32 = crc32(file_crc32, src_buffer, (unsigned int)src_len);
    cmp_len = strm.total_out;
    if (cmp_len <= 0) return;
    FILE *fileGz = fopen(fname, "wb");
    if (!fileGz) return;
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
    fwrite (&pCmp[2] , sizeof(char), cmp_len-6, fileGz); //-6 as LZ78 format has 2 bytes header and 4 bytes tail
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
} //writeNiiGz()
#endif

int nii_saveNII(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts) {
    hdr.vox_offset = 352;
    size_t imgsz = nii_ImgBytes(hdr);
    #ifndef myDisableZLib
        if  ((opts.isGz) &&  (strlen(opts.pigzname)  < 1) &&  ((imgsz+hdr.vox_offset) <  2147483647) ) { //use internal compressor
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
        strcat(command, "\" \"");
        strcat(command, fname);
        strcat(command, "\""); //add quotes in case spaces in filename 'pigz "c:\my dir\img.nii"'
    	#if defined(_WIN64) || defined(_WIN32) //using CreateProcess instead of system to run in background (avoids screen flicker)
    	PROCESS_INFORMATION ProcessInfo; //This is what we get as an [out] parameter
    	STARTUPINFO StartupInfo; //This is an [in] parameter
    	ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    	StartupInfo.cb = sizeof StartupInfo ; //Only compulsory field
    	if(CreateProcess(NULL, command, NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL, NULL,&StartupInfo,&ProcessInfo)) { 
        	WaitForSingleObject(ProcessInfo.hProcess,INFINITE);
        	CloseHandle(ProcessInfo.hThread);
        	CloseHandle(ProcessInfo.hProcess);
    	} else
    	#ifdef myUseCOut
    	std::cout<<"compression failed "<<command<<std::endl;
		#else
		printf("compression failed %s\n",command);
    	#endif
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
    int zeroPadLen = (1 + log10(nVol));
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


int saveDcm2Nii(int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TSearchList *nameList, struct TDCMopts opts) {
    bool iVaries = intensityScaleVaries(nConvert,dcmSort,dcmList);
    uint64_t indx = dcmSort[0].indx;
    uint64_t indx0 = dcmSort[0].indx;
    bool saveAs3D = dcmList[indx].isHasPhase;
    struct nifti_1_header hdr0;
    unsigned char * img = nii_loadImgX(nameList->str[indx], &hdr0,dcmList[indx], iVaries);
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

    
    /*if ((nConvert < 2) && (dcmList[indx].locationsInAcquisition > 0)) { //stack philips 4D file
        
        int nAcq = dcmList[indx].locationsInAcquisition;
        if ((hdr0.dim[0] < 4) && ((hdr0.dim[3]%nAcq)==0)) {
            hdr0.dim[4] = hdr0.dim[3]/nAcq;
            hdr0.dim[3] = nAcq;
            hdr0.dim[0] = 4;
        }
        if (hdr0.dim[0] > 3) {
            if (dcmList[indx].patientPositionSequentialRepeats > 1) //swizzle 3rd and 4th dimension (Philips stores time as 3rd dimension)
                imgM = nii_XYTZ_XYZT(imgM, &hdr0,dcmList[indx].patientPositionSequentialRepeats );
        }
    }*/
    //printf(" %d %d %d %d %lu\n", hdr0.dim[1], hdr0.dim[2], hdr0.dim[3], hdr0.dim[4], (unsigned long)[imgM length]);
    if (nConvert > 1) {
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
            } else
                hdr0.dim[3] = nConvert;
            float dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
            if ((hdr0.dim[4] > 0) && (dx ==0) && (dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_PHILIPS)) {
                swapDim3Dim4(hdr0.dim[3],hdr0.dim[4],dcmSort);
                dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
                //printf("swizzling 3rd and 4th dimensions (XYTZ -> XYZT), assuming interslice distance is %f\n",dx);
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
            img = nii_loadImgX(nameList->str[indx], &hdrI, dcmList[indx],iVaries);
            if ((hdr0.dim[1] != hdrI.dim[1]) || (hdr0.dim[2] != hdrI.dim[2]) || (hdr0.bitpix != hdrI.bitpix)) {
                    #ifdef myUseCOut
    	std::cout<<"Error: image dimensions differ "<<nameList->str[dcmSort[0].indx]<<"  "<<nameList->str[indx]<<std::endl;
		#else
                printf("Error: image dimensions differ %s %s",nameList->str[dcmSort[0].indx], nameList->str[indx]);
                #endif
                return EXIT_FAILURE;
            }
            memcpy(&imgM[(uint64_t)i*imgsz], &img[0], imgsz);
            free(img);
        }
    }

    //printf("Mango %zd\n", (imgsz* nConvert)); return 0;
    char pathoutname[2048] = {""};
    if (nii_createFilename(dcmList[dcmSort[0].indx], pathoutname, opts) == EXIT_FAILURE) return EXIT_FAILURE;
    if (strlen(pathoutname) <1) return EXIT_FAILURE;
    int sliceDir = 0;
    if (hdr0.dim[3] > 1)
        sliceDir =headerDcm2Nii2(dcmList[dcmSort[0].indx],dcmList[dcmSort[nConvert-1].indx] , &hdr0);
    if (sliceDir < 0) {
        imgM = nii_flipZ(imgM, &hdr0);
        sliceDir = abs(sliceDir);
    }
    bool isFinalADC = nii_SaveDTI(pathoutname,nConvert, dcmSort, dcmList, opts, sliceDir);
    isFinalADC =isFinalADC; //simply to silence compiler warning when myNoSave defined

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
    if ((hdr0.dim[4] > 1) && (saveAs3D))
        nii_saveNII3D(pathoutname, hdr0, imgM,opts);
    else {
        if ((isFinalADC) && (hdr0.dim[4] > 2)) { //ADC maps can disrupt analysis: save a copy with the ADC map, and another without
            char pathoutnameADC[2048] = {""};
            strcat(pathoutnameADC,pathoutname);
            strcat(pathoutnameADC,"_ADC");
            nii_saveNII(pathoutnameADC, hdr0, imgM, opts);
            hdr0.dim[4] = hdr0.dim[4]-1;
        };
        nii_saveNII(pathoutname, hdr0, imgM, opts);
    }
#endif
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

bool isSameSet (struct TDICOMdata d1, struct TDICOMdata d2) {
    //returns true if d1 and d2 should be stacked together as a signle output
    if (!d1.isValid) return false;
    if (!d2.isValid) return false;
    if ((d1.dateTime == d2.dateTime) && (d1.seriesNum == d2.seriesNum) && (d1.bitsAllocated == d2.bitsAllocated)
        && (d1.xyzDim[1] == d2.xyzDim[1]) && (d1.xyzDim[2] == d2.xyzDim[2]) && (d1.xyzDim[3] == d2.xyzDim[3]) )
        return true;
    return false;
} //isSameSet()

void searchDirForDICOM(char *path, struct TSearchList *nameList, int maxDepth, int depth) {
    tinydir_dir dir;
    tinydir_open(&dir, path);
    while (dir.has_next) {
        tinydir_file file;
        tinydir_readfile(&dir, &file);
        //printf("%s\n", file.name);
        char filename[768] ="";
        strcat(filename, path);
        strcat(filename,kFileSep);
        strcat(filename, file.name);
        if ((file.is_dir) && (depth < maxDepth) && (file.name[0] != '.'))
            searchDirForDICOM(filename, nameList, maxDepth, depth+1);
        else if (isDICOMfile(filename)) {
            if (nameList->numItems < nameList->maxItems) {
                nameList->str[nameList->numItems]  = (char *)malloc(strlen(filename)+1);
                strcpy(nameList->str[nameList->numItems],filename);
                //printf("OK\n");
            }
            nameList->numItems++;
            //printf("dcm %lu %s \n",nameList->numItems, filename);
        } else {
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
        if (dcmSort[i].img == dcmSort[i-1].img)
            nDuplicates ++;
        else {
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

int removeDuplicatesVerbose(int nConvert, struct TDCMsort dcmSort[], struct TSearchList *nameList){
    //done AFTER sorting, so duplicates will be sequential
    if (nConvert < 2) return nConvert;
    int nDuplicates = 0;
    for (int i = 1; i < nConvert; i++) {
        if (dcmSort[i].img == dcmSort[i-1].img) {
                #ifdef myUseCOut
    	std::cout<<"\t"<<nameList->str[dcmSort[i-1].indx]<<"\t"<<nameList->str[dcmSort[i].indx] <<std::endl;
		#else
            printf("\t%s\t%s\n",nameList->str[dcmSort[i-1].indx],nameList->str[dcmSort[i].indx]);
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

int convert_parRec(struct TDCMopts opts) {
    //sample dataset from Ed Gronenschild <ed.gronenschild@maastrichtuniversity.nl>
    struct TSearchList nameList;
    nameList.numItems = 1;
    nameList.maxItems = 1;
    nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //we reserve one pointer (32 or 64 bits) per potential file
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nameList.numItems * sizeof(struct  TDICOMdata));
    nameList.str[0]  = (char *)malloc(strlen(opts.indir)+1);
    strcpy(nameList.str[0],opts.indir);
    dcmList[0] = nii_readParRec(nameList.str[0]);
    struct TDCMsort dcmSort[1];
    dcmSort[0].indx = 0;
    saveDcm2Nii(1, dcmSort, dcmList, &nameList, opts);
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

int nii_loadDir (struct TDCMopts* opts) {
    //printf("-->%s",opts->filename);
    //return EXIT_FAILURE;
    if (strlen(opts->indir) < 1) {
         #ifdef myUseCOut
    	std::cout<<"No input"<<std::endl;
		#else
        printf("No input\n");
        #endif
        return EXIT_FAILURE;
    }
    #ifdef myUseCOut
     std::cout << "Version  " <<kDCMvers <<std::endl; 
    #else
    printf("Version %s\n",kDCMvers);
    #endif

    
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
    if (isFile && ((isExt(indir, ".par")) || (isExt(indir, ".rec"))) ) {
        strcpy(opts->indir, indir); //set to original file name, not path
        return convert_parRec(*opts);
    }

    getFileName(opts->indirParent, opts->indir);
    struct TSearchList nameList;
    nameList.numItems = 0;
    nameList.maxItems = 64000-1;
    nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //we reserve one pointer (32 or 64 bits) per potential file
    //1: find filenames of dicom files
    searchDirForDICOM(opts->indir, &nameList,  5,1);
    if (nameList.numItems < 1) {
            #ifdef myUseCOut
    	std::cout << "Error: unable to find any DICOM images in "<< opts->indir <<std::endl;
    	#else
        printf("Error: unable to find any DICOM images in %s\n", opts->indir);
        #endif
        free(nameList.str);
        return EXIT_FAILURE;
    }
    if (nameList.numItems < 1) {
                #ifdef myUseCOut
    	std::cout << "Overwhelmed: found more than "<<nameList.maxItems<<" DICOM images in " << opts->indir <<std::endl;
    	#else
        printf("Overwhelmed: found more than %lu DICOM images in %s\n",nameList.maxItems, opts->indir);
        #endif
        //goto freeMem;
        return EXIT_FAILURE;
    }
    long long nDcm = nameList.numItems;
    #ifdef myUseCOut
    	std::cout << "Found "<< nameList.numItems <<" DICOM images" <<std::endl;
    	#else
    printf( "Found %lu DICOM images\n", nameList.numItems);
    #endif
    // struct TDICOMdata dcmList [nameList.numItems]; //<- this exhausts the stack for large arrays
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nameList.numItems * sizeof(struct  TDICOMdata));
    for (int i = 0; i < nameList.numItems; i++ )
        dcmList[i] = readDICOMv(nameList.str[i], opts->isVerbose);
    //3: stack DICOMs with the same Series
    int nConvertTotal = 0;
    for (int i = 0; i < nDcm; i++ ) {
        if ((dcmList[i].converted2NII == 0) && (dcmList[i].isValid)) {
            int nConvert = 0;
            for (int j = i; j < nDcm; j++)
                if (isSameSet(dcmList[i],dcmList[j]) )
                    nConvert ++;
            struct TDCMsort dcmSort[nConvert];
            nConvert = 0;
            for (int j = i; j < nDcm; j++)
                if (isSameSet(dcmList[i],dcmList[j]) ) {
                    dcmSort[nConvert].indx = j;
                    dcmSort[nConvert].img = ((uint64_t)dcmList[j].seriesNum << 32)+ dcmList[j].imageNum;
                    dcmList[j].converted2NII = 1;
                    nConvert ++;
                }
            qsort(dcmSort, nConvert, sizeof(struct TDCMsort), compareTDCMsort); //sort based on series and image numbers....
            if (opts->isVerbose)
                nConvert = removeDuplicatesVerbose(nConvert, dcmSort, &nameList);
            else
                nConvert = removeDuplicates(nConvert, dcmSort);
            nConvertTotal += nConvert;
            saveDcm2Nii(nConvert, dcmSort, dcmList, &nameList, *opts);
        }//convert all images of this series
    }
    free(dcmList);
    if (nConvertTotal == 0) 
        #ifdef myUseCOut
    	std::cout << "No valid DICOM files were found\n" <<std::endl;
    	#else
    	printf("No valid DICOM files were found\n");
    	#endif
    if (nameList.numItems > 0)
        for (int i = 0; i < nameList.numItems; i++)
            free(nameList.str[i]);
    free(nameList.str);
    return EXIT_SUCCESS;
} //nii_loadDir()

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
    strcpy(opts->indir,"");
    strcpy(opts->outdir,"");
    opts->isGz = false;
    opts->isFlipY = true;
#ifdef myDebug
    opts->isVerbose =   true;
#else
    opts->isVerbose = false;
#endif
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
    sprintf(opts->optsname, "%s%s", getenv("HOME"), STATUSFILENAME);
    strcpy(opts->indir,"");
    strcpy(opts->outdir,"");
    opts->isGz = false;
    opts->isFlipY = true; //false: images in raw DICOM orientation, true: image rows flipped to cartesian coordinates
#ifdef myDebug
        opts->isVerbose =   true;
#else
        opts->isVerbose = false;
#endif
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
    fprintf(fp, "filename=%s\n", opts.filename);
    fclose(fp);
} //saveIniFile()

#endif




