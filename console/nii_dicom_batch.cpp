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
#ifndef HAVE_R
#include "nifti1.h"
#endif
#include "nii_dicom_batch.h"
#include "nii_foreign.h"
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
	const char kPathSeparator ='\\';
	const char kFileSep[2] = "\\";
#else
	const char kPathSeparator ='/';
	const char kFileSep[2] = "/";
#endif

#ifdef HAVE_R
#include "ImageList.h"

#undef isnan
#define isnan ISNAN
#endif

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
    	char cwd[1024];
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
    printMessage("Saving %d DTI gradients. GE Reorienting %s : please validate. isCol=%d sliceDir=%d flp=%d %d %d\n", d->CSA.numDti, d->protocolName, col, sliceDir, flp.v[0], flp.v[1],flp.v[2]);
	if (!col)
		printMessage(" reorienting for ROW phase-encoding untested.\n");
    for (int i = 0; i < d->CSA.numDti; i++) {
        float vLen = sqrt( (vx[i].V[1]*vx[i].V[1])
                          + (vx[i].V[2]*vx[i].V[2])
                          + (vx[i].V[3]*vx[i].V[3]));
        if ((vx[i].V[0] <= FLT_EPSILON)|| (vLen <= FLT_EPSILON) ) { //bvalue=0
            for (int v= 1; v < 4; v++)
                vx[i].V[v] = 0.0f;
            continue; //do not normalize or reorient 0 vectors
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
        printMessage("Siemens/Philips DTI directions require head first supine acquisition\n");
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
                printWarning("Volume %d appears to be an ADC map (non-zero b-value with zero vector length)\n", i);
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
    if (abs(sliceDir) == kSliceOrientMosaicNegativeDeterminant) {
       printWarning("Saving %d DTI gradients. Validate vectors (matrix had a negative determinant).\n", d->CSA.numDti); //perhaps Siemens sagittal
    } else if ( d->sliceOrient == kSliceOrientTra) {
        printMessage("Saving %d DTI gradients. Validate vectors.\n", d->CSA.numDti);
    } else if ( d->sliceOrient == kSliceOrientUnknown) {
    	printWarning("Saving %d DTI gradients. Validate vectors (image slice orientation not reported, e.g. 2001,100B).\n", d->CSA.numDti);
    } else {
        printWarning("Saving %d DTI gradients. Validate vectors (images are not axial slices).\n", d->CSA.numDti);
    }
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

void nii_SaveText(char pathoutname[], struct TDICOMdata d, struct TDCMopts opts, struct nifti_1_header *h, char * dcmname) {
	if (!opts.isCreateText) return;
	char txtname[2048] = {""};
	strcpy (txtname,pathoutname);
    strcat (txtname,".txt");
    //printMessage("Saving text %s\n",txtname);
    FILE *fp = fopen(txtname, "w");
    fprintf(fp, "%s\tField Strength:\t%g\tProtocolName:\t%s\tScanningSequence00180020:\t%s\tTE:\t%g\tTR:\t%g\tSeriesNum:\t%ld\tAcquNum:\t%d\tImageNum:\t%d\tImageComments:\t%s\tDateTime:\t%f\tName:\t%s\tConvVers:\t%s\tDoB:\t%s\tGender:\t%c\tAge:\t%s\tDimXYZT:\t%d\t%d\t%d\t%d\tCoil:\t%d\tEchoNum:\t%d\tOrient(6)\t%g\t%g\t%g\t%g\t%g\t%g\tbitsAllocated\t%d\tInputName\t%s\n",
      pathoutname, d.fieldStrength, d.protocolName, d.scanningSequence, d.TE, d.TR, d.seriesNum, d.acquNum, d.imageNum, d.imageComments,
      d.dateTime, d.patientName, kDCMvers, d.patientBirthDate, d.patientSex, d.patientAge, h->dim[1], h->dim[2], h->dim[3], h->dim[4],
            d.coilNum,d.echoNum, d.orient[1], d.orient[2], d.orient[3], d.orient[4], d.orient[5], d.orient[6],
            d.bitsAllocated, dcmname);
    fclose(fp);
}// nii_SaveText()

#define myReadAsciiCsa

#ifdef myReadAsciiCsa
//read from the ASCII portion of the Siemens CSA series header
//  this is not recommended: poorly documented
//  it is better to stick to the binary portion of the Siemens CSA image header

#if defined(_WIN64) || defined(_WIN32)
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

/*int readKeyX(const char * key,  char * buffer, int remLength) { //look for text key in binary data stream, return subsequent integer value
	int ret = 0;
	char *keyPos = (char *)memmem(buffer, remLength, key, strlen(key));
	printWarning("<><><>\n");
	if (!keyPos) return 0;
	printWarning("<><> %d\n", strlen(keyPos));
	int i = (int)strlen(key);
	int numDigits = 0;
	while( ( i< remLength) && (numDigits >= 0) ) {
		printMessage("%c", keyPos[i]);
		if( keyPos[i] >= '0' && keyPos[i] <= '9' ) {
			ret = (10 * ret) + keyPos[i] - '0';
			numDigits ++;
		} else if (numDigits > 0)
			numDigits = -1;
		i++;
	}
	printWarning("---> %d\n", ret);
	return ret;
} //readKey()
*/

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
		if ((isQuote) && (keyPos[i] != '"') && (outLen < kDICOMStr)) {
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
        //if (!littleEndianPlatform())
        //    nifti_swap_4bytes(1, &tagCSA.nitems);
        //printf("%d CSA of %s %d\n",lPos, tagCSA.name, tagCSA.nitems);
        lPos +=sizeof(tagCSA);

        if (strcmp(tagCSA.name, "MrPhoenixProtocol") == 0)
        	return lPos;
        for (int lI = 1; lI <= tagCSA.nitems; lI++) {
                memcpy(&itemCSA, &buff[lPos], sizeof(itemCSA));
                lPos +=sizeof(itemCSA);
                //if (!littleEndianPlatform())
                //    nifti_swap_4bytes(1, &itemCSA.xx2_Len);
                lPos += ((itemCSA.xx2_Len +3)/4)*4;
        }
    }
    return 0;
} // phoenixOffsetCSASeriesHeader()

void siemensCsaAscii(const char * filename,  int csaOffset, int csaLength, float* delayTimeInTR, float* phaseOversampling, float* phaseResolution, float* txRefAmp, float* shimSetting, int* baseResolution, int* interp, int* partialFourier, int* echoSpacing, int* parallelReductionFactorInPlane, char* coilID, char* consistencyInfo, char* coilElements, char* pulseSequenceDetails, char* fmriExternalInfo, char * protocolName) {
 //reads ASCII portion of CSASeriesHeaderInfo and returns lEchoTrainDuration or lEchoSpacing value
 // returns 0 if no value found
 	*delayTimeInTR = 0.0;
 	*phaseOversampling = 0.0;
 	*phaseResolution = 0.0;
 	*txRefAmp = 0.0;
 	*baseResolution = 0;
 	*interp = 0;
 	*partialFourier = 0;
 	*echoSpacing = 0;
 	for (int i = 0; i < 8; i++)
 		shimSetting[i] = 0.0;
 	strcpy(coilID, "");
 	strcpy(consistencyInfo, "");
 	strcpy(coilElements, "");
 	strcpy(pulseSequenceDetails, "");
 	strcpy(fmriExternalInfo, "");
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
		char keyStrEnd[] = "### ASCCONV END";
		char *keyPosEnd = (char *)memmem(keyPos, csaLengthTrim, keyStrEnd, strlen(keyStrEnd));
		if ((keyPosEnd) && ((keyPosEnd - keyPos) < csaLengthTrim)) //ignore binary data at end
			csaLengthTrim = (int)(keyPosEnd - keyPos);
		char keyStrES[] = "sFastImaging.lEchoSpacing";
		*echoSpacing  = readKey(keyStrES, keyPos, csaLengthTrim);
		char keyStrBase[] = "sKSpace.lBaseResolution";
		*baseResolution = readKey(keyStrBase, keyPos, csaLengthTrim);
		char keyStrInterp[] = "sKSpace.uc2DInterpolation";
		*interp = readKey(keyStrInterp, keyPos, csaLengthTrim);
		char keyStrPF[] = "sKSpace.ucPhasePartialFourier";
		*partialFourier = readKey(keyStrPF, keyPos, csaLengthTrim);
		//char keyStrETD[] = "sFastImaging.lEchoTrainDuration";
		//*echoTrainDuration = readKey(keyStrETD, keyPos, csaLengthTrim);
		char keyStrAF[] = "sPat.lAccelFactPE";
		*parallelReductionFactorInPlane = readKey(keyStrAF, keyPos, csaLengthTrim);
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
		char keyStrExt[] = "FmriExternalInfo";
		readKeyStr(keyStrExt,  keyPos, csaLengthTrim, fmriExternalInfo);
		char keyStrPn[] = "tProtocolName";
		readKeyStr(keyStrPn,  keyPos, csaLengthTrim, protocolName);
		char keyStrDelay[] = "lDelayTimeInTR";
		*delayTimeInTR = readKeyFloat(keyStrDelay, keyPos, csaLengthTrim);
		char keyStrOver[] = "sKSpace.dPhaseOversamplingForDialog";
		*phaseOversampling = readKeyFloat(keyStrOver, keyPos, csaLengthTrim);
		char keyStrPhase[] = "sKSpace.dPhaseResolution";
		*phaseResolution = readKeyFloat(keyStrPhase, keyPos, csaLengthTrim);
		char keyStrAmp[] = "sTXSPEC.asNucleusInfo[0].flReferenceAmplitude";
		*txRefAmp = readKeyFloat(keyStrAmp, keyPos, csaLengthTrim);
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
 // #define myReadGeProtocolBlock
#endif
#ifdef myReadGeProtocolBlock
int  geProtocolBlock(const char * filename,  int geOffset, int geLength, int isVerbose, int* sliceOrder, int* viewOrder) {
	*sliceOrder = 0;
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
	*sliceOrder  = readKey(keyStrSO, (char *) pUnCmp, unCmpSz);
	char keyStrVO[] = "VIEWORDER"; //"MATRIXX";
	*viewOrder  = readKey(keyStrVO, (char *) pUnCmp, unCmpSz);
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

void json_Float(FILE *fp, const char *sLabel, float sVal) {
	if (sVal <= 0.0) return;
	fprintf(fp, sLabel, sVal );
} //json_Float

void nii_SaveBIDS(char pathoutname[], struct TDICOMdata d, struct TDCMopts opts, struct nifti_1_header *h, const char * filename) {
//https://docs.google.com/document/d/1HFUkAEE-pB-angVcYe6pf_-fVf4sCpOHKesUvfb8Grc/edit#
// Generate Brain Imaging Data Structure (BIDS) info
// sidecar JSON file (with the same  filename as the .nii.gz file, but with .json extension).
// we will use %g for floats since exponents are allowed
// we will not set the locale, so decimal separator is always a period, as required
//  https://www.ietf.org/rfc/rfc4627.txt
	if ((!opts.isCreateBIDS) && (opts.isOnlyBIDS)) printMessage("Input-only mode: no BIDS/NIfTI output generated.\n");
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
	if (d.fieldStrength > 0.0) fprintf(fp, "\t\"MagneticFieldStrength\": %g,\n", d.fieldStrength );
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
	if ((d.intenScalePhilips != 0) || (d.manufacturer == kMANUFACTURER_PHILIPS)) { //for details, see PhilipsPrecise()
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
	if (d.echoNum > 1) fprintf(fp, "\t\"EchoNumber\": %d,\n", d.echoNum);
	if ((d.TE > 0.0) && (!d.isXRay)) fprintf(fp, "\t\"EchoTime\": %g,\n", d.TE / 1000.0 );
	//if ((d.TE2 > 0.0) && (!d.isXRay)) fprintf(fp, "\t\"EchoTime2\": %g,\n", d.TE2 / 1000.0 );
	json_Float(fp, "\t\"RepetitionTime\": %g,\n", d.TR / 1000.0 );
    json_Float(fp, "\t\"InversionTime\": %g,\n", d.TI / 1000.0 );
	json_Float(fp, "\t\"FlipAngle\": %g,\n", d.flipAngle );
	float pf = 1.0f; //partial fourier
	bool interp = false; //2D interpolation
	float phaseOversampling = 0.0;
	int viewOrderGE = -1;
	int sliceOrderGE = -1;
	if (d.phaseEncodingGE != kGE_PHASE_DIRECTION_UNKNOWN) { //only set for GE
		if (d.phaseEncodingGE == kGE_PHASE_DIRECTION_BOTTOM_UP) fprintf(fp, "\t\"PhaseEncodingGE\": \"BottomUp\",\n" );
		if (d.phaseEncodingGE == kGE_PHASE_DIRECTION_TOP_DOWN) fprintf(fp, "\t\"PhaseEncodingGE\": \"TopDown\",\n" );
		if (d.phaseEncodingGE == kGE_PHASE_DIRECTION_CENTER_OUT_REV) fprintf(fp, "\t\"PhaseEncodingGE\": \"CenterOutReversed\",\n" );
		if (d.phaseEncodingGE == kGE_PHASE_DIRECTION_CENTER_OUT) fprintf(fp, "\t\"PhaseEncodingGE\": \"CenterOut\",\n" );
	}
	#ifdef myReadGeProtocolBlock
	if ((d.manufacturer == kMANUFACTURER_GE) && (d.protocolBlockStartGE> 0) && (d.protocolBlockLengthGE > 19)) {
		printWarning("Using GE Protocol Data Block for BIDS data (beware: new feature)\n");
		int ok = geProtocolBlock(filename, d.protocolBlockStartGE, d.protocolBlockLengthGE, opts.isVerbose, &sliceOrderGE, &viewOrderGE);
		if (ok != EXIT_SUCCESS)
			printWarning("Unable to decode GE protocol block\n");
		printMessage(" ViewOrder %d SliceOrder %d\n", viewOrderGE, sliceOrderGE);
	} //read protocolBlockGE
	#endif
	#ifdef myReadAsciiCsa
	if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (d.CSA.SeriesHeader_offset > 0) && (d.CSA.SeriesHeader_length > 0)) {
		int baseResolution, interpInt, partialFourier, echoSpacing, parallelReductionFactorInPlane;
		float delayTimeInTR, phaseResolution, txRefAmp, shimSetting[8];
		char protocolName[kDICOMStr], fmriExternalInfo[kDICOMStr], coilID[kDICOMStr], consistencyInfo[kDICOMStr], coilElements[kDICOMStr], pulseSequenceDetails[kDICOMStr];
		siemensCsaAscii(filename,  d.CSA.SeriesHeader_offset, d.CSA.SeriesHeader_length, &delayTimeInTR, &phaseOversampling, &phaseResolution, &txRefAmp, shimSetting, &baseResolution, &interpInt, &partialFourier, &echoSpacing, &parallelReductionFactorInPlane, coilID, consistencyInfo, coilElements, pulseSequenceDetails, fmriExternalInfo, protocolName);
		if (partialFourier > 0) {
			//https://github.com/ismrmrd/siemens_to_ismrmrd/blob/master/parameter_maps/IsmrmrdParameterMap_Siemens_EPI_FLASHREF.xsl
			if (partialFourier == 1) pf = 0.5; // 4/8
			if (partialFourier == 2) pf = 0.625; // 5/8
			if (partialFourier == 4) pf = 0.75;
			if (partialFourier == 8) pf = 0.875;
			fprintf(fp, "\t\"PartialFourier\": %g,\n", pf);
		}
		if (interpInt > 0) {
			interp = true;
			fprintf(fp, "\t\"Interpolation2D\": %d,\n", interp);
		}
		if (baseResolution > 0) fprintf(fp, "\t\"BaseResolution\": %d,\n", baseResolution );
		if (shimSetting[0] != 0.0) {
			fprintf(fp, "\t\"ShimSetting\": [\n");
			for (int i = 0; i < 8; i++) {
				if (i != 0)
					fprintf(fp, ",\n");
				fprintf(fp, "\t\t%g", shimSetting[i]);
			}
			fprintf(fp, "\t],\n");
		}
		//DelayTimeInTR
		// https://groups.google.com/forum/#!topic/bids-discussion/nmg1BOVH1SU
		// https://groups.google.com/forum/#!topic/bids-discussion/seD7AtJfaFE
		json_Float(fp, "\t\"DelayTime\": %g,\n", delayTimeInTR/ 1000000.0); //DelayTimeInTR usec -> sec
		json_Float(fp, "\t\"TxRefAmp\": %g,\n", txRefAmp);
		json_Float(fp, "\t\"PhaseResolution\": %g,\n", phaseResolution);
		json_Float(fp, "\t\"PhaseOversampling\": %g,\n", phaseOversampling); //usec -> sec
		json_Float(fp, "\t\"VendorReportedEchoSpacing\": %g,\n", echoSpacing / 1000000.0); //usec -> sec
		//ETD and epiFactor not useful/reliable https://github.com/rordenlab/dcm2niix/issues/127
		//if (echoTrainDuration > 0) fprintf(fp, "\t\"EchoTrainDuration\": %g,\n", echoTrainDuration / 1000000.0); //usec -> sec
		//if (epiFactor > 0) fprintf(fp, "\t\"EPIFactor\": %d,\n", epiFactor);
		json_Str(fp, "\t\"ReceiveCoilName\": \"%s\",\n", coilID);
		json_Str(fp, "\t\"ReceiveCoilActiveElements\": \"%s\",\n", coilElements);
		json_Str(fp, "\t\"PulseSequenceDetails\": \"%s\",\n", pulseSequenceDetails);
		json_Str(fp, "\t\"FmriExternalInfo\": \"%s\",\n", fmriExternalInfo);
		if (strlen(d.protocolName) < 1)  //insert protocol name if it exists in CSA but not DICOM header: https://github.com/nipy/heudiconv/issues/80
			json_Str(fp, "\t\"ProtocolName\": \"%s\",\n", protocolName);
		json_Str(fp, "\t\"ConsistencyInfo\": \"%s\",\n", consistencyInfo);
		if (parallelReductionFactorInPlane > 0) {//AccelFactorPE -> phase encoding
			if (d.accelFactPE < 1.0) { //value not found in DICOM header, but WAS found in CSA ascii
				d.accelFactPE = parallelReductionFactorInPlane; //value found in ASCII but not in DICOM (0051,1011)
				//fprintf(fp, "\t\"ParallelReductionFactorInPlane\": %g,\n", d.accelFactPE);
			}
			if (parallelReductionFactorInPlane != (int)(d.accelFactPE))
				printWarning("ParallelReductionFactorInPlane reported in DICOM [0051,1011] (%d) does not match CSA series value %d\n", (int)(d.accelFactPE), parallelReductionFactorInPlane);
		}
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
		if  (h->dim[2] == h->dim[2]) //phase encoding does not matter
			reconMatrixPE = h->dim[2];
		else if (d.phaseEncodingRC =='R')
			reconMatrixPE = h->dim[2];
		else if (d.phaseEncodingRC =='C')
			reconMatrixPE = h->dim[1];
    }
	if (reconMatrixPE > 0) fprintf(fp, "\t\"ReconMatrixPE\": %d,\n", reconMatrixPE );
    double bandwidthPerPixelPhaseEncode = d.bandwidthPerPixelPhaseEncode;
    if (bandwidthPerPixelPhaseEncode == 0.0)
    	bandwidthPerPixelPhaseEncode = 	d.CSA.bandwidthPerPixelPhaseEncode;
    json_Float(fp, "\t\"BandwidthPerPixelPhaseEncode\": %g,\n", bandwidthPerPixelPhaseEncode );
    if (d.accelFactPE > 1.0) fprintf(fp, "\t\"ParallelReductionFactorInPlane\": %g,\n", d.accelFactPE);
	//EffectiveEchoSpacing
	// Siemens bandwidthPerPixelPhaseEncode already accounts for the effects of parallel imaging,
	// interpolation, phaseOversampling, and phaseResolution, in the context of the size of the
	// *reconstructed* data in the PE dimension
    double effectiveEchoSpacing = 0.0;
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
    if ((reconMatrixPE > 0) && (effectiveEchoSpacing > 0.0))
	  fprintf(fp, "\t\"TotalReadoutTime\": %g,\n", effectiveEchoSpacing * (reconMatrixPE - 1.0));
    json_Float(fp, "\t\"PixelBandwidth\": %g,\n", d.pixelBandwidth );
	if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (d.dwellTime > 0))
		fprintf(fp, "\t\"DwellTime\": %g,\n", d.dwellTime * 1E-9);
	// Phase encoding polarity
	int phPos = d.CSA.phaseEncodingDirectionPositive;
	if (viewOrderGE > -1)
		phPos = viewOrderGE;
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
		//these next lines temporary while we understand GE
		if (viewOrderGE > -1) {
			fprintf(fp, "\",\n");
			if (d.phaseEncodingRC == 'C') //Values should be "R"ow, "C"olumn or "?"Unknown
				fprintf(fp, "\t\"ProbablePhaseEncodingDirection\": \"j");
			else if (d.phaseEncodingRC == 'R')
					fprintf(fp, "\t\"ProbablePhaseEncodingDirection\": \"i");
			else
				fprintf(fp, "\t\"ProbablePhaseEncodingDirection\": \"?");
		}
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
	// Slice Timing GE
	if ((sliceOrderGE > -1) && (h->dim[3] > 1) && (h->dim[4] > 1) && (d.TR > 0)) { //
		//Warning: not correct for multiband sequences... not sure how these are stored
		//Warning: will not create correct times for sparse acquisitions where DelayTimeInTR > 0
		float t = d.TR/ (float)h->dim[3] ;
		fprintf(fp, "\t\"ProbableSliceTiming\": [\n");
		if (sliceOrderGE == 1) {//interleaved ascending
			for (int i = 0; i < h->dim[3]; i++) {
				if (i != 0)
					fprintf(fp, ",\n");
				int s = (i / 2);
				if ((i % 2) != 0) s += (h->dim[3]+1)/2;
				fprintf(fp, "\t\t%g", 	(float) s * t / 1000.0 );
			}
		} else { //sequential ascending
			for (int i = 0; i < h->dim[3]; i++) {
				if (i != 0)
					fprintf(fp, ",\n");
				fprintf(fp, "\t\t%g", 	(float) i * t / 1000.0 );
			}
		}
		fprintf(fp, "\t],\n");
	}
	//Slice Timing Siemens
	if (d.CSA.sliceTiming[0] >= 0.0) {
   		fprintf(fp, "\t\"SliceTiming\": [\n");
   		if (d.CSA.protocolSliceNumber1 > 1) {
   			//https://github.com/rordenlab/dcm2niix/issues/40
   			//equivalent to dicm2nii "s.SliceTiming = s.SliceTiming(end:-1:1);"
   			int mx = 0;
   			for (int i = 0; i < kMaxEPI3D; i++) {
				if (d.CSA.sliceTiming[i] < 0.0) break;
				mx++;
			}
			mx--;
			for (int i = mx; i >= 0; i--) {
				if (d.CSA.sliceTiming[i] < 0.0) break;
				if (i != mx)
					fprintf(fp, ",\n");
				fprintf(fp, "\t\t%g", d.CSA.sliceTiming[i] / 1000.0 );
			}
   		} else {
			for (int i = 0; i < kMaxEPI3D; i++) {
				if (d.CSA.sliceTiming[i] < 0.0) break;
				if (i != 0)
					fprintf(fp, ",\n");
				fprintf(fp, "\t\t%g", d.CSA.sliceTiming[i] / 1000.0 );
			}
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
	fprintf(fp, "\t\"ConversionSoftwareVersion\": \"%s\"\n", kDCMvers );
	//fprintf(fp, "\t\"DicomConversion\": [\"dcm2niix\", \"%s\"]\n", kDCMvers );
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

int * nii_SaveDTI(char pathoutname[],int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TDCMopts opts, int sliceDir, struct TDTI4D *dti4D, int * numADC) {
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
        for (int i = 0; i < numDti; i++)
                printMessage("bxyz %g %g %g %g\n",vx[i].V[0],vx[i].V[1],vx[i].V[2],vx[i].V[3]);
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
	for (int i = 0; i < numDti; i++) {
		bvals[i] = vx[i].V[0];
		//printMessage("---bxyz %g %g %g %g\n",vx[i].V[0],vx[i].V[1],vx[i].V[2],vx[i].V[3]);
		if (isADCnotDTI(vx[i])) {
            *numADC = *numADC + 1;
            bvals[i] = kADCval;
            //printMessage("+++bxyz %d\n",i);
        }
        bvals[i] = bvals[i] + (0.5 * i/numDti); //add a small bias so ties are kept in sequential order
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
            printMessage("%d\tB=\t%g\tVec=\t%g\t%g\t%g\n",i, vx[i].V[0],
                   vx[i].V[1],vx[i].V[2],vx[i].V[3]);

        } //for each direction
    }
    //printMessage("%f\t%f\t%f",dcmList[indx0].CSA.dtiV[1][1],dcmList[indx0].CSA.dtiV[1][2],dcmList[indx0].CSA.dtiV[1][3]);
#ifdef HAVE_R
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
}// nii_SaveDTI()

float sqr(float v){
    return v*v;
}// sqr()

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
//#ifdef _MSC_VER
	TDCMsort * dcmSortIn = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
//#else
//    struct TDCMsort dcmSortIn[nConvert];
//#endif
    for (int i = 0; i < nConvert; i++) dcmSortIn[i] = dcmSort[i];
    int i = 0;
    for (int b = 0; b < d3; b++)
        for (int a = 0; a < d4; a++) {
            int k = (a *d3) + b;
            //printMessage("%d -> %d %d ->%d\n",i,a, b, k);
            dcmSort[k] = dcmSortIn[i];
            i++;
        }
//#ifdef _MSC_VER
	free(dcmSortIn);
//#endif
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
            	printError("You do not have write permissions for the directory %s\n",opts.outdir);
                return EXIT_FAILURE;
            }
            printWarning("%s write permission denied. Saving to working directory %s \n", opts.outdir, pth);
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
    size_t start = 0;
    size_t pos = 0;
    bool isCoilReported = false;
    bool isEchoReported = false;
    bool isSeriesReported = false;
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
            if (f == 'C') strcat (outname,dcm.imageComments);
            if (f == 'D') strcat (outname,dcm.seriesDescription);
        	if (f == 'E') {
        		isEchoReported = true;
                sprintf(newstr, "%d", dcm.echoNum);
                strcat (outname,newstr);
            }
            if (f == 'F')
                strcat (outname,opts.indirParent);
            if (f == 'I')
                strcat (outname,dcm.patientID);
            if (f == 'J')
                strcat (outname,dcm.seriesInstanceUID);
            if (f == 'K')
                strcat (outname,dcm.studyInstanceUID);
            if (f == 'L') //"L"ocal Institution-generated description or classification of the Procedure Step that was performed.
                strcat (outname,dcm.procedureStepDescription);
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
            if (f == 'P') {
                strcat (outname,dcm.protocolName);
                if (strlen(dcm.protocolName) < 1)
                	printWarning("Unable to append protocol name (0018,1030) to filename (it is empty).\n");
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
				#ifdef mySegmentByAcq
				sprintf(newstr, "%d", dcm.acquNum);
				strcat (outname,newstr);
				#else
    			printWarning("Ignoring '%%u' in output filename (recompile to segment by acquisition)\n");
    			#endif
			}
			if (f == 'V') {
				if (dcm.manufacturer == kMANUFACTURER_GE)
					strcat (outname,"GE");
				else if (dcm.manufacturer == kMANUFACTURER_PHILIPS)
					strcat (outname,"Philips");
				else if (dcm.manufacturer == kMANUFACTURER_SIEMENS)
					strcat (outname,"Siemens");
				else if (dcm.manufacturer == kMANUFACTURER_TOSHIBA)
					strcat (outname,"Toshiba");
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
                    //sprintf(zeroPad,"%%0%dd",atoi(&f));
                    sprintf(zeroPad,"%%0%dd",f - '0');
                    sprintf(newstr, zeroPad, dcm.seriesNum);
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
    if (!isCoilReported && (dcm.coilNum > 1)) {
        sprintf(newstr, "_c%d", dcm.coilNum);
        strcat (outname,newstr);
    }
    if ((!isEchoReported) && (dcm.isMultiEcho) && (dcm.echoNum >= 1)) { //multiple echoes saved as same series
        sprintf(newstr, "_e%d", dcm.echoNum);
        strcat (outname,newstr);
        isEchoReported = true;
    }
    if ((!isSeriesReported) && (!isEchoReported) && (dcm.echoNum > 1)) { //last resort: user provided no method to disambiguate echo number in filename
        sprintf(newstr, "_e%d", dcm.echoNum);
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
    if (dcm.triggerDelayTime >= 1) {
    	sprintf(newstr, "_t%d", (int)roundf(dcm.triggerDelayTime));
        strcat (outname,newstr);
    }
    if (strlen(outname) < 1) strcpy(outname, "dcm2nii_invalidName");
    if (outname[0] == '.') outname[0] = '_'; //make sure not a hidden file
    //eliminate illegal characters http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx
    #if defined(_WIN64) || defined(_WIN32) //https://stackoverflow.com/questions/1976007/what-characters-are-forbidden-in-windows-and-linux-directory-names
    for (size_t pos = 0; pos<strlen(outname); pos ++)
        if ((outname[pos] == '<') || (outname[pos] == '>') || (outname[pos] == ':')
            || (outname[pos] == '"') // || (outname[pos] == '/') || (outname[pos] == '\\')
            || (outname[pos] == '^')
            || (outname[pos] == '*') || (outname[pos] == '|') || (outname[pos] == '?'))
            outname[pos] = '_';
    for (int pos = 0; pos<strlen(outname); pos ++)
        if (outname[pos] == '/')
        	outname[pos] = kPathSeparator; //for Windows, convert "/" to "\"
    #else
    for (size_t pos = 0; pos<strlen(outname); pos ++)
        if (outname[pos] == ':') //not allowed by MacOS
        	outname[pos] = '_';
    #endif
    char baseoutname[2048] = {""};
    strcat (baseoutname,pth);
    char appendChar[2] = {"a"};
    appendChar[0] = kPathSeparator;
    if ((pth[strlen(pth)-1] != kPathSeparator) && (outname[0] != kPathSeparator))
        strcat (baseoutname,appendChar);
	//Allow user to specify new folders, e.g. "-f dir/%p" or "-f %s/%p/%m"
	// These folders are created if they do not exist
    char *sep = strchr(outname, kPathSeparator);
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
    char niiFilenameBase[1024] = {"/usr/myFolder/dicom.dcm"};
    nii_createFilename(d, niiFilenameBase, opts) ;
    strcpy(niiFilename,"Example output filename: '");
    strcat(niiFilename,niiFilenameBase);
    if (opts.isGz)
        strcat(niiFilename,".nii.gz'");
    else
        strcat(niiFilename,".nii'");
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

void writeNiiGz (char * baseName, struct nifti_1_header hdr,  unsigned char* src_buffer, unsigned long src_len, int gzLevel) {
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
    int zLevel = MZ_DEFAULT_LEVEL;//Z_DEFAULT_COMPRESSION;
    if ((gzLevel > 0) && (gzLevel < 11))
    	zLevel = gzLevel;
    if (zLevel > MZ_UBER_COMPRESSION)
    	zLevel = MZ_UBER_COMPRESSION;
    if (deflateInit(&strm, zLevel)!= Z_OK) {
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

#ifdef HAVE_R

// Version of nii_saveNII() for R/divest: create nifti_image pointer and push onto stack
int nii_saveNII (char *niiFilename, struct nifti_1_header hdr, unsigned char *im, struct TDCMopts opts)
{
    hdr.vox_offset = 352;

    // Extract the basename from the full file path
    // R always uses '/' as the path separator, so this should work on all platforms
    char *start = niiFilename + strlen(niiFilename);
    while (*start != '/')
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

void nii_saveAttributes (struct TDICOMdata &data, struct nifti_1_header &header, struct TDCMopts &opts)
{
    ImageList *images = (ImageList *) opts.imageList;
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
    if ((data.CSA.bandwidthPerPixelPhaseEncode > 0.0) && (header.dim[2] > 0) && (header.dim[1] > 0)) {
        if (data.phaseEncodingRC =='C')
            images->addAttribute("dwellTime", 1.0/data.CSA.bandwidthPerPixelPhaseEncode/header.dim[2]);
        else if (data.phaseEncodingRC == 'R')
            images->addAttribute("dwellTime", 1.0/data.CSA.bandwidthPerPixelPhaseEncode/header.dim[1]);
    }
    if (data.phaseEncodingRC == 'C')
        images->addAttribute("phaseEncodingDirection", "j");
    else if (data.phaseEncodingRC == 'R')
        images->addAttribute("phaseEncodingDirection", "i");
    if (data.CSA.phaseEncodingDirectionPositive != -1)
        images->addAttribute("phaseEncodingSign", data.CSA.phaseEncodingDirectionPositive == 0 ? -1 : 1);
}

#else

int nii_saveNII(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts) {
    if (opts.isOnlyBIDS) return EXIT_SUCCESS;
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
			writeNiiGz (niiFilename, hdr,  im, imgsz, opts.gzLevel);
			return EXIT_SUCCESS;
		}
		#endif
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
    	#ifndef myDisableGzSizeLimits
    	if ((imgsz+hdr.vox_offset) >  kMaxPigz) {
        	printWarning("Saving uncompressed data: image too large for pigz.\n");
    		return EXIT_SUCCESS;
    	}
    	#endif
    	char command[768];
    	strcpy(command, "\"" );
        strcat(command, opts.pigzname );
        if ((opts.gzLevel > 0) &&  (opts.gzLevel < 12)) {
        	char newstr[256];
        	sprintf(newstr, "\" -n -f -%d \"", opts.gzLevel);
        	strcat(command, newstr);
        } else
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
                //printMessage("compression --- %s\n",command);
        		WaitForSingleObject(ProcessInfo.hProcess,INFINITE);
        		CloseHandle(ProcessInfo.hThread);
        		CloseHandle(ProcessInfo.hProcess);
    		} else
    			printMessage("compression failed %s\n",command);
    	#else //if win else linux
        int ret = system(command);
        if (ret == -1)
        	printWarning("Failed to execute: %s\n",command);
        #endif //else linux
        printMessage("compress: %s\n",command);
    }
    return EXIT_SUCCESS;
}// nii_saveNII()

#endif

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
void nii_scale16bitSigned(unsigned char *img, struct nifti_1_header *hdr) {
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
	if (scale < 2) return; //already uses dynamic range
    hdr->scl_slope = hdr->scl_slope/ scale;
	for (int i=0; i < nVox; i++)
    	img16[i] = img16[i] * scale;
    printMessage("Maximizing 16-bit range: raw %d..%d is%d\n", min16, max16, scale);
    nii_storeIntegerScaleFactor(scale, hdr);
}


void nii_scale16bitUnsigned(unsigned char *img, struct nifti_1_header *hdr){
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
	if (scale < 2) return; //already uses dynamic range
	hdr->scl_slope = hdr->scl_slope/ scale;
	for (int i=0; i < nVox; i++)
    	img16[i] = img16[i] * scale;
    printMessage("Maximizing 16-bit range: raw max %d is%d\n", max16, scale);
    nii_storeIntegerScaleFactor(scale, hdr);
}

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
    //printMessage("max16= %d vox=%d %fms\n",max16, nVox, ((double)(clock()-start))/1000);
    if (max16 > 32767) {
        printMessage("Note: rare 16-bit UNSIGNED integer image. Older tools may require 32-bit conversion\n");
    }
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

unsigned char * nii_saveNII3Dtilt(char * niiFilename, struct nifti_1_header * hdr, unsigned char* im, struct TDCMopts opts, float * sliceMMarray, float gantryTiltDeg, int manufacturer ) {
    //correct for gantry tilt - http://www.mathworks.com/matlabcentral/fileexchange/24458-dicom-gantry-tilt-correction
    if (opts.isOnlyBIDS) return im;
    if (gantryTiltDeg == 0.0) return im;
    struct nifti_1_header hdrIn = *hdr;
    int nVox2DIn = hdrIn.dim[1]*hdrIn.dim[2];
    if ((nVox2DIn < 1) || (hdrIn.dim[0] != 3) || (hdrIn.dim[3] < 3)) return im;
    if (hdrIn.datatype != DT_INT16) {
        printMessage("Only able to correct gantry tilt for 16-bit integer data with at least 3 slices.");
        return im;
    }
    printMessage("Gantry Tilt Correction is new: please validate conversions\n");
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
    // printMessage("gantry tilt pixels per mm %g\n",GNTtanPx);
    short * imIn16 = ( short*) im;
	//create new output image: larger due to skew
	// compute how many pixels slice must be extended due to skew
    int s = hdrIn.dim[3] - 1; //top slice
    float maxSliceMM = fabs(s * hdrIn.pixdim[3]);
    if (sliceMMarray != NULL) maxSliceMM = fabs(sliceMMarray[s]);
    int pxOffset = ceil(fabs(GNTtanPx*maxSliceMM));
    // printMessage("Tilt extends slice by %d pixels", pxOffset);
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
    if (opts.isOnlyBIDS) return EXIT_SUCCESS;
    int nVox2D = hdr.dim[1]*hdr.dim[2];
    if ((nVox2D < 1) || (hdr.dim[0] != 3) ) return EXIT_FAILURE;
    if ((hdr.datatype != DT_UINT8) && (hdr.datatype != DT_RGB24) && (hdr.datatype != DT_INT16)) {
        printMessage("Only able to make equidistant slices from 3D 8,16,24-bit volumes with at least 3 slices.");
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
    	printMessage("Unable to equalize slice distances: slice number not consistent with slice position.\n");
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
}// nii_saveNII3Deq()

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

int nii_saveCrop(char * niiFilename, struct nifti_1_header hdr, unsigned char* im, struct TDCMopts opts) {
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
    const int returnCode = nii_saveNII3D(niiFilenameCrop, hdrX, imX, opts);
    free(imX);
    return returnCode;
}// nii_saveCrop()

float dicomTimeToSec (float dicomTime) {
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

void checkSliceTiming(struct TDICOMdata * d, struct TDICOMdata * d1) {
//detect images with slice timing errors. https://github.com/rordenlab/dcm2niix/issues/126
	if ((d->TR < 0.0) || (d->CSA.sliceTiming[0] < 0.0)) return; //no slice timing
	float minT = d->CSA.sliceTiming[0];
	float maxT = minT;
	for (int i = 0; i < kMaxEPI3D; i++) {
		if (d->CSA.sliceTiming[i] < 0.0) break;
		if (d->CSA.sliceTiming[i] < minT) minT = d->CSA.sliceTiming[i];
		if (d->CSA.sliceTiming[i] > maxT) maxT = d->CSA.sliceTiming[i];
	}
	if ((minT != maxT) && (maxT <= d->TR)) return; //looks fine
	if ((minT == maxT) && (d->is3DAcq)) return; //fine: 3D EPI
	if ((minT == maxT) && (d->CSA.multiBandFactor == d->CSA.mosaicSlices)) return; //fine: all slices single excitation
	if ((strlen(d->seriesDescription) > 0) && (strstr(d->seriesDescription, "SBRef") != NULL))  return; //fine: single-band calibration data, the slice timing WILL exceed the TR
	//check if 2nd image has valud slice timing
	float minT1 = d1->CSA.sliceTiming[0];
	float maxT1 = minT1;
	for (int i = 0; i < kMaxEPI3D; i++) {
		if (d1->CSA.sliceTiming[i] < 0.0) break;
		if (d1->CSA.sliceTiming[i] < minT1) minT1 = d1->CSA.sliceTiming[i];
		if (d1->CSA.sliceTiming[i] > maxT1) maxT1 = d1->CSA.sliceTiming[i];
	}
	if ((minT1 == maxT1) || (maxT1 >= d->TR)) { //both first and second image corrupted
		printWarning("CSA slice timing appears corrupted (range %g..%g, TR=%gms)\n", minT, maxT, d->TR);
		return;
	}
	//1st image corrupted, but 2nd looks ok - substitute values from 2nd image
	for (int i = 0; i < kMaxEPI3D; i++) {
		d->CSA.sliceTiming[i] = d1->CSA.sliceTiming[i];
		if (d1->CSA.sliceTiming[i] < 0.0) break;
	}
	d->CSA.multiBandFactor = d1->CSA.multiBandFactor;
	printMessage("CSA slice timing based on 2nd volume, 1st volume corrupted (CMRR bug, range %g..%g, TR=%gms)\n", minT, maxT, d->TR);
}//checkSliceTiming

int saveDcm2NiiCore(int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TSearchList *nameList, struct TDCMopts opts, struct TDTI4D *dti4D, int segVol) {
    bool iVaries = intensityScaleVaries(nConvert,dcmSort,dcmList);
    float *sliceMMarray = NULL; //only used if slices are not equidistant
    uint64_t indx = dcmSort[0].indx;
    uint64_t indx0 = dcmSort[0].indx;
    uint64_t indx1 = indx0;
    if (nConvert > 1) indx1 = dcmSort[1].indx;
    if (opts.isIgnoreDerivedAnd2D && dcmList[indx].isDerived) {
    	printMessage("Ignoring derived image(s) of series %ld %s\n", dcmList[indx].seriesNum,  nameList->str[indx]);
    	return EXIT_SUCCESS;
    }
    if ((opts.isIgnoreDerivedAnd2D) && ((dcmList[indx].isLocalizer) || (strcmp(dcmList[indx].sequenceName, "_fl3d1_ns")== 0) || (strcmp(dcmList[indx].sequenceName, "_fl2d1")== 0)) ) {
    	printMessage("Ignoring localizer (sequence %s) of series %ld %s\n", dcmList[indx].sequenceName, dcmList[indx].seriesNum,  nameList->str[indx]);
    	return EXIT_SUCCESS;
    }
    if ((opts.isIgnoreDerivedAnd2D) && (nConvert < 2) && (dcmList[indx].xyzDim[3] < 2)) {
    	printMessage("Ignoring 2D image of series %ld %s\n", dcmList[indx].seriesNum,  nameList->str[indx]);
    	return EXIT_SUCCESS;
    }
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
    //printMessage(" %d %d %d %d %lu\n", hdr0.dim[1], hdr0.dim[2], hdr0.dim[3], hdr0.dim[4], (unsigned long)[imgM length]);
    if (nConvert > 1) {
        //next: determine gantry tilt
        if (dcmList[indx0].gantryTilt != 0.0f)
            printMessage(" Warning: note these images have gantry tilt of %g degrees (manufacturer ID = %d)\n", dcmList[indx0].gantryTilt, dcmList[indx0].manufacturer);
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
            } else {
                hdr0.dim[3] = nConvert;
                if ((nAcq > 1) && (nConvert != nAcq)) {
                    printMessage("Slice positions repeated, but number of slices (%d) not divisible by number of repeats (%d): missing images?\n", nConvert, nAcq);
                }
            }
            //next: detect if ANY file flagged as echo vaies
            for (int i = 0; i < nConvert; i++)
            	if (dcmList[dcmSort[i].indx].isMultiEcho)
            		dcmList[indx0].isMultiEcho = true;
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
					printMessage(" OnsetTime = [");
					for (int i = 0; i < nConvert; i++)
							if (isSamePosition(dcmList[indx0],dcmList[dcmSort[i].indx])) {
								float trDiff = acquisitionTimeDifference(&dcmList[indx0], &dcmList[dcmSort[i].indx]);
								printMessage(" %g", trDiff);
							}
					printMessage(" ]\n");
				} //if trVaries
            } //if PET
            //next: detect variable inter-slice distance
            float dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
            bool dxVaries = false;
            for (int i = 1; i < nConvert; i++)
                if (!isSameFloatT(dx,intersliceDistance(dcmList[dcmSort[i-1].indx],dcmList[dcmSort[i].indx]),0.2))
                    dxVaries = true;
            if (hdr0.dim[4] < 2) {
                if (dxVaries) {
                    sliceMMarray = (float *) malloc(sizeof(float)*nConvert);
                    sliceMMarray[0] = 0.0f;
                    printMessage("Dims %d %d %d %d %d\n", hdr0.dim[1], hdr0.dim[2], hdr0.dim[3], hdr0.dim[4], nAcq);
                    printWarning("Interslice distance varies in this volume (incompatible with NIfTI format).\n");
                    printMessage(" Distance from first slice:\n");
                    printMessage("dx=[0");
                    for (int i = 1; i < nConvert; i++) {
                        float dx0 = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[i].indx]);
                        printMessage(" %g", dx0);
                        sliceMMarray[i] = dx0;
                    }
                    printMessage("]\n");
					int imageNumRange = 1 + abs( dcmList[dcmSort[nConvert-1].indx].imageNum -  dcmList[dcmSort[0].indx].imageNum);
					if ((imageNumRange > 1) && (imageNumRange != nConvert)) {
						printWarning("Missing images? Expected %d images, but instance number (0020,0013) ranges from %d to %d\n", nConvert, dcmList[dcmSort[0].indx].imageNum, dcmList[dcmSort[nConvert-1].indx].imageNum);
						printMessage("instance=[");
						for (int i = 0; i < nConvert; i++) {
							printMessage(" %d", dcmList[dcmSort[i].indx].imageNum);

						}
						printMessage("]\n");
                    } //imageNum not sequential

                }
            }
            if ((hdr0.dim[4] > 0) && (dxVaries) && (dx == 0.0) &&  ((dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_GE)  || (dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_PHILIPS))  ) { //Niels Janssen has provided GE sequential multi-phase acquisitions that also require swizzling
                swapDim3Dim4(hdr0.dim[3],hdr0.dim[4],dcmSort);
                dx = intersliceDistance(dcmList[dcmSort[0].indx],dcmList[dcmSort[1].indx]);
                printMessage("swizzling 3rd and 4th dimensions (XYTZ -> XYZT), assuming interslice distance is %f\n",dx);
            }
            if ((dx == 0.0 ) && (!dxVaries)) { //all images are the same slice - 16 Dec 2014
                printMessage(" Warning: all images appear to be a single slice - please check slice/vector orientation\n");
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
        for (int i = 1; i < nConvert; i++) { //stack additional images
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
        if (hdr0.dim[4] > 1) //for 4d datasets, last volume should be acquired before first
        	checkDateTimeOrder(&dcmList[dcmSort[0].indx], &dcmList[dcmSort[nConvert-1].indx]);
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
    char pathoutname[2048] = {""};
    if (nii_createFilename(dcmList[dcmSort[0].indx], pathoutname, opts) == EXIT_FAILURE) {
        free(imgM);
        return EXIT_FAILURE;
    }
    if (strlen(pathoutname) <1) {
        free(imgM);
        return EXIT_FAILURE;
    }
    // Prevent these DICOM files from being reused.
    for(int i = 0; i < nConvert; ++i)
      dcmList[dcmSort[i].indx].converted2NII = 1;
    if (opts.numSeries < 0) { //report series number but do not convert
    	if (segVol >= 0)
    		printMessage("\t%ld.%d\t%s\n", dcmList[dcmSort[0].indx].seriesNum, segVol-1, pathoutname);
    	else
    		printMessage("\t%ld\t%s\n", dcmList[dcmSort[0].indx].seriesNum, pathoutname);
    	printMessage(" %s\n",nameList->str[dcmSort[0].indx]);
    	return EXIT_SUCCESS;
    }
    checkSliceTiming(&dcmList[indx0], &dcmList[indx1]);
    int sliceDir = 0;
    if (hdr0.dim[3] > 1)sliceDir = headerDcm2Nii2(dcmList[dcmSort[0].indx],dcmList[dcmSort[nConvert-1].indx] , &hdr0, true);
	//UNCOMMENT NEXT TWO LINES TO RE-ORDER MOSAIC WHERE CSA's protocolSliceNumber does not start with 1
	if (dcmList[dcmSort[0].indx].CSA.protocolSliceNumber1 > 1) {
		printWarning("Weird CSA 'ProtocolSliceNumber' (System/Miscellaneous/ImageNumbering reversed): VALIDATE SLICETIMING AND BVECS\n");
		//https://www.healthcare.siemens.com/siemens_hwem-hwem_ssxa_websites-context-root/wcm/idc/groups/public/@global/@imaging/@mri/documents/download/mdaz/nzmy/~edisp/mri_60_graessner-01646277.pdf
		//see https://github.com/neurolabusc/dcm2niix/issues/40
		sliceDir = -1; //not sure how to handle negative determinants?
	}
	if (sliceDir < 0) {
        imgM = nii_flipZ(imgM, &hdr0);
        sliceDir = abs(sliceDir); //change this, we have flipped the image so GE DTI bvecs no longer need to be flipped!
    }
    // skip converting if user has specified one or more series, but has not specified this one
    if (opts.numSeries > 0) {
      int i = 0;
      float seriesNum = (float) dcmList[dcmSort[0].indx].seriesNum;
      if (segVol > 0)
      	seriesNum = seriesNum + ((float) segVol - 1.0) / 10.0; //n.b. we will have problems if segVol > 9. However, 9 distinct TEs/scalings/PhaseMag seems unlikely
      for (; i < opts.numSeries; i++) {
        if (isSameFloatGE(opts.seriesNumber[i], seriesNum)) {
        //if (opts.seriesNumber[i] == dcmList[dcmSort[0].indx].seriesNum) {
          break;
        }
      }
      if (i == opts.numSeries) {
        return EXIT_SUCCESS;
      }
    }
    //move before headerDcm2Nii2 checkSliceTiming(&dcmList[indx0], &dcmList[indx1]);
    //nii_SaveBIDS(pathoutname, dcmList[dcmSort[0].indx], opts, dti4D, &hdr0, nameList->str[dcmSort[0].indx]);
    nii_SaveBIDS(pathoutname, dcmList[dcmSort[0].indx], opts, &hdr0, nameList->str[dcmSort[0].indx]);
    if (opts.isOnlyBIDS) {
    	//note we waste time loading every image, however this ensures hdr0 matches actual output
        free(imgM);
        return EXIT_SUCCESS;
    }
	nii_SaveText(pathoutname, dcmList[dcmSort[0].indx], opts, &hdr0, nameList->str[indx]);
	int numADC = 0;
    int * volOrderIndex = nii_SaveDTI(pathoutname,nConvert, dcmSort, dcmList, opts, sliceDir, dti4D, &numADC);
    PhilipsPrecise(&dcmList[dcmSort[0].indx], opts.isPhilipsFloatNotDisplayScaling, &hdr0, opts.isVerbose);
    if ((opts.isMaximize16BitRange) && (hdr0.datatype == DT_INT16)) {
    	nii_scale16bitSigned(imgM, &hdr0); //allow INT16 to use full dynamic range
    } else if ((opts.isMaximize16BitRange) && (hdr0.datatype == DT_UINT16) &&  (!dcmList[dcmSort[0].indx].isSigned)) {
    	nii_scale16bitUnsigned(imgM, &hdr0); //allow UINT16 to use full dynamic range
    } else if ((!opts.isMaximize16BitRange) && (hdr0.datatype == DT_UINT16) &&  (!dcmList[dcmSort[0].indx].isSigned))
    	nii_check16bitUnsigned(imgM, &hdr0); //save UINT16 as INT16 if we can do this losslessly
    printMessage( "Convert %d DICOM as %s (%dx%dx%dx%d)\n",  nConvert, pathoutname, hdr0.dim[1],hdr0.dim[2],hdr0.dim[3],hdr0.dim[4]);
    //~ if (!dcmList[dcmSort[0].indx].isSlicesSpatiallySequentialPhilips)
    //~ 	nii_reorderSlices(imgM, &hdr0, dti4D);
    if (hdr0.dim[3] < 2)
    	printWarning("Check that 2D images are not mirrored.\n");
#ifndef HAVE_R
    else
        fflush(stdout); //GUI buffers printf, display all results
#endif
    if ((dcmList[dcmSort[0].indx].is3DAcq) && (hdr0.dim[3] > 1) && (hdr0.dim[0] < 4))
        imgM = nii_setOrtho(imgM, &hdr0); //printMessage("ortho %d\n", echoInt (33));
    else if (opts.isFlipY)//(FLIP_Y) //(dcmList[indx0].CSA.mosaicSlices < 2) &&
        imgM = nii_flipY(imgM, &hdr0);
    else
    	printMessage("DICOM row order preserved: may appear upside down in tools that ignore spatial transforms\n");
#ifndef myNoSave
    // Indicates success or failure of the (last) save
    int returnCode = EXIT_FAILURE;
    //printMessage(" x--> %d ----\n", nConvert);
    if (! opts.isRGBplanar) //save RGB as packed RGBRGBRGB... instead of planar RRR..RGGG..GBBB..B
        imgM = nii_planar2rgb(imgM, &hdr0, true); //NIfTI is packed while Analyze was planar
    if ((hdr0.dim[4] > 1) && (saveAs3D))
        returnCode = nii_saveNII3D(pathoutname, hdr0, imgM,opts);
    else {
        if (volOrderIndex) //reorder volumes
        	imgM = reorderVolumes(&hdr0, imgM, volOrderIndex);
#ifndef HAVE_R
		if ((opts.isIgnoreDerivedAnd2D) && (numADC > 0))
			printMessage("Ignoring derived diffusion image(s). Better isotropic and ADC maps can be generated later processing.\n");
		if ((!opts.isIgnoreDerivedAnd2D) && (numADC > 0)) {//ADC maps can disrupt analysis: save a copy with the ADC map, and another without
			char pathoutnameADC[2048] = {""};
			strcat(pathoutnameADC,pathoutname);
			strcat(pathoutnameADC,"_ADC");
			if (opts.isSave3D)
				nii_saveNII3D(pathoutnameADC, hdr0, imgM, opts);
			else
				nii_saveNII(pathoutnameADC, hdr0, imgM, opts);
		}
#endif
		imgM = removeADC(&hdr0, imgM, numADC);
#ifndef HAVE_R
		if (opts.isSave3D)
			returnCode = nii_saveNII3D(pathoutname, hdr0, imgM, opts);
		else
        	returnCode = nii_saveNII(pathoutname, hdr0, imgM, opts);
#endif
    }
#endif
    if (dcmList[indx0].gantryTilt != 0.0) {
        if (dcmList[indx0].isResampled) {
            printMessage("Tilt correction skipped: 0008,2111 reports RESAMPLED\n");
        } else if (opts.isTiltCorrect) {
            imgM = nii_saveNII3Dtilt(pathoutname, &hdr0, imgM,opts, sliceMMarray, dcmList[indx0].gantryTilt, dcmList[indx0].manufacturer);
            strcat(pathoutname,"_Tilt");
        } else
            printMessage("Tilt correction skipped\n");
    }
    if (sliceMMarray != NULL) {
        if (dcmList[indx0].isResampled) {
            printMessage("Slice thickness correction skipped: 0008,2111 reports RESAMPLED\n");
        }
        else
            returnCode = nii_saveNII3Deq(pathoutname, hdr0, imgM,opts, sliceMMarray);
        free(sliceMMarray);
    }
    if ((opts.isCrop) && (dcmList[indx0].is3DAcq)   && (hdr0.dim[3] > 1) && (hdr0.dim[0] < 4))//for T1 scan: && (dcmList[indx0].TE < 25)
        returnCode = nii_saveCrop(pathoutname, hdr0, imgM,opts); //n.b. must be run AFTER nii_setOrtho()!
#ifdef HAVE_R
    // Note that for R, only one image should be created per series
    // Hence the logical OR here
    if (returnCode == EXIT_SUCCESS || nii_saveNII(pathoutname,hdr0,imgM,opts) == EXIT_SUCCESS)
        nii_saveAttributes(dcmList[dcmSort[0].indx], hdr0, opts);
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
	for (int s = 1; s <= series; s++) {
		for (int i = 0; i < dcmList[indx].xyzDim[4]; i++)
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
		int ret2 = saveDcm2NiiCore(nConvert, dcmSort, dcmList, nameList, opts, dti4D, s);
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
        bool acqNumVaries, bitDepthVaries, dateTimeVaries, echoVaries, phaseVaries, coilVaries, nameVaries, nameEmpty, orientVaries;
};

TWarnings setWarnings() {
	TWarnings r;
	r.acqNumVaries = false;
	r.bitDepthVaries = false;
	r.dateTimeVaries = false;
	r.phaseVaries = false;
	r.echoVaries = false;
	r.coilVaries = false;
	r.nameVaries = false;
	r.nameEmpty = false;
	r.orientVaries = false;
	return r;
}

bool isSameSet (struct TDICOMdata d1, struct TDICOMdata d2, struct TDCMopts* opts, struct TWarnings* warnings, bool *isMultiEcho) {
    //returns true if d1 and d2 should be stacked together as a single output
    if (!d1.isValid) return false;
    if (!d2.isValid) return false;
    if (d1.modality != d2.modality) return false; //do not stack MR and CT data!
    if (d1.isDerived != d2.isDerived) return false; //do not stack raw and derived image types
    if (d1.manufacturer != d2.manufacturer) return false; //do not stack data from different vendors
	if (d1.seriesNum != d2.seriesNum) return false;
	#ifdef mySegmentByAcq
    if (d1.acquNum != d2.acquNum) return false;
    #else
    if (d1.acquNum != d2.acquNum) {
        if (!warnings->acqNumVaries)
        	printMessage("slices stacked despite varying acquisition numbers (if this is not desired please recompile)\n");
        warnings->acqNumVaries = true;
    }
    #endif
	if ((d1.bitsAllocated != d2.bitsAllocated) || (d1.xyzDim[1] != d2.xyzDim[1]) || (d1.xyzDim[2] != d2.xyzDim[2]) || (d1.xyzDim[3] != d2.xyzDim[3]) ) {
        if (!warnings->bitDepthVaries)
        	printMessage("slices not stacked: dimensions or bit-depth varies\n");
        warnings->bitDepthVaries = true;
        return false;
    }
    if (!isSameFloatDouble(d1.dateTime, d2.dateTime)) { //beware, some vendors incorrectly store Image Time (0008,0033) as Study Time (0008,0030).
    	if (!warnings->dateTimeVaries)
    		printMessage("slices not stacked: Study Date/Time (0008,0020 / 0008,0030) varies %12.12f ~= %12.12f\n", d1.dateTime, d2.dateTime);
    	warnings->dateTimeVaries = true;
    	return false;
    }
    if (opts->isForceStackSameSeries) {
    	if ((d1.TE != d2.TE) || (d1.echoNum != d2.echoNum))
    		*isMultiEcho = true;
    	return true; //we will stack these images, even if they differ in the following attributes
    }
    if ((d1.isHasImaginary != d2.isHasImaginary) || (d1.isHasPhase != d2.isHasPhase) || ((d1.isHasReal != d2.isHasReal))) {
    	if (!warnings->phaseVaries)
        	printMessage("slices not stacked: some are phase/real/imaginary maps, others are not. Use 'merge 2D slices' option to force stacking\n");
    	warnings->phaseVaries = true;
    	return false;
    }
    if ((d1.TE != d2.TE) || (d1.echoNum != d2.echoNum)) {
        if ((!warnings->echoVaries) && (d1.isXRay)) //for CT/XRay we check DICOM tag 0018,1152 (XRayExposure)
        	printMessage("slices not stacked: X-Ray Exposure varies (exposure %g, %g; number %d, %d). Use 'merge 2D slices' option to force stacking\n", d1.TE, d2.TE,d1.echoNum, d2.echoNum );
        if ((!warnings->echoVaries) && (!d1.isXRay)) //for MRI
        	printMessage("slices not stacked: echo varies (TE %g, %g; echo %d, %d). Use 'merge 2D slices' option to force stacking\n", d1.TE, d2.TE,d1.echoNum, d2.echoNum );
        warnings->echoVaries = true;
        *isMultiEcho = true;
        return false;
    }
    if (d1.coilNum != d2.coilNum) {
        if (!warnings->coilVaries)
        	printMessage("slices not stacked: coil varies\n");
        warnings->coilVaries = true;
        return false;
    }
    if ((strlen(d1.protocolName) < 1) && (strlen(d2.protocolName) < 1)) {
    	if (!warnings->nameEmpty)
    	printWarning("Empty protocol name(s) (0018,1030)\n");
    	warnings->nameEmpty = true;
    } else if ((strcmp(d1.protocolName, d2.protocolName) != 0)) {
        if (!warnings->nameVaries)
        	printMessage("slices not stacked: protocol name varies '%s' != '%s'\n", d1.protocolName, d2.protocolName);
        warnings->nameVaries = true;
        return false;
    }
    if ((!isSameFloatGE(d1.orient[1], d2.orient[1]) || !isSameFloatGE(d1.orient[2], d2.orient[2]) ||  !isSameFloatGE(d1.orient[3], d2.orient[3]) ||
    		!isSameFloatGE(d1.orient[4], d2.orient[4]) || !isSameFloatGE(d1.orient[5], d2.orient[5]) ||  !isSameFloatGE(d1.orient[6], d2.orient[6]) ) ) {
        if (!warnings->orientVaries)
        	printMessage("slices not stacked: orientation varies (localizer?) [%g %g %g %g %g %g] != [%g %g %g %g %g %g]\n",
               d1.orient[1], d1.orient[2], d1.orient[3],d1.orient[4], d1.orient[5], d1.orient[6],
               d2.orient[1], d2.orient[2], d2.orient[3],d2.orient[4], d2.orient[5], d2.orient[6]);
        warnings->orientVaries = true;
        return false;
    }
    return true;
}// isSameSet()

int singleDICOM(struct TDCMopts* opts, char *fname) {
    char filename[768] ="";
    strcat(filename, fname);
    if (isDICOMfile(filename) == 0) {
        printError("Not a DICOM image : %s\n", filename);
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
    dcmList[0].converted2NII = 1;
    dcmList[0] = readDICOMv(nameList.str[0], opts->isVerbose, opts->compressFlag, &dti4D); //ignore compile warning - memory only freed on first of 2 passes
    fillTDCMsort(dcmSort[0], 0, dcmList[0]);
    int ret = saveDcm2Nii(1, dcmSort, dcmList, &nameList, *opts, &dti4D);
    return ret;
}// singleDICOM()

size_t fileBytes(const char * fname) {
    FILE *fp = fopen(fname, "rb");
	if (!fp)  return 0;
	fseek(fp, 0, SEEK_END);
	size_t fileLen = ftell(fp);
    fclose(fp);
    return fileLen;
} //fileBytes()

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
        } else {
        	if (fileBytes(filename) > 2048)
            	convert_foreign (filename, *opts);
        	#ifdef MY_DEBUG
            	printMessage("Not a dicom:\t%s\n", filename);
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
        printMessage("%d images have identical time, series, acquisition and image values. DUPLICATES REMOVED.\n", nDuplicates);
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
        printMessage("%d images have identical time, series, acquisition and image values. Duplicates removed.\n", nDuplicates);
    return nConvert - nDuplicates;
}// removeDuplicatesVerbose()

int convert_parRec(struct TDCMopts opts) {
    //sample dataset from Ed Gronenschild <ed.gronenschild@maastrichtuniversity.nl>
    struct TSearchList nameList;
    int ret = EXIT_FAILURE;
    nameList.numItems = 1;
    nameList.maxItems = 1;
    nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //we reserve one pointer (32 or 64 bits) per potential file
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nameList.numItems * sizeof(struct  TDICOMdata));
    nameList.str[0]  = (char *)malloc(strlen(opts.indir)+1);
    strcpy(nameList.str[0],opts.indir);
    TDTI4D dti4D;
    dcmList[0] = nii_readParRec(nameList.str[0], opts.isVerbose, &dti4D, false);
    struct TDCMsort dcmSort[1];
    dcmSort[0].indx = 0;
    if (dcmList[0].isValid)
    	ret = saveDcm2Nii(1, dcmSort, dcmList, &nameList, opts, &dti4D);
    free(dcmList);//if (nConvertTotal == 0)
    if (nameList.numItems < 1)
    	printMessage("No valid PAR/REC files were found\n");
    if (nameList.numItems > 0)
        for (int i = 0; i < (int)nameList.numItems; i++)
            free(nameList.str[i]);
    free(nameList.str);
    return ret;
}// convert_parRec()

void freeNameList(struct TSearchList nameList) {
    if (nameList.numItems > 0) {
        unsigned long n = nameList.numItems;
        if (n > nameList.maxItems) n = nameList.maxItems; //assigned if (nameList->numItems < nameList->maxItems)
        for (unsigned long i = 0; i < n; i++)
            free(nameList.str[i]);
    }
    free(nameList.str);
}

int nii_loadDir(struct TDCMopts* opts) {
    //Identifies all the DICOM files in a folder and its subfolders
    if (strlen(opts->indir) < 1) {
        printMessage("No input\n");
        return EXIT_FAILURE;
    }
    char indir[512];
    strcpy(indir,opts->indir);
    bool isFile = is_fileNotDir(opts->indir);
    //bool isParRec = (isFile && ( (isExt(indir, ".par")) || (isExt(indir, ".rec"))) );
    if (isFile) //if user passes ~/dicom/mr1.dcm we will look at all files in ~/dicom
        dropFilenameFromPath(opts->indir);//getParentFolder(opts.indir, opts.indir);
    dropTrailingFileSep(opts->indir);
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
		return EXIT_FAILURE;
		#endif
    }
    /*if (isFile && ((isExt(indir, ".gz")) || (isExt(indir, ".tgz"))) ) {
        #ifndef myDisableTarGz
         #ifndef myDisableZLib
          untargz( indir, opts->outdir);
         #endif
        #endif
    }*/
    getFileName(opts->indirParent, opts->indir);
    if (isFile && ( (isExt(indir, ".v"))) )
		return convert_foreign (indir, *opts);
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
            strcpy(opts->indir, pname); //set to original file name, not path
            return convert_parRec(*opts);
        };
    }
    if ((isFile) && (opts->isOnlySingleFile))
        return singleDICOM(opts, indir);
    struct TSearchList nameList;
	nameList.maxItems = 24000; // larger requires more memory, smaller more passes
    //1: find filenames of dicom files: up to two passes if we found more files than we allocated memory
    for (int i = 0; i < 2; i++ ) {
        nameList.str = (char **) malloc((nameList.maxItems+1) * sizeof(char *)); //reserve one pointer (32 or 64 bits) per potential file
        nameList.numItems = 0;
        searchDirForDICOM(opts->indir, &nameList, opts->dirSearchDepth, 0, opts);
        if (nameList.numItems <= nameList.maxItems)
            break;
        freeNameList(nameList);
        nameList.maxItems = nameList.numItems+1;
        //printMessage("Second pass required, found %ld images\n", nameList.numItems);
    }
    if (nameList.numItems < 1) {
        if (opts->dirSearchDepth > 0)
        	printError("Unable to find any DICOM images in %s (or subfolders %d deep)\n", opts->indir, opts->dirSearchDepth);
        else //keep silent for dirSearchDepth = 0 - presumably searching multiple folders
        	; //printError("Unable to find any DICOM images in %s%s\n", opts->indir, str);
        free(nameList.str); //ignore compile warning - memory only freed on first of 2 passes
        return kEXIT_NO_VALID_FILES_FOUND;
    }
    size_t nDcm = nameList.numItems;
    printMessage( "Found %lu DICOM file(s)\n", nameList.numItems); //includes images and other non-image DICOMs
    // struct TDICOMdata dcmList [nameList.numItems]; //<- this exhausts the stack for large arrays
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nameList.numItems * sizeof(struct  TDICOMdata));
    struct TDTI4D dti4D;
    int nConvertTotal = 0;
    bool compressionWarning = false;
    bool convertError = false;
    for (int i = 0; i < (int)nDcm; i++ ) {
    	if ((isExt(nameList.str[i], ".par")) && (isDICOMfile(nameList.str[i]) < 1)) {
			strcpy(opts->indir, nameList.str[i]); //set to original file name, not path
            dcmList[i].converted2NII = 1;
            int ret = convert_parRec(*opts);
            if (ret == EXIT_SUCCESS)
            	nConvertTotal++;
            else
            	convertError = true;
            continue;
    	}
        dcmList[i] = readDICOMv(nameList.str[i], opts->isVerbose, opts->compressFlag, &dti4D); //ignore compile warning - memory only freed on first of 2 passes
        //~ if ((dcmList[i].isValid) &&((dcmList[i].totalSlicesIn4DOrder != NULL) ||(dcmList[i].patientPositionNumPhilips > 1) || (dcmList[i].CSA.numDti > 1))) { //4D dataset: dti4D arrays require huge amounts of RAM - write this immediately
        if ((dcmList[i].isValid) &&((dti4D.sliceOrder[0] >= 0) || (dcmList[i].CSA.numDti > 1))) { //4D dataset: dti4D arrays require huge amounts of RAM - write this immediately
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
    }
#ifdef HAVE_R
    if (opts->isScanOnly) {
        TWarnings warnings = setWarnings();
        // Create the first series from the first DICOM file
        TDicomSeries firstSeries;
        firstSeries.representativeData = dcmList[0];
        firstSeries.files.push_back(nameList.str[0]);
        opts->series.push_back(firstSeries);
        // Iterate over the remaining files
        for (size_t i = 1; i < nDcm; i++) {
            bool matched = false;
            // If the file matches an existing series, add it to the corresponding file list
            for (int j = 0; j < opts->series.size(); j++) {
                bool isMultiEchoUnused;
                if (isSameSet(opts->series[j].representativeData, dcmList[i], opts, &warnings, &isMultiEchoUnused)) {
                    opts->series[j].files.push_back(nameList.str[i]);
                    matched = true;
                    break;
                }
            }
            // If not, create a new series object
            if (!matched) {
                TDicomSeries nextSeries;
                nextSeries.representativeData = dcmList[i];
                nextSeries.files.push_back(nameList.str[i]);
                opts->series.push_back(nextSeries);
            }
        }
        // To avoid a spurious warning below
        nConvertTotal = nDcm;
    } else {
#endif
    //3: stack DICOMs with the same Series
    for (int i = 0; i < (int)nDcm; i++ ) {
		if ((dcmList[i].converted2NII == 0) && (dcmList[i].isValid)) {
			int nConvert = 0;
			struct TWarnings warnings = setWarnings();
			bool isMultiEcho = false;
			for (int j = i; j < (int)nDcm; j++)
				if (isSameSet(dcmList[i], dcmList[j], opts, &warnings, &isMultiEcho ) )
					nConvert++;
			if (nConvert < 1) nConvert = 1; //prevents compiler warning for next line: never executed since j=i always causes nConvert ++
			TDCMsort * dcmSort = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
			nConvert = 0;
			isMultiEcho = false;
			for (int j = i; j < (int)nDcm; j++)
				if (isSameSet(dcmList[i], dcmList[j], opts, &warnings, &isMultiEcho)) {
                    dcmList[j].converted2NII = 1; //do not reprocess repeats
                    fillTDCMsort(dcmSort[nConvert], j, dcmList[j]);
					nConvert++;
				} else {
					dcmList[i].isMultiEcho = isMultiEcho;
					dcmList[j].isMultiEcho = isMultiEcho;
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
#ifdef HAVE_R
    }
#endif
    free(dcmList);
    freeNameList(nameList);
    if (convertError)
        return EXIT_FAILURE; //at least one image failed to convert
    if (nConvertTotal == 0) {
        printMessage("No valid DICOM images were found\n"); //we may have found valid DICOM files but they are not DICOM images
        return kEXIT_NO_VALID_FILES_FOUND;
    }
    return EXIT_SUCCESS;
}// nii_loadDir()

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

#if defined(_WIN64) || defined(_WIN32)
#else //UNIX

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

void readFindPigz (struct TDCMopts *opts, const char * argv[]) {
    #if defined(_WIN64) || defined(_WIN32)
    strcpy(opts->pigzname,"pigz.exe");
    if (!is_exe(opts->pigzname)) {
      #if defined(__APPLE__)
        #ifdef myDisableZLib
        printMessage("Compression requires %s in the same folder as the executable http://macappstore.org/pigz/\n",opts->pigzname);
		#else //myUseZLib
 		printMessage("Compression will be faster with %s in the same folder as the executable http://macappstore.org/pigz/\n",opts->pigzname);
		#endif
        strcpy(opts->pigzname,"");
      #else
        #ifdef myDisableZLib
        printMessage("Compression requires %s in the same folder as the executable\n",opts->pigzname);
		#else //myUseZLib
 		printMessage("Compression will be faster with %s in the same folder as the executable\n",opts->pigzname);
		#endif
        strcpy(opts->pigzname,"");
       #endif
    } else
    	strcpy(opts->pigzname,".\\pigz"); //drop
    #else
    char str[1024];
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
    char exepth[1024];
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
    	printMessage("Compression will be faster with 'pigz' installed http://macappstore.org/pigz/\n");
      #endif
	#else //if APPLE else ...
	  #ifdef myDisableZLib
    	printMessage("Compression requires 'pigz' to be installed\n");
      #else //myUseZLib
    	printMessage("Compression will be faster with 'pigz' installed\n");
      #endif
    #endif
	return;
  	pigzFound: //Success
  	strcpy(opts->pigzname,str);
	//printMessage("Found pigz: %s\n", str);
    #endif
} //readFindPigz()

void setDefaultOpts (struct TDCMopts *opts, const char * argv[]) { //either "setDefaultOpts(opts,NULL)" or "setDefaultOpts(opts,argv)" where argv[0] is path to search
    strcpy(opts->pigzname,"");
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
    //printMessage("%d %s\n",opts->compressFlag, opts->compressname);
    strcpy(opts->indir,"");
    strcpy(opts->outdir,"");
    strcpy(opts->imageComments,"");
    opts->isOnlySingleFile = false; //convert all files in a directory, not just a single file
    opts->isForceStackSameSeries = false;
    opts->isIgnoreDerivedAnd2D = false;
    opts->isPhilipsFloatNotDisplayScaling = true;
    opts->isCrop = false;
    opts->isGz = false;
    opts->isSave3D = false;
    opts->dirSearchDepth = 5;
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
