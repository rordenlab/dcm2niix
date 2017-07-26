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
}// is_dir()

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
	else
		printMessage("Impossible GE slice orientation!");
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
    fprintf(fp, "%s\tField Strength:\t%g\tProtocolName:\t%s\tScanningSequence00180020:\t%s\tTE:\t%g\tTR:\t%g\tSeriesNum:\t%ld\tAcquNum:\t%d\tImageNum:\t%d\tImageComments:\t%s\tDateTime:\t%f\tName:\t%s\tConvVers:\t%s\tDoB:\t%s\tGender:\t%s\tAge:\t%s\tDimXYZT:\t%d\t%d\t%d\t%d\tCoil:\t%d\tEchoNum:\t%d\tOrient(6)\t%g\t%g\t%g\t%g\t%g\t%g\tbitsAllocated\t%d\tInputName\t%s\n",
      pathoutname, d.fieldStrength, d.protocolName, d.scanningSequence, d.TE, d.TR, d.seriesNum, d.acquNum, d.imageNum, d.imageComments,
      d.dateTime, d.patientName, kDCMvers, d.birthDate, d.gender, d.age, h->dim[1], h->dim[2], h->dim[3], h->dim[4],
            d.coilNum,d.echoNum, d.orient[1], d.orient[2], d.orient[3], d.orient[4], d.orient[5], d.orient[6],
            d.bitsAllocated, dcmname);
    fclose(fp);
}// nii_SaveText()

bool isDerived(struct TDICOMdata d) {
	#define kDerivedStr "DERIVED"
	if ((strlen(d.imageType) < strlen(kDerivedStr)) || (strstr(d.imageType, kDerivedStr) == NULL))
		return false;
	else
		return true;
}

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
	register char *cur, *last;
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

int siemensEchoEPIFactor(const char * filename,  int csaOffset, int csaLength, int* echoSpacing, int* echoTrainDuration) {
 //reads ASCII portion of CSASeriesHeaderInfo and returns lEchoTrainDuration or lEchoSpacing value
 // returns 0 if no value found
 	*echoSpacing = 0;
 	*echoTrainDuration = 0;
 	if ((csaOffset < 0) || (csaLength < 8)) return 0;
	FILE * pFile = fopen ( filename, "rb" );
	if(pFile==NULL) return 0;
	fseek (pFile , 0 , SEEK_END);
	long lSize = ftell (pFile);
	if (lSize < (csaOffset+csaLength)) {
		fclose (pFile);
		return 0;
	}
	fseek(pFile, csaOffset, SEEK_SET);
	char * buffer = (char*) malloc (csaLength);
	if(buffer == NULL) return 0;
	size_t result = fread (buffer,1,csaLength,pFile);
	if(result != csaLength) return 0;
	//next bit complicated: restrict to ASCII portion to avoid buffer overflow errors in BINARY portion
	int startAscii = phoenixOffsetCSASeriesHeader((unsigned char *)buffer, csaLength);
	int csaLengthTrim = csaLength;
	char * bufferTrim = buffer;
	if ((startAscii > 0) && (startAscii < csaLengthTrim) ){ //ignore binary data at start
		bufferTrim += startAscii;
		csaLengthTrim -= startAscii;
	}
	int ret = 0;
	char keyStr[] = "### ASCCONV BEGIN"; //skip to start of ASCII often "### ASCCONV BEGIN ###" but also "### ASCCONV BEGIN object=MrProtDataImpl@MrProtocolData"
	char *keyPos = (char *)memmem(bufferTrim, csaLengthTrim, keyStr, strlen(keyStr));
	if (keyPos) {
		csaLengthTrim -= (keyPos-bufferTrim);
		char keyStrEnd[] = "### ASCCONV END";
		char *keyPosEnd = (char *)memmem(keyPos, csaLengthTrim, keyStrEnd, strlen(keyStrEnd));
		if ((keyPosEnd) && ((keyPosEnd - keyPos) < csaLengthTrim)) //ignore binary data at end
			csaLengthTrim = (int)(keyPosEnd - keyPos);
		char keyStrES[] = "sFastImaging.lEchoSpacing";
		*echoSpacing  = readKey(keyStrES, keyPos, csaLengthTrim);
		char keyStrETD[] = "sFastImaging.lEchoTrainDuration";
		*echoTrainDuration = readKey(keyStrETD, keyPos, csaLengthTrim);
		char keyStrEF[] = "sFastImaging.lEPIFactor";
		ret = readKey(keyStrEF, keyPos, csaLengthTrim);
	}
	fclose (pFile);
	free (buffer);
	return ret;
}

#endif //myReadAsciiCsa

