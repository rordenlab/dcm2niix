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
#endif
#ifndef myDisableZLib
    #ifdef MiniZ
        #include "miniz.c"  //single file clone of libz
    #else
        #include <zlib.h>
    #endif
#else
	#undef MiniZ
#endif
#include "tinydir.h"
#include "print.h"
#include "nifti1_io_core.h"
#ifndef USING_R
#include "nifti1.h"
#endif
#include "nii_dicom_batch.h"
#ifndef USING_R
#include "nii_foreign.h"
#endif
#include "nii_dicom.h"
#include <ctype.h> //toupper
#include <float.h>
#include <math.h>
#include <stdbool.h> //requires VS 2015 or later
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
//#ifdef myEnableOtsu
//	#include "nii_ostu_ml.h" //provide better brain crop, but artificially reduces signal variability in air
//#endif
#include <time.h>  // clock_t, clock, CLOCKS_PER_SEC
#include "nii_ortho.h"
#if defined(_WIN64) || defined(_WIN32)
    #include <windows.h> //write to registry
#endif
#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif
#if defined(_WIN64) || defined(_WIN32)
	#define myTextFileInputLists //comment out to disable this feature: https://github.com/rordenlab/dcm2niix/issues/288
	const char kPathSeparator ='\\';
	const char kFileSep[2] = "\\";
#else
	#define myTextFileInputLists
	const char kPathSeparator ='/';
	const char kFileSep[2] = "/";
#endif

#ifdef USING_R
#include "ImageList.h"

#undef isnan
#define isnan ISNAN
#endif

#define newTilt

struct TDCMsort {
  uint64_t indx, img;
  uint32_t dimensionIndexValues[MAX_NUMBER_OF_DIMENSIONS];
};


struct TSearchList {
    unsigned long numItems, maxItems;
    char **str;
};

#ifndef PATH_MAX
	#define PATH_MAX 4096
#endif

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
    	char cwd[PATH_MAX];
   		char* ok = getcwd(cwd, sizeof(cwd));
   		if (ok !=NULL)
   			strcat (path,cwd);
    }
}

void dropTrailingFileSep(char *path) { //
   size_t len = strlen(path) - 1;
   if (len <= 0) return;
   if (path[len] == '/')
   	path[len] = '\0';
   else if (path[len] == '\\')
   	path[len] = '\0';
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

#if defined(_WIN64) || defined(_WIN32)
	//Windows does not support lstat
	int is_dir(const char *pathname, int follow_link) {
		struct stat s;
		if ((NULL == pathname) || (0 == strlen(pathname)))
			return 0;
		int err = stat(pathname, &s);
		if(-1 == err)
			return 0; // does not exist
		else {
			if(S_ISDIR(s.st_mode)) {
			   return 1; // it's a dir
			} else {
				return 0;// exists but is no dir
			}
		}
	}// is_dir()
#else //if windows else Unix
	int is_dir(const char *pathname, int follow_link)
	{
		struct stat s;
		int retval;
		if ((NULL == pathname) || (0 == strlen(pathname)))
			return 0; // does not exist
		retval = follow_link ? stat(pathname, &s) : lstat(pathname, &s);
		if ((-1 != retval) && (S_ISDIR(s.st_mode)))
			return 1; // it's a dir
		return 0; // exists but is no dir
	}// is_dir()
#endif

void geCorrectBvecs(struct TDICOMdata *d, int sliceDir, struct TDTI *vx, int isVerbose){
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
    	printMessage("GE DTI directions require head first supine acquisition\n");
		return;
    }
    bool col = false;
    if (d->phaseEncodingRC == 'C')
        col = true;
    else if (d->phaseEncodingRC != 'R') {
        printWarning("Unable to determine DTI gradients, 0018,1312 should be either R or C");
        return;
    }
    if (abs(sliceDir) != 3)
        printWarning("GE DTI only tested for axial acquisitions (solution: use Xiangrui Li's dicm2nii)\n");
    //GE vectors from Xiangrui Li' dicm2nii, validated with datasets from https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging
	ivec3 flp;
	if (abs(sliceDir) == 1)
		flp = setiVec3(1, 1, 0); //SAGITTAL
	else if (abs(sliceDir) == 2)
		flp = setiVec3(0, 1, 1); //CORONAL
	else if (abs(sliceDir) == 3)
		flp = setiVec3(0, 0, 1); //AXIAL
	else {
		printMessage("Impossible GE slice orientation!");
		flp = setiVec3(0, 0, 1); //AXIAL???
	}
	if (sliceDir < 0)
    	flp.v[2] = 1 - flp.v[2];
    if ((isVerbose) || (!col)) {
		printMessage("Saving %d DTI gradients. GE Reorienting %s : please validate. isCol=%d sliceDir=%d flp=%d %d %d\n", d->CSA.numDti, d->protocolName, col, sliceDir, flp.v[0], flp.v[1],flp.v[2]);
		if (!col)
			printWarning("Reorienting for ROW phase-encoding untested.\n");
	}
	bool scaledBValWarning = false;
    for (int i = 0; i < d->CSA.numDti; i++) {
        float vLen = sqrt( (vx[i].V[1]*vx[i].V[1])
                          + (vx[i].V[2]*vx[i].V[2])
                          + (vx[i].V[3]*vx[i].V[3]));
         if ((vx[i].V[0] <= FLT_EPSILON)|| (vLen <= FLT_EPSILON) ) { //bvalue=0
            for (int v= 1; v < 4; v++)
                vx[i].V[v] = 0.0f;
            continue; //do not normalize or reorient 0 vectors
        }
        if ((vLen > 0.03) && (vLen < 0.97)) {
        	//bVal scaled by norm(g)^2 https://github.com/rordenlab/dcm2niix/issues/163
        	float bVal = vx[i].V[0] * (vLen * vLen);
        	if (!scaledBValWarning) {
        		printMessage("GE BVal scaling (e.g. %g -> %g s/mm^2)\n", vx[i].V[0], bVal);
        		scaledBValWarning = true;
        	}
        	vx[i].V[0] = bVal;
        	vx[i].V[1] = vx[i].V[1]/vLen;
        	vx[i].V[2] = vx[i].V[2]/vLen;
        	vx[i].V[3] = vx[i].V[3]/vLen;
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
}// geCorrectBvecs()

void siemensPhilipsCorrectBvecs(struct TDICOMdata *d, int sliceDir, struct TDTI *vx, int isVerbose){
    //see Matthew Robson's  http://users.fmrib.ox.ac.uk/~robson/internal/Dicom2Nifti111.m
    //convert DTI vectors from scanner coordinates to image frame of reference
    //Uses 6 orient values from ImageOrientationPatient  (0020,0037)
    // requires PatientPosition 0018,5100 is HFS (head first supine)
    if ((d->manufacturer != kMANUFACTURER_BRUKER) && (d->manufacturer != kMANUFACTURER_HITACHI) && (d->manufacturer != kMANUFACTURER_UIH) && (d->manufacturer != kMANUFACTURER_SIEMENS) && (d->manufacturer != kMANUFACTURER_PHILIPS)) return;
    if (d->CSA.numDti < 1) return;
    if (d->manufacturer == kMANUFACTURER_UIH) {
    	for (int i = 0; i < d->CSA.numDti; i++) {
    		vx[i].V[2] = -vx[i].V[2];
    		for (int v= 0; v < 4; v++)
            	if (vx[i].V[v] == -0.0f) vx[i].V[v] = 0.0f; //remove sign from values that are virtually zero
		}
    	//for (int i = 0; i < 3; i++)
    	//	printf("%g %g %g\n", vx[i].V[1], vx[i].V[2], vx[i].V[3]);
    	return;
    } //https://github.com/rordenlab/dcm2niix/issues/225
    if ((toupper(d->patientOrient[0])== 'H') && (toupper(d->patientOrient[1])== 'F') && (toupper(d->patientOrient[2])== 'S'))
        ; //participant was head first supine
    else {
        printMessage("Check Siemens/Philips bvecs: expected Patient Position (0018,5100) to be 'HFS' not '%s'\n", d->patientOrient);
        //return; //see https://github.com/rordenlab/dcm2niix/issues/238
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
            if (vx[i].V[0] > 5.0) //Philip stores n.b. UIH B=1.25126 Vec=0,0,0 while Philips stored isotropic images
                printWarning("Volume %d appears to be derived image ADC/Isotropic (non-zero b-value with zero vector length)\n", i);
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
    if (d->isVectorFromBMatrix) {
        printWarning("Saving %d DTI gradients. Eddy users: B-matrix does not encode b-vector polarity (issue 265).\n", d->CSA.numDti);
    }  else if (abs(sliceDir) == kSliceOrientMosaicNegativeDeterminant) {
       printWarning("Saving %d DTI gradients. Validate vectors (matrix had a negative determinant).\n", d->CSA.numDti); //perhaps Siemens sagittal
    } else if (( d->sliceOrient == kSliceOrientTra) || (d->manufacturer != kMANUFACTURER_PHILIPS)) {
        if (isVerbose)
        	printMessage("Saving %d DTI gradients. Validate vectors.\n", d->CSA.numDti);
    } else if ( d->sliceOrient == kSliceOrientUnknown)
    	printWarning("Saving %d DTI gradients. Validate vectors (image slice orientation not reported, e.g. 2001,100B).\n", d->CSA.numDti);
	if (d->manufacturer == kMANUFACTURER_BRUKER)
		printWarning("Bruker DTI support experimental (issue 265).\n");
}// siemensPhilipsCorrectBvecs()

bool isNanPosition(struct TDICOMdata d) { //in 2007 some Siemens RGB DICOMs did not include the PatientPosition 0020,0032 tag
    if (isnan(d.patientPosition[1])) return true;
    if (isnan(d.patientPosition[2])) return true;
    if (isnan(d.patientPosition[3])) return true;
    return false;
}// isNanPosition()

bool isSamePosition(struct TDICOMdata d, struct TDICOMdata d2){
    if ( isNanPosition(d) ||  isNanPosition(d2)) return false;
    if (!isSameFloat(d.patientPosition[1],d2.patientPosition[1])) return false;
    if (!isSameFloat(d.patientPosition[2],d2.patientPosition[2])) return false;
    if (!isSameFloat(d.patientPosition[3],d2.patientPosition[3])) return false;
    return true;
}// isSamePosition()

void nii_saveText(char pathoutname[], struct TDICOMdata d, struct TDCMopts opts, struct nifti_1_header *h, char * dcmname) {
	if (!opts.isCreateText) return;
	char txtname[2048] = {""};
	strcpy (txtname,pathoutname);
    strcat (txtname,".txt");
    //printMessage("Saving text %s\n",txtname);
    FILE *fp = fopen(txtname, "w");
    fprintf(fp, "%s\tField Strength:\t%g\tProtocolName:\t%s\tScanningSequence00180020:\t%s\tTE:\t%g\tTR:\t%g\tSeriesNum:\t%ld\tAcquNum:\t%d\tImageNum:\t%d\tImageComments:\t%s\tDateTime:\t%f\tName:\t%s\tConvVers:\t%s\tDoB:\t%s\tGender:\t%c\tAge:\t%s\tDimXYZT:\t%d\t%d\t%d\t%d\tCoil:\t%d\tEchoNum:\t%d\tOrient(6)\t%g\t%g\t%g\t%g\t%g\t%g\tbitsAllocated\t%d\tInputName\t%s\n",
      pathoutname, d.fieldStrength, d.protocolName, d.scanningSequence, d.TE, d.TR, d.seriesNum, d.acquNum, d.imageNum, d.imageComments,
      d.dateTime, d.patientName, kDCMvers, d.patientBirthDate, d.patientSex, d.patientAge, h->dim[1], h->dim[2], h->dim[3], h->dim[4],
            d.coilCrc,d.echoNum, d.orient[1], d.orient[2], d.orient[3], d.orient[4], d.orient[5], d.orient[6],
            d.bitsAllocated, dcmname);
    fclose(fp);
}// nii_saveText()

#ifndef USING_R
#define myReadAsciiCsa
#endif

#ifdef myReadAsciiCsa
//read from the ASCII portion of the Siemens CSA series header
//  this is not recommended: poorly documented
//  it is better to stick to the binary portion of the Siemens CSA image header

#if  defined(_WIN64) || defined(_WIN32) || defined(__sun) || (defined(__APPLE__) && defined(__POWERPC__))
//https://opensource.apple.com/source/Libc/Libc-1044.1.2/string/FreeBSD/memmem.c
/*-
 * Copyright (c) 2005 Pascal Gloor <pascal.gloor@spale.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
const void * memmem(const char *l, size_t l_len, const char *s, size_t s_len) {
	char *cur, *last;
	const char *cl = (const char *)l;
	const char *cs = (const char *)s;
	/* we need something to compare */
	if (l_len == 0 || s_len == 0)
		return NULL;
	/* "s" must be smaller or equal to "l" */
	if (l_len < s_len)
		return NULL;
	/* special case where s_len == 1 */
	if (s_len == 1)
		return memchr(l, (int)*cs, l_len);
	/* the last position where its possible to find "s" in "l" */
	last = (char *)cl + l_len - s_len;
	for (cur = (char *)cl; cur <= last; cur++)
		if (cur[0] == cs[0] && memcmp(cur, cs, s_len) == 0)
			return cur;
	return NULL;
}
//n.b. memchr returns "const void *" not "void *" for Windows C++ https://msdn.microsoft.com/en-us/library/d7zdhf37.aspx
#endif //for systems without memmem

int readKeyN1(const char * key,  char * buffer, int remLength) { //look for text key in binary data stream, return subsequent integer value
	int ret = 0;
	char *keyPos = (char *)memmem(buffer, remLength, key, strlen(key));
	if (!keyPos) return -1;
	int i = (int)strlen(key);
	while( ( i< remLength) && (keyPos[i] != 0x0A) ) {
		if( keyPos[i] >= '0' && keyPos[i] <= '9' )
			ret = (10 * ret) + keyPos[i] - '0';
		i++;
	}
	return ret;
} //readKeyN1() //return -1 if key not found

int readKey(const char * key,  char * buffer, int remLength) { //look for text key in binary data stream, return subsequent integer value
	int ret = 0;
	char *keyPos = (char *)memmem(buffer, remLength, key, strlen(key));
	if (!keyPos) return 0;
	int i = (int)strlen(key);
	while( ( i< remLength) && (keyPos[i] != 0x0A) ) {
		if( keyPos[i] >= '0' && keyPos[i] <= '9' )
			ret = (10 * ret) + keyPos[i] - '0';
		i++;
	}
	return ret;
} //readKey()

float readKeyFloatNan(const char * key,  char * buffer, int remLength) { //look for text key in binary data stream, return subsequent integer value
	char *keyPos = (char *)memmem(buffer, remLength, key, strlen(key));
	if (!keyPos) return NAN;
	char str[kDICOMStr];
	strcpy(str, "");
	char tmpstr[2];
  	tmpstr[1] = 0;
	int i = (int)strlen(key);
	while( ( i< remLength) && (keyPos[i] != 0x0A) ) {
		if( (keyPos[i] >= '0' && keyPos[i] <= '9') || (keyPos[i] <= '.') || (keyPos[i] <= '-') ) {
			tmpstr[0] = keyPos[i];
			strcat (str, tmpstr);
		}
		i++;
	}
	if (strlen(str) < 1) return NAN;
	return atof(str);
} //readKeyFloatNan()

float readKeyFloat(const char * key,  char * buffer, int remLength) { //look for text key in binary data stream, return subsequent integer value
	char *keyPos = (char *)memmem(buffer, remLength, key, strlen(key));
	if (!keyPos) return 0.0;
	char str[kDICOMStr];
	strcpy(str, "");
	char tmpstr[2];
  	tmpstr[1] = 0;
	int i = (int)strlen(key);
	while( ( i< remLength) && (keyPos[i] != 0x0A) ) {
		if( (keyPos[i] >= '0' && keyPos[i] <= '9') || (keyPos[i] <= '.') || (keyPos[i] <= '-') ) {
			tmpstr[0] = keyPos[i];
			strcat (str, tmpstr);
		}
		i++;
	}
	if (strlen(str) < 1) return 0.0;
	return atof(str);
} //readKeyFloat()

void readKeyStr(const char * key,  char * buffer, int remLength, char* outStr) {
//if key is CoilElementID.tCoilID the string 'CoilElementID.tCoilID	 = 	""Head_32""' returns 'Head32'
	strcpy(outStr, "");
	char *keyPos = (char *)memmem(buffer, remLength, key, strlen(key));
	if (!keyPos) return;
	int i = (int)strlen(key);
	int outLen = 0;
	char tmpstr[2];
  	tmpstr[1] = 0;
  	bool isQuote = false;
	while( ( i < remLength) && (keyPos[i] != 0x0A) ) {
		if ((isQuote) && (keyPos[i] != '"') && (outLen < kDICOMStrLarge)) {
			tmpstr[0] = keyPos[i];
			strcat (outStr, tmpstr);
  			outLen ++;
		}
		if (keyPos[i] == '"') {
			if (outLen > 0) break;
			isQuote = true;
		}
		i++;
	}
} //readKeyStr()

int phoenixOffsetCSASeriesHeader(unsigned char *buff, int lLength) {
    //returns offset to ASCII Phoenix data
    if (lLength < 36) return 0;
    if ((buff[0] != 'S') || (buff[1] != 'V') || (buff[2] != '1') || (buff[3] != '0') ) return EXIT_FAILURE;
    int lPos = 8; //skip 8 bytes of data, 'SV10' plus  2 32-bit values unused1 and unused2
    int lnTag = buff[lPos]+(buff[lPos+1]<<8)+(buff[lPos+2]<<16)+(buff[lPos+3]<<24);
    if ((buff[lPos+4] != 77) || (lnTag < 1)) return 0;
    lPos += 8; //skip 8 bytes of data, 32-bit lnTag plus 77 00 00 0
    TCSAtag tagCSA;
    TCSAitem itemCSA;
    for (int lT = 1; lT <= lnTag; lT++) {
        memcpy(&tagCSA, &buff[lPos], sizeof(tagCSA)); //read tag
        if (!littleEndianPlatform())
            nifti_swap_4bytes(1, &tagCSA.nitems);
        //printf("%d CSA of %s %d\n",lPos, tagCSA.name, tagCSA.nitems);
        lPos +=sizeof(tagCSA);
        if (strcmp(tagCSA.name, "MrPhoenixProtocol") == 0)
        	return lPos;
        for (int lI = 1; lI <= tagCSA.nitems; lI++) {
                memcpy(&itemCSA, &buff[lPos], sizeof(itemCSA));
                lPos +=sizeof(itemCSA);
                if (!littleEndianPlatform())
                    nifti_swap_4bytes(1, &itemCSA.xx2_Len);
                lPos += ((itemCSA.xx2_Len +3)/4)*4;
        }
    }
    return 0;
} // phoeechoSpacingnixOffsetCSASeriesHeader()

#define kMaxWipFree 64
typedef struct {
	float delayTimeInTR, phaseOversampling, phaseResolution, txRefAmp;
    int phaseEncodingLines, existUcImageNumb, ucMode, baseResolution, interp, partialFourier,echoSpacing,
    	difBipolar, parallelReductionFactorInPlane, refLinesPE;
    float alFree[kMaxWipFree] ;
    float adFree[kMaxWipFree];
    float alTI[kMaxWipFree];
    float dThickness, ulShape, sPositionDTra, sNormalDTra;
} TCsaAscii;

void siemensCsaAscii(const char * filename, TCsaAscii* csaAscii, int csaOffset, int csaLength, float* shimSetting, char* coilID, char* consistencyInfo, char* coilElements, char* pulseSequenceDetails, char* fmriExternalInfo, char* protocolName, char* wipMemBlock) {
 //reads ASCII portion of CSASeriesHeaderInfo and returns lEchoTrainDuration or lEchoSpacing value
 // returns 0 if no value found
    csaAscii->delayTimeInTR = -0.001;
 	csaAscii->phaseOversampling = 0.0;
 	csaAscii->phaseResolution = 0.0;
 	csaAscii->txRefAmp = 0.0;
 	csaAscii->phaseEncodingLines = 0;
 	csaAscii->existUcImageNumb = 0;
 	csaAscii->ucMode = -1;
 	csaAscii->baseResolution = 0;
 	csaAscii->interp = 0;
 	csaAscii->partialFourier = 0;
 	csaAscii->echoSpacing = 0;
 	csaAscii->difBipolar = 0; //0=not assigned,1=bipolar,2=monopolar
 	csaAscii->parallelReductionFactorInPlane = 0;
 	csaAscii->refLinesPE = 0;
 	for (int i = 0; i < 8; i++)
 		shimSetting[i] = 0.0;
 	strcpy(coilID, "");
 	strcpy(consistencyInfo, "");
 	strcpy(coilElements, "");
 	strcpy(pulseSequenceDetails, "");
 	strcpy(fmriExternalInfo, "");
 	strcpy(wipMemBlock, "");
 	strcpy(protocolName, "");
 	if ((csaOffset < 0) || (csaLength < 8)) return;
	FILE * pFile = fopen ( filename, "rb" );
	if(pFile==NULL) return;
	fseek (pFile , 0 , SEEK_END);
	long lSize = ftell (pFile);
	if (lSize < (csaOffset+csaLength)) {
		fclose (pFile);
		return;
	}
	fseek(pFile, csaOffset, SEEK_SET);
	char * buffer = (char*) malloc (csaLength);
	if(buffer == NULL) return;
	size_t result = fread (buffer,1,csaLength,pFile);
	if ((int)result != csaLength) return;
	//next bit complicated: restrict to ASCII portion to avoid buffer overflow errors in BINARY portion
	int startAscii = phoenixOffsetCSASeriesHeader((unsigned char *)buffer, csaLength);
	int csaLengthTrim = csaLength;
	char * bufferTrim = buffer;
	if ((startAscii > 0) && (startAscii < csaLengthTrim) ) { //ignore binary data at start
		bufferTrim += startAscii;
		csaLengthTrim -= startAscii;
	}
	char keyStr[] = "### ASCCONV BEGIN"; //skip to start of ASCII often "### ASCCONV BEGIN ###" but also "### ASCCONV BEGIN object=MrProtDataImpl@MrProtocolData"
	char *keyPos = (char *)memmem(bufferTrim, csaLengthTrim, keyStr, strlen(keyStr));
	if (keyPos) {
		//We could detect multi-echo MPRAGE here, e.g. "lContrasts	 = 	4"- but ideally we want an earlier detection
		csaLengthTrim -= (keyPos-bufferTrim);
		//FmriExternalInfo listed AFTER AscConvEnd and uses different delimiter ||
		// char keyStrExt[] = "FmriExternalInfo";
		// readKeyStr(keyStrExt,  keyPos, csaLengthTrim, fmriExternalInfo);
		#define myCropAtAscConvEnd
		#ifdef myCropAtAscConvEnd
		char keyStrEnd[] = "### ASCCONV END";
		char *keyPosEnd = (char *)memmem(keyPos, csaLengthTrim, keyStrEnd, strlen(keyStrEnd));
		if ((keyPosEnd) && ((keyPosEnd - keyPos) < csaLengthTrim)) //ignore binary data at end
			csaLengthTrim = (int)(keyPosEnd - keyPos);
		#endif
		char keyStrLns[] = "sKSpace.lPhaseEncodingLines";
		csaAscii->phaseEncodingLines = readKey(keyStrLns, keyPos, csaLengthTrim);
		char keyStrUcImg[] = "sSliceArray.ucImageNumb";
		csaAscii->existUcImageNumb = readKey(keyStrUcImg, keyPos, csaLengthTrim);
		char keyStrUcMode[] = "sSliceArray.ucMode";
		csaAscii->ucMode = readKeyN1(keyStrUcMode, keyPos, csaLengthTrim);
		char keyStrBase[] = "sKSpace.lBaseResolution";
		csaAscii->baseResolution = readKey(keyStrBase, keyPos, csaLengthTrim);
		char keyStrInterp[] = "sKSpace.uc2DInterpolation";
		csaAscii->interp = readKey(keyStrInterp, keyPos, csaLengthTrim);
		char keyStrPF[] = "sKSpace.ucPhasePartialFourier";
		csaAscii->partialFourier = readKey(keyStrPF, keyPos, csaLengthTrim);
		char keyStrES[] = "sFastImaging.lEchoSpacing";
		csaAscii->echoSpacing  = readKey(keyStrES, keyPos, csaLengthTrim);
		char keyStrDS[] = "sDiffusion.dsScheme";
		csaAscii->difBipolar = readKey(keyStrDS, keyPos, csaLengthTrim);
		if (csaAscii->difBipolar == 0) {
			char keyStrROM[] = "ucReadOutMode";
			csaAscii->difBipolar = readKey(keyStrROM, keyPos, csaLengthTrim);
			if ((csaAscii->difBipolar >= 1) && (csaAscii->difBipolar <= 2)) { //E11C Siemens/CMRR dsScheme: 1=bipolar, 2=unipolar, B17 CMRR ucReadOutMode 0x1=monopolar, 0x2=bipolar
				csaAscii->difBipolar = 3 - csaAscii->difBipolar;
			} //https://github.com/poldracklab/fmriprep/pull/1359#issuecomment-448379329
		}
		char keyStrAF[] = "sPat.lAccelFactPE";
		csaAscii->parallelReductionFactorInPlane = readKey(keyStrAF, keyPos, csaLengthTrim);
		char keyStrRef[] = "sPat.lRefLinesPE";
		csaAscii->refLinesPE = readKey(keyStrRef, keyPos, csaLengthTrim);


		//char keyStrETD[] = "sFastImaging.lEchoTrainDuration";
		//*echoTrainDuration = readKey(keyStrETD, keyPos, csaLengthTrim);
		//char keyStrEF[] = "sFastImaging.lEPIFactor";
		//ret = readKey(keyStrEF, keyPos, csaLengthTrim);
		char keyStrCoil[] = "sCoilElementID.tCoilID";
		readKeyStr(keyStrCoil,  keyPos, csaLengthTrim, coilID);
		char keyStrCI[] = "sProtConsistencyInfo.tMeasuredBaselineString";
		readKeyStr(keyStrCI,  keyPos, csaLengthTrim, consistencyInfo);
		char keyStrCS[] = "sCoilSelectMeas.sCoilStringForConversion";
		readKeyStr(keyStrCS,  keyPos, csaLengthTrim, coilElements);
		char keyStrSeq[] = "tSequenceFileName";
		readKeyStr(keyStrSeq,  keyPos, csaLengthTrim, pulseSequenceDetails);
		char keyStrWipMemBlock[] = "sWipMemBlock.tFree";
		readKeyStr(keyStrWipMemBlock,  keyPos, csaLengthTrim, wipMemBlock);
		char keyStrPn[] = "tProtocolName";
		readKeyStr(keyStrPn,  keyPos, csaLengthTrim, protocolName);
		//read ALL alTI[*] values
		for (int k = 0; k < kMaxWipFree; k++)
			csaAscii->alTI[k] = NAN;
		char keyStrTiFree[] = "alTI[";
		//check if ANY csaAscii.alFree tags exist
		char *keyPosTi = (char *)memmem(keyPos, csaLengthTrim, keyStrTiFree, strlen(keyStrTiFree));
		if (keyPosTi) {
			for (int k = 0; k < kMaxWipFree; k++) {
				char txt[1024] = {""};
				sprintf(txt, "%s%d]", keyStrTiFree,k);
				csaAscii->alTI[k] = readKeyFloatNan(txt, keyPos, csaLengthTrim);
			}
		}
		//read ALL csaAscii.alFree[*] values
		for (int k = 0; k < kMaxWipFree; k++)
			csaAscii->alFree[k] = 0.0;
		char keyStrAlFree[] = "sWipMemBlock.alFree[";
		//check if ANY csaAscii.alFree tags exist
		char *keyPosFree = (char *)memmem(keyPos, csaLengthTrim, keyStrAlFree, strlen(keyStrAlFree));
		if (keyPosFree) {
			for (int k = 0; k < kMaxWipFree; k++) {
				char txt[1024] = {""};
				sprintf(txt, "%s%d]", keyStrAlFree,k);
				csaAscii->alFree[k] = readKeyFloat(txt, keyPos, csaLengthTrim);
			}
		}
		//read ALL csaAscii.adFree[*] values
		for (int k = 0; k < kMaxWipFree; k++)
			csaAscii->adFree[k] = NAN;
		char keyStrAdFree[50];
		strcpy(keyStrAdFree, "sWipMemBlock.adFree[");
		//char keyStrAdFree[] = "sWipMemBlock.adFree[";
		//check if ANY csaAscii.adFree tags exist
		keyPosFree = (char *)memmem(keyPos, csaLengthTrim, keyStrAdFree, strlen(keyStrAdFree));
		if (!keyPosFree) { //"Wip" -> "WiP", modern -> old Siemens
			strcpy(keyStrAdFree, "sWiPMemBlock.adFree[");
			keyPosFree = (char *)memmem(keyPos, csaLengthTrim, keyStrAdFree, strlen(keyStrAdFree));
		}
		if (keyPosFree) {
			for (int k = 0; k < kMaxWipFree; k++) {
				char txt[1024] = {""};
				sprintf(txt, "%s%d]", keyStrAdFree,k);
				csaAscii->adFree[k] = readKeyFloatNan(txt, keyPos, csaLengthTrim);
			}
		}
		//read labelling plane
		char keyStrDThickness[] = "sRSatArray.asElm[1].dThickness";
		csaAscii->dThickness = readKeyFloat(keyStrDThickness, keyPos, csaLengthTrim);
		if (csaAscii->dThickness > 0.0) {
			char keyStrUlShape[] = "sRSatArray.asElm[1].ulShape";
			csaAscii->ulShape = readKeyFloat(keyStrUlShape, keyPos, csaLengthTrim);
			char keyStrSPositionDTra[] = "sRSatArray.asElm[1].sPosition.dTra";
			csaAscii->sPositionDTra = readKeyFloat(keyStrSPositionDTra, keyPos, csaLengthTrim);
			char keyStrSNormalDTra[] = "sRSatArray.asElm[1].sNormal.dTra";
			csaAscii->sNormalDTra = readKeyFloat(keyStrSNormalDTra, keyPos, csaLengthTrim);
		}
		//read delay time
		char keyStrDelay[] = "lDelayTimeInTR";
		csaAscii->delayTimeInTR = readKeyFloat(keyStrDelay, keyPos, csaLengthTrim);
		char keyStrOver[] = "sKSpace.dPhaseOversamplingForDialog";
		csaAscii->phaseOversampling = readKeyFloat(keyStrOver, keyPos, csaLengthTrim);
		char keyStrPhase[] = "sKSpace.dPhaseResolution";
		csaAscii->phaseResolution = readKeyFloat(keyStrPhase, keyPos, csaLengthTrim);
		char keyStrAmp[] = "sTXSPEC.asNucleusInfo[0].flReferenceAmplitude";
		csaAscii->txRefAmp = readKeyFloat(keyStrAmp, keyPos, csaLengthTrim);
		//lower order shims: newer sequences
		char keyStrSh0[] = "sGRADSPEC.asGPAData[0].lOffsetX";
		shimSetting[0] = readKeyFloat(keyStrSh0, keyPos, csaLengthTrim);
		char keyStrSh1[] = "sGRADSPEC.asGPAData[0].lOffsetY";
		shimSetting[1] = readKeyFloat(keyStrSh1, keyPos, csaLengthTrim);
		char keyStrSh2[] = "sGRADSPEC.asGPAData[0].lOffsetZ";
		shimSetting[2] = readKeyFloat(keyStrSh2, keyPos, csaLengthTrim);
		//lower order shims: older sequences
		char keyStrSh0s[] = "sGRADSPEC.lOffsetX";
		if (shimSetting[0] == 0.0) shimSetting[0] = readKeyFloat(keyStrSh0s, keyPos, csaLengthTrim);
		char keyStrSh1s[] = "sGRADSPEC.lOffsetY";
		if (shimSetting[1] == 0.0) shimSetting[1] = readKeyFloat(keyStrSh1s, keyPos, csaLengthTrim);
		char keyStrSh2s[] = "sGRADSPEC.lOffsetZ";
		if (shimSetting[2] == 0.0) shimSetting[2] = readKeyFloat(keyStrSh2s, keyPos, csaLengthTrim);
		//higher order shims: older sequences
		char keyStrSh3[] = "sGRADSPEC.alShimCurrent[0]";
		shimSetting[3] = readKeyFloat(keyStrSh3, keyPos, csaLengthTrim);
		char keyStrSh4[] = "sGRADSPEC.alShimCurrent[1]";
		shimSetting[4] = readKeyFloat(keyStrSh4, keyPos, csaLengthTrim);
		char keyStrSh5[] = "sGRADSPEC.alShimCurrent[2]";
		shimSetting[5] = readKeyFloat(keyStrSh5, keyPos, csaLengthTrim);
		char keyStrSh6[] = "sGRADSPEC.alShimCurrent[3]";
		shimSetting[6] = readKeyFloat(keyStrSh6, keyPos, csaLengthTrim);
		char keyStrSh7[] = "sGRADSPEC.alShimCurrent[4]";
		shimSetting[7] = readKeyFloat(keyStrSh7, keyPos, csaLengthTrim);
	}
	fclose (pFile);
	free (buffer);
	return;
} // siemensCsaAscii()

#endif //myReadAsciiCsa()

#ifndef myDisableZLib
 //Uncomment next line to decode GE Protocol Data Block, for caveats see https://github.com/rordenlab/dcm2niix/issues/163
 #define myReadGeProtocolBlock
#endif
#ifdef myReadGeProtocolBlock
int  geProtocolBlock(const char * filename,  int geOffset, int geLength, int isVerbose, int* sliceOrder, int* viewOrder, int* mbAccel) {
	*sliceOrder = -1;
	*viewOrder = 0;
	int ret = EXIT_FAILURE;
 	if ((geOffset < 0) || (geLength < 20)) return ret;
	FILE * pFile = fopen ( filename, "rb" );
	if(pFile==NULL) return ret;
	fseek (pFile , 0 , SEEK_END);
	long lSize = ftell (pFile);
	if (lSize < (geOffset+geLength)) {
		fclose (pFile);
		return ret;
	}
	fseek(pFile, geOffset, SEEK_SET);
	uint8_t * pCmp = (uint8_t*) malloc (geLength); //uint8_t -> mz_uint8
	if(pCmp == NULL) return ret;
	size_t result = fread (pCmp,1,geLength,pFile);
	if ((int)result != geLength) return ret;
	int cmpSz = geLength;
	//http://www.forensicswiki.org/wiki/Gzip
	// always little endia! http://www.onicos.com/staff/iz/formats/gzip.html
	if (cmpSz < 20) return ret;
	if ((pCmp[0] != 31) || (pCmp[1] != 139) || (pCmp[2] != 8)) return ret; //check signature and deflate algorithm
	uint8_t  flags = pCmp[3];
	bool isFNAME = ((flags & 0x08) == 0x08);
	bool isFCOMMENT = ((flags & 0x10) == 0x10);
	uint32_t hdrSz = 10;
	if (isFNAME) {//skip null-terminated string FNAME
		for (hdrSz = hdrSz; hdrSz < cmpSz; hdrSz++)
			if (pCmp[hdrSz] == 0) break;
		hdrSz++;
	}
	if (isFCOMMENT) {//skip null-terminated string COMMENT
		for (hdrSz = hdrSz; hdrSz < cmpSz; hdrSz++)
			if (pCmp[hdrSz] == 0) break;
		hdrSz++;
	}
	uint32_t unCmpSz = ((uint32_t)pCmp[cmpSz-4])+((uint32_t)pCmp[cmpSz-3] << 8)+((uint32_t)pCmp[cmpSz-2] << 16)+((uint32_t)pCmp[cmpSz-1] << 24);
	//printf(">> %d %d %zu %zu %zu\n", isFNAME, isFCOMMENT, cmpSz, unCmpSz, hdrSz);
	z_stream s;
	memset (&s, 0, sizeof (z_stream));
	#ifdef myDisableMiniZ
    #define MZ_DEFAULT_WINDOW_BITS 15 // Window bits
	#endif
	inflateInit2(&s, -MZ_DEFAULT_WINDOW_BITS);
	uint8_t *pUnCmp = (uint8_t *)malloc((size_t)unCmpSz);
	s.avail_out = unCmpSz;
	s.next_in = pCmp+ hdrSz;
	s.avail_in = cmpSz-hdrSz-8;
	s.next_out = (uint8_t *) pUnCmp;
	#ifdef myDisableMiniZ
	ret = inflate(&s, Z_SYNC_FLUSH);
	if (ret != Z_STREAM_END) {
		free(pUnCmp);
		return EXIT_FAILURE;
	}
	#else
	ret = mz_inflate(&s, MZ_SYNC_FLUSH);
	if (ret != MZ_STREAM_END) {
		free(pUnCmp);
		return EXIT_FAILURE;
	}
	#endif
	//https://groups.google.com/forum/#!msg/comp.protocols.dicom/mxnCkv8A-i4/W_uc6SxLwHQJ
	// DISCOVERY MR750 / 24\MX\MR Software release:DV24.0_R01_1344.a) are now storing an XML file
	//   <?xml version="1.0" encoding="UTF-8"?>
	if ((pUnCmp[0] == '<') &&  (pUnCmp[1] == '?'))
		printWarning("New XML-based GE Protocol Block is not yet supported: please report issue on dcm2niix Github page\n");
	char keyStrSO[] = "SLICEORDER";
	*sliceOrder  = readKeyN1(keyStrSO, (char *) pUnCmp, unCmpSz);
	char keyStrVO[] = "VIEWORDER"; 
	*viewOrder  = readKey(keyStrVO, (char *) pUnCmp, unCmpSz);
	char keyStrMB[] = "MBACCEL";
	*mbAccel  = readKey(keyStrMB, (char *) pUnCmp, unCmpSz);
	if (isVerbose > 1) {
		printMessage("GE Protocol Block %s bytes %d compressed, %d uncompressed @ %d\n", filename, geLength, unCmpSz, geOffset);
		printMessage(" ViewOrder %d SliceOrder %d\n", *viewOrder, *sliceOrder);
		printMessage("%s\n", pUnCmp);
	}
	free(pUnCmp);
	return EXIT_SUCCESS;
}
#endif //myReadGeProtocolBlock()

void json_Str(FILE *fp, const char *sLabel, char *sVal) {
	if (strlen(sVal) < 1) return;
	//fprintf(fp, sLabel, sVal );
	//convert  \ ' " characters to _ see https://github.com/rordenlab/dcm2niix/issues/131
	for (size_t pos = 0; pos < strlen(sVal); pos ++) {
        if ((sVal[pos] == '\'') || (sVal[pos] == '"') || (sVal[pos] == '\\'))
            sVal[pos] = '_';
    }
	fprintf(fp, sLabel, sVal );
	/*char outname[PATH_MAX] = {""};
	char appendChar[2] = {"\\"};
	char passChar[2] = {"\\"};
	for (int pos = 0; pos<strlen(sVal); pos ++) {
        if ((sVal[pos] == '\'') || (sVal[pos] == '"') || (sVal[pos] == '\\'))
            strcpy(outname, appendChar);
        passChar[0] = sVal[pos];
        strcpy(outname, passChar);
    }
	fprintf(fp, sLabel, outname );*/
} //json_Str

void json_FloatNotNan(FILE *fp, const char *sLabel, float sVal) {
	if (isnan(sVal)) return;
	fprintf(fp, sLabel, sVal );
} //json_Float

void print_FloatNotNan(const char *sLabel, int iVal, float sVal) {
	if (isnan(sVal)) return;
	printMessage(sLabel, iVal, sVal);
} //json_Float

void json_Float(FILE *fp, const char *sLabel, float sVal) {
	if (sVal <= 0.0) return;
	fprintf(fp, sLabel, sVal );
} //json_Float

void rescueProtocolName(struct TDICOMdata *d, const char * filename) {
	//tools like gdcmanon strip protocol name (0018,1030) but for Siemens we can recover it from CSASeriesHeaderInfo (0029,1020)
	if ((d->manufacturer != kMANUFACTURER_SIEMENS) || (d->CSA.SeriesHeader_offset < 1) || (d->CSA.SeriesHeader_length < 1)) return;
	if (strlen(d->protocolName) > 0) return;
#ifdef myReadAsciiCsa
	float shimSetting[8];
	char protocolName[kDICOMStrLarge], fmriExternalInfo[kDICOMStrLarge], coilID[kDICOMStrLarge], consistencyInfo[kDICOMStrLarge], coilElements[kDICOMStrLarge], pulseSequenceDetails[kDICOMStrLarge], wipMemBlock[kDICOMStrLarge];
	TCsaAscii csaAscii;
	siemensCsaAscii(filename, &csaAscii, d->CSA.SeriesHeader_offset, d->CSA.SeriesHeader_length, shimSetting, coilID, consistencyInfo, coilElements, pulseSequenceDetails, fmriExternalInfo, protocolName, wipMemBlock);
	strcpy(d->protocolName, protocolName);
#endif
}

void nii_SaveBIDS(char pathoutname[], struct TDICOMdata d, struct TDCMopts opts, struct nifti_1_header *h, const char * filename) {
//https://docs.google.com/document/d/1HFUkAEE-pB-angVcYe6pf_-fVf4sCpOHKesUvfb8Grc/edit#
// Generate Brain Imaging Data Structure (BIDS) info
// sidecar JSON file (with the same  filename as the .nii.gz file, but with .json extension).
// we will use %g for floats since exponents are allowed
// we will not set the locale, so decimal separator is always a period, as required
//  https://www.ietf.org/rfc/rfc4627.txt
	if ((!opts.isCreateBIDS) && (opts.isOnlyBIDS)) printMessage("Input-only mode: no BIDS/NIfTI output generated for '%s'\n", pathoutname);
	if (!opts.isCreateBIDS) return;
	char txtname[2048] = {""};
	strcpy (txtname,pathoutname);
	strcat (txtname,".json");
	FILE *fp = fopen(txtname, "w");
	fprintf(fp, "{\n");
	switch (d.modality) {
		case kMODALITY_CR:
			fprintf(fp, "\t\"Modality\": \"CR\",\n" );
			break;
		case kMODALITY_CT:
			fprintf(fp, "\t\"Modality\": \"CT\",\n" );
			break;
		case kMODALITY_MR:
			fprintf(fp, "\t\"Modality\": \"MR\",\n" );
			break;
		case kMODALITY_PT:
			fprintf(fp, "\t\"Modality\": \"PT\",\n" );
			break;
		case kMODALITY_US:
			fprintf(fp, "\t\"Modality\": \"US\",\n" );
			break;
	};
	//attempt to determine BIDS sequence type
/*(0018,0024) SequenceName
ep_b: dwi
epfid2d: perf
epfid2d: bold
epfid3d1_15: swi
epse2d: dwi (when b-vals specified)
epse2d: fmap (spin echo, e.g. TOPUP, nb could also be extra B=0 for DWI sequence)
fl2d: localizer
fl3d1r_t: angio
fl3d1r_tm: angio
fl3d1r: angio
fl3d1r: swi
fl3d1r: ToF
fm2d: fmap (gradient echo, e.g. FUGUE)
spc3d: T2
spcir: flair (dark fluid)
spcR: PD
tfl3d: T1
tfl_me3d5_16ns: T1 (ME-MPRAGE)
tir2d: flair
tse2d: PD
tse2d: T2
tse3d: T2*/
	/*
	if (d.manufacturer == kMANUFACTURER_SIEMENS) {
		#define kLabel_UNKNOWN  0
		#define kLabel_T1w  1
		#define kLabel_T2w  2
		#define kLabel_bold  3
		#define kLabel_perf  4
		#define kLabel_dwi  5
		#define kLabel_fieldmap  6
		int iLabel = kLabel_UNKNOWN;
		if (d.CSA.numDti > 1) iLabel = kLabel_dwi;
		//if ((iLabel == kLabel_UNKNOWN) && (d.is2DAcq))
		//if ((iLabel == kLabel_UNKNOWN) && (d.is3DAcq))
		if (iLabel != kLabel_UNKNOWN) {
			char tLabel[20] = {""};
			if (iLabel == kLabel_dwi) strcat (tLabel,"dwi");
			json_Str(fp, "\t\"ModalityLabel\": \"%s\",\n", tLabel);
		}
	}*/
	//report vendor
	if (d.fieldStrength > 0.0) fprintf(fp, "\t\"MagneticFieldStrength\": %g,\n", d.fieldStrength );
	//Imaging Frequency (0018,0084) can be useful https://support.brainvoyager.com/brainvoyager/functional-analysis-preparation/29-pre-processing/78-epi-distortion-correction-echo-spacing-and-bandwidth
	// however, UIH stores 128176031 not 128.176031 https://github.com/rordenlab/dcm2niix/issues/225
	if (d.imagingFrequency < 9000000)
		json_Float(fp, "\t\"ImagingFrequency\": %g,\n", d.imagingFrequency);
	switch (d.manufacturer) {
		case kMANUFACTURER_BRUKER:
			fprintf(fp, "\t\"Manufacturer\": \"Bruker\",\n" );
			break;
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
		case kMANUFACTURER_HITACHI:
			fprintf(fp, "\t\"Manufacturer\": \"Hitachi\",\n" );
			break;
		case kMANUFACTURER_UIH:
			fprintf(fp, "\t\"Manufacturer\": \"UIH\",\n" );
			break;
	};
	json_Str(fp, "\t\"ManufacturersModelName\": \"%s\",\n", d.manufacturersModelName);
	json_Str(fp, "\t\"InstitutionName\": \"%s\",\n", d.institutionName);
	json_Str(fp, "\t\"InstitutionalDepartmentName\": \"%s\",\n", d.institutionalDepartmentName);
	json_Str(fp, "\t\"InstitutionAddress\": \"%s\",\n", d.institutionAddress);
	json_Str(fp, "\t\"DeviceSerialNumber\": \"%s\",\n", d.deviceSerialNumber );
	json_Str(fp, "\t\"StationName\": \"%s\",\n", d.stationName );
	if (!opts.isAnonymizeBIDS) {
		json_Str(fp, "\t\"SeriesInstanceUID\": \"%s\",\n", d.seriesInstanceUID);
		json_Str(fp, "\t\"StudyInstanceUID\": \"%s\",\n", d.studyInstanceUID);
		json_Str(fp, "\t\"ReferringPhysicianName\": \"%s\",\n", d.referringPhysicianName);
		json_Str(fp, "\t\"StudyID\": \"%s\",\n", d.studyID);
		//Next lines directly reveal patient identity
		json_Str(fp, "\t\"PatientName\": \"%s\",\n", d.patientName);
		json_Str(fp, "\t\"PatientID\": \"%s\",\n", d.patientID);
		json_Str(fp, "\t\"AccessionNumber\": \"%s\",\n", d.accessionNumber);
		if (strlen(d.patientBirthDate) == 8) { //DICOM DA YYYYMMDD -> ISO 8601 "YYYY-MM-DD"
			int ayear,amonth,aday;
			sscanf(d.patientBirthDate, "%4d%2d%2d", &ayear, &amonth, &aday);
			fprintf(fp, "\t\"PatientBirthDate\": ");
			fprintf(fp, (ayear >= 0 && ayear <= 9999) ? "\"%4d" : "\"%+4d", ayear);
			fprintf(fp, "-%02d-%02d\",\n", amonth, aday);
		}
		if (d.patientSex != '?') fprintf(fp, "\t\"PatientSex\": \"%c\",\n",  d.patientSex);
		json_Float(fp, "\t\"PatientWeight\": %g,\n", d.patientWeight);
		//d.patientBirthDate //convert from DICOM  YYYYMMDD to JSON
		//d.patientAge //4-digit Age String: nnnD, nnnW, nnnM, nnnY;
	}
	json_Str(fp, "\t\"BodyPartExamined\": \"%s\",\n", d.bodyPartExamined);
	json_Str(fp, "\t\"PatientPosition\": \"%s\",\n", d.patientOrient);  // 0018,5100 = PatientPosition in DICOM
	json_Str(fp, "\t\"ProcedureStepDescription\": \"%s\",\n", d.procedureStepDescription);
	json_Str(fp, "\t\"SoftwareVersions\": \"%s\",\n", d.softwareVersions);
	//json_Str(fp, "\t\"MRAcquisitionType\": \"%s\",\n", d.mrAcquisitionType);
	if (d.is2DAcq)  fprintf(fp, "\t\"MRAcquisitionType\": \"2D\",\n");
	if (d.is3DAcq)  fprintf(fp, "\t\"MRAcquisitionType\": \"3D\",\n");
	json_Str(fp, "\t\"SeriesDescription\": \"%s\",\n", d.seriesDescription);
	json_Str(fp, "\t\"ProtocolName\": \"%s\",\n", d.protocolName);
	json_Str(fp, "\t\"ScanningSequence\": \"%s\",\n", d.scanningSequence);
	json_Str(fp, "\t\"SequenceVariant\": \"%s\",\n", d.sequenceVariant);
	json_Str(fp, "\t\"ScanOptions\": \"%s\",\n", d.scanOptions);
	json_Str(fp, "\t\"SequenceName\": \"%s\",\n", d.sequenceName);
	if (strlen(d.imageType) > 0) {
		fprintf(fp, "\t\"ImageType\": [\"");
		bool isSep = false;
		for (size_t i = 0; i < strlen(d.imageType); i++) {
			if (d.imageType[i] != '_') {
				if (isSep)
		  			fprintf(fp, "\", \"");
				isSep = false;
				fprintf(fp, "%c", d.imageType[i]);
			} else
				isSep = true;
		}
		fprintf(fp, "\"],\n");
	}
	if (d.isDerived) //DICOM is derived image or non-spatial file (sounds, etc)
		fprintf(fp, "\t\"RawImage\": false,\n");
	if (d.seriesNum > 0) fprintf(fp, "\t\"SeriesNumber\": %ld,\n", d.seriesNum);
	//Chris Gorgolewski: BIDS standard specifies ISO8601 date-time format (Example: 2016-07-06T12:49:15.679688)
	//Lines below directly save DICOM values
	if (d.acquisitionTime > 0.0 && d.acquisitionDate > 0.0){
		long acquisitionDate = d.acquisitionDate;
		double acquisitionTime = d.acquisitionTime;
		char acqDateTimeBuf[64];
		//snprintf(acqDateTimeBuf, sizeof acqDateTimeBuf, "%+08ld%+08f", acquisitionDate, acquisitionTime);
		snprintf(acqDateTimeBuf, sizeof acqDateTimeBuf, "%+08ld%+013.5f", acquisitionDate, acquisitionTime); //CR 20170404 add zero pad so 1:23am appears as +012300.00000 not +12300.00000
		//printMessage("acquisitionDateTime %s\n",acqDateTimeBuf);
		int ayear,amonth,aday,ahour,amin;
		double asec;
		int count = 0;
		sscanf(acqDateTimeBuf, "%5d%2d%2d%3d%2d%lf%n", &ayear, &amonth, &aday, &ahour, &amin, &asec, &count);  //CR 20170404 %lf not %f for double precision
		//printf("-%02d-%02dT%02d:%02d:%02.6f\",\n", amonth, aday, ahour, amin, asec);
		if (count) { // ISO 8601 specifies a sign must exist for distant years.
			//report time of the day only format, https://www.cs.tut.fi/~jkorpela/iso8601.html
			fprintf(fp, "\t\"AcquisitionTime\": \"%02d:%02d:%02.6f\",\n",ahour, amin, asec);
			//report date and time together
			if (!opts.isAnonymizeBIDS) {
				fprintf(fp, "\t\"AcquisitionDateTime\": ");
				fprintf(fp, (ayear >= 0 && ayear <= 9999) ? "\"%4d" : "\"%+4d", ayear);
				fprintf(fp, "-%02d-%02dT%02d:%02d:%02.6f\",\n", amonth, aday, ahour, amin, asec);
			}
		} //if (count)
	} //if acquisitionTime and acquisitionDate recorded
	// if (d.acquisitionTime > 0.0) fprintf(fp, "\t\"AcquisitionTime\": %f,\n", d.acquisitionTime );
	// if (d.acquisitionDate > 0.0) fprintf(fp, "\t\"AcquisitionDate\": %8.0f,\n", d.acquisitionDate );
	if (d.acquNum > 0)
		fprintf(fp, "\t\"AcquisitionNumber\": %d,\n", d.acquNum);
	json_Str(fp, "\t\"ImageComments\": \"%s\",\n", d.imageComments);
	json_Str(fp, "\t\"ConversionComments\": \"%s\",\n", opts.imageComments);
	//if conditionals: the following values are required for DICOM MRI, but not available for CT
	json_Float(fp, "\t\"TriggerDelayTime\": %g,\n", d.triggerDelayTime );
	if (d.RWVScale != 0) {
		fprintf(fp, "\t\"PhilipsRWVSlope\": %g,\n", d.RWVScale );
		fprintf(fp, "\t\"PhilipsRWVIntercept\": %g,\n", d.RWVIntercept );
	}
	if ((d.intenScalePhilips != 0) && (d.manufacturer == kMANUFACTURER_PHILIPS)) { //for details, see PhilipsPrecise()
		fprintf(fp, "\t\"PhilipsRescaleSlope\": %g,\n", d.intenScale );
		fprintf(fp, "\t\"PhilipsRescaleIntercept\": %g,\n", d.intenIntercept );
		fprintf(fp, "\t\"PhilipsScaleSlope\": %g,\n", d.intenScalePhilips );
		fprintf(fp, "\t\"UsePhilipsFloatNotDisplayScaling\": %d,\n", opts.isPhilipsFloatNotDisplayScaling);
	}
	//PET ISOTOPE MODULE ATTRIBUTES
	json_Float(fp, "\t\"RadionuclidePositronFraction\": %g,\n", d.radionuclidePositronFraction );
	json_Float(fp, "\t\"RadionuclideTotalDose\": %g,\n", d.radionuclideTotalDose );
	json_Float(fp, "\t\"RadionuclideHalfLife\": %g,\n", d.radionuclideHalfLife );
	json_Float(fp, "\t\"DoseCalibrationFactor\": %g,\n", d.doseCalibrationFactor );
    json_Float(fp, "\t\"IsotopeHalfLife\": %g,\n", d.ecat_isotope_halflife);
    json_Float(fp, "\t\"Dosage\": %g,\n", d.ecat_dosage);
	//CT parameters
	if ((d.TE > 0.0) && (d.isXRay)) fprintf(fp, "\t\"XRayExposure\": %g,\n", d.TE );
    //MRI parameters
    if (!d.isXRay) { //with CT scans, slice thickness often varies
    	//beware, not used correctly by all vendors https://public.kitware.com/pipermail/insight-users/2005-September/014711.html
    	json_Float(fp, "\t\"SliceThickness\": %g,\n", d.zThick );
    	json_Float(fp, "\t\"SpacingBetweenSlices\": %g,\n", d.zSpacing);
    }
	json_Float(fp, "\t\"SAR\": %g,\n", d.SAR );
	if (d.numberOfAverages > 1.0) json_Float(fp, "\t\"NumberOfAverages\": %g,\n", d.numberOfAverages );
	if ((d.echoNum > 1) || (d.isMultiEcho)) fprintf(fp, "\t\"EchoNumber\": %d,\n", d.echoNum);
	if ((d.TE > 0.0) && (!d.isXRay)) fprintf(fp, "\t\"EchoTime\": %g,\n", d.TE / 1000.0 );
	//if ((d.TE2 > 0.0) && (!d.isXRay)) fprintf(fp, "\t\"EchoTime2\": %g,\n", d.TE2 / 1000.0 );
	json_Float(fp, "\t\"RepetitionTime\": %g,\n", d.TR / 1000.0 );
	json_Float(fp, "\t\"InversionTime\": %g,\n", d.TI / 1000.0 );
	json_Float(fp, "\t\"FlipAngle\": %g,\n", d.flipAngle );
	bool interp = false; //2D interpolation
	float phaseOversampling = 0.0;
	//n.b. https://neurostars.org/t/getting-missing-ge-information-required-by-bids-for-common-preprocessing/1357/7
	json_Str(fp, "\t\"PhaseEncodingDirectionDisplayed\": \"%s\",\n", d.phaseEncodingDirectionDisplayedUIH);
	if ((d.manufacturer == kMANUFACTURER_GE) && (d.phaseEncodingGE != kGE_PHASE_ENCODING_POLARITY_UNKNOWN)) { //only set for GE
		if (d.phaseEncodingGE == kGE_PHASE_ENCODING_POLARITY_UNFLIPPED) fprintf(fp, "\t\"PhaseEncodingPolarityGE\": \"Unflipped\",\n" );
		if (d.phaseEncodingGE == kGE_PHASE_ENCODING_POLARITY_FLIPPED) fprintf(fp, "\t\"PhaseEncodingPolarityGE\": \"Flipped\",\n" );
	}

	float delayTimeInTR = -0.01;
	#ifdef myReadAsciiCsa
	if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (d.CSA.SeriesHeader_offset > 0) && (d.CSA.SeriesHeader_length > 0)) {
		float pf = 1.0f; //partial fourier
		float shimSetting[8];

		char protocolName[kDICOMStrLarge], fmriExternalInfo[kDICOMStrLarge], coilID[kDICOMStrLarge], consistencyInfo[kDICOMStrLarge], coilElements[kDICOMStrLarge], pulseSequenceDetails[kDICOMStrLarge], wipMemBlock[kDICOMStrLarge];
		TCsaAscii csaAscii;
		siemensCsaAscii(filename, &csaAscii, d.CSA.SeriesHeader_offset, d.CSA.SeriesHeader_length, shimSetting, coilID, consistencyInfo, coilElements, pulseSequenceDetails, fmriExternalInfo, protocolName, wipMemBlock);
		if ((d.phaseEncodingLines < 1) && (csaAscii.phaseEncodingLines > 0))
			d.phaseEncodingLines = csaAscii.phaseEncodingLines;
		//if (d.phaseEncodingLines != csaAscii.phaseEncodingLines) //e.g. phaseOversampling
		//	printWarning("PhaseEncodingLines reported in DICOM (%d) header does not match value CSA-ASCII (%d) %s\n", d.phaseEncodingLines, csaAscii.phaseEncodingLines, pathoutname);
		delayTimeInTR = csaAscii.delayTimeInTR;
		phaseOversampling = csaAscii.phaseOversampling;
		if (csaAscii.existUcImageNumb > 0) {
			if (d.CSA.protocolSliceNumber1 < 2) {
				printWarning("Assuming mosaics saved in reverse order due to 'sSliceArray.ucImageNumb'\n");
				//never seen such an image in the wild.... sliceDir may need to be reversed
			}
			d.CSA.protocolSliceNumber1 = 2;
		}
		//ultra-verbose output for deciphering adFree/alFree/alTI values:
		/*
		if (opts.isVerbose > 1) {
			for (int i = 0; i < kMaxWipFree; i++)
				print_FloatNotNan("adFree[%d]=\t%g\n",i, csaAscii.adFree[i]);
			for (int i = 0; i < kMaxWipFree; i++)
				print_FloatNotNan("alFree[%d]=\t%g\n",i, csaAscii.alFree[i]);
			for (int i = 0; i < kMaxWipFree; i++)
				print_FloatNotNan("alTI[%d]=\t%g\n",i, csaAscii.alTI[i]);
		} //verbose
		*/
		//ASL specific tags - 2D pCASL Danny J.J. Wang http://www.loft-lab.org
		if ((strstr(pulseSequenceDetails,"ep2d_pcasl")) || (strstr(pulseSequenceDetails,"ep2d_pcasl_UI_PHC"))) {
			json_FloatNotNan(fp, "\t\"LabelOffset\": %g,\n", csaAscii.adFree[1]); //mm
			json_FloatNotNan(fp, "\t\"PostLabelDelay\": %g,\n", csaAscii.adFree[2] * (1.0/1000000.0)); //usec -> sec
			json_FloatNotNan(fp, "\t\"NumRFBlocks\": %g,\n", csaAscii.adFree[3]);
			json_FloatNotNan(fp, "\t\"RFGap\": %g,\n", csaAscii.adFree[4] * (1.0/1000000.0)); //usec -> sec
			json_FloatNotNan(fp, "\t\"MeanGzx10\": %g,\n", csaAscii.adFree[10]);  // mT/m
			json_FloatNotNan(fp, "\t\"PhiAdjust\": %g,\n", csaAscii.adFree[11]);  // percent
		}
		//ASL specific tags - 3D pCASL Danny J.J. Wang http://www.loft-lab.org
		if (strstr(pulseSequenceDetails,"tgse_pcasl")) {
			json_FloatNotNan(fp, "\t\"RFGap\": %g,\n", csaAscii.adFree[4] * (1.0/1000000.0)); //usec -> sec
			json_FloatNotNan(fp, "\t\"MeanGzx10\": %g,\n", csaAscii.adFree[10]);  // mT/m
			json_FloatNotNan(fp, "\t\"T1\": %g,\n", csaAscii.adFree[12] * (1.0/1000000.0)); //usec -> sec
		}
		//ASL specific tags - 2D PASL Siemens Product
		if (strstr(pulseSequenceDetails,"ep2d_pasl")) {
			json_FloatNotNan(fp, "\t\"InversionTime\": %g,\n", csaAscii.alTI[0] * (1.0/1000000.0)); //us -> sec
			json_FloatNotNan(fp, "\t\"SaturationStopTime\": %g,\n", csaAscii.alTI[2] * (1.0/1000000.0)); //us -> sec
		}
		//ASL specific tags - 3D PASL Siemens Product http://adni.loni.usc.edu/wp-content/uploads/2010/05/ADNI3_Basic_Siemens_Skyra_E11.pdf
		if (strstr(pulseSequenceDetails,"tgse_pasl")) {
			json_FloatNotNan(fp, "\t\"BolusDuration\": %g,\n", csaAscii.alTI[0] * (1.0/1000000.0)); //usec->sec
			json_FloatNotNan(fp, "\t\"InversionTime\": %g,\n", csaAscii.alTI[2] * (1.0/1000000.0)); //usec -> sec
			//json_FloatNotNan(fp, "\t\"SaturationStopTime\": %g,\n", csaAscii.alTI[2] * (1.0/1000.0));
		}
		//PASL http://www.pubmed.com/11746944 http://www.pubmed.com/21606572
		if (strstr(pulseSequenceDetails,"ep2d_fairest")) {
			json_FloatNotNan(fp, "\t\"PostInversionDelay\": %g,\n", csaAscii.adFree[2] * (1.0/1000.0)); //usec->sec
			json_FloatNotNan(fp, "\t\"PostLabelDelay\": %g,\n", csaAscii.adFree[4] * (1.0/1000.0)); //usec -> sec
		}
		//ASL specific tags - Oxford (Thomas OKell)
		bool isOxfordASL = false;
		if (strstr(pulseSequenceDetails,"to_ep2d_VEPCASL")) { //Oxford 2D pCASL
			isOxfordASL = true;
			json_FloatNotNan(fp, "\t\"InversionTime\": %g,\n", csaAscii.alTI[2] * (1.0/1000000.0)); //ms->sec
			json_FloatNotNan(fp, "\t\"BolusDuration\": %g,\n", csaAscii.alTI[0] * (1.0/1000000.0)); //usec -> sec
			//alTI[0]	 = 	700000
			//alTI[2]	 = 	1800000

			json_Float(fp, "\t\"TagRFFlipAngle\": %g,\n", csaAscii.alFree[4]);
			json_Float(fp, "\t\"TagRFDuration\": %g,\n", csaAscii.alFree[5]/1000000.0); //usec -> sec
			json_Float(fp, "\t\"TagRFSeparation\": %g,\n", csaAscii.alFree[6]/1000000.0); //usec -> sec
			json_FloatNotNan(fp, "\t\"MeanTagGradient\": %g,\n", csaAscii.adFree[0]); //mTm
			json_FloatNotNan(fp, "\t\"TagGradientAmplitude\": %g,\n", csaAscii.adFree[1]); //mTm
			json_Float(fp, "\t\"TagDuration\": %g,\n", csaAscii.alFree[9]/ 1000.0); //ms -> sec
			json_Float(fp, "\t\"MaximumT1Opt\": %g,\n", csaAscii.alFree[10]/ 1000.0); //ms -> sec
			//report post label delay

			int nPLD = 0;
			bool isValid = true; //detect gaps in PLD array: If user sets PLD1=250, PLD2=0 PLD3=375 only PLD1 was acquired
			for (int k = 11; k < 31; k++) {
				if ((isnan(csaAscii.alFree[k])) || (csaAscii.alFree[k] <= 0.0)) isValid = false;
				if (isValid) nPLD ++;
			} //for k
			if (nPLD > 0) { // record PostLabelDelays, these are listed as "PLD0","PLD1",etc in PDF
				fprintf(fp, "\t\"InitialPostLabelDelay\": [\n"); //https://docs.google.com/document/d/15tnn5F10KpgHypaQJNNGiNKsni9035GtDqJzWqkkP6c/edit#
				for (int i = 0; i < nPLD; i++) {
					if (i != 0)
						fprintf(fp, ",\n");
					fprintf(fp, "\t\t%g", csaAscii.alFree[i+11]/ 1000.0); //ms -> sec
				}
				fprintf(fp, "\t],\n");
			}
			/*isValid = true; //detect gaps in PLD array: If user sets PLD1=250, PLD2=0 PLD3=375 only PLD1 was acquired
			for (int k = 11; k < 31; k++) {
				if (isValid) {
					char newstr[256];
					sprintf(newstr, "\t\"PLD%d\": %%g,\n", k-11);
					json_Float(fp, newstr, csaAscii.alFree[k]/ 1000.0); //ms -> sec
					if (csaAscii.alFree[k] <= 0.0) isValid = false;
				}//isValid
			} //for k */
			for (int k = 3; k < 11; k++) { //vessel locations
				char newstr[256];
				sprintf(newstr, "\t\"sWipMemBlock.AdFree%d\": %%g,\n", k);
				json_FloatNotNan(fp, newstr, csaAscii.adFree[k]);
			}
		}
		if (strstr(pulseSequenceDetails,"jw_tgse_VEPCASL")) { //Oxford 3D pCASL
			isOxfordASL = true;
			json_Float(fp, "\t\"TagRFFlipAngle\": %g,\n", csaAscii.alFree[6]);
			json_Float(fp, "\t\"TagRFDuration\": %g,\n", csaAscii.alFree[7]/1000000.0); //usec -> sec
			json_Float(fp, "\t\"TagRFSeparation\": %g,\n", csaAscii.alFree[8]/1000000.0); //usec -> sec
			json_Float(fp, "\t\"MaximumT1Opt\": %g,\n", csaAscii.alFree[9]/1000.0); //ms -> sec
			json_Float(fp, "\t\"Tag0\": %g,\n", csaAscii.alFree[10]/1000.0); //DelayTimeInTR usec -> sec
			json_Float(fp, "\t\"Tag1\": %g,\n", csaAscii.alFree[11]/1000.0); //DelayTimeInTR usec -> sec
			json_Float(fp, "\t\"Tag2\": %g,\n", csaAscii.alFree[12]/1000.0); //DelayTimeInTR usec -> sec
			json_Float(fp, "\t\"Tag3\": %g,\n", csaAscii.alFree[13]/1000.0); //DelayTimeInTR usec -> sec

			int nPLD = 0;
			bool isValid = true; //detect gaps in PLD array: If user sets PLD1=250, PLD2=0 PLD3=375 only PLD1 was acquired
			for (int k = 30; k < 38; k++) {
				if ((isnan(csaAscii.alFree[k])) || (csaAscii.alFree[k] <= 0.0)) isValid = false;
				if (isValid) nPLD ++;
			} //for k
			if (nPLD > 0) { // record PostLabelDelays, these are listed as "PLD0","PLD1",etc in PDF
				fprintf(fp, "\t\"InitialPostLabelDelay\": [\n"); //https://docs.google.com/document/d/15tnn5F10KpgHypaQJNNGiNKsni9035GtDqJzWqkkP6c/edit#
				for (int i = 0; i < nPLD; i++) {
					if (i != 0)
						fprintf(fp, ",\n");
					fprintf(fp, "\t\t%g", csaAscii.alFree[i+30]/ 1000.0); //ms -> sec
				}
				fprintf(fp, "\t],\n");
			}
			/*
			json_Float(fp, "\t\"PLD0\": %g,\n", csaAscii.alFree[30]/1000.0);
			json_Float(fp, "\t\"PLD1\": %g,\n", csaAscii.alFree[31]/1000.0);
			json_Float(fp, "\t\"PLD2\": %g,\n", csaAscii.alFree[32]/1000.0);
			json_Float(fp, "\t\"PLD3\": %g,\n", csaAscii.alFree[33]/1000.0);
			json_Float(fp, "\t\"PLD4\": %g,\n", csaAscii.alFree[34]/1000.0);
			json_Float(fp, "\t\"PLD5\": %g,\n", csaAscii.alFree[35]/1000.0);
			*/
		}
		if (isOxfordASL) { //properties common to 2D and 3D ASL
			//labelling plane
			fprintf(fp, "\t\"TagPlaneDThickness\": %g,\n", csaAscii.dThickness);
			fprintf(fp, "\t\"TagPlaneUlShape\": %g,\n", csaAscii.ulShape);
			fprintf(fp, "\t\"TagPlaneSPositionDTra\": %g,\n", csaAscii.sPositionDTra);
			fprintf(fp, "\t\"TagPlaneSNormalDTra\": %g,\n", csaAscii.sNormalDTra);
		}
		//general properties
		if (csaAscii.partialFourier > 0) {
			//https://github.com/ismrmrd/siemens_to_ismrmrd/blob/master/parameter_maps/IsmrmrdParameterMap_Siemens_EPI_FLASHREF.xsl
			if (csaAscii.partialFourier == 1) pf = 0.5; // 4/8
			if (csaAscii.partialFourier == 2) pf = 0.625; // 5/8
			if (csaAscii.partialFourier == 4) pf = 0.75;
			if (csaAscii.partialFourier == 8) pf = 0.875;
			fprintf(fp, "\t\"PartialFourier\": %g,\n", pf);
		}
		if (csaAscii.interp > 0) {
			interp = true;
			fprintf(fp, "\t\"Interpolation2D\": %d,\n", interp);
		}
		if (csaAscii.baseResolution > 0) fprintf(fp, "\t\"BaseResolution\": %d,\n", csaAscii.baseResolution );
		if (shimSetting[0] != 0.0) {
			fprintf(fp, "\t\"ShimSetting\": [\n");
			for (int i = 0; i < 8; i++) {
				if (i != 0)
					fprintf(fp, ",\n");
				fprintf(fp, "\t\t%g", shimSetting[i]);
			}
			fprintf(fp, "\t],\n");
		}
		if (d.CSA.numDti > 0) { //
			if (csaAscii.difBipolar == 1) fprintf(fp, "\t\"DiffusionScheme\": \"Bipolar\",\n" );
			if (csaAscii.difBipolar == 2) fprintf(fp, "\t\"DiffusionScheme\": \"Monopolar\",\n" );
		}
		//DelayTimeInTR
		// https://groups.google.com/forum/#!topic/bids-discussion/nmg1BOVH1SU
		// https://groups.google.com/forum/#!topic/bids-discussion/seD7AtJfaFE
		json_Float(fp, "\t\"DelayTime\": %g,\n", delayTimeInTR/ 1000000.0); //DelayTimeInTR usec -> sec
		json_Float(fp, "\t\"TxRefAmp\": %g,\n", csaAscii.txRefAmp);
		json_Float(fp, "\t\"PhaseResolution\": %g,\n", csaAscii.phaseResolution);
		json_Float(fp, "\t\"PhaseOversampling\": %g,\n", phaseOversampling);
		json_Float(fp, "\t\"VendorReportedEchoSpacing\": %g,\n", csaAscii.echoSpacing / 1000000.0); //usec -> sec
		//ETD and epiFactor not useful/reliable https://github.com/rordenlab/dcm2niix/issues/127
		//if (echoTrainDuration > 0) fprintf(fp, "\t\"EchoTrainDuration\": %g,\n", echoTrainDuration / 1000000.0); //usec -> sec
		//if (epiFactor > 0) fprintf(fp, "\t\"EPIFactor\": %d,\n", epiFactor);
		json_Str(fp, "\t\"ReceiveCoilName\": \"%s\",\n", coilID);
		json_Str(fp, "\t\"ReceiveCoilActiveElements\": \"%s\",\n", coilElements);
		if (strcmp(coilElements,d.coilName) != 0)
			json_Str(fp, "\t\"CoilString\": \"%s\",\n", d.coilName);
		strcpy(d.coilName, "");
		json_Str(fp, "\t\"PulseSequenceDetails\": \"%s\",\n", pulseSequenceDetails);
		json_Str(fp, "\t\"FmriExternalInfo\": \"%s\",\n", fmriExternalInfo);
		json_Str(fp, "\t\"WipMemBlock\": \"%s\",\n", wipMemBlock);
		if (strlen(d.protocolName) < 1)  //insert protocol name if it exists in CSA but not DICOM header: https://github.com/nipy/heudiconv/issues/80
			json_Str(fp, "\t\"ProtocolName\": \"%s\",\n", protocolName);
		if (csaAscii.refLinesPE > 0)
			fprintf(fp, "\t\"RefLinesPE\": %d,\n", csaAscii.refLinesPE);
		json_Str(fp, "\t\"ConsistencyInfo\": \"%s\",\n", consistencyInfo);
		if (csaAscii.parallelReductionFactorInPlane > 0) {//AccelFactorPE -> phase encoding
			if (d.accelFactPE < 1.0) { //value not found in DICOM header, but WAS found in CSA ascii
				d.accelFactPE = csaAscii.parallelReductionFactorInPlane; //value found in ASCII but not in DICOM (0051,1011)
				//fprintf(fp, "\t\"ParallelReductionFactorInPlane\": %g,\n", d.accelFactPE);
			}
			if (csaAscii.parallelReductionFactorInPlane != (int)(d.accelFactPE))
				printWarning("ParallelReductionFactorInPlane reported in DICOM [0051,1011] (%d) does not match CSA series value %d\n", (int)(d.accelFactPE), csaAscii.parallelReductionFactorInPlane);
		}
	} else { //e.g. Siemens Vida does not have CSA header, but has many attributes
		json_Str(fp, "\t\"ReceiveCoilActiveElements\": \"%s\",\n", d.coilElements);
		if (strcmp(d.coilElements,d.coilName) != 0)
			json_Str(fp, "\t\"CoilString\": \"%s\",\n", d.coilName);
		if ((!d.is3DAcq) && (d.phaseEncodingLines > d.echoTrainLength) && (d.echoTrainLength > 1)) {
				//ETL is > 1, as some GE files list 1, as an example see series mr_0005 in dcm_qa_nih
				float pf = (float)d.phaseEncodingLines;
				if (d.accelFactPE > 1)
					pf = (float)pf / (float)d.accelFactPE; //estimate: not sure if we round up or down
				pf = (float)d.echoTrainLength / (float)pf;
				if (pf < 1.0) //e.g. if difference between lines and echo length not all explained by iPAT (SENSE/GRAPPA)
					fprintf(fp, "\t\"PartialFourier\": %g,\n", pf);
		} //compute partial Fourier: not reported in XA10, so infer
		//printf("PhaseLines=%d EchoTrainLength=%d  SENSE=%g\n", d.phaseEncodingLines, d.echoTrainLength, d.accelFactPE); //n.b. we can not distinguish pF from SENSE/GRAPPA for UIH
	}
	#endif
	if (d.CSA.multiBandFactor > 1) //AccelFactorSlice
		fprintf(fp, "\t\"MultibandAccelerationFactor\": %d,\n", d.CSA.multiBandFactor);
	json_Float(fp, "\t\"PercentPhaseFOV\": %g,\n", d.phaseFieldofView);
	if (d.echoTrainLength > 1) //>1 as for Siemens EPI this is 1, Siemens uses EPI factor http://mriquestions.com/echo-planar-imaging.html
		fprintf(fp, "\t\"EchoTrainLength\": %d,\n", d.echoTrainLength); //0018,0091 Combination of partial fourier and in-plane parallel imaging
    if (d.phaseEncodingSteps > 0) fprintf(fp, "\t\"PhaseEncodingSteps\": %d,\n", d.phaseEncodingSteps );
	if (d.phaseEncodingLines > 0) fprintf(fp, "\t\"AcquisitionMatrixPE\": %d,\n", d.phaseEncodingLines );

	//Compute ReconMatrixPE
	// Actual size of the *reconstructed* data in the PE dimension, which does NOT match
	// phaseEncodingLines in the case of interpolation or phaseResolution < 100%
	// We'll need this for generating a value for effectiveEchoSpacing that is consistent
	// with the *reconstructed* data.
	int reconMatrixPE = d.phaseEncodingLines;

    if ((h->dim[2] > 0) && (h->dim[1] > 0)) {
		if  (h->dim[1] == h->dim[2]) //phase encoding does not matter
			reconMatrixPE = h->dim[2];
		else if (d.phaseEncodingRC =='C')
			reconMatrixPE = h->dim[2]; //see dcm_qa: NOPF_NOPAT_NOPOS_PERES100_ES0P59_BW2222_200PFOV_AP_0034
		else if (d.phaseEncodingRC =='R')
			reconMatrixPE = h->dim[1];
    }
	if (reconMatrixPE > 0) fprintf(fp, "\t\"ReconMatrixPE\": %d,\n", reconMatrixPE );
    double bandwidthPerPixelPhaseEncode = d.bandwidthPerPixelPhaseEncode;
    if (bandwidthPerPixelPhaseEncode == 0.0)
    	bandwidthPerPixelPhaseEncode = 	d.CSA.bandwidthPerPixelPhaseEncode;
    json_Float(fp, "\t\"BandwidthPerPixelPhaseEncode\": %g,\n", bandwidthPerPixelPhaseEncode );
    //if ((!d.is3DAcq) && (d.accelFactPE > 1.0)) fprintf(fp, "\t\"ParallelReductionFactorInPlane\": %g,\n", d.accelFactPE);
	if (d.accelFactPE > 1.0) fprintf(fp, "\t\"ParallelReductionFactorInPlane\": %g,\n", d.accelFactPE); //https://github.com/rordenlab/dcm2niix/issues/314
	//EffectiveEchoSpacing
	// Siemens bandwidthPerPixelPhaseEncode already accounts for the effects of parallel imaging,
	// interpolation, phaseOversampling, and phaseResolution, in the context of the size of the
	// *reconstructed* data in the PE dimension
    double effectiveEchoSpacing = 0.0;
    //next: dicm2nii's method for determining effectiveEchoSpacing if bandwidthPerPixelPhaseEncode is unknown, see issue 315
	//if ((reconMatrixPE > 0) && (bandwidthPerPixelPhaseEncode <= 0.0) && (d.CSA.sliceMeasurementDuration >= 0))
	//	effectiveEchoSpacing =  d.CSA.sliceMeasurementDuration / (reconMatrixPE * 1000.0);
	if ((reconMatrixPE > 0) && (bandwidthPerPixelPhaseEncode > 0.0))
	    effectiveEchoSpacing = 1.0 / (bandwidthPerPixelPhaseEncode * reconMatrixPE);
    if (d.effectiveEchoSpacingGE > 0.0)
    	effectiveEchoSpacing = d.effectiveEchoSpacingGE / 1000000.0;
    json_Float(fp, "\t\"EffectiveEchoSpacing\": %g,\n", effectiveEchoSpacing);
	// Calculate true echo spacing (should match what Siemens reports on the console)
	// i.e., should match "echoSpacing" extracted from the ASCII CSA header, when that exists
    double trueESfactor = 1.0;
	if (d.accelFactPE > 1.0) trueESfactor /= d.accelFactPE;
	if (phaseOversampling > 0.0)
	  trueESfactor *= (1.0 + phaseOversampling);
	float derivedEchoSpacing = 0.0;
	derivedEchoSpacing = bandwidthPerPixelPhaseEncode * trueESfactor * reconMatrixPE;
	if (derivedEchoSpacing != 0) derivedEchoSpacing = 1/derivedEchoSpacing;
	json_Float(fp, "\t\"DerivedVendorReportedEchoSpacing\": %g,\n", derivedEchoSpacing);
    //TotalReadOutTime: Really should be called "EffectiveReadOutTime", by analogy with "EffectiveEchoSpacing".
	// But BIDS spec calls it "TotalReadOutTime".
	// So, we DO NOT USE EchoTrainLength, because not trying to compute the actual (physical) readout time.
	// Rather, the point of computing "EffectiveEchoSpacing" properly is so that this
	// "Total(Effective)ReadOutTime" can be computed straightforwardly as the product of the
	// EffectiveEchoSpacing and the size of the *reconstructed* matrix in the PE direction.
    // see https://fsl.fmrib.ox.ac.uk/fsl/fslwiki/topup/TopupUsersGuide#A--datain
	// FSL definition is start of first line until start of last line.
	// Other than the use of (n-1), the value is basically just 1.0/bandwidthPerPixelPhaseEncode.
    // https://github.com/rordenlab/dcm2niix/issues/130
    if ((reconMatrixPE > 0) && (effectiveEchoSpacing > 0.0) && (d.manufacturer != kMANUFACTURER_UIH))
	  fprintf(fp, "\t\"TotalReadoutTime\": %g,\n", effectiveEchoSpacing * (reconMatrixPE - 1.0));
    if (d.manufacturer == kMANUFACTURER_UIH) //https://github.com/rordenlab/dcm2niix/issues/225
    	json_Float(fp, "\t\"TotalReadoutTime\": %g,\n", d.acquisitionDuration / 1000.0);
    json_Float(fp, "\t\"PixelBandwidth\": %g,\n", d.pixelBandwidth );
	if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (d.dwellTime > 0))
		fprintf(fp, "\t\"DwellTime\": %g,\n", d.dwellTime * 1E-9);
	// Phase encoding polarity
	int phPos = d.CSA.phaseEncodingDirectionPositive;
	//next two conditionals updated: make GE match Siemens
	if (d.phaseEncodingGE == kGE_PHASE_ENCODING_POLARITY_UNFLIPPED) phPos = 1;
    if (d.phaseEncodingGE == kGE_PHASE_ENCODING_POLARITY_FLIPPED) phPos = 0;
	if (((d.phaseEncodingRC == 'R') || (d.phaseEncodingRC == 'C')) &&  (!d.is3DAcq) && (phPos < 0)) {
		//when phase encoding axis is known but we do not know phase encoding polarity
		// https://github.com/rordenlab/dcm2niix/issues/163
		// This will typically correspond with InPlanePhaseEncodingDirectionDICOM
		if (d.phaseEncodingRC == 'C') //Values should be "R"ow, "C"olumn or "?"Unknown
			fprintf(fp, "\t\"PhaseEncodingAxis\": \"j\",\n");
		else if (d.phaseEncodingRC == 'R')
				fprintf(fp, "\t\"PhaseEncodingAxis\": \"i\",\n");
	}
	if (((d.phaseEncodingRC == 'R') || (d.phaseEncodingRC == 'C')) &&  (!d.is3DAcq) && (phPos >= 0)) {
		if (d.phaseEncodingRC == 'C') //Values should be "R"ow, "C"olumn or "?"Unknown
			fprintf(fp, "\t\"PhaseEncodingDirection\": \"j");
		else if (d.phaseEncodingRC == 'R')
				fprintf(fp, "\t\"PhaseEncodingDirection\": \"i");
		else
			fprintf(fp, "\t\"PhaseEncodingDirection\": \"?");
		//phaseEncodingDirectionPositive has one of three values: UNKNOWN (-1), NEGATIVE (0), POSITIVE (1)
		//However, DICOM and NIfTI are reversed in the j (ROW) direction
		//Equivalent to dicm2nii's "if flp(iPhase), phPos = ~phPos; end"
		//for samples see https://github.com/rordenlab/dcm2niix/issues/125
		if (phPos < 0)
			fprintf(fp, "?"); //unknown
		else if ((phPos == 0) && (d.phaseEncodingRC != 'C'))
			fprintf(fp, "-");
		else if ((d.phaseEncodingRC == 'C') && (phPos == 1) && (opts.isFlipY))
			fprintf(fp, "-");
		else if ((d.phaseEncodingRC == 'C') && (phPos == 0) && (!opts.isFlipY))
			fprintf(fp, "-");
		fprintf(fp, "\",\n");
	} //only save PhaseEncodingDirection if BOTH direction and POLARITY are known
	//Slice Timing UIH or GE >>>>
	//in theory, we should also report XA10 slice times here, but see series 24 of https://github.com/rordenlab/dcm2niix/issues/236
	if ((!d.is3DAcq) && (d.CSA.sliceTiming[0] >= 0.0)) {
   		fprintf(fp, "\t\"SliceTiming\": [\n");
		for (int i = 0; i < h->dim[3]; i++) {
			if (i != 0)
				fprintf(fp, ",\n");
			fprintf(fp, "\t\t%g", d.CSA.sliceTiming[i] / 1000.0 );
		}
		fprintf(fp, "\t],\n");
	}
	//DICOM orientation and phase encoding: useful for 3D undistortion. Original DICOM values: DICOM not NIfTI space, ignores if 3D image re-oriented
	fprintf(fp, "\t\"ImageOrientationPatientDICOM\": [\n");
	for (int i = 1; i < 7; i++) {
		if (i != 1)
			fprintf(fp, ",\n");
		fprintf(fp, "\t\t%g", d.orient[i]);
	}
	fprintf(fp, "\t],\n");
	if (d.phaseEncodingRC == 'C') fprintf(fp, "\t\"InPlanePhaseEncodingDirectionDICOM\": \"COL\",\n" );
	if (d.phaseEncodingRC == 'R') fprintf(fp, "\t\"InPlanePhaseEncodingDirectionDICOM\": \"ROW\",\n" );
	// Finish up with info on the conversion tool
	fprintf(fp, "\t\"ConversionSoftware\": \"dcm2niix\",\n");
	fprintf(fp, "\t\"ConversionSoftwareVersion\": \"%s\"\n", kDCMdate );
	//fprintf(fp, "\t\"ConversionSoftwareVersion\": \"%s\"\n", kDCMvers );kDCMdate
	fprintf(fp, "}\n");
    fclose(fp);
}// nii_SaveBIDS()

bool isADCnotDTI(TDTI bvec) { //returns true if bval!=0 but all bvecs == 0 (Philips code for derived ADC image)
	return ((!isSameFloat(bvec.V[0],0.0f)) && //not a B-0 image
    	((isSameFloat(bvec.V[1],0.0f)) && (isSameFloat(bvec.V[2],0.0f)) && (isSameFloat(bvec.V[3],0.0f)) ) );
}

unsigned char * removeADC(struct nifti_1_header *hdr, unsigned char *inImg, int numADC) {
//for speed we just clip the number of volumes, the realloc routine would be nice
// we do not want to copy input to a new smaller array since 4D DTI datasets can be huge
// and that would require almost twice as much RAM
	if (numADC < 1) return inImg;
	hdr->dim[4] = hdr->dim[4] - numADC;
	if (hdr->dim[4] < 2)
		hdr->dim[0] = 3; //e.g. 4D 2-volume DWI+ADC becomes 3D DWI if ADC is removed
	return inImg;
} //removeADC()

//#define naive_reorder_vols //for simple, fast re-ordering that consumes a lot of RAM
#ifdef naive_reorder_vols
unsigned char * reorderVolumes(struct nifti_1_header *hdr, unsigned char *inImg, int * volOrderIndex) {
//reorder volumes to place ADC at end and (optionally) B=0 at start
// volOrderIndex[0] reports location of desired first volume
//  naive solution creates an output buffer that doubles RAM usage (2 *numVol)
	int numVol = hdr->dim[4];
	int numVolBytes = hdr->dim[1]*hdr->dim[2]*hdr->dim[3]*(hdr->bitpix/8);
	if ((!volOrderIndex) || (numVol < 1) || (numVolBytes < 1)) return inImg;
	unsigned char *outImg = (unsigned char *)malloc(numVolBytes * numVol);
	int outPos = 0;
	for (int i = 0; i < numVol; i++) {
		memcpy(&outImg[outPos], &inImg[volOrderIndex[i] * numVolBytes], numVolBytes); // dest, src, bytes
        outPos += numVolBytes;
	} //for each volume
	free(volOrderIndex);
	free(inImg);
	return outImg;
} //reorderVolumes()
#else // naive_reorder_vols
unsigned char * reorderVolumes(struct nifti_1_header *hdr, unsigned char *inImg, int * volOrderIndex) {
//reorder volumes to place ADC at end and (optionally) B=0 at start
// volOrderIndex[0] reports location of desired first volume
// complicated by fact that 4D DTI data is often huge
//  simple solutions would create an output buffer that would double RAM usage (2 *numVol)
//  here we bubble-sort volumes in place to use numVols+1 memory
	int numVol = hdr->dim[4];
	int numVolBytes = hdr->dim[1]*hdr->dim[2]*hdr->dim[3]*(hdr->bitpix/8);
	int * inPos = (int *) malloc(numVol * sizeof(int));
	for (int i = 0; i < numVol; i++)
        inPos[i] = i;
	unsigned char *tempVol = (unsigned char *)malloc(numVolBytes);
	int outPos = 0;
	for (int o = 0; o < numVol; o++) {
		int i = inPos[volOrderIndex[o]]; //input volume
		if (i == o) continue; //volume in correct order
		memcpy(&tempVol[0], &inImg[o * numVolBytes], numVolBytes); //make temp
        memcpy(&inImg[o * numVolBytes], &inImg[i * numVolBytes], numVolBytes); //copy volume to desire location dest, src, bytes
        memcpy(&inImg[i * numVolBytes], &tempVol[0], numVolBytes); //copy unsorted volume
        inPos[o] = i;
        outPos += numVolBytes;
	} //for each volume
	free(inPos);
	free(volOrderIndex);
	free(tempVol);
	return inImg;
} //reorderVolumes()
#endif // naive_reorder_vols

float * bvals; //global variable for cmp_bvals
int cmp_bvals(const void *a, const void *b){
    int ia = *(int *)a;
    int ib = *(int *)b;
    //return bvals[ia] > bvals[ib] ? -1 : bvals[ia] < bvals[ib];
    return bvals[ia] < bvals[ib] ? -1 : bvals[ia] > bvals[ib];
} // cmp_bvals()

bool isAllZeroFloat(float v1, float v2, float v3) {
	if (!isSameFloatGE(v1, 0.0)) return false;
	if (!isSameFloatGE(v2, 0.0)) return false;
	if (!isSameFloatGE(v3, 0.0)) return false;
	return true;
}

int * nii_saveDTI(char pathoutname[],int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TDCMopts opts, int sliceDir, struct TDTI4D *dti4D, int * numADC) {
    //reports non-zero if any volumes should be excluded (e.g. philip stores an ADC maps)
    //to do: works with 3D mosaics and 4D files, must remove repeated volumes for 2D sequences....
    *numADC = 0;
    if (opts.isOnlyBIDS) return NULL;
    uint64_t indx0 = dcmSort[0].indx; //first volume
    int numDti = dcmList[indx0].CSA.numDti;
    if (numDti < 1) return NULL;
    if ((numDti < 3) && (nConvert < 3)) return NULL;
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
        dcmList[indx0].CSA.numDti = numDti; //warning structure not changed outside scope!
    }
    bool bValueVaries = false;
    for (int i = 1; i < numDti; i++) //check if all bvalues match first volume
        if (vx[i].V[0] != vx[0].V[0]) bValueVaries = true;
    //optional: record b-values even without variability
    float minBval = vx[0].V[0];
    for (int i = 1; i < numDti; i++) //check if all bvalues match first volume
        if (vx[i].V[0] < minBval) minBval = vx[i].V[0];
    if (minBval > 50.0) bValueVaries = true;
    //do not save files without variability
    if (!bValueVaries) {
        bool bVecVaries = false;
        for (int i = 1; i < numDti; i++) {//check if all bvalues match first volume
            if (vx[i].V[1] != vx[0].V[1]) bVecVaries = true;
            if (vx[i].V[2] != vx[0].V[2]) bVecVaries = true;
            if (vx[i].V[3] != vx[0].V[3]) bVecVaries = true;
        }
        if (!bVecVaries) {
			free(vx);
			return NULL;
        }
        if (opts.isVerbose) {
        	for (int i = 0; i < numDti; i++)
                printMessage("bxyz %g %g %g %g\n",vx[i].V[0],vx[i].V[1],vx[i].V[2],vx[i].V[3]);
        }
        //Stutters XINAPSE7 seem to save B=0 as B=2000, but these are not derived? https://github.com/rordenlab/dcm2niix/issues/182
        bool bZeroBvec = false;
        for (int i = 0; i < numDti; i++) {//check if all bvalues match first volume
            if (isAllZeroFloat(vx[i].V[1], vx[i].V[2], vx[i].V[3])) {
            	vx[i].V[0] = 0;
            	//printWarning("volume %d might be B=0\n", i);
            	bZeroBvec = true;
            }
        }
        if (bZeroBvec)
        	printWarning("Assuming volumes without gradients are actually B=0\n");
        else {
        	printWarning("No bvec/bval files created. Only one B-value reported for all volumes: %g\n",vx[0].V[0]);
        	free(vx);
        	return NULL;
        }
    }
    //report values:
    //for (int i = 1; i < numDti; i++) //check if all bvalues match first volume
    //    printMessage("%d bval= %g  bvec= %g %g %g\n",i, vx[i].V[0], vx[i].V[1], vx[i].V[2], vx[i].V[3]);
	int minB0idx = 0;
    float minB0 = vx[0].V[0];
    for (int i = 0; i < numDti; i++)
        if (vx[i].V[0] < minB0) {
            minB0 = vx[i].V[0];
            minB0idx = i;
        }
    float maxB0 = vx[0].V[0];
    for (int i = 0; i < numDti; i++)
        if (vx[i].V[0] > maxB0)
            maxB0 = vx[i].V[0];
    //for CMRR sequences unweighted volumes are not actually B=0 but they have B near zero
    if (minB0 > 50) printWarning("This diffusion series does not have a B0 (reference) volume\n");
	if ((!opts.isSortDTIbyBVal) && (minB0idx > 0))
		printMessage("Note: B0 not the first volume in the series (FSL eddy reference volume is %d)\n", minB0idx);
	float kADCval = maxB0 + 1; //mark as unusual
    *numADC = 0;
	bvals = (float *) malloc(numDti * sizeof(float));
	int numGEwarn = 0;
	bool isGEADC = (dcmList[indx0].numberOfDiffusionDirectionGE == 0);
	for (int i = 0; i < numDti; i++) {
		bvals[i] = vx[i].V[0];
		//printMessage("---bxyz %g %g %g %g\n",vx[i].V[0],vx[i].V[1],vx[i].V[2],vx[i].V[3]);
		//Philips includes derived isotropic images
		//if (((dcmList[indx0].manufacturer == kMANUFACTURER_GE) || (dcmList[indx0].manufacturer == kMANUFACTURER_PHILIPS)) && (isADCnotDTI(vx[i]))) {
        if (((dcmList[indx0].manufacturer == kMANUFACTURER_GE)) && (isADCnotDTI(vx[i]))) {
            numGEwarn += 1;
            if (isGEADC) { //e.g. GE Trace where bval=900, bvec=0,0,0
            	*numADC = *numADC + 1;
            	//printWarning("GE ADC volume %d\n", i+1);
            	bvals[i] = kADCval;
            } else
            	vx[i].V[0] = 0; //e.g. GE raw B=0 where bval=900, bvec=0,0,0
        } //see issue 245
        if (((dcmList[indx0].manufacturer == kMANUFACTURER_PHILIPS)) && (isADCnotDTI(vx[i]))) {
            *numADC = *numADC + 1;
            bvals[i] = kADCval;
            //printMessage("+++bxyz %d\n",i);
        }
        bvals[i] = bvals[i] + (0.5 * i/numDti); //add a small bias so ties are kept in sequential order
	}
	if (numGEwarn > 0)
		printWarning("Some images had bval>0 but bvec=0 (either Trace or b=0, see issue 245)\n");
	if ((*numADC == numDti) || (numGEwarn  == numDti)) {
		//all isotropic/ADC images - no valid bvecs
		*numADC = 0;
		free(bvals);
		free(vx);
		return NULL;
	}
	if (*numADC > 0) {
		// DWIs (i.e. short diffusion scans with too few directions to
		// calculate tensors...they typically acquire b=0 + 3 b > 0 so
		// the isotropic trace or MD can be calculated) often come as
		// b=0 and trace pairs, with the b=0 and trace in either order,
		// and often as "ORIGINAL", even though the trace is not.
		// The bval file is needed for downstream processing to know
		// * which is the b=0 and which is the trace, and
		// * what b is for the trace,
		// so dcm2niix should *always* write the bval and bvec files,
		// AND include the b for the trace for DWIs.
		// One hackish way to accomplish that is to set *numADC = 0
		// when *numADC == 1 && numDti == 2.
		// - Rob Reid, 2017-11-29.
		if ((*numADC == 1) && ((numDti - *numADC) < 2)){
			*numADC = 0;
			printMessage("Note: this appears to be a b=0+trace DWI; ADC/trace removal has been disabled.\n");
		}
		else{
		  if ((numDti - *numADC) < 2) {
			if (!dcmList[indx0].isDerived) //no need to warn if images are derived Trace/ND pair
			  printWarning("No bvec/bval files created: only single value after ADC excluded\n");
			*numADC = 0;
			free(bvals);
			free(vx);
			return NULL;
		  }
		  printMessage("Note: %d volumes appear to be ADC or trace images that will be removed to allow processing\n",
					   *numADC);
		}
	}
	//sort ALL including ADC
	int * volOrderIndex = (int *) malloc(numDti * sizeof(int));
	for (int i = 0; i < numDti; i++)
        volOrderIndex[i] = i;
	if (opts.isSortDTIbyBVal)
		qsort(volOrderIndex, numDti, sizeof(*volOrderIndex), cmp_bvals);
	else if (*numADC > 0) {
		int o = 0;
		for (int i = 0; i < numDti; i++) {
			if (bvals[i] < kADCval) {
        		volOrderIndex[o] = i;
        		o++;
        	} //if not ADC
        } //for each volume
	} //if sort else if has ADC
	free(bvals);
	//save VX as sorted
	TDTI * vxOrig = (TDTI *)malloc(numDti * sizeof(TDTI));
	for (int i = 0; i < numDti; i++)
    	vxOrig[i] = vx[i];
    //remove ADC
	numDti = numDti - *numADC;
	free(vx);
	vx = (TDTI *)malloc(numDti * sizeof(TDTI));
    for (int i = 0; i < numDti; i++)
    	vx[i] = vxOrig[volOrderIndex[i]];
    free(vxOrig);
    //if no ADC or sequential, the is no need to re-order volumes
	bool isSequential = true;
	for (int i = 1; i < (numDti + *numADC); i++)
		if (volOrderIndex[i] <= volOrderIndex[i-1])
			isSequential = false;
	if (isSequential) {
		free(volOrderIndex);
		volOrderIndex = NULL;
	}
	if (!isSequential)
	  printMessage("DTI volumes re-ordered by ascending b-value\n");
	dcmList[indx0].CSA.numDti = numDti; //warning structure not changed outside scope!
    geCorrectBvecs(&dcmList[indx0],sliceDir, vx, opts.isVerbose);
    siemensPhilipsCorrectBvecs(&dcmList[indx0],sliceDir, vx, opts.isVerbose);
    if (!opts.isFlipY ) { //!FLIP_Y&& (dcmList[indx0].CSA.mosaicSlices < 2) mosaics are always flipped in the Y direction
        for (int i = 0; i < (numDti); i++) {
            if (fabs(vx[i].V[2]) > FLT_EPSILON)
                vx[i].V[2] = -vx[i].V[2];
        } //for each direction
    } //if not a mosaic
    if (opts.isVerbose) {
        for (int i = 0; i < (numDti); i++) {
            printMessage("%d\tB=\t%g\tVec=\t%g\t%g\t%g\n",i, vx[i].V[0],
                   vx[i].V[1],vx[i].V[2],vx[i].V[3]);

        } //for each direction
    }
    //printMessage("%f\t%f\t%f",dcmList[indx0].CSA.dtiV[1][1],dcmList[indx0].CSA.dtiV[1][2],dcmList[indx0].CSA.dtiV[1][3]);
#ifdef USING_R
    std::vector<double> bValues(numDti);
    std::vector<double> bVectors(numDti*3);
    for (int i = 0; i < numDti; i++)
    {
        bValues[i] = vx[i].V[0];
        for (int j = 0; j < 3; j++)
            bVectors[i+j*numDti] = vx[i].V[j+1];
    }
    // The image hasn't been created yet, so the attributes must be deferred
    ImageList *images = (ImageList *) opts.imageList;
    images->addDeferredAttribute("bValues", bValues);
    images->addDeferredAttribute("bVectors", bVectors, numDti, 3);
#else
    if (opts.isSaveNRRD) {
    	if (numDti < kMaxDTI4D) {
    		dcmList[indx0].CSA.numDti = numDti;
    		for (int i = 0; i < numDti; i++) //for each direction
        		for (int v = 0; v < 4; v++) //for each vector+B-value
            		dti4D->S[i].V[v] = vx[i].V[v];
        }
    	free(vx);
    	return volOrderIndex;
    }
    char txtname[2048] = {""};
    strcpy (txtname,pathoutname);
    strcat (txtname,".bval");
    //printMessage("Saving DTI %s\n",txtname);
    FILE *fp = fopen(txtname, "w");
    if (fp == NULL) {
        free(vx);
        return volOrderIndex;
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
    if (dcmList[indx0].isVectorFromBMatrix)
    	strcat (txtname,".mvec");
    else
    	strcat (txtname,".bvec");
    //printMessage("Saving DTI %s\n",txtname);
    fp = fopen(txtname, "w");
    if (fp == NULL) {
        free(vx);
        return volOrderIndex;
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
#endif
    free(vx);
    return volOrderIndex;
}// nii_saveDTI()

float sqr(float v){
    return v*v;
}// sqr()

#ifdef newTilt //see issue 254
float vec3Length (vec3 v) { //normalize vector length
	return sqrt( (v.v[0]*v.v[0])
                      + (v.v[1]*v.v[1])
                      + (v.v[2]*v.v[2]));
}

float vec3maxMag (vec3 v) { //return signed vector with maximum magnitude
	float mx = v.v[0];
	if (fabs(v.v[1]) > fabs(mx)) mx = v.v[1];
	if (fabs(v.v[2]) > fabs(mx)) mx = v.v[2];
	return mx;
}

vec3 makePositive(vec3 v) {
	//we do not no order of cross product or order of instance number (e.g. head->foot, foot->head)
	// this function matches the polarity of slice direction inferred from patient position and image orient
	vec3 ret = v;
	if (vec3maxMag(v) >= 0.0) return ret;
	ret.v[0] = -ret.v[0];
	ret.v[1] = -ret.v[1];
	ret.v[2] = -ret.v[2];
	return ret;
}

void vecRep (vec3 v) { //normalize vector length
   printMessage("[%g %g %g]\n", v.v[0], v.v[1], v.v[2]);
}

//Precise method for determining gantry tilt
// rationale:
//   gantry tilt (0018,1120) is optional
//   some tools may correct gantry tilt but not reset 0018,1120
//   0018,1120 might be saved at low precision (though patientPosition, orient might be as well)
//https://github.com/rordenlab/dcm2niix/issues/253
float computeGantryTiltPrecise(struct TDICOMdata d1, struct TDICOMdata d2, int isVerbose) {
	float ret = 0.0;
	if (isNanPosition(d1)) return ret;
	vec3 slice_vector = setVec3(d2.patientPosition[1] - d1.patientPosition[1],
    	d2.patientPosition[2] - d1.patientPosition[2],
    	d2.patientPosition[3] - d1.patientPosition[3]);
    float len = vec3Length(slice_vector);
	if (isSameFloat(len, 0.0)) {
		slice_vector = setVec3(d1.patientPositionLast[1] - d1.patientPosition[1],
    		d1.patientPositionLast[2] - d1.patientPosition[2],
    		d1.patientPositionLast[3] - d1.patientPosition[3]);
    	len = vec3Length(slice_vector);
    	if (isSameFloat(len, 0.0)) return ret;
	}
	if (isnan(slice_vector.v[0])) return ret;
	slice_vector = makePositive(slice_vector);
	vec3 read_vector = setVec3(d1.orient[1],d1.orient[2],d1.orient[3]);
    vec3 phase_vector = setVec3(d1.orient[4],d1.orient[5],d1.orient[6]);
    vec3 slice_vector90 = crossProduct(read_vector ,phase_vector); //perpendicular
    slice_vector90 = makePositive(slice_vector90);
	float len90 = vec3Length(slice_vector90);
    if (isSameFloat(len90, 0.0)) return ret;
    float dotX = dotProduct(slice_vector90, slice_vector);
    float cosX = dotX / (len * len90);
    float degX = acos(cosX) * (180.0 / M_PI); //arccos, radian -> degrees
    if (!isSameFloat(cosX, 1.0))
		ret = degX;
    if ((isSameFloat(ret, 0.0)) && (isSameFloat(ret, d1.gantryTilt)) ) return 0.0;
    //determine if gantry tilt is positive or negative
    vec3 signv = crossProduct(slice_vector,slice_vector90);
	float sign = vec3maxMag(signv);
	if (isSameFloatGE(ret, 0.0)) return 0.0; //parallel vectors
	if (sign > 0.0) ret = -ret; //the length of len90 was negative, negative gantry tilt
    //while (ret >= 89.99) ret -= 90;
    //while (ret <= -89.99) ret += 90;
	if (isSameFloatGE(ret, 0.0)) return 0.0;
    if ((isVerbose) || (isnan(ret)))  {
    	printMessage("Gantry Tilt Parameters (see issue 253)\n");
    	printMessage(" Read ="); vecRep(read_vector);
    	printMessage(" Phase ="); vecRep(phase_vector);
    	printMessage(" CrossReadPhase ="); vecRep(slice_vector90);
    	printMessage(" Slice ="); vecRep(slice_vector);
    }
    printMessage("Gantry Tilt based on 0018,1120 %g, estimated from slice vector %g\n", d1.gantryTilt, ret);
	return ret;
}
#endif //newTilt //see issue 254


float intersliceDistance(struct TDICOMdata d1, struct TDICOMdata d2) {
    //some MRI scans have gaps between slices, some CT have overlapping slices. Comparing adjacent slices provides measure for dx between slices
    if ( isNanPosition(d1) ||  isNanPosition(d2))
        return d1.xyzMM[3];
    float tilt = 1.0;
    //printMessage("0020,0032 %g %g %g -> %g %g %g\n",d1.patientPosition[1],d1.patientPosition[2],d1.patientPosition[3],d2.patientPosition[1],d2.patientPosition[2],d2.patientPosition[3]);
    if (d1.gantryTilt != 0)
        tilt = (float) cos(d1.gantryTilt  * M_PI/180); //for CT scans with gantry tilt, we need to compute distance between slices, not distance along bed
    return tilt * sqrt( sqr(d1.patientPosition[1]-d2.patientPosition[1])+
                sqr(d1.patientPosition[2]-d2.patientPosition[2])+
                sqr(d1.patientPosition[3]-d2.patientPosition[3]));
} //intersliceDistance()

//#define myInstanceNumberOrderIsNotSpatial
//instance number is virtually always ordered based on spatial position.
// interleaved/multi-band conversion will be disrupted if instance number refers to temporal order
// these functions reorder images based on spatial position
// this situation is exceptionally rare, and there is a performance penalty
// further, there may be unintended consequences.
// Therefore, use of myInstanceNumberOrderIsNotSpatial is NOT recommended
//  a better solution is to fix the sequences that generated those files
//  as such images will probably disrupt most tools.
// This option is only to salvage borked data.
// This code has also not been tested on data stored in TXYZ rather than XYZT order
//#ifdef myInstanceNumberOrderIsNotSpatial

float intersliceDistanceSigned(struct TDICOMdata d1, struct TDICOMdata d2) {
	//reports distance between two slices, signed as 2nd slice can be in front or behind 1st
	vec3 slice_vector = setVec3(d2.patientPosition[1] - d1.patientPosition[1],
    	d2.patientPosition[2] - d1.patientPosition[2],
    	d2.patientPosition[3] - d1.patientPosition[3]);
    float len = vec3Length(slice_vector);
    if (isSameFloat(len, 0.0)) return len;
    if (d1.gantryTilt != 0)
        len = len * cos(d1.gantryTilt  * M_PI/180);
    vec3 read_vector = setVec3(d1.orient[1],d1.orient[2],d1.orient[3]);
    vec3 phase_vector = setVec3(d1.orient[4],d1.orient[5],d1.orient[6]);
    vec3 slice_vector90 = crossProduct(read_vector ,phase_vector); //perpendicular
	float dot = dotProduct(slice_vector90, slice_vector);
    if (dot < 0.0) return -len;
	return len;
}

//https://stackoverflow.com/questions/36714030/c-sort-float-array-while-keeping-track-of-indices/36714204
struct TFloatSort{
   float value;
   int index;
};

 int compareTFloatSort(const void *a,const void *b){
  struct TFloatSort *a1 = (struct TFloatSort *)a;
  struct TFloatSort *a2 = (struct TFloatSort*)b;
  if((*a1).value > (*a2).value) return 1;
  if((*a1).value < (*a2).value) return -1;
  //if value is tied, retain index order (useful for TXYZ images?)
  if((*a1).index > (*a2).index) return 1;
  if((*a1).index < (*a2).index) return -1;
  return 0;
} // compareTFloatSort()

bool ensureSequentialSlicePositions(int d3, int d4, struct TDCMsort dcmSort[], struct TDICOMdata dcmList[]) {
    //ensure slice position is sequential: either ascending [1 2 3] or descending [3 2 1], not [1 3 2], [3 1 2] etc.
    //n.b. as currently designed, this will force swapDim3Dim4() for 4D data
    int nConvert = d3 * d4;
    if (d3 < 3) return true; //always consistent
    float dx = intersliceDistanceSigned(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
	bool isAscending1 = (dx > 0);
    bool isConsistent = true;
	for(int i=1; i < d3; i++) {
		dx = intersliceDistanceSigned(dcmList[dcmSort[i-1].indx],dcmList[dcmSort[i].indx]);
		bool isAscending = (dx > 0);
		if (isAscending != isAscending1) isConsistent = false; //direction reverses
	}
	if (isConsistent) return true;
	printWarning("Order specified by DICOM instance number is not spatial (reordering).\n");
	TFloatSort * floatSort = (TFloatSort *)malloc(d3 * sizeof(TFloatSort));
    for(int i=0; i < d3; i++) {
		dx = intersliceDistanceSigned(dcmList[dcmSort[0].indx],dcmList[dcmSort[i].indx]);
		floatSort[i].value = dx;
		floatSort[i].index=i;
	}
	TDCMsort* dcmSortIn = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
	for(int i=0; i < nConvert; i++)
		dcmSortIn[i] = dcmSort[i];
	qsort(floatSort, d3, sizeof(struct TFloatSort), compareTFloatSort); //sort based on series and image numbers....
	for(int vol=0; vol < d4; vol++) {
		int volInc = vol * d3;
		for(int i=0; i < d3; i++)
			dcmSort[volInc+i] = dcmSortIn[volInc+floatSort[i].index];
	}
	free(floatSort);
	free(dcmSortIn);
	return false;
} // ensureSequentialSlicePositions()
//#endif //myInstanceNumberOrderIsNotSpatial

void swapDim3Dim4(int d3, int d4, struct TDCMsort dcmSort[]) {
    //swap space and time: input A0,A1...An,B0,B1...Bn output A0,B0,A1,B1,...
    int nConvert = d3 * d4;
	TDCMsort * dcmSortIn = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
    for (int i = 0; i < nConvert; i++) dcmSortIn[i] = dcmSort[i];
    int i = 0;
    for (int b = 0; b < d3; b++)
        for (int a = 0; a < d4; a++) {
            int k = (a *d3) + b;
            //printMessage("%d -> %d %d ->%d\n",i,a, b, k);
            dcmSort[k] = dcmSortIn[i];
            i++;
        }
	free(dcmSortIn);
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
 } */

void niiDeleteFnm(const char* outname, const char* ext) {
    char niiname[2048] = {""};
    strcat (niiname,outname);
    strcat (niiname,ext);
    if (is_fileexists(niiname))
    	remove(niiname);
}

void niiDelete(const char*niiname) {
	//for niiname "~/d/img" delete img.nii, img.bvec, img.bval, img.json
	niiDeleteFnm(niiname,".nii");
	niiDeleteFnm(niiname,".nii.gz");
	niiDeleteFnm(niiname,".nrrd");
	niiDeleteFnm(niiname,".nhdr");
	niiDeleteFnm(niiname,".raw.gz");
	niiDeleteFnm(niiname,".json");
	niiDeleteFnm(niiname,".bval");
	niiDeleteFnm(niiname,".bvec");
}

bool niiExists(const char*pathoutname) {
    char niiname[2048] = {""};
    strcat (niiname,pathoutname);
    strcat (niiname,".nii");
    if (is_fileexists(niiname)) return true;
    char gzname[2048] = {""};
    strcat (gzname,pathoutname);
    strcat (gzname,".nii.gz");
    if (is_fileexists(gzname)) return true;
    strcpy (niiname,pathoutname);
    strcat (niiname,".nrrd");
    if (is_fileexists(niiname)) return true;
    strcpy (niiname,pathoutname);
    strcat (niiname,".nhdr");
    if (is_fileexists(niiname)) return true;
    return false;
} //niiExists()

#ifndef W_OK
#define W_OK 2 /* write mode check */
#endif

int strcicmp(char const *a, char const *b) //case insensitive compare
{
    for (;; a++, b++) {
        int d = tolower(*a) - tolower(*b);
        if (d != 0 || !*a)
            return d;
    }
}// strcicmp()

bool isExt (char *file_name, const char* ext) {
    char *p_extension;
    if((p_extension = strrchr(file_name,'.')) != NULL )
        if(strcicmp(p_extension,ext) == 0) return true;
    //if(strcmp(p_extension,ext) == 0) return true;
    return false;
}// isExt()


int nii_createFilename(struct TDICOMdata dcm, char * niiFilename, struct TDCMopts opts) {
    char pth[PATH_MAX] = {""};
    if (strlen(opts.outdir) > 0) {
        strcpy(pth, opts.outdir);
        int w =access(pth,W_OK);
        if (w != 0) {
        	//should never happen except with "-b i": see kEXIT_OUTPUT_FOLDER_READ_ONLY for early termination
        	// with "-b i" the code below generates a warning but no files are created
			if (getcwd(pth, sizeof(pth)) != NULL) {
				#ifdef USE_CWD_IF_OUTDIR_NO_WRITE //optional: fall back to current working directory
				w =access(pth,W_OK);
				if (w != 0) {
					printError("You do not have write permissions for the directory %s\n",opts.outdir);
					return EXIT_FAILURE;
				}
				printWarning("%s write permission denied. Saving to working directory %s \n", opts.outdir, pth);
				#else
				printError("You do not have write permissions for the directory %s\n",opts.outdir);
				return EXIT_FAILURE;
				#endif
			}
        }
     }
    char inname[PATH_MAX] = {""};//{"test%t_%av"}; //% a = acquisition, %n patient name, %t time
    strcpy(inname, opts.filename);
    bool isDcmExt = isExt(inname, ".dcm"); // "%r.dcm" with multi-echo should generate "1.dcm", "1e2.dcm"
	if (isDcmExt) {
		inname[strlen(inname) - 4] = '\0';
	}
    char outname[PATH_MAX] = {""};
    char newstr[256];
    if (strlen(inname) < 1) {
        strcpy(inname, "T%t_N%n_S%s");
    }
    size_t start = 0;
    size_t pos = 0;
    bool isCoilReported = false;
    bool isEchoReported = false;
    bool isSeriesReported = false;
    //bool isAcquisitionReported = false;
    bool isImageNumReported = false;
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
        	if (f == 'A')  {
        		isCoilReported = true;
                strcat (outname,dcm.coilName);
            }
            if (f == 'B') strcat (outname,dcm.imageBaseName);
            if (f == 'C') strcat (outname,dcm.imageComments);
            if (f == 'D') strcat (outname,dcm.seriesDescription);
        	if (f == 'E') {
        		isEchoReported = true;
                sprintf(newstr, "%d", dcm.echoNum);
                strcat (outname,newstr);
            }
            if (f == 'F')
                strcat (outname,opts.indirParent);
            if (f == 'G')
                strcat(outname, dcm.accessionNumber);
            if (f == 'I')
                strcat (outname,dcm.patientID);
            if (f == 'J')
                strcat (outname,dcm.seriesInstanceUID);
            if (f == 'K')
                strcat (outname,dcm.studyInstanceUID);
            if (f == 'L') //"L"ocal Institution-generated description or classification of the Procedure Step that was performed.
                strcat (outname,dcm.procedureStepDescription);
            if (f == 'M') {
                if (dcm.manufacturer == kMANUFACTURER_BRUKER)
                    strcat (outname,"Br");
                else if (dcm.manufacturer == kMANUFACTURER_GE)
                    strcat (outname,"GE");
                else if (dcm.manufacturer == kMANUFACTURER_TOSHIBA)
                    strcat (outname,"To");
                else if (dcm.manufacturer == kMANUFACTURER_UIH)
                	strcat (outname,"UI");
                else if (dcm.manufacturer == kMANUFACTURER_PHILIPS)
                    strcat (outname,"Ph");
                else if (dcm.manufacturer == kMANUFACTURER_SIEMENS)
                    strcat (outname,"Si");
                else
                    strcat (outname,"NA"); //manufacturer name not available
            }
            if (f == 'N')
                strcat (outname,dcm.patientName);
            if (f == 'O')
                strcat (outname,dcm.instanceUID);
            if (f == 'P') {
            	strcat (outname,dcm.protocolName);
                if (strlen(dcm.protocolName) < 1)
                	printWarning("Unable to append protocol name (0018,1030) to filename (it is empty).\n");
            }
            if (f == 'R') {
                sprintf(newstr, "%d", dcm.imageNum);
                strcat (outname,newstr);
                isImageNumReported = true;
            }
            if (f == 'Q')
                strcat (outname,dcm.scanningSequence);
            if (f == 'S') {
            	sprintf(newstr, "%ld", dcm.seriesNum);
                strcat (outname,newstr);
                isSeriesReported = true;
            }
            if (f == 'T') {
                sprintf(newstr, "%0.0f", dcm.dateTime);
                strcat (outname,newstr);
            }
			if (f == 'U') {
				if (opts.isRenameNotConvert) {
					sprintf(newstr, "%d", dcm.acquNum);
					strcat (outname,newstr);
					//isAcquisitionReported = true;
				} else {
					#ifdef mySegmentByAcq
					sprintf(newstr, "%d", dcm.acquNum);
					strcat (outname,newstr);
					//isAcquisitionReported = true;
					#else
					printWarning("Ignoring '%%u' in output filename (recompile to segment by acquisition)\n");
					#endif
    			}
			}
			if (f == 'V') {
				if (dcm.manufacturer == kMANUFACTURER_BRUKER)
					strcat (outname,"Bruker");
				else if (dcm.manufacturer == kMANUFACTURER_GE)
					strcat (outname,"GE");
				else if (dcm.manufacturer == kMANUFACTURER_PHILIPS)
					strcat (outname,"Philips");
				else if (dcm.manufacturer == kMANUFACTURER_SIEMENS)
					strcat (outname,"Siemens");
				else if (dcm.manufacturer == kMANUFACTURER_TOSHIBA)
					strcat (outname,"Toshiba");
				else if (dcm.manufacturer == kMANUFACTURER_UIH)
					strcat (outname,"UIH");
				else
					strcat (outname,"NA");
			}
			if (f == 'X')
				strcat (outname,dcm.studyID);
            if (f == 'Z')
                strcat (outname,dcm.sequenceName);
            if ((f >= '0') && (f <= '9')) {
                if ((pos<strlen(inname)) && (toupper(inname[pos+1]) == 'S')) {
                    char zeroPad[12] = {""};
                    sprintf(zeroPad,"%%0%dd",f - '0');
                    sprintf(newstr, zeroPad, dcm.seriesNum);
                    strcat (outname,newstr);
                    pos++; // e.g. %3f requires extra increment: skip both number and following character
                }
                if ((pos<strlen(inname)) && (toupper(inname[pos+1]) == 'R')) {
                    char zeroPad[12] = {""};
                    sprintf(zeroPad,"%%0%dd",f - '0');
                    sprintf(newstr, zeroPad, dcm.imageNum);
                    isImageNumReported = true;
                    strcat (outname,newstr);
                    pos++; // e.g. %3f requires extra increment: skip both number and following character
                }
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
    if ((!isCoilReported) && (dcm.isCoilVaries)) {
        //sprintf(newstr, "_c%d", dcm.coilNum);
        //strcat (outname,newstr);
        strcat (outname, "_c");
        strcat (outname,dcm.coilName);
    }
    // myMultiEchoFilenameSkipEcho1 https://github.com/rordenlab/dcm2niix/issues/237
    #ifdef myMultiEchoFilenameSkipEcho1
    if ((!isEchoReported) && (dcm.isMultiEcho) && (dcm.echoNum >= 1)) { //multiple echoes saved as same series
    #else
    if ((!isEchoReported) && ((dcm.isMultiEcho) || (dcm.echoNum > 1))) { //multiple echoes saved as same series
    #endif
        sprintf(newstr, "_e%d", dcm.echoNum);
        strcat (outname,newstr);
        isEchoReported = true;
    }
    if ((!isSeriesReported) && (!isEchoReported) && (dcm.echoNum > 1)) { //last resort: user provided no method to disambiguate echo number in filename
        sprintf(newstr, "_e%d", dcm.echoNum);
        strcat (outname,newstr);
        isEchoReported = true;
    }
    if ((dcm.isNonParallelSlices) && (!isImageNumReported)) {
    	sprintf(newstr, "_i%05d", dcm.imageNum);
        strcat (outname,newstr);
    }
    /*if (dcm.maxGradDynVol > 0) { //Philips segmented
        sprintf(newstr, "_v%04d", dcm.gradDynVol+1); //+1 as indexed from zero
        strcat (outname,newstr);
    }*/
    if (dcm.isHasImaginary) {
    	strcat (outname,"_imaginary"); //has phase map
    }
    if (dcm.isHasReal) {
    	strcat (outname,"_real"); //has phase map
    }
    if (dcm.isHasPhase) {
    	strcat (outname,"_ph"); //has phase map
    	if (dcm.isHasMagnitude)
    		strcat (outname,"Mag"); //Philips enhanced with BOTH phase and Magnitude in single file
    }
    if ((dcm.triggerDelayTime >= 1) && (dcm.manufacturer != kMANUFACTURER_GE)){ //issue 336 GE uses this for slice timing
    	sprintf(newstr, "_t%d", (int)roundf(dcm.triggerDelayTime));
        strcat (outname,newstr);
    }
    if (dcm.isRawDataStorage) //avoid name clash for Philips XX_ files
    	strcat (outname,"_Raw");
    if (dcm.isGrayscaleSoftcopyPresentationState) //avoid name clash for Philips PS_ files
    	strcat (outname,"_PS");
    if (isDcmExt)
    	strcat (outname,".dcm");
    if (strlen(outname) < 1) strcpy(outname, "dcm2nii_invalidName");
    if (outname[0] == '.') outname[0] = '_'; //make sure not a hidden file
    //eliminate illegal characters http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx
    // https://github.com/rordenlab/dcm2niix/issues/237
    #ifdef myOsSpecificFilenameMask
     #define kMASK_WINDOWS_SPECIAL_CHARACTERS 0
    #else
     #define kMASK_WINDOWS_SPECIAL_CHARACTERS 1
    #endif
    #if defined(_WIN64) || defined(_WIN32) || defined(kMASK_WINDOWS_SPECIAL_CHARACTERS)//https://stackoverflow.com/questions/1976007/what-characters-are-forbidden-in-windows-and-linux-directory-names
    for (size_t pos = 0; pos<strlen(outname); pos ++)
        if ((outname[pos] == '<') || (outname[pos] == '>') || (outname[pos] == ':')
            || (outname[pos] == '"') // || (outname[pos] == '/') || (outname[pos] == '\\')
            || (outname[pos] == '^')
            || (outname[pos] == '*') || (outname[pos] == '|') || (outname[pos] == '?'))
            outname[pos] = '_';
	#if defined(_WIN64) || defined(_WIN32)
		const char kForeignPathSeparator ='/';
	#else
		const char kForeignPathSeparator ='\\';
	#endif
    for (int pos = 0; pos<strlen(outname); pos ++)
        if (outname[pos] == kForeignPathSeparator)
        	outname[pos] = kPathSeparator; //e.g. for Windows, convert "/" to "\"
    #else
    for (size_t pos = 0; pos<strlen(outname); pos ++)
        if (outname[pos] == ':') //not allowed by MacOS
        	outname[pos] = '_';
    #endif
    char baseoutname[2048] = {""};
    strcat (baseoutname,pth);
    char appendChar[2] = {"a"};
    appendChar[0] = kPathSeparator;
    if ((strlen(pth) > 0) && (pth[strlen(pth)-1] != kPathSeparator) && (outname[0] != kPathSeparator))
        strcat (baseoutname,appendChar);
	//Allow user to specify new folders, e.g. "-f dir/%p" or "-f %s/%p/%m"
	// These folders are created if they do not exist
    char *sep = strchr(outname, kPathSeparator);
#if defined(USING_R) && (defined(_WIN64) || defined(_WIN32))
    // R also uses forward slash on Windows, so allow it here
    if (!sep)
        sep = strchr(outname, kForeignPathSeparator);
#endif
    if (sep) {
    	char newdir[2048] = {""};
    	strcat (newdir,baseoutname);
    	//struct stat st = {0};
    	for (size_t pos = 0; pos< strlen(outname); pos ++) {
    		if (outname[pos] == kPathSeparator) {
    			//if (stat(newdir, &st) == -1)
    			if (!is_dir(newdir,true))
    			#if defined(_WIN64) || defined(_WIN32)
					mkdir(newdir);
    			#else
					mkdir(newdir, 0700);
    			#endif
    		}
			char ch[12] = {""};
            sprintf(ch,"%c",outname[pos]);
    		strcat (newdir,ch);
    	}
    }
    //printMessage("path='%s' name='%s'\n", pathoutname, outname);
    //make sure outname is unique
    strcat (baseoutname,outname);
    char pathoutname[2048] = {""};
    strcat (pathoutname,baseoutname);
    if ((niiExists(pathoutname)) && (opts.nameConflictBehavior == kNAME_CONFLICT_SKIP)) {
    	printWarning("Skipping existing file named %s\n", pathoutname);
    	return EXIT_FAILURE;
    }
    if ((niiExists(pathoutname)) && (opts.nameConflictBehavior == kNAME_CONFLICT_OVERWRITE)) {
    	printWarning("Overwriting existing file with the name %s\n", pathoutname);
    	niiDelete(pathoutname);
    	strcpy(niiFilename,pathoutname);
    	return EXIT_SUCCESS;
    }
    int i = 0;
    while (niiExists(pathoutname) && (i < 26)) {
        strcpy(pathoutname,baseoutname);
        appendChar[0] = 'a'+i;
        strcat (pathoutname,appendChar);
        i++;
    }
    if (i >= 26) {
        printError("Too many NIFTI images with the name %s\n", baseoutname);
        return EXIT_FAILURE;
    }
    //printMessage("-->%s\n",pathoutname); return EXIT_SUCCESS;
    //printMessage("outname=%s\n", pathoutname);
    strcpy(niiFilename,pathoutname);
    return EXIT_SUCCESS;
} //nii_createFilename()

void  nii_createDummyFilename(char * niiFilename, struct TDCMopts opts) {
    //generate string that illustrating sample of filename
    struct TDICOMdata d = clear_dicom_data();
    strcpy(d.patientName, "John_Doe");
    strcpy(d.patientID, "ID123");
    strcpy(d.accessionNumber, "ID123");
    strcpy(d.imageType,"ORIGINAL");
    strcpy(d.imageComments, "imgComments");
    strcpy(d.studyDate, "1/1/1977");
    strcpy(d.studyTime, "11:11:11");
    strcpy(d.protocolName, "MPRAGE");
    strcpy(d.seriesDescription, "T1_mprage");
    strcpy(d.sequenceName, "T1");
    strcpy(d.scanningSequence, "tfl3d1_ns");
    strcpy(d.sequenceVariant, "tfl3d1_ns");
    strcpy(d.manufacturersModelName, "N/A");
    strcpy(d.procedureStepDescription, "");
    strcpy(d.seriesInstanceUID, "");
    strcpy(d.studyInstanceUID, "");
    strcpy(d.bodyPartExamined,"");
    strcpy(opts.indirParent,"myFolder");
    char niiFilenameBase[PATH_MAX] = {"/usr/myFolder/dicom.dcm"};
    nii_createFilename(d, niiFilenameBase, opts) ;
    strcpy(niiFilename,"Example output filename: '");
    strcat(niiFilename,niiFilenameBase);
    if (opts.isSaveNRRD) {
		if (opts.isGz)
			strcat(niiFilename,".nhdr'");
		else
			strcat(niiFilename,".nrrd'");
    } else {
		if (opts.isGz)
			strcat(niiFilename,".nii.gz'");
		else
			strcat(niiFilename,".nii'");
    }
}// nii_createDummyFilename()

#ifndef myDisableZLib

#ifndef MiniZ
unsigned long mz_compressBound(unsigned long source_len) {
	return compressBound(source_len);
}

unsigned long mz_crc32(unsigned long crc, const unsigned char *ptr, size_t buf_len) {
    return crc32(crc, ptr, (uInt) buf_len);
}
#endif

#ifndef MZ_UBER_COMPRESSION //defined in miniz, not defined in zlib
 #define MZ_UBER_COMPRESSION 9
#endif

#ifndef MZ_DEFAULT_LEVEL
 #define MZ_DEFAULT_LEVEL 6
#endif

void writeNiiGz (char * baseName, struct nifti_1_header hdr,  unsigned char* src_buffer, unsigned long src_len, int gzLevel, bool isSkipHeader) {
    //create gz file in RAM, save to disk http://www.zlib.net/zlib_how.html
    // in general this single-threaded approach is slower than PIGZ but is useful for slow (network attached) disk drives
    char fname[2048] = {""};
    strcpy (fname,baseName);
    if (!isSkipHeader) strcat (fname,".nii.gz");
    unsigned long hdrPadBytes = sizeof(hdr) + 4; //348 byte header + 4 byte pad
    if (isSkipHeader) hdrPadBytes = 0;
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
    int zLevel = MZ_DEFAULT_LEVEL;//Z_DEFAULT_COMPRESSION;
    if ((gzLevel > 0) && (gzLevel < 11))
    	zLevel = gzLevel;
    if (zLevel > MZ_UBER_COMPRESSION)
    	zLevel = MZ_UBER_COMPRESSION;
    if (deflateInit(&strm, zLevel)!= Z_OK) {
        free(pCmp);
        return;
    }
    //unsigned char *pHdr = (unsigned char *)malloc(hdrPadBytes);
    unsigned char *pHdr;
    if (!isSkipHeader) {
		//add header
		pHdr = (unsigned char *)malloc(hdrPadBytes);
		pHdr[hdrPadBytes-1] = 0; pHdr[hdrPadBytes-2] = 0; pHdr[hdrPadBytes-3] = 0; pHdr[hdrPadBytes-4] = 0;
		memcpy(pHdr,&hdr, sizeof(hdr));
		strm.avail_in = (unsigned int)hdrPadBytes; // size of input
		strm.next_in = (uint8_t *)pHdr; // input header -- TPX  strm.next_in = (Bytef *)pHdr; uint32_t
		deflate(&strm, Z_NO_FLUSH);
    }
    //add image
    strm.avail_in = (unsigned int)src_len; // size of input
	strm.next_in = (uint8_t *)src_buffer; // input image -- TPX strm.next_in = (Bytef *)src_buffer;
    deflate(&strm, Z_FINISH); //Z_NO_FLUSH;
    //finish up
    deflateEnd(&strm);
    unsigned long file_crc32 = mz_crc32(0L, Z_NULL, 0);
    if (!isSkipHeader) file_crc32 = mz_crc32(file_crc32, pHdr, (unsigned int)hdrPadBytes);
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
    if (!isSkipHeader) free(pHdr);
} //writeNiiGz()
#endif

#ifdef USING_R

// Version of nii_saveNII() for R/divest: create nifti_image pointer and push onto stack
int nii_saveNII (char *niiFilename, struct nifti_1_header hdr, unsigned char *im, struct TDCMopts opts, struct TDICOMdata d)
{
    hdr.vox_offset = 352;
    // Extract the basename from the full file path
    char *start = niiFilename + strlen(niiFilename);
    while (start >= niiFilename && *start != '/' && *start != kPathSeparator)
        start--;
    std::string name(++start);
    nifti_image *image = nifti_convert_nhdr2nim(hdr, niiFilename);
    if (image == NULL)
        return EXIT_FAILURE;
    image->data = (void *) im;
    ImageList *images = (ImageList *) opts.imageList;
    images->append(image, name);
    free(image);
    return EXIT_SUCCESS;
}

void nii_saveAttributes (struct TDICOMdata &data, struct nifti_1_header &header, struct TDCMopts &opts, const char *filename)
{
    ImageList *images = (ImageList *) opts.imageList;
    switch (data.modality) {
        case kMODALITY_CR:
            images->addAttribute("modality", "CR");
            break;
        case kMODALITY_CT:
            images->addAttribute("modality", "CT");
            break;
        case kMODALITY_MR:
            images->addAttribute("modality", "MR");
            break;
        case kMODALITY_PT:
            images->addAttribute("modality", "PT");
            break;
        case kMODALITY_US:
            images->addAttribute("modality", "US");
            break;
    }
    switch (data.manufacturer) {
        case kMANUFACTURER_SIEMENS:
        	images->addAttribute("manufacturer", "Siemens");
        	break;
        case kMANUFACTURER_GE:
        	images->addAttribute("manufacturer", "GE");
        	break;
        case kMANUFACTURER_PHILIPS:
        	images->addAttribute("manufacturer", "Philips");
        	break;
        case kMANUFACTURER_TOSHIBA:
        	images->addAttribute("manufacturer", "Toshiba");
        	break;
    }
    if (strlen(data.manufacturersModelName) > 0)
        images->addAttribute("scannerModelName", data.manufacturersModelName);
    if (strlen(data.imageType) > 0)
        images->addAttribute("imageType", data.imageType);
    if (data.seriesNum > 0)
        images->addAttribute("seriesNumber", int(data.seriesNum));
    if (strlen(data.seriesDescription) > 0)
        images->addAttribute("seriesDescription", data.seriesDescription);
    if (strlen(data.sequenceName) > 0)
        images->addAttribute("sequenceName", data.sequenceName);
    if (strlen(data.protocolName) > 0)
        images->addAttribute("protocolName", data.protocolName);
    if (strlen(data.studyDate) >= 8 && strcmp(data.studyDate,"00000000") != 0)
        images->addDateAttribute("studyDate", data.studyDate);
    if (strlen(data.studyTime) > 0 && strncmp(data.studyTime,"000000",6) != 0)
        images->addAttribute("studyTime", data.studyTime);
    if (data.fieldStrength > 0.0)
        images->addAttribute("fieldStrength", data.fieldStrength);
    if (data.flipAngle > 0.0)
        images->addAttribute("flipAngle", data.flipAngle);
    if (data.TE > 0.0)
        images->addAttribute("echoTime", data.TE);
    if (data.TR > 0.0)
        images->addAttribute("repetitionTime", data.TR);
    if (data.TI > 0.0)
        images->addAttribute("inversionTime", data.TI);
    if (!data.isXRay) {
        if (data.zThick > 0.0)
            images->addAttribute("sliceThickness", data.zThick);
        if (data.zSpacing > 0.0)
            images->addAttribute("sliceSpacing", data.zSpacing);
    }
    if (data.CSA.multiBandFactor > 1)
        images->addAttribute("multibandFactor", data.CSA.multiBandFactor);
    if (data.phaseEncodingSteps > 0)
        images->addAttribute("phaseEncodingSteps", data.phaseEncodingSteps);
    if (data.phaseEncodingLines > 0)
        images->addAttribute("phaseEncodingLines", data.phaseEncodingLines);

    // Calculations relating to the reconstruction in the phase encode direction,
    // which are needed to derive effective echo spacing and readout time below.
    // See the nii_SaveBIDS() function for details
    int reconMatrixPE = data.phaseEncodingLines;
    if ((header.dim[2] > 0) && (header.dim[1] > 0)) {
        if (header.dim[2] == header.dim[2]) //phase encoding does not matter
            reconMatrixPE = header.dim[2];
        else if (data.phaseEncodingRC =='R')
            reconMatrixPE = header.dim[2];
        else if (data.phaseEncodingRC =='C')
            reconMatrixPE = header.dim[1];
    }

    double bandwidthPerPixelPhaseEncode = data.bandwidthPerPixelPhaseEncode;
    if (bandwidthPerPixelPhaseEncode == 0.0)
        bandwidthPerPixelPhaseEncode = data.CSA.bandwidthPerPixelPhaseEncode;
    double effectiveEchoSpacing = 0.0;
    if ((reconMatrixPE > 0) && (bandwidthPerPixelPhaseEncode > 0.0))
        effectiveEchoSpacing = 1.0 / (bandwidthPerPixelPhaseEncode * reconMatrixPE);
    if (data.effectiveEchoSpacingGE > 0.0)
        effectiveEchoSpacing = data.effectiveEchoSpacingGE / 1000000.0;

    if (effectiveEchoSpacing > 0.0)
        images->addAttribute("effectiveEchoSpacing", effectiveEchoSpacing);
    if ((reconMatrixPE > 0) && (effectiveEchoSpacing > 0.0))
        images->addAttribute("effectiveReadoutTime", effectiveEchoSpacing * (reconMatrixPE - 1.0));
    if (data.pixelBandwidth > 0.0)
        images->addAttribute("pixelBandwidth", data.pixelBandwidth);
    if ((data.manufacturer == kMANUFACTURER_SIEMENS) && (data.dwellTime > 0))
        images->addAttribute("dwellTime", data.dwellTime * 1e-9);

    // Phase encoding polarity
    // We only save these attributes if both direction and polarity are known
    if (((data.phaseEncodingRC == 'R') || (data.phaseEncodingRC == 'C')) &&  (!data.is3DAcq) && ((data.CSA.phaseEncodingDirectionPositive == 1) || (data.CSA.phaseEncodingDirectionPositive == 0))) {
        if (data.phaseEncodingRC == 'C') {
            images->addAttribute("phaseEncodingDirection", "j");
            // Notice the XOR (^): the sense of phaseEncodingDirectionPositive
            // is reversed if we are flipping the y-axis
            images->addAttribute("phaseEncodingSign", ((data.CSA.phaseEncodingDirectionPositive == 0) ^ opts.isFlipY) ? -1 : 1);
        }
        else if (data.phaseEncodingRC == 'R') {
            images->addAttribute("phaseEncodingDirection", "i");
            images->addAttribute("phaseEncodingSign", data.CSA.phaseEncodingDirectionPositive == 0 ? -1 : 1);
        }
    }
    
    // Slice timing (stored in seconds)
    if (data.CSA.sliceTiming[0] >= 0.0 && (data.manufacturer == kMANUFACTURER_UIH || data.manufacturer == kMANUFACTURER_GE || (data.manufacturer == kMANUFACTURER_SIEMENS && !data.isXA10A))) {
        std::vector<double> sliceTimes;
        for (int i=0; i<header.dim[3]; i++) {
            if (data.CSA.sliceTiming[i] < 0.0)
                break;
            sliceTimes.push_back(data.CSA.sliceTiming[i] / 1000.0);
        }
        images->addAttribute("sliceTiming", sliceTimes);
    }

    if (strlen(data.patientID) > 0)
        images->addAttribute("patientIdentifier", data.patientID);
    if (strlen(data.patientName) > 0)
        images->addAttribute("patientName", data.patientName);
    if (strlen(data.patientBirthDate) >= 8 && strcmp(data.patientBirthDate,"00000000") != 0)
        images->addDateAttribute("patientBirthDate", data.patientBirthDate);
    if (strlen(data.patientAge) > 0 && strcmp(data.patientAge,"000Y") != 0)
        images->addAttribute("patientAge", data.patientAge);
    if (data.patientSex == 'F')
        images->addAttribute("patientSex", "F");
    else if (data.patientSex == 'M')
        images->addAttribute("patientSex", "M");
    if (data.patientWeight > 0.0)
        images->addAttribute("patientWeight", data.patientWeight);
    if (strlen(data.imageComments) > 0)
        images->addAttribute("comments", data.imageComments);
}

#else

int pigz_File(char * fname, struct TDCMopts opts, size_t imgsz) {
	//given "/dir/file.nii" creates "/dir/file.nii.gz"
	char blockSize[768];
	strcpy(blockSize, "");
	//-b 960 increases block size from 128 to 960: each block has 32kb lead in... so less redundancy
	if (imgsz > 1000000) strcpy(blockSize, " -b 960");
	char command[768];
	strcpy(command, "\"" );
	strcat(command, opts.pigzname );
	if ((opts.gzLevel > 0) &&  (opts.gzLevel < 12)) {
		char newstr[256];
		sprintf(newstr, "\"%s -n -f -%d \"", blockSize, opts.gzLevel);
		strcat(command, newstr);
	} else {
		char newstr[256];
		sprintf(newstr, "\"%s -n \"", blockSize);
		strcat(command, newstr);
	}
	strcat(command, fname);
	strcat(command, "\""); //add quotes in case spaces in filename 'pigz "c:\my dir\img.nii"'
	#if defined(_WIN64) || defined(_WIN32) //using CreateProcess instead of system to run in background (avoids screen flicker)
		DWORD exitCode;
	PROCESS_INFORMATION ProcessInfo = {0};
	STARTUPINFO startupInfo= {0};
	startupInfo.cb = sizeof(startupInfo);
		//StartupInfo.cb = sizeof StartupInfo ; //Only compulsory field
		if(CreateProcess(NULL, command, NULL,NULL,FALSE,NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW,NULL, NULL,&startupInfo,&ProcessInfo)) {
			//printMessage("compression --- %s\n",command);
			WaitForSingleObject(ProcessInfo.hProcess,INFINITE);
			CloseHandle(ProcessInfo.hThread);
			CloseHandle(ProcessInfo.hProcess);
		} else
			printMessage("Compression failed %s\n",command);
	#else //if win else linux
	int ret = system(command);
	if (ret == -1)
		printWarning("Failed to execute: %s\n",command);
	#endif //else linux
	printMessage("Compress: %s\n",command);
    return EXIT_SUCCESS;
} // pigz_File()

int nii_saveNRRD(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts, struct TDICOMdata d, struct TDTI4D *dti4D, int numDTI) {
	int n, nDim = hdr.dim[0];
    printMessage("NRRD writer is experimental\n");
    if (nDim < 1) return EXIT_FAILURE;
    bool isGz = opts.isGz;
	size_t imgsz = nii_ImgBytes(hdr);
	if  ((isGz) && (imgsz >=  2147483647)) {
		printWarning("Saving huge image uncompressed (many GZip tools have 2 Gb limit).\n");
		isGz = false;
	}
	char fname[2048] = {""};
    strcpy (fname, niiFilename);
    if (isGz)
    	strcat (fname,".nhdr"); //nrrd or nhdr
    else
    	strcat (fname,".nrrd"); //nrrd or nhdr
	FILE *fp = fopen(fname, "w");
    fprintf(fp,"NRRD0005\n");
    fprintf(fp,"# Complete NRRD file format specification at:\n");
    fprintf(fp,"# http://teem.sourceforge.net/nrrd/format.html\n");
    fprintf(fp,"# dcm2niix %s NRRD export transforms by Tashrif Billah\n", kDCMdate);
    char rgbNoneStr[10] = {""};
	//type tag
    switch (hdr.datatype) {
		case DT_RGB24:
        	fprintf(fp,"type: uint8\n");
        	strcpy (rgbNoneStr, " none");
            break;
		case DT_UINT8:
        	fprintf(fp,"type: uint8\n");
            break;
        case DT_INT16:
            fprintf(fp,"type: int16\n");
            break;
        case DT_UINT16:
			fprintf(fp,"type: uint16\n");
            break;
        case DT_FLOAT32:
			fprintf(fp,"type: float\n");
            break;
        case DT_INT32:
			fprintf(fp,"type: int32\n");
            break;
        case DT_FLOAT64:
			fprintf(fp,"type: double\n");
            break;
        default:
        	printError("Unknown NRRD datatype %d\n", hdr.datatype);
        	fclose(fp);
    		return EXIT_FAILURE;
    }
    //dimension tag
    if (hdr.datatype == DT_RGB24)
    	fprintf(fp,"dimension: %d\n", nDim+1); //RGB is first dimension
    else
    	fprintf(fp,"dimension: %d\n", nDim);
    //space tag
	fprintf(fp,"space: right-anterior-superior\n");
	//sizes tag
	fprintf(fp,"sizes:");
	if (hdr.datatype == DT_RGB24) fprintf(fp," 3");
	for (int i = 1; i <= hdr.dim[0]; i++)
		fprintf(fp," %d", hdr.dim[i]);
    fprintf(fp,"\n");
    //thicknesses
    if ((d.zThick > 0.0) && (nDim >= 3)) {
    	fprintf(fp,"thicknesses:  NaN  NaN %g", d.zThick);
    	int n = 3;
		while (n < nDim ) {
	 		fprintf(fp," NaN");
	 		n ++;
		}
    	fprintf(fp,"\n");
    }
    //byteskip only for .nhdr, not .nrrd
	if (littleEndianPlatform()) //raw data in native format
		fprintf(fp,"endian: little\n");
	else
		fprintf(fp,"endian: big\n");
    if (isGz) {
    	fprintf(fp,"encoding: gzip\n");
    	strcpy (fname, niiFilename);
    	strcat (fname,".raw.gz");
    	char basefname[2048] = {""};
    	getFileNameX(basefname, fname, 2048);
    	fprintf(fp,"data file: %s\n", basefname);
    } else
    	fprintf(fp,"encoding: raw\n");
	fprintf(fp,"space units: \"mm\" \"mm\" \"mm\"\n");
	//origin
	fprintf(fp,"space origin: (%g,%g,%g)\n", hdr.srow_x[3],hdr.srow_y[3],hdr.srow_z[3]);
	//space directions:
	fprintf(fp,"space directions:%s (%g,%g,%g) (%g,%g,%g) (%g,%g,%g)", rgbNoneStr, hdr.srow_x[0],hdr.srow_y[0],hdr.srow_z[0],
		hdr.srow_x[1],hdr.srow_y[1],hdr.srow_z[1], hdr.srow_x[2],hdr.srow_y[2],hdr.srow_z[2] );
	n = 3;
	while (n < nDim ) {
	 fprintf(fp," none");
	 n ++;
	}
	fprintf(fp,"\n");
	//centerings tag
	if (hdr.dim[0] < 4) //*check RGB, more dims
		fprintf(fp,"centerings:%s cell cell cell\n", rgbNoneStr);
	else
		fprintf(fp,"centerings:%s cell cell cell ???\n", rgbNoneStr);
	//kinds tag
	fprintf(fp,"kinds:");
	if (hdr.datatype == DT_RGB24) fprintf(fp," RGB-color");
	n = 0;
	while ((n < nDim ) && (n < 3)) {
	 fprintf(fp," space"); //dims 1..3
	 n ++;
	}
	while (n < nDim ) {
	 fprintf(fp," list"); //dims 4..7
	 n ++;
	}
	fprintf(fp,"\n");
	//http://teem.sourceforge.net/nrrd/format.html
	bool isFloat = (hdr.datatype == DT_FLOAT64) || (hdr.datatype == DT_FLOAT32);
	if (((!isSameFloat(hdr.scl_inter, 0.0)) || (!isSameFloat(hdr.scl_slope, 1.0))) && (!isFloat)) {
		//http://teem.sourceforge.net/nrrd/format.html
		double dtMin = 0.0; //DT_UINT8, DT_RGB24, DT_UINT16
		if (hdr.datatype == DT_INT16) dtMin = -32768.0;
		if (hdr.datatype == DT_INT32) dtMin = -2147483648.0;
		fprintf(fp,"oldmin: %8.8f\n", (dtMin * hdr.scl_slope)  + hdr.scl_inter);
		double dtMax = 255.00; //DT_UINT8, DT_RGB24
		if (hdr.datatype == DT_INT16) dtMax = 32767.0;
		if (hdr.datatype == DT_UINT16) dtMax = 65535.0;
		if (hdr.datatype == DT_INT32) dtMax = 2147483647.0;
		fprintf(fp,"oldmax: %8.8f\n", (dtMax * hdr.scl_slope)  + hdr.scl_inter);
	}
	//Slicer DWIconvert values
	if (d.modality == kMODALITY_MR)  fprintf(fp,"DICOM_0008_0060_Modality:=MR\n");
	if (d.modality == kMODALITY_CT)  fprintf(fp,"DICOM_0008_0060_Modality:=CT\n");
	if (d.manufacturer == kMANUFACTURER_SIEMENS) fprintf(fp,"DICOM_0008_0070_Manufacturer:=SIEMENS\n");
	if (d.manufacturer == kMANUFACTURER_PHILIPS) fprintf(fp,"DICOM_0008_0070_Manufacturer:=Philips Medical Systems\n");
	if (d.manufacturer == kMANUFACTURER_GE) fprintf(fp,"DICOM_0008_0070_Manufacturer:=GE MEDICAL SYSTEMS\n");
	if (strlen(d.manufacturersModelName) > 0)  fprintf(fp,"DICOM_0008_1090_ManufacturerModelName:=%s\n",d.manufacturersModelName);
	if (strlen(d.scanOptions) > 0)  fprintf(fp,"DICOM_0018_0022_ScanOptions:=%s\n",d.scanOptions);
	if (d.is2DAcq)  fprintf(fp,"DICOM_0018_0023_MRAcquisitionType:=2D\n");
	if (d.is3DAcq)  fprintf(fp,"DICOM_0018_0023_MRAcquisitionType:=3D\n");
	//if (strlen(d.mrAcquisitionType) > 0)  fprintf(fp,"DICOM_0018_0023_MRAcquisitionType:=%s\n",d.mrAcquisitionType);
	if (d.TR > 0.0) fprintf(fp,"DICOM_0018_0080_RepetitionTime:=%g\n",d.TR);
	if ((d.TE > 0.0) && (!d.isXRay)) fprintf(fp,"DICOM_0018_0081_EchoTime:=%g\n",d.TE);
	if ((d.TE > 0.0) && (d.isXRay)) fprintf(fp,"DICOM_0018_1152_XRayExposure:=%g\n",d.TE);
	if (d.numberOfAverages > 0.0) fprintf(fp,"DICOM_0018_0083_NumberOfAverages:=%g\n",d.numberOfAverages);
	if (d.fieldStrength > 0.0) fprintf(fp,"DICOM_0018_0087_MagneticFieldStrength:=%g\n",d.fieldStrength);
	if (strlen(d.softwareVersions) > 0)  fprintf(fp,"DICOM_0018_1020_SoftwareVersions:=%s\n",d.softwareVersions);
	if (d.flipAngle > 0.0) fprintf(fp,"DICOM_0018_1314_FlipAngle:=%g\n",d.flipAngle);
	//multivolume but NOT DTI, e.g. fMRI/DCE see https://www.slicer.org/wiki/Documentation/4.4/Modules/MultiVolumeExplorer
	//  https://github.com/QIICR/PkModeling/blob/master/PkSolver/IO/MultiVolumeMetaDictReader.cxx#L34-L58
	//  for "MultiVolume.FrameLabels:="
	//    https://www.slicer.org/wiki/Documentation/4.4/Modules/MultiVolumeExplorer
	//  for "axis 0 index values:="
	//  https://github.com/mhe/pynrrd/issues/71
	// "I don't know if it is a good idea for dcm2niix to mimic Slicer converter tags" Andrey Fedorov
	/*
	if ((nDim > 3) && (hdr.dim[4] > 1) && (numDTI < 1)) {
		if ((d.TE > 0.0) && (!d.isXRay)) fprintf(fp,"MultiVolume.DICOM.EchoTime:=%g\n",d.TE);
		if (d.flipAngle > 0.0) fprintf(fp,"MultiVolume.DICOM.FlipAngle:=%g\n",d.flipAngle);
		if (d.TR > 0.0) fprintf(fp,"MultiVolume.DICOM.RepetitionTime:=%g\n",d.TR);
		fprintf(fp,"MultiVolume.FrameIdentifyingDICOMTagName:=TriggerTime\n");
		fprintf(fp,"MultiVolume.FrameIdentifyingDICOMTagUnits:=ms\n");
		fprintf(fp,"MultiVolume.FrameLabels:=");
		double frameTime = d.TR;
		if (d.triggerDelayTime > 0.0)
			frameTime = d.triggerDelayTime / (hdr.dim[4] - 1); //GE dce data
		for (int i = 0; i < (hdr.dim[4]-1); i++)
			fprintf(fp,"%g,", i * frameTime);
    	fprintf(fp,"%g\n", (hdr.dim[4]-1) * frameTime);
		fprintf(fp,"MultiVolume.NumberOfFrames:=%d\n",hdr.dim[4]);
	} */
	//DWI values
	if ((nDim > 3) && (numDTI > 0) && (numDTI < kMaxDTI4D)) {
		mat33 inv;
		LOAD_MAT33(inv, hdr.pixdim[1],0.0,0.0,  0.0,hdr.pixdim[2],0.0,  0.0, 0.0,hdr.pixdim[3]);
		inv = nifti_mat33_inverse(inv);
		mat33 s;
		LOAD_MAT33(s,hdr.srow_x[0],hdr.srow_x[1],hdr.srow_x[2],
			hdr.srow_y[0],hdr.srow_y[1],hdr.srow_y[2],
			hdr.srow_z[0],hdr.srow_z[1],hdr.srow_z[2]);
		mat33 mf = nifti_mat33_mul(inv, s);
		fprintf(fp,"measurement frame: (%g,%g,%g) (%g,%g,%g) (%g,%g,%g)\n",
			mf.m[0][0],mf.m[1][0],mf.m[2][0],
			mf.m[0][1],mf.m[1][1],mf.m[2][1],
			mf.m[0][2],mf.m[1][2],mf.m[2][2]);
		//modality tag
		fprintf(fp,"modality:=DWMRI\n");
		float b_max = 0.0;
		for (int i = 0; i < numDTI; i++)
			if (dti4D->S[i].V[0] > b_max)
				b_max = dti4D->S[i].V[0];
		fprintf(fp,"DWMRI_b-value:=%g\n", b_max);
		//gradient tag, e.g. DWMRI_gradient_0000:=0.0   0.0   0.0
		for (int i = 0; i < numDTI; i++) {
			float factor = 0.0;
			if (b_max > 0) factor = sqrt(dti4D->S[i].V[0]/b_max);
			if ( (dti4D->S[i].V[0] > 50.0) && (isSameFloatGE(0.0, dti4D->S[i].V[1])) && (isSameFloatGE(0.0, dti4D->S[i].V[2])) && (isSameFloatGE(0.0, dti4D->S[i].V[3]))  ) {
				//On May 2, 2019, at 10:47 AM, Gordon L. Kindlmann <> wrote:
				//(assuming b_max  2000, we write "isotropic" for the b=2000 isotropic image, and specify the b-value if it is an isotropic image but not b-bax
				// DWMRI_gradient_0003:=isotropic b=1000
				// DWMRI_gradient_0004:=isotropic
				if (isSameFloatGE(b_max, dti4D->S[i].V[0]))
					fprintf(fp,"DWMRI_gradient_%04d:=isotropic\n", i);
				else
					fprintf(fp,"DWMRI_gradient_%04d:=isotropic b=%g\n", i, dti4D->S[i].V[0]);
			} else
				fprintf(fp,"DWMRI_gradient_%04d:=%.17g %.17g %.17g\n", i, factor*dti4D->S[i].V[1], factor*dti4D->S[i].V[2], factor*dti4D->S[i].V[3]);
			//printf("%g =  %g %g %g>>>>\n",dti4D->S[i].V[0],  dti4D->S[i].V[1],dti4D->S[i].V[2],dti4D->S[i].V[3]);

		}
	}
	fprintf(fp,"\n"); //blank line: end of NRRD header
	if (!isGz) fwrite(&im[0], imgsz, 1, fp);
	fclose(fp);
    if (!isGz) return EXIT_SUCCESS;
    //below: gzip file
    if  (strlen(opts.pigzname)  < 1) { //internal compression
    	writeNiiGz (fname, hdr, im, imgsz, opts.gzLevel, true);
    	return EXIT_SUCCESS;
    }
	//below pigz
	strcpy (fname, niiFilename); //without gz
	strcat (fname,".raw");
    fp = fopen(fname, "wb");
    fwrite(&im[0], imgsz, 1, fp);
    fclose(fp);
    return pigz_File(fname, opts, imgsz);
} // nii_saveNRRD()

void swapEndian(struct nifti_1_header* hdr, unsigned char* im, bool isNative) {
	//swap endian from big->little or little->big
	// must be told which is native to detect datatype and number of voxels
	// one could also auto-detect: hdr->sizeof_hdr==348
	if (!isNative) swap_nifti_header(hdr);
	int nVox = 1;
    for (int i = 1; i < 8; i++)
        if (hdr->dim[i] > 1) nVox = nVox * hdr->dim[i];
	int bitpix = hdr->bitpix;
	int datatype = hdr->datatype;
	if (isNative) swap_nifti_header(hdr);
	if (datatype == DT_RGBA32) return;
	//n.b. do not swap 8-bit, 24-bit RGB, and 32-bit RGBA
	if (bitpix == 16) nifti_swap_2bytes(nVox, im);
	if (bitpix == 32) nifti_swap_4bytes(nVox, im);
	if (bitpix == 64) nifti_swap_8bytes(nVox, im);
}

int nii_saveNII(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts, struct TDICOMdata d) {
    if (opts.isOnlyBIDS) return EXIT_SUCCESS;
    if (opts.isSaveNRRD) {
		struct TDTI4D dti4D;
    	return nii_saveNRRD(niiFilename, hdr, im, opts, d, &dti4D, 0);
    }
    hdr.vox_offset = 352;
    size_t imgsz = nii_ImgBytes(hdr);
    if (imgsz < 1) {
    	printMessage("Error: Image size is zero bytes %s\n", niiFilename);
    	return EXIT_FAILURE;
    }
    #ifndef myDisableGzSizeLimits
		//see https://github.com/rordenlab/dcm2niix/issues/124
		uint64_t  kMaxPigz  = 4294967264;
		//https://stackoverflow.com/questions/5272825/detecting-64bit-compile-in-c
		#ifndef UINTPTR_MAX
		uint64_t  kMaxGz = 2147483647;
		#elif UINTPTR_MAX == 0xffffffff
		uint64_t  kMaxGz = 2147483647;
		#elif UINTPTR_MAX == 0xffffffffffffffff
		uint64_t  kMaxGz = kMaxPigz;
		#else
		compiler error: unable to determine is 32 or 64 bit
		#endif
		#ifndef myDisableZLib
		if  ((opts.isGz) &&  (strlen(opts.pigzname)  < 1) &&  ((imgsz+hdr.vox_offset) >=  kMaxGz) ) { //use internal compressor
			printWarning("Saving uncompressed data: internal compressor unable to process such large files.\n");
			if ((imgsz+hdr.vox_offset) <  kMaxPigz)
				printWarning(" Hint: using external compressor (pigz) should help.\n");
		} else if  ((opts.isGz) &&  (strlen(opts.pigzname)  < 1) &&  ((imgsz+hdr.vox_offset) <  kMaxGz) ) { //use internal compressor
			if (!opts.isSaveNativeEndian) swapEndian(&hdr, im, true); //byte-swap endian (e.g. little->big)    	
			writeNiiGz (niiFilename, hdr,  im, imgsz, opts.gzLevel, false);
			if (!opts.isSaveNativeEndian) swapEndian(&hdr, im, false); //unbyte-swap endian (e.g. big->little)
			return EXIT_SUCCESS;
		}
		#endif
    #endif
    char fname[2048] = {""};
    strcpy (fname,niiFilename);
    strcat (fname,".nii");
	#if defined(_WIN64) || defined(_WIN32)
	if ((opts.isGz) && (opts.isPipedGz))
    	printWarning("The 'optimal' piped gz is only available for Unix\n");
	#else //if windows else Unix
	if ((opts.isGz) && (opts.isPipedGz) && (strlen(opts.pigzname)  > 0) ) {
		//piped gz
    	printMessage(" Optimal piped gz will fail if pigz version < 2.3.4.\n");
    	char command[768];
    	strcpy(command, "\"" );
    	strcat(command, opts.pigzname );
    	if ((opts.gzLevel > 0) &&  (opts.gzLevel < 12)) {
        	char newstr[256];
        	sprintf(newstr, "\" -n -f -%d > \"", opts.gzLevel);
        	strcat(command, newstr);
    	} else
        	strcat(command, "\" -n -f > \""); //current versions of pigz (2.3) built on Windows can hang if the filename is included, presumably because it is not finding the path characters ':\'
    	strcat(command, fname);
    	strcat(command, ".gz\""); //add quotes in case spaces in filename 'pigz "c:\my dir\img.nii"'
		//strcat(command, "x.gz\""); //add quotes in case spaces in filename 'pigz "c:\my dir\img.nii"'
        if (opts.isVerbose)
        	printMessage("Compress: %s\n",command);
    	FILE *pigzPipe;
		if (( pigzPipe = popen(command, "w")) == NULL) {
    		printError("Unable to open pigz pipe\n");
        	return EXIT_FAILURE;
    	}
    	if (!opts.isSaveNativeEndian) swapEndian(&hdr, im, true); //byte-swap endian (e.g. little->big)
    	fwrite(&hdr, sizeof(hdr), 1, pigzPipe);
    	uint32_t pad = 0;
    	fwrite(&pad, sizeof( pad), 1, pigzPipe);
    	fwrite(&im[0], imgsz, 1, pigzPipe);
    	pclose(pigzPipe);
    	if (!opts.isSaveNativeEndian) swapEndian(&hdr, im, false); //unbyte-swap endian (e.g. big->little)
		return EXIT_SUCCESS;
    }
	#endif
    FILE *fp = fopen(fname, "wb");
    if (!fp) return EXIT_FAILURE;
    if (!opts.isSaveNativeEndian) swapEndian(&hdr, im, true); //byte-swap endian (e.g. little->big)
    fwrite(&hdr, sizeof(hdr), 1, fp);
    uint32_t pad = 0;
    fwrite(&pad, sizeof( pad), 1, fp);
    fwrite(&im[0], imgsz, 1, fp);
    fclose(fp);
    if (!opts.isSaveNativeEndian) swapEndian(&hdr, im, false); //unbyte-swap endian (e.g. big->little)
    if ((opts.isGz) &&  (strlen(opts.pigzname)  > 0) ) {
    	#ifndef myDisableGzSizeLimits
    	if ((imgsz+hdr.vox_offset) >  kMaxPigz) {
        	printWarning("Saving uncompressed data: image too large for pigz.\n");
    		return EXIT_SUCCESS;
    	}
    	#endif
    	return pigz_File(fname, opts, imgsz);
    }
    return EXIT_SUCCESS;
}// nii_saveNII()

#endif

int nii_saveNIIx(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts) {
	struct TDICOMdata dcm = clear_dicom_data();
	return nii_saveNII(niiFilename, hdr, im, opts, dcm);
}

int nii_saveNII3D(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts, struct TDICOMdata d) {
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
    char zeroPad[PATH_MAX] = {""};
	double fnVol = nVol;
	int zeroPadLen = (1 + log10( fnVol));
    sprintf(zeroPad,"%%s_%%0%dd",zeroPadLen);
    for (int i = 1; i <= nVol; i++) {
        sprintf(fname,zeroPad,niiFilename,i);
        if (nii_saveNII(fname, hdr1, (unsigned char*)&im[pos], opts, d) == EXIT_FAILURE) return EXIT_FAILURE;
        pos += imgsz;
    }
    return EXIT_SUCCESS;
}// nii_saveNII3D()

/*
//this version can convert INT16->UINT16
// some were concerned about this https://github.com/rordenlab/dcm2niix/issues/198
void nii_scale16bitSigned(unsigned char *img, struct nifti_1_header *hdr){
    if (hdr->datatype != DT_INT16) return;
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    int nVox = hdr->dim[1]*hdr->dim[2]* dim3to7;
    if (nVox < 1) return;
    int16_t * img16 = (int16_t*) img;
    int16_t max16 = img16[0];
    int16_t min16 = max16;
    //clock_t start = clock();
    for (int i=0; i < nVox; i++) {
        if (img16[i] < min16)
            min16 = img16[i];
        if (img16[i] > max16)
            max16 = img16[i];
    }
    int kMx = 32000; //actually 32767 - maybe a bit of padding for interpolation ringing
    bool isConvertToUint16 = true; //if false output is always same as input: INT16, if true and no negative values output will be UINT16
    if ((isConvertToUint16) && (min16 >= 0))
    	kMx = 64000;
    int scale = kMx / (int)max16;
	if (abs(min16) > max16)
		scale = kMx / (int)abs(min16);
	if (scale < 2) return; //already uses dynamic range
    hdr->scl_slope = hdr->scl_slope/ scale;
	if ((isConvertToUint16) && (min16 >= 0)) { //only positive values: save as UINT16 0..65535
		hdr->datatype = DT_UINT16;
		uint16_t * uimg16 = (uint16_t*) img;
    	for (int i=0; i < nVox; i++)
    		uimg16[i] = (int)img16[i] * scale;
	} else {//includes negative values: save as INT16 -32768..32768
		for (int i=0; i < nVox; i++)
    		img16[i] = img16[i] * scale;
	}
    printMessage("Maximizing 16-bit range: raw %d..%d\n", min16, max16);
}*/

void nii_storeIntegerScaleFactor(int scale, struct nifti_1_header *hdr) {
//appends NIfTI header description field with " isN" where N is integer scaling
	char newstr[256];
	sprintf(newstr, " is%d", scale);
	if ((strlen(newstr)+strlen(hdr->descrip)) < 80)
		strcat (hdr->descrip,newstr);
}

void nii_mask12bit(unsigned char *img, struct nifti_1_header *hdr) {
//https://github.com/rordenlab/dcm2niix/issues/251
    if (hdr->datatype != DT_INT16) return;
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    int nVox = hdr->dim[1]*hdr->dim[2]* dim3to7;
    if (nVox < 1) return;
	int16_t * img16 = (int16_t*) img;
    for (int i=0; i < nVox; i++)
    	img16[i] = img16[i] & 4095; //12 bit data ranges from 0..4095, any other values are overflow
}

void nii_scale16bitSigned(unsigned char *img, struct nifti_1_header *hdr, int isVerbose) {
//lossless scaling of INT16 data: e.g. input with range -100...3200 and scl_slope=1
//  will be stored as -1000...32000 with scl_slope 0.1
    if (hdr->datatype != DT_INT16) return;
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    int nVox = hdr->dim[1]*hdr->dim[2]* dim3to7;
    if (nVox < 1) return;
    int16_t * img16 = (int16_t*) img;
    int16_t max16 = img16[0];
    int16_t min16 = max16;
    for (int i=0; i < nVox; i++) {
        if (img16[i] < min16)
            min16 = img16[i];
        if (img16[i] > max16)
            max16 = img16[i];
    }
    int kMx = 32000; //actually 32767 - maybe a bit of padding for interpolation ringing
    int scale = kMx / (int)max16;
	if (abs(min16) > max16)
		scale = kMx / (int)abs(min16);
	if (scale < 2) {
    	if (isVerbose)
    		printMessage("Sufficient 16-bit range: raw %d..%d\n", min16, max16);
		return; //already uses dynamic range
	}
    hdr->scl_slope = hdr->scl_slope/ scale;
	for (int i=0; i < nVox; i++)
    	img16[i] = img16[i] * scale;
    printMessage("Maximizing 16-bit range: raw %d..%d is%d\n", min16, max16, scale);
    nii_storeIntegerScaleFactor(scale, hdr);
}

void nii_scale16bitUnsigned(unsigned char *img, struct nifti_1_header *hdr, int isVerbose){
//lossless scaling of UINT16 data: e.g. input with range 0...3200 and scl_slope=1
//  will be stored as 0...64000 with scl_slope 0.05
    if (hdr->datatype != DT_UINT16) return;
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    int nVox = hdr->dim[1]*hdr->dim[2]* dim3to7;
    if (nVox < 1) return;
    uint16_t * img16 = (uint16_t*) img;
    uint16_t max16 = img16[0];
    for (int i=0; i < nVox; i++)
        if (img16[i] > max16)
            max16 = img16[i];
    int kMx = 64000; //actually 65535 - maybe a bit of padding for interpolation ringing
    int scale = kMx / (int)max16;
	if (scale < 2)  {
    	if (isVerbose > 0)
    		printMessage("Sufficient unsigned 16-bit range: raw max %d\n",  max16);
		return; //already uses dynamic range
	}
	hdr->scl_slope = hdr->scl_slope/ scale;
	for (int i=0; i < nVox; i++)
    	img16[i] = img16[i] * scale;
    printMessage("Maximizing 16-bit range: raw max %d is%d\n", max16, scale);
    nii_storeIntegerScaleFactor(scale, hdr);
}

#define UINT16_TO_INT16_IF_LOSSLESS
#ifdef UINT16_TO_INT16_IF_LOSSLESS
void nii_check16bitUnsigned(unsigned char *img, struct nifti_1_header *hdr, int isVerbose){
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
    //printMessage("max16= %d vox=%d %fms\n",max16, nVox, ((double)(clock()-start))/1000);
    if (max16 > 32767) {
        if (isVerbose > 0)
        	printMessage("Note: rare 16-bit UNSIGNED integer image. Older tools may require 32-bit conversion\n");
    }
    else
        hdr->datatype = DT_INT16;
} //nii_check16bitUnsigned()
#else
void nii_check16bitUnsigned(unsigned char *img, struct nifti_1_header *hdr, int isVerbose){
    if (hdr->datatype != DT_UINT16) return;
	if (isVerbose < 1) return;
	printMessage("Note: rare 16-bit UNSIGNED integer image. Older tools may require 32-bit conversion\n");
}
#endif

//void reportPos(struct TDICOMdata d1) {
//	printMessage("Instance\t%d\t0020,0032\t%g\t%g\t%g\n", d1.imageNum, d1.patientPosition[1],d1.patientPosition[2],d1.patientPosition[3]);
//}

int siemensCtKludge(int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[]) {
    //Siemens CT bug: when a user draws an open object graphics object onto a 2D slice this is appended as an additional image,
    //regardless of slice position. These images do not report number of positions in the volume, so we need tedious leg work to detect
    uint64_t indx0 = dcmSort[0].indx;
    if ((nConvert < 2) ||(dcmList[indx0].manufacturer != kMANUFACTURER_SIEMENS) || (!isSameFloat(dcmList[indx0].TR ,0.0f))) return nConvert;
    float prevDx = 0.0;
    for (int i = 1; i < nConvert; i++) {
        float dx = intersliceDistance(dcmList[indx0],dcmList[dcmSort[i].indx]);
        if ((!isSameFloat(dx,0.0f)) && (dx < prevDx)) {
            //for (int j = 1; j < nConvert; j++)
            //	reportPos(dcmList[dcmSort[j].indx]);
            printMessage("Slices skipped: image position not sequential, admonish your vendor (Siemens OOG?)\n");
            return i;
        }
        prevDx = dx;
    }
    return nConvert; //all images in sequential order
}// siemensCtKludge()

int isSameFloatT (float a, float b, float tolerance) {
    return (fabs (a - b) <= tolerance);
}

void adjustOriginForNegativeTilt(struct nifti_1_header * hdr, float shiftPxY) {
    if (hdr->sform_code > 0) {
        // Adjust the srow_*  offsets using srow_y
        hdr->srow_x[3] -= shiftPxY * hdr->srow_y[0];
        hdr->srow_y[3] -= shiftPxY * hdr->srow_y[1];
        hdr->srow_z[3] -= shiftPxY * hdr->srow_y[2];
    }

    if (hdr->qform_code > 0) {
        // Adjust the quaternion offsets using quatern_* and pixdim
        mat44 mat = nifti_quatern_to_mat44(hdr->quatern_b, hdr->quatern_c, hdr->quatern_d,
                                           hdr->qoffset_x, hdr->qoffset_y, hdr->qoffset_z,
                                           hdr->pixdim[1], hdr->pixdim[2], hdr->pixdim[3], hdr->pixdim[0]);

        hdr->qoffset_x -= shiftPxY * mat.m[1][0];
        hdr->qoffset_y -= shiftPxY * mat.m[1][1];
        hdr->qoffset_z -= shiftPxY * mat.m[1][2];
    }
}

unsigned char * nii_saveNII3DtiltFloat32(char * niiFilename, struct nifti_1_header * hdr, unsigned char* im, struct TDCMopts opts, struct TDICOMdata d, float * sliceMMarray, float gantryTiltDeg, int manufacturer ) {
    //correct for gantry tilt - http://www.mathworks.com/matlabcentral/fileexchange/24458-dicom-gantry-tilt-correction
    if (opts.isOnlyBIDS) return im;
    if (gantryTiltDeg == 0.0) return im;
    struct nifti_1_header hdrIn = *hdr;
    int nVox2DIn = hdrIn.dim[1]*hdrIn.dim[2];
    if ((nVox2DIn < 1) || (hdrIn.dim[0] != 3) || (hdrIn.dim[3] < 3)) return im;
    if (hdrIn.datatype != DT_FLOAT32) {
        printMessage("Only able to correct gantry tilt for 16-bit integer or 32-bit float data with at least 3 slices.");
        return im;
    }
    printMessage("Gantry Tilt Correction is new: please validate conversions\n");
    float GNTtanPx = tan(gantryTiltDeg / (180/M_PI))/hdrIn.pixdim[2]; //tangent(degrees->radian)
    //unintuitive step: reverse sign for negative gantry tilt, therefore -27deg == +27deg (why @!?#)
    // seen in http://www.mathworks.com/matlabcentral/fileexchange/28141-gantry-detector-tilt-correction/content/gantry2.m
    // also validated with actual data...
    #ifndef newTilt
    if (manufacturer == kMANUFACTURER_PHILIPS) //see 'Manix' example from Osirix
        GNTtanPx = - GNTtanPx;
    else if ((manufacturer == kMANUFACTURER_SIEMENS) && (gantryTiltDeg > 0.0))
        GNTtanPx = - GNTtanPx;
    else if (manufacturer == kMANUFACTURER_GE)
        ; //do nothing
    else
    	if (gantryTiltDeg < 0.0) GNTtanPx = - GNTtanPx; //see Toshiba examples from John Muschelli
    #endif //newTilt
    float * imIn32 = ( float*) im;
	//create new output image: larger due to skew
	// compute how many pixels slice must be extended due to skew
    int s = hdrIn.dim[3] - 1; //top slice
    float maxSliceMM = fabs(s * hdrIn.pixdim[3]);
    if (sliceMMarray != NULL) maxSliceMM = fabs(sliceMMarray[s]);
    int pxOffset = ceil(fabs(GNTtanPx*maxSliceMM));
    // printMessage("Tilt extends slice by %d pixels", pxOffset);
	hdr->dim[2] = hdr->dim[2] + pxOffset;

    // When there is negative tilt, the image origin must be adjusted for the padding that will be added.
    if (GNTtanPx < 0) {
        // printMessage("Adjusting origin for %d pixels padding (float)\n", pxOffset);
        adjustOriginForNegativeTilt(hdr, pxOffset);
    }

	int nVox2D = hdr->dim[1]*hdr->dim[2];
	unsigned char * imOut = (unsigned char *)malloc(nVox2D * hdrIn.dim[3] * 4);// *4 as 32-bits per voxel, sizeof(float) );
	float * imOut32 = ( float*) imOut;

	//set surrounding voxels to padding (if present) or darkest observed value
	bool hasPixelPaddingValue = !isnan(d.pixelPaddingValue);
	float pixelPaddingValue;
	if (hasPixelPaddingValue) {
		pixelPaddingValue = d.pixelPaddingValue;
	}
	else {
		// Find darkest pixel value. Note that `hasPixelPaddingValue` remains false so that the darkest value
		// will not trigger nearest neighbor interpolation below when this value is found in the image.
		pixelPaddingValue = imIn32[0];
		for (int v = 0; v < (nVox2DIn * hdrIn.dim[3]); v++)
			if (imIn32[v] < pixelPaddingValue)
                pixelPaddingValue = imIn32[v];
	}
	for (int v = 0; v < (nVox2D * hdrIn.dim[3]); v++)
		imOut32[v] = pixelPaddingValue;

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
				for (int c = 0; c < hdrIn.dim[1]; c++) { //for each column
					float valLo = (float) imIn32[rLo+c];
					float valHi = (float) imIn32[rHi+c];
					if (hasPixelPaddingValue && (valLo == pixelPaddingValue || valHi == pixelPaddingValue)) {
						// https://github.com/rordenlab/dcm2niix/issues/262 - Use nearest neighbor interpolation
						// when at least one of the values is padding.
						imOut32[rOut+c] = fracHi >= 0.5 ? valHi : valLo;
					}
					else {
						imOut32[rOut+c] = round(valLo*fracLo + valHi*fracHi);
					}
				} //for c (each column)
			} //rI (input row) in range
		} //for r (each row)
	} //for s (each slice)*/
	free(im);
	if (sliceMMarray != NULL) return imOut; //we will save after correcting for variable slice thicknesses
    char niiFilenameTilt[2048] = {""};
    strcat(niiFilenameTilt,niiFilename);
    strcat(niiFilenameTilt,"_Tilt");
    nii_saveNII3D(niiFilenameTilt, *hdr, imOut, opts, d);
    return imOut;
}// nii_saveNII3DtiltFloat32()

unsigned char * nii_saveNII3Dtilt(char * niiFilename, struct nifti_1_header * hdr, unsigned char* im, struct TDCMopts opts, struct TDICOMdata d, float * sliceMMarray, float gantryTiltDeg, int manufacturer ) {
    //correct for gantry tilt - http://www.mathworks.com/matlabcentral/fileexchange/24458-dicom-gantry-tilt-correction
    if (opts.isOnlyBIDS) return im;
    if (gantryTiltDeg == 0.0) return im;
    struct nifti_1_header hdrIn = *hdr;
    int nVox2DIn = hdrIn.dim[1]*hdrIn.dim[2];
    if ((nVox2DIn < 1) || (hdrIn.dim[0] != 3) || (hdrIn.dim[3] < 3)) return im;
    if (hdrIn.datatype == DT_FLOAT32)
        return nii_saveNII3DtiltFloat32(niiFilename, hdr, im, opts, d, sliceMMarray, gantryTiltDeg, manufacturer);
    if (hdrIn.datatype != DT_INT16) {
        printMessage("Only able to correct gantry tilt for 16-bit integer data with at least 3 slices.");
        return im;
    }
    printMessage("Gantry Tilt Correction is new: please validate conversions\n");
    float GNTtanPx = tan(gantryTiltDeg / (180/M_PI))/hdrIn.pixdim[2]; //tangent(degrees->radian)
    //unintuitive step: reverse sign for negative gantry tilt, therefore -27deg == +27deg (why @!?#)
    // seen in http://www.mathworks.com/matlabcentral/fileexchange/28141-gantry-detector-tilt-correction/content/gantry2.m
    // also validated with actual data...
    #ifndef newTilt
    if (manufacturer == kMANUFACTURER_PHILIPS) //see 'Manix' example from Osirix
        GNTtanPx = - GNTtanPx;
    else if ((manufacturer == kMANUFACTURER_SIEMENS) && (gantryTiltDeg > 0.0))
        GNTtanPx = - GNTtanPx;
    else if (manufacturer == kMANUFACTURER_GE)
        ; //do nothing
    else
        if (gantryTiltDeg < 0.0) GNTtanPx = - GNTtanPx; //see Toshiba examples from John Muschelli
    #endif //newTilt
    short * imIn16 = ( short*) im;
	//create new output image: larger due to skew
	// compute how many pixels slice must be extended due to skew
    int s = hdrIn.dim[3] - 1; //top slice
    float maxSliceMM = fabs(s * hdrIn.pixdim[3]);
    if (sliceMMarray != NULL) maxSliceMM = fabs(sliceMMarray[s]);
    int pxOffset = ceil(fabs(GNTtanPx*maxSliceMM));
    // printMessage("Tilt extends slice by %d pixels", pxOffset);
	hdr->dim[2] = hdr->dim[2] + pxOffset;

    // When there is negative tilt, the image origin must be adjusted for the padding that will be added.
    if (GNTtanPx < 0) {
        // printMessage("Adjusting origin for %d pixels padding (short)\n", pxOffset);
        adjustOriginForNegativeTilt(hdr, pxOffset);
    }

	int nVox2D = hdr->dim[1]*hdr->dim[2];
	unsigned char * imOut = (unsigned char *)malloc(nVox2D * hdrIn.dim[3] * 2);// *2 as 16-bits per voxel, sizeof( short) );
	short * imOut16 = ( short*) imOut;

	//set surrounding voxels to padding (if present) or darkest observed value
	bool hasPixelPaddingValue = !isnan(d.pixelPaddingValue);
	short pixelPaddingValue;
	if (hasPixelPaddingValue) {
		pixelPaddingValue = (short) round(d.pixelPaddingValue);
	}
	else {
		// Find darkest pixel value. Note that `hasPixelPaddingValue` remains false so that the darkest value
		// will not trigger nearest neighbor interpolation below when this value is found in the image.
		pixelPaddingValue = imIn16[0];
		for (int v = 0; v < (nVox2DIn * hdrIn.dim[3]); v++)
			if (imIn16[v] < pixelPaddingValue)
				pixelPaddingValue = imIn16[v];
	}
	for (int v = 0; v < (nVox2D * hdrIn.dim[3]); v++)
		imOut16[v] = pixelPaddingValue;
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
					short valLo = imIn16[rLo+c];
					short valHi = imIn16[rHi+c];
					if (hasPixelPaddingValue && (valLo == pixelPaddingValue || valHi == pixelPaddingValue)) {
						// https://github.com/rordenlab/dcm2niix/issues/262 - Use nearest neighbor interpolation
						// when at least one of the values is padding.
						imOut16[rOut+c] = fracHi >= 0.5 ? valHi : valLo;
					}
					else {
						imOut16[rOut+c] = round((((float) valLo)*fracLo) + ((float) valHi)*fracHi);
					}
				} //for c (each column)
			} //rI (input row) in range
		} //for r (each row)
	} //for s (each slice)*/
	free(im);
    if (sliceMMarray != NULL) return imOut; //we will save after correcting for variable slice thicknesses
    char niiFilenameTilt[2048] = {""};
    strcat(niiFilenameTilt,niiFilename);
    strcat(niiFilenameTilt,"_Tilt");
    nii_saveNII3D(niiFilenameTilt, *hdr, imOut, opts, d);
    return imOut;
}// nii_saveNII3Dtilt()

int nii_saveNII3Deq(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts, struct TDICOMdata d, float * sliceMMarray ) {
    //convert image with unequal slice distances to equal slice distances
    //sliceMMarray = 0.0 3.0 6.0 12.0 22.0 <- ascending distance from first slice
    if (opts.isOnlyBIDS) return EXIT_SUCCESS;
    int nVox2D = hdr.dim[1]*hdr.dim[2];
    if ((nVox2D < 1) || (hdr.dim[0] != 3) ) return EXIT_FAILURE;
    if ((hdr.datatype !=  DT_FLOAT32) && (hdr.datatype != DT_UINT8) && (hdr.datatype != DT_RGB24) && (hdr.datatype != DT_INT16)) {
        printMessage("Only able to make equidistant slices from 8,16,24-bit integer or 32-bit float data with at least 3 slices.");
        return EXIT_FAILURE;
    }
    float mn = sliceMMarray[1] - sliceMMarray[0];
    for (int i = 1; i < hdr.dim[3]; i++) {
    	float dx = sliceMMarray[i] - sliceMMarray[i-1];
        if ((dx < mn) && (!isSameFloat(dx, 0.0))) // <- allow slice direction to reverse
            mn = sliceMMarray[i] - sliceMMarray[i-1];
    }
    if (mn <= 0.0f) {
    	printMessage("Unable to equalize slice distances: slice order not consistently ascending:\n");
    	printMessage("dx=[0");
		for (int i = 1; i < hdr.dim[3]; i++)
			printMessage(" %g", sliceMMarray[i-1]);
		printMessage("]\n");
		printMessage(" Recompiling with '-DmyInstanceNumberOrderIsNotSpatial' might help.\n");
    	return EXIT_FAILURE;
    }
    int slices = hdr.dim[3];
    slices = (int)ceil((sliceMMarray[slices-1]-0.5*(sliceMMarray[slices-1]-sliceMMarray[slices-2]))/mn); //-0.5: fence post
    if (slices > (hdr.dim[3] * 2)) {
        slices = 2 * hdr.dim[3];
        mn = (sliceMMarray[hdr.dim[3]-1]) / (slices-1);
    }
    //printMessage("-->%g mn slices %d orig %d\n", mn, slices, hdr.dim[3]);
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
    if (hdr.datatype == DT_FLOAT32) {
        float * im32 = ( float*) im;
        imX = (unsigned char *)malloc( (nVox2D * slices)  *  4);//sizeof(float)
        float * imX32 = ( float*) imX;
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
                memcpy(&imX32[sliceXi], &im32[sHi], nVox2D* sizeof(float)); //memcpy( dest, src, bytes)
            } else {
                float fracHi = (sliceXmm-mmLo)/ (mmHi-mmLo);
                float fracLo = 1.0 - fracHi;
                //weight between two slices
                for (int v=0; v < nVox2D; v++)
                    imX32[sliceXi+v] = round( ( (float)im32[sLo+v]*fracLo) + (float)im32[sHi+v]*fracHi);
            }
        }
    } else if (hdr.datatype == DT_INT16) {
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
    nii_saveNII3D(niiFilenameEq, hdrX, imX, opts, d);
    free(imX);
    return EXIT_SUCCESS;
}// nii_saveNII3Deq()

/*int nii_saveNII3Deq(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts, float * sliceMMarray ) {
    //convert image with unequal slice distances to equal slice distances
    //sliceMMarray = 0.0 3.0 6.0 12.0 22.0 <- ascending distance from first slice
    if (opts.isOnlyBIDS) return EXIT_SUCCESS;

    int nVox2D = hdr.dim[1]*hdr.dim[2];
    if ((nVox2D < 1) || (hdr.dim[0] != 3) ) return EXIT_FAILURE;
    if ((hdr.datatype !=  DT_FLOAT32) && (hdr.datatype != DT_UINT8) && (hdr.datatype != DT_RGB24) && (hdr.datatype != DT_INT16)) {
        printMessage("Only able to make equidistant slices from 8,16,24-bit integer or 32-bit float data with at least 3 slices.");
        return EXIT_FAILURE;
    }
    //find lowest and highest slice
    float lo = sliceMMarray[0];
    float hi = lo;
    for (int i = 1; i < hdr.dim[3]; i++) {
    	if (sliceMMarray[i] < lo)
    		lo = sliceMMarray[i];
    	if (sliceMMarray[i] > hi)
    		hi = sliceMMarray[i];
    }
    if (isSameFloat(lo,hi)) return EXIT_SUCCESS;


    float mn = fabs(sliceMMarray[1] - sliceMMarray[0]);
    for (int i = 1; i < (hdr.dim[3]-1); i++) {
    	for (int j = i+1; j < hdr.dim[3]; j++) {
    		float dx = fabs(sliceMMarray[i] - sliceMMarray[j]);
        	if ((dx < mn) && (dx > 0.0))
            	mn = dx;
        }
    }
    if (mn <= 0.0f) {
    	printMessage("Unable to equalize slice distances: slice number not consistent with slice position.\n");
    	return EXIT_FAILURE;
    }
    int slices = hdr.dim[3];
    //slices = (int)ceil((sliceMMarray[slices-1]-0.5*(sliceMMarray[slices-1]-sliceMMarray[slices-2]))/mn); //-0.5: fence post
    slices = (int)round((hi-lo+mn)/mn); //+mn: fence post
    printMessage("lo=%g hi=%g mn=%g slices=%d\n", lo, hi, mn, slices);
    if (slices > (hdr.dim[3] * 2)) {
        slices = 2 * hdr.dim[3];
        mn = (sliceMMarray[hdr.dim[3]-1]) / (slices-1);
    }
    //printMessage("-->%g mn slices %d orig %d\n", mn, slices, hdr.dim[3]);
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
    if (hdr.datatype == DT_FLOAT32) {
        float * im32 = ( float*) im;
        imX = (unsigned char *)malloc( (nVox2D * slices)  *  4);//sizeof(float)
        float * imX32 = ( float*) imX;
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
                memcpy(&imX32[sliceXi], &im32[sHi], nVox2D* sizeof(float)); //memcpy( dest, src, bytes)

            } else {
                float fracHi = (sliceXmm-mmLo)/ (mmHi-mmLo);
                float fracLo = 1.0 - fracHi;
                //weight between two slices
                for (int v=0; v < nVox2D; v++)
                    imX32[sliceXi+v] = round( ( (float)im32[sLo+v]*fracLo) + (float)im32[sHi+v]*fracHi);
            }
        }
    } else if (hdr.datatype == DT_INT16) {
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
}// nii_saveNII3Deq() */

float PhilipsPreciseVal (float lPV, float lRS, float lRI, float lSS) {
    if ((lRS*lSS) == 0) //avoid divide by zero
        return 0.0;
    else
        return (lPV * lRS + lRI) / (lRS * lSS);
}

void PhilipsPrecise(struct TDICOMdata * d, bool isPhilipsFloatNotDisplayScaling, struct nifti_1_header *h, int verbose) {
	if (d->manufacturer != kMANUFACTURER_PHILIPS) return; //not Philips
	if (!isSameFloatGE(0.0, d->RWVScale)) {
		h->scl_slope = d->RWVScale;
    	h->scl_inter = d->RWVIntercept;
		printMessage("Using RWVSlope:RWVIntercept = %g:%g\n",d->RWVScale,d->RWVIntercept);
		printMessage(" Philips Scaling Values RS:RI:SS = %g:%g:%g (see PMC3998685)\n",d->intenScale,d->intenIntercept,d->intenScalePhilips);
		if (verbose == 0) return;
		printMessage("Potential Alternative Intensity Scalings\n");
		printMessage(" R = raw value, P = precise value, D = displayed value\n");
		printMessage(" RS = rescale slope, RI = rescale intercept,  SS = scale slope\n");
		printMessage(" D = R * RS + RI    , P = D/(RS * SS)\n");
		return;
	}
	if (d->intenScalePhilips == 0)  return; //no Philips Precise
	//we will report calibrated "FP" values http://www.ncbi.nlm.nih.gov/pmc/articles/PMC3998685/
	float l0 = PhilipsPreciseVal (0, d->intenScale, d->intenIntercept, d->intenScalePhilips);
	float l1 = PhilipsPreciseVal (1, d->intenScale, d->intenIntercept, d->intenScalePhilips);
	float intenScaleP = d->intenScale;
	float intenInterceptP = d->intenIntercept;
	if (l0 != l1) {
		intenInterceptP = l0;
		intenScaleP = l1-l0;
	}
	if (isSameFloat(d->intenIntercept,intenInterceptP) && isSameFloat(d->intenScale, intenScaleP)) return; //same result for both methods: nothing to do or report!
	printMessage("Philips Scaling Values RS:RI:SS = %g:%g:%g (see PMC3998685)\n",d->intenScale,d->intenIntercept,d->intenScalePhilips);
	if (verbose > 0) {
		printMessage(" R = raw value, P = precise value, D = displayed value\n");
		printMessage(" RS = rescale slope, RI = rescale intercept,  SS = scale slope\n");
		printMessage(" D = R * RS + RI    , P = D/(RS * SS)\n");
		printMessage(" D scl_slope:scl_inter = %g:%g\n", d->intenScale,d->intenIntercept);
		printMessage(" P scl_slope:scl_inter = %g:%g\n", intenScaleP,intenInterceptP);
	}
	//#define myUsePhilipsPrecise
	if (isPhilipsFloatNotDisplayScaling) {
		if (verbose > 0) printMessage(" Using P values ('-p n ' for D values)\n");
		//to change DICOM:
		//d->intenScale = intenScaleP;
		//d->intenIntercept = intenInterceptP;
		//to change NIfTI
    	h->scl_slope = intenScaleP;
    	h->scl_inter = intenInterceptP;
    	d->intenScalePhilips = 0; //so we never run this TWICE!
	} else if (verbose > 0)
		printMessage(" Using D values ('-p y ' for P values)\n");
} //PhilipsPrecise()

void smooth1D(int num, double * im) {
	if (num < 3) return;
	double * src = (double *) malloc(sizeof(double)*num);
	memcpy(&src[0], &im[0], num * sizeof(double)); //memcpy( dest, src, bytes)
	double frac = 0.25;
	for (int i = 1; i < (num-1); i++)
		im[i] = (src[i-1]*frac) + (src[i]*frac*2) + (src[i+1]*frac);
	free(src);
}// smooth1D()

int nii_saveCrop(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts, struct TDICOMdata d) {
    //remove excess neck slices - assumes output of nii_setOrtho()
    if (opts.isOnlyBIDS) return EXIT_SUCCESS;
    int nVox2D = hdr.dim[1]*hdr.dim[2];
    if ((nVox2D < 1) || (fabs(hdr.pixdim[3]) < 0.001) || (hdr.dim[0] != 3) || (hdr.dim[3] < 128)) return EXIT_FAILURE;
    if ((hdr.datatype != DT_INT16) && (hdr.datatype != DT_UINT16)) {
        printMessage("Only able to crop 16-bit volumes.");
        return EXIT_FAILURE;
    }
	short * im16 = ( short*) im;
	unsigned short * imu16 = (unsigned short*) im;
	float kThresh = 0.09; //more than 9% of max brightness
	/*#ifdef myEnableOtsu
	// This code removes noise "haze" yielding better volume rendering and smaller gz compressed files
	// However, it may disrupt "differennce of gaussian" segmentation estimates
	// Therefore feature was removed from dcm2niix, which aims for lossless conversion
	kThresh = 0.0001;
	if (hdr.datatype == DT_UINT16)
		maskBackgroundU16 (imu16, hdr.dim[1],hdr.dim[2],hdr.dim[3], 5,2, true);
	else
		maskBackground16 (im16, hdr.dim[1],hdr.dim[2],hdr.dim[3], 5,2, true);
	#endif*/
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
    	return EXIT_FAILURE;
    }
    smooth1D(slices, sliceSums);
    for (int i = 0; i  < slices; i++) sliceSums[i] = sliceSums[i] / maxSliceVal; //so brightest slice has value 1
	//dorsal crop: eliminate slices with more than 5% brightness
	int dorsalCrop;
	for (dorsalCrop = (slices-1); dorsalCrop >= 1; dorsalCrop--)
		if (sliceSums[dorsalCrop-1] > kThresh) break;
	if (dorsalCrop <= 1) {
		free(sliceSums);
		return EXIT_FAILURE;
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
    	return EXIT_FAILURE;
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
    //printMessage("%d %d %d\n", ventralMaxSlice, minSlice, maxSlice);
	int gap = round((maxSlice-minSlice)*0.8);//add 40% for cerebellum
	if ((minSlice-gap) > 1)
        ventralCrop = minSlice-gap;
	free(sliceSums);
	if (ventralCrop > dorsalCrop) return EXIT_FAILURE;
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
	printMessage(" Cropping from slice %d to %d (of %d)\n", ventralCrop, dorsalCrop, slices);
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
    const int returnCode = nii_saveNII3D(niiFilenameCrop, hdrX, imX, opts, d);
    free(imX);
    return returnCode;
}// nii_saveCrop()

float dicomTimeToSec (float dicomTime) {
//convert HHMMSS to seconds, 135300.024 -> 135259.731 are 0.293 sec apart
	char acqTimeBuf[64];
	snprintf(acqTimeBuf, sizeof acqTimeBuf, "%+013.5f", (double)dicomTime);
	int ahour,amin;
	double asec;
	int count = 0;
	sscanf(acqTimeBuf, "%3d%2d%lf%n", &ahour, &amin, &asec, &count);
	if (!count) return -1;
	return  (ahour * 3600)+(amin * 60) + asec;
}

float acquisitionTimeDifference(struct TDICOMdata * d1, struct TDICOMdata * d2) {
	if (d1->acquisitionDate != d2->acquisitionDate) return -1; //to do: scans running across midnight
	float sec1 = dicomTimeToSec(d1->acquisitionTime);
	float sec2 = dicomTimeToSec(d2->acquisitionTime);
	//printMessage("%g\n",d2->acquisitionTime);
	if ((sec1 < 0) || (sec2 < 0)) return -1;
	return (sec2 - sec1);
}
void checkDateTimeOrder(struct TDICOMdata * d, struct TDICOMdata * d1) {
	if (d->acquisitionDate < d1->acquisitionDate) return; //d1 occurred on later date
	if (d->acquisitionTime <= d1->acquisitionTime) return; //d1 occurred on later (or same) time
	if (d->imageNum > d1->imageNum)
		printWarning("Images not sorted in ascending instance number (0020,0013)\n");
	else
		printWarning("Images sorted by instance number  [0020,0013](%d..%d), but AcquisitionTime [0008,0032] suggests a different order (%g..%g) \n", d->imageNum,d1->imageNum, d->acquisitionTime,d1->acquisitionTime);
}

void checkSliceTiming(struct TDICOMdata * d, struct TDICOMdata * d1, int verbose, int isForceSliceTimeHHMMSS) {
//detect images with slice timing errors. https://github.com/rordenlab/dcm2niix/issues/126
//modified 20190704: this function now ensures all slice times are in msec
	if ((d->TR < 0.0) || (d->CSA.sliceTiming[0] < 0.0)) return; //no slice timing
	int nSlices = 0;
	while ((nSlices < kMaxEPI3D) && (d->CSA.sliceTiming[nSlices] >= 0.0))
		nSlices++;
	if (nSlices < 1) return;
	bool isSliceTimeHHMMSS = (d->manufacturer == kMANUFACTURER_UIH);
	if (isForceSliceTimeHHMMSS) isSliceTimeHHMMSS = true;
	//if (d->isXA10A) isSliceTimeHHMMSS = true; //for XA10 use TimeAfterStart 0x0021,0x1104 -> Siemens de-identification can corrupt acquisition ties https://github.com/rordenlab/dcm2niix/issues/236
	if (isSliceTimeHHMMSS) {//handle midnight crossing
		for (int i = 0; i < nSlices; i++)
			d->CSA.sliceTiming[i] = dicomTimeToSec(d->CSA.sliceTiming[i]);
		float minT = d->CSA.sliceTiming[0];
		float maxT = minT;
		for (int i = 0; i < nSlices; i++) {
			if (d->CSA.sliceTiming[i] < minT) minT = d->CSA.sliceTiming[i];
			if (d->CSA.sliceTiming[i] < maxT) maxT = d->CSA.sliceTiming[i];
		}
		//printf("%d %g  ---> %g..%g\n", nSlices, d->TR, minT, maxT);
		float kMidnightSec = 86400;
		float kNoonSec = 43200;
		if ((maxT - minT) > kNoonSec) { //volume started before midnight but ended next day!
			//identify and fix 'Cinderella error' where clock resets at midnight: untested
			printWarning("Acquisition crossed midnight: check slice timing\n");
			for (int i = 0; i < nSlices; i++)
				if (d->CSA.sliceTiming[i] > kNoonSec) d->CSA.sliceTiming[i] = d->CSA.sliceTiming[i] - kMidnightSec;
						minT = d->CSA.sliceTiming[0];
			for (int i = 0; i < nSlices; i++)
				if (d->CSA.sliceTiming[i] < minT) minT = d->CSA.sliceTiming[i];
		}
		for (int i = 0; i < nSlices; i++)
			d->CSA.sliceTiming[i] = d->CSA.sliceTiming[i] - minT;
	} //XA10/UIH: HHMMSS -> Sec
	float minT = d->CSA.sliceTiming[0];
	float maxT = minT;
	for (int i = 0; i < kMaxEPI3D; i++) {
		if (d->CSA.sliceTiming[i] < 0.0) break;
		if (d->CSA.sliceTiming[i] < minT) minT = d->CSA.sliceTiming[i];
		if (d->CSA.sliceTiming[i] > maxT) maxT = d->CSA.sliceTiming[i];
	}
	if (isSliceTimeHHMMSS) //convert HHMMSS to msec
		for (int i = 0; i < kMaxEPI3D; i++)
			d->CSA.sliceTiming[i] = dicomTimeToSec(d->CSA.sliceTiming[i]) * 1000.0;
	float TRms = d->TR; //d->TR in msec!
	if ((minT != maxT) && (maxT <= TRms)) {
		if (verbose != 0)
			printMessage("Slice timing range appears reasonable (range %g..%g, TR=%g ms)\n", minT, maxT, TRms);
		return; //looks fine
	}
	if ((minT == maxT) && (d->is3DAcq)) return; //fine: 3D EPI
	if ((minT == maxT) && (d->CSA.multiBandFactor == d->CSA.mosaicSlices)) return; //fine: all slices single excitation
	if ((strlen(d->seriesDescription) > 0) && (strstr(d->seriesDescription, "SBRef") != NULL))  return; //fine: single-band calibration data, the slice timing WILL exceed the TR
	//check if 2nd image has valid slice timing
	float minT1 = d1->CSA.sliceTiming[0];
	float maxT1 = minT1;
	for (int i = 0; i < nSlices; i++) {
		//if (d1->CSA.sliceTiming[i] < 0.0) break;
		if (d1->CSA.sliceTiming[i] < minT1) minT1 = d1->CSA.sliceTiming[i];
		if (d1->CSA.sliceTiming[i] > maxT1) maxT1 = d1->CSA.sliceTiming[i];
	}
	if ((minT1 < 0.0) && (d->rtia_timerGE >= 0.0)) return; //use rtia timer
	if (minT1 < 0.0) { //https://github.com/neurolabusc/MRIcroGL/issues/31
		printWarning("Siemens MoCo? Bogus slice timing (range %g..%g, TR=%g seconds)\n", minT1, maxT1, TRms);
		return;
	}
	if ((minT1 == maxT1) || (maxT1 >= TRms)) { //both first and second image corrupted
		printWarning("Slice timing appears corrupted (range %g..%g, TR=%g ms)\n", minT1, maxT1, TRms);
		return;
	}
	//1st image corrupted, but 2nd looks ok - substitute values from 2nd image
	for (int i = 0; i < kMaxEPI3D; i++) {
		d->CSA.sliceTiming[i] = d1->CSA.sliceTiming[i];
		if (d1->CSA.sliceTiming[i] < 0.0) break;
	}
	d->CSA.multiBandFactor = d1->CSA.multiBandFactor;
	printMessage("CSA slice timing based on 2nd volume, 1st volume corrupted (CMRR bug, range %g..%g, TR=%g ms)\n", minT, maxT, TRms);
}//checkSliceTiming()

void sliceTimingXA(struct TDCMsort *dcmSort,struct TDICOMdata *dcmList, struct nifti_1_header * hdr, int verbose, const char * filename, int nConvert) {
    //Siemens XA10 slice timing
    // Ignore first volume: For an example of erroneous first volume timing, see series 10 (Functional_w_SMS=3) https://github.com/rordenlab/dcm2niix/issues/240
    // an alternative would be to use 0018,9074 - this would need to be converted from DT to Secs, and is scrambled if de-identifies data see enhanced de-identified series 26 from issue 236
    uint64_t indx0 = dcmSort[0].indx; //first volume
    if (!dcmList[indx0].isXA10A) return;
    if ( (nConvert == (hdr->dim[3]*hdr->dim[4])) && (hdr->dim[3] < (kMaxEPI3D-1)) && (hdr->dim[3] > 1) && (hdr->dim[4] > 1)) {
		//XA11 2D classic
		for (int v = 0; v < hdr->dim[3]; v++)
			dcmList[indx0].CSA.sliceTiming[v] = dcmList[dcmSort[v].indx].CSA.sliceTiming[0];
	} else if ( (nConvert == (hdr->dim[4])) && (hdr->dim[3] < (kMaxEPI3D-1)) && (hdr->dim[3] > 1) && (hdr->dim[4] > 1)) {
		//XA10 mosaics - these are missing a lot of information
		float mn = dcmList[dcmSort[1].indx].CSA.sliceTiming[0];
		//get slice timing from second volume
		for (int v = 0; v < hdr->dim[3]; v++) {
			dcmList[indx0].CSA.sliceTiming[v] = dcmList[dcmSort[1].indx].CSA.sliceTiming[v];
			if (dcmList[indx0].CSA.sliceTiming[v] < mn) mn = dcmList[indx0].CSA.sliceTiming[v];
		}
		if (mn < 0.0) mn = 0.0;
		int mb = 0;
		for (int v = 0; v < hdr->dim[3]; v++) {
			dcmList[indx0].CSA.sliceTiming[v] -= mn;
			if (isSameFloatGE(dcmList[indx0].CSA.sliceTiming[v], 0.0)) mb ++;
		}
		if ((dcmList[indx0].CSA.multiBandFactor < 2) && (mb > 1))
			dcmList[indx0].CSA.multiBandFactor = mb;
		//for (int v = 0; v < hdr->dim[3]; v++)
		//	printf("XA10sliceTiming\t%d\t%g\n", v, dcmList[indx0].CSA.sliceTiming[v]);
	}
} //sliceTimingXA()

void rescueSliceTimingGE(struct TDICOMdata * d, int verbose, int nSL, const char * filename) {
	//we can often read GE slice timing from TriggerTime (0018,1060) or RTIA Timer (0021,105E)
	// if both of these methods fail, we can often guess based on slice order stored in the Private Protocol Data Block (0025,101B)
	// this is referred to as "rescue" as we only know the TR, not the TA. So assumes continuous scans with no gap
	if (d->is3DAcq) return; //no need for slice times
	if (nSL < 2) return;
	if (d->manufacturer != kMANUFACTURER_GE) return;
	if ((d->protocolBlockStartGE < 1) || (d->protocolBlockLengthGE < 19)) return;
	#ifdef myReadGeProtocolBlock
	//GE final desperate attempt to determine slice order
	// GE does not provide a good estimate for TA: here we use TR, which will be wrong for sparse designs
	// Also, unclear what happens if slice order is flipped
	// Therefore, we warning the user that we are guessing
	int viewOrderGE = -1;
	int sliceOrderGE = -1;
	int mbAccel = -1;
	//printWarning("Using GE Protocol Data Block for BIDS data (beware: new feature)\n");
	int ok = geProtocolBlock(filename, d->protocolBlockStartGE, d->protocolBlockLengthGE, verbose, &sliceOrderGE, &viewOrderGE, &mbAccel);
	if (ok != EXIT_SUCCESS) {
		printWarning("Unable to decode GE protocol block\n");
		return;
	}
	if (mbAccel > 1) {
		d->CSA.multiBandFactor = mbAccel;
		printWarning("Unabled to compute slice times for GE multi-band. SliceOrder=%d (seq=0, int=1)\n", sliceOrderGE);
		d->CSA.sliceTiming[0] = -1.0;
		return;
		
	}
	if (d->CSA.sliceTiming[0] >= 0.0) return; //slice times calculated - moved here to detect multiband, see issue 336
	
	if ((sliceOrderGE < 0) || (sliceOrderGE > 1)) return;
	// 0=sequential/1=interleaved
	printWarning("Guessing slice times using ProtocolBlock SliceOrder=%d (seq=0, int=1)\n", sliceOrderGE);
	int nOdd = nSL / 2;
	float secPerSlice = d->TR/nSL; //should be TA not TR! We do not know TR
	if (sliceOrderGE == 0) {
		for (int i = 0; i < nSL; i++)
			d->CSA.sliceTiming[i] = i * secPerSlice;
	} else {
		for (int i = 0; i < nSL; i++) {
			if (i % 2 == 0) { //ODD slices since we index from 0!
				d->CSA.sliceTiming[i] = (i/2) * secPerSlice;
				//printf("%g\n", d->CSA.sliceTiming[i]);
			} else {
				d->CSA.sliceTiming[i] = (nOdd+((i+1)/2)) * secPerSlice;
				//printf("%g\n", d->CSA.sliceTiming[i]);
			}
		} //for each slice
	} //if interleaved
	#endif
}

void reverseSliceTiming(struct TDICOMdata * d,  int verbose, int nSL) {
	if ((d->CSA.protocolSliceNumber1 == 0) || ((d->CSA.protocolSliceNumber1 == 1))) return; //slices not flipped
	if (d->is3DAcq) return; //no need for slice times
	if (d->CSA.sliceTiming[0] < 0.0) return; //no slice times
	if (nSL > kMaxEPI3D) return;
	if (nSL < 2) return;
	if (verbose)
   		printMessage("Slices were spatially flipped, so slice times are flipped\n");
   	d->CSA.protocolSliceNumber1 = 0;
   	float sliceTiming[kMaxEPI3D];
   	for (int i = 0; i < nSL; i++)
		sliceTiming[i] = d->CSA.sliceTiming[i];
	for (int i = 0; i < nSL; i++)
		d->CSA.sliceTiming[i] = sliceTiming[(nSL-1)-i];
}

int sliceTimingSiemens2D(struct TDCMsort *dcmSort,struct TDICOMdata *dcmList, struct nifti_1_header * hdr, int verbose, const char * filename, int nConvert) {
	//only for Siemens 2D images, use acquisitionTime
	uint64_t indx0 = dcmSort[0].indx; //first volume
    if (!(dcmList[indx0].manufacturer == kMANUFACTURER_SIEMENS)) return 0;
	if (dcmList[indx0].is3DAcq) return 0; //no need for slice times
	if (dcmList[indx0].CSA.sliceTiming[0] >= 0.0) return 0; //slice times calculated
	if (dcmList[indx0].CSA.mosaicSlices > 1) return 0;
    if (nConvert != (hdr->dim[3]*hdr->dim[4])) return 0;
    if (hdr->dim[3] > (kMaxEPI3D-1)) return 0;
    int nZero = 0; //infer multiband: E11C may not populate kPATModeText
    for (int v = 0; v < hdr->dim[3]; v++) {
		dcmList[indx0].CSA.sliceTiming[v] = dcmList[dcmSort[v].indx].acquisitionTime; //nb format is HHMMSS we need to handle midnight-crossing and convert to ms,  see checkSliceTiming()
		if (dcmList[indx0].CSA.sliceTiming[v] == dcmList[indx0].CSA.sliceTiming[0]) nZero++;
	}
	if ((dcmList[indx0].CSA.multiBandFactor < 2) && (nZero > 1))
		dcmList[indx0].CSA.multiBandFactor = nZero;	
	return 1;
}

void rescueSliceTimingSiemens(struct TDICOMdata * d, int verbose, int nSL, const char * filename) {
	if (d->is3DAcq) return; //no need for slice times
	if (d->CSA.multiBandFactor > 1) return; //pattern of multiband slice order unknown
	if (d->CSA.sliceTiming[0] >= 0.0) return; //slice times calculated
	if (d->CSA.mosaicSlices < 2) return; //20190807 E11C 2D (not mosaic) files do not report mosaicAcqTimes or multi-band factor.
	if (nSL < 2) return;
	if ((d->manufacturer != kMANUFACTURER_SIEMENS) || (d->CSA.SeriesHeader_offset < 1) || (d->CSA.SeriesHeader_length < 1)) return;
#ifdef myReadAsciiCsa
	float shimSetting[8];
	char protocolName[kDICOMStrLarge], fmriExternalInfo[kDICOMStrLarge], coilID[kDICOMStrLarge], consistencyInfo[kDICOMStrLarge], coilElements[kDICOMStrLarge], pulseSequenceDetails[kDICOMStrLarge], wipMemBlock[kDICOMStrLarge];
	TCsaAscii csaAscii;
	siemensCsaAscii(filename, &csaAscii, d->CSA.SeriesHeader_offset, d->CSA.SeriesHeader_length, shimSetting, coilID, consistencyInfo, coilElements, pulseSequenceDetails, fmriExternalInfo, protocolName, wipMemBlock);
	int ucMode = csaAscii.ucMode;
	if ((ucMode < 1) || (ucMode == 3) || (ucMode > 4)) return;
	float trSec = d->TR / 1000.0;
	float delaySec = csaAscii.delayTimeInTR/ 1000000.0;
	float taSec = trSec - delaySec;
	float sliceTiming[kMaxEPI3D];
	for (int i = 0; i < nSL; i++)
			sliceTiming[i] = i * taSec/nSL * 1000.0; //expected in ms
	if (ucMode == 1) //asc
		for (int i = 0; i < nSL; i++)
			d->CSA.sliceTiming[i] = sliceTiming[i];
	if (ucMode == 2) //desc
		for (int i = 0; i < nSL; i++)
			d->CSA.sliceTiming[i] = sliceTiming[(nSL-1) - i];
	if (ucMode == 4) { //int
		int oddInc = 0; //for slices 1,3,5
		int evenInc = (nSL+1) / 2; //for 4 slices 0,1,2,3 we will order [2,0,3,1] for 5 slices [0,3,1,4,2]
		if (nSL % 2 == 0) { //Siemens interleaved for acquisitions with odd number of slices https://www.mccauslandcenter.sc.edu/crnl/tools/stc
			oddInc = evenInc;
			evenInc = 0;
		}
		for (int i = 0; i < nSL; i++) {
			if (i % 2 == 0) {//odd slice 1,3,etc [indexed from 0]!
				d->CSA.sliceTiming[i] = sliceTiming[oddInc];
				//printf("%d %d\n", i, oddInc);
				oddInc += 1;
			} else { //even slice
				d->CSA.sliceTiming[i] = sliceTiming[evenInc];
				//printf("%d %d %d\n", i, evenInc, nSL);
				evenInc += 1;
			}
		}
	} //if ucMode == 3 int
	//dicm2nii provides sSliceArray.ucImageNumb - similar to protocolSliceNumber1
	//if asc_header(s, 'sSliceArray.ucImageNumb'), t = t(nSL:-1:1); end % rev-num
#endif
}

void sliceTimingUIH(struct TDCMsort *dcmSort,struct TDICOMdata *dcmList, struct nifti_1_header * hdr, int verbose, const char * filename, int nConvert) {
	uint64_t indx0 = dcmSort[0].indx; //first volume
    if (!(dcmList[indx0].manufacturer == kMANUFACTURER_UIH)) return;
    if (nConvert != (hdr->dim[3]*hdr->dim[4])) return;
    if (hdr->dim[3] > (kMaxEPI3D-1)) return;
    if (hdr->dim[4] < 2) return;
    for (int v = 0; v < hdr->dim[3]; v++)
		dcmList[indx0].CSA.sliceTiming[v] = dcmList[dcmSort[v].indx].acquisitionTime; //nb format is HHMMSS we need to handle midnight-crossing and convert to ms,  see checkSliceTiming()
}

void sliceTimingGE(struct TDCMsort *dcmSort,struct TDICOMdata *dcmList, struct nifti_1_header * hdr, int verbose, const char * filename, int nConvert) {
	//GE check slice timing >>>
	uint64_t indx0 = dcmSort[0].indx; //first volume
	if (!(dcmList[indx0].manufacturer == kMANUFACTURER_GE)) return;
	bool GEsliceTiming_x0018x1060 = false;
	if ((hdr->dim[3] < (kMaxEPI3D-1)) && (hdr->dim[3] > 1) && (hdr->dim[4] > 1)) {
		//GE: 1st method for "epi" PSD
		//0018x1060 is defined in msec: http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0018,1060)
		//as of 20190704 dcm2niix expects sliceTiming to be encoded in msec for all vendors
		GEsliceTiming_x0018x1060 = true;
		for (int v = 0; v < hdr->dim[3]; v++) {
			if (dcmList[dcmSort[v].indx].CSA.sliceTiming[0] < 0)
				GEsliceTiming_x0018x1060 = false;
				dcmList[indx0].CSA.sliceTiming[v] = dcmList[dcmSort[v].indx].CSA.sliceTiming[0]; //ms 20190704
				//dcmList[indx0].CSA.sliceTiming[v] = dcmList[dcmSort[v].indx].CSA.sliceTiming[0] / 1000.0; //ms -> sec prior to 20190704
		}
		//printMessage(">>>>Reading GE slice timing from 0018,1060\n");
		//0018,1060 provides time at end of acquisition, not start...
		if (GEsliceTiming_x0018x1060) {
			float minT = dcmList[indx0].CSA.sliceTiming[0];
			float maxT = minT;
			for (int v = 0; v < hdr->dim[3]; v++)
				if (dcmList[indx0].CSA.sliceTiming[v] < minT)
					minT = dcmList[indx0].CSA.sliceTiming[v];
			for (int v = 0; v < hdr->dim[3]; v++)
				if (dcmList[indx0].CSA.sliceTiming[v] > maxT)
					maxT = dcmList[indx0].CSA.sliceTiming[v];
			for (int v = 0; v < hdr->dim[3]; v++)
				dcmList[indx0].CSA.sliceTiming[v] = dcmList[indx0].CSA.sliceTiming[v] - minT;
			if (isSameFloatGE(minT, maxT)) {
				//ABCD simulated GE DICOMs do not populate 0018,1060 correctly
				GEsliceTiming_x0018x1060 = false;
				dcmList[indx0].CSA.sliceTiming[0] = -1.0; //no valid slice times
			}
		} //adjust: first slice is time = 0.0
	} //GE slice timing from 0018,1060
	if ((!GEsliceTiming_x0018x1060) && (hdr->dim[3] < (kMaxEPI3D-1)) && (hdr->dim[3] > 1) && (hdr->dim[4] > 1)) {
		//printMessage(">>>>Reading GE slice timing from epiRT (0018,1060 did not work)\n");
		//GE: 2nd method for "epiRT" PSD
		//ignore bogus values of first volume https://neurostars.org/t/getting-missing-ge-information-required-by-bids-for-common-preprocessing/1357/6
		// this necessarily requires at last two volumes, hence dim[4] > 1
		int j = hdr->dim[3];
		//since first volume is bogus, we define the volume start time as the first slice in the second volume
		float minTime = dcmList[dcmSort[j].indx].rtia_timerGE;
		float maxTime = minTime;
		for (int v = 0; v < hdr->dim[3]; v++) {
			if (dcmList[dcmSort[v+j].indx].rtia_timerGE < minTime)
				minTime = dcmList[dcmSort[v+j].indx].rtia_timerGE;
			if (dcmList[dcmSort[v+j].indx].rtia_timerGE > maxTime)
				maxTime = dcmList[dcmSort[v+j].indx].rtia_timerGE;
		}
		//compare all slice times in 2nd volume to start time for this volume
		if (maxTime != minTime) {
			double scale2Sec = 1.0;
			if (dcmList[indx0].TR > 0.0) { //issue 286: determine units for rtia_timerGE
				//See https://github.com/rordenlab/dcm2niix/tree/master/GE
				// Nikadon's DV24 data stores RTIA Timer as seconds, issue 286 14_LX uses 1/10,000 sec
				// The slice timing should always be less than the TR (which is in ms)
				// Below we assume 1/10,000 of a sec if slice time is >90% and less than <100% of a TR
				// Will not work for sparse designs, but slice timing inappropriate for those datasets
				float maxSliceTimeFrac = (maxTime-minTime) / dcmList[indx0].TR; //should be slightly less than 1.0
				if ((maxSliceTimeFrac > 9.0) && (maxSliceTimeFrac < 10))
					scale2Sec = 1.0 / 10000.0;
				//printMessage(">> %g %g %g\n", maxSliceTimeFrac, scale2Sec, dcmList[indx0].TR);
			}
			double scale2ms = scale2Sec * 1000.0; //20190704: convert slice timing values to ms for all vendors
			for (int v = 0; v < hdr->dim[3]; v++)
				dcmList[indx0].CSA.sliceTiming[v] = (dcmList[dcmSort[v+j].indx].rtia_timerGE - minTime) * scale2ms;
			dcmList[indx0].CSA.sliceTiming[hdr->dim[3]] = -1;
			//detect multi-band
			int nZero = 0;
			for (int v = 0; v < hdr->dim[3]; v++)
				if (isSameFloatGE(dcmList[indx0].CSA.sliceTiming[hdr->dim[3]], 0.0))
					nZero ++;
			if ((nZero > 1) && (nZero < hdr->dim[3]) && ((hdr->dim[3] % nZero) == 0))
				dcmList[indx0].CSA.multiBandFactor = nZero;
			//report times
			if (verbose > 0) {
				printMessage("GE slice timing (sec)\n");
				printMessage("\tTime\tX\tY\tZ\tInstance\n");
				for (int v = 0; v < hdr->dim[3]; v++) {
					if (v == (hdr->dim[3]-1))
						printMessage("...\n");
					if ((v < 4) || (v == (hdr->dim[3]-1)))
						printMessage("\t%g\t%g\t%g\t%g\t%d\n", dcmList[indx0].CSA.sliceTiming[v] / 1000.0, dcmList[dcmSort[v+j].indx].patientPosition[1], dcmList[dcmSort[v+j].indx].patientPosition[2], dcmList[dcmSort[v+j].indx].patientPosition[3], dcmList[dcmSort[v+j].indx].imageNum);

				} //for v
			} //verbose > 1
		} //if maxTime != minTIme
	} //GE slice timing from 0021,105E
} //sliceTimingGE()

int sliceTimingCore(struct TDCMsort *dcmSort,struct TDICOMdata *dcmList, struct nifti_1_header * hdr, int verbose, const char * filename, int nConvert) {
	int sliceDir = 0;
	if (hdr->dim[3] < 2) return sliceDir;
	//uint64_t indx0 = dcmSort[0].indx;
	//uint64_t indx1 = dcmSort[1].indx;
	struct TDICOMdata * d0 = &dcmList[dcmSort[0].indx];
	uint64_t indx1 = dcmSort[1].indx;
	if (nConvert < 2) indx1 = dcmSort[0].indx;
	struct TDICOMdata * d1 = &dcmList[indx1];
	sliceTimingGE(dcmSort, dcmList, hdr, verbose, filename, nConvert);
	sliceTimingUIH(dcmSort, dcmList, hdr, verbose, filename, nConvert);
	int isSliceTimeHHMMSS = sliceTimingSiemens2D(dcmSort, dcmList, hdr, verbose, filename, nConvert);
	sliceTimingXA(dcmSort, dcmList, hdr, verbose, filename, nConvert);
	checkSliceTiming(d0, d1, verbose, isSliceTimeHHMMSS);
    rescueSliceTimingSiemens(d0, verbose, hdr->dim[3], filename); //desperate attempts if conventional methods fail
    rescueSliceTimingGE(d0, verbose, hdr->dim[3], filename); //desperate attempts if conventional methods fail
    if (hdr->dim[3] > 1)sliceDir = headerDcm2Nii2(dcmList[dcmSort[0].indx], dcmList[indx1] , hdr, true);
	//UNCOMMENT NEXT TWO LINES TO RE-ORDER MOSAIC WHERE CSA's protocolSliceNumber does not start with 1
	if (dcmList[dcmSort[0].indx].CSA.protocolSliceNumber1 > 1) {
		printWarning("Weird CSA 'ProtocolSliceNumber' (System/Miscellaneous/ImageNumbering reversed): VALIDATE SLICETIMING AND BVECS\n");
		//https://www.healthcare.siemens.com/siemens_hwem-hwem_ssxa_websites-context-root/wcm/idc/groups/public/@global/@imaging/@mri/documents/download/mdaz/nzmy/~edisp/mri_60_graessner-01646277.pdf
		//see https://github.com/neurolabusc/dcm2niix/issues/40
		sliceDir = -1; //not sure how to handle negative determinants?
	}
	if (sliceDir < 0) {
    	if ((dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_UIH) || (dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_GE))
    		dcmList[dcmSort[0].indx].CSA.protocolSliceNumber1 = -1;
    }
	reverseSliceTiming(d0, verbose, hdr->dim[3]);
	return sliceDir;
} //sliceTiming()

/*void reportMat44o(char *str, mat44 A) {
    printMessage("%s = [%g %g %g %g; %g %g %g %g; %g %g %g %g; 0 0 0 1]\n",str,
           A.m[0][0],A.m[0][1],A.m[0][2],A.m[0][3],
           A.m[1][0],A.m[1][1],A.m[1][2],A.m[1][3],
           A.m[2][0],A.m[2][1],A.m[2][2],A.m[2][3]);
}*/

int saveDcm2NiiCore(int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TSearchList *nameList, struct TDCMopts opts, struct TDTI4D *dti4D, int segVol) {
    bool iVaries = intensityScaleVaries(nConvert,dcmSort,dcmList);
    float *sliceMMarray = NULL; //only used if slices are not equidistant
    uint64_t indx = dcmSort[0].indx;
    uint64_t indx0 = dcmSort[0].indx;
    uint64_t indx1 = indx0;
    if (nConvert > 1) indx1 = dcmSort[1].indx;
    uint64_t indxEnd = dcmSort[nConvert-1].indx;
    #ifdef newTilt //see issue 254
    if ((nConvert > 1) && ((dcmList[indx0].modality == kMODALITY_CT) || (dcmList[indx0].isXRay) || (dcmList[indx0].gantryTilt > 0.0))) {
    	dcmList[indx0].gantryTilt = computeGantryTiltPrecise(dcmList[indx0], dcmList[indxEnd], opts.isVerbose);
    	if (isnan(dcmList[indx0].gantryTilt)) return EXIT_FAILURE;
    }
    #endif //newTilt see issue 254
    if ((dcmList[indx].isXA10A) && (dcmList[indx].CSA.mosaicSlices < 0)) {
    	printMessage("Siemens XA10 Mosaics are not primary images and lack vital data.\n");
    	printMessage(" See https://github.com/rordenlab/dcm2niix/issues/236\n");
    	#ifdef mySaveXA10Mosaics
		int n;
		printMessage("INPUT REQUIRED FOR %s\n", dcmList[indx].imageBaseName);
		printMessage("PLEASE ENTER NUMBER OF SLICES IN MOSAIC:\n");
		scanf ("%d",&n);
		for (int i = 0; i < nConvert; i++)
			dcmList[dcmSort[i].indx].CSA.mosaicSlices = n;
		#endif
    }
    if (opts.isIgnoreDerivedAnd2D && dcmList[indx].isDerived) {
    	printMessage("Ignoring derived image(s) of series %ld %s\n", dcmList[indx].seriesNum,  nameList->str[indx]);
    	return EXIT_SUCCESS;
    }
    if ((opts.isIgnoreDerivedAnd2D) && ((dcmList[indx].isLocalizer)  || (strcmp(dcmList[indx].sequenceName, "_tfl2d1")== 0) || (strcmp(dcmList[indx].sequenceName, "_fl3d1_ns")== 0) || (strcmp(dcmList[indx].sequenceName, "_fl2d1")== 0)) ) {
    	printMessage("Ignoring localizer (sequence %s) of series %ld %s\n", dcmList[indx].sequenceName, dcmList[indx].seriesNum,  nameList->str[indx]);
    	return EXIT_SUCCESS;
    }
    if ((opts.isIgnoreDerivedAnd2D) && (nConvert < 2) && (dcmList[indx].CSA.mosaicSlices < 2) && (dcmList[indx].xyzDim[3] < 2)) {
    	printMessage("Ignoring 2D image of series %ld %s\n", dcmList[indx].seriesNum,  nameList->str[indx]);
    	return EXIT_SUCCESS;
    }
    if (dcmList[indx].manufacturer == kMANUFACTURER_UNKNOWN)
		printWarning("Unable to determine manufacturer (0008,0070), so conversion is not tuned for vendor.\n");
    #ifdef myForce3DPhaseRealImaginary //compiler option: segment each phase/real/imaginary map
    bool saveAs3D = dcmList[indx].isHasPhase || dcmList[indx].isHasReal  || dcmList[indx].isHasImaginary;
    #else
    bool saveAs3D = false;
    #endif
    struct nifti_1_header hdr0;
    unsigned char * img = nii_loadImgXL(nameList->str[indx], &hdr0,dcmList[indx], iVaries, opts.compressFlag, opts.isVerbose, dti4D);
    if (strlen(opts.imageComments) > 0) {
    	for (int i = 0; i < 24; i++) hdr0.aux_file[i] = 0; //remove dcm.imageComments
        snprintf(hdr0.aux_file,24,"%s",opts.imageComments);
    }
    if (opts.isVerbose)
        printMessage("Converting %s\n",nameList->str[indx]);
    if (img == NULL) return EXIT_FAILURE;
    //if (iVaries) img = nii_iVaries(img, &hdr0);
    size_t imgsz = nii_ImgBytes(hdr0);
    unsigned char *imgM = (unsigned char *)malloc(imgsz* (uint64_t)nConvert);
    memcpy(&imgM[0], &img[0], imgsz);
    free(img);
    bool isReorder = false;
    //printMessage(" %d %d %d %d %lu\n", hdr0.dim[1], hdr0.dim[2], hdr0.dim[3], hdr0.dim[4], (unsigned long)[imgM length]);
    if (nConvert > 1) {
        //next: detect trigger time see example https://www.slicer.org/wiki/Documentation/4.4/Modules/MultiVolumeExplorer
        double triggerDx = dcmList[dcmSort[nConvert-1].indx].triggerDelayTime - dcmList[indx0].triggerDelayTime;
        dcmList[indx0].triggerDelayTime = triggerDx;
        //next: determine gantry tilt
        if (dcmList[indx0].gantryTilt != 0.0f)
            printWarning("Note these images have gantry tilt of %g degrees (manufacturer ID = %d)\n", dcmList[indx0].gantryTilt, dcmList[indx0].manufacturer);
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
                printMessage(" stacking %d acquisitions as a single volume\n", nAcq);
                //some Siemens CT scans use multiple acquisitions for a single volume, perhaps also check that slice position does not repeat?
                hdr0.dim[3] = nConvert;
            } else*/ if ( (nAcq > 1) && ((nConvert/nAcq) > 1) && ((nConvert%nAcq)==0) ) {
                hdr0.dim[3] = nConvert/nAcq;
                hdr0.dim[4] = nAcq;
                hdr0.dim[0] = 4;
            } else if ((dcmList[indx0].isXA10A) && (nConvert > nAcq) && (nAcq > 1) ) {
            	nAcq -= 1;
                hdr0.dim[3] = nConvert/nAcq;
                hdr0.dim[4] = nAcq;
                hdr0.dim[0] = 4;
                if ((nAcq > 1) && (nConvert != nAcq)) {
                	printMessage("Slice positions repeated, but number of slices (%d) not divisible by number of repeats (%d): converting only complete volumes.\n", nConvert, nAcq);
                }
            } else {
                hdr0.dim[3] = nConvert;
                if ((nAcq > 1) && (nConvert != nAcq)) {
                	printMessage("Slice positions repeated, but number of slices (%d) not divisible by number of repeats (%d): missing images?\n", nConvert, nAcq);
                	if (dcmList[indx0].locationsInAcquisition > 0)
                		 printMessage("Hint: expected %d locations", dcmList[indx0].locationsInAcquisition);
                }
            }
            //next options removed: features now thoroughly detected in nii_loadDir()
			for (int i = 0; i < nConvert; i++) { //make sure 1st volume describes shared features
				if (dcmList[dcmSort[i].indx].isCoilVaries) dcmList[indx0].isCoilVaries = true;
				if (dcmList[dcmSort[i].indx].isMultiEcho) dcmList[indx0].isMultiEcho = true;
			  	if (dcmList[dcmSort[i].indx].isNonParallelSlices) dcmList[indx0].isNonParallelSlices = true;
			  	if (dcmList[dcmSort[i].indx].isHasPhase) dcmList[indx0].isHasPhase = true;
				if (dcmList[dcmSort[i].indx].isHasReal) dcmList[indx0].isHasReal = true;
				if (dcmList[dcmSort[i].indx].isHasImaginary) dcmList[indx0].isHasImaginary = true;
			}
			//next: detect variable inter-volume time https://github.com/rordenlab/dcm2niix/issues/184
    		if (dcmList[indx0].modality == kMODALITY_PT) {
				bool trVaries = false;
				bool dayVaries = false;
				float tr = -1;
				uint64_t prevVolIndx = indx0;
				for (int i = 0; i < nConvert; i++)
						if (isSamePosition(dcmList[indx0],dcmList[dcmSort[i].indx])) {
							float trDiff = acquisitionTimeDifference(&dcmList[prevVolIndx], &dcmList[dcmSort[i].indx]);
							prevVolIndx = dcmSort[i].indx;
							if (trDiff <= 0) continue;
							if (tr < 0) tr = trDiff;
							if (trDiff < 0) dayVaries = true;
							if (!isSameFloatGE(tr,trDiff))
								trVaries = true;
						}
				if (trVaries) {
					if (dayVaries)
						printWarning("Seconds between volumes varies (perhaps run through midnight)\n");
					else
						printWarning("Seconds between volumes varies\n");
					// saveAs3D = true;
					//  printWarning("Creating independent volumes as time between volumes varies\n");
					if (opts.isVerbose) {
						printMessage(" OnsetTime = [");
						for (int i = 0; i < nConvert; i++)
								if (isSamePosition(dcmList[indx0],dcmList[dcmSort[i].indx])) {
									float trDiff = acquisitionTimeDifference(&dcmList[indx0], &dcmList[dcmSort[i].indx]);
									printMessage(" %g", trDiff);
								}
						printMessage(" ]\n");
					}
				} //if trVaries
            } //if PET
            
            //next: detect variable inter-slice distance
            float dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
			#ifdef myInstanceNumberOrderIsNotSpatial
			if  (!isSameFloat(dx, 0.0)) //only for XYZT, not TXYZ: perhaps run for swapDim3Dim4? Extremely rare anomaly
            	if (!ensureSequentialSlicePositions(hdr0.dim[3],hdr0.dim[4], dcmSort, dcmList))
            		dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
            indx0 = dcmSort[0].indx;
   			if (nConvert > 1) indx1 = dcmSort[1].indx;
   			isReorder = true;
            #endif
            bool dxVaries = false;
            for (int i = 1; i < nConvert; i++)
                if (!isSameFloatT(dx,intersliceDistance(dcmList[dcmSort[i-1].indx],dcmList[dcmSort[i].indx]),0.2))
                    dxVaries = true;
            if (hdr0.dim[4] < 2) {
                if (dxVaries) {
                    sliceMMarray = (float *) malloc(sizeof(float)*nConvert);
                    sliceMMarray[0] = 0.0f;
                    printWarning("Interslice distance varies in this volume (incompatible with NIfTI format).\n");
                    if (opts.isVerbose) {
						printMessage("Dimensions %d %d %d %d nAcq %d nConvert %d\n", hdr0.dim[1], hdr0.dim[2], hdr0.dim[3], hdr0.dim[4], nAcq, nConvert);
						printMessage(" Distance from first slice:\n");
						printMessage("dx=[0");
						for (int i = 1; i < nConvert; i++) {
							float dx0 = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[i].indx]);
							printMessage(" %g", dx0);
							sliceMMarray[i] = dx0;
						}
						printMessage("]\n");
                    }
                    #ifndef myInstanceNumberOrderIsNotSpatial
                    //kludge to handle single volume without instance numbers (0020,0013), e.g. https://www.morphosource.org/Detail/MediaDetail/Show/media_id/8430
					bool isInconsistenSliceDir = false;
					int slicePositionRepeats = 1; //how many times is first position repeated
					if (nConvert > 2) {
						float dxPrev = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
						if (isSameFloatGE(dxPrev, 0.0)) slicePositionRepeats++;
						for (int i = 2; i < nConvert; i++) {
                        	float dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[i].indx]);
							if (dx < dxPrev)
								isInconsistenSliceDir = true;
							if (isSameFloatGE(dxPrev, 0.0)) slicePositionRepeats++;
							dxPrev = dx;
                    	}
					}
					if ((isInconsistenSliceDir) && (slicePositionRepeats == 1))  {
						//printWarning("Slice order as defined by instance number not spatially sequential.\n");
						//printWarning("Attempting to reorder slices based on spatial position.\n");
						ensureSequentialSlicePositions(hdr0.dim[3],hdr0.dim[4], dcmSort, dcmList);
						dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
						hdr0.pixdim[3] = dx;
						isInconsistenSliceDir = false;
						
						//code below duplicates prior code, could be written as modular function(s)
						
						
						//qball
						
						indx0 = dcmSort[0].indx;
   						if (nConvert > 1) indx1 = dcmSort[1].indx;
   						dxVaries = false;
   						dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
   						for (int i = 1; i < nConvert; i++)
                			if (!isSameFloatT(dx,intersliceDistance(dcmList[dcmSort[i-1].indx],dcmList[dcmSort[i].indx]),0.2))
                    			dxVaries = true;
   						for (int i = 1; i < nConvert; i++)
							sliceMMarray[i] = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[i].indx]);
						//printf("dx=[");
						//for (int i = 1; i < nConvert; i++)
						//	printf("%g ", intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[i].indx]) );
						//printf("\n");
						isReorder = true;
						bool isInconsistenSliceDir = false;
						int slicePositionRepeats = 1; //how many times is first position repeated
						if (nConvert > 2) {
							float dxPrev = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
							if (isSameFloatGE(dxPrev, 0.0)) slicePositionRepeats++;
							for (int i = 2; i < nConvert; i++) {
	                        	float dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[i].indx]);
								if (dx < dxPrev)
									isInconsistenSliceDir = true;
								if (isSameFloatGE(dxPrev, 0.0)) slicePositionRepeats++;
								dxPrev = dx;
	                    	}
						}
						if (!dxVaries) {
							printMessage("Slice re-ordering resolved inter-slice distance variability.\n");
							free(sliceMMarray);
							sliceMMarray = NULL;	
						}
                    							
					}
					if (isInconsistenSliceDir) {
						printMessage("Unable to equalize slice distances: slice order not consistently ascending.\n");
						printMessage("First spatial position repeated %d times\n", slicePositionRepeats);
						printError(" Recompiling with '-DmyInstanceNumberOrderIsNotSpatial' might help.\n");
						return EXIT_FAILURE;
					}
					#endif
					int imageNumRange = 1 + abs( dcmList[dcmSort[nConvert-1].indx].imageNum -  dcmList[dcmSort[0].indx].imageNum);
					if ((imageNumRange > 1) && (imageNumRange != nConvert)) {
						printWarning("Missing images? Expected %d images, but instance number (0020,0013) ranges from %d to %d\n", nConvert, dcmList[dcmSort[0].indx].imageNum, dcmList[dcmSort[nConvert-1].indx].imageNum);
						if (opts.isVerbose) {
							printMessage("instance=[");
							for (int i = 0; i < nConvert; i++) {
								printMessage(" %d", dcmList[dcmSort[i].indx].imageNum);
							}
							printMessage("]\n");
						}
                    } //imageNum not sequential
				} //dx varies
            } //not 4D
            if ((hdr0.dim[4] > 0) && (dxVaries) && (dx == 0.0) &&  ((dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_UNKNOWN)  || (dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_GE)  || (dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_PHILIPS))  ) { //Niels Janssen has provided GE sequential multi-phase acquisitions that also require swizzling
                swapDim3Dim4(hdr0.dim[3],hdr0.dim[4],dcmSort);
                dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
                if (opts.isVerbose)
                	printMessage("Swizzling 3rd and 4th dimensions (XYTZ -> XYZT), assuming interslice distance is %f\n",dx);
            }
            if ((dx == 0.0 ) && (!dxVaries)) { //all images are the same slice - 16 Dec 2014
                printWarning("All images appear to be a single slice - please check slice/vector orientation\n");
                hdr0.dim[3] = 1;
                hdr0.dim[4] = nConvert;
                hdr0.dim[0] = 4;
            }
            if ((dx > 0) && (!isSameFloatGE(dx, hdr0.pixdim[3])))
            	hdr0.pixdim[3] = dx;
            dcmList[dcmSort[0].indx].xyzMM[3] = dx; //16Sept2014 : correct DICOM for true distance between slice centers:
            // e.g. MCBI Siemens ToF 0018:0088 reports 16mm SpacingBetweenSlices, but actually 0.5mm
        } else if (hdr0.dim[4] < 2) {
            hdr0.dim[4] = nConvert;
            hdr0.dim[0] = 4;
        } else {
            hdr0.dim[5] = nConvert;
            hdr0.dim[0] = 5;
        }
        /*if (nConvert > 1) { //next determine if TR is true time between volumes
        	double startTime = dcmList[indx0].acquisitionTime;
        	double endTime = startTime;
        	for (int i = 1; i < nConvert; i++) {
            	double sliceTime = dcmList[dcmSort[i].indx].acquisitionTime;
            	if (sliceTime < startTime) startTime = sliceTime;
            	if (sliceTime > endTime) endTime = sliceTime;
            }
            double seriesTime = (endTime - startTime);
            if (endTime > 0)
            	printMessage("%g - %g = %g\n", endTime, startTime, seriesTime);

        }*/
        //printMessage(" %d %d %d %d %lu\n", hdr0.dim[1], hdr0.dim[2], hdr0.dim[3], hdr0.dim[4], (unsigned long)[imgM length]);
        struct nifti_1_header hdrI;
        //double time = -1.0;
        if ((!opts.isOnlyBIDS) && (nConvert > 1)) {
 			int iStart = 1;
 			if (isReorder) iStart = 0;
 			//for (int i = 1; i < nConvert; i++) { //<- works except where ensureSequentialSlicePositions() changes 1st slice
			for (int i = iStart; i < nConvert; i++) { //stack additional images
				indx = dcmSort[i].indx;
				//double time2 = dcmList[dcmSort[i].indx].acquisitionTime;
				//if (time != time2)
				//	printWarning("%g\n", time2);
				//time = time2;
				//if (headerDcm2Nii(dcmList[indx], &hdrI) == EXIT_FAILURE) return EXIT_FAILURE;
				img = nii_loadImgXL(nameList->str[indx], &hdrI, dcmList[indx],iVaries, opts.compressFlag, opts.isVerbose, dti4D);
				if (img == NULL) return EXIT_FAILURE;
				if ((hdr0.dim[1] != hdrI.dim[1]) || (hdr0.dim[2] != hdrI.dim[2]) || (hdr0.bitpix != hdrI.bitpix)) {
					printError("Image dimensions differ %s %s",nameList->str[dcmSort[0].indx], nameList->str[indx]);
					free(imgM);
					free(img);
					return EXIT_FAILURE;
				}
				memcpy(&imgM[(uint64_t)i*imgsz], &img[0], imgsz);
				free(img);
			}
        } //skip if we are only creating BIDS
        if (hdr0.dim[4] > 1) //for 4d datasets, last volume should be acquired before first
        	checkDateTimeOrder(&dcmList[dcmSort[0].indx], &dcmList[dcmSort[nConvert-1].indx]);
    }
	int sliceDir = sliceTimingCore(dcmSort, dcmList, &hdr0, opts.isVerbose, nameList->str[dcmSort[0].indx], nConvert);
    //move before headerDcm2Nii2 checkSliceTiming(&dcmList[indx0], &dcmList[indx1]);
    char pathoutname[2048] = {""};
    if (nii_createFilename(dcmList[dcmSort[0].indx], pathoutname, opts) == EXIT_FAILURE) {
        free(imgM);
        return EXIT_FAILURE;
    }
    if (strlen(pathoutname) <1) {
        free(imgM);
        return EXIT_FAILURE;
    }
    nii_SaveBIDS(pathoutname, dcmList[dcmSort[0].indx], opts, &hdr0, nameList->str[dcmSort[0].indx]);
    if (opts.isOnlyBIDS) {
    	//note we waste time loading every image, however this ensures hdr0 matches actual output
        free(imgM);
        return EXIT_SUCCESS;
    }
	if ((segVol >= 0) && (hdr0.dim[4] > 1)) {
    	int inVol = hdr0.dim[4];
    	int nVol = 0;
    	for (int v = 0; v < inVol; v++)
    		if (dti4D->gradDynVol[v] == segVol)
    			nVol ++;
    	if (nVol < 1) {
    		printError("Series %d does not exist\n", segVol);
    		return EXIT_FAILURE;
    	}
    	size_t imgsz4D = imgsz;
    	if (nVol < 2)
    		hdr0.dim[0] = 3; //3D
    	hdr0.dim[4] = 1;
    	size_t imgsz3D = nii_ImgBytes(hdr0);
		unsigned char *img4D = (unsigned char *)malloc(imgsz4D);
    	memcpy(&img4D[0], &imgM[0], imgsz4D);
    	free(imgM);
    	imgM = (unsigned char *)malloc(imgsz3D * nVol);
    	int outVol = 0;
    	for (int v = 0; v < inVol; v++) {
    		if ((dti4D->gradDynVol[v] == segVol) && (outVol < nVol)) {
    			memcpy(&imgM[outVol * imgsz3D], &img4D[v * imgsz3D], imgsz3D);
    			outVol ++;
    		}
    	}
    	hdr0.dim[4] = nVol;
		imgsz = nii_ImgBytes(hdr0);
    	free(img4D);
    	saveAs3D = false;
    }
    if (strlen(dcmList[dcmSort[0].indx].protocolName) < 1) //beware: tProtocolName can vary within a series "t1+AF8-mpr+AF8-ns+AF8-sag+AF8-p2+AF8-iso" vs "T1_mprage_ns_sag_p2_iso 1.0mm_192"
    	rescueProtocolName(&dcmList[dcmSort[0].indx], nameList->str[dcmSort[0].indx]);
    // Prevent these DICOM files from being reused.
    for(int i = 0; i < nConvert; ++i)
      dcmList[dcmSort[i].indx].converted2NII = 1;
    if (opts.numSeries < 0) { //report series number but do not convert
    	int segVolEcho = segVol;
    	if ((dcmList[dcmSort[0].indx].echoNum > 1) && (segVolEcho <= 0))
    		segVolEcho = dcmList[dcmSort[0].indx].echoNum+1;
    	if (segVolEcho >= 0) {
    		printMessage("\t%u.%d\t%s\n", dcmList[dcmSort[0].indx].seriesUidCrc, segVolEcho-1, pathoutname);
    		//printMessage("\t%ld.%d\t%s\n", dcmList[dcmSort[0].indx].seriesNum, segVol-1, pathoutname);
    	} else {
    		printMessage("\t%u\t%s\n", dcmList[dcmSort[0].indx].seriesUidCrc, pathoutname);
    		//printMessage("\t%ld\t%s\n", dcmList[dcmSort[0].indx].seriesNum, pathoutname);
        }
    	printMessage(" %s\n",nameList->str[dcmSort[0].indx]);
    	return EXIT_SUCCESS;
    }
	if (sliceDir < 0) {
        imgM = nii_flipZ(imgM, &hdr0);
        sliceDir = abs(sliceDir); //change this, we have flipped the image so GE DTI bvecs no longer need to be flipped!
    }
    // skip converting if user has specified one or more series, but has not specified this one
    if (opts.numSeries > 0) {
      int i = 0;
      //double seriesNum = (double) dcmList[dcmSort[0].indx].seriesNum;
      double seriesNum = (double) dcmList[dcmSort[0].indx].seriesUidCrc;
      int segVolEcho = segVol;
      if ((dcmList[dcmSort[0].indx].echoNum > 1) && (segVolEcho <= 0))
      		segVolEcho = dcmList[dcmSort[0].indx].echoNum+1;
      if (segVolEcho > 0)
      	seriesNum = seriesNum + ((double) segVolEcho - 1.0) / 10.0;
      for (; i < opts.numSeries; i++) {
        if (isSameDouble(opts.seriesNumber[i], seriesNum)) { 
        //if (opts.seriesNumber[i] == dcmList[dcmSort[0].indx].seriesNum) {
          break;
        }
      }
      if (i == opts.numSeries) {
        return EXIT_SUCCESS;
      }
    }
	nii_saveText(pathoutname, dcmList[dcmSort[0].indx], opts, &hdr0, nameList->str[indx]);
	int numADC = 0;
    int * volOrderIndex = nii_saveDTI(pathoutname,nConvert, dcmSort, dcmList, opts, sliceDir, dti4D, &numADC);
    PhilipsPrecise(&dcmList[dcmSort[0].indx], opts.isPhilipsFloatNotDisplayScaling, &hdr0, opts.isVerbose);
    if ((dcmList[dcmSort[0].indx].bitsStored == 12) && (dcmList[dcmSort[0].indx].bitsAllocated == 16))
    	nii_mask12bit(imgM, &hdr0);
    if ((opts.isMaximize16BitRange == kMaximize16BitRange_True) && (hdr0.datatype == DT_INT16)) {
    	nii_scale16bitSigned(imgM, &hdr0, opts.isVerbose); //allow INT16 to use full dynamic range
    } else if ((opts.isMaximize16BitRange  == kMaximize16BitRange_True) && (hdr0.datatype == DT_UINT16) &&  (!dcmList[dcmSort[0].indx].isSigned)) {
    	nii_scale16bitUnsigned(imgM, &hdr0, opts.isVerbose); //allow UINT16 to use full dynamic range
    } else if ((opts.isMaximize16BitRange == kMaximize16BitRange_False) && (hdr0.datatype == DT_UINT16) &&  (!dcmList[dcmSort[0].indx].isSigned))
    	nii_check16bitUnsigned(imgM, &hdr0, opts.isVerbose); //save UINT16 as INT16 if we can do this losslessly
    printMessage( "Convert %d DICOM as %s (%dx%dx%dx%d)\n",  nConvert, pathoutname, hdr0.dim[1],hdr0.dim[2],hdr0.dim[3],hdr0.dim[4]);
    #ifndef USING_R
    fflush(stdout); //show immediately if run from MRIcroGL GUI
    #endif
	//~ if (!dcmList[dcmSort[0].indx].isSlicesSpatiallySequentialPhilips)
    //~ 	nii_reorderSlices(imgM, &hdr0, dti4D);
    //hdr0.pixdim[3] = dxNoTilt;
    if (hdr0.dim[3] < 2)
    	printWarning("Check that 2D images are not mirrored.\n");
#ifndef USING_R
    else
        fflush(stdout); //GUI buffers printf, display all results
#endif
    if ((opts.isRotate3DAcq) && (dcmList[dcmSort[0].indx].is3DAcq) && (hdr0.dim[3] > 1) && (hdr0.dim[0] < 4))
        imgM = nii_setOrtho(imgM, &hdr0); //printMessage("ortho %d\n", echoInt (33));
    else if (opts.isFlipY)//(FLIP_Y) //(dcmList[indx0].CSA.mosaicSlices < 2) &&
        imgM = nii_flipY(imgM, &hdr0);
    else
    	printMessage("DICOM row order preserved: may appear upside down in tools that ignore spatial transforms\n");
	//begin: gantry tilt we need to save the shear in the transform
	mat44 sForm;
	LOAD_MAT44(sForm,
	    hdr0.srow_x[0],hdr0.srow_x[1],hdr0.srow_x[2],hdr0.srow_x[3],
		hdr0.srow_y[0],hdr0.srow_y[1],hdr0.srow_y[2],hdr0.srow_y[3],
		hdr0.srow_z[0],hdr0.srow_z[1],hdr0.srow_z[2],hdr0.srow_z[3]);
	if (!isSameFloatGE(dcmList[indx0].gantryTilt, 0.0)) {
    	float thetaRad = dcmList[indx0].gantryTilt * M_PI / 180.0;
    	float c = cos(thetaRad);
    	if (!isSameFloatGE(c, 0.0)) {
    		mat33 shearMat;
    		LOAD_MAT33(shearMat, 1.0, 0.0, 0.0,
    			0.0, 1.0, sin(thetaRad)/c,
    			0.0, 0.0, 1.0);
    		mat33 s;
    		LOAD_MAT33(s,hdr0.srow_x[0],hdr0.srow_x[1],hdr0.srow_x[2],
    			hdr0.srow_y[0],hdr0.srow_y[1],hdr0.srow_y[2],
              	hdr0.srow_z[0],hdr0.srow_z[1],hdr0.srow_z[2]);
			s = nifti_mat33_mul(shearMat, s);
			mat44 shearForm;
			LOAD_MAT44(shearForm, s.m[0][0],s.m[0][1],s.m[0][2],hdr0.srow_x[3],
				s.m[1][0],s.m[1][1],s.m[1][2],hdr0.srow_y[3],
				s.m[2][0],s.m[2][1],s.m[2][2],hdr0.srow_z[3]);
			setQSForm(&hdr0,shearForm, true);
    	} //avoid div/0: cosine not zero
    } //if gantry tilt
 	//end: gantry tilt we need to save the shear in the transform
    int returnCode = EXIT_FAILURE;
#ifndef myNoSave
    // Indicates success or failure of the (last) save
    //printMessage(" x--> %d ----\n", nConvert);
    if (! opts.isRGBplanar) //save RGB as packed RGBRGBRGB... instead of planar RRR..RGGG..GBBB..B
        imgM = nii_planar2rgb(imgM, &hdr0, true); //NIfTI is packed while Analyze was planar
    if ((hdr0.dim[4] > 1) && (saveAs3D))
        returnCode = nii_saveNII3D(pathoutname, hdr0, imgM,opts, dcmList[dcmSort[0].indx]);
    else {
        if (volOrderIndex) //reorder volumes
        	imgM = reorderVolumes(&hdr0, imgM, volOrderIndex);
#ifndef USING_R
		if ((opts.isIgnoreDerivedAnd2D) && (numADC > 0))
			printMessage("Ignoring derived diffusion image(s). Better isotropic and ADC maps can be generated later processing.\n");
		if ((!opts.isIgnoreDerivedAnd2D) && (numADC > 0)) {//ADC maps can disrupt analysis: save a copy with the ADC map, and another without
			char pathoutnameADC[2048] = {""};
			strcat(pathoutnameADC,pathoutname);
			strcat(pathoutnameADC,"_ADC");
			if (opts.isSave3D)
				nii_saveNII3D(pathoutnameADC, hdr0, imgM, opts, dcmList[dcmSort[0].indx]);
			else
				nii_saveNII(pathoutnameADC, hdr0, imgM, opts, dcmList[dcmSort[0].indx]);
		}
#endif
		imgM = removeADC(&hdr0, imgM, numADC);
#ifndef USING_R
		if (opts.isSaveNRRD)
			returnCode = nii_saveNRRD(pathoutname, hdr0, imgM, opts, dcmList[dcmSort[0].indx], dti4D, dcmList[indx0].CSA.numDti);
		else if (opts.isSave3D)
			returnCode = nii_saveNII3D(pathoutname, hdr0, imgM, opts, dcmList[dcmSort[0].indx]);
		else
        	returnCode = nii_saveNII(pathoutname, hdr0, imgM, opts, dcmList[dcmSort[0].indx]);
#endif
    }
#endif
    if (dcmList[indx0].gantryTilt != 0.0) {
    	setQSForm(&hdr0,sForm, true);
        //if (dcmList[indx0].isResampled) { //we no detect based on image orientation https://github.com/rordenlab/dcm2niix/issues/253
        //    printMessage("Tilt correction skipped: 0008,2111 reports RESAMPLED\n");
        //} else
        if (opts.isTiltCorrect) {
        	imgM = nii_saveNII3Dtilt(pathoutname, &hdr0, imgM,opts, dcmList[dcmSort[0].indx], sliceMMarray, dcmList[indx0].gantryTilt, dcmList[indx0].manufacturer);
            strcat(pathoutname,"_Tilt");
        } else
            printMessage("Tilt correction skipped\n");
    }
    if (sliceMMarray != NULL) {
        if (dcmList[indx0].isResampled) {
            printMessage("Slice thickness correction skipped: 0008,2111 reports RESAMPLED\n");
        }
        else
            returnCode = nii_saveNII3Deq(pathoutname, hdr0, imgM,opts, dcmList[dcmSort[0].indx], sliceMMarray);
        free(sliceMMarray);
    }
    if ((opts.isRotate3DAcq) && (opts.isCrop) && (dcmList[indx0].is3DAcq)   && (hdr0.dim[3] > 1) && (hdr0.dim[0] < 4))//for T1 scan: && (dcmList[indx0].TE < 25)
        returnCode = nii_saveCrop(pathoutname, hdr0, imgM, opts, dcmList[dcmSort[0].indx]); //n.b. must be run AFTER nii_setOrtho()!
#ifdef USING_R
    // Note that for R, only one image should be created per series
    // Hence this extra test
	if (returnCode != EXIT_SUCCESS)
        returnCode = nii_saveNII(pathoutname, hdr0, imgM, opts, dcmList[dcmSort[0].indx]);
    if (returnCode == EXIT_SUCCESS)
        nii_saveAttributes(dcmList[dcmSort[0].indx], hdr0, opts, nameList->str[dcmSort[0].indx]);
#endif
    free(imgM);
    return returnCode;//EXIT_SUCCESS;
}// saveDcm2NiiCore()

int saveDcm2Nii(int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TSearchList *nameList, struct TDCMopts opts, struct TDTI4D *dti4D) {
	//this wrapper does nothing if all the images share the same echo time and scale
	// however, it segments images when these properties vary
	uint64_t indx = dcmSort[0].indx;
	if ((!dcmList[indx].isScaleOrTEVaries) || (dcmList[indx].xyzDim[4] < 2))
		return saveDcm2NiiCore(nConvert, dcmSort, dcmList, nameList, opts, dti4D, -1);
	if ((dcmList[indx].xyzDim[4]) && (dti4D->sliceOrder[0] < 0)) {
		printError("Unexpected error for image with varying echo time or intensity scaling\n");
		return EXIT_FAILURE;
	}
    int ret = EXIT_SUCCESS;
	//check for repeated echoes - count unique number of echoes
	//code below checks for multi-echoes - not required if maxNumberOfEchoes reported in PARREC
	int echoNum[kMaxDTI4D];
	int echo = 1;
	for (int i = 0; i < dcmList[indx].xyzDim[4]; i++)
		echoNum[i] = 0;
	echoNum[0] = 1;
	for (int i = 1; i < dcmList[indx].xyzDim[4]; i++) {
		for (int j = 0; j < i; j++)
			if (dti4D->TE[i] == dti4D->TE[j]) echoNum[i] = echoNum[j];
		if (echoNum[i] == 0) {
			echo++;
			echoNum[i] = echo;
		}
	}
	if (echo > 1) dcmList[indx].isMultiEcho = true;
	//check for repeated volumes
	int series = 1;
	for (int i = 0; i < dcmList[indx].xyzDim[4]; i++)
		dti4D->gradDynVol[i] = 0;
	dti4D->gradDynVol[0] = 1;
	for (int i = 1; i < dcmList[indx].xyzDim[4]; i++) {
		for (int j = 0; j < i; j++)
			if (isSameFloatGE(dti4D->triggerDelayTime[i], dti4D->triggerDelayTime[j]) && (dti4D->intenIntercept[i] == dti4D->intenIntercept[j]) && (dti4D->intenScale[i] == dti4D->intenScale[j]) && (dti4D->isReal[i] == dti4D->isReal[j]) && (dti4D->isImaginary[i] == dti4D->isImaginary[j]) && (dti4D->isPhase[i] == dti4D->isPhase[j]) && (dti4D->TE[i] == dti4D->TE[j]))
				dti4D->gradDynVol[i] = dti4D->gradDynVol[j];
		if (dti4D->gradDynVol[i] == 0) {
			series++;
			dti4D->gradDynVol[i] = series;
		}
	}
	//bvec/bval saved for each series (real, phase, magnitude, imaginary) https://github.com/rordenlab/dcm2niix/issues/219
	TDTI4D dti4Ds;
	dti4Ds = *dti4D;
	bool isHasDti = (dcmList[indx].CSA.numDti > 0);
	if ((isHasDti) && (dcmList[indx].CSA.numDti == dcmList[indx].xyzDim[4])) {
		int nDti = 0;
		for (int i = 0; i < dcmList[indx].xyzDim[4]; i++) {
			if (dti4D->gradDynVol[i] == 1) {
				dti4Ds.S[nDti].V[0] = dti4Ds.S[i].V[0];
				dti4Ds.S[nDti].V[1] = dti4Ds.S[i].V[1];
				dti4Ds.S[nDti].V[2] = dti4Ds.S[i].V[2];
				dti4Ds.S[nDti].V[3] = dti4Ds.S[i].V[3];
				nDti++;
			}
		}
		dcmList[indx].CSA.numDti = nDti;
	}
	//save each series
	for (int s = 1; s <= series; s++) {
		for (int i = 0; i < dcmList[indx].xyzDim[4]; i++) {
			 if (dti4D->gradDynVol[i] == s) {
			 	//dti4D->gradDynVol[i] = s;
				//nVol ++;
				dcmList[indx].TE = dti4D->TE[i];
				dcmList[indx].intenScale = dti4D->intenScale[i];
				dcmList[indx].intenIntercept = dti4D->intenIntercept[i];
				dcmList[indx].isHasPhase = dti4D->isPhase[i];
				dcmList[indx].isHasReal = dti4D->isReal[i];
				dcmList[indx].isHasImaginary = dti4D->isImaginary[i];
				dcmList[indx].intenScalePhilips = dti4D->intenScalePhilips[i];
				dcmList[indx].RWVScale = dti4D->RWVScale[i];
				dcmList[indx].RWVIntercept = dti4D->RWVIntercept[i];
				dcmList[indx].triggerDelayTime = dti4D->triggerDelayTime[i];
				dcmList[indx].isHasMagnitude = false;
				dcmList[indx].echoNum = echoNum[i];
				break;
			}
		}
		if (s > 1) dcmList[indx].CSA.numDti = 0; //only save bvec for first type (magnitude)
		int ret2 = saveDcm2NiiCore(nConvert, dcmSort, dcmList, nameList, opts, &dti4Ds, s);
        if (ret2 != EXIT_SUCCESS) ret = ret2; //return EXIT_SUCCESS only if ALL are successful
	}
    return ret;
}// saveDcm2Nii()

void fillTDCMsort(struct TDCMsort& tdcmref, const uint64_t indx, const struct TDICOMdata& dcmdata){
  // Copy the relevant parts of dcmdata to tdcmref.
  tdcmref.indx = indx;
  //printf("series/image %d %d\n", dcmdata.seriesNum, dcmdata.imageNum);
  tdcmref.img = ((uint64_t)dcmdata.seriesNum << 32) + dcmdata.imageNum;
  for(int i = 0; i < MAX_NUMBER_OF_DIMENSIONS; ++i)
    tdcmref.dimensionIndexValues[i] = dcmdata.dimensionIndexValues[i];
  //lines below added to cope with extreme anonymization
  // https://github.com/rordenlab/dcm2niix/issues/211
  if (tdcmref.dimensionIndexValues[MAX_NUMBER_OF_DIMENSIONS-1] != 0) return;
  //Since dimensionIndexValues are indexed from 1, 0 indicates unused
  // we leverage this as a hail mary attempt to distinguish images with identical series and instance numbers
  //See Correction Number CP-1242:
  //  "Clarify in the description of dimension indices ... start from 1"
  //  0008,0032 stored as HHMMSS.FFFFFF, there are 86400000 ms per day
  //  dimensionIndexValues stored as uint32, so encode acquisition time in ms
  uint32_t h = trunc(dcmdata.acquisitionTime / 10000.0);
  double tm = dcmdata.acquisitionTime - (h * 10000.0);
  uint32_t m = trunc(tm / 100.0);
  tm = tm - (m * 100.0);
  uint32_t ms = round(tm * 1000);
  ms += (h * 3600000) + (m * 60000);
  //printf("HHMMSS.FFFF %.5f ->  %d  ms\n",  dcmdata.acquisitionTime, ms);
  tdcmref.dimensionIndexValues[MAX_NUMBER_OF_DIMENSIONS-1] = ms;
} // fillTDCMsort()

int compareTDCMsort(void const *item1, void const *item2) {
	//for quicksort http://blog.ablepear.com/2011/11/objective-c-tuesdays-sorting-arrays.html
	struct TDCMsort const *dcm1 = (const struct TDCMsort *)item1;
	struct TDCMsort const *dcm2 = (const struct TDCMsort *)item2;
	//to do: detect duplicates with SOPInstanceUID (0008,0018) - accurate but slow text comparison
	int retval = 0;   // tie
	if (dcm1->img < dcm2->img)
		retval = -1;
	else if (dcm1->img > dcm2->img)
		retval = 1;
	//printf("%d  %d\n",  dcm1->img, dcm2->img);
	//for(int i=0; i < MAX_NUMBER_OF_DIMENSIONS; i++)
	//	printf("%d  %d\n", dcm1->dimensionIndexValues[i], dcm2->dimensionIndexValues[i]);
	if(retval != 0) return retval; //sorted images
	// Check the dimensionIndexValues (useful for enhanced DICOM 4D series).
	// ->img is basically behaving as a (seriesNum, imageNum) sort key
	// concatenated into a (large) integer for qsort.  That is unwieldy when
	// dimensionIndexValues need to be compared, because the existence of
	// uint128_t, uint256_t, etc. is not guaranteed.  This sorts by
	// (seriesNum, ImageNum, div[0], div[1], ...), or if you think of it as a
	// number, the dimensionIndexValues come after the decimal point.
	for(int i=0; i < MAX_NUMBER_OF_DIMENSIONS; ++i){
		if(dcm1->dimensionIndexValues[i] < dcm2->dimensionIndexValues[i])
		  return -1;
		else if(dcm1->dimensionIndexValues[i] > dcm2->dimensionIndexValues[i])
		  return 1;
	}
	return retval;
} //compareTDCMsort()
/*int compareTDCMsort(void const *item1, void const *item2) {
    //for quicksort http://blog.ablepear.com/2011/11/objective-c-tuesdays-sorting-arrays.html
    struct TDCMsort const *dcm1 = (const struct TDCMsort *)item1;
    struct TDCMsort const *dcm2 = (const struct TDCMsort *)item2;

    int retval = 0;   // tie

    if (dcm1->img < dcm2->img)
        retval = -1;
    else if (dcm1->img > dcm2->img)
        retval = 1;

    if(retval == 0){
      // Check the dimensionIndexValues (useful for enhanced DICOM 4D series).
      // ->img is basically behaving as a (seriesNum, imageNum) sort key
      // concatenated into a (large) integer for qsort.  That is unwieldy when
      // dimensionIndexValues need to be compared, because the existence of
      // uint128_t, uint256_t, etc. is not guaranteed.  This sorts by
      // (seriesNum, ImageNum, div[0], div[1], ...), or if you think of it as a
      // number, the dimensionIndexValues come after the decimal point.
      for(int i=0; i < MAX_NUMBER_OF_DIMENSIONS; ++i){
        if(dcm1->dimensionIndexValues[i] < dcm2->dimensionIndexValues[i]){
          retval = -1;
          break;
        }
        else if(dcm1->dimensionIndexValues[i] > dcm2->dimensionIndexValues[i]){
          retval = 1;
          break;
        }
      }
    }
    return retval;
} //compareTDCMsort()*/

/*int isSameFloatGE (float a, float b) {
//Kludge for bug in 0002,0016="DIGITAL_JACKET", 0008,0070="GE MEDICAL SYSTEMS" DICOM data: Orient field (0020:0037) can vary 0.00604261 == 0.00604273 !!!
    //return (a == b); //niave approach does not have any tolerance for rounding errors
    return (fabs (a - b) <= 0.0001);
}*/

int isSameFloatDouble (double a, double b) {
    //Kludge for bug in 0002,0016="DIGITAL_JACKET", 0008,0070="GE MEDICAL SYSTEMS" DICOM data: Orient field (0020:0037) can vary 0.00604261 == 0.00604273 !!!
    // return (a == b); //niave approach does not have any tolerance for rounding errors
    return (fabs (a - b) <= 0.0001);
}

struct TWarnings { //generate a warning only once per set
        bool acqNumVaries, bitDepthVaries, dateTimeVaries, echoVaries, phaseVaries, coilVaries, forceStackSeries, seriesUidVaries, nameVaries, nameEmpty, orientVaries;
};

TWarnings setWarnings() {
	TWarnings r;
	r.acqNumVaries = false;
	r.bitDepthVaries = false;
	r.dateTimeVaries = false;
	r.phaseVaries = false;
	r.echoVaries = false;
	r.coilVaries = false;
	r.seriesUidVaries = false;
	r.forceStackSeries = false;
	r.nameVaries = false;
	r.nameEmpty = false;
	r.orientVaries = false;
	return r;
}

bool isSameSet (struct TDICOMdata d1, struct TDICOMdata d2, struct TDCMopts* opts, struct TWarnings* warnings, bool *isMultiEcho, bool *isNonParallelSlices, bool *isCoilVaries) {
    //returns true if d1 and d2 should be stacked together as a single output
    if (!d1.isValid) return false;
    if (!d2.isValid) return false;
    if (d1.modality != d2.modality) return false; //do not stack MR and CT data!
    if (d1.isDerived != d2.isDerived) return false; //do not stack raw and derived image types
    if (d1.manufacturer != d2.manufacturer) return false; //do not stack data from different vendors
	bool isForceStackSeries = false;
	if ((opts->isForceStackDCE) && (d1.isStackableSeries) && (d2.isStackableSeries) && (d1.seriesNum != d2.seriesNum)) {
		if (!warnings->forceStackSeries)
        	printMessage("DCE volumes stacked despite varying series number (use '-m o' to turn off merging).\n");
        warnings->forceStackSeries = true;
        isForceStackSeries = true;
	}
	if ((d1.manufacturer == kMANUFACTURER_SIEMENS) && (strcmp(d1.protocolName, d2.protocolName) == 0) && (strlen(d1.softwareVersions) > 4) && (strlen(d1.sequenceName) > 4) && (strlen(d2.sequenceName) > 4))  {
		if (strstr(d1.sequenceName, "_ep_b") && strstr(d2.sequenceName, "_ep_b") && (strstr(d1.softwareVersions, "VB13") || strstr(d1.softwareVersions, "VB12"))  ) {
			//Siemens B12/B13 users with a "DWI" but not "DTI" license would ofter create multi-series acquisitions
			if (!warnings->forceStackSeries)
        		printMessage("Diffusion images stacked despite varying series number (early Siemens DTI).\n");
        	warnings->forceStackSeries = true;
        	isForceStackSeries = true;
		}
	}
	if (isForceStackSeries)
		;
	else if ((d1.isXA10A) && (d2.isXA10A) && (d1.seriesNum > 1000) && (d2.seriesNum > 1000)) {
		//kludge XA10A (0020,0011) increments [16001, 16002, ...] https://github.com/rordenlab/dcm2niix/issues/236
		//images from series 16001,16002 report different study times (0008,0030)!
		if ((d1.seriesNum / 1000) != (d2.seriesNum / 1000)) return false;
	} else if (d1.seriesNum != d2.seriesNum) return false;
	#ifdef mySegmentByAcq
    if (d1.acquNum != d2.acquNum) return false;
    #endif
    bool isSameStudyInstanceUID = false;
    if ((strlen(d1.studyInstanceUID)> 1) && (strlen(d2.studyInstanceUID)> 1)) {
    	if (strcmp(d1.studyInstanceUID, d2.studyInstanceUID) == 0)
			isSameStudyInstanceUID = true;
    }
    bool isSameTime = isSameFloatDouble(d1.dateTime, d2.dateTime);
    if ((isSameStudyInstanceUID) && (d1.isXA10A) && (d2.isXA10A))
		isSameTime = true; //kludge XA10A 0008,0030 incorrect https://github.com/rordenlab/dcm2niix/issues/236
    if ((!isSameStudyInstanceUID) && (!isSameTime)) return false;
    if ((d1.bitsAllocated != d2.bitsAllocated) || (d1.xyzDim[1] != d2.xyzDim[1]) || (d1.xyzDim[2] != d2.xyzDim[2]) || (d1.xyzDim[3] != d2.xyzDim[3]) ) {
        if (!warnings->bitDepthVaries)
        	printMessage("Slices not stacked: dimensions or bit-depth varies\n");
        warnings->bitDepthVaries = true;
        return false;
    }
    #ifndef myIgnoreStudyTime
    if (!isSameTime) { //beware, some vendors incorrectly store Image Time (0008,0033) as Study Time (0008,0030).
    	if (!warnings->dateTimeVaries)
    		printMessage("Slices not stacked: Study Date/Time (0008,0020 / 0008,0030) varies %12.12f ~= %12.12f\n", d1.dateTime, d2.dateTime);
    	warnings->dateTimeVaries = true;
    	return false;
    }
    #endif
    if ((opts->isForceStackSameSeries == 1) || ((opts->isForceStackSameSeries == 2) && (d1.isXRay) )) {
    	// "isForceStackSameSeries == 2" will automatically stack CT scans but not MR
    	//if ((d1.TE != d2.TE) || (d1.echoNum != d2.echoNum))
    	if ((!(isSameFloat(d1.TE, d2.TE))) || (d1.echoNum != d2.echoNum))
    		*isMultiEcho = true;
    	return true; //we will stack these images, even if they differ in the following attributes
    }
    if ((d1.isHasImaginary != d2.isHasImaginary) || (d1.isHasPhase != d2.isHasPhase) || ((d1.isHasReal != d2.isHasReal))) {
    	if (!warnings->phaseVaries)
        	printMessage("Slices not stacked: some are phase/real/imaginary maps, others are not. Use 'f 2D slices' option to force stacking\n");
    	warnings->phaseVaries = true;
    	return false;
    }
    //if ((d1.TE != d2.TE) || (d1.echoNum != d2.echoNum)) {
    if ((!(isSameFloat(d1.TE, d2.TE)) ) || (d1.echoNum != d2.echoNum)) {
        if ((!warnings->echoVaries) && (d1.isXRay)) //for CT/XRay we check DICOM tag 0018,1152 (XRayExposure)
        	printMessage("Slices not stacked: X-Ray Exposure varies (exposure %g, %g; number %d, %d). Use 'merge 2D slices' option to force stacking\n", d1.TE, d2.TE,d1.echoNum, d2.echoNum );
        if ((!warnings->echoVaries) && (!d1.isXRay)) //for MRI
        	printMessage("Slices not stacked: echo varies (TE %g, %g; echo %d, %d). Use 'merge 2D slices' option to force stacking\n", d1.TE, d2.TE,d1.echoNum, d2.echoNum );
        warnings->echoVaries = true;
        *isMultiEcho = true;
        return false;
    }
    if (d1.coilCrc != d2.coilCrc) {
        if (!warnings->coilVaries)
        	printMessage("Slices not stacked: coil varies\n");
        warnings->coilVaries = true;
        *isCoilVaries = true;
        return false;
    }
    if ((strlen(d1.protocolName) < 1) && (strlen(d2.protocolName) < 1)) {
    	if (!warnings->nameEmpty)
    	printWarning("Empty protocol name(s) (0018,1030)\n");
    	warnings->nameEmpty = true;
    } else if ((strcmp(d1.protocolName, d2.protocolName) != 0)) {
        if (!warnings->nameVaries)
        	printMessage("Slices not stacked: protocol name varies '%s' != '%s'\n", d1.protocolName, d2.protocolName);
        warnings->nameVaries = true;
        return false;
    }
    if ((!isSameFloatGE(d1.orient[1], d2.orient[1]) || !isSameFloatGE(d1.orient[2], d2.orient[2]) ||  !isSameFloatGE(d1.orient[3], d2.orient[3]) ||
    		!isSameFloatGE(d1.orient[4], d2.orient[4]) || !isSameFloatGE(d1.orient[5], d2.orient[5]) ||  !isSameFloatGE(d1.orient[6], d2.orient[6]) ) ) {
        if ((!warnings->orientVaries) && (!d1.isNonParallelSlices))
        	printMessage("Slices not stacked: orientation varies (vNav or localizer?) [%g %g %g %g %g %g] != [%g %g %g %g %g %g]\n",
               d1.orient[1], d1.orient[2], d1.orient[3],d1.orient[4], d1.orient[5], d1.orient[6],
               d2.orient[1], d2.orient[2], d2.orient[3],d2.orient[4], d2.orient[5], d2.orient[6]);
        warnings->orientVaries = true;
        *isNonParallelSlices = true;
        return false;
    }
    if (d1.acquNum != d2.acquNum) {
        if ((!warnings->acqNumVaries) && (opts->isVerbose)) //virtually always people want to stack these
        	printMessage("Slices stacked despite varying acquisition numbers (if this is not desired recompile with 'mySegmentByAcq')\n");
        warnings->acqNumVaries = true;
    }
    if ((!isForceStackSeries) && (d1.seriesUidCrc != d2.seriesUidCrc)) {
        if (!warnings->seriesUidVaries)
        	printMessage("Slices not stacked: series instance UID varies (duplicates all other properties)\n");
        warnings->seriesUidVaries = true;
        return false;
    }
    return true;
}// isSameSet()

void freeNameList(struct TSearchList nameList) {
    if (nameList.numItems > 0) {
        unsigned long n = nameList.numItems;
        if (n > nameList.maxItems) n = nameList.maxItems; //assigned if (nameList->numItems < nameList->maxItems)
        for (unsigned long i = 0; i < n; i++)
            free(nameList.str[i]);
    }
    free(nameList.str);
}

int singleDICOM(struct TDCMopts* opts, char *fname) {
    if (isDICOMfile(fname) == 0) {
        printError("Not a DICOM image : %s\n", fname);
        return 0;
    }
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc( sizeof(struct  TDICOMdata));
    struct TDTI4D dti4D;
    struct TSearchList nameList;
    nameList.maxItems = 1; // larger requires more memory, smaller more passes
    nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //reserve one pointer (32 or 64 bits) per potential file
    nameList.numItems = 0;
    nameList.str[nameList.numItems]  = (char *)malloc(strlen(fname)+1);
    strcpy(nameList.str[nameList.numItems],fname);
    nameList.numItems++;
    struct TDCMsort dcmSort[1];
    dcmList[0].converted2NII = 1;
    dcmList[0] = readDICOMv(nameList.str[0], opts->isVerbose, opts->compressFlag, &dti4D); //ignore compile warning - memory only freed on first of 2 passes
    fillTDCMsort(dcmSort[0], 0, dcmList[0]);
    int ret = saveDcm2Nii(1, dcmSort, dcmList, &nameList, *opts, &dti4D);
    freeNameList(nameList);
    return ret;
}// singleDICOM()

#ifdef myTextFileInputLists //https://github.com/rordenlab/dcm2niix/issues/288
int textDICOM(struct TDCMopts* opts, char *fname) {
	//check input file
    FILE *fp = fopen(fname, "r");
    if (fp == NULL)
#ifdef USING_R
        return EXIT_FAILURE;
#else
    	exit(EXIT_FAILURE);
#endif
	int nConvert = 0;
    char dcmname[2048];
    while (fgets(dcmname, sizeof(dcmname), fp)) {
		int sz = strlen(dcmname);
		if (sz > 0 && dcmname[sz-1] == '\n') dcmname[sz-1] = 0; //Unix LF
		if (sz > 1 && dcmname[sz-2] == '\r') dcmname[sz-2] = 0; //Windows CR/LF
		//if (isDICOMfile(dcmname) == 0) { //<- this will reject DICOM metadata not wrapped with a header
        if ((!is_fileexists(dcmname)) || (!is_fileNotDir(dcmname)) ) { //<-this will accept meta data
        	fclose(fp);
        	printError("Problem with file '%s'\n", dcmname);
        	return EXIT_FAILURE;
    	}
    	//printf("%s\n", dcmname);
		nConvert ++;
    }
    fclose(fp);
    if (nConvert < 1) {
    	printError("No DICOM files found '%s'\n", dcmname);
    	return EXIT_FAILURE;
    }
    printMessage("Found %d DICOM file(s)\n", nConvert);
    #ifndef USING_R
    fflush(stdout); //show immediately if run from MRIcroGL GUI
    #endif
    TDCMsort * dcmSort = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
	struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nConvert * sizeof(struct  TDICOMdata));
    struct TDTI4D dti4D;
    struct TSearchList nameList;
    nameList.maxItems = nConvert; // larger requires more memory, smaller more passes
    nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //reserve one pointer (32 or 64 bits) per potential file
    nameList.numItems = 0;
	nConvert = 0;
	fp = fopen(fname, "r");
    while (fgets(dcmname, sizeof(dcmname), fp)) {
		int sz = strlen(dcmname);
		if (sz > 0 && dcmname[sz-1] == '\n') dcmname[sz-1] = 0; //Unix LF
		if (sz > 1 && dcmname[sz-2] == '\r') dcmname[sz-2] = 0; //Windows CR/LF
		nameList.str[nameList.numItems]  = (char *)malloc(strlen(dcmname)+1);
    	strcpy(nameList.str[nameList.numItems],dcmname);
    	nameList.numItems++;
		dcmList[nConvert] = readDICOMv(nameList.str[nConvert], opts->isVerbose, opts->compressFlag, &dti4D); //ignore compile warning - memory only freed on first of 2 passes
		fillTDCMsort(dcmSort[nConvert], nConvert, dcmList[nConvert]);
		nConvert ++;
    }
    fclose(fp);
    qsort(dcmSort, nConvert, sizeof(struct TDCMsort), compareTDCMsort); //sort based on series and image numbers....
	int ret = saveDcm2Nii(nConvert, dcmSort, dcmList, &nameList, *opts, &dti4D);
    free(dcmSort);
    free(dcmList);
    freeNameList(nameList);
    return ret;
}//textDICOM()

#else //ifdef myTextFileInputLists
int textDICOM(struct TDCMopts* opts, char *fname) {
	printError("Unable to parse txt files: re-compile with 'myTextFileInputLists' (see issue 288)");
	return EXIT_FAILURE;
}
#endif

size_t fileBytes(const char * fname) {
    FILE *fp = fopen(fname, "rb");
	if (!fp)  return 0;
	fseek(fp, 0, SEEK_END);
	size_t fileLen = ftell(fp);
    fclose(fp);
    return fileLen;
} //fileBytes()

void searchDirForDICOM(char *path, struct TSearchList *nameList, int maxDepth, int depth, struct TDCMopts* opts ) {
    tinydir_dir dir;
    tinydir_open(&dir, path);
    while (dir.has_next) {
        tinydir_file file;
        file.is_dir = 0; //avoids compiler warning: this is set by tinydir_readfile
        tinydir_readfile(&dir, &file);
        //printMessage("%s\n", file.name);
        char filename[768] ="";
        strcat(filename, path);
        strcat(filename,kFileSep);
        strcat(filename, file.name);
        if ((file.is_dir) && (depth < maxDepth) && (file.name[0] != '.'))
            searchDirForDICOM(filename, nameList, maxDepth, depth+1, opts);
        else if (!file.is_reg) //ignore files "." and ".."
            ;
        else if ((strlen(file.name) < 1) || (file.name[0]=='.'))
        	; //printMessage("skipping hidden file %s\n", file.name);
        else if ((strlen(file.name) == 8) && (strcicmp(file.name, "DICOMDIR") == 0))
        	; //printMessage("skipping DICOMDIR\n");
        else if ((isDICOMfile(filename) > 0) || (isExt(filename, ".par")) ) {
            if (nameList->numItems < nameList->maxItems) {
                nameList->str[nameList->numItems]  = (char *)malloc(strlen(filename)+1);
                strcpy(nameList->str[nameList->numItems],filename);
            }
            nameList->numItems++;
            //printMessage("dcm %lu %s \n",nameList->numItems, filename);
#ifndef USING_R
        } else {
        	if (fileBytes(filename) > 2048)
            	convert_foreign (filename, *opts);
        	#ifdef MY_DEBUG
            	printMessage("Not a dicom:\t%s\n", filename);
        	#endif
#endif
        }
        tinydir_next(&dir);
    }
    tinydir_close(&dir);
}// searchDirForDICOM()

int removeDuplicates(int nConvert, struct TDCMsort dcmSort[]){
    //done AFTER sorting, so duplicates will be sequential
    if (nConvert < 2) return nConvert;
    int nDuplicates = 0;
    for (int i = 1; i < nConvert; i++) {
        if (compareTDCMsort(&dcmSort[i], &dcmSort[i-1]) == 0) {
            nDuplicates ++;
        } else {
            dcmSort[i-nDuplicates].img = dcmSort[i].img;
            dcmSort[i-nDuplicates].indx = dcmSort[i].indx;
            for(int j = 0; j < MAX_NUMBER_OF_DIMENSIONS; ++j)
              dcmSort[i - nDuplicates].dimensionIndexValues[j] = dcmSort[i].dimensionIndexValues[j];
        }
    }
    if (nDuplicates > 0)
        printMessage("%d images have identical time, series, acquisition and instance values. DUPLICATES REMOVED.\n", nDuplicates);
    return nConvert - nDuplicates;
}// removeDuplicates()

int removeDuplicatesVerbose(int nConvert, struct TDCMsort dcmSort[], struct TSearchList *nameList){
    //done AFTER sorting, so duplicates will be sequential
    if (nConvert < 2) return nConvert;
    int nDuplicates = 0;
    for (int i = 1; i < nConvert; i++) {
        if (compareTDCMsort(&dcmSort[i], &dcmSort[i-1]) == 0) {
            printMessage("\t%s\t=\t%s\n",nameList->str[dcmSort[i-1].indx],nameList->str[dcmSort[i].indx]);
            nDuplicates ++;
        } else {
            dcmSort[i-nDuplicates].img = dcmSort[i].img;
            dcmSort[i-nDuplicates].indx = dcmSort[i].indx;
            for(int j = 0; j < MAX_NUMBER_OF_DIMENSIONS; ++j)
              dcmSort[i - nDuplicates].dimensionIndexValues[j] = dcmSort[i].dimensionIndexValues[j];
        }
    }
    if (nDuplicates > 0)
        printMessage("%d images have identical time, series, acquisition and instance values. Duplicates removed.\n", nDuplicates);
    return nConvert - nDuplicates;
}// removeDuplicatesVerbose()

int convert_parRec(char * fnm, struct TDCMopts opts) {
    //sample dataset from Ed Gronenschild <ed.gronenschild@maastrichtuniversity.nl>
    struct TSearchList nameList;
    int ret = EXIT_FAILURE;
    nameList.numItems = 1;
    nameList.maxItems = 1;
    nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //we reserve one pointer (32 or 64 bits) per potential file
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nameList.numItems * sizeof(struct  TDICOMdata));
    nameList.str[0]  = (char *)malloc(strlen(fnm)+1);
    strcpy(nameList.str[0], fnm);
    //nameList.str[0]  = (char *)malloc(strlen(opts.indir)+1);
    //strcpy(nameList.str[0],opts.indir);
    TDTI4D dti4D;
    dcmList[0] = nii_readParRec(nameList.str[0], opts.isVerbose, &dti4D, false);
    struct TDCMsort dcmSort[1];
    dcmSort[0].indx = 0;
    if (dcmList[0].isValid)
    	ret = saveDcm2Nii(1, dcmSort, dcmList, &nameList, opts, &dti4D);
    free(dcmList);//if (nConvertTotal == 0)
    if (nameList.numItems < 1)
    	printMessage("No valid PAR/REC files were found\n");
    freeNameList(nameList);
    return ret;
}// convert_parRec()

int copyFile (char * src_path, char * dst_path) {
	#define BUFFSIZE 32768
	unsigned char buffer[BUFFSIZE];
    FILE *fin = fopen(src_path, "rb");
    if (fin == NULL) {
    	printError("Check file permissions: Unable to open input %s\n", src_path);
    	return EXIT_SUCCESS;
    }
    if (is_fileexists(dst_path)) {
    	if (true) {
    		printWarning("Naming conflict (duplicates?): '%s' '%s'\n", src_path, dst_path);
    		return EXIT_SUCCESS;
    	} else {
    		printError("File naming conflict. Existing file %s\n", dst_path);
    		return EXIT_FAILURE;
    	}
    }
	FILE *fou = fopen(dst_path, "wb");
    if (fou == NULL) {
        printError("Check file permission. Unable to open output %s\n", dst_path);
    	return EXIT_FAILURE;
    }
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUFFSIZE, fin)) != 0) {
        if(fwrite(buffer, 1, bytes, fou) != bytes) {
        	printError("Unable to write %zu bytes to output %s\n", bytes, dst_path);
            return EXIT_FAILURE;
        }
    }
    fclose(fin);
    fclose(fou);
    return EXIT_SUCCESS;
}

#ifdef USING_R

// This implementation differs enough from the mainline one to be separated
int searchDirRenameDICOM(char *path, int maxDepth, int depth, struct TDCMopts* opts ) {
    // The tinydir_open_sorted function reads the whole directory at once,
    // which is necessary in this context since we may be creating new
    // files in the same directory, which we don't want to further examine
    tinydir_dir dir;
    int count = 0;
    if (tinydir_open_sorted(&dir, path) != 0)
        return -1;

    for (size_t i=0; i<dir.n_files; i++) {
        // If this directory entry is a subdirectory, search it recursively
        tinydir_file &file = dir._files[i];
        const std::string sourcePath = std::string(path) + kFileSep + file.name;
        char *sourcePathPtr = const_cast<char*>(sourcePath.c_str());
        if ((file.is_dir) && (depth < maxDepth) && (file.name[0] != '.')) {
            const int subdirectoryCount = searchDirRenameDICOM(sourcePathPtr, maxDepth, depth+1, opts);
            if (subdirectoryCount < 0) {
                tinydir_close(&dir);
                return -1;
            }
            count += subdirectoryCount;
        } else if (file.is_reg && strlen(file.name) > 0 && file.name[0] != '.' && strcicmp(file.name,"DICOMDIR") != 0 && isDICOMfile(sourcePathPtr)) {
            TDICOMdata dcm = readDICOM(sourcePathPtr);
			if (dcm.imageNum > 0) {
				if ((opts->isIgnoreDerivedAnd2D) && ((dcm.isLocalizer) || (strcmp(dcm.sequenceName, "_tfl2d1")== 0) || (strcmp(dcm.sequenceName, "_fl3d1_ns")== 0) || (strcmp(dcm.sequenceName, "_fl2d1")== 0)) ) {
					printMessage("Ignoring localizer %s\n", sourcePathPtr);
                    opts->ignoredPaths.push_back(sourcePath);
				} else if ((opts->isIgnoreDerivedAnd2D && dcm.isDerived) ) {
					printMessage("Ignoring derived %s\n", sourcePathPtr);
                    opts->ignoredPaths.push_back(sourcePath);
				} else {
					// Create an initial file name
                    char outname[PATH_MAX] = {""};
					if (dcm.echoNum > 1)
                        dcm.isMultiEcho = true;
					nii_createFilename(dcm, outname, *opts);

                    // If the file name part of the target path has no extension, add ".dcm"
                    std::string targetPath(outname);
                    std::string targetStem, targetExtension;
                    const size_t periodLoc = targetPath.find_last_of('.');
                    if (periodLoc == targetPath.length() - 1) {
                        targetStem = targetPath.substr(0, targetPath.length() - 1);
                        targetExtension = ".dcm";
                    } else if (periodLoc == std::string::npos || periodLoc < targetPath.find_last_of("\\/")) {
                        targetStem = targetPath;
                        targetExtension = ".dcm";
                    } else {
                        targetStem = targetPath.substr(0, periodLoc);
                        targetExtension = targetPath.substr(periodLoc);
                    }

                    // Deduplicate the target path to avoid overwriting existing files
                    targetPath = targetStem + targetExtension;
                    GetRNGstate();
                    while (is_fileexists(targetPath.c_str())) {
                        std::ostringstream suffix;
                        unsigned suffixValue = static_cast<unsigned>(round(R::unif_rand() * (R_pow_di(2.0,24) - 1.0)));
                        suffix << std::hex << std::setfill('0') << std::setw(6) << suffixValue;
                        targetPath = targetStem + "_" + suffix.str() + targetExtension;
                    }
                    PutRNGstate();

                    // Copy the file, unless the source and target paths are the same
                    if (targetPath.compare(sourcePath) == 0) {
                        if (opts->isVerbose > 1)
                            printMessage("Skipping %s, which would be copied onto itself\n", sourcePathPtr);
                    } else if (copyFile(sourcePathPtr, const_cast<char*>(targetPath.c_str())) == EXIT_SUCCESS) {
                        opts->sourcePaths.push_back(sourcePath);
                        opts->targetPaths.push_back(targetPath);
                        count++;
                        if (opts->isVerbose > 0)
                            printMessage("Copying %s -> %s\n", sourcePathPtr, targetPath.c_str());
                    } else {
                        printWarning("Unable to copy to path %s\n", targetPath.c_str());
                    }
				}
			}
        }
    }
    return count;
}

#else

int searchDirRenameDICOM(char *path, int maxDepth, int depth, struct TDCMopts* opts ) {
    int retAll = 0;
    tinydir_dir dir;
    if (tinydir_open_sorted(&dir, path) != 0) {
		if (opts->isVerbose > 0)
			printMessage("Unable to open %s\n", path);
        return -1;
    }
    if (dir.n_files < 1) {
		if (opts->isVerbose > 0)
			printMessage("No files in %s\n", path);
        return 0;
    }
	if (opts->isVerbose > 0)
		printMessage("Found %lu items in %s\n", dir.n_files, path);
    for (size_t i=0; i<dir.n_files; i++) {
        // If this directory entry is a subdirectory, search it recursively
        tinydir_file &file = dir._files[i];
        char filename[768] ="";
        strcat(filename, path);
        strcat(filename,kFileSep);
        strcat(filename, file.name);
        if ((file.is_dir) && (depth < maxDepth) && (file.name[0] != '.')) {
        	int retSub = searchDirRenameDICOM(filename, maxDepth, depth+1, opts);
        	if (retSub < 0) return retSub;
        	retAll += retSub;
        } else if (!file.is_reg) //ignore files "." and ".."
            ;
        else if ((strlen(file.name) < 1) || (file.name[0]=='.'))
        	; //printMessage("skipping hidden file %s\n", file.name);
        else if ((strlen(file.name) == 8) && (strcicmp(file.name, "DICOMDIR") == 0))
        	; //printMessage("skipping DICOMDIR\n");
        else if (isDICOMfile(filename) > 0 ) {
            //printMessage("dcm %s \n", filename);
			struct TDICOMdata dcm = readDICOM(filename); //ignore compile warning - memory only freed on first of 2 passes
			//~ if ((dcm.isValid) &&((dcm.totalSlicesIn4DOrder != NULL) ||(dcm.patientPositionNumPhilips > 1) || (dcm.CSA.numDti > 1))) { //4D dataset: dti4D arrays require huge amounts of RAM - write this immediately
			if (dcm.imageNum > 0) { //use imageNum instead of isValid to convert non-images (kWaveformSq will have instance number but is not a valid image)
				if ((opts->isIgnoreDerivedAnd2D) && ((dcm.isLocalizer)  || (strcmp(dcm.sequenceName, "_tfl2d1")== 0) || (strcmp(dcm.sequenceName, "_fl3d1_ns")== 0) || (strcmp(dcm.sequenceName, "_fl2d1")== 0)) ) {
					printMessage("Ignoring localizer %s\n", filename);
				} else if ((opts->isIgnoreDerivedAnd2D && dcm.isDerived) ) {
					printMessage("Ignoring derived %s\n", filename);
				} else {
					char outname[PATH_MAX] = {""};
					if (dcm.echoNum > 1) dcm.isMultiEcho = true; //last resort: Siemens gives different echoes the same image number: avoid overwriting, e.g "-f %r.dcm" should generate "1.dcm", "1_e2.dcm" for multi-echo volumes
					nii_createFilename(dcm, outname, *opts);
					//if (isDcmExt) strcat (outname,".dcm");
					int ret = copyFile (filename, outname);
					if (ret != EXIT_SUCCESS) {
						printError("Unable to rename all DICOM images.\n");
						return -1;
					}
					retAll += 1;
					if (opts->isVerbose > 0)
						printMessage("Renaming %s -> %s\n", filename, outname);
				}
			}
        }
        tinydir_next(&dir);
    }
    tinydir_close(&dir);
    return retAll;
}// searchDirForDICOM()

#endif // USING_R

//Timing
#define myTimer

//"BubbleSort" method uses nested "for i = 0..nDCM; for j = i+1..nDCM"
// the alternative is to quick-sort based on seriesUID and only test for matches in buckets where seriesUID matches
// the advantage of the bubble sort method is that it has been used extensively
// the quick sort method should be faster when handling thousands of files.
// difference very small for typical datasets (~0.1s for 3200 DICOMs)
//#define myBubbleSort
#ifndef myBubbleSort
struct TCRCsort {
	uint64_t indx;
	uint32_t crc;
};

void fillTCRCsort(struct TCRCsort& tcrcref, const uint64_t indx, const uint32_t crc){
  tcrcref.indx = indx;
  tcrcref.crc = crc;
}

int compareTCRCsort(void const *item1, void const *item2) {
	//for quicksort http://blog.ablepear.com/2011/11/objective-c-tuesdays-sorting-arrays.html
	struct TCRCsort const *dcm1 = (const struct TCRCsort *)item1;
	struct TCRCsort const *dcm2 = (const struct TCRCsort *)item2;
	if (dcm1->crc < dcm2->crc)
		return -1;
	else if (dcm1->crc > dcm2->crc)
		return 1;
	return 0; //tie
}
#endif

#ifdef myTimer
int reportProgress(int progressPct, float frac) {
	int newProgressPct = round(100.0 * frac);
	const int kMinPct = 5; //e.g. if 10 then report 0.1, 0.2, 0.3...
	newProgressPct = (newProgressPct / kMinPct) * kMinPct; //if MinPct is 5 and we are 87 percent done report 85%
	if (newProgressPct == progressPct) return progressPct;
	if (newProgressPct != progressPct) //only report for change
		printProgress((float)newProgressPct/100.0);
	return newProgressPct;
}
#endif


int nii_loadDirCore(char *indir, struct TDCMopts* opts) {
    struct TSearchList nameList;
    #if defined(_WIN64) || defined(_WIN32) || defined(USING_R)
	nameList.maxItems = 24000; // larger requires more memory, smaller more passes
    #else //UNIX, not R
	nameList.maxItems = 96000; // larger requires more memory, smaller more passes
    #endif
    //progress variables
    const float kStage1Frac = 0.05; //e.g. finding files requires ~05pct
    const float kStage2Frac = 0.45; //e.g. reading headers and converting 4D files requires ~45pct
    const float kStage3Frac = 0.50; //e.g. converting 2D/3D files to 3D/4D  files requires ~50pct
    int progressPct = 0; //proportion correct, 0..100
    if (opts->isProgress)
    	progressPct = reportProgress(-1, 0.0); //report 0%
    #ifdef myTimer
    clock_t start = clock();
    #endif
	//1: find filenames of dicom files: up to two passes if we found more files than we allocated memory
    for (int i = 0; i < 2; i++ ) {
        nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //reserve one pointer (32 or 64 bits) per potential file
        nameList.numItems = 0;
        searchDirForDICOM(indir, &nameList, opts->dirSearchDepth, 0, opts);
        if (nameList.numItems <= nameList.maxItems)
            break;
        freeNameList(nameList);
        nameList.maxItems = nameList.numItems+1;
        //printMessage("Second pass required, found %ld images\n", nameList.numItems);
    }
    if (nameList.numItems < 1) {
        if (opts->dirSearchDepth > 0)
        	printError("Unable to find any DICOM images in %s (or subfolders %d deep)\n", indir, opts->dirSearchDepth);
        else //keep silent for dirSearchDepth = 0 - presumably searching multiple folders
        	{};
        free(nameList.str); //ignore compile warning - memory only freed on first of 2 passes
        return kEXIT_NO_VALID_FILES_FOUND;
    }
    size_t nDcm = nameList.numItems;
    printMessage( "Found %lu DICOM file(s)\n", nameList.numItems); //includes images and other non-image DICOMs
    #ifdef myTimer
    if (opts->isProgress > 1) printMessage ("Stage 1 (Count number of DICOMs) required %f seconds.\n",((float)(clock()-start))/CLOCKS_PER_SEC);
    start = clock();
	#endif
	if (opts->isProgress)
    	progressPct = reportProgress(progressPct, kStage1Frac); //proportion correct, 0..100
	// struct TDICOMdata dcmList [nameList.numItems]; //<- this exhausts the stack for large arrays
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nameList.numItems * sizeof(struct  TDICOMdata));
    struct TDTI4D dti4D;
    int nConvertTotal = 0;
    bool compressionWarning = false;
    bool convertError = false;
    bool isDcmExt = isExt(opts->filename, ".dcm"); // "%r.dcm" with multi-echo should generate "1.dcm", "1e2.dcm"
	if (isDcmExt) opts->filename[strlen(opts->filename) - 4] = 0; // "%s_%r.dcm" -> "%s_%r"
	//consider OpenMP
	// g++-9 -I.  main_console.cpp nii_foreign.cpp nii_dicom.cpp jpg_0XC3.cpp ujpeg.cpp nifti1_io_core.cpp nii_ortho.cpp nii_dicom_batch.cpp  -o dcm2niix -DmyDisableOpenJPEG -fopenmp 
    for (int i = 0; i < (int)nDcm; i++ ) {
    	if ((isExt(nameList.str[i], ".par")) && (isDICOMfile(nameList.str[i]) < 1)) {
			//strcpy(opts->indir, nameList.str[i]); //set to original file name, not path
            dcmList[i].converted2NII = 1;
            int ret = convert_parRec(nameList.str[i] , *opts);
            if (ret == EXIT_SUCCESS)
            	nConvertTotal++;
            else
            	convertError = true;
            continue;
    	}
        dcmList[i] = readDICOMv(nameList.str[i], opts->isVerbose, opts->compressFlag, &dti4D); //ignore compile warning - memory only freed on first of 2 passes
        if ((dcmList[i].isValid) && ((dti4D.sliceOrder[0] >= 0) || (dcmList[i].CSA.numDti > 1))) { //4D dataset: dti4D arrays require huge amounts of RAM - write this immediately
			struct TDCMsort dcmSort[1];
			fillTDCMsort(dcmSort[0], i, dcmList[i]);
            dcmList[i].converted2NII = 1;
            int ret = saveDcm2Nii(1, dcmSort, dcmList, &nameList, *opts, &dti4D);
            if (ret == EXIT_SUCCESS)
            	nConvertTotal++;
            else
            	convertError = true;
        }
    	if ((dcmList[i].compressionScheme != kCompressNone) && (!compressionWarning) && (opts->compressFlag != kCompressNone)) {
    		compressionWarning = true; //generate once per conversion rather than once per image
        	printMessage("Image Decompression is new: please validate conversions\n");
    	}
    	if (opts->isProgress)
    		progressPct = reportProgress(progressPct, kStage1Frac+ (kStage2Frac *(float)i/(float)nDcm)); //proportion correct, 0..100
    }
    #ifdef myTimer
    if (opts->isProgress > 1) printMessage ("Stage 2 (Read DICOM headers, Convert 4D) required %f seconds.\n",((float)(clock()-start))/CLOCKS_PER_SEC);
	start = clock();
	#endif
	if (opts->isRenameNotConvert) {
    	return EXIT_SUCCESS;
    }
#ifdef USING_R
    if (opts->isScanOnly) {
        TWarnings warnings = setWarnings();
        // Create the first series from the first DICOM file
        TDicomSeries firstSeries;
        char firstSeriesName[2048] = "";
        nii_createFilename(dcmList[0], firstSeriesName, *opts);
        firstSeries.name = firstSeriesName;
        firstSeries.representativeData = dcmList[0];
        firstSeries.files.push_back(nameList.str[0]);
        opts->series.push_back(firstSeries);
        // Iterate over the remaining files
        for (size_t i = 1; i < nDcm; i++) {
            bool matched = false;
            // If the file matches an existing series, add it to the corresponding file list
            for (int j = 0; j < opts->series.size(); j++) {
                bool isMultiEchoUnused, isNonParallelSlices, isCoilVaries;
                if (isSameSet(opts->series[j].representativeData, dcmList[i], opts, &warnings, &isMultiEchoUnused, &isNonParallelSlices, &isCoilVaries)) {
                    opts->series[j].files.push_back(nameList.str[i]);
                    matched = true;
                    break;
                }
            }
            // If not, create a new series object
            if (!matched) {
                TDicomSeries nextSeries;
                char nextSeriesName[2048] = "";
                nii_createFilename(dcmList[i], nextSeriesName, *opts);
                nextSeries.name = nextSeriesName;
                nextSeries.representativeData = dcmList[i];
                nextSeries.files.push_back(nameList.str[i]);
                opts->series.push_back(nextSeries);
            }
        }
        // To avoid a spurious warning below
        nConvertTotal = nDcm;
    } else {
#endif
    #ifdef myBubbleSort
    //3: stack DICOMs with the same Series
    struct TWarnings warnings = setWarnings();
    for (int i = 0; i < (int)nDcm; i++ ) {
		if ((dcmList[i].converted2NII == 0) && (dcmList[i].isValid)) {
			int nConvert = 0;
			bool isMultiEcho = false;
			bool isNonParallelSlices = false;
			bool isCoilVaries = false;
			for (int j = i; j < (int)nDcm; j++)
				if (isSameSet(dcmList[i], dcmList[j], opts, &warnings, &isMultiEcho, &isNonParallelSlices, &isCoilVaries) )
					nConvert++;
			if (nConvert < 1) nConvert = 1; //prevents compiler warning for next line: never executed since j=i always causes nConvert ++
			TDCMsort * dcmSort = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
			nConvert = 0;
			for (int j = i; j < (int)nDcm; j++) {
				isMultiEcho = false;
				isNonParallelSlices = false;
				isCoilVaries = false;
				if (isSameSet(dcmList[i], dcmList[j], opts, &warnings, &isMultiEcho, &isNonParallelSlices, &isCoilVaries)) {
                    dcmList[j].converted2NII = 1; //do not reprocess repeats
                    fillTDCMsort(dcmSort[nConvert], j, dcmList[j]);
					nConvert++;
				} else {
					if (isNonParallelSlices) {
						dcmList[i].isNonParallelSlices = true;
						dcmList[j].isNonParallelSlices = true;
					}
					if (isMultiEcho) {
						dcmList[i].isMultiEcho = true;
						dcmList[j].isMultiEcho = true;
					}
					if (isCoilVaries) {
						dcmList[i].isCoilVaries = true;
						dcmList[j].isCoilVaries = true;
					}
				} //unable to stack images: mark files that may need file name dis-ambiguation
			}
			qsort(dcmSort, nConvert, sizeof(struct TDCMsort), compareTDCMsort); //sort based on series and image numbers....
			//dcmList[dcmSort[0].indx].isMultiEcho = isMultiEcho;
			if (opts->isVerbose)
				nConvert = removeDuplicatesVerbose(nConvert, dcmSort, &nameList);
			else
				//nConvert = removeDuplicatesVerbose(nConvert, dcmSort, &nameList);
				nConvert = removeDuplicates(nConvert, dcmSort);
			int ret = saveDcm2Nii(nConvert, dcmSort, dcmList, &nameList, *opts, &dti4D);
			if (ret == EXIT_SUCCESS)
            	nConvertTotal += nConvert;
            else
            	convertError = true;
			free(dcmSort);
		}//convert all images of this series
    }
    #else //avoid bubble sort - dont check all images for match, only those with identical series instance UID
    //3: stack DICOMs with the same Series
    struct TWarnings warnings = setWarnings();
    //sort by series instance UID ... avoids bubble-sort penalty
    TCRCsort * crcSort = (TCRCsort *)malloc(nDcm * sizeof(TCRCsort));
    for (int i = 0; i < (int)nDcm; i++ )
    	fillTCRCsort(crcSort[i], i, dcmList[i].seriesUidCrc);
    qsort(crcSort, nDcm, sizeof(struct TCRCsort), compareTCRCsort); //sort based on series and image numbers....
    int * convertIdxs = (int *)malloc(sizeof(int) * (nDcm));
    for (int i = 0; i < (int)nDcm; i++ ) {
    	int ii = crcSort[i].indx;
    	if (dcmList[ii].converted2NII) continue;
    	if (!dcmList[ii].isValid) continue;
		int nConvert = 0;
		bool isMultiEcho = false;
		bool isNonParallelSlices = false;
		bool isCoilVaries = false;
		for (int j = i; j < (int)nDcm; j++) {
			int ji = crcSort[j].indx;
			if (dcmList[ii].seriesUidCrc != dcmList[ji].seriesUidCrc) break; //seriesUID no longer matches no need to examine any subsequent images
			isMultiEcho = false;
			isNonParallelSlices = false;
			isCoilVaries = false;
			if (isSameSet(dcmList[ii], dcmList[ji], opts, &warnings, &isMultiEcho, &isNonParallelSlices, &isCoilVaries)) {
				dcmList[ji].converted2NII = 1; //do not reprocess repeats
				convertIdxs[nConvert] = ji;
				nConvert++;
			} else {
				if (isNonParallelSlices) {
					dcmList[ii].isNonParallelSlices = true;
					dcmList[ji].isNonParallelSlices = true;
				}
				if (isMultiEcho) {
					dcmList[ii].isMultiEcho = true;
					dcmList[ji].isMultiEcho = true;
				}
				if (isCoilVaries) {
					dcmList[ii].isCoilVaries = true;
					dcmList[ji].isCoilVaries = true;
				}
			} //unable to stack images: mark files that may need file name dis-ambiguation
		} //for all images with same seriesUID as first one
		TDCMsort * dcmSort = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
		for (int j = 0; j < nConvert; j++)
			fillTDCMsort(dcmSort[j], convertIdxs[j], dcmList[convertIdxs[j]]);
		qsort(dcmSort, nConvert, sizeof(struct TDCMsort), compareTDCMsort); //sort based on series and image numbers....
		if (opts->isVerbose)
			nConvert = removeDuplicatesVerbose(nConvert, dcmSort, &nameList);
		else
			nConvert = removeDuplicates(nConvert, dcmSort);
		int ret = saveDcm2Nii(nConvert, dcmSort, dcmList, &nameList, *opts, &dti4D);
		if (ret == EXIT_SUCCESS)
        	nConvertTotal += nConvert;
        else
        	convertError = true;
		free(dcmSort);
		if (opts->isProgress)
			progressPct = reportProgress(progressPct, kStage1Frac+kStage2Frac+ (kStage3Frac *(float)nConvertTotal/(float)nDcm)); //proportion correct, 0..100
    }
    free(convertIdxs);
	free(crcSort);
    #endif
#ifdef USING_R
    }
#endif
	#ifdef myTimer
    if (opts->isProgress > 1)
		printMessage ("Stage 3 (Convert 2D and 3D images) required %f seconds.\n",((float)(clock()-start))/CLOCKS_PER_SEC);
	#endif
    if (opts->isProgress) progressPct = reportProgress(progressPct, 1); //proportion correct, 0..100
    free(dcmList);
    freeNameList(nameList);
    if (convertError) {
    	if (nConvertTotal == 0)
    		return EXIT_FAILURE; //nothing converted
    	printError("Converted %d of %lu files\n", nConvertTotal, nDcm);	
    	return kEXIT_SOME_OK_SOME_BAD; //partial failure  
    }
    if (nConvertTotal == 0) {
        printMessage("No valid DICOM images were found\n"); //we may have found valid DICOM files but they are not DICOM images
        return kEXIT_NO_VALID_FILES_FOUND;
    }
    return EXIT_SUCCESS;
} //nii_loadDirCore()

int nii_loadDirOneDirAtATime(char *path, struct TDCMopts* opts, int maxDepth, int depth) {
    //return kEXIT_NO_VALID_FILES_FOUND if no files in ANY sub folders
    //return EXIT_FAILURE if ANY failure
    //return EXIT_SUCCESS if no failures and at least one image converted
    int ret = nii_loadDirCore(path, opts);
    if (ret == EXIT_FAILURE) return ret;
    tinydir_dir dir;
    tinydir_open(&dir, path);
    while (dir.has_next) {
        tinydir_file file;
        file.is_dir = 0; //avoids compiler warning: this is set by tinydir_readfile
        tinydir_readfile(&dir, &file);
        char filename[768] ="";
        strcat(filename, path);
        strcat(filename,kFileSep);
        strcat(filename, file.name);
        if ((file.is_dir) && (depth < maxDepth) && (file.name[0] != '.')) {
        	int retSub = nii_loadDirOneDirAtATime(filename, opts, maxDepth, depth+1);
        	if (retSub == EXIT_FAILURE) return retSub;
        	if (retSub == EXIT_SUCCESS) ret = retSub;
        }
        tinydir_next(&dir);
    }
    tinydir_close(&dir);
    return ret;
}

int nii_loadDir(struct TDCMopts* opts) {
    //Identifies all the DICOM files in a folder and its subfolders
    if (strlen(opts->indir) < 1) {
        printMessage("No input\n");
        return EXIT_FAILURE;
    }
    char indir[512];
    strcpy(indir,opts->indir);
    bool isFile = is_fileNotDir(indir);
    if (isFile) //if user passes ~/dicom/mr1.dcm we will look at all files in ~/dicom
        dropFilenameFromPath(opts->indir);
    dropTrailingFileSep(opts->indir);
    if (!is_dir(opts->indir,true)) {
		printError("Input folder invalid: %s\n",opts->indir);
		return kEXIT_INPUT_FOLDER_INVALID;    		
    }
    
#ifdef USING_R
    // Full file paths are only used by R/divest when reorganising DICOM files
    if (opts->isRenameNotConvert) {
#endif
    if (strlen(opts->outdir) < 1) {
        strcpy(opts->outdir,opts->indir);
    } else
    	dropTrailingFileSep(opts->outdir);
    if (!is_dir(opts->outdir,true)) {
		#ifdef myUseInDirIfOutDirUnavailable
		printWarning("Output folder invalid %s will try %s\n",opts->outdir,opts->indir);
		strcpy(opts->outdir,opts->indir);
		#else
		printError("Output folder invalid: %s\n",opts->outdir);
		return kEXIT_OUTPUT_FOLDER_INVALID;
		#endif
    }
    //check file permissions
    if ((opts->isCreateBIDS != false) || (opts->isOnlyBIDS != true)) { //output files expected: either BIDS or images
    	int w =access(opts->outdir,W_OK);
    	if (w != 0) {
    		#ifdef USE_CWD_IF_OUTDIR_NO_WRITE
    		char outdir[512];
    		strcpy(outdir,opts->outdir);
			strcpy(opts->outdir,opts->indir);
			w =access(opts->outdir,W_OK);
			if (w != 0) {
				printError("Unable to write to output folder: %s\n",outdir);
				return kEXIT_OUTPUT_FOLDER_READ_ONLY;				
			} else
				printWarning("Writing to working directory, unable to write to output folder: %s\n",outdir);
			#else
    		printError("Unable to write to output folder: %s\n",opts->outdir);
			return kEXIT_OUTPUT_FOLDER_READ_ONLY;
    		#endif
		}
	}	
#ifdef USING_R
    }
#endif
    getFileNameX(opts->indirParent, opts->indir, 512);
#ifndef USING_R
    if (isFile && ( (isExt(indir, ".v"))) )
		return convert_foreign (indir, *opts);
#endif
    if (isFile && ( (isExt(indir, ".par")) || (isExt(indir, ".rec"))) ) {
        char pname[512], rname[512];
        strcpy(pname,indir);
        strcpy(rname,indir);
        changeExt (pname, "PAR");
        changeExt (rname, "REC");
        #ifndef _MSC_VER //Linux is case sensitive, #include <unistd.h>
   		if( access( rname, F_OK ) != 0 ) changeExt (rname, "rec");
   		if( access( pname, F_OK ) != 0 ) changeExt (pname, "par");
		#endif
        if (is_fileNotDir(rname)  &&  is_fileNotDir(pname) ) {
            //strcpy(opts->indir, pname); //set to original file name, not path
            return convert_parRec(pname, *opts);
        };
    }
    if (isFile && (opts->isOnlySingleFile) && isExt(indir, ".txt") )
    	return textDICOM(opts, indir);
	if (opts->isRenameNotConvert) {
		int nConvert = searchDirRenameDICOM(opts->indir, opts->dirSearchDepth, 0, opts);
		if (nConvert < 0) return kEXIT_RENAME_ERROR;
#ifdef USING_R
		printMessage("Renamed %d DICOMs\n", nConvert);
#else
		printMessage("Converted %d DICOMs\n", nConvert);
#endif
		return EXIT_SUCCESS;
	}
    if ((isFile) && (opts->isOnlySingleFile))
    	return singleDICOM(opts, indir);
    if (opts->isOneDirAtATime) {
		int maxDepth = opts->dirSearchDepth;
		opts->dirSearchDepth = 0;
        strcpy(indir,opts->indir);
		return nii_loadDirOneDirAtATime(indir, opts, maxDepth, 0);
	} else
		return nii_loadDirCore(opts->indir, opts);

}// nii_loadDir()

/* cleaner than findPigz - perhaps validate someday
 void findExe(char name[512], const char * argv[]) {
    if (is_exe(name)) return; //name exists as provided
    char basename[PATH_MAX];
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

#if defined(_WIN64) || defined(_WIN32) || defined(USING_R)
#else //UNIX, not R

int findpathof(char *pth, const char *exe) {
//Find executable by searching the PATH environment variable.
// http://www.linuxquestions.org/questions/programming-9/get-full-path-of-a-command-in-c-117965/
	char *searchpath;
	char *beg, *end;
	int stop, found;
	size_t len;
	if (strchr(exe, '/') != NULL) {
		if (realpath(exe, pth) == NULL) return 0;
		return  is_exe(pth);
	}
	searchpath = getenv("PATH");
	if (searchpath == NULL) return 0;
	if (strlen(searchpath) <= 0) return 0;
	beg = searchpath;
	stop = 0; found = 0;
	do {
	end = strchr(beg, ':');
	if (end == NULL) {
		len = strlen(beg);
		if (len == 0) return 0;
		//gcc 8.1 warning: specified bound depends on the length of the source argument
		//https://developers.redhat.com/blog/2018/05/24/detecting-string-truncation-with-gcc-8/
		//strncpy(pth, beg, len);
		strcpy(pth,beg);
		stop = 1;
	} else {
	   strncpy(pth, beg, end - beg);
	   pth[end - beg] = '\0';
	   len = end - beg;
	}
	//gcc8.1 warning: specified bound depends on the length of the source argument
	//if (pth[len - 1] != '/') strncat(pth, "/", 1);
	if (pth[len - 1] != '/') strcat(pth, "/");
	strncat(pth, exe, PATH_MAX - len);
	found = is_exe(pth);
	if (!stop) beg = end + 1;
	} while (!stop && !found);
	if (!found) strcpy(pth,"");
	return found;
}
#endif

#ifndef USING_R
void readFindPigz (struct TDCMopts *opts, const char * argv[]) {
	#if defined(_WIN64) || defined(_WIN32)
    strcpy(opts->pigzname,"pigz.exe");
    if (!is_exe(opts->pigzname)) {
      #if defined(__APPLE__)
        #ifdef myDisableZLib
        printMessage("Compression requires %s in the same folder as the executable http://macappstore.org/pigz/\n",opts->pigzname);
		#else //myUseZLib
		if (opts->isVerbose > 0)
 			printMessage("Compression will be faster with %s in the same folder as the executable http://macappstore.org/pigz/\n",opts->pigzname);
		#endif
        strcpy(opts->pigzname,"");
      #else
        #ifdef myDisableZLib
        printMessage("Compression requires %s in the same folder as the executable\n",opts->pigzname);
		#else //myUseZLib
		if (opts->isVerbose > 0)
 			printMessage("Compression will be faster with %s in the same folder as the executable\n",opts->pigzname);
		#endif
        strcpy(opts->pigzname,"");
       #endif
    } else
    	strcpy(opts->pigzname,".\\pigz"); //drop
    #else
    char str[PATH_MAX];
    //possible pigz names
    const char * nams[] = {
    "pigz",
    "pigz_mricron",
    "pigz_afni",
	};
	#define n_nam (sizeof (nams) / sizeof (const char *))
	for (int n = 0; n < (int)n_nam; n++) {
		if (findpathof(str, nams[n])) {
			strcpy(opts->pigzname,str);
			//printMessage("Found pigz: %s\n", str);
			return;
		 }
    }
	//possible pigz paths
    const char * pths[] = {
    "/usr/local/bin/",
    "/usr/bin/",
	};
	#define n_pth (sizeof (pths) / sizeof (const char *))
    char exepth[PATH_MAX];
    strcpy(exepth,argv[0]);
    dropFilenameFromPath(exepth);//, opts.pigzname);
    char appendChar[2] = {"a"};
    appendChar[0] = kPathSeparator;
    if (exepth[strlen(exepth)-1] != kPathSeparator) strcat (exepth,appendChar);
	//see if pigz in any path
    for (int n = 0; n < (int)n_nam; n++) {
        //printf ("%d: %s\n", i, nams[n]);
    	for (int p = 0; p < (int)n_pth; p++) {
			strcpy(str, pths[p]);
			strcat(str, nams[n]);
			if (is_exe(str))
				goto pigzFound;
    	} //p
    	//check exepth
    	strcpy(str, exepth);
		strcat(str, nams[n]);
		if (is_exe(str))
			goto pigzFound;
    } //n
    //Failure:
    #if defined(__APPLE__)
	  #ifdef myDisableZLib
    	printMessage("Compression requires 'pigz' to be installed http://macappstore.org/pigz/\n");
      #else //myUseZLib
      	if (opts->isVerbose > 0)
    		printMessage("Compression will be faster with 'pigz' installed http://macappstore.org/pigz/\n");
      #endif
	#else //if APPLE else ...
	  #ifdef myDisableZLib
    	printMessage("Compression requires 'pigz' to be installed\n");
      #else //myUseZLib
      	if (opts->isVerbose > 0)
    		printMessage("Compression will be faster with 'pigz' installed\n");
      #endif
    #endif
	return;
  	pigzFound: //Success
  	strcpy(opts->pigzname,str);
	//printMessage("Found pigz: %s\n", str);
    #endif
} //readFindPigz()
#endif

void setDefaultOpts (struct TDCMopts *opts, const char * argv[]) { //either "setDefaultOpts(opts,NULL)" or "setDefaultOpts(opts,argv)" where argv[0] is path to search
    strcpy(opts->pigzname,"");
#ifndef USING_R
    readFindPigz(opts, argv);
#endif
    #ifdef myEnableJasper
    opts->compressFlag = kCompressYes; //JASPER for JPEG2000
	#else
		#ifdef myDisableOpenJPEG
		opts->compressFlag = kCompressNone; //no decompressor
		#else
		opts->compressFlag = kCompressYes; //OPENJPEG for JPEG2000
		#endif
	#endif
    //printMessage("%d %s\n",opts->compressFlag, opts->compressname);
    strcpy(opts->indir,"");
    strcpy(opts->outdir,"");
    strcpy(opts->imageComments,"");
    opts->isOnlySingleFile = false; //convert all files in a directory, not just a single file
    opts->isOneDirAtATime = false;
    opts->isRenameNotConvert = false;
    opts->isForceStackSameSeries = 2; //automatic: stack CTs, do not stack MRI
    opts->isForceStackDCE = true;
    opts->isIgnoreDerivedAnd2D = false;
    opts->isPhilipsFloatNotDisplayScaling = true;
    opts->isCrop = false;
    opts->isRotate3DAcq = true;
    opts->isGz = false;
    opts->isSaveNativeEndian = true;
    opts->isSaveNRRD = false;
    opts->isPipedGz = false; //e.g. pipe data directly to pigz instead of saving uncompressed to disk
    opts->isSave3D = false;
    opts->dirSearchDepth = 5;
    opts->isProgress = 0;
    opts->nameConflictBehavior = kNAME_CONFLICT_ADD_SUFFIX;
    #ifdef myDisableZLib
    	opts->gzLevel = 6;
    #else
    	opts->gzLevel = MZ_DEFAULT_LEVEL; //-1;
    #endif
    opts->isMaximize16BitRange = false; //e.g. if INT16 image has range 0..500 scale to be 0..50000 with hdr.scl_slope =  hdr.scl_slope * 0.01
    opts->isFlipY = true; //false: images in raw DICOM orientation, true: image rows flipped to cartesian coordinates
    opts->isRGBplanar = false; //false for NIfTI (RGBRGB...), true for Analyze (RRR..RGGG..GBBB..B)
    opts->isCreateBIDS =  true;
    opts->isOnlyBIDS = false;
    opts->isSortDTIbyBVal = false;
    #ifdef myNoAnonymizeBIDS
    opts->isAnonymizeBIDS = false;
    #else
    opts->isAnonymizeBIDS = true;
    #endif
    opts->isCreateText = false;
#ifdef myDebug
        opts->isVerbose =   true;
#else
        opts->isVerbose = false;
#endif
    opts->isTiltCorrect = true;
    opts->numSeries = 0;
    memset(opts->seriesNumber, 0, sizeof(opts->seriesNumber));
    strcpy(opts->filename,"%f_%p_%t_%s");
} // setDefaultOpts()

#if defined(_WIN64) || defined(_WIN32)
//windows has unusual file permissions for many users - lets save preferences to the registry
void saveIniFile (struct TDCMopts opts) {
	HKEY hKey;
	if(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\dcm2nii",0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
		RegCloseKey(hKey);
		return;
	}
	printMessage("Saving defaults to registry\n");
	DWORD dwValue    = opts.isGz;
	RegSetValueExA(hKey, "isGZ", 0, REG_DWORD, reinterpret_cast<BYTE *>(&dwValue), sizeof(dwValue));
	dwValue    = opts.isMaximize16BitRange;
	RegSetValueExA(hKey, "isMaximize16BitRange", 0, REG_DWORD, reinterpret_cast<BYTE *>(&dwValue), sizeof(dwValue));
	RegSetValueExA(hKey,"filename",0, REG_SZ,(LPBYTE)opts.filename, strlen(opts.filename)+1);
	RegCloseKey(hKey);
} //saveIniFile()

void readIniFile (struct TDCMopts *opts, const char * argv[]) {
    setDefaultOpts(opts, argv);
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
    if(RegQueryValueExA(hKey,"isMaximize16BitRange", 0, (LPDWORD )&dwDataType, reinterpret_cast<BYTE *>(&dwValue), &vSize) == ERROR_SUCCESS)
    	opts->isMaximize16BitRange = dwValue;
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
	setDefaultOpts(opts, argv);
    sprintf(opts->optsname, "%s%s", getenv("HOME"), STATUSFILENAME);
    FILE *fp = fopen(opts->optsname, "r");
    if (fp == NULL) return;
    char Setting[20],Value[255];
    //while ( fscanf(fp, "%[^=]=%s\n", Setting, Value) == 2 ) {
    //while ( fscanf(fp, "%[^=]=%s\n", Setting, Value) == 2 ) {
    while ( fscanf(fp, "%[^=]=%[^\n]\n", Setting, Value) == 2 ) {
        //printMessage(">%s<->'%s'\n",Setting,Value);
        if ( strcmp(Setting,"isGZ") == 0 )
            opts->isGz = atoi(Value);
        if ( strcmp(Setting,"isMaximize16BitRange") == 0 )
            opts->isMaximize16BitRange = atoi(Value);
        else if ( strcmp(Setting,"isBIDS") == 0 )
            opts->isCreateBIDS = atoi(Value);
        else if ( strcmp(Setting,"filename") == 0 )
            strcpy(opts->filename,Value);
    }
    fclose(fp);
}// readIniFile()

void saveIniFile (struct TDCMopts opts) {
    FILE *fp = fopen(opts.optsname, "w");
    //printMessage("%s\n",localfilename);
    if (fp == NULL) return;
    printMessage("Saving defaults file %s\n", opts.optsname);
    fprintf(fp, "isGZ=%d\n", opts.isGz);
    fprintf(fp, "isMaximize16BitRange=%d\n", opts.isMaximize16BitRange);
    fprintf(fp, "isBIDS=%d\n", opts.isCreateBIDS);
    fprintf(fp, "filename=%s\n", opts.filename);
    fclose(fp);
} //saveIniFile()

#endif