void nii_SaveBIDS(char pathoutname[], struct TDICOMdata d, struct TDCMopts opts, struct TDTI4D *dti4D, struct nifti_1_header *h, const char * filename) {
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
	if (!opts.isAnonymizeBIDS) {
		if (strlen(d.seriesInstanceUID) > 0)
			fprintf(fp, "\t\"SeriesInstanceUID\": \"%s\",\n", d.seriesInstanceUID );
		if (strlen(d.studyInstanceUID) > 0)
			fprintf(fp, "\t\"StudyInstanceUID\": \"%s\",\n", d.studyInstanceUID );
		if (strlen(d.referringPhysicianName) > 0)
			fprintf(fp, "\t\"ReferringPhysicianName\": \"%s\",\n", d.referringPhysicianName );
		if (strlen(d.studyID) > 0)
			fprintf(fp, "\t\"StudyID\": \"%s\",\n", d.studyID );
		//Next lines directly reveal patient identity
		//if (strlen(d.patientName) > 0)
		//	fprintf(fp, "\t\"PatientName\": \"%s\",\n", d.patientName );
		//if (strlen(d.patientID) > 0)
		//	fprintf(fp, "\t\"PatientID\": \"%s\",\n", d.patientID );
	}
	#ifdef myReadAsciiCsa
	if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (d.CSA.SeriesHeader_offset > 0) && (d.CSA.SeriesHeader_length > 0) &&
	    (strlen(d.scanningSequence) > 1) && (d.scanningSequence[0] == 'E') && (d.scanningSequence[1] == 'P')) { //for EPI scans only
		int echoSpacing, echoTrainDuration, epiFactor;
		epiFactor = siemensEchoEPIFactor(filename,  d.CSA.SeriesHeader_offset, d.CSA.SeriesHeader_length, &echoSpacing, &echoTrainDuration);
		//printMessage("ES %d ETD %d EPI %d\n", echoSpacing, echoTrainDuration, epiFactor);
		if (echoSpacing > 0)
			 fprintf(fp, "\t\"EchoSpacing\": %g,\n", echoSpacing / 1000000.0); //usec -> sec
		if (echoTrainDuration > 0)
			 fprintf(fp, "\t\"EchoTrainDuration\": %g,\n", echoTrainDuration / 1000000.0); //usec -> sec
		if (epiFactor > 0)
			 fprintf(fp, "\t\"EPIFactor\": %d,\n", epiFactor);
	}
	#endif
	if (d.echoTrainLength > 1) //>1 as for Siemens EPI this is 1, Siemens uses EPI factor http://mriquestions.com/echo-planar-imaging.html
		fprintf(fp, "\t\"EchoTrainLength\": %d,\n", d.echoTrainLength);
	if (d.echoNum > 1)
		fprintf(fp, "\t\"EchoNumber\": %d,\n", d.echoNum);
	if (d.isNonImage) //DICOM is derived image or non-spatial file (sounds, etc)
		fprintf(fp, "\t\"RawImage\": false,\n");
	if (d.acquNum > 0)
		fprintf(fp, "\t\"AcquisitionNumber\": %d,\n", d.acquNum);
	if (strlen(d.institutionName) > 0)
		fprintf(fp, "\t\"InstitutionName\": \"%s\",\n", d.institutionName );
	if (strlen(d.institutionAddress) > 0)
		fprintf(fp, "\t\"InstitutionAddress\": \"%s\",\n", d.institutionAddress );
	if (strlen(d.deviceSerialNumber) > 0)
		fprintf(fp, "\t\"DeviceSerialNumber\": \"%s\",\n", d.deviceSerialNumber );
	if (strlen(d.softwareVersions) > 0)
		fprintf(fp, "\t\"SoftwareVersions\": \"%s\",\n", d.softwareVersions );
	if (strlen(d.procedureStepDescription) > 0)
		fprintf(fp, "\t\"ProcedureStepDescription\": \"%s\",\n", d.procedureStepDescription );
	if (strlen(d.scanningSequence) > 0)
		fprintf(fp, "\t\"ScanningSequence\": \"%s\",\n", d.scanningSequence );
	if (strlen(d.sequenceVariant) > 0)
		fprintf(fp, "\t\"SequenceVariant\": \"%s\",\n", d.sequenceVariant );
	if (strlen(d.seriesDescription) > 0)
		fprintf(fp, "\t\"SeriesDescription\": \"%s\",\n", d.seriesDescription );
	if (strlen(d.bodyPartExamined) > 0)
		fprintf(fp, "\t\"BodyPartExamined\": \"%s\",\n", d.bodyPartExamined );
	if (strlen(d.protocolName) > 0)
		fprintf(fp, "\t\"ProtocolName\": \"%s\",\n", d.protocolName );
	if (strlen(d.sequenceName) > 0)
		fprintf(fp, "\t\"SequenceName\": \"%s\",\n", d.sequenceName );
	if (strlen(d.imageType) > 0) {
		fprintf(fp, "\t\"ImageType\": [\"");
		bool isSep = false;
		for (int i = 0; i < strlen(d.imageType); i++) {
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
	//if conditionals: the following values are required for DICOM MRI, but not available for CT
	if ((d.intenScalePhilips != 0) || (d.manufacturer == kMANUFACTURER_PHILIPS)) { //for details, see PhilipsPrecise()
		fprintf(fp, "\t\"PhilipsRescaleSlope\": %g,\n", d.intenScale );
		fprintf(fp, "\t\"PhilipsRescaleIntercept\": %g,\n", d.intenIntercept );
		fprintf(fp, "\t\"PhilipsScaleSlope\": %g,\n", d.intenScalePhilips );
		fprintf(fp, "\t\"UsePhilipsFloatNotDisplayScaling\": %d,\n", opts.isPhilipsFloatNotDisplayScaling);
	}
	//PET ISOTOPE MODULE ATTRIBUTES
	if (d.radionuclidePositronFraction > 0.0) fprintf(fp, "\t\"RadionuclidePositronFraction\": %g,\n", d.radionuclidePositronFraction );
	if (d.radionuclideTotalDose > 0.0) fprintf(fp, "\t\"RadionuclideTotalDose\": %g,\n", d.radionuclideTotalDose );
	if (d.radionuclideHalfLife > 0.0) fprintf(fp, "\t\"RadionuclideHalfLife\": %g,\n", d.radionuclideHalfLife );
	if (d.doseCalibrationFactor > 0.0) fprintf(fp, "\t\"DoseCalibrationFactor\": %g,\n", d.doseCalibrationFactor );
	//MRI parameters
	if (d.fieldStrength > 0.0) fprintf(fp, "\t\"MagneticFieldStrength\": %g,\n", d.fieldStrength );
	if (d.flipAngle > 0.0) fprintf(fp, "\t\"FlipAngle\": %g,\n", d.flipAngle );
	if ((d.TE > 0.0) && (!d.isXRay)) fprintf(fp, "\t\"EchoTime\": %g,\n", d.TE / 1000.0 );
    if ((d.TE > 0.0) && (d.isXRay)) fprintf(fp, "\t\"XRayExposure\": %g,\n", d.TE );
    if (d.TR > 0.0) fprintf(fp, "\t\"RepetitionTime\": %g,\n", d.TR / 1000.0 );
    if (d.TI > 0.0) fprintf(fp, "\t\"InversionTime\": %g,\n", d.TI / 1000.0 );
    if (d.ecat_isotope_halflife > 0.0) fprintf(fp, "\t\"IsotopeHalfLife\": %g,\n", d.ecat_isotope_halflife);
    if (d.ecat_dosage > 0.0) fprintf(fp, "\t\"Dosage\": %g,\n", d.ecat_dosage);
    double bandwidthPerPixelPhaseEncode = d.bandwidthPerPixelPhaseEncode;
    int phaseEncodingLines = d.phaseEncodingLines;
    if ((phaseEncodingLines == 0) &&  (h->dim[2] > 0) && (h->dim[1] > 0)) {
		if  (h->dim[2] == h->dim[2]) //phase encoding does not matter
			phaseEncodingLines = h->dim[2];
		else if (d.phaseEncodingRC =='R')
			phaseEncodingLines = h->dim[2];
		else if (d.phaseEncodingRC =='C')
			phaseEncodingLines = h->dim[1];
    }
    if (bandwidthPerPixelPhaseEncode == 0.0)
    	bandwidthPerPixelPhaseEncode = 	d.CSA.bandwidthPerPixelPhaseEncode;
    if (phaseEncodingLines > 0.0) fprintf(fp, "\t\"PhaseEncodingLines\": %d,\n", phaseEncodingLines );
    if (bandwidthPerPixelPhaseEncode > 0.0)
    	fprintf(fp, "\t\"BandwidthPerPixelPhaseEncode\": %g,\n", bandwidthPerPixelPhaseEncode );
    double effectiveEchoSpacing = 0.0;
    if ((phaseEncodingLines > 0) && (bandwidthPerPixelPhaseEncode > 0.0))
    	effectiveEchoSpacing = 1.0 / (bandwidthPerPixelPhaseEncode * phaseEncodingLines) ;
    if (effectiveEchoSpacing > 0.0)
    		fprintf(fp, "\t\"EffectiveEchoSpacing\": %g,\n", effectiveEchoSpacing);
    //FSL definition is start of first line until start of last line, so n-1 unless accelerated in-plane acquisition
    // to check: partial Fourier, iPAT, etc.
	int fencePost = 1;
    if (d.accelFactPE > 1.0)
    	fencePost = (int)round(d.accelFactPE); //e.g. if 64 lines with iPAT=2, we want time from start of first until start of 62nd effective line
    if ((d.phaseEncodingSteps > 1) && (effectiveEchoSpacing > 0.0))
		fprintf(fp, "\t\"TotalReadoutTime\": %g,\n", effectiveEchoSpacing * ((float)d.phaseEncodingSteps - fencePost));
    if (d.accelFactPE > 1.0) {
    		fprintf(fp, "\t\"AccelFactPE\": %g,\n", d.accelFactPE);
    		if (effectiveEchoSpacing > 0.0)
    			fprintf(fp, "\t\"TrueEchoSpacing\": %g,\n", effectiveEchoSpacing * d.accelFactPE);
	}
	if (d.CSA.sliceTiming[0] >= 0.0) {
   		fprintf(fp, "\t\"SliceTiming\": [\n");
   		for (int i = 0; i < kMaxEPI3D; i++) {
   			if (d.CSA.sliceTiming[i] < 0.0) break;
			if (i != 0)
				fprintf(fp, ",\n");
			fprintf(fp, "\t\t%g", d.CSA.sliceTiming[i] / 1000.0 );
		}
		fprintf(fp, "\t],\n");
	}
	if (((d.phaseEncodingRC == 'R') || (d.phaseEncodingRC == 'C')) && ((d.CSA.phaseEncodingDirectionPositive == 1) || (d.CSA.phaseEncodingDirectionPositive == 0))) {
		if (d.phaseEncodingRC == 'C') //Values should be "R"ow, "C"olumn or "?"Unknown
			fprintf(fp, "\t\"PhaseEncodingDirection\": \"j");
		else if (d.phaseEncodingRC == 'R')
				fprintf(fp, "\t\"PhaseEncodingDirection\": \"i");
		else
			fprintf(fp, "\t\"PhaseEncodingDirection\": \"?");
		//phaseEncodingDirectionPositive has one of three values: UNKNOWN (-1), NEGATIVE (0), POSITIVE (1)
		//However, DICOM and NIfTI are reversed in the j (ROW) direction
		//Equivalent to dicm2nii's "if flp(iPhase), phPos = ~phPos; end"
		if (d.CSA.phaseEncodingDirectionPositive == -1)
			fprintf(fp, "?"); //unknown
		else if ((d.CSA.phaseEncodingDirectionPositive == 1) && ((opts.isFlipY)))
			fprintf(fp, "-");
		else if ((d.CSA.phaseEncodingDirectionPositive == 0) && ((!opts.isFlipY)))
			fprintf(fp, "-");
		fprintf(fp, "\",\n");
	} //only save PhaseEncodingDirection if BOTH direction and POLARITY are known
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

unsigned char * removeADC(struct nifti_1_header *hdr, unsigned char *inImg, bool * isADC) {
	int numVolOut = 0;
	int numVolIn = hdr->dim[4];
	int numVolBytes = hdr->dim[1]*hdr->dim[2]*hdr->dim[3]*(hdr->bitpix/8);
	if ((!isADC) || (numVolIn < 1) || (numVolBytes < 1)) return inImg;
	for (int i = 0; i < numVolIn; i++)
		if (!isADC[i])
			numVolOut++;
	if (numVolOut < 1) return inImg;
	//printMessage("Removing ADC maps, %d volumes reduced to %d\n", numVolIn, numVolOut);
	unsigned char *outImg = (unsigned char *)malloc(numVolBytes * numVolOut);
	int outPos = 0;
	for (int i = 0; i < numVolIn; i++) {
		if (!isADC[i]) {
			memcpy(&outImg[outPos], &inImg[i * numVolBytes], numVolBytes); // dest, src, bytes
            outPos += numVolBytes;
		}
	} //for each volume
	free(isADC);
	free(inImg);
	hdr->dim[4] = numVolOut;
	return outImg;
} //removeADC()


bool * nii_SaveDTI(char pathoutname[],int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TDCMopts opts, int sliceDir, struct TDTI4D *dti4D) {
    //reports non-zero if any volumes should be excluded (e.g. philip stores an ADC maps)
    //to do: works with 3D mosaics and 4D files, must remove repeated volumes for 2D sequences....
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
        for (int i = 1; i < numDti; i++)
                printMessage("bxyz %g %g %g %g\n",vx[i].V[0],vx[i].V[1],vx[i].V[2],vx[i].V[3]);
        printWarning("No bvec/bval files created. Only one B-value reported for all volumes: %g\n",vx[0].V[0]);
        free(vx);
        return NULL;
    }
    int firstB0 = -1;
    for (int i = 0; i < numDti; i++) //check if all bvalues match first volume
        if (isSameFloat(vx[i].V[0],0) ) {
            firstB0 = i;
            break;
        }
    if (firstB0 < 0) printWarning("This diffusion series does not have a B0 (reference) volume\n");
    if (firstB0 > 0) printMessage("Note: B0 not the first volume in the series (FSL eddy reference volume is %d)\n", firstB0);
	int numADC = 0;
    bool * isADC = NULL;
    if (dcmList[dcmSort[0].indx].manufacturer == kMANUFACTURER_PHILIPS) {
    	isADC = (bool *)malloc(numDti * sizeof(bool));
    	int o = 0; //output index
        for (int i = 0; i < numDti; i++) {//check if all bvalues match first volume
        	if (isADCnotDTI(vx[i])) {
        	    isADC[i] = true;
            	numADC++;
            	printMessage("Volume %d is not a normal DTI image (ADC?)\n", i+1);
            } else { //save output
            	vx[o] = vx[i];
            	isADC[i] = false;
            	o++;
            }
        } //
        numDti = numDti - numADC;
        dcmList[indx0].CSA.numDti = numDti; //warning structure not changed outside scope!
        if (numADC > 0) {
            printMessage("Note: %d volumes appear to be ADC images that will be removed to allow processing\n", numADC);
        	if (numDti == 0) {
        		printWarning("No bvec/bval files created: no volumes with bvecs applied \n");
        		free(isADC);
        		free(vx);
        		return NULL;
        	}
        }
        if (numADC == 0) { //no ADC images
        	free(isADC);
        	isADC = NULL;
        }
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
        return isADC;
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
        return isADC;
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
    return isADC;
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
    int start = 0;
    int pos = 0;
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
            if (f == 'C')
                strcat (outname,dcm.imageComments);
            if (f == 'D')
                strcat (outname,dcm.seriesDescription);
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
            if (f == 'P')
                strcat (outname,dcm.protocolName);
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
    if (dcm.isHasPhase)
    	strcat (outname,"_ph"); //manufacturer name not available

    if (strlen(outname) < 1) strcpy(outname, "dcm2nii_invalidName");
    if (outname[0] == '.') outname[0] = '_'; //make sure not a hidden file
    //eliminate illegal characters http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx
    for (int pos = 0; pos<strlen(outname); pos ++)
        if ((outname[pos] == '<') || (outname[pos] == '>') || (outname[pos] == ':')
            || (outname[pos] == '"') || (outname[pos] == '\\') || (outname[pos] == '/')
            || (outname[pos] == '^')
            || (outname[pos] == '*') || (outname[pos] == '|') || (outname[pos] == '?'))
            outname[pos] = '_';
    //printMessage("outname=*%s* %d %d\n", outname, pos,start);
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
    int zLevel = Z_DEFAULT_COMPRESSION;
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
    hdr.vox_offset = 352;
    size_t imgsz = nii_ImgBytes(hdr);
    if (imgsz < 1) {
    	printMessage("Error: Image size is zero bytes %s\n", niiFilename);
    	return EXIT_FAILURE;
    }
    #ifndef myDisableZLib
    if  ((opts.isGz) &&  (strlen(opts.pigzname)  < 1) &&  ((imgsz+hdr.vox_offset) <  2147483647) ) { //use internal compressor
        writeNiiGz (niiFilename, hdr,  im, imgsz, opts.gzLevel);
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
        system(command);
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

void PhilipsPrecise (struct TDICOMdata * d, bool isPhilipsFloatNotDisplayScaling, struct nifti_1_header *h) {
	if ((d->intenScalePhilips == 0) || (d->manufacturer != kMANUFACTURER_PHILIPS)) return; //not Philips
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
	printMessage("Philips Precise RS:RI:SS = %g:%g:%g (see PMC3998685)\n",d->intenScale,d->intenIntercept,d->intenScalePhilips);
	printMessage(" R = raw value, P = precise value, D = displayed value\n");
	printMessage(" RS = rescale slope, RI = rescale intercept,  SS = scale slope\n");
	printMessage(" D = R * RS + RI    , P = D/(RS * SS)\n");
	printMessage(" D scl_slope:scl_inter = %g:%g\n", d->intenScale,d->intenIntercept);
	printMessage(" P scl_slope:scl_inter = %g:%g\n", intenScaleP,intenInterceptP);
	//#define myUsePhilipsPrecise
	if (isPhilipsFloatNotDisplayScaling) {
		printMessage(" Using P values ('-p n ' for D values)\n");
		//to change DICOM:
		//d->intenScale = intenScaleP;
		//d->intenIntercept = intenInterceptP;
		//to change NIfTI
    	h->scl_slope = intenScaleP;
    	h->scl_inter = intenInterceptP;
    	d->intenScalePhilips = 0; //so we never run this TWICE!
	} else
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
    int nVox2D = hdr.dim[1]*hdr.dim[2];
    if ((nVox2D < 1) || (fabs(hdr.pixdim[3]) < 0.001) || (hdr.dim[0] != 3) || (hdr.dim[3] < 128)) return EXIT_FAILURE;
    if ((hdr.datatype != DT_INT16) && (hdr.datatype != DT_UINT16)) {
        printMessage("Only able to crop 16-bit volumes.");
        return EXIT_FAILURE;
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

int saveDcm2Nii(int nConvert, struct TDCMsort dcmSort[],struct TDICOMdata dcmList[], struct TSearchList *nameList, struct TDCMopts opts, struct TDTI4D *dti4D) {
    bool iVaries = intensityScaleVaries(nConvert,dcmSort,dcmList);
    float *sliceMMarray = NULL; //only used if slices are not equidistant
    uint64_t indx = dcmSort[0].indx;
    uint64_t indx0 = dcmSort[0].indx;
    if (opts.isIgnoreDerivedAnd2D && isDerived(dcmList[indx])) {
    	printMessage("Ignoring derived image(s) of series %ld %s\n", dcmList[indx].seriesNum,  nameList->str[indx]);
    	return EXIT_SUCCESS;
    }
    if ((opts.isIgnoreDerivedAnd2D) && (strcmp(dcmList[indx].sequenceName, "_fl2d1")== 0)) {
    	printMessage("Ignoring localizer (sequence %s) of series %ld %s\n", dcmList[indx].sequenceName, dcmList[indx].seriesNum,  nameList->str[indx]);
    	return EXIT_SUCCESS;
    }
    if ((opts.isIgnoreDerivedAnd2D) && (nConvert < 2) && (dcmList[indx].xyzDim[3] < 2)) {
    	printMessage("Ignoring 2D image of series %ld %s\n", dcmList[indx].seriesNum,  nameList->str[indx]);
    	return EXIT_SUCCESS;
    }
    bool saveAs3D = dcmList[indx].isHasPhase;
    struct nifti_1_header hdr0;
    unsigned char * img = nii_loadImgXL(nameList->str[indx], &hdr0,dcmList[indx], iVaries, opts.compressFlag, opts.isVerbose);
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
                if (nAcq > 1) {
                    printMessage("Slice positions repeated, but number of slices (%d) not divisible by number of repeats (%d): missing images?\n", nConvert, nAcq);
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
        for (int i = 1; i < nConvert; i++) { //stack additional images
            indx = dcmSort[i].indx;
            //if (headerDcm2Nii(dcmList[indx], &hdrI) == EXIT_FAILURE) return EXIT_FAILURE;
            img = nii_loadImgXL(nameList->str[indx], &hdrI, dcmList[indx],iVaries, opts.compressFlag, opts.isVerbose);
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
    if (hdr0.dim[3] > 1)sliceDir = headerDcm2Nii2(dcmList[dcmSort[0].indx],dcmList[dcmSort[nConvert-1].indx] , &hdr0);
	//UNCOMMENT NEXT TWO LINES TO RE-ORDER MOSAIC WHERE CSA's protocolSliceNumber does not start with 1
	if (dcmList[dcmSort[0].indx].CSA.protocolSliceNumber1 > 1) {
		printWarning("Weird CSA 'ProtocolSliceNumber' (%d): SPATIAL, SLICE-ORDER AND DTI TRANSFORMS UNTESTED\n", dcmList[dcmSort[0].indx].CSA.protocolSliceNumber1);
		//see https://github.com/neurolabusc/dcm2niix/issues/40
		sliceDir = -1; //not sure how to handle negative determinants?
	}
	if (sliceDir < 0) {
        imgM = nii_flipZ(imgM, &hdr0);
        sliceDir = abs(sliceDir); //change this, we have flipped the image so GE DTI bvecs no longer need to be flipped!
    }
    nii_SaveBIDS(pathoutname, dcmList[dcmSort[0].indx], opts, dti4D, &hdr0, nameList->str[dcmSort[0].indx]);
	nii_SaveText(pathoutname, dcmList[dcmSort[0].indx], opts, &hdr0, nameList->str[indx]);
    bool * isADC = nii_SaveDTI(pathoutname,nConvert, dcmSort, dcmList, opts, sliceDir, dti4D);
    if ((hdr0.datatype == DT_UINT16) &&  (!dcmList[dcmSort[0].indx].isSigned)) nii_check16bitUnsigned(imgM, &hdr0);
    printMessage( "Convert %d DICOM as %s (%dx%dx%dx%d)\n",  nConvert, pathoutname, hdr0.dim[1],hdr0.dim[2],hdr0.dim[3],hdr0.dim[4]);
    PhilipsPrecise(&dcmList[dcmSort[0].indx], opts.isPhilipsFloatNotDisplayScaling, &hdr0);

    if (!dcmList[dcmSort[0].indx].isSlicesSpatiallySequentialPhilips)
    	nii_reorderSlices(imgM, &hdr0, dti4D);
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
        if (isADC) { //ADC maps can disrupt analysis: save a copy with the ADC map, and another without
#ifndef HAVE_R
            char pathoutnameADC[2048] = {""};
            strcat(pathoutnameADC,pathoutname);
            strcat(pathoutnameADC,"_ADC");
            nii_saveNII(pathoutnameADC, hdr0, imgM, opts);
#endif
			imgM = removeADC(&hdr0, imgM, isADC);
            //hdr0.dim[4] = hdr0.dim[4]-numFinalADC;
        };
#ifndef HAVE_R
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
    return EXIT_SUCCESS;
}// saveDcm2Nii()

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
    //return (a == b); //niave approach does not have any tolerance for rounding errors
    return (fabs (a - b) <= 0.0001);
}

int isSameFloatDouble (double a, double b) {
    //Kludge for bug in 0002,0016="DIGITAL_JACKET", 0008,0070="GE MEDICAL SYSTEMS" DICOM data: Orient field (0020:0037) can vary 0.00604261 == 0.00604273 !!!
    // return (a == b); //niave approach does not have any tolerance for rounding errors
    return (fabs (a - b) <= 0.0001);
}

struct TWarnings { //generate a warning only once per set
        bool acqNumVaries, bitDepthVaries, dateTimeVaries, echoVaries, coilVaries, nameVaries, orientVaries;
};

TWarnings setWarnings() {
	TWarnings r;
	r.acqNumVaries = false;
	r.bitDepthVaries = false;
	r.dateTimeVaries = false;
	r.echoVaries = false;
	r.coilVaries = false;
	r.nameVaries = false;
	r.orientVaries = false;
	return r;
}

bool isSameSet (struct TDICOMdata d1, struct TDICOMdata d2, bool isForceStackSameSeries, struct TWarnings* warnings, bool *isMultiEcho) {
    //returns true if d1 and d2 should be stacked together as a single output
    *isMultiEcho = false;
    if (!d1.isValid) return false;
    if (!d2.isValid) return false;
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
    if (isForceStackSameSeries) {
    	if ((d1.TE != d2.TE) || (d1.echoNum != d2.echoNum))
    		*isMultiEcho = true;
    	return true; //we will stack these images, even if they differ in the following attributes
    }
    if (!isSameFloatDouble(d1.dateTime, d2.dateTime)) { //beware, some vendors incorrectly store Image Time (0008,0033) as Study Time (0008,0030).
    	if (!warnings->dateTimeVaries)
    		printMessage("slices not stacked: Study Data/Time (0008,0020 / 0008,0030) varies %12.12f ~= %12.12f\n", d1.dateTime, d2.dateTime);
    	warnings->dateTimeVaries = true;
    	return false;
    }
    if ((d1.TE != d2.TE) || (d1.echoNum != d2.echoNum)) {
        if ((!warnings->echoVaries) && (d1.isXRay)) //for CT/XRay we check DICOM tag 0018,1152 (XRayExposure)
        	printMessage("slices not stacked: X-Ray Exposure varies (%g, %g; number %d, %d)\n", d1.TE, d2.TE,d1.echoNum, d2.echoNum );
        if ((!warnings->echoVaries) && (!d1.isXRay)) //for MRI
        	printMessage("slices not stacked: echo varies (TE %g, %g; echo %d, %d)\n", d1.TE, d2.TE,d1.echoNum, d2.echoNum );
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
    if ((strcmp(d1.protocolName, d2.protocolName) != 0)) {
        if ((!warnings->nameVaries))
        	printMessage("slices not stacked: protocol name varies\n");
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
    dcmSort[0].indx = 0;
    dcmSort[0].img = ((uint64_t)dcmList[0].seriesNum << 32) + dcmList[0].imageNum;
    dcmList[0].converted2NII = 1;
    dcmList[0] = readDICOMv(nameList.str[0], opts->isVerbose, opts->compressFlag, &dti4D); //ignore compile warning - memory only freed on first of 2 passes
    return saveDcm2Nii(1, dcmSort, dcmList, &nameList, *opts, &dti4D);
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
        else if (isDICOMfile(filename) > 0) {
            if (nameList->numItems < nameList->maxItems) {
                nameList->str[nameList->numItems]  = (char *)malloc(strlen(filename)+1);
                strcpy(nameList->str[nameList->numItems],filename);
                //printMessage("OK\n");
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
        if (dcmSort[i].img == dcmSort[i-1].img) {
            nDuplicates ++;
        } else {
            dcmSort[i-nDuplicates].img = dcmSort[i].img;
            dcmSort[i-nDuplicates].indx = dcmSort[i].indx;
        }
    }
    if (nDuplicates > 0)
        printMessage("Some images have identical time, series, acquisition and image values. DUPLICATES REMOVED.\n");
    return nConvert - nDuplicates;
}// removeDuplicates()

int removeDuplicatesVerbose(int nConvert, struct TDCMsort dcmSort[], struct TSearchList *nameList){
    //done AFTER sorting, so duplicates will be sequential
    if (nConvert < 2) return nConvert;
    int nDuplicates = 0;
    for (int i = 1; i < nConvert; i++) {
        if (dcmSort[i].img == dcmSort[i-1].img) {
            printMessage("\t%s\t=\t%s\n",nameList->str[dcmSort[i-1].indx],nameList->str[dcmSort[i].indx]);
            nDuplicates ++;
        } else {
            dcmSort[i-nDuplicates].img = dcmSort[i].img;
            dcmSort[i-nDuplicates].indx = dcmSort[i].indx;
        }
    }
    if (nDuplicates > 0)
        printMessage("Some images have identical time, series, acquisition and image values. Duplicates removed.\n");
    return nConvert - nDuplicates;
}// removeDuplicates()

bool isExt (char *file_name, const char* ext) {
    char *p_extension;
    if((p_extension = strrchr(file_name,'.')) != NULL )
        if(strcicmp(p_extension,ext) == 0) return true;
    //if(strcmp(p_extension,ext) == 0) return true;
    return false;
}// isExt()

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
    if (nameList.numItems < 1)
    	printMessage("No valid PAR/REC files were found\n");
    if (nameList.numItems > 0)
        for (int i = 0; i < nameList.numItems; i++)
            free(nameList.str[i]);
    free(nameList.str);

    return EXIT_SUCCESS;
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
        searchDirForDICOM(opts->indir, &nameList,  5, 1, opts);
        if (nameList.numItems <= nameList.maxItems)
            break;
        freeNameList(nameList);
        nameList.maxItems = nameList.numItems+1;
        //printMessage("Second pass required, found %ld images\n", nameList.numItems);
    }
    if (nameList.numItems < 1) {
        printError("Unable to find any DICOM images in %s\n", opts->indir);
        free(nameList.str); //ignore compile warning - memory only freed on first of 2 passes
        return EXIT_FAILURE;
    }
    size_t nDcm = nameList.numItems;
    printMessage( "Found %lu DICOM image(s)\n", nameList.numItems);
    // struct TDICOMdata dcmList [nameList.numItems]; //<- this exhausts the stack for large arrays
    struct TDICOMdata *dcmList  = (struct TDICOMdata *)malloc(nameList.numItems * sizeof(struct  TDICOMdata));
    struct TDTI4D dti4D;
    int nConvertTotal = 0;
    bool compressionWarning = false;
    for (int i = 0; i < nDcm; i++ ) {
        dcmList[i] = readDICOMv(nameList.str[i], opts->isVerbose, opts->compressFlag, &dti4D); //ignore compile warning - memory only freed on first of 2 passes
        if ((dcmList[i].isValid) &&((dcmList[i].patientPositionNumPhilips > 1) || (dcmList[i].CSA.numDti > 1))) { //4D dataset: dti4D arrays require huge amounts of RAM - write this immediately
            struct TDCMsort dcmSort[1];
            dcmSort[0].indx = i;
            dcmSort[0].img = ((uint64_t)dcmList[i].seriesNum << 32) + dcmList[i].imageNum;
            dcmList[i].converted2NII = 1;
            saveDcm2Nii(1, dcmSort, dcmList, &nameList, *opts, &dti4D);
            nConvertTotal++;
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
                if (isSameSet(opts->series[j].representativeData, dcmList[i], opts->isForceStackSameSeries, &warnings, &isMultiEchoUnused)) {
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
    for (int i = 0; i < nDcm; i++ ) {
		if ((dcmList[i].converted2NII == 0) && (dcmList[i].isValid)) {
			int nConvert = 0;
			struct TWarnings warnings = setWarnings();
			bool isMultiEcho;
			for (int j = i; j < nDcm; j++)
				if (isSameSet(dcmList[i], dcmList[j], opts->isForceStackSameSeries, &warnings, &isMultiEcho ) )
					nConvert++;
			if (nConvert < 1) nConvert = 1; //prevents compiler warning for next line: never executed since j=i always causes nConvert ++
			TDCMsort * dcmSort = (TDCMsort *)malloc(nConvert * sizeof(TDCMsort));
			nConvert = 0;
			for (int j = i; j < nDcm; j++)
				if (isSameSet(dcmList[i], dcmList[j], opts->isForceStackSameSeries, &warnings, &isMultiEcho)) {
					dcmSort[nConvert].indx = j;
					dcmSort[nConvert].img = ((uint64_t)dcmList[j].seriesNum << 32) + dcmList[j].imageNum;
					dcmList[j].converted2NII = 1;
					nConvert++;
				} else {
					dcmList[i].isMultiEcho = isMultiEcho;
					dcmList[j].isMultiEcho = isMultiEcho;
				}
			qsort(dcmSort, nConvert, sizeof(struct TDCMsort), compareTDCMsort); //sort based on series and image numbers....
			if (opts->isVerbose)
				nConvert = removeDuplicatesVerbose(nConvert, dcmSort, &nameList);
			else
				nConvert = removeDuplicates(nConvert, dcmSort);
			nConvertTotal += nConvert;
			saveDcm2Nii(nConvert, dcmSort, dcmList, &nameList, *opts, &dti4D);
			free(dcmSort);
		}//convert all images of this series
    }
#ifdef HAVE_R
    }
#endif
    free(dcmList);
    if (nConvertTotal == 0) {
        printMessage("No valid DICOM files were found\n");
    }
    freeNameList(nameList);
    //if (nameList.numItems > 0)
    //    for (int i = 0; i < nameList.numItems; i++)
    //        free(nameList.str[i]);
    //free(nameList.str);
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

void readFindPigz (struct TDCMopts *opts, const char * argv[]) {
    #if defined(_WIN64) || defined(_WIN32)
    strcpy(opts->pigzname,"pigz.exe");
    if (!is_exe(opts->pigzname)) {
        #ifdef myDisableZLib
        printMessage("Compression requires %s in the same folder as the executable\n",opts->pigzname);
		#else //myUseZLib
 		printMessage("Compression will be faster with %s in the same folder as the executable\n",opts->pigzname);
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
        if (argv == NULL) { //no exectuable path provided
			if (!is_exe(opts->pigzname))
				strcpy(opts->pigzname,"");
        	return;
        }
        if (!is_exe(opts->pigzname))  {
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
             	#ifdef myDisableZLib
                printMessage("Compression requires %s\n",pigz);
            	#else //myUseZLib
                printMessage("Compression will be faster with %s\n",pigz);
            	#endif
            	strcpy(opts->pigzname,"");
            } //no pigz_mricron in exe's folder
        } //no /usr/local/pigz_mricron
       }//no /usr/bin/pigz
    } //no /usr/local/pigz
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
    opts->isOnlySingleFile = false; //convert all files in a directory, not just a single file
    opts->isForceStackSameSeries = false;
    opts->isIgnoreDerivedAnd2D = false;
    opts->isPhilipsFloatNotDisplayScaling = true;
    opts->isCrop = false;
    opts->isGz = false;
    opts->gzLevel = -1;
    opts->isFlipY = true; //false: images in raw DICOM orientation, true: image rows flipped to cartesian coordinates
    opts->isRGBplanar = false; //false for NIfTI (RGBRGB...), true for Analyze (RRR..RGGG..GBBB..B)
    opts->isCreateBIDS =  true;
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
DWORD dwValue    = opts.isGz;
  //RegSetValueEx(hKey,"isGZ", 0, REG_DWORD,reinterpret_cast<BYTE *>(&dwValue),sizeof(dwValue));
  //RegSetValueExA(hKey, "isGZ", 0, REG_DWORD, (LPDWORD)&dwValue, sizeof(dwValue));
  RegSetValueExA(hKey, "isGZ", 0, REG_DWORD, reinterpret_cast<BYTE *>(&dwValue), sizeof(dwValue));
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
    fprintf(fp, "isGZ=%d\n", opts.isGz);
    fprintf(fp, "isBIDS=%d\n", opts.isCreateBIDS);
    fprintf(fp, "filename=%s\n", opts.filename);
    fclose(fp);
} //saveIniFile()

#endif
