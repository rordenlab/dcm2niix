//#define MY_DEBUG
#if defined(_WIN64) || defined(_WIN32)
	#include <windows.h> //write to registry
#endif
#ifdef _MSC_VER
	#include <direct.h>
	#define getcwd _getcwd
	#define chdir _chrdir
	#include "io.h"
	#include <math.h>
	//#define snprintf _snprintf
	//#define vsnprintf _vsnprintf
	#define strcasecmp _stricmp
	#define strncasecmp _strnicmp
#else
	#include <unistd.h>
#endif
//#include <time.h> //clock()
#ifndef USING_R
#include "nifti1.h"
#endif
#include "print.h"
#include "nii_dicom.h"
#include <sys/types.h>
#include <sys/stat.h> // discriminate files from folders
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h> //toupper
#include <math.h>
#include <string.h>
#include <stddef.h>
#include "jpg_0XC3.h"
#include <float.h>
#include <stdint.h>
#include "nifti1_io_core.h"

#ifdef USING_R
  #undef isnan
  #define isnan ISNAN
#endif

#ifndef myDisableClassicJPEG
  #ifdef myTurboJPEG
   #include <turbojpeg.h>
  #else
	#include "ujpeg.h"
  #endif
#endif
#ifdef myEnableJasper
    #include <jasper/jasper.h>
#endif
#ifndef myDisableOpenJPEG
    #include "openjpeg.h"

#ifdef myEnableJasper
  ERROR: YOU CAN NOT COMPILE WITH myEnableJasper AND NOT myDisableOpenJPEG OPTIONS SET SIMULTANEOUSLY
#endif

unsigned char * imagetoimg(opj_image_t * image)
{
    int numcmpts = image->numcomps;
    int sgnd = image->comps[0].sgnd ;
    int width = image->comps[0].w;
    int height = image->comps[0].h;
    int bpp = (image->comps[0].prec + 7) >> 3; //e.g. 12 bits requires 2 bytes
    int imgbytes = bpp * width * height * numcmpts;
    bool isOK = true;
    if (numcmpts > 1) {
        for (int comp = 1; comp < numcmpts; comp++) { //check RGB data
            if (image->comps[0].w != image->comps[comp].w) isOK = false;
            if (image->comps[0].h != image->comps[comp].h) isOK = false;
            if (image->comps[0].dx != image->comps[comp].dx) isOK = false;
            if (image->comps[0].dy != image->comps[comp].dy) isOK = false;
            if (image->comps[0].prec != image->comps[comp].prec) isOK = false;
            if (image->comps[0].sgnd != image->comps[comp].sgnd) isOK = false;
        }
        if (numcmpts != 3) isOK = false; //we only handle Gray and RedGreenBlue, not GrayAlpha or RedGreenBlueAlpha
        if (image->comps[0].prec != 8) isOK = false; //only 8-bit for RGB data
    }
    if ((image->comps[0].prec < 1) || (image->comps[0].prec > 16)) isOK = false; //currently we only handle 1 and 2 byte data
    if (!isOK) {
        printMessage("jpeg decode failure w*h %d*%d bpp %d sgnd %d components %d OpenJPEG=%s\n", width, height, bpp, sgnd, numcmpts,  opj_version());
        return NULL;
    }
    #ifdef MY_DEBUG
    printMessage("w*h %d*%d bpp %d sgnd %d components %d OpenJPEG=%s\n", width, height, bpp, sgnd, numcmpts,  opj_version());
    #endif
    //extract the data
    if ((bpp < 1) || (bpp > 2) || (width < 1) || (height < 1) || (imgbytes < 1)) {
        printError("Catastrophic decompression error\n");
        return NULL;
    }
    unsigned char *img = (unsigned char *)malloc(imgbytes);
    uint16_t * img16ui = (uint16_t*) img; //unsigned 16-bit
    int16_t * img16i = (int16_t*) img; //signed 16-bit
    if (sgnd) bpp = -bpp;
    if (bpp == -1) {
        free(img);
        printError("Signed 8-bit DICOM?\n");
        return NULL;
    }
    //n.b. Analyze rgb-24 are PLANAR e.g. RRR..RGGG..GBBB..B not RGBRGBRGB...RGB
    int pix = 0; //ouput pixel
    for (int cmptno = 0; cmptno < numcmpts; ++cmptno) {
        int cpix = 0; //component pixel
        int* v = image->comps[cmptno].data;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                switch (bpp) {
                    case 1:
                        img[pix] = (unsigned char) v[cpix];
                        break;
                    case 2:
                        img16ui[pix] = (uint16_t) v[cpix];
                        break;
                    case -2:
                        img16i[pix] = (int16_t) v[cpix];
                        break;
                }
                pix ++;
                cpix ++;
            }//for x
        } //for y
    } //for each component
    return img;
}// imagetoimg()

typedef struct bufinfo {
    unsigned char *buf;
    unsigned char *cur;
    size_t len;
} BufInfo;

static void my_stream_free (void * p_user_data) { //do nothing
    //BufInfo d = (BufInfo) p_user_data;
    //free(d.buf);
} // my_stream_free()

static OPJ_UINT32 opj_read_from_buffer(void * p_buffer, OPJ_UINT32 p_nb_bytes, BufInfo* p_file) {
    OPJ_UINT32 l_nb_read;
    if(p_file->cur + p_nb_bytes < p_file->buf + p_file->len )
    {
        l_nb_read = p_nb_bytes;
    }
    else
    {
        l_nb_read = (OPJ_UINT32)(p_file->buf + p_file->len - p_file->cur);
    }
    memcpy(p_buffer, p_file->cur, l_nb_read);
    p_file->cur += l_nb_read;

    return l_nb_read ? l_nb_read : ((OPJ_UINT32)-1);
} //opj_read_from_buffer()

static OPJ_UINT32 opj_write_from_buffer(void * p_buffer, OPJ_UINT32 p_nb_bytes, BufInfo* p_file) {
    memcpy(p_file->cur,p_buffer, p_nb_bytes);
    p_file->cur += p_nb_bytes;
    p_file->len += p_nb_bytes;
    return p_nb_bytes;
} // opj_write_from_buffer()

static OPJ_SIZE_T opj_skip_from_buffer(OPJ_SIZE_T p_nb_bytes, BufInfo * p_file) {
    if(p_file->cur + p_nb_bytes < p_file->buf + p_file->len )
    {
        p_file->cur += p_nb_bytes;
        return p_nb_bytes;
    }
    p_file->cur = p_file->buf + p_file->len;
    return (OPJ_SIZE_T)-1;
} //opj_skip_from_buffer()

//fix for https://github.com/neurolabusc/dcm_qa/issues/5
static OPJ_BOOL opj_seek_from_buffer(OPJ_SIZE_T p_nb_bytes, BufInfo * p_file) {
    //printf("opj_seek_from_buffer %d + %d -> %d + %d\n", p_file->cur , p_nb_bytes, p_file->buf, p_file->len);
    if (p_nb_bytes < p_file->len ) {
        p_file->cur = p_file->buf + p_nb_bytes;
        return OPJ_TRUE;
    }
    p_file->cur = p_file->buf + p_file->len;
    return OPJ_FALSE;
} //opj_seek_from_buffer()

/*static OPJ_BOOL opj_seek_from_buffer(OPJ_SIZE_T p_nb_bytes, BufInfo * p_file) {
    if((p_file->cur + p_nb_bytes) < (p_file->buf + p_file->len) ) {
        p_file->cur += p_nb_bytes;
        return OPJ_TRUE;
    }
    p_file->cur = p_file->buf + p_file->len;
    return OPJ_FALSE;
} //opj_seek_from_buffer()*/

opj_stream_t* opj_stream_create_buffer_stream(BufInfo* p_file, OPJ_UINT32 p_size, OPJ_BOOL p_is_read_stream) {
    opj_stream_t* l_stream;
    if(! p_file) return NULL;
    l_stream = opj_stream_create(p_size, p_is_read_stream);
    if(! l_stream) return NULL;
    opj_stream_set_user_data(l_stream, p_file , my_stream_free);
    opj_stream_set_user_data_length(l_stream, p_file->len);
    opj_stream_set_read_function(l_stream,  (opj_stream_read_fn) opj_read_from_buffer);
    opj_stream_set_write_function(l_stream, (opj_stream_write_fn) opj_write_from_buffer);
    opj_stream_set_skip_function(l_stream, (opj_stream_skip_fn) opj_skip_from_buffer);
    opj_stream_set_seek_function(l_stream, (opj_stream_seek_fn) opj_seek_from_buffer);
    return l_stream;
} //opj_stream_create_buffer_stream()

unsigned char * nii_loadImgCoreOpenJPEG(char* imgname, struct nifti_1_header hdr, struct TDICOMdata dcm, int compressFlag) {
    //OpenJPEG library is not well documented and has changed between versions
    //Since the JPEG is embedded in a DICOM we need to skip bytes at the start of the file
    // In theory we might also want to strip data that exists AFTER the image, see gdcmJPEG2000Codec.c
    unsigned char * ret = NULL;
    opj_dparameters_t params;
    opj_codec_t *codec;
    opj_image_t *jpx;
    opj_stream_t *stream;
    FILE *reader = fopen(imgname, "rb");
    fseek(reader, 0, SEEK_END);
    long size = ftell(reader)- dcm.imageStart;
    if (size <= 8) return NULL;
    fseek(reader, dcm.imageStart, SEEK_SET);
    unsigned char *data = (unsigned char*) malloc(size);
    size_t sz = fread(data, 1, size, reader);
    fclose(reader);
    if (sz < size) return NULL;
    OPJ_CODEC_FORMAT format = OPJ_CODEC_JP2;
    //DICOM JPEG2k is SUPPOSED to start with codestream, but some vendors include a header
    if (data[0] == 0xFF && data[1] == 0x4F && data[2] == 0xFF && data[3] == 0x51) format = OPJ_CODEC_J2K;
    opj_set_default_decoder_parameters(&params);
    BufInfo dx;
    dx.buf = data;
    dx.cur = data;
    dx.len = size;
    stream = opj_stream_create_buffer_stream(&dx, (OPJ_UINT32)size, true);
    if (stream == NULL) return NULL;
    codec = opj_create_decompress(format);
    // setup the decoder decoding parameters using user parameters
    if ( !opj_setup_decoder(codec, &params) ) goto cleanup2;
    // Read the main header of the codestream and if necessary the JP2 boxes
    if(! opj_read_header( stream, codec, &jpx)){
        printError( "OpenJPEG failed to read the header %s (offset %d)\n", imgname, dcm.imageStart);
        //comment these next lines to abort: include these to create zero-padded slice
        #ifdef MY_ZEROFILLBROKENJPGS
        //fix broken slices https://github.com/scitran-apps/dcm2niix/issues/4
        printError( "Zero-filled slice created\n");
        int imgbytes = (hdr.bitpix/8)*hdr.dim[1]*hdr.dim[2];
        ret = (unsigned char*) calloc(imgbytes,1);
        #endif
        goto cleanup2;
    }
    // Get the decoded image
    if ( !( opj_decode(codec, stream, jpx) && opj_end_decompress(codec,stream) ) ) {
        printError( "OpenJPEG j2k_to_image failed to decode %s\n",imgname);
        goto cleanup1;
    }
    ret = imagetoimg(jpx);
cleanup1:
    opj_image_destroy(jpx);
cleanup2:
    free(dx.buf);
    opj_stream_destroy(stream);
    opj_destroy_codec(codec);
    return ret;
}
#endif //if

#ifndef M_PI
#define M_PI           3.14159265358979323846
#endif

float deFuzz(float v) {
    if (fabs(v) < 0.00001)
        return 0;
    else
        return v;

}

#ifdef MY_DEBUG
void reportMat33(char *str, mat33 A) {
    printMessage("%s = [%g %g %g ; %g %g %g; %g %g %g ]\n",str,
           deFuzz(A.m[0][0]),deFuzz(A.m[0][1]),deFuzz(A.m[0][2]),
           deFuzz(A.m[1][0]),deFuzz(A.m[1][1]),deFuzz(A.m[1][2]),
           deFuzz(A.m[2][0]),deFuzz(A.m[2][1]),deFuzz(A.m[2][2]));
}

void reportMat44(char *str, mat44 A) {
//example: reportMat44((char*)"out",*R);
    printMessage("%s = [%g %g %g %g; %g %g %g %g; %g %g %g %g; 0 0 0 1]\n",str,
           deFuzz(A.m[0][0]),deFuzz(A.m[0][1]),deFuzz(A.m[0][2]),deFuzz(A.m[0][3]),
           deFuzz(A.m[1][0]),deFuzz(A.m[1][1]),deFuzz(A.m[1][2]),deFuzz(A.m[1][3]),
           deFuzz(A.m[2][0]),deFuzz(A.m[2][1]),deFuzz(A.m[2][2]),deFuzz(A.m[2][3]));
}
#endif

int verify_slice_dir (struct TDICOMdata d, struct TDICOMdata d2, struct nifti_1_header *h, mat44 *R, int isVerbose){
    //returns slice direction: 1=sag,2=coronal,3=axial, -= flipped
    if (h->dim[3] < 2) return 0; //don't care direction for single slice
    int iSL = 1; //find Z-slice direction: row with highest magnitude of 3rd column
    if ( (fabs(R->m[1][2]) >= fabs(R->m[0][2]))
        && (fabs(R->m[1][2]) >= fabs(R->m[2][2]))) iSL = 2; //
    if ( (fabs(R->m[2][2]) >= fabs(R->m[0][2]))
        && (fabs(R->m[2][2]) >= fabs(R->m[1][2]))) iSL = 3; //axial acquisition
    float pos = NAN;
    if ( !isnan(d2.patientPosition[iSL]) ) { //patient position fields exist
        pos = d2.patientPosition[iSL];
        if (isSameFloat(pos, d.patientPosition[iSL])) pos = NAN;
#ifdef MY_DEBUG
        if (!isnan(pos)) printMessage("position determined using lastFile %f\n",pos);
#endif
    }
    if (isnan(pos) &&( !isnan(d.patientPositionLast[iSL]) ) ) { //patient position fields exist
        pos = d.patientPositionLast[iSL];
        if (isSameFloat(pos, d.patientPosition[iSL])) pos = NAN;
#ifdef MY_DEBUG
        if (!isnan(pos)) printMessage("position determined using last (4d) %f\n",pos);
#endif
    }
    if (isnan(pos) && ( !isnan(d.stackOffcentre[iSL])) )
        pos = d.stackOffcentre[iSL];
    if (isnan(pos) && ( !isnan(d.lastScanLoc)) )
        pos = d.lastScanLoc;
    //if (isnan(pos))
    vec4 x;
    x.v[0] = 0.0; x.v[1] = 0.0; x.v[2]=(float)(h->dim[3]-1.0); x.v[3] = 1.0;
    vec4 pos1v = nifti_vect44mat44_mul(x, *R);
    float pos1 = pos1v.v[iSL-1];//-1 as C indexed from 0
    bool flip = false;
    if (!isnan(pos)) // we have real SliceLocation for last slice or volume center
        flip = (pos > R->m[iSL-1][3]) != (pos1 > R->m[iSL-1][3]); // same direction?, note C indices from 0
    else {// we do some guess work and warn user
		vec3 readV = setVec3(d.orient[1],d.orient[2],d.orient[3]);
		vec3 phaseV = setVec3(d.orient[4],d.orient[5],d.orient[6]);
		//printMessage("rd %g %g %g\n",readV.v[0],readV.v[1],readV.v[2]);
		//printMessage("ph %g %g %g\n",phaseV.v[0],phaseV.v[1],phaseV.v[2]);
    	vec3 sliceV = crossProduct(readV, phaseV); //order important: this is our hail mary
    	flip = ((sliceV.v[0]+sliceV.v[1]+sliceV.v[2]) < 0);
    	//printMessage("verify slice dir %g %g %g\n",sliceV.v[0],sliceV.v[1],sliceV.v[2]);
    	if (isVerbose) { //1st pass only
			if (!d.isDerived) {//do not warn user if image is derived
				printWarning("Unable to determine slice direction: please check whether slices are flipped\n");
			} else {
				printWarning("Unable to determine slice direction: please check whether slices are flipped (derived image)\n");
            }
    	}
    }
    if (flip) {
        for (int i = 0; i < 4; i++)
            R->m[i][2] = -R->m[i][2];
    }
    if (flip)
        iSL = -iSL;
	#ifdef MY_DEBUG
    printMessage("verify slice dir %d %d %d\n",h->dim[1],h->dim[2],h->dim[3]);
    //reportMat44((char*)"Rout",*R);
    printMessage("flip = %d\n",flip);
    printMessage("sliceDir = %d\n",iSL);
    printMessage(" pos1 = %f\n",pos1);
	#endif
	return iSL;
} //verify_slice_dir()

mat44 noNaN(mat44 Q44, bool isVerbose, bool * isBogus) //simplify any headers that have NaN values
{
    mat44 ret = Q44;
    bool isNaN44 = false;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            if (isnan(ret.m[i][j]))
                isNaN44 = true;
    if (isNaN44) {
        *isBogus = true;
        if (isVerbose)
        	printWarning("Bogus spatial matrix (perhaps non-spatial image): inspect spatial orientation\n");
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                if (i == j)
                    ret.m[i][j] = 1;
                else
                    ret.m[i][j] = 0;
        ret.m[1][1] = -1;
    } //if isNaN detected
    return ret;
}

#define kSessionOK 0
#define kSessionBadMatrix 1

void setQSForm(struct nifti_1_header *h, mat44 Q44i, bool isVerbose) {
    bool isBogus = false;
    mat44 Q44 = noNaN(Q44i, isVerbose, & isBogus);
    if ((h->session_error == kSessionBadMatrix) || (isBogus)) {
    	h->session_error = kSessionBadMatrix;
    	h->sform_code = NIFTI_XFORM_UNKNOWN;
    } else
    	h->sform_code = NIFTI_XFORM_SCANNER_ANAT;
    h->srow_x[0] = Q44.m[0][0];
    h->srow_x[1] = Q44.m[0][1];
    h->srow_x[2] = Q44.m[0][2];
    h->srow_x[3] = Q44.m[0][3];
    h->srow_y[0] = Q44.m[1][0];
    h->srow_y[1] = Q44.m[1][1];
    h->srow_y[2] = Q44.m[1][2];
    h->srow_y[3] = Q44.m[1][3];
    h->srow_z[0] = Q44.m[2][0];
    h->srow_z[1] = Q44.m[2][1];
    h->srow_z[2] = Q44.m[2][2];
    h->srow_z[3] = Q44.m[2][3];
    float dumdx, dumdy, dumdz;
    nifti_mat44_to_quatern( Q44 , &h->quatern_b, &h->quatern_c, &h->quatern_d,&h->qoffset_x, &h->qoffset_y, &h->qoffset_z, &dumdx, &dumdy, &dumdz,&h->pixdim[0]) ;
    h->qform_code = h->sform_code;
} //setQSForm()

#ifdef my_unused

ivec3 maxCol(mat33 R) {
//return index of maximum column in 3x3 matrix, e.g. [1 0 0; 0 1 0; 0 0 1] -> 1,2,3
	ivec3 ixyz;
	//foo is abs(R)
    mat33 foo;
    for (int i=0 ; i < 3 ; i++ )
        for (int j=0 ; j < 3 ; j++ )
            foo.m[i][j] =  fabs(R.m[i][j]);
	//ixyz.v[0] : row with largest value in column 1
	ixyz.v[0] = 1;
	if ((foo.m[1][0] > foo.m[0][0]) && (foo.m[1][0] >= foo.m[2][0]))
		ixyz.v[0] = 2; //2nd column largest column
	else if ((foo.m[2][0] > foo.m[0][0]) && (foo.m[2][0] > foo.m[1][0]))
		ixyz.v[0] = 3; //3rd column largest column
	//ixyz.v[1] : row with largest value in column 2, but not the same row as ixyz.v[1]
	if (ixyz.v[0] == 1) {
		ixyz.v[1] = 2;
		if (foo.m[2][1] > foo.m[1][1])
			ixyz.v[1] = 3;
	} else if (ixyz.v[0] == 2) {
		ixyz.v[1] = 1;
		if (foo.m[2][1] > foo.m[0][1])
			ixyz.v[1] = 3;
	} else { //ixyz.v[0] == 3
		ixyz.v[1] = 1;
		if (foo.m[1][1] > foo.m[0][1])
			ixyz.v[1] = 2;
	}
	//ixyz.v[2] : 3rd row, constrained by previous rows
	ixyz.v[2] = 6 - ixyz.v[1] - ixyz.v[0];//sum of 1+2+3
	return ixyz;
}

int sign(float x) {
//returns -1,0,1 depending on if X is less than, equal to or greater than zero
	if (x < 0)
		return -1;
	else if (x > 0)
		return 1;
	return 0;
}

// Subfunction: get dicom xform matrix and related info
// This is a direct port of  Xiangrui Li's dicm2nii function
mat44 xform_mat(struct TDICOMdata d) {
	vec3 readV = setVec3(d.orient[1],d.orient[2],d.orient[3]);
	vec3 phaseV = setVec3(d.orient[4],d.orient[5],d.orient[6]);
    vec3 sliceV = crossProduct(readV ,phaseV);
    mat33 R;
    LOAD_MAT33(R, readV.v[0], readV.v[1], readV.v[2],
    	phaseV.v[0], phaseV.v[1], phaseV.v[2],
    	sliceV.v[0], sliceV.v[1], sliceV.v[2]);
    R = nifti_mat33_transpose(R);
	//reportMat33((char*)"R",R);
	ivec3 ixyz = maxCol(R);
	//printMessage("%d %d %d\n", ixyz.v[0], ixyz.v[1], ixyz.v[2]);
	int iSL = ixyz.v[2]; // 1/2/3 for Sag/Cor/Tra slice
	float cosSL = R.m[iSL-1][2];
	//printMessage("cosSL\t%g\n", cosSL);
	//vec3 pixdim = setVec3(d.xyzMM[1], d.xyzMM[2], d.xyzMM[3]);
	//printMessage("%g %g %g\n", pixdim.v[0], pixdim.v[1], pixdim.v[2]);
	mat33 pixdim;
    LOAD_MAT33(pixdim, d.xyzMM[1], 0.0, 0.0,
    	0.0, d.xyzMM[2], 0.0,
    	0.0, 0.0, d.xyzMM[3]);
	R = nifti_mat33_mul(R, pixdim);
	//reportMat33((char*)"R",R);
	mat44 R44;
	LOAD_MAT44(R44, R.m[0][0], R.m[0][1], R.m[0][2], d.patientPosition[1],
		R.m[1][0], R.m[1][1], R.m[1][2], d.patientPosition[2],
		R.m[2][0], R.m[2][1], R.m[2][2], d.patientPosition[3]);
	//reportMat44((char*)"R",R44);
	//rest are former: R = verify_slice_dir(R, s, dim, iSL)


	if ((d.xyzDim[3]<2) && (d.CSA.mosaicSlices < 2))
		return R44; //don't care direction for single slice
	vec3 dim = setVec3(d.xyzDim[1], d.xyzDim[2], d.xyzDim[3]);
	if (d.CSA.mosaicSlices > 1) { //Siemens mosaic: use dim(1) since no transpose to img
        float nRowCol = ceil(sqrt((double) d.CSA.mosaicSlices));
        dim.v[0] = dim.v[0] / nRowCol;
        dim.v[1] = dim.v[1] / nRowCol;
        dim.v[2] = d.CSA.mosaicSlices;
		vec4 dim4 = setVec4((nRowCol-1)*dim.v[0]/2.0f, (nRowCol-1)*dim.v[1]/2.0f, 0);
		vec4 offset = nifti_vect44mat44_mul(dim4, R44 );
        //printMessage("%g %g %g\n", dim.v[0], dim.v[1], dim.v[2]);
        //printMessage("%g %g %g\n", dim4.v[0], dim4.v[1], dim4.v[2]);
        //printMessage("%g %g %g %g\n", offset.v[0], offset.v[1], offset.v[2], offset.v[3]);
		//printMessage("nRowCol\t%g\n", nRowCol);
		R44.m[0][3] = offset.v[0];
		R44.m[1][3] = offset.v[1];
		R44.m[2][3] = offset.v[2];
		//R44.m[3][3] = offset.v[3];
		if  (sign(d.CSA.sliceNormV[iSL]) != sign(cosSL)) {
			R44.m[0][2] = -R44.m[0][2];
			R44.m[1][2] = -R44.m[1][2];
			R44.m[2][2] = -R44.m[2][2];
			R44.m[3][2] = -R44.m[3][2];
		}
        //reportMat44((char*)"iR44",R44);
		return R44;
	} else if (true) {
//SliceNormalVector TO DO
		printMessage("Not completed");
#ifndef USING_R
		exit(2);
#endif
		return R44;
	}
	printMessage("Unable to determine spatial transform\n");
#ifndef USING_R
	exit(1);
#else
    return R44;
#endif
}

mat44 set_nii_header(struct TDICOMdata d) {
	mat44 R = xform_mat(d);
	//R(1:2,:) = -R(1:2,:); % dicom LPS to nifti RAS, xform matrix before reorient
    for (int i=0; i<2; i++)
        for(int j=0; j<4; j++)
            R.m[i][j] = -R.m[i][j];
	#ifdef MY_DEBUG
    reportMat44((char*)"R44",R);
	#endif
}
#endif

/*mat44 doQuadruped(mat44 m) {
	mat44 m_in = m;
	mat44 rot;
        LOAD_MAT44(rot, 1.0,0.0,0.0,0.0,
               0.0,0.0,-1.0,0.0,
               0.0,-1.0,0.0,0.0);
    return nifti_mat44_mul( rot, m_in );
}*/

// This code predates  Xiangrui Li's set_nii_header function
mat44 set_nii_header_x(struct TDICOMdata d, struct TDICOMdata d2, struct nifti_1_header *h, int* sliceDir, int isVerbose) {
    *sliceDir = 0;
    mat44 Q44 = nifti_dicom2mat(d.orient, d.patientPosition, d.xyzMM);
    //Q44 = doQuadruped(Q44);
	if (d.isSegamiOasis == true) {
		//Segami reconstructions appear to disregard DICOM spatial parameters: assume center of volume is isocenter and no table tilt
		// Consider sample image with d.orient (0020,0037) = -1 0 0; 0 1 0: this suggests image RAI (L->R, P->A, S->I) but the vendors viewing software suggests LPS
		//Perhaps we should ignore 0020,0037 and 0020,0032 as they are hidden in sequence 0054,0022, but in this case no positioning is provided
		// http://www.cs.ucl.ac.uk/fileadmin/cmic/Documents/DavidAtkinson/DICOM.pdf
		// https://www.slicer.org/wiki/Coordinate_systems
    	LOAD_MAT44(Q44, -h->pixdim[1],0,0,0, 0,-h->pixdim[2],0,0, 0,0,h->pixdim[3],0); //X and Y dimensions flipped in NIfTI (RAS) vs DICOM (LPS)
    	vec4 originVx = setVec4( (h->dim[1]+1.0f)/2.0f, (h->dim[2]+1.0f)/2.0f, (h->dim[3]+1.0f)/2.0f);
    	vec4 originMm = nifti_vect44mat44_mul(originVx, Q44);
    	for (int i = 0; i < 3; i++)
            Q44.m[i][3] = -originMm.v[i]; //set origin to center voxel
        if (isVerbose) {
        	//printMessage("origin (vx) %g %g %g\n",originVx.v[0],originVx.v[1],originVx.v[2]);
    		//printMessage("origin (mm) %g %g %g\n",originMm.v[0],originMm.v[1],originMm.v[2]);
    		printWarning("Segami coordinates defy DICOM convention, please check orientation\n");
    	}
    	return Q44;
    }
    //next line only for Siemens mosaic: ignore for UIH grid
	//	https://github.com/xiangruili/dicm2nii/commit/47ad9e6d9bc8a999344cbd487d602d420fb1509f
    if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (d.CSA.mosaicSlices > 1)) {
        double nRowCol = ceil(sqrt((double) d.CSA.mosaicSlices));
        double lFactorX = (d.xyzDim[1] -(d.xyzDim[1]/nRowCol)   )/2.0;
        double lFactorY = (d.xyzDim[2] -(d.xyzDim[2]/nRowCol)   )/2.0;
        Q44.m[0][3] =(float)((Q44.m[0][0]*lFactorX)+(Q44.m[0][1]*lFactorY)+Q44.m[0][3]);
		Q44.m[1][3] = (float)((Q44.m[1][0] * lFactorX) + (Q44.m[1][1] * lFactorY) + Q44.m[1][3]);
		Q44.m[2][3] = (float)((Q44.m[2][0] * lFactorX) + (Q44.m[2][1] * lFactorY) + Q44.m[2][3]);
        for (int c=0; c<2; c++)
            for (int r=0; r<4; r++)
                Q44.m[c][r] = -Q44.m[c][r];
        mat33 Q;

        LOAD_MAT33(Q, d.orient[1], d.orient[4],d.CSA.sliceNormV[1],
                   d.orient[2],d.orient[5],d.CSA.sliceNormV[2],
                   d.orient[3],d.orient[6],d.CSA.sliceNormV[3]);
        if  (nifti_mat33_determ(Q) < 0) { //Siemens sagittal are R>>L, whereas NIfTI is L>>R, we retain Siemens order on disk so ascending is still ascending, but we need to have the spatial transform reflect this.
            mat44 det;
            *sliceDir = kSliceOrientMosaicNegativeDeterminant; //we need to handle DTI vectors accordingly
            LOAD_MAT44(det, 1.0l,0.0l,0.0l,0.0l, 0.0l,1.0l,0.0l,0.0l, 0.0l,0.0l,-1.0l,0.0l);
            //patient_to_tal.m[2][3] = 1-d.CSA.MosaicSlices;
            Q44 = nifti_mat44_mul(Q44,det);
        }
    } else { //not a mosaic
        *sliceDir = verify_slice_dir(d, d2, h, &Q44, isVerbose);
        for (int c=0; c<4; c++)// LPS to nifti RAS, xform matrix before reorient
            for (int r=0; r<2; r++) //swap rows 1 & 2
                Q44.m[r][c] = - Q44.m[r][c];
    }
	#ifdef MY_DEBUG
    reportMat44((char*)"Q44",Q44);
	#endif
    return Q44;
}

int headerDcm2NiiSForm(struct TDICOMdata d, struct TDICOMdata d2,  struct nifti_1_header *h, int isVerbose) { //fill header s and q form
    //see http://nifti.nimh.nih.gov/pub/dist/src/niftilib/nifti1_io.c
    //returns sliceDir: 0=unknown,1=sag,2=coro,3=axial,-=reversed slices
    int sliceDir = 0;
    if (h->dim[3] < 2) {
    	mat44 Q44 = set_nii_header_x(d, d2, h, &sliceDir, isVerbose);
    	setQSForm(h,Q44, isVerbose);
    	return sliceDir; //don't care direction for single slice
    }
    h->sform_code = NIFTI_XFORM_UNKNOWN;
    h->qform_code = NIFTI_XFORM_UNKNOWN;
    bool isOK = false;
    for (int i = 1; i <= 6; i++)
        if (d.orient[i] != 0.0) isOK = true;
    if (!isOK) {
        //we will have to guess, assume axial acquisition saved in standard Siemens style?
        d.orient[1] = 1.0f; d.orient[2] = 0.0f;  d.orient[3] = 0.0f;
        d.orient[1] = 0.0f; d.orient[2] = 1.0f;  d.orient[3] = 0.0f;
        if ((d.isDerived) || ((d.bitsAllocated == 8) && (d.samplesPerPixel == 3) && (d.manufacturer == kMANUFACTURER_SIEMENS))) {
           printMessage("Unable to determine spatial orientation: 0020,0037 missing (probably not a problem: derived image)\n");
        } else {
            printMessage("Unable to determine spatial orientation: 0020,0037 missing (Type 1 attribute: not a valid DICOM) Series %ld\n", d.seriesNum);
        }
    }
    mat44 Q44 = set_nii_header_x(d, d2, h, &sliceDir, isVerbose);
    setQSForm(h,Q44, isVerbose);
    return sliceDir;
} //headerDcm2NiiSForm()

int headerDcm2Nii2(struct TDICOMdata d, struct TDICOMdata d2, struct nifti_1_header *h, int isVerbose) { //final pass after de-mosaic
    char txt[1024] = {""};
    if (h->slice_code == NIFTI_SLICE_UNKNOWN) h->slice_code = d.CSA.sliceOrder;
    if (h->slice_code == NIFTI_SLICE_UNKNOWN) h->slice_code = d2.CSA.sliceOrder; //sometimes the first slice order is screwed up https://github.com/eauerbach/CMRR-MB/issues/29
    if (d.modality == kMODALITY_MR)
    	sprintf(txt, "TE=%.2g;Time=%.3f", d.TE,d.acquisitionTime);
    else
    	sprintf(txt, "Time=%.3f", d.acquisitionTime);
    if (d.CSA.phaseEncodingDirectionPositive >= 0) {
        char dtxt[1024] = {""};
        sprintf(dtxt, ";phase=%d", d.CSA.phaseEncodingDirectionPositive);
        strcat(txt,dtxt);
    }
    //from dicm2nii 20151117 InPlanePhaseEncodingDirection
    if (d.phaseEncodingRC =='R')
        h->dim_info = (3 << 4) + (1 << 2) + 2;
    if (d.phaseEncodingRC =='C')
        h->dim_info = (3 << 4) + (2 << 2) + 1;
    if (d.CSA.multiBandFactor > 1) {
        char dtxt[1024] = {""};
        sprintf(dtxt, ";mb=%d", d.CSA.multiBandFactor);
        strcat(txt,dtxt);
    }
    // GCC 8 warns about truncation using snprintf
    // snprintf(h->descrip,80, "%s",txt);
    memcpy(h->descrip, txt, 79);
    h->descrip[79] = '\0';

    if (strlen(d.imageComments) > 0)
        snprintf(h->aux_file,24,"%.23s",d.imageComments);
    return headerDcm2NiiSForm(d,d2, h, isVerbose);
} //headerDcm2Nii2()

int dcmStrLen (int len, int kMaxLen) {
    if (len < kMaxLen)
        return len+1;
    else
        return kMaxLen;
} //dcmStrLen()

struct TDICOMdata clear_dicom_data() {
    struct TDICOMdata d;
    //d.dti4D = NULL;
    d.locationsInAcquisition = 0;
    d.modality = kMODALITY_UNKNOWN;
    d.effectiveEchoSpacingGE = 0;
    for (int i=0; i < 4; i++) {
            d.CSA.dtiV[i] = 0;
        d.patientPosition[i] = NAN;
        //d.patientPosition2nd[i] = NAN; //used to distinguish XYZT vs XYTZ for Philips 4D
        d.patientPositionLast[i] = NAN; //used to compute slice direction for Philips 4D
        d.stackOffcentre[i] = NAN;
        d.angulation[i] = 0.0f;
        d.xyzMM[i] = 1;
    }
    for (int i=0; i < MAX_NUMBER_OF_DIMENSIONS; ++i)
      d.dimensionIndexValues[i] = 0;
    //d.CSA.sliceTiming[0] = -1.0f; //impossible value denotes not known
    for (int z = 0; z < kMaxEPI3D; z++)
    	d.CSA.sliceTiming[z] = -1.0;
    d.CSA.numDti = 0;
    for (int i=0; i < 5; i++)
        d.xyzDim[i] = 1;
    for (int i = 0; i < 7; i++)
        d.orient[i] = 0.0f;
    strcpy(d.patientName, "");
    strcpy(d.patientID, "");
    strcpy(d.accessionNumber, "");
    strcpy(d.imageType,"");
    strcpy(d.imageComments, "");
    strcpy(d.imageBaseName, "");
    strcpy(d.phaseEncodingDirectionDisplayedUIH, "");
    strcpy(d.studyDate, "");
    strcpy(d.studyTime, "");
    strcpy(d.protocolName, "");
    strcpy(d.seriesDescription, "");
    strcpy(d.sequenceName, "");
    strcpy(d.scanningSequence, "");
    strcpy(d.sequenceVariant, "");
    strcpy(d.manufacturersModelName, "");
    strcpy(d.institutionalDepartmentName, "");
    strcpy(d.procedureStepDescription, "");
    strcpy(d.institutionName, "");
    strcpy(d.referringPhysicianName, "");
    strcpy(d.institutionAddress, "");
    strcpy(d.deviceSerialNumber, "");
    strcpy(d.softwareVersions, "");
    strcpy(d.stationName, "");
    strcpy(d.scanOptions, "");
    //strcpy(d.mrAcquisitionType, "");
    strcpy(d.seriesInstanceUID, "");
    strcpy(d.instanceUID, "");
    strcpy(d.studyID, "");
    strcpy(d.studyInstanceUID, "");
    strcpy(d.bodyPartExamined,"");
    strcpy(d.coilName, "");
    strcpy(d.coilElements, "");
    d.phaseEncodingLines = 0;
    //~ d.patientPositionSequentialRepeats = 0;
    //~ d.patientPositionRepeats = 0;
    d.isHasPhase = false;
    d.isHasReal = false;
    d.isHasImaginary = false;
    d.isHasMagnitude = false;
    //d.maxGradDynVol = -1; //PAR/REC only
    d.sliceOrient = kSliceOrientUnknown;
    d.dateTime = (double)19770703150928.0;
    d.acquisitionTime = 0.0f;
    d.acquisitionDate = 0.0f;
    d.manufacturer = kMANUFACTURER_UNKNOWN;
    d.isPlanarRGB = false;
    d.lastScanLoc = NAN;
    d.TR = 0.0;
    d.TE = 0.0;
    d.TI = 0.0;
    d.flipAngle = 0.0;
    d.bandwidthPerPixelPhaseEncode = 0.0;
    d.acquisitionDuration = 0.0;
    d.imagingFrequency = 0.0;
    d.numberOfAverages = 0.0;
    d.fieldStrength = 0.0;
    d.SAR = 0.0;
    d.pixelBandwidth = 0.0;
    d.zSpacing = 0.0;
    d.zThick = 0.0;
    //~ d.numberOfDynamicScans = 0;
    d.echoNum = 1;
    d.echoTrainLength = 0;
    d.phaseFieldofView = 0.0;
    d.dwellTime = 0;
    d.protocolBlockStartGE = 0;
    d.protocolBlockLengthGE = 0;
    d.phaseEncodingSteps = 0;
    d.coilCrc = 0;
    d.seriesUidCrc = 0;
    d.instanceUidCrc = 0;
    d.accelFactPE = 0.0;
    //d.patientPositionNumPhilips = 0;
    d.imageBytes = 0;
    d.intenScale = 1;
    d.intenScalePhilips = 0;
    d.intenIntercept = 0;
    d.gantryTilt = 0.0;
    d.radionuclidePositronFraction = 0.0;
    d.radionuclideTotalDose = 0.0;
    d.radionuclideHalfLife = 0.0;
    d.doseCalibrationFactor = 0.0;
    d.ecat_isotope_halflife = 0.0;
    d.ecat_dosage = 0.0;
    d.seriesNum = 1;
    d.acquNum = 0;
    d.imageNum = 1;
    d.imageStart = 0;
    d.is3DAcq = false; //e.g. MP-RAGE, SPACE, TFE
    d.is2DAcq = false; //
    d.isDerived = false; //0008,0008 = DERIVED,CSAPARALLEL,POSDISP
    d.isSegamiOasis = false; //these images do not store spatial coordinates
    d.isGrayscaleSoftcopyPresentationState = false;
    d.isRawDataStorage = false;
    d.isDiffusion = false;
    d.isVectorFromBMatrix = false;
    d.isStackableSeries = false; //combine DCE series https://github.com/rordenlab/dcm2niix/issues/252
    d.isXA10A = false; //https://github.com/rordenlab/dcm2niix/issues/236
    d.triggerDelayTime = 0.0;
    d.RWVScale = 0.0;
    d.RWVIntercept = 0.0;
    d.isScaleOrTEVaries = false;
    d.bitsAllocated = 16;//bits
    d.bitsStored = 0;
    d.samplesPerPixel = 1;
    d.pixelPaddingValue = NAN;
    d.isValid = false;
    d.isXRay = false;
    d.isMultiEcho = false;
    d.isSigned = false; //default is unsigned!
    d.isFloat = false; //default is for integers, not single or double precision
    d.isResampled = false; //assume data not resliced to remove gantry tilt problems
    d.isLocalizer = false;
    d.isNonParallelSlices = false;
    d.isCoilVaries = false;
    d.compressionScheme = 0; //none
    d.isExplicitVR = true;
    d.isLittleEndian = true; //DICOM initially always little endian
    d.converted2NII = 0;
    d.numberOfDiffusionDirectionGE = -1;
    d.phaseEncodingGE = kGE_PHASE_ENCODING_POLARITY_UNKNOWN;
    d.rtia_timerGE = -1.0;
    d.numberOfImagesInGridUIH = 0;
    d.phaseEncodingRC = '?';
    d.patientSex = '?';
    d.patientWeight = 0.0;
    strcpy(d.patientBirthDate, "");
    strcpy(d.patientAge, "");
    d.CSA.bandwidthPerPixelPhaseEncode = 0.0;
    d.CSA.mosaicSlices = 0;
    d.CSA.sliceNormV[1] = 0.0;
    d.CSA.sliceNormV[2] = 0.0;
    d.CSA.sliceNormV[3] = 1.0; //default Siemens Image Numbering is F>>H https://www.mccauslandcenter.sc.edu/crnl/tools/stc
    d.CSA.sliceOrder = NIFTI_SLICE_UNKNOWN;
    d.CSA.slice_start = 0;
    d.CSA.slice_end = 0;
    d.CSA.protocolSliceNumber1 = 0;
    d.CSA.phaseEncodingDirectionPositive = -1; //unknown
    d.CSA.isPhaseMap = false;
    d.CSA.multiBandFactor = 1;
    d.CSA.SeriesHeader_offset = 0;
    d.CSA.SeriesHeader_length = 0;
    return d;
} //clear_dicom_data()

int isdigitdot(int c) { //returns true if digit or '.'
	if (c == '.') return 1;
	return isdigit(c);
}

void dcmStrDigitsDotOnlyKey(char key, char* lStr) {
    //e.g. string "F:2.50" returns 2.50 if key==":"
    size_t len = strlen(lStr);
    if (len < 1) return;
    bool isKey = false;
    for (int i = 0; i < (int) len; i++) {
        if (!isdigitdot(lStr[i]) ) {
            isKey =  (lStr[i] == key);
            lStr[i] = ' ';

        } else if (!isKey)
        	lStr[i] = ' ';
    }
} //dcmStrDigitsOnlyKey()

void dcmStrDigitsOnlyKey(char key, char* lStr) {
    //e.g. string "p2s3" returns 2 if key=="p" and 3 if key=="s"
    size_t len = strlen(lStr);
    if (len < 1) return;
    bool isKey = false;
    for (int i = 0; i < (int) len; i++) {
        if (!isdigit(lStr[i]) ) {
            isKey =  (lStr[i] == key);
            lStr[i] = ' ';

        } else if (!isKey)
        	lStr[i] = ' ';
    }
} //dcmStrDigitsOnlyKey()

void dcmStrDigitsOnly(char* lStr) {
    //e.g. change "H11" to " 11"
    size_t len = strlen(lStr);
    if (len < 1) return;
    for (int i = 0; i < (int) len; i++)
        if (!isdigit(lStr[i]) )
            lStr[i] = ' ';
}

// Karl Malbrain's compact CRC-32. See "A compact CCITT crc16 and crc32 C implementation that balances processor cache usage against speed": http://www.geocities.com/malbrain/
uint32_t mz_crc32X(unsigned char *ptr, size_t buf_len)
{
  static const uint32_t s_crc32[16] = { 0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
  uint32_t crcu32 = 0;
  if (!ptr) return crcu32;
  crcu32 = ~crcu32; while (buf_len--) { uint8_t b = *ptr++; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b & 0xF)]; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b >> 4)]; }
  return ~crcu32;
}

void dcmStr(int lLength, unsigned char lBuffer[], char* lOut, bool isStrLarge = false) {
    if (lLength < 1) return;
//#ifdef _MSC_VER
	char * cString = (char *)malloc(sizeof(char) * (lLength + 1));
//#else
//	char cString[lLength + 1];
//#endif
    cString[lLength] =0;
    memcpy(cString, (char*)&lBuffer[0], lLength);
    //memcpy(cString, test, lLength);
    //printMessage("X%dX\n", (unsigned char)d.patientName[1]);
    for (int i = 0; i < lLength; i++)
        //assume specificCharacterSet (0008,0005) is ISO_IR 100 http://en.wikipedia.org/wiki/ISO/IEC_8859-1
        if (cString[i]< 1) {
            unsigned char c = (unsigned char)cString[i];
            if ((c >= 192) && (c <= 198)) cString[i] = 'A';
            if (c == 199) cString[i] = 'C';
            if ((c >= 200) && (c <= 203)) cString[i] = 'E';
            if ((c >= 204) && (c <= 207)) cString[i] = 'I';
            if (c == 208) cString[i] = 'D';
            if (c == 209) cString[i] = 'N';
            if ((c >= 210) && (c <= 214)) cString[i] = 'O';
            if (c == 215) cString[i] = 'x';
            if (c == 216) cString[i] = 'O';
            if ((c >= 217) && (c <= 220)) cString[i] = 'O';
            if (c == 221) cString[i] = 'Y';
            if ((c >= 224) && (c <= 230)) cString[i] = 'a';
            if (c == 231) cString[i] = 'c';
            if ((c >= 232) && (c <= 235)) cString[i] = 'e';
            if ((c >= 236) && (c <= 239)) cString[i] = 'i';
            if (c == 240) cString[i] = 'o';
            if (c == 241) cString[i] = 'n';
            if ((c >= 242) && (c <= 246)) cString[i] = 'o';
            if (c == 248) cString[i] = 'o';
            if ((c >= 249) && (c <= 252)) cString[i] = 'u';
            if (c == 253) cString[i] = 'y';
            if (c == 255) cString[i] = 'y';
        }
    for (int i = 0; i < lLength; i++)
        if ((cString[i]<1) || (cString[i]==' ') || (cString[i]==',') || (cString[i]=='^') || (cString[i]=='/') || (cString[i]=='\\')  || (cString[i]=='%') || (cString[i]=='*') || (cString[i] == 9) || (cString[i] == 10) || (cString[i] == 11) || (cString[i] == 13)) cString[i] = '_';
    int len = 1;
    for (int i = 1; i < lLength; i++) { //remove repeated "_"
        if ((cString[i-1]!='_') || (cString[i]!='_')) {
            cString[len] =cString[i];
            len++;
        }
    } //for each item
    if (cString[len-1] == '_') len--;
    //while ((len > 0) && (cString[len]=='_')) len--; //remove trailing '_'
    cString[len] = 0; //null-terminate, strlcpy does this anyway
    int maxLen = kDICOMStr;
    if (isStrLarge) maxLen = kDICOMStrLarge;
    len = dcmStrLen(len, maxLen);
    if (len == maxLen) { //we need space for null-termination
		if (cString[len-2] == '_') len = len -2;
	}
    memcpy(lOut,cString,len-1);
    lOut[len-1] = 0;
//#ifdef _MSC_VER
	free(cString);
//#endif
} //dcmStr()

#ifdef MY_OLD //this code works on Intel but not some older systems https://github.com/rordenlab/dcm2niix/issues/327
float dcmFloat(int lByteLength, unsigned char lBuffer[], bool littleEndian) {//read binary 32-bit float
    //http://stackoverflow.com/questions/2782725/converting-float-values-from-big-endian-to-little-endian
    bool swap = (littleEndian != littleEndianPlatform());
    float retVal = 0;
    if (lByteLength < 4) return retVal;
    memcpy(&retVal, (char*)&lBuffer[0], 4);
    if (!swap) return retVal;
    float swapVal;
    char *inFloat = ( char* ) & retVal;
    char *outFloat = ( char* ) & swapVal;
    outFloat[0] = inFloat[3];
    outFloat[1] = inFloat[2];
    outFloat[2] = inFloat[1];
    outFloat[3] = inFloat[0];
    //printMessage("swapped val = %f\n",swapVal);
    return swapVal;
} //dcmFloat()

double dcmFloatDouble(const size_t lByteLength, const unsigned char lBuffer[],
                      const bool littleEndian) {//read binary 64-bit float
    //http://stackoverflow.com/questions/2782725/converting-float-values-from-big-endian-to-little-endian
    bool swap = (littleEndian != littleEndianPlatform());
    double retVal = 0.0f;
    if (lByteLength < 8) return retVal;
    memcpy(&retVal, (char*)&lBuffer[0], 8);
    if (!swap) return retVal;
    char *floatToConvert = ( char* ) & lBuffer;
    char *returnFloat = ( char* ) & retVal;
    //swap the bytes into a temporary buffer
    returnFloat[0] = floatToConvert[7];
    returnFloat[1] = floatToConvert[6];
    returnFloat[2] = floatToConvert[5];
    returnFloat[3] = floatToConvert[4];
    returnFloat[4] = floatToConvert[3];
    returnFloat[5] = floatToConvert[2];
    returnFloat[6] = floatToConvert[1];
    returnFloat[7] = floatToConvert[0];
    //printMessage("swapped val = %f\n",retVal);
    return retVal;
} //dcmFloatDouble()
#else

float dcmFloat(int lByteLength, unsigned char lBuffer[], bool littleEndian) {//read binary 32-bit float
    //http://stackoverflow.com/questions/2782725/converting-float-values-from-big-endian-to-little-endian
    if (lByteLength < 4) return 0.0;
    bool swap = (littleEndian != littleEndianPlatform());
    union {
        uint32_t i;
        float f;
        uint8_t c[4];
  } i,o;
  memcpy(&i.i, (char*)&lBuffer[0], 4);
  //printf("%02x%02x%02x%02x\n",i.c[0], i.c[1], i.c[2], i.c[3]);
    if (!swap) return i.f;
  o.c[0] = i.c[3];
  o.c[1] = i.c[2];
  o.c[2] = i.c[1];
  o.c[3] = i.c[0];
  //printf("swp %02x%02x%02x%02x\n",o.c[0], o.c[1], o.c[2], o.c[3]);
  return o.f;
} //dcmFloat()

double dcmFloatDouble(const size_t lByteLength, const unsigned char lBuffer[],
                      const bool littleEndian) {//read binary 64-bit float
    //http://stackoverflow.com/questions/2782725/converting-float-values-from-big-endian-to-little-endian
    if (lByteLength < 8) return 0.0;
    bool swap = (littleEndian != littleEndianPlatform());
    union {
        uint32_t i;
        double d;
        uint8_t c[8];
  } i,o;
  memcpy(&i.i, (char*)&lBuffer[0], 8);
  if (!swap) return i.d;
  o.c[0] = i.c[7];
  o.c[1] = i.c[6];
  o.c[2] = i.c[5];
  o.c[3] = i.c[4];
  o.c[4] = i.c[3];
  o.c[5] = i.c[2];
  o.c[6] = i.c[1];
  o.c[7] = i.c[0];
  return o.d;
} //dcmFloatDouble()
#endif

int dcmInt (int lByteLength, unsigned char lBuffer[], bool littleEndian) { //read binary 16 or 32 bit integer
    if (littleEndian) {
        if (lByteLength <= 3)
            return  lBuffer[0] | (lBuffer[1]<<8); //shortint vs word?
        return lBuffer[0]+(lBuffer[1]<<8)+(lBuffer[2]<<16)+(lBuffer[3]<<24); //shortint vs word?
    }
    if (lByteLength <= 3)
        return  lBuffer[1] | (lBuffer[0]<<8); //shortint vs word?
    return lBuffer[3]+(lBuffer[2]<<8)+(lBuffer[1]<<16)+(lBuffer[0]<<24); //shortint vs word?
} //dcmInt()


uint32_t dcmAttributeTag (unsigned char lBuffer[], bool littleEndian) {
    // read Attribute Tag (AT) value
    // return in Group + (Element << 16) format
    if (littleEndian)
        return lBuffer[0]+(lBuffer[1]<<8)+(lBuffer[2]<<16)+(lBuffer[3]<<24);
    return lBuffer[1]+(lBuffer[0]<<8)+(lBuffer[3]<<16)+(lBuffer[2]<<24);
} //dcmInt()
/*
//the code below trims strings after integer
// does not appear required not http://en.cppreference.com/w/cpp/string/byte/atoi
//  "(atoi) Discards any whitespace characters until the first non-whitespace character is found, then takes as many characters as possible to form a valid integer"
int dcmStrInt (const int lByteLength, const unsigned char lBuffer[]) {//read int stored as a string
//returns first integer e.g. if 0043,1039 is "1000\8\0\0" the result will be 1000
    if (lByteLength < 1) return 0; //error
    bool isOK = false;
    int i = 0;
    for (i = 0; i <= lByteLength; i++) {
        if ((lBuffer[i] >= '0') && (lBuffer[i] <= '9'))
        	isOK = true;
        else if (isOK)
        	break;
    }
    if (!isOK) return 0; //error
	char * cString = (char *)malloc(sizeof(char) * (i + 1));
    cString[i] =0;
    memcpy(cString, (const unsigned char*)(&lBuffer[0]), i);
    int ret = atoi(cString);
	free(cString);
	return ret;
} //dcmStrInt()
*/

int dcmStrInt (const int lByteLength, const unsigned char lBuffer[]) {//read int stored as a string
//#ifdef _MSC_VER
	char * cString = (char *)malloc(sizeof(char) * (lByteLength + 1));
//#else
//	char cString[lByteLength + 1];
//#endif
    cString[lByteLength] =0;
    memcpy(cString, (const unsigned char*)(&lBuffer[0]), lByteLength);
    //printMessage(" --> *%s* %s%s\n",cString, &lBuffer[0],&lBuffer[1]);
    int ret = atoi(cString);
//#ifdef _MSC_VER
	free(cString);
//#endif
	return ret;
} //dcmStrInt()

int dcmStrManufacturer (const int lByteLength, unsigned char lBuffer[]) {//read float stored as a string
    if (lByteLength < 2) return kMANUFACTURER_UNKNOWN;
//#ifdef _MSC_VER
	char * cString = (char *)malloc(sizeof(char) * (lByteLength + 1));
//#else
//	char cString[lByteLength + 1];
//#endif
	int ret = kMANUFACTURER_UNKNOWN;
    cString[lByteLength] =0;
    memcpy(cString, (char*)&lBuffer[0], lByteLength);
    //printMessage("MANU %s\n",cString);
    if ((toupper(cString[0])== 'S') && (toupper(cString[1])== 'I'))
        ret = kMANUFACTURER_SIEMENS;
    if ((toupper(cString[0])== 'G') && (toupper(cString[1])== 'E'))
        ret = kMANUFACTURER_GE;
    if ((toupper(cString[0])== 'H') && (toupper(cString[1])== 'I'))
        ret = kMANUFACTURER_HITACHI;
    if ((toupper(cString[0])== 'P') && (toupper(cString[1])== 'H'))
        ret = kMANUFACTURER_PHILIPS;
    if ((toupper(cString[0])== 'T') && (toupper(cString[1])== 'O'))
        ret = kMANUFACTURER_TOSHIBA;
    if ((toupper(cString[0])== 'U') && (toupper(cString[1])== 'I'))
        ret = kMANUFACTURER_UIH;
    if ((toupper(cString[0])== 'B') && (toupper(cString[1])== 'R'))
        ret = kMANUFACTURER_BRUKER;
//#ifdef _MSC_VER
	free(cString);
//#endif
	return ret;
} //dcmStrManufacturer

float csaMultiFloat (unsigned char buff[], int nItems, float Floats[], int *ItemsOK) {
    //warning: lFloats indexed from 1! will fill lFloats[1]..[nFloats]
    //if lnItems == 1, returns first item, if lnItems > 1 returns index of final successful conversion
    TCSAitem itemCSA;
    *ItemsOK = 0;
    if (nItems < 1)  return 0.0f;
    Floats[1] = 0;
    int lPos = 0;
    for (int lI = 1; lI <= nItems; lI++) {
        memcpy(&itemCSA, &buff[lPos], sizeof(itemCSA));
        lPos +=sizeof(itemCSA);

        // Storage order is always little-endian, so byte-swap required values if necessary
        if (!littleEndianPlatform())
            nifti_swap_4bytes(1, &itemCSA.xx2_Len);

        if (itemCSA.xx2_Len > 0) {
            char * cString = (char *)malloc(sizeof(char) * (itemCSA.xx2_Len));
            memcpy(cString, &buff[lPos], itemCSA.xx2_Len); //TPX memcpy(&cString, &buff[lPos], sizeof(cString));
            lPos += ((itemCSA.xx2_Len +3)/4)*4;
            //printMessage(" %d item length %d = %s\n",lI, itemCSA.xx2_Len, cString);
            Floats[lI] = (float) atof(cString);
            *ItemsOK = lI; //some sequences have store empty items
            free(cString);
        }
    } //for each item
    return Floats[1];
} //csaMultiFloat()

bool csaIsPhaseMap (unsigned char buff[], int nItems) {
    //returns true if the tag "ImageHistory" has an item named "CC:ComplexAdd"
    TCSAitem itemCSA;
    if (nItems < 1)  return false;
    int lPos = 0;
    for (int lI = 1; lI <= nItems; lI++) {
        memcpy(&itemCSA, &buff[lPos], sizeof(itemCSA));
        lPos +=sizeof(itemCSA);

        // Storage order is always little-endian, so byte-swap required values if necessary
        if (!littleEndianPlatform())
            nifti_swap_4bytes(1, &itemCSA.xx2_Len);

        if (itemCSA.xx2_Len > 0) {
//#ifdef _MSC_VER
            char * cString = (char *)malloc(sizeof(char) * (itemCSA.xx2_Len + 1));
//#else
 //           char cString[itemCSA.xx2_Len];
//#endif
            memcpy(cString, &buff[lPos], sizeof(itemCSA.xx2_Len)); //TPX memcpy(&cString, &buff[lPos], sizeof(cString));
            lPos += ((itemCSA.xx2_Len +3)/4)*4;
            //printMessage(" %d item length %d = %s\n",lI, itemCSA.xx2_Len, cString);
            if (strcmp(cString, "CC:ComplexAdd") == 0)
                return true;
//#ifdef _MSC_VER
            free(cString);
//#endif
        }
    } //for each item
    return false;
} //csaIsPhaseMap()

void checkSliceTimes(struct TCSAdata *CSA, int itemsOK, int isVerbose, bool is3DAcq) {
	if ((is3DAcq) || (itemsOK < 1)) //we expect 3D sequences to be simultaneous
    	return;
	if (itemsOK > kMaxEPI3D) {
		printError("Please increase kMaxEPI3D and recompile\n");
		return;
	}
	float maxTimeValue, minTimeValue, timeValue1;
	minTimeValue = CSA->sliceTiming[0];
	for (int z = 0; z < itemsOK; z++)
		if (CSA->sliceTiming[z] < minTimeValue)
		 minTimeValue = CSA->sliceTiming[z];
	//CSA can report negative slice times
	// https://neurostars.org/t/slice-timing-illegal-values-in-fmriprep/1516/8
	// Nov 1, 2018 <siemens-healthineers.com> wrote:
	//  If you have an interleaved dataset we can more definitively validate this formula (aka sliceTime(i) - min(sliceTimes())).
	if (minTimeValue < 0) {
		printWarning("Adjusting for negative MosaicRefAcqTimes (issue 271).\n");
		for (int z = 0; z < itemsOK; z++)
			CSA->sliceTiming[z] = CSA->sliceTiming[z] - minTimeValue;
	}
	CSA->multiBandFactor = 1;
	timeValue1 = CSA->sliceTiming[0];
	int nTimeZero = 0;
	if (CSA->sliceTiming[0] == 0)
			nTimeZero++;
	int minTimeIndex = 0;
	int maxTimeIndex = minTimeIndex;
	minTimeValue = CSA->sliceTiming[0];
	maxTimeValue = minTimeValue;
	if (isVerbose > 1)
		printMessage("   sliceTimes %g\t", CSA->sliceTiming[0]);
	for (int z = 1; z < itemsOK; z++) { //find index and value of fastest time
		if (isVerbose > 1)
			printMessage("%g\t",  CSA->sliceTiming[z]);
		if (CSA->sliceTiming[z] == 0)
			nTimeZero++;
		if (CSA->sliceTiming[z] < minTimeValue) {
			minTimeValue = CSA->sliceTiming[z];
			minTimeIndex = (float) z;
		}
		if (CSA->sliceTiming[z] > maxTimeValue) {
			maxTimeValue = CSA->sliceTiming[z];
			maxTimeIndex = (float) z;
		}
		if (CSA->sliceTiming[z] == timeValue1) CSA->multiBandFactor++;
	}
	if (isVerbose > 1)
		printMessage("\n");
	CSA->slice_start = minTimeIndex;
	CSA->slice_end = maxTimeIndex;
	if (minTimeIndex == maxTimeIndex) {
		if (isVerbose)
			printMessage("No variability in slice times (3D EPI?)\n");
	}
	if (nTimeZero < 2) { //not for multi-band, not 3D
		if (minTimeIndex == 1)
			CSA->sliceOrder = NIFTI_SLICE_ALT_INC2;// e.g. 3,1,4,2
		else if (minTimeIndex == (itemsOK-2))
			CSA->sliceOrder = NIFTI_SLICE_ALT_DEC2;// e.g. 2,4,1,3 or   5,2,4,1,3
		else if ((minTimeIndex == 0) && (CSA->sliceTiming[1] < CSA->sliceTiming[2]))
			CSA->sliceOrder = NIFTI_SLICE_SEQ_INC; // e.g. 1,2,3,4
		else if ((minTimeIndex == 0) && (CSA->sliceTiming[1] > CSA->sliceTiming[2]))
			CSA->sliceOrder = NIFTI_SLICE_ALT_INC; //e.g. 1,3,2,4
		else if ((minTimeIndex == (itemsOK-1)) && (CSA->sliceTiming[itemsOK-3] > CSA->sliceTiming[itemsOK-2]))
			CSA->sliceOrder = NIFTI_SLICE_SEQ_DEC; //e.g. 4,3,2,1  or 5,4,3,2,1
		else if ((minTimeIndex == (itemsOK-1)) && (CSA->sliceTiming[itemsOK-3] < CSA->sliceTiming[itemsOK-2]))
			CSA->sliceOrder = NIFTI_SLICE_ALT_DEC; //e.g.  4,2,3,1 or 3,5,2,4,1
		else {
			if (!is3DAcq) //we expect 3D sequences to be simultaneous
				printWarning("Unable to determine slice order from CSA tag MosaicRefAcqTimes\n");
		}
	}
	if ((CSA->sliceOrder != NIFTI_SLICE_UNKNOWN) && (nTimeZero > 1) && (nTimeZero < itemsOK)) {
		if (isVerbose)
			printMessage(" Multiband x%d sequence: setting slice order as UNKNOWN (instead of %d)\n", nTimeZero, CSA->sliceOrder);
		CSA->sliceOrder = NIFTI_SLICE_UNKNOWN;
	}
} //checkSliceTimes()

int readCSAImageHeader(unsigned char *buff, int lLength, struct TCSAdata *CSA, int isVerbose, bool is3DAcq) {
    //see also http://afni.nimh.nih.gov/pub/dist/src/siemens_dicom_csa.c
    //printMessage("%c%c%c%c\n",buff[0],buff[1],buff[2],buff[3]);
    if (lLength < 36) return EXIT_FAILURE;
    if ((buff[0] != 'S') || (buff[1] != 'V') || (buff[2] != '1') || (buff[3] != '0') ) return EXIT_FAILURE;
    int lPos = 8; //skip 8 bytes of data, 'SV10' plus  2 32-bit values unused1 and unused2
    int lnTag = buff[lPos]+(buff[lPos+1]<<8)+(buff[lPos+2]<<16)+(buff[lPos+3]<<24);
    if (buff[lPos+4] != 77) return EXIT_FAILURE;
    lPos += 8; //skip 8 bytes of data, 32-bit lnTag plus 77 00 00 0
    TCSAtag tagCSA;
    TCSAitem itemCSA;
    int itemsOK;
    float lFloats[7];
    for (int lT = 1; lT <= lnTag; lT++) {
        memcpy(&tagCSA, &buff[lPos], sizeof(tagCSA)); //read tag
        lPos +=sizeof(tagCSA);
        // Storage order is always little-endian, so byte-swap required values if necessary
        if (!littleEndianPlatform())
            nifti_swap_4bytes(1, &tagCSA.nitems);
        if (isVerbose > 1) //extreme verbosity: show every CSA tag
        	printMessage("   %d CSA of %s %d\n",lPos, tagCSA.name, tagCSA.nitems);
        /*if (true) {
        	printMessage("%d CSA of %s %d\n",lPos, tagCSA.name, tagCSA.nitems);
        	float * vals = (float *)malloc(sizeof(float) * (tagCSA.nitems + 1));
			csaMultiFloat (&buff[lPos], tagCSA.nitems,vals, &itemsOK);
			if (itemsOK > 0) {
				for (int z = 1; z <= itemsOK; z++) //find index and value of fastest time
                    printMessage("%g\t",  vals[z]);
            	printMessage("\n");
            }
        }*/

        if (tagCSA.nitems > 0) {
            if (strcmp(tagCSA.name, "ImageHistory") == 0)
                CSA->isPhaseMap =  csaIsPhaseMap(&buff[lPos], tagCSA.nitems);
            else if (strcmp(tagCSA.name, "NumberOfImagesInMosaic") == 0)
                CSA->mosaicSlices = (int) round(csaMultiFloat (&buff[lPos], 1,lFloats, &itemsOK));
            else if (strcmp(tagCSA.name, "B_value") == 0) {
                CSA->dtiV[0] = csaMultiFloat (&buff[lPos], 1,lFloats, &itemsOK);
                if (CSA->dtiV[0] < 0.0) {
                    printWarning("(Corrupt) CSA reports negative b-value! %g\n",CSA->dtiV[0]);
                    CSA->dtiV[0] = 0.0;
                }
                CSA->numDti = 1; //triggered by b-value, as B0 images do not have DiffusionGradientDirection tag
            }
            else if ((strcmp(tagCSA.name, "DiffusionGradientDirection") == 0) && (tagCSA.nitems > 2)){
                CSA->dtiV[1] = csaMultiFloat (&buff[lPos], 3,lFloats, &itemsOK);
                CSA->dtiV[2] = lFloats[2];
                CSA->dtiV[3] = lFloats[3];
                if (isVerbose)
                    printMessage("DiffusionGradientDirection %f %f %f\n",lFloats[1],lFloats[2],lFloats[3]);
            } else if ((strcmp(tagCSA.name, "SliceNormalVector") == 0) && (tagCSA.nitems > 2)){
                CSA->sliceNormV[1] = csaMultiFloat (&buff[lPos], 3,lFloats, &itemsOK);
                CSA->sliceNormV[2] = lFloats[2];
                CSA->sliceNormV[3] = lFloats[3];
                if (isVerbose > 1)
                    printMessage("   SliceNormalVector %f %f %f\n",CSA->sliceNormV[1],CSA->sliceNormV[2],CSA->sliceNormV[3]);
            } else if (strcmp(tagCSA.name, "SliceMeasurementDuration") == 0)
                CSA->sliceMeasurementDuration = csaMultiFloat (&buff[lPos], 3,lFloats, &itemsOK);
            else if (strcmp(tagCSA.name, "BandwidthPerPixelPhaseEncode") == 0)
                CSA->bandwidthPerPixelPhaseEncode = csaMultiFloat (&buff[lPos], 3,lFloats, &itemsOK);
            else if ((strcmp(tagCSA.name, "MosaicRefAcqTimes") == 0) && (tagCSA.nitems > 3)  ){
				if (itemsOK > kMaxEPI3D) {
					printError("Please increase kMaxEPI3D and recompile\n");
				} else {
					float * sliceTimes = (float *)malloc(sizeof(float) * (tagCSA.nitems + 1));
					csaMultiFloat (&buff[lPos], tagCSA.nitems,sliceTimes, &itemsOK);
                 	for (int z = 0; z < kMaxEPI3D; z++)
        				CSA->sliceTiming[z] = -1.0;
                 	for (int z = 0; z < itemsOK; z++)
        				CSA->sliceTiming[z] = sliceTimes[z+1];
					free(sliceTimes);
					checkSliceTimes(CSA, itemsOK, isVerbose, is3DAcq);
                }
            } else if (strcmp(tagCSA.name, "ProtocolSliceNumber") == 0)
                CSA->protocolSliceNumber1 = (int) round (csaMultiFloat (&buff[lPos], 1,lFloats, &itemsOK));
            else if (strcmp(tagCSA.name, "PhaseEncodingDirectionPositive") == 0)
                CSA->phaseEncodingDirectionPositive = (int) round (csaMultiFloat (&buff[lPos], 1,lFloats, &itemsOK));
            for (int lI = 1; lI <= tagCSA.nitems; lI++) {
                memcpy(&itemCSA, &buff[lPos], sizeof(itemCSA));
                lPos +=sizeof(itemCSA);
                // Storage order is always little-endian, so byte-swap required values if necessary
                if (!littleEndianPlatform())
                    nifti_swap_4bytes(1, &itemCSA.xx2_Len);
                lPos += ((itemCSA.xx2_Len +3)/4)*4;
            }
        } //if at least 1 item
    }// for lT 1..lnTag
    if (CSA->protocolSliceNumber1 > 1) CSA->sliceOrder = NIFTI_SLICE_UNKNOWN;
    return EXIT_SUCCESS;
} // readCSAImageHeader()

void dcmMultiShorts (int lByteLength, unsigned char lBuffer[], int lnShorts, uint16_t *lShorts, bool littleEndian) {
//read array of unsigned shorts US http://dicom.nema.org/dicom/2013/output/chtml/part05/sect_6.2.html
    if ((lnShorts < 1) || (lByteLength != (lnShorts * 2))) return;
    memcpy(&lShorts[0], (uint16_t *)&lBuffer[0], lByteLength);
    bool swap = (littleEndian != littleEndianPlatform());
    if (swap)
    	nifti_swap_2bytes(lnShorts, &lShorts[0]);
} //dcmMultiShorts()

void dcmMultiLongs (int lByteLength, unsigned char lBuffer[], int lnLongs, uint32_t *lLongs, bool littleEndian) {
  //read array of unsigned longs UL http://dicom.nema.org/dicom/2013/output/chtml/part05/sect_6.2.html
  if((lnLongs < 1) || (lByteLength != (lnLongs * 4)))
    return;
  memcpy(&lLongs[0], (uint32_t *)&lBuffer[0], lByteLength);
  bool swap = (littleEndian != littleEndianPlatform());
  if (swap)
    nifti_swap_4bytes(lnLongs, &lLongs[0]);
} //dcmMultiLongs()

void dcmMultiFloat (int lByteLength, char lBuffer[], int lnFloats, float *lFloats) {
    //warning: lFloats indexed from 1! will fill lFloats[1]..[nFloats]
    if ((lnFloats < 1) || (lByteLength < 1)) return;
//#ifdef _MSC_VER
	char * cString = (char *)malloc(sizeof(char) * (lByteLength + 1));
//#else
//	char cString[lByteLength + 1];
//#endif
    memcpy(cString, (char*)&lBuffer[0], lByteLength);
    cString[lByteLength] = 0; //null terminate
    char *temp=( char *)malloc(lByteLength+1);
    int f = 0,lStart = 0;
    bool isOK = false;
    for (int i = 0; i <= lByteLength; i++) {
        if ((lBuffer[i] >= '0') && (lBuffer[i] <= '9')) isOK = true;
        if ((isOK) && ((i == (lByteLength)) || (lBuffer[i] == '/')  || (lBuffer[i] == ' ')  || (lBuffer[i] == '\\') )){
            //x strlcpy(temp,&cString[lStart],i-lStart+1);
            snprintf(temp,i-lStart+1,"%s",&cString[lStart]);
            //printMessage("dcmMultiFloat %s\n",temp);
            if (f < lnFloats) {
                f ++;
                lFloats[f] = (float) atof(temp);
                isOK = false;
                //printMessage("%d == %f\n", f, atof(temp));
            } //if f <= nFloats
            lStart = i+1;
        } //if isOK
    }  //for i to length
    free(temp);
//#ifdef _MSC_VER
	free(cString);
//#endif
} //dcmMultiFloat()

float dcmStrFloat (const int lByteLength, const unsigned char lBuffer[]) { //read float stored as a string
//#ifdef _MSC_VER
	char * cString = (char *)malloc(sizeof(char) * (lByteLength + 1));
//#else
//	char cString[lByteLength + 1];
//#endif
    memcpy(cString, (char*)&lBuffer[0], lByteLength);
    cString[lByteLength] = 0; //null terminate
    float ret = (float) atof(cString);
//#ifdef _MSC_VER
	free(cString);
//#endif
	return ret;
} //dcmStrFloat()

int headerDcm2Nii(struct TDICOMdata d, struct nifti_1_header *h, bool isComputeSForm) {
    //printMessage("bytes %dx%dx%d %d, %d\n",d.XYZdim[1],d.XYZdim[2],d.XYZdim[3], d.Allocbits_per_pixel, d.samplesPerPixel);
	memset(h, 0, sizeof(nifti_1_header)); //zero-fill structure so unused items are consistent
    for (int i = 0; i < 80; i++) h->descrip[i] = 0;
    for (int i = 0; i < 24; i++) h->aux_file[i] = 0;
    for (int i = 0; i < 18; i++) h->db_name[i] = 0;
    for (int i = 0; i < 10; i++) h->data_type[i] = 0;
    for (int i = 0; i < 16; i++) h->intent_name[i] = 0;
    if ((d.bitsAllocated == 8) && (d.samplesPerPixel == 3)) {
        h->intent_code = NIFTI_INTENT_ESTIMATE; //make sure we treat this as RGBRGB...RGB
        h->datatype = DT_RGB24;
    } else if ((d.bitsAllocated == 8) && (d.samplesPerPixel == 1))
        h->datatype = DT_UINT8;
    else if ((d.bitsAllocated == 12) && (d.samplesPerPixel == 1))
        h->datatype = DT_INT16;
    else if ((d.bitsAllocated == 16) && (d.samplesPerPixel == 1) && (d.isSigned))
        h->datatype = DT_INT16;
    else if ((d.bitsAllocated == 16) && (d.samplesPerPixel == 1) && (!d.isSigned))
        h->datatype = DT_UINT16;
    else if ((d.bitsAllocated == 32) && (d.isFloat))
        h->datatype = DT_FLOAT32;
    else if (d.bitsAllocated == 32)
        h->datatype = DT_INT32;
    else if ((d.bitsAllocated == 64) && (d.isFloat))
        h->datatype = DT_FLOAT64;
    else {
        printMessage("Unsupported DICOM bit-depth %d with %d samples per pixel\n",d.bitsAllocated,d.samplesPerPixel);
        return EXIT_FAILURE;
    }
    if ((h->datatype == DT_UINT16) && (d.bitsStored > 0) &&(d.bitsStored < 16))
        h->datatype = DT_INT16; // DT_INT16 is more widely supported, same represenation for values 0..32767
    for (int i = 0; i < 8; i++) {
        h->pixdim[i] = 0.0f;
        h->dim[i] = 0;
    }
    //next items listed as unused in NIfTI format, but zeroed for consistency across runs
	h->extents = 0;
    h->session_error = kSessionOK;
    h->glmin = 0; //unused, but make consistent
    h->glmax = 0; //unused, but make consistent
    h->regular = 114; //in legacy Analyze this was always 114
    //these are important
    h->scl_inter = d.intenIntercept;
    h->scl_slope = d.intenScale;
    h->cal_max = 0;
    h->cal_min = 0;
    h->magic[0]='n';
    h->magic[1]='+';
    h->magic[2]='1';
    h->magic[3]='\0';
    h->vox_offset = (float) d.imageStart;
    if (d.bitsAllocated == 12)
    	h->bitpix = 16 * d.samplesPerPixel;
    else
    	h->bitpix = d.bitsAllocated * d.samplesPerPixel;
    h->pixdim[1] = d.xyzMM[1];
    h->pixdim[2] = d.xyzMM[2];
    h->pixdim[3] = d.xyzMM[3];
    h->pixdim[4] = d.TR/1000; //TR reported in msec, time is in sec
    h->dim[1] = d.xyzDim[1];
    h->dim[2] = d.xyzDim[2];
    h->dim[3] = d.xyzDim[3];
    h->dim[4] = d.xyzDim[4];
    h->dim[5] = 1;
    h->dim[6] = 1;
    h->dim[7] = 1;
    if (h->dim[4] < 2)
        h->dim[0] = 3;
    else
        h->dim[0] = 4;
    for (int i = 0; i <= 3; i++) {
        h->srow_x[i] = 0.0f;
        h->srow_y[i] = 0.0f;
        h->srow_z[i] = 0.0f;
    }
    h->slice_start = 0;
    h->slice_end = 0;
    h->srow_x[0] = -1;
    h->srow_y[2] = 1;
    h->srow_z[1] = -1;
	h->srow_x[3] = ((float) h->dim[1] / 2);
	h->srow_y[3] = -((float)h->dim[3] / 2);
	h->srow_z[3] = ((float)h->dim[2] / 2);
    h->qform_code = NIFTI_XFORM_UNKNOWN;
    h->sform_code = NIFTI_XFORM_UNKNOWN;
    h->toffset = 0;
    h->intent_code = NIFTI_INTENT_NONE;
    h->dim_info = 0; //Freq, Phase and Slice all unknown
    h->xyzt_units = NIFTI_UNITS_MM + NIFTI_UNITS_SEC;
    h->slice_duration = 0; //avoid +inf/-inf, NaN
    h->intent_p1 = 0;  //avoid +inf/-inf, NaN
    h->intent_p2 = 0;  //avoid +inf/-inf, NaN
    h->intent_p3 = 0;  //avoid +inf/-inf, NaN
    h->pixdim[0] = 1; //QFactor should be 1 or -1
    h->sizeof_hdr = 348; //used to signify header does not need to be byte-swapped
    h->slice_code = d.CSA.sliceOrder;
    if (isComputeSForm)
    	headerDcm2Nii2(d, d, h, false);
    return EXIT_SUCCESS;
} // headerDcm2Nii()

bool isFloatDiff (float a, float b) {
    return (fabs (a - b) > FLT_EPSILON);
} //isFloatDiff()

mat33 nifti_mat33_reorder_cols( mat33 m, ivec3 v ) {
    // matlab equivalent ret = m(:, v); where v is 1,2,3 [INDEXED FROM ONE!!!!]
    mat33 ret;
    for (int r=0; r<3; r++) {
        for(int c=0; c<3; c++)
            ret.m[r][c] = m.m[r][v.v[c]-1];
    }
    return ret;
} //nifti_mat33_reorder_cols()

void changeExt (char *file_name, const char* ext) {
    char *p_extension;
    p_extension = strrchr(file_name, '.');
    //if ((p_extension >  file_name) && (strlen(ext) < 1))
    //	p_extension--;
    if (p_extension)
        strcpy(++p_extension, ext);
} //changeExt()

void cleanStr(char* lOut) {
//e.g. strings such as image comments with special characters (e.g. "G/6/2009") can disrupt file saves
	size_t lLength = strlen(lOut);
    if (lLength < 1) return;
	char * cString = (char *)malloc(sizeof(char) * (lLength + 1));
    cString[lLength] =0;
    memcpy(cString, (char*)&lOut[0], lLength);
    for (int i = 0; i < lLength; i++)
        //assume specificCharacterSet (0008,0005) is ISO_IR 100 http://en.wikipedia.org/wiki/ISO/IEC_8859-1
        if (cString[i]< 1) {
            unsigned char c = (unsigned char)cString[i];
            if ((c >= 192) && (c <= 198)) cString[i] = 'A';
            if (c == 199) cString[i] = 'C';
            if ((c >= 200) && (c <= 203)) cString[i] = 'E';
            if ((c >= 204) && (c <= 207)) cString[i] = 'I';
            if (c == 208) cString[i] = 'D';
            if (c == 209) cString[i] = 'N';
            if ((c >= 210) && (c <= 214)) cString[i] = 'O';
            if (c == 215) cString[i] = 'x';
            if (c == 216) cString[i] = 'O';
            if ((c >= 217) && (c <= 220)) cString[i] = 'O';
            if (c == 221) cString[i] = 'Y';
            if ((c >= 224) && (c <= 230)) cString[i] = 'a';
            if (c == 231) cString[i] = 'c';
            if ((c >= 232) && (c <= 235)) cString[i] = 'e';
            if ((c >= 236) && (c <= 239)) cString[i] = 'i';
            if (c == 240) cString[i] = 'o';
            if (c == 241) cString[i] = 'n';
            if ((c >= 242) && (c <= 246)) cString[i] = 'o';
            if (c == 248) cString[i] = 'o';
            if ((c >= 249) && (c <= 252)) cString[i] = 'u';
            if (c == 253) cString[i] = 'y';
            if (c == 255) cString[i] = 'y';
        }
    for (int i = 0; i < lLength; i++)
        if ((cString[i]<1) || (cString[i]==' ') || (cString[i]==',') || (cString[i]=='^') || (cString[i]=='/') || (cString[i]=='\\')  || (cString[i]=='%') || (cString[i]=='*') || (cString[i] == 9) || (cString[i] == 10) || (cString[i] == 11) || (cString[i] == 13)) cString[i] = '_';
    int len = 1;
    for (int i = 1; i < lLength; i++) { //remove repeated "_"
        if ((cString[i-1]!='_') || (cString[i]!='_')) {
            cString[len] =cString[i];
            len++;
        }
    } //for each item
    if (cString[len-1] == '_') len--;
    cString[len] = 0; //null-terminate, strlcpy does this anyway
    int maxLen = kDICOMStr;
    len = dcmStrLen(len, maxLen);
    if (len == maxLen) { //we need space for null-termination
		if (cString[len-2] == '_') len = len -2;
	}
    memcpy(lOut,cString,len-1);
    lOut[len-1] = 0;
	free(cString);
} //cleanStr()

int isSameFloatGE (float a, float b) {
//Kludge for bug in 0002,0016="DIGITAL_JACKET", 0008,0070="GE MEDICAL SYSTEMS" DICOM data: Orient field (0020:0037) can vary 0.00604261 == 0.00604273 !!!
    //return (a == b); //niave approach does not have any tolerance for rounding errors
    return (fabs (a - b) <= 0.0001);
}

struct TDICOMdata  nii_readParRec (char * parname, int isVerbose, struct TDTI4D *dti4D, bool isReadPhase) {
struct TDICOMdata d = clear_dicom_data();
strcpy(d.protocolName, ""); //erase dummy with empty
strcpy(d.seriesDescription, ""); //erase dummy with empty
strcpy(d.sequenceName, ""); //erase dummy with empty
strcpy(d.scanningSequence, "");
FILE *fp = fopen(parname, "r");
if (fp == NULL) return d;
#define LINESZ 2048
#define	kSlice	0
#define	kEcho	1
#define	kDyn	2
#define	kCardiac	3
#define	kImageType	4
#define	kSequence	5
#define	kIndex	6
//V3 only identical for columns 1..6
#define	kBitsPerVoxel 7 //V3: not per slice: "Image pixel size [8 or 16 bits]"
#define	kXdim	9 //V3: not per slice: "Recon resolution (x, y)"
#define	kYdim	10 //V3: not per slice: "Recon resolution (x, y)"
int	kRI	= 11; //V3: 7
int	kRS	= 12; //V3: 8
int	kSS	= 13; //V3: 9
int	kAngulationAPs = 16; //V3: 12
int	kAngulationFHs = 17; //V3: 13
int	kAngulationRLs = 18; //V3: 14
int	kPositionAP	= 19; //V3: 15
int	kPositionFH	= 20; //V3: 16
int	kPositionRL	= 21; //V3: 17
#define	kThickmm	22 //V3: not per slice: "Slice thickness [mm]"
#define	kGapmm	23 //V3: not per slice: "Slice gap [mm]"
int kSliceOrients = 25; //V3: 19
int	kXmm = 28; //V3:  22
int	kYmm = 29; //V3: 23
int	kTEcho = 30; //V3: 24
int	kDynTime = 31; //V3: 25
int	kTriggerTime = 32; //V3: 26
int	kbval = 33; //V3: 27
//the following do not exist in V3
#define	kInversionDelayMs 40
#define	kbvalNumber 41
#define	kGradientNumber 42
//the following do not exist in V40 or earlier
#define	kv1	47
#define	kv2	45
#define	kv3	46
//the following do not exist in V41 or earlier
#define	kASL	48
#define kMaxImageType 4 //4 observed image types: real, imag, mag, phase (in theory also subsequent calculation such as B1)
    printWarning("dcm2niix PAR is not actively supported (hint: use dicm2nii)\n");
    if (isReadPhase) printWarning(" Reading phase images from PAR/REC\n");
    char buff[LINESZ];
	//next values: PAR V3 only
	int v3BitsPerVoxel = 16; //V3: not per slice: "Image pixel size [8 or 16 bits]"
	int v3Xdim = 128; //not per slice: "Recon resolution (x, y)"
	int v3Ydim	= 128; //V3: not per slice: "Recon resolution (x, y)"
	float v3Thickmm	= 2.0; //V3: not per slice: "Slice thickness [mm]"
	float v3Gapmm	= 0.0; //V3: not per slice: "Slice gap [mm]"
	//from top of header
	int maxNumberOfDiffusionValues = 1;
	int maxNumberOfGradientOrients = 1;
	int maxNumberOfCardiacPhases = 1;
	int maxNumberOfEchoes = 1;
	int maxNumberOfDynamics = 1;
	int maxNumberOfMixes = 1;
	int maxNumberOfLabels = 1;//Number of label types   <0=no ASL>
    float maxBValue = 0.0f;
    float maxDynTime = 0.0f;
    float minDynTime = 999999.0f;
    float TE = 0.0;
    int minDyn = 32767;
    int maxDyn = 0;
    int minSlice = 32767;
    int maxSlice = 0;
    bool ADCwarning = false;
    bool isTypeWarning = false;
    bool isType4Warning = false;
    bool isSequenceWarning = false;
    int numSlice2D = 0;
    int prevDyn = -1;
    bool dynNotAscending = false;
    int parVers = 0;
    int maxSeq = -1; //maximum value of Seq column
    int seq1 = -1; //value of Seq volume for first slice
    int maxEcho = 1;
    int maxCardiac = 1;
    int nCols = 26;
    //int diskSlice = 0;
    int num3DExpected = 0; //number of 3D volumes in the top part of the header
    int num2DExpected = 0; //number of 2D slices described in the top part of the header
    int maxVol = -1;
    int patientPositionNumPhilips = 0;
    d.isValid = false;
    const int kMaxCols = 49;
    float *cols = (float *)malloc(sizeof(float) * (kMaxCols+1));
    for (int i = 0; i < kMaxCols; i++)
    	cols[i] = 0.0; //old versions of PAR do not fill all columns - beware of buffer overflow
    char *p = fgets (buff, LINESZ, fp);
    bool isIntenScaleVaries = false;
    for (int i = 0; i < kMaxDTI4D; i++) {
        dti4D->S[i].V[0] = -1.0;
        dti4D->TE[i] = -1.0;
    }
    for (int i = 0; i < kMaxSlice2D; i++)
    	dti4D->sliceOrder[i] = -1;
    while (p) {
        if (strlen(buff) < 1)
            continue;
        if (buff[0] == '#') { //comment
            char Comment[7][50];
            sscanf(buff, "# %s %s %s %s %s %s V%s\n", Comment[0], Comment[1], Comment[2], Comment[3],Comment[4], Comment[5],Comment[6]);
            if ((strcmp(Comment[0], "sl") == 0) && (strcmp(Comment[1], "ec") == 0) ) {
            	num3DExpected = maxNumberOfGradientOrients * maxNumberOfDiffusionValues * maxNumberOfLabels
	 				* maxNumberOfCardiacPhases * maxNumberOfEchoes * maxNumberOfDynamics * maxNumberOfMixes;
            	num2DExpected = d.xyzDim[3] * num3DExpected;
	 			if ((num2DExpected ) >= kMaxSlice2D) {
					printError("Use dicm2nii or increase kMaxDTI4D to be more than %d\n", num2DExpected);
					printMessage("  slices*grad*bval*cardiac*echo*dynamic*mix*label = %d*%d*%d*%d*%d*%d*%d*%d\n",
            		d.xyzDim[3],  maxNumberOfGradientOrients,maxNumberOfDiffusionValues,
    		maxNumberOfCardiacPhases, maxNumberOfEchoes, maxNumberOfDynamics, maxNumberOfMixes, maxNumberOfLabels);
					free (cols);
					return d;
	 			}
			}
            if (strcmp(Comment[1], "TRYOUT") == 0) {
                //sscanf(buff, "# %s %s %s %s %s %s V%s\n", Comment[0], Comment[1], Comment[2], Comment[3],Comment[4], Comment[5],Comment[6]);
                parVers = (int)round(atof(Comment[6])*10); //4.2 = 42 etc
                if (parVers <= 29) {
                    printMessage("Unsupported old PAR version %0.2f (use dicm2nii)\n", parVers/10.0);
                    return d;
                    //nCols = 26; //e.g. PAR 3.0 has 26 relevant columns
                } if (parVers < 40) {
                    nCols = 29; // PAR 3.0?
					kRI	= 7;
					kRS	= 8;
					kSS	= 9;
					kAngulationAPs = 12;
					kAngulationFHs = 13;
					kAngulationRLs = 14;
					kPositionAP	= 15;
					kPositionFH	= 16;
					kPositionRL	= 17;
					kSliceOrients = 19;
					kXmm = 22;
					kYmm = 23;
					kTEcho = 24;
					kDynTime = 25;
					kTriggerTime = 26;
					kbval = 27;
                } else if (parVers < 41)
                	nCols = kv1; //e.g PAR 4.0
                else if (parVers < 42)
                    nCols = kASL; //e.g. PAR 4.1 - last column is final diffusion b-value
                else
                    nCols = kMaxCols; //e.g. PAR 4.2
            }
            //the following do not exist in V3
            p = fgets (buff, LINESZ, fp);//get next line
            continue;
        } //process '#' comment
        if (buff[0] == '.') { //tag
            char Comment[9][50];
            for (int i = 0; i < 9; i++)
            	strcpy(Comment[i], "");
            sscanf(buff, ". %s %s %s %s %s %s %s %s %s\n", Comment[0], Comment[1],Comment[2], Comment[3], Comment[4], Comment[5], Comment[6], Comment[7], Comment[8]);
            if ((strcmp(Comment[0], "Acquisition") == 0) && (strcmp(Comment[1], "nr") == 0)) {
                d.acquNum = atoi( Comment[3]);
                d.seriesNum = d.acquNum;
            }
            if ((strcmp(Comment[0], "Recon") == 0) && (strcmp(Comment[1], "resolution") == 0)) {
                v3Xdim = (int) atoi(Comment[5]);
                v3Ydim = (int) atoi(Comment[6]);
                //printMessage("recon %d,%d\n", v3Xdim,v3Ydim);
            }
            if ((strcmp(Comment[1], "pixel") == 0) && (strcmp(Comment[2], "size") == 0)) {
                v3BitsPerVoxel = (int) atoi(Comment[8]);
                //printMessage("bits %d\n", v3BitsPerVoxel);
            }
            if ((strcmp(Comment[0], "Slice") == 0) && (strcmp(Comment[1], "gap") == 0)) {
                v3Gapmm = (float) atof(Comment[4]);
                //printMessage("gap %g\n", v3Gapmm);
            }
            if ((strcmp(Comment[0], "Slice") == 0) && (strcmp(Comment[1], "thickness") == 0)) {
                v3Thickmm = (float) atof(Comment[4]);
                //printMessage("thick %g\n", v3Thickmm);
            }
            if ((strcmp(Comment[0], "Repetition") == 0) && (strcmp(Comment[1], "time") == 0))
                d.TR = (float) atof(Comment[4]);
            if ((strcmp(Comment[0], "Patient") == 0) && (strcmp(Comment[1], "name") == 0)) {
                strcpy(d.patientName, Comment[3]);
                strcat(d.patientName, Comment[4]);
                strcat(d.patientName, Comment[5]);
                strcat(d.patientName, Comment[6]);
                strcat(d.patientName, Comment[7]);
                cleanStr(d.patientName);
                //printMessage("%s\n",d.patientName);
            }
            if ((strcmp(Comment[0], "Technique") == 0) && (strcmp(Comment[1], ":") == 0)) {
                strcpy(d.patientID, Comment[2]);
                strcat(d.patientID, Comment[3]);
                strcat(d.patientID, Comment[4]);
                strcat(d.patientID, Comment[5]);
                strcat(d.patientID, Comment[6]);
                strcat(d.patientID, Comment[7]);
                cleanStr(d.patientID);
            }
            if ((strcmp(Comment[0], "Protocol") == 0) && (strcmp(Comment[1], "name") == 0)) {
                strcpy(d.protocolName, Comment[3]);
                strcat(d.protocolName, Comment[4]);
                strcat(d.protocolName, Comment[5]);
                strcat(d.protocolName, Comment[6]);
                strcat(d.protocolName, Comment[7]);
                cleanStr(d.protocolName);
            }
            if ((strcmp(Comment[0], "Examination") == 0) && (strcmp(Comment[1], "name") == 0)) {
            	strcpy(d.imageComments, Comment[3]);
                strcat(d.imageComments, Comment[4]);
                strcat(d.imageComments, Comment[5]);
                strcat(d.imageComments, Comment[6]);
                strcat(d.imageComments, Comment[7]);
                cleanStr(d.imageComments);
            }
            if ((strcmp(Comment[0], "Series") == 0) && (strcmp(Comment[1], "Type") == 0)) {
            	strcpy(d.seriesDescription, Comment[3]);
                strcat(d.seriesDescription, Comment[4]);
                strcat(d.seriesDescription, Comment[5]);
                strcat(d.seriesDescription, Comment[6]);
                strcat(d.seriesDescription, Comment[7]);
                cleanStr(d.seriesDescription);
            }
            if ((strcmp(Comment[0], "Examination") == 0) && (strcmp(Comment[1], "date/time") == 0)) {
            	if ((strlen(Comment[3]) >= 10) && (strlen(Comment[5]) >= 8)) {
            		//DICOM date format is YYYYMMDD, but PAR stores YYYY.MM.DD 2016.03.25
            		d.studyDate[0] = Comment[3][0];
            		d.studyDate[1] = Comment[3][1];
            		d.studyDate[2] = Comment[3][2];
            		d.studyDate[3] = Comment[3][3];
            		d.studyDate[4] = Comment[3][5];
            		d.studyDate[5] = Comment[3][6];
            		d.studyDate[6] = Comment[3][8];
            		d.studyDate[7] = Comment[3][9];
            		d.studyDate[8] = '\0';
    				//DICOM time format is HHMMSS.FFFFFF, but PAR stores HH:MM:SS, e.g. 18:00:42 or 09:34:16
            		d.studyTime[0] = Comment[5][0];
            		d.studyTime[1] = Comment[5][1];
            		d.studyTime[2] = Comment[5][3];
            		d.studyTime[3] = Comment[5][4];
            		d.studyTime[4] = Comment[5][6];
            		d.studyTime[5] = Comment[5][7];
            		d.studyTime[6] = '\0';
    				d.dateTime = (atof(d.studyDate)* 1000000) + atof(d.studyTime);
    			}
            }
            if ((strcmp(Comment[0], "Off") == 0) && (strcmp(Comment[1], "Centre") == 0)) {
                //Off Centre midslice(ap,fh,rl) [mm]
                d.stackOffcentre[2] = (float) atof(Comment[5]);
				d.stackOffcentre[3] = (float) atof(Comment[6]);
				d.stackOffcentre[1] = (float) atof(Comment[7]);
            }
            if ((strcmp(Comment[0], "Patient") == 0) && (strcmp(Comment[1], "position") == 0)) {
                //Off Centre midslice(ap,fh,rl) [mm]
                d.patientOrient[0] = toupper(Comment[3][0]);
                d.patientOrient[1] = toupper(Comment[4][0]);
                d.patientOrient[2] = toupper(Comment[5][0]);
                d.patientOrient[3] = 0;
            }
            if ((strcmp(Comment[0], "Max.") == 0) && (strcmp(Comment[3], "slices/locations") == 0)) {
                d.xyzDim[3] = atoi(Comment[5]);
            }
            if ((strcmp(Comment[0], "Max.") == 0) && (strcmp(Comment[3], "diffusion") == 0)) {
                maxNumberOfDiffusionValues = atoi(Comment[6]);
                //if (maxNumberOfDiffusionValues > 1) maxNumberOfDiffusionValues -= 1; //if two listed, one is B=0
            }
            if ((strcmp(Comment[0], "Max.") == 0) && (strcmp(Comment[3], "gradient") == 0)) {
                maxNumberOfGradientOrients = atoi(Comment[6]);
                //Warning ISOTROPIC scans may be stored that are not reported here! 32 directions plus isotropic = 33 volumes
            }
            if ((strcmp(Comment[0], "Max.") == 0) && (strcmp(Comment[3], "cardiac") == 0)) {
                maxNumberOfCardiacPhases = atoi(Comment[6]);
            }
            if ((strcmp(Comment[0], "Max.") == 0) && (strcmp(Comment[3], "echoes") == 0)) {
                maxNumberOfEchoes = atoi(Comment[5]);
                if (maxNumberOfEchoes > 1) d.isMultiEcho = true;
            }
            if ((strcmp(Comment[0], "Max.") == 0) && (strcmp(Comment[3], "dynamics") == 0)) {
                maxNumberOfDynamics = atoi(Comment[5]);
            }
            if ((strcmp(Comment[0], "Max.") == 0) && (strcmp(Comment[3], "mixes") == 0)) {
                maxNumberOfMixes = atoi(Comment[5]);
                if (maxNumberOfMixes > 1)
                	printError("maxNumberOfMixes > 1. Please update this software to support these images\n");
            }
            if ((strcmp(Comment[0], "Number") == 0) && (strcmp(Comment[2], "label") == 0)) {
                maxNumberOfLabels = atoi(Comment[7]);
                if (maxNumberOfLabels < 1) maxNumberOfLabels = 1;
            }
            p = fgets (buff, LINESZ, fp);//get next line
            continue;
        } //process '.' tag
        if (strlen(buff) < 24) { //empty line
            p = fgets (buff, LINESZ, fp);//get next line
            continue;
        }
        if (parVers < 20) {
            printError("PAR files should have 'CLINICAL TRYOUT' line with a version from 2.0-4.2: %s\n", parname);
            free (cols);
            return d;
        }
        for (int i = 0; i <= nCols; i++)
            cols[i] = strtof(p, &p); // p+1 skip comma, read a float
		//printMessage("xDim %dv%d yDim %dv%d bits  %dv%d\n", d.xyzDim[1],(int)cols[kXdim], d.xyzDim[2], (int)cols[kYdim], d.bitsAllocated, (int)cols[kBitsPerVoxel]);
		if ((int)cols[kSlice] == 0) { //line does not contain attributes
			p = fgets (buff, LINESZ, fp);//get next line
			continue;
		}
		//diskSlice ++;
        bool isADC = false;
        if ((maxNumberOfGradientOrients >= 2) && (cols[kbval] > 50) && isSameFloat(0.0, cols[kv1]) && isSameFloat(0.0, cols[kv2]) && isSameFloat(0.0, cols[kv2]) ) {
        	isADC = true;
        	ADCwarning = true;
        }
        //printMessage(">>%d  %d\n", (int)cols[kSlice],  diskSlice);
        if (numSlice2D < 1) {
            d.xyzMM[1] = cols[kXmm];
            d.xyzMM[2] = cols[kYmm];
            if (parVers < 40) { //v3 does things differently
            	//cccc
            	d.xyzDim[1] = v3Xdim;
				d.xyzDim[2] = v3Ydim;
            	d.xyzMM[3] = v3Thickmm + v3Gapmm;
            	d.bitsAllocated = v3BitsPerVoxel;
            	d.bitsStored = v3BitsPerVoxel;
            } else {
            	d.xyzDim[1] = (int) cols[kXdim];
				d.xyzDim[2] = (int) cols[kYdim];
            	d.xyzMM[3] = cols[kThickmm] + cols[kGapmm];
            	d.bitsAllocated = (int) cols[kBitsPerVoxel];
				d.bitsStored = (int) cols[kBitsPerVoxel];

            }
            d.patientPosition[1] = cols[kPositionRL];
            d.patientPosition[2] = cols[kPositionAP];
            d.patientPosition[3] = cols[kPositionFH];
            d.angulation[1] = cols[kAngulationRLs];
            d.angulation[2] = cols[kAngulationAPs];
            d.angulation[3] = cols[kAngulationFHs];
			d.sliceOrient = (int) cols[kSliceOrients];
			d.TE = cols[kTEcho];
            d.echoNum = cols[kEcho];
            d.TI = cols[kInversionDelayMs];
			d.intenIntercept = cols[kRI];
            d.intenScale = cols[kRS];
            d.intenScalePhilips = cols[kSS];
        } else {
            if (parVers >= 40) {
				if ((d.xyzDim[1] != cols[kXdim]) || (d.xyzDim[2] != cols[kYdim]) || (d.bitsAllocated != cols[kBitsPerVoxel]) ) {
					printError("Slice dimensions or bit depth varies %s\n", parname);
					printError("xDim %dv%d yDim %dv%d bits  %dv%d\n", d.xyzDim[1],(int)cols[kXdim], d.xyzDim[2], (int)cols[kYdim], d.bitsAllocated, (int)cols[kBitsPerVoxel]);
					return d;
				}
            }
            if ((d.intenScale != cols[kRS]) || (d.intenIntercept != cols[kRI]))
                isIntenScaleVaries = true;
        }
        if (cols[kImageType] == 0) d.isHasMagnitude = true;
        if (cols[kImageType] != 0) d.isHasPhase = true;
        if ((isSameFloat(cols[kImageType],18)) && (!isTypeWarning)) {
        	printWarning("Field map in Hz will be saved as the 'real' image.\n");
        	isTypeWarning = true;
        } else if (((cols[kImageType] < 0.0) || (cols[kImageType] > 4.0)) && (!isTypeWarning)) {
        	printError("Unknown type %g: not magnitude[0], real[1], imaginary[2] or phase[3].\n", cols[kImageType]);
        	isTypeWarning = true;
        }
        if (cols[kDyn] > maxDyn) maxDyn = (int) cols[kDyn];
        if (cols[kDyn] < minDyn) minDyn = (int) cols[kDyn];
        if (cols[kDyn] < prevDyn) dynNotAscending = true;
        prevDyn = cols[kDyn];
        if (cols[kDynTime] > maxDynTime) maxDynTime = cols[kDynTime];
        if (cols[kDynTime] < minDynTime) minDynTime = cols[kDynTime];
        if (cols[kEcho] > maxEcho) maxEcho = cols[kEcho];
        if (cols[kCardiac] > maxCardiac) maxCardiac = cols[kCardiac];
        if ((cols[kEcho] == 1) && (cols[kDyn] == 1) && (cols[kCardiac] == 1) && (cols[kGradientNumber] == 1)) {
			if (cols[kSlice] == 1) {
				d.patientPosition[1] = cols[kPositionRL];
            	d.patientPosition[2] = cols[kPositionAP];
            	d.patientPosition[3] = cols[kPositionFH];
			}
			patientPositionNumPhilips++;
		}
		if (true) { //for every slice
			int slice = (int)cols[kSlice];
			if (slice < minSlice) minSlice = slice;
			if (slice > maxSlice) {
				maxSlice = slice;
				d.patientPositionLast[1] = cols[kPositionRL];
            	d.patientPositionLast[2] = cols[kPositionAP];
            	d.patientPositionLast[3] = cols[kPositionFH];
			}
			int volStep =  maxNumberOfDynamics;
			int vol = ((int)cols[kDyn] - 1);
			#ifdef old
				int gradDynVol = (int)cols[kGradientNumber] - 1;
				if (gradDynVol < 0) gradDynVol = 0; //old PAREC without cols[kGradientNumber]
				vol = vol + (volStep * (gradDynVol));
				if (vol < 0) vol = 0;
				volStep = volStep * maxNumberOfGradientOrients;
				int bval = (int)cols[kbvalNumber];
				if (bval > 2) //b=0 is 0, b=1000 is 1, b=2000 is 2 - b=0 does not have multiple directions
					bval = bval - 1;
				else
					bval = 1;
				//if (slice == 1) printMessage("bVal %d bVec %d isADC %d nbVal %d nGrad %d\n",(int) cols[kbvalNumber], (int)cols[kGradientNumber], isADC, maxNumberOfDiffusionValues, maxNumberOfGradientOrients);
				vol = vol  + (volStep * (bval- 1));
				volStep = volStep * (maxNumberOfDiffusionValues-1);
				if (isADC)
					vol = volStep + (bval-1);
			#else
				if (maxNumberOfDiffusionValues > 1) {
					int grad = (int)cols[kGradientNumber] - 1;
					if (grad < 0) grad = 0; //old v4 does not have this tag
					int bval = (int)cols[kbvalNumber] - 1;
					if (bval < 0) bval = 0; //old v4 does not have this tag
					if (isADC)
						vol = vol + (volStep * maxNumberOfDiffusionValues * maxNumberOfGradientOrients) +bval;
					else
						vol = vol + (volStep * grad) + (bval * maxNumberOfGradientOrients);

					volStep = volStep * (maxNumberOfDiffusionValues+1) * maxNumberOfGradientOrients;
					//if (slice == 1) printMessage("vol %d step %d bVal %d bVec %d isADC %d nbVal %d nGrad %d\n", vol, volStep, (int) cols[kbvalNumber], (int)cols[kGradientNumber], isADC, maxNumberOfDiffusionValues, maxNumberOfGradientOrients);
				}
			#endif
			vol = vol  + (volStep * ((int)cols[kEcho] - 1));
			volStep = volStep * maxNumberOfEchoes;
			vol = vol  + (volStep * ((int)cols[kCardiac] - 1));
			volStep = volStep * maxNumberOfCardiacPhases;
			int ASL = (int)cols[kASL];
			if (ASL < 1) ASL = 1;
			vol = vol  + (volStep * (ASL - 1));
			volStep = volStep * maxNumberOfLabels;
			//if ((int)cols[kSequence] > 0)
			int seq = (int)cols[kSequence];
			if (seq1 < 0) seq1 = seq;
			if (seq > maxSeq) maxSeq = seq;
			if (seq != seq1) {//sequence varies within this PAR file
				if (!isSequenceWarning) {
					isSequenceWarning =true;
					printWarning("'scanning sequence' column varies within a single file. This behavior is not described at the top of the header.\n");
				}
        		vol = vol  + (volStep * 1);
				volStep = volStep * 2;
			}
			//if (slice == 1)  printMessage("%d\t%d\t%d\t%d\t%d\n", isADC,(int)cols[kbvalNumber], (int)cols[kGradientNumber], bval, vol);
			if (vol > maxVol) maxVol = vol;
			bool isReal = (cols[kImageType] == 1);
			bool isImaginary = (cols[kImageType] == 2);
			bool isPhase = (cols[kImageType] == 3);
			if (cols[kImageType] == 4) {
				if (!isType4Warning) {
        			printWarning("Unknown image type (4). Be aware the 'phase' image is of an unknown type.\n");
        			isType4Warning = true;
        		}
				isPhase = true; //2019
			}
			if ((cols[kImageType] < 0.0) || (cols[kImageType] > 3.0))
				isReal = true; //<- this is not correct, kludge for bug in ROGERS_20180526_WIP_B0_NS_8_1.PAR
			if (isReal) vol += num3DExpected;
			if (isImaginary) vol += (2*num3DExpected);
			if (isPhase) vol += (3*num3DExpected);
			if (vol >= kMaxDTI4D) {
					printError("Use dicm2nii or increase kMaxDTI4D (currently %d)to be more than %d\n", kMaxDTI4D, kMaxImageType*num2DExpected);
					printMessage("  slices*grad*bval*cardiac*echo*dynamic*mix*label = %d*%d*%d*%d*%d*%d*%d*%d\n",
            		d.xyzDim[3],  maxNumberOfGradientOrients, maxNumberOfDiffusionValues,
    		maxNumberOfCardiacPhases, maxNumberOfEchoes, maxNumberOfDynamics, maxNumberOfMixes, maxNumberOfLabels);
					free (cols);
					return d;
	 		}
	 		// dti4D->S[vol].V[0] = cols[kbval];
			//dti4D->gradDynVol[vol] = gradDynVol;
			dti4D->TE[vol] = cols[kTEcho];
			if (isSameFloatGE(cols[kTEcho], 0))
				dti4D->TE[vol] = TE;//kludge for cols[kImageType]==18 where TE set as 0
			else
				TE = cols[kTEcho];
            dti4D->triggerDelayTime[vol] = cols[kTriggerTime];
			if (dti4D->TE[vol] < 0) dti4D->TE[vol] = 0; //used to detect sparse volumes
			dti4D->intenIntercept[vol] = cols[kRI];
			dti4D->intenScale[vol] = cols[kRS];
			dti4D->intenScalePhilips[vol] = cols[kSS];
			dti4D->isReal[vol] = isReal;
			dti4D->isImaginary[vol] = isImaginary;
			dti4D->isPhase[vol] = isPhase;
			if ((maxNumberOfGradientOrients > 1) && (parVers > 40)) {
				dti4D->S[vol].V[0] = cols[kbval];
    			dti4D->S[vol].V[1] = cols[kv1];
    			dti4D->S[vol].V[2] = cols[kv2];
    			dti4D->S[vol].V[3] = cols[kv3];
    			if ((vol+1) > d.CSA.numDti)
    				d.CSA.numDti = vol+1;
			}
			//if (slice == 1) printWarning("%d\n", (int)cols[kEcho]);
			slice = slice + (vol * d.xyzDim[3]);
			//offset images by type: mag+0,real+1, imag+2,phase+3
			//if (cols[kImageType] != 0) //yikes - phase maps!
			//	slice = slice + numExpected;
			//printWarning("%d\t%d\n", slice -1, numSlice2D);
            if ((slice >= 0)  && (slice < kMaxSlice2D)  && (numSlice2D < kMaxSlice2D) && (numSlice2D >= 0)) {
				dti4D->sliceOrder[slice -1] = numSlice2D;
				//printMessage("%d\t%d\t%d\n", numSlice2D, slice, (int)cols[kSlice],(int)vol);
			}
			numSlice2D++;
        }
        //printMessage("%f %f %lu\n",cols[9],cols[kGradientNumber], strlen(buff))
        p = fgets (buff, LINESZ, fp);//get next line
    }
    free (cols);
    fclose (fp);
    if ((parVers <= 0) || (numSlice2D < 1)) {
		printError("Invalid PAR format header (unable to detect version or slices) %s\n", parname);
    	return d;
    }
    d.manufacturer = kMANUFACTURER_PHILIPS;
    d.isValid = true;
    d.isSigned = true;
    //remove unused volumes - this will happen if unless we have all 4 image types: real, imag, mag, phase
    maxVol = 0;
    for (int i = 0; i < kMaxDTI4D; i++) {
    	if (dti4D->TE[i] > -1.0) {
			dti4D->TE[maxVol] = dti4D->TE[i];
			dti4D->triggerDelayTime[maxVol] = dti4D->triggerDelayTime[i];
			dti4D->intenIntercept[maxVol] = dti4D->intenIntercept[i];
			dti4D->intenScale[maxVol] = dti4D->intenScale[i];
			dti4D->intenScalePhilips[maxVol] = dti4D->intenScalePhilips[i];
			dti4D->isReal[maxVol] = dti4D->isReal[i];
			dti4D->isImaginary[maxVol] = dti4D->isImaginary[i];
			dti4D->isPhase[maxVol] = dti4D->isPhase[i];
			dti4D->S[maxVol].V[0] = dti4D->S[i].V[0];
			dti4D->S[maxVol].V[1] = dti4D->S[i].V[1];
			dti4D->S[maxVol].V[2] = dti4D->S[i].V[2];
			dti4D->S[maxVol].V[3] = dti4D->S[i].V[3];
    		maxVol = maxVol + 1;
    	}
    }
    if (d.CSA.numDti > 0) d.CSA.numDti = maxVol; //e.g. gradient 2 can skip B=0 but include isotropic
    //remove unused slices - this will happen if unless we have all 4 image types: real, imag, mag, phase
    if (numSlice2D > kMaxSlice2D) { //check again after reading, as top portion of header does not report image types or isotropics
    	printError("Increase kMaxSlice2D from %d to at least %d (or use dicm2nii).\n", kMaxSlice2D, numSlice2D);
        d.isValid = false;
    }
    int slice = 0;
    for (int i = 0; i < kMaxSlice2D; i++) {
        if (dti4D->sliceOrder[i] > -1) { //this slice was populated
        	dti4D->sliceOrder[slice]	= dti4D->sliceOrder[i];
        	slice = slice + 1;
        }
    }
    if (slice != numSlice2D) {
    	printError("Catastrophic error: found %d but expected %d slices. %s\n", slice, numSlice2D, parname);
        printMessage("  slices*grad*bval*cardiac*echo*dynamic*mix*labels = %d*%d*%d*%d*%d*%d*%d*%d\n",
            		d.xyzDim[3],  maxNumberOfGradientOrients, maxNumberOfDiffusionValues,
    		maxNumberOfCardiacPhases, maxNumberOfEchoes, maxNumberOfDynamics, maxNumberOfMixes,maxNumberOfLabels);
        d.isValid = false;
    }
    d.isScaleOrTEVaries = true;
	if (numSlice2D > kMaxSlice2D) {
		printError("Overloaded slice re-ordering. Number of slices (%d) exceeds kMaxSlice2D (%d)\n", numSlice2D, kMaxSlice2D);
		dti4D->sliceOrder[0] = -1;
	}
	if ((maxSlice-minSlice+1) != d.xyzDim[3]) {
		int numSlice = (maxSlice - minSlice)+1;
		printWarning("Expected %d slices, but found %d (%d..%d). %s\n", d.xyzDim[3], numSlice, minSlice, maxSlice, parname);
		if (numSlice <= 0) d.isValid = false;
		d.xyzDim[3] = numSlice;
		num2DExpected = d.xyzDim[3] * num3DExpected;
	}
    if ((maxBValue <= 0.0f) && (maxDyn > minDyn) && (maxDynTime > minDynTime)) { //use max vs min Dyn instead of && (d.CSA.numDti > 1)
    	int numDyn = (maxDyn - minDyn)+1;
    	if (numDyn != maxNumberOfDynamics) {
    		printWarning("Expected %d dynamics, but found %d (%d..%d).\n", maxNumberOfDynamics, numDyn, minDyn, maxDyn);
			maxNumberOfDynamics = numDyn;
			num3DExpected = maxNumberOfGradientOrients * maxNumberOfDiffusionValues * maxNumberOfLabels
	 				* maxNumberOfCardiacPhases * maxNumberOfEchoes * maxNumberOfDynamics * maxNumberOfMixes;
            num2DExpected = d.xyzDim[3] * num3DExpected;
    	}
    	float TRms =  1000.0f * (maxDynTime - minDynTime) / (float)(numDyn-1); //-1 for fence post
    	//float TRms =  1000.0f * (maxDynTime - minDynTime) / (float)(d.CSA.numDti-1);
    	if (fabs(TRms - d.TR) > 0.005f)
    		printWarning("Reported TR=%gms, measured TR=%gms (prospect. motion corr.?)\n", d.TR, TRms);
    	d.TR = TRms;
    }
    if ((isTypeWarning) && ((numSlice2D % num2DExpected) != 0) && ((numSlice2D % d.xyzDim[3]) == 0) ) {
    	num2DExpected = numSlice2D;
    }
    if ( ((numSlice2D % num2DExpected) != 0) && ((numSlice2D % d.xyzDim[3]) == 0) ) {
    	num2DExpected = d.xyzDim[3] * (int)(numSlice2D / d.xyzDim[3]);
    	if (!ADCwarning) printWarning("More volumes than described in header (ADC or isotropic?)\n");
    }
    if ((numSlice2D % num2DExpected) != 0) {
    	printMessage("Found %d slices, but expected divisible by %d: slices*grad*bval*cardiac*echo*dynamic*mix*labels = %d*%d*%d*%d*%d*%d*%d*%d %s\n", numSlice2D, num2DExpected,
    		d.xyzDim[3],  maxNumberOfGradientOrients, maxNumberOfDiffusionValues,
    		maxNumberOfCardiacPhases, maxNumberOfEchoes, maxNumberOfDynamics, maxNumberOfMixes,maxNumberOfLabels, parname);
    	d.isValid = false;
    }
    if (dynNotAscending) {
        printWarning("PAR file volumes not saved in ascending temporal order (please check re-ordering)\n");
    }
    if ((slice % d.xyzDim[3]) != 0) {
        printError("Total number of slices (%d) not divisible by slices per 3D volume (%d) [acquisition aborted]. Try dicm2nii or R2AGUI: %s\n", slice, d.xyzDim[3], parname);
        d.isValid = false;
        return d;
    }
    d.xyzDim[4] = slice/d.xyzDim[3];
    d.locationsInAcquisition = d.xyzDim[3];
    if (ADCwarning)
        printWarning("PAR/REC dataset includes derived (isotropic, ADC, etc) map(s) that could disrupt analysis. Please remove volume and ensure vectors are reported correctly\n");
    if (isIntenScaleVaries)
       printWarning("Intensity slope/intercept varies between slices! [check resulting images]\n");
    if ((isVerbose) && (d.isValid)) {
		printMessage("  slices*grad*bval*cardiac*echo*dynamic*mix*labels = %d*%d*%d*%d*%d*%d*%d*%d\n",
            		d.xyzDim[3],  maxNumberOfGradientOrients, maxNumberOfDiffusionValues,
    		maxNumberOfCardiacPhases, maxNumberOfEchoes, maxNumberOfDynamics, maxNumberOfMixes,maxNumberOfLabels);
    }
    if ((d.xyzDim[3] > 1) && (minSlice == 1) && (maxSlice > minSlice)) { //issue 273
		float dx[4];
		dx[1] = (d.patientPosition[1]-d.patientPositionLast[1]);
		dx[2] = (d.patientPosition[2]-d.patientPositionLast[2]);
		dx[3] = (d.patientPosition[3]-d.patientPositionLast[3]);
		//compute error using 3D pythagorean theorm
		float sliceMM = sqrt(pow(dx[1],2)+pow(dx[2],2)+pow(dx[3],2)  );
		sliceMM = sliceMM / (maxSlice - minSlice);
		if (!(isSameFloatGE(sliceMM, d.xyzMM[3]))) {
			//if (d.xyzMM[3] > 0.0)
			printWarning("Distance between slices reported by slice gap+thick does not match estimate from slice positions (issue 273).\n");
			d.xyzMM[3] = sliceMM;
		}
    } //issue 273
    printMessage("Done reading PAR header version %.1f, with %d slices\n", (float)parVers/10, numSlice2D);
	//see Xiangrui Li 's dicm2nii (also BSD license)
	// http://www.mathworks.com/matlabcentral/fileexchange/42997-dicom-to-nifti-converter
	// Rotation order and signs are figured out by trial and error, not 100% sure
	float d2r = (float) (M_PI/180.0);
	vec3 ca = setVec3(cos(d.angulation[1]*d2r),cos(d.angulation[2]*d2r),cos(d.angulation[3]*d2r));
	vec3 sa = setVec3(sin(d.angulation[1]*d2r),sin(d.angulation[2]*d2r),sin(d.angulation[3]*d2r));
	mat33 rx,ry,rz;
    LOAD_MAT33(rx,1.0f, 0.0f, 0.0f, 0.0f, ca.v[0], -sa.v[0], 0.0f, sa.v[0], ca.v[0]);
    LOAD_MAT33(ry, ca.v[1], 0.0f, sa.v[1], 0.0f, 1.0f, 0.0f, -sa.v[1], 0.0f, ca.v[1]);
    LOAD_MAT33(rz, ca.v[2], -sa.v[2], 0.0f, sa.v[2], ca.v[2], 0.0f, 0.0f, 0.0f, 1.0f);
    mat33 R = nifti_mat33_mul( rx,ry );
    R = nifti_mat33_mul( R,rz);
    ivec3 ixyz = setiVec3(1,2,3);
    if (d.sliceOrient == kSliceOrientSag) {
        ixyz = setiVec3(2,3,1);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                if (c != 1) R.m[r][c] = -R.m[r][c]; //invert first and final columns
    }else if (d.sliceOrient == kSliceOrientCor) {
        ixyz = setiVec3(1,3,2);
        for (int r = 0; r < 3; r++)
            R.m[r][2] = -R.m[r][2]; //invert rows of final column
    }
    R = nifti_mat33_reorder_cols(R,ixyz); //dicom rotation matrix
    d.orient[1] = R.m[0][0]; d.orient[2] = R.m[1][0]; d.orient[3] = R.m[2][0];
    d.orient[4] = R.m[0][1]; d.orient[5] = R.m[1][1]; d.orient[6] = R.m[2][1];
    mat33 diag;
    LOAD_MAT33(diag, d.xyzMM[1],0.0f,0.0f,  0.0f,d.xyzMM[2],0.0f,  0.0f,0.0f, d.xyzMM[3]);
    R= nifti_mat33_mul( R, diag );
    mat44 R44;
    LOAD_MAT44(R44, R.m[0][0],R.m[0][1],R.m[0][2],d.stackOffcentre[1],
               R.m[1][0],R.m[1][1],R.m[1][2],d.stackOffcentre[2],
               R.m[2][0],R.m[2][1],R.m[2][2],d.stackOffcentre[3]);
    vec3 x;
    if (parVers > 40) //guess
        x = setVec3(((float)d.xyzDim[1]-1)/2,((float)d.xyzDim[2]-1)/2,((float)d.xyzDim[3]-1)/2);
    else
        x = setVec3((float)d.xyzDim[1]/2,(float)d.xyzDim[2]/2,((float)d.xyzDim[3]-1)/2);
    mat44 eye;
    LOAD_MAT44(eye, 1.0f,0.0f,0.0f,x.v[0],
               0.0f,1.0f,0.0f,x.v[1],
               0.0f,0.0f,1.0f,x.v[2]);
    eye= nifti_mat44_inverse( eye ); //we wish to compute R/eye, so compute invEye and calculate R*invEye
    R44= nifti_mat44_mul( R44 , eye );
    vec4 y;
    y.v[0]=0.0f; y.v[1]=0.0f; y.v[2]=(float) d.xyzDim[3]-1.0f; y.v[3]=1.0f;
    y= nifti_vect44mat44_mul(y, R44 );
    int iOri = 2; //for axial, slices are 3rd dimenson (indexed from 0) (k)
    if (d.sliceOrient == kSliceOrientSag) iOri = 0; //for sagittal, slices are 1st dimension (i)
    if (d.sliceOrient == kSliceOrientCor) iOri = 1; //for coronal, slices are 2nd dimension (j)
    if (d.xyzDim[3] > 1) { //detect and fix Philips Bug
		//Est: assuming "image offcentre (ap,fh,rl in mm )" is correct
		float stackOffcentreEst[4];
		stackOffcentreEst[1] = (d.patientPosition[1]+d.patientPositionLast[1]) * 0.5;
		stackOffcentreEst[2] = (d.patientPosition[2]+d.patientPositionLast[2]) * 0.5;
		stackOffcentreEst[3] = (d.patientPosition[3]+d.patientPositionLast[3]) * 0.5;
		//compute error using 3D pythagorean theorm
		stackOffcentreEst[0] = sqrt(pow(stackOffcentreEst[1]-d.stackOffcentre[1],2)+pow(stackOffcentreEst[2]-d.stackOffcentre[2],2)+pow(stackOffcentreEst[3]-d.stackOffcentre[3],2)  );
		//Est: assuming "image offcentre (ap,fh,rl in mm )" is stored in order rl,ap,fh
		float stackOffcentreRev[4];
		stackOffcentreRev[1] = (d.patientPosition[2]+d.patientPositionLast[2]) * 0.5;
		stackOffcentreRev[2] = (d.patientPosition[3]+d.patientPositionLast[3]) * 0.5;
		stackOffcentreRev[3] = (d.patientPosition[1]+d.patientPositionLast[1]) * 0.5;
		//compute error using 3D pythagorean theorm
		stackOffcentreRev[0] = sqrt(pow(stackOffcentreRev[1]-d.stackOffcentre[1],2)+pow(stackOffcentreRev[2]-d.stackOffcentre[2],2)+pow(stackOffcentreRev[3]-d.stackOffcentre[3],2)  );
		//detect, report and fix error
		if ((stackOffcentreEst[0] > 1.0) && (stackOffcentreRev[0] < stackOffcentreEst[0])) {
			//error detected: the ">1.0" handles the low precision of the "Off Centre" values
			printMessage("Order of 'image offcentre (ap,fh,rl in mm )' appears incorrect (assuming rl,ap,fh)\n");
			printMessage(" err[ap,fh,rl]= %g (%g %g %g) \n",stackOffcentreEst[0],stackOffcentreEst[1],stackOffcentreEst[2],stackOffcentreEst[3]);
			printMessage(" err[rl,ap,fh]= %g (%g %g %g) \n",stackOffcentreRev[0],stackOffcentreRev[1],stackOffcentreRev[2],stackOffcentreRev[3]);
			printMessage(" orient\t%d\tOffCentre 1st->mid->nth\t%g\t%g\t%g\t->\t%g\t%g\t%g\t->\t%g\t%g\t%g\t=\t%g\t%s\n",iOri,
				d.patientPosition[1],d.patientPosition[2],d.patientPosition[3],
				d.stackOffcentre[1], d.stackOffcentre[2], d.stackOffcentre[3],
				d.patientPositionLast[1],d.patientPositionLast[2],d.patientPositionLast[3],(d.patientPosition[iOri+1] - d.patientPositionLast[iOri+1]), parname);
			//correct patientPosition
			for (int i = 1; i < 4; i++)
				stackOffcentreRev[i] = d.patientPosition[i];
			d.patientPosition[1] = stackOffcentreRev[2];
			d.patientPosition[2] = stackOffcentreRev[3];
			d.patientPosition[3] = stackOffcentreRev[1];
			//correct patientPositionLast
			for (int i = 1; i < 4; i++)
				stackOffcentreRev[i] = d.patientPositionLast[i];
			d.patientPositionLast[1] = stackOffcentreRev[2];
			d.patientPositionLast[2] = stackOffcentreRev[3];
			d.patientPositionLast[3] = stackOffcentreRev[1];
		} //if bug: report and fix
	} //if 3D data
	bool flip = false;
	//assume head first supine
	if ((iOri == 0) && (((d.patientPosition[iOri+1] - d.patientPositionLast[iOri+1]) > 0))) flip = true; //6/2018 : TODO, not sure if this is >= or >
	if ((iOri == 1) && (((d.patientPosition[iOri+1] - d.patientPositionLast[iOri+1]) <= 0))) flip = true; //<= not <, leslie_dti_6_1.PAR
 	if ((iOri == 2) && (((d.patientPosition[iOri+1] - d.patientPositionLast[iOri+1]) <= 0))) flip = true; //<= not <, see leslie_dti_3_1.PAR
	if (flip) {
	//if ((d.patientPosition[iOri+1] - d.patientPositionLast[iOri+1]) < 0) {
	//if  (( (y.v[iOri]-R44.m[iOri][3])>0 ) == ( (y.v[iOri]-d.stackOffcentre[iOri+1])>0 ) ) {
		d.patientPosition[1] = R44.m[0][3];
		d.patientPosition[2] = R44.m[1][3];
		d.patientPosition[3] = R44.m[2][3];
		d.patientPositionLast[1] = y.v[0];
		d.patientPositionLast[2] = y.v[1];
		d.patientPositionLast[3] = y.v[2];
		//printWarning(" Flipping slice order: please verify %s\n", parname);
	}else {
		//printWarning(" NOT Flipping slice order: please verify %s\n", parname);
		d.patientPosition[1] = y.v[0];
		d.patientPosition[2] = y.v[1];
		d.patientPosition[3] = y.v[2];
		d.patientPositionLast[1] = R44.m[0][3];
		d.patientPositionLast[2] = R44.m[1][3];
		d.patientPositionLast[3] = R44.m[2][3];
	}
    //finish up
    changeExt (parname, "REC");
    #ifndef _MSC_VER //Linux is case sensitive, #include <unistd.h>
    if( access( parname, F_OK ) != 0 ) changeExt (parname, "rec");
	#endif
    d.locationsInAcquisition = d.xyzDim[3];
    d.imageStart = 0;
    if (d.CSA.numDti >= kMaxDTI4D) {
        printError("Unable to convert DTI [increase kMaxDTI4D] found %d directions\n", d.CSA.numDti);
        d.CSA.numDti = 0;
    };
    //check if dimensions vary
    if (maxVol > 0) { //maxVol indexed from 0
		for (int i = 1; i <= maxVol; i++) {
			//if (dti4D->gradDynVol[i] > d.maxGradDynVol) d.maxGradDynVol = dti4D->gradDynVol[i];
			if (dti4D->intenIntercept[i] != dti4D->intenIntercept[0]) d.isScaleOrTEVaries = true;
			if (dti4D->intenScale[i] != dti4D->intenScale[0]) d.isScaleOrTEVaries = true;
			if (dti4D->intenScalePhilips[i] != dti4D->intenScalePhilips[0]) d.isScaleOrTEVaries = true;
			if (dti4D->isPhase[i] != dti4D->isPhase[0]) d.isScaleOrTEVaries = true;
			if (dti4D->isReal[i] != dti4D->isReal[0]) d.isScaleOrTEVaries = true;
			if (dti4D->isImaginary[i] != dti4D->isImaginary[0]) d.isScaleOrTEVaries = true;
			if (dti4D->triggerDelayTime[i] != dti4D->triggerDelayTime[0]) d.isScaleOrTEVaries = true;
		}
		//if (d.isScaleOrTEVaries)
		//	printWarning("Varying dimensions (echoes, phase maps, intensity scaling) will require volumes to be saved separately (hint: you may prefer dicm2nii output)\n");
    }
    //if (d.CSA.numDti > 1)
    //	for (int i = 0; i < d.CSA.numDti; i++)
    //		printMessage("%d\tb=\t%g\tv=\t%g\t%g\t%g\n",i,dti4D->S[i].V[0],dti4D->S[i].V[1],dti4D->S[i].V[2],dti4D->S[i].V[3]);
    //check DTI makes sense
    if (d.CSA.numDti > 1) {
    	bool v1varies = false;
    	bool v2varies = false;
    	bool v3varies = false;
    	for (int i = 1; i < d.CSA.numDti; i++) {
    		if (dti4D->S[0].V[1] != dti4D->S[i].V[1]) v1varies = true;
    		if (dti4D->S[0].V[2] != dti4D->S[i].V[2]) v2varies = true;
    		if (dti4D->S[0].V[3] != dti4D->S[i].V[3]) v3varies = true;
    	}
    	if ((!v1varies) || (!v2varies) || (!v3varies))
    		 printError("Bizarre b-vectors %s\n", parname);
    }
    if ((maxEcho > 1) || (maxCardiac > 1)) printWarning("Multiple Echo (%d) or Cardiac (%d). Carefully inspect output\n", maxEcho,  maxCardiac);
    if ((maxEcho > 1) || (maxCardiac > 1)) d.isScaleOrTEVaries = true;
    return d;
} //nii_readParRec()

size_t nii_SliceBytes(struct nifti_1_header hdr) {
    //size of 2D slice
    size_t imgsz = hdr.bitpix/8;
    for (int i = 1; i < 3; i++)
        if (hdr.dim[i]  > 1)
            imgsz = imgsz * hdr.dim[i];
    return imgsz;
} //nii_SliceBytes()

size_t nii_ImgBytes(struct nifti_1_header hdr) {
    size_t imgsz = hdr.bitpix/8;
    for (int i = 1; i < 8; i++)
        if (hdr.dim[i]  > 1)
            imgsz = imgsz * hdr.dim[i];
    return imgsz;
} //nii_ImgBytes()

//unsigned char * nii_demosaic(unsigned char* inImg, struct nifti_1_header *hdr, int nMosaicSlices, int ProtocolSliceNumber1) {
unsigned char * nii_demosaic(unsigned char* inImg, struct nifti_1_header *hdr, int nMosaicSlices, bool isUIH) {
    //demosaic http://nipy.org/nibabel/dicom/dicom_mosaic.html
    if (nMosaicSlices < 2) return inImg;
    //Byte inImg[ [img length] ];
    //[img getBytes:&inImg length:[img length]];
    int nCol = (int) ceil(sqrt((double) nMosaicSlices));
    int nRow = nCol;
    //n.b. Siemens store 20 images as 5x5 grid, UIH as 5rows, 4 Col https://github.com/rordenlab/dcm2niix/issues/225
    if (isUIH)
    	nRow = ceil((float)nMosaicSlices/(float)nCol);
    //printf("%d = %dx%d\n", nMosaicSlices, nCol, nRow);
    int colBytes = hdr->dim[1]/nCol * hdr->bitpix/8;
    int lineBytes = hdr->dim[1] * hdr->bitpix/8;
    int rowBytes = hdr->dim[1] * hdr->dim[2]/nRow * hdr->bitpix/8;
    int col = 0;
    int row = 0;
    int lOutPos = 0;
    hdr->dim[1] = hdr->dim[1]/nCol;
    hdr->dim[2] = hdr->dim[2]/nRow;
    hdr->dim[3] = nMosaicSlices;
    size_t imgsz = nii_ImgBytes(*hdr);
    unsigned char *outImg = (unsigned char *)malloc(imgsz);
    for (int m=1; m <= nMosaicSlices; m++) {
        int lPos = (row * rowBytes) + (col * colBytes);
        for (int y = 0; y < hdr->dim[2]; y++) {
            memcpy(&outImg[lOutPos], &inImg[lPos], colBytes); // dest, src, bytes
            lPos += lineBytes;
            lOutPos +=colBytes;
        }
        col ++;
        if (col >= nCol) {
            row ++;
            col = 0;
        } //start new column
    } //for m = each mosaic slice
    free(inImg);
    return outImg;
} // nii_demosaic()

unsigned char * nii_flipImgY(unsigned char* bImg, struct nifti_1_header *hdr){
    //DICOM row order opposite from NIfTI
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    size_t lineBytes = hdr->dim[1] * hdr->bitpix/8;
    if ((hdr->datatype == DT_RGB24) && (hdr->bitpix == 24) && (hdr->intent_code == NIFTI_INTENT_NONE)) {
        //we use the intent code to indicate planar vs triplet...
        lineBytes = hdr->dim[1];
        dim3to7 = dim3to7 * 3;
    } //rgb data saved planar (RRR..RGGGG..GBBB..B
//#ifdef _MSC_VER
	unsigned char * line = (unsigned char *)malloc(sizeof(unsigned char) * (lineBytes));
//#else
//	unsigned char line[lineBytes];
//#endif
    size_t sliceBytes = hdr->dim[2] * lineBytes;
    int halfY = hdr->dim[2] / 2; //note truncated toward zero, so halfY=2 regardless of 4 or 5 columns
    for (int sl = 0; sl < dim3to7; sl++) { //for each 2D slice
        size_t slBottom = (size_t)sl*sliceBytes;
        size_t slTop = (((size_t)sl+1)*sliceBytes)-lineBytes;
        for (int y = 0; y < halfY; y++) {
            //swap order of lines
            memcpy(line, &bImg[slBottom], lineBytes);//memcpy(&line, &bImg[slBottom], lineBytes);
            memcpy(&bImg[slBottom], &bImg[slTop], lineBytes);
            memcpy(&bImg[slTop], line, lineBytes);//tpx memcpy(&bImg[slTop], &line, lineBytes);
            slTop -= lineBytes;
            slBottom += lineBytes;
        } //for y
    } //for each slice
//#ifdef _MSC_VER
	free(line);
//#endif
    return bImg;
} // nii_flipImgY()

unsigned char * nii_flipImgZ(unsigned char* bImg, struct nifti_1_header *hdr){
    //DICOM row order opposite from NIfTI
    int halfZ = hdr->dim[3] / 2; //note truncated toward zero, so halfY=2 regardless of 4 or 5 columns
    if (halfZ < 1) return bImg;
    int dim4to7 = 1;
    for (int i = 4; i < 8; i++)
        if (hdr->dim[i] > 1) dim4to7 = dim4to7 * hdr->dim[i];
    size_t sliceBytes = hdr->dim[1] * hdr->dim[2] * hdr->bitpix/8;
    size_t volBytes = sliceBytes * hdr->dim[3];
	unsigned char * slice = (unsigned char *)malloc(sizeof(unsigned char) * (sliceBytes));
    for (int vol = 0; vol < dim4to7; vol++) { //for each 2D slice
        size_t slBottom = vol*volBytes;
        size_t slTop = ((vol+1)*volBytes)-sliceBytes;
        for (int z = 0; z < halfZ; z++) {
            //swap order of lines
            memcpy(slice, &bImg[slBottom], sliceBytes); //TPX memcpy(&slice, &bImg[slBottom], sliceBytes);
            memcpy(&bImg[slBottom], &bImg[slTop], sliceBytes);
            memcpy(&bImg[slTop], slice, sliceBytes); //TPX
            slTop -= sliceBytes;
            slBottom += sliceBytes;
        } //for Z
    } //for each volume
	free(slice);
    return bImg;
} // nii_flipImgZ()

/*unsigned char * nii_reorderSlices(unsigned char* bImg, struct nifti_1_header *h, struct TDTI4D *dti4D){
    //flip slice order - Philips scanners can save data in non-contiguous order
    //if ((h->dim[3] < 2) || (h->dim[4] > 1)) return bImg;
    return bImg;
    if (h->dim[3] < 2) return bImg;
    if (h->dim[3] >= kMaxDTI4D) {
    	printWarning("Unable to reorder slices (%d > %d)\n", h->dim[3], kMaxDTI4D);
    	return bImg;
    }
    printError("OBSOLETE<<< Slices not spatially contiguous: please check output [new feature]\n"); return bImg;
    int dim4to7 = 1;
    for (int i = 4; i < 8; i++)
        if (h->dim[i] > 1) dim4to7 = dim4to7 * h->dim[i];
    int sliceBytes = h->dim[1] * h->dim[2] * h->bitpix/8;
    if (sliceBytes < 0)  return bImg;
    size_t volBytes = sliceBytes * h->dim[3];
    unsigned char *srcImg = (unsigned char *)malloc(volBytes);
    //printMessage("Reordering %d volumes\n", dim4to7);
    for (int v = 0; v < dim4to7; v++) {
    //for (int v = 0; v < 1; v++) {

    	size_t volStart = v * volBytes;
    	memcpy(&srcImg[0], &bImg[volStart], volBytes); //dest, src, size
    	for (int z = 0; z < h->dim[3]; z++) { //for each slice
			int src = dti4D->S[z].sliceNumberMrPhilips - 1; //-1 as Philips indexes slices from 1 not 0
			if ((v > 0) && (dti4D->S[0].sliceNumberMrPhilipsVol2 >= 0))
				src = dti4D->S[z].sliceNumberMrPhilipsVol2 - 1;
			//printMessage("Reordering volume %d slice %d\n", v, dti4D->S[z].sliceNumberMrPhilips);
			if ((src < 0) || (src >= h->dim[3])) continue;
			memcpy(&bImg[volStart+(src*sliceBytes)], &srcImg[z*sliceBytes], sliceBytes); //dest, src, size
    	}
    }
    free(srcImg);
    return bImg;
}// nii_reorderSlices()
*/
unsigned char * nii_flipZ(unsigned char* bImg, struct nifti_1_header *h){
    //flip slice order
    if (h->dim[3] < 2) return bImg;
    mat33 s;
    mat44 Q44;
    LOAD_MAT33(s,h->srow_x[0],h->srow_x[1],h->srow_x[2], h->srow_y[0],h->srow_y[1],h->srow_y[2],
               h->srow_z[0],h->srow_z[1],h->srow_z[2]);
    LOAD_MAT44(Q44,h->srow_x[0],h->srow_x[1],h->srow_x[2],h->srow_x[3],
               h->srow_y[0],h->srow_y[1],h->srow_y[2],h->srow_y[3],
               h->srow_z[0],h->srow_z[1],h->srow_z[2],h->srow_z[3]);
    vec4 v= setVec4(0.0f,0.0f,(float) h->dim[3]-1.0f);
    v = nifti_vect44mat44_mul(v, Q44); //after flip this voxel will be the origin
    mat33 mFlipZ;
    LOAD_MAT33(mFlipZ,1.0f, 0.0f, 0.0f, 0.0f,1.0f,0.0f, 0.0f,0.0f,-1.0f);
    s= nifti_mat33_mul( s , mFlipZ );
    LOAD_MAT44(Q44, s.m[0][0],s.m[0][1],s.m[0][2],v.v[0],
               s.m[1][0],s.m[1][1],s.m[1][2],v.v[1],
               s.m[2][0],s.m[2][1],s.m[2][2],v.v[2]);
    //printMessage(" ----------> %f %f %f\n",v.v[0],v.v[1],v.v[2]);
    setQSForm(h,Q44, true);
    //printMessage("nii_flipImgY dims %dx%dx%d %d \n",h->dim[1],h->dim[2], dim3to7,h->bitpix/8);
    return nii_flipImgZ(bImg,h);
}// nii_flipZ()

unsigned char * nii_flipY(unsigned char* bImg, struct nifti_1_header *h){
    mat33 s;
    mat44 Q44;
    LOAD_MAT33(s,h->srow_x[0],h->srow_x[1],h->srow_x[2], h->srow_y[0],h->srow_y[1],h->srow_y[2],
               h->srow_z[0],h->srow_z[1],h->srow_z[2]);
    LOAD_MAT44(Q44,h->srow_x[0],h->srow_x[1],h->srow_x[2],h->srow_x[3],
               h->srow_y[0],h->srow_y[1],h->srow_y[2],h->srow_y[3],
               h->srow_z[0],h->srow_z[1],h->srow_z[2],h->srow_z[3]);
    vec4 v= setVec4(0,(float) h->dim[2]-1,0);
    v = nifti_vect44mat44_mul(v, Q44); //after flip this voxel will be the origin
    mat33 mFlipY;
    LOAD_MAT33(mFlipY,1.0f, 0.0f, 0.0f, 0.0f,-1.0f,0.0f, 0.0f,0.0f,1.0f);

    s= nifti_mat33_mul( s , mFlipY );
    LOAD_MAT44(Q44, s.m[0][0],s.m[0][1],s.m[0][2],v.v[0],
               s.m[1][0],s.m[1][1],s.m[1][2],v.v[1],
               s.m[2][0],s.m[2][1],s.m[2][2],v.v[2]);
    setQSForm(h,Q44, true);
    //printMessage("nii_flipImgY dims %dx%d %d \n",h->dim[1],h->dim[2], h->bitpix/8);
    return nii_flipImgY(bImg,h);
}// nii_flipY()

/*void conv12bit16bit(unsigned char * img, struct nifti_1_header hdr) {
//convert 12-bit allocated data to 16-bit
// works for MR-MONO2-12-angio-an1 from http://www.barre.nom.fr/medical/samples/
// looks wrong: this sample toggles between big and little endian stores
	printWarning("Support for images that allocate 12 bits is experimental\n");
	int nVox = nii_ImgBytes(hdr) / (hdr.bitpix/8);
    for (int i=(nVox-1); i >= 0; i--) {
    	int i16 = i * 2;
    	int i12 = floor(i * 1.5);
    	uint16_t val;
    	if ((i % 2) != 1) {
    		val = (img[i12+0] << 4) + (img[i12+1] >> 4);
    	} else {
    		val = ((img[i12+0] & 0x0F) << 8) + img[i12+1];
		}

		//if ((i % 2) != 1) {
    	//	val = img[i12+0]  + ((img[i12+1] & 0xF0) << 4);
    	//} else {
    	//	val = (img[i12+0] & 0x0F) + (img[i12+1] << 4);
		//}
		val = val & 0xFFF;
        img[i16+0] = val & 0xFF;
        img[i16+1] = (val >> 8) & 0xFF;
    }
} //conv12bit16bit()*/

void conv12bit16bit(unsigned char * img, struct nifti_1_header hdr) {
//convert 12-bit allocated data to 16-bit
// works for MR-MONO2-12-angio-an1 from http://www.barre.nom.fr/medical/samples/
// looks wrong: this sample toggles between big and little endian stores
	printWarning("Support for images that allocate 12 bits is experimental\n");
	int nVox = (int) nii_ImgBytes(hdr) / (hdr.bitpix/8);
    for (int i=(nVox-1); i >= 0; i--) {
    	int i16 = i * 2;
    	int i12 = floor(i * 1.5);
    	uint16_t val;
    	if ((i % 2) != 1) {
    		val = img[i12+1] + (img[i12+0] << 8);
    		val = val >> 4;
    	} else {
    		val = img[i12+0] + (img[i12+1] << 8);
		}
        img[i16+0] = val & 0xFF;
        img[i16+1] = (val >> 8) & 0xFF;
    }
} //conv12bit16bit()

unsigned char * nii_loadImgCore(char* imgname, struct nifti_1_header hdr, int bitsAllocated) {
    size_t imgsz = nii_ImgBytes(hdr);
    size_t imgszRead = imgsz;
    if (bitsAllocated == 12)
         imgszRead = round(imgsz * 0.75);
    FILE *file = fopen(imgname , "rb");
	if (!file) {
         printError("Unable to open '%s'\n", imgname);
         return NULL;
    }
	fseek(file, 0, SEEK_END);
	long fileLen=ftell(file);
    if (fileLen < (imgszRead+(long) hdr.vox_offset)) {
        //previously  (fileLen < (imgszRead+hdr.vox_offset))
        // FileSize < (ImageSize+HeaderSize): 42399788 < (42398702+1086)
		// FileSize < (ImageSize+HeaderSize): 42399788 < ( 42399792.00)
        //note hdr.vox_offset is a float, and without a type-cast it can lead to unusual values
        //https://www.nitrc.org/forum/message.php?msg_id=27155
        printMessage("FileSize < (ImageSize+HeaderSize): %ld < (%zu+%ld) \n", fileLen, imgszRead, (long)hdr.vox_offset);
        //printMessage("FileSize < (ImageSize+HeaderSize): %ld < (%zu) \n", fileLen, imgszRead+(long)hdr.vox_offset);
        printWarning("File not large enough to store image data: %s\n", imgname);
        return NULL;
    }
	fseek(file, (long) hdr.vox_offset, SEEK_SET);
    unsigned char *bImg = (unsigned char *)malloc(imgsz);
    //int i = 0;
    //while (bImg[i] == 0) i++;
    //printMessage("%d %d<\n",i,bImg[i]);
    size_t  sz = fread(bImg, 1, imgszRead, file);
	fclose(file);
	if (sz < imgszRead) {
         printError("Only loaded %zu of %zu bytes for %s\n", sz, imgszRead, imgname);
         return NULL;
    }
	if (bitsAllocated == 12)
	 conv12bit16bit(bImg, hdr);
    return bImg;
} //nii_loadImgCore()

unsigned char * nii_planar2rgb(unsigned char* bImg, struct nifti_1_header *hdr, int isPlanar) {
	//DICOM data saved in triples RGBRGBRGB, NIfTI RGB saved in planes RRR..RGGG..GBBBB..B
	if (bImg == NULL) return NULL;
	if (hdr->datatype != DT_RGB24) return bImg;
	if (isPlanar == 0) return bImg;
	int dim3to7 = 1;
	for (int i = 3; i < 8; i++)
		if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
	int sliceBytes8 = hdr->dim[1]*hdr->dim[2];
	int sliceBytes24 = sliceBytes8 * 3;
	unsigned char * slice24 = (unsigned char *)malloc(sizeof(unsigned char) * (sliceBytes24));
	int sliceOffsetRGB = 0;
	int sliceOffsetR = 0;
	int sliceOffsetG = sliceOffsetR + sliceBytes8;
	int sliceOffsetB = sliceOffsetR + 2*sliceBytes8;
	//printMessage("planar->rgb %dx%dx%d\n", hdr->dim[1],hdr->dim[2], dim3to7);
    int i = 0;
	for (int sl = 0; sl < dim3to7; sl++) { //for each 2D slice
		memcpy(slice24, &bImg[sliceOffsetRGB], sliceBytes24);
		for (int rgb = 0; rgb < sliceBytes8; rgb++) {
			bImg[i++] =slice24[sliceOffsetR+rgb];
			bImg[i++] =slice24[sliceOffsetG+rgb];
			bImg[i++] =slice24[sliceOffsetB+rgb];
		}
		sliceOffsetRGB += sliceBytes24;
	} //for each slice
	free(slice24);
	return bImg;
} //nii_planar2rgb()

unsigned char * nii_rgb2planar(unsigned char* bImg, struct nifti_1_header *hdr, int isPlanar) {
    //DICOM data saved in triples RGBRGBRGB, Analyze RGB saved in planes RRR..RGGG..GBBBB..B
    if (bImg == NULL) return NULL;
    if (hdr->datatype != DT_RGB24) return bImg;
    if (isPlanar == 1) return bImg;//return nii_bgr2rgb(bImg,hdr);
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    int sliceBytes8 = hdr->dim[1]*hdr->dim[2];
    int sliceBytes24 = sliceBytes8 * 3;
	unsigned char * slice24 = (unsigned char *)malloc(sizeof(unsigned char) * (sliceBytes24));
    //printMessage("rgb->planar %dx%dx%d\n", hdr->dim[1],hdr->dim[2], dim3to7);
    int sliceOffsetR = 0;
    for (int sl = 0; sl < dim3to7; sl++) { //for each 2D slice
        memcpy(slice24, &bImg[sliceOffsetR], sliceBytes24); //TPX memcpy(&slice24, &bImg[sliceOffsetR], sliceBytes24);
        int sliceOffsetG = sliceOffsetR + sliceBytes8;
        int sliceOffsetB = sliceOffsetR + 2*sliceBytes8;
        int i = 0;
        int j = 0;
        for (int rgb = 0; rgb < sliceBytes8; rgb++) {
            bImg[sliceOffsetR+j] =slice24[i++];
            bImg[sliceOffsetG+j] =slice24[i++];
            bImg[sliceOffsetB+j] =slice24[i++];
            j++;
        }
        sliceOffsetR += sliceBytes24;
    } //for each slice
	free(slice24);
    return bImg;
} //nii_rgb2Planar()

unsigned char * nii_iVaries(unsigned char *img, struct nifti_1_header *hdr){
    //each DICOM image can have its own intesity scaling, whereas NIfTI requires the same scaling for all images in a file
    //WARNING: do this BEFORE nii_check16bitUnsigned!!!!
    //if (hdr->datatype != DT_INT16) return img;
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    int nVox = hdr->dim[1]*hdr->dim[2]* dim3to7;
    if (nVox < 1) return img;
    float * img32=(float*)malloc(nVox*sizeof(float));
    if (hdr->datatype == DT_UINT8) {
        uint8_t * img8i = (uint8_t*) img;
        for (int i=0; i < nVox; i++)
            img32[i] = img8i[i];
    } else if (hdr->datatype == DT_UINT16) {
        uint16_t * img16ui = (uint16_t*) img;
        for (int i=0; i < nVox; i++)
            img32[i] = img16ui[i];
    } else if (hdr->datatype == DT_INT16) {
        int16_t * img16i = (int16_t*) img;
        for (int i=0; i < nVox; i++)
            img32[i] = img16i[i];
    } else if (hdr->datatype == DT_INT32) {
        int32_t * img32i = (int32_t*) img;
        for (int i=0; i < nVox; i++)
            img32[i] = (float) img32i[i];
    }
    free (img); //release previous image
    for (int i=0; i < nVox; i++)
        img32[i] = (img32[i]* hdr->scl_slope)+hdr->scl_inter;
    hdr->scl_slope = 1;
    hdr->scl_inter = 0;
    hdr->datatype = DT_FLOAT;
    hdr->bitpix = 32;
    return (unsigned char*) img32;
} //nii_iVaries()


/*unsigned char * nii_reorderSlicesX(unsigned char* bImg, struct nifti_1_header *hdr, struct TDTI4D *dti4D) {
//Philips can save slices in any random order... rearrange all of them
	int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    if (dim3to7 < 2) return bImg;
    uint64_t sliceBytes = hdr->dim[1]*hdr->dim[2]*hdr->bitpix/8;
	unsigned char *sliceImg = (unsigned char *)malloc( sliceBytes);
	uint32_t *idx = (uint32_t *)malloc( dim3to7 * sizeof(uint32_t));
	for (int i = 0; i < dim3to7; i++) //for each volume
		idx[i] = i;
    for (int i = 0; i < dim3to7; i++) { //for each volume
		int fromSlice = idx[dti4D->sliceOrder[i]];
		//if (i < 10) printMessage(" ===> Moving slice from/to positions\t%d\t%d\n", i, toSlice);
		if (i != fromSlice) {
			uint64_t inPos = fromSlice * sliceBytes;
			uint64_t outPos = i * sliceBytes;
			memcpy( &sliceImg[0], &bImg[outPos], sliceBytes); //dest, src -> copy slice about to be overwritten
			memcpy( &bImg[outPos], &bImg[inPos], sliceBytes); //dest, src
			memcpy( &bImg[inPos], &sliceImg[0], sliceBytes); //
			idx[i] = fromSlice;
    	}
    }
    free(idx);
    free(sliceImg);
    return bImg;
}*/

unsigned char * nii_reorderSlicesX(unsigned char* bImg, struct nifti_1_header *hdr, struct TDTI4D *dti4D) {
//Philips can save slices in any random order... rearrange all of them
	int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    if (dim3to7 < 2) return bImg;
    if (dim3to7 > kMaxSlice2D) return bImg;
    uint64_t imgSz = nii_ImgBytes(*hdr);
    uint64_t sliceBytes = hdr->dim[1]*hdr->dim[2]*hdr->bitpix/8;
	unsigned char *outImg = (unsigned char *)malloc( imgSz);
    memcpy( &outImg[0],&bImg[0], imgSz);
    for (int i = 0; i < dim3to7; i++) { //for each volume
		int fromSlice = dti4D->sliceOrder[i];
		//if (i < 10) printMessage(" ===> Moving slice from/to positions\t%d\t%d\n", i, toSlice);
		//printMessage(" ===> Moving slice from/to positions\t%d\t%d\n", fromSlice, i);
		if ((i < 0) || (fromSlice >= dim3to7)) {
			printError("Re-ordered slice out-of-volume %d\n", fromSlice);
		} else if (i != fromSlice) {
			uint64_t inPos = fromSlice * sliceBytes;
			uint64_t outPos = i * sliceBytes;
			memcpy( &bImg[outPos], &outImg[inPos], sliceBytes);
    	}
    }
    free(outImg);
    return bImg;
}

/*unsigned char * nii_reorderSlicesX(unsigned char* bImg, struct nifti_1_header *hdr, struct TDTI4D *dti4D) {
//Philips can save slices in any random order... rearrange all of them
	//if (sliceOrder == NULL) return bImg;
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    if (dim3to7 < 2) return bImg;
    //printMessage(" NOT reordering %d Philips slices.\n", dim3to7); return bImg;
    uint64_t sliceBytes = hdr->dim[1]*hdr->dim[2]*hdr->bitpix/8;
	unsigned char *sliceImg = (unsigned char *)malloc( sliceBytes);
    //for (int i = 0; i < dim3to7; i++) { //for each volume
    uint64_t imgSz = nii_ImgBytes(*hdr);
    //this uses a lot of RAM, someday this could be done in place...
    unsigned char *outImg = (unsigned char *)malloc( imgSz);
    memcpy( &outImg[0],&bImg[0], imgSz);

    for (int i = 0; i < dim3to7; i++) { //for each volume
		int toSlice = dti4D->sliceOrder[i];
		//if (i < 10) printMessage(" ===> Moving slice from/to positions\t%d\t%d\n", i, toSlice);
		if (i != toSlice) {
			uint64_t inPos = i * sliceBytes;
			uint64_t outPos = toSlice * sliceBytes;
			memcpy( &bImg[outPos], &outImg[inPos], sliceBytes);
    	}
    }
    free(sliceImg);
    free(outImg);
    return bImg;
}*/

/*unsigned char * nii_XYTZ_XYZT(unsigned char* bImg, struct nifti_1_header *hdr, int seqRepeats) {
    //Philips can save time as 3rd dimensions, NIFTI requires time is 4th dimension
    int dim4to7 = 1;
    for (int i = 4; i < 8; i++)
        if (hdr->dim[i] > 1) dim4to7 = dim4to7 * hdr->dim[i];
    if ((hdr->dim[3] < 2) || (dim4to7 < 2)) return bImg;
    printMessage("Converting XYTZ to XYZT with %d slices (Z) and %d volumes (T).\n",hdr->dim[3], dim4to7);
    if ((dim4to7 % seqRepeats) != 0) {
        printError("Patient position repeats %d times, but this does not evenly divide number of volumes (%d)\n", seqRepeats,dim4to7);
        seqRepeats = 1;
    }
    uint64_t typeRepeats = dim4to7 / seqRepeats;
    uint64_t sliceBytes = hdr->dim[1]*hdr->dim[2]*hdr->bitpix/8;
    uint64_t seqBytes = sliceBytes * seqRepeats;
    uint64_t typeBytes = seqBytes * hdr->dim[3];
    uint64_t imgSz = nii_ImgBytes(*hdr);
    //this uses a lot of RAM, someday this could be done in place...
    unsigned char *outImg = (unsigned char *)malloc( imgSz);
    //memcpy(&tempImg[0], &bImg[0], imgSz);
    uint64_t origPos = 0;
    uint64_t Pos = 0; //
    for (int t = 0; t < (int)typeRepeats; t++) { //for each volume
        for (int s = 0; s < seqRepeats; s++) {
            origPos = (t*typeBytes) +s*sliceBytes;
            for (int z = 0; z < hdr->dim[3]; z++) { //for each slice
                memcpy( &outImg[Pos],&bImg[origPos], sliceBytes);
                Pos += sliceBytes;
                origPos += seqBytes;
            }
        }//for s
    }
    free(bImg);
    return outImg;
} //nii_XYTZ_XYZT()
*/

unsigned char * nii_byteswap(unsigned char *img, struct nifti_1_header *hdr){
    if (hdr->bitpix < 9) return img;
    uint64_t nvox = nii_ImgBytes(*hdr) / (hdr->bitpix/8);
    void *ar = (void*) img;
    if (hdr->bitpix == 16) nifti_swap_2bytes( nvox , ar );
    if (hdr->bitpix == 32) nifti_swap_4bytes( nvox , ar );
    if (hdr->bitpix == 64) nifti_swap_8bytes( nvox , ar );
    return img;
} //nii_byteswap()

#ifdef myEnableJasper
unsigned char * nii_loadImgCoreJasper(char* imgname, struct nifti_1_header hdr, struct TDICOMdata dcm, int compressFlag) {
    if (jas_init()) {
        return NULL;
    }
    jas_stream_t *in;
    jas_image_t *image;
    jas_setdbglevel(0);
    if (!(in = jas_stream_fopen(imgname, "rb"))) {
        printError( "Cannot open input image file %s\n", imgname);
        return NULL;
    }
    //int isSeekable = jas_stream_isseekable(in);
    jas_stream_seek(in, dcm.imageStart, 0);
    int infmt = jas_image_getfmt(in);
    if (infmt < 0) {
        printError( "Input image has unknown format %s offset %d bytes %d\n", imgname, dcm.imageStart, dcm.imageBytes);
        return NULL;
    }
    char opt[] = "\0";
    char *inopts = opt;
    if (!(image = jas_image_decode(in, infmt, inopts))) {
        printError("Cannot decode image data %s offset %d bytes %d\n", imgname, dcm.imageStart, dcm.imageBytes);
        return NULL;
    }
    int numcmpts;
    int cmpts[4];
    switch (jas_clrspc_fam(jas_image_clrspc(image))) {
        case JAS_CLRSPC_FAM_RGB:
            if (jas_image_clrspc(image) != JAS_CLRSPC_SRGB)
                printWarning("Inaccurate color\n");
            numcmpts = 3;
            if ((cmpts[0] = jas_image_getcmptbytype(image,
                                                    JAS_IMAGE_CT_COLOR(JAS_CLRSPC_CHANIND_RGB_R))) < 0 ||
                (cmpts[1] = jas_image_getcmptbytype(image,
                                                    JAS_IMAGE_CT_COLOR(JAS_CLRSPC_CHANIND_RGB_G))) < 0 ||
                (cmpts[2] = jas_image_getcmptbytype(image,
                                                    JAS_IMAGE_CT_COLOR(JAS_CLRSPC_CHANIND_RGB_B))) < 0) {
                printError("Missing color component\n");
                return NULL;
            }
            break;
        case JAS_CLRSPC_FAM_GRAY:
            if (jas_image_clrspc(image) != JAS_CLRSPC_SGRAY)
                printWarning("Inaccurate color\n");
            numcmpts = 1;
            if ((cmpts[0] = jas_image_getcmptbytype(image,
                                                    JAS_IMAGE_CT_COLOR(JAS_CLRSPC_CHANIND_GRAY_Y))) < 0) {
                printError("Missing color component\n");
                return NULL;
            }
            break;
        default:
            printError("Unsupported color space\n");
            return NULL;
            break;
    }
    int width = jas_image_cmptwidth(image, cmpts[0]);
    int height = jas_image_cmptheight(image, cmpts[0]);
    int prec = jas_image_cmptprec(image, cmpts[0]);
    int sgnd = jas_image_cmptsgnd(image, cmpts[0]);
    #ifdef MY_DEBUG
    printMessage("offset %d w*h %d*%d bpp %d sgnd %d components %d '%s' Jasper=%s\n",dcm.imageStart, width, height, prec, sgnd, numcmpts, imgname, jas_getversion());
    #endif
    for (int cmptno = 0; cmptno < numcmpts; ++cmptno) {
        if (jas_image_cmptwidth(image, cmpts[cmptno]) != width ||
            jas_image_cmptheight(image, cmpts[cmptno]) != height ||
            jas_image_cmptprec(image, cmpts[cmptno]) != prec ||
            jas_image_cmptsgnd(image, cmpts[cmptno]) != sgnd ||
            jas_image_cmpthstep(image, cmpts[cmptno]) != jas_image_cmpthstep(image, 0) ||
            jas_image_cmptvstep(image, cmpts[cmptno]) != jas_image_cmptvstep(image, 0) ||
            jas_image_cmpttlx(image, cmpts[cmptno]) != jas_image_cmpttlx(image, 0) ||
            jas_image_cmpttly(image, cmpts[cmptno]) != jas_image_cmpttly(image, 0)) {
            printMessage("The NIfTI format cannot be used to represent an image with this geometry.\n");
            return NULL;
        }
    }
    //extract the data
    int bpp = (prec + 7) >> 3; //e.g. 12 bits requires 2 bytes
    int imgbytes = bpp * width * height * numcmpts;
    if ((bpp < 1) || (bpp > 2) || (width < 1) || (height < 1) || (imgbytes < 1)) {
        printError("Catastrophic decompression error\n");
        return NULL;
    }
    jas_seqent_t v;
    unsigned char *img = (unsigned char *)malloc(imgbytes);
    uint16_t * img16ui = (uint16_t*) img; //unsigned 16-bit
    int16_t * img16i = (int16_t*) img; //signed 16-bit
    if (sgnd) bpp = -bpp;
    if (bpp == -1) {
        printError("Signed 8-bit DICOM?\n");
        return NULL;
    }
    jas_matrix_t *data;
    jas_seqent_t *d;
    data = 0;
    int cmptno, y, x;
    int pix = 0;
    for (cmptno = 0; cmptno < numcmpts; ++cmptno) {
        if (!(data = jas_matrix_create(1, width))) {
            free(img);
            return NULL;
        }
    }
    //n.b. Analyze rgb-24 are PLANAR e.g. RRR..RGGG..GBBB..B not RGBRGBRGB...RGB
    for (cmptno = 0; cmptno < numcmpts; ++cmptno) {
        for (y = 0; y < height; ++y) {
            if (jas_image_readcmpt(image, cmpts[cmptno], 0, y, width, 1, data)) {
                free(img);
                return NULL;
            }
            d = jas_matrix_getref(data, 0, 0);
            for (x = 0; x < width; ++x) {
                v = *d;
                switch (bpp) {
                    case 1:
                        img[pix] = v;
                        break;
                    case 2:
                        img16ui[pix] = v;
                        break;
                    case -2:
                        img16i[pix] = v;
                        break;
                }
                pix ++;
                ++d;
            }//for x
        } //for y
    } //for each component
    jas_matrix_destroy(data);
    jas_image_destroy(image);
    jas_image_clearfmts();
    return img;
} //nii_loadImgCoreJasper()
#endif

struct TJPEG {
    long offset;
    long size;
};

TJPEG *  decode_JPEG_SOF_0XC3_stack (const char *fn, int skipBytes, bool isVerbose, int frames, bool isLittleEndian) {
#define abortGoto() free(lOffsetRA); return NULL;
    TJPEG *lOffsetRA = (TJPEG*) malloc(frames * sizeof(TJPEG));
    FILE *reader = fopen(fn, "rb");
    fseek(reader, 0, SEEK_END);
    long lRawSz = ftell(reader)- skipBytes;
    if (lRawSz <= 8) {
        printError("Unable to open %s\n", fn);
        abortGoto(); //read failure
    }
    fseek(reader, skipBytes, SEEK_SET);
    unsigned char *lRawRA = (unsigned char*) malloc(lRawSz);
    size_t lSz = fread(lRawRA, 1, lRawSz, reader);
    fclose(reader);
    if (lSz < (size_t)lRawSz) {
        printError("Unable to read %s\n", fn);
        abortGoto(); //read failure
    }
    long lRawPos = 0; //starting position
    int frame = 0;
    while ((frame < frames) && ((lRawPos+10) < lRawSz)) {
        int tag = dcmInt(4,&lRawRA[lRawPos],isLittleEndian);
        lRawPos += 4; //read tag
        int tagLength = dcmInt(4,&lRawRA[lRawPos],isLittleEndian);
        long tagEnd =lRawPos + tagLength + 4;
        if (isVerbose)
            printMessage("Tag %#x length %d end at %ld\n", tag, tagLength, tagEnd+skipBytes);
        lRawPos += 4; //read tag length
        if ((lRawRA[lRawPos] != 0xFF) || (lRawRA[lRawPos+1] != 0xD8) || (lRawRA[lRawPos +2] != 0xFF)) {
            if (isVerbose)
                printWarning("JPEG signature 0xFFD8FF not found at offset %d of %s\n", skipBytes, fn);
        } else {
            lOffsetRA[frame].offset = lRawPos+skipBytes;
            lOffsetRA[frame].size = tagLength;
            frame ++;
        }
        lRawPos = tagEnd;
    }
    free(lRawRA);
    if (frame < frames) {
        printMessage("Only found %d of %d JPEG fragments. Please use dcmdjpeg or gdcmconv to uncompress data.\n", frame, frames);
        abortGoto();
    }
    return lOffsetRA;
}

unsigned char * nii_loadImgJPEGC3(char* imgname, struct nifti_1_header hdr, struct TDICOMdata dcm, bool isVerbose) {
    //arcane and inefficient lossless compression method popularized by dcmcjpeg, examples at http://www.osirix-viewer.com/resources/dicom-image-library/
    int dimX, dimY, bits, frames;
    //clock_t start = clock();
    // https://github.com/rii-mango/JPEGLosslessDecoderJS/blob/master/tests/data/jpeg_lossless_sel1-8bit.dcm
    //N.B. this current code can not extract a 2D image that is saved as multiple fragments, for example see the JPLL files at
    // ftp://medical.nema.org/MEDICAL/Dicom/DataSets/WG04/
    //Live javascript code that can handle these is at
    // https://github.com/chafey/cornerstoneWADOImageLoader
    //I have never seen these segmented images in the wild, so we will simply warn the user if we encounter such a file
    //int Sz = JPEG_SOF_0XC3_sz (imgname, (dcm.imageStart - 4), dcm.isLittleEndian);
    //printMessage("Sz %d %d\n", Sz, dcm.imageBytes );
    //This behavior is legal but appears extremely rare
    //ftp://medical.nema.org/medical/dicom/final/cp900_ft.pdf
    if (65536 == dcm.imageBytes)
        printError("One frame may span multiple fragments. SOFxC3 lossless JPEG. Please extract with dcmdjpeg or gdcmconv.\n");
    unsigned char * ret = decode_JPEG_SOF_0XC3 (imgname, dcm.imageStart, isVerbose, &dimX, &dimY, &bits, &frames, 0);
    if (ret == NULL) {
    	printMessage("Unable to decode JPEG. Please use dcmdjpeg to uncompress data.\n");
        return NULL;
    }
    //printMessage("JPEG %fms\n", ((double)(clock()-start))/1000);
    if (hdr.dim[3] != frames) { //multi-slice image saved as multiple image fragments rather than a single image
        //printMessage("Unable to decode all slices (%d/%d). Please use dcmdjpeg to uncompress data.\n", frames, hdr.dim[3]);
        if (ret != NULL) free(ret);
        TJPEG * offsetRA = decode_JPEG_SOF_0XC3_stack (imgname, dcm.imageStart-8, isVerbose, hdr.dim[3], dcm.isLittleEndian);
        if (offsetRA == NULL) return NULL;
        size_t slicesz = nii_SliceBytes(hdr);
        size_t imgsz = slicesz * hdr.dim[3];
        size_t pos = 0;
        unsigned char *bImg = (unsigned char *)malloc(imgsz);
        for (int frame = 0; frame < hdr.dim[3]; frame++) {
            if (isVerbose)
                printMessage("JPEG frame %d has %ld bytes @ %ld\n", frame, offsetRA[frame].size, offsetRA[frame].offset);
            unsigned char * ret = decode_JPEG_SOF_0XC3 (imgname, (int)offsetRA[frame].offset, false, &dimX, &dimY, &bits, &frames, (int)offsetRA[frame].size);
            if (ret == NULL) {
                printMessage("Unable to decode JPEG. Please use dcmdjpeg to uncompress data.\n");
                free(bImg);
                return NULL;
            }
            memcpy(&bImg[pos], ret, slicesz); //dest, src, size
            free(ret);
            pos += slicesz;
        }
        free(offsetRA);
        return bImg;
    }
    return ret;
}

#ifndef F_OK
#define F_OK 0 /* existence check */
#endif

#ifndef myDisableClassicJPEG

#ifdef myTurboJPEG //if turboJPEG instead of nanoJPEG for classic JPEG decompression

//unsigned char * nii_loadImgJPEG50(char* imgname, struct nifti_1_header hdr, struct TDICOMdata dcm) {
unsigned char * nii_loadImgJPEG50(char* imgname, struct TDICOMdata dcm) {
//decode classic JPEG using nanoJPEG
    //printMessage("50 offset %d\n", dcm.imageStart);
    if ((dcm.samplesPerPixel != 1) && (dcm.samplesPerPixel != 3)) {
        printError("%d components (expected 1 or 3) in a JPEG image '%s'\n", dcm.samplesPerPixel, imgname);
        return NULL;
    }
    if( access(imgname, F_OK ) == -1 ) {
        printError("Unable to find '%s'\n", imgname);
        return NULL;
    }
    //load compressed data
    FILE *f = fopen(imgname, "rb");
    fseek(f, 0, SEEK_END);
    long unsigned int _jpegSize = (long unsigned int) ftell(f);
    _jpegSize = _jpegSize - dcm.imageStart;
    if (_jpegSize < 8) {
        printError("File too small\n");
        fclose(f);
        return NULL;
    }
    unsigned char* _compressedImage = (unsigned char *)malloc(_jpegSize);
    fseek(f, dcm.imageStart, SEEK_SET);
    _jpegSize = (long unsigned int) fread(_compressedImage, 1, _jpegSize, f);
    fclose(f);
    int jpegSubsamp, width, height;
    //printMessage("Decoding with turboJPEG\n");
	tjhandle _jpegDecompressor = tjInitDecompress();
	tjDecompressHeader2(_jpegDecompressor, _compressedImage, _jpegSize, &width, &height, &jpegSubsamp);
	int COLOR_COMPONENTS = dcm.samplesPerPixel;
	//printMessage("turboJPEG h*w %d*%d sampling %d components %d\n", width, height, jpegSubsamp, COLOR_COMPONENTS);
	if ((jpegSubsamp == TJSAMP_GRAY) && (COLOR_COMPONENTS != 1)) {
        printError("Grayscale jpegs should not have %d components '%s'\n", COLOR_COMPONENTS, imgname);
	}
	if ((jpegSubsamp != TJSAMP_GRAY) && (COLOR_COMPONENTS != 3)) {
        printError("Color jpegs should not have %d components '%s'\n", COLOR_COMPONENTS, imgname);
	}
	//unsigned char bImg[width*height*COLOR_COMPONENTS]; //!< will contain the decompressed image
	unsigned char *bImg = (unsigned char *)malloc(width*height*COLOR_COMPONENTS);
	if (COLOR_COMPONENTS == 1) //TJPF_GRAY
		tjDecompress2(_jpegDecompressor, _compressedImage, _jpegSize, bImg, width, 0/*pitch*/, height, TJPF_GRAY, TJFLAG_FASTDCT);
	else
		tjDecompress2(_jpegDecompressor, _compressedImage, _jpegSize, bImg, width, 0/*pitch*/, height, TJPF_RGB, TJFLAG_FASTDCT);
	//printMessage("turboJPEG h*w %d*%d (sampling %d)\n", width, height, jpegSubsamp);
	tjDestroy(_jpegDecompressor);
	return bImg;
}

#else //if turboJPEG else use nanojpeg...

//unsigned char * nii_loadImgJPEG50(char* imgname, struct nifti_1_header hdr, struct TDICOMdata dcm) {
unsigned char * nii_loadImgJPEG50(char* imgname, struct TDICOMdata dcm) {
//decode classic JPEG using nanoJPEG
    //printMessage("50 offset %d\n", dcm.imageStart);
    if( access(imgname, F_OK ) == -1 ) {
        printError("Unable to find '%s'\n", imgname);
        return NULL;
    }
    //load compressed data
    FILE *f = fopen(imgname, "rb");
    fseek(f, 0, SEEK_END);
    int size = (int) ftell(f);
    size = size - dcm.imageStart;
    if (size < 8) {
        printError("File too small '%s'\n", imgname);
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc(size);
    fseek(f, dcm.imageStart, SEEK_SET);
    size = (int) fread(buf, 1, size, f);
    fclose(f);
    //decode
    njInit();
    if (njDecode(buf, size)) {
        printError("Unable to decode JPEG image.\n");
        return NULL;
    }
    free(buf);
    unsigned char *bImg = (unsigned char *)malloc(njGetImageSize());
    memcpy(bImg, njGetImage(), njGetImageSize()); //dest, src, size
    njDone();
    return bImg;
}
#endif
#endif

uint32_t rleInt(int lIndex, unsigned char lBuffer[], bool swap) {//read binary 32-bit integer
    uint32_t retVal = 0;
    memcpy(&retVal, (char*)&lBuffer[lIndex * 4], 4);
    if (!swap) return retVal;
    uint32_t swapVal;
    char *inInt = ( char* ) & retVal;
    char *outInt = ( char* ) & swapVal;
    outInt[0] = inInt[3];
    outInt[1] = inInt[2];
    outInt[2] = inInt[1];
    outInt[3] = inInt[0];
    return swapVal;
} //rleInt()

unsigned char * nii_loadImgPMSCT_RLE1(char* imgname, struct nifti_1_header hdr, struct TDICOMdata dcm) {
//Transfer Syntax 1.3.46.670589.33.1.4.1 also handled by TomoVision and GDCM's rle2img
//https://github.com/malaterre/GDCM/blob/a923f206060e85e8d81add565ae1b9dd7b210481/Examples/Cxx/rle2img.cxx
//see rle2img: Philips/ELSCINT1 run-length compression 07a1,1011= PMSCT_RLE1
    if (dcm.imageBytes < 66 ) { //64 for header+ 2 byte minimum image
        printError("%d is not enough bytes for PMSCT_RLE1 compression '%s'\n", dcm.imageBytes, imgname);
        return NULL;
    }
    int bytesPerSample = dcm.samplesPerPixel * (dcm.bitsAllocated / 8);
	if (bytesPerSample != 2) { //there is an RGB variation of this format, but we have not seen it in the wild
        printError("PMSCT_RLE1 should be 16-bits per sample (please report on Github and use pmsct_rgb1).\n");
        return NULL;
	}
    FILE *file = fopen(imgname , "rb");
	if (!file) {
         printError("Unable to open %s\n", imgname);
         return NULL;
    }
	fseek(file, 0, SEEK_END);
	long fileLen=ftell(file);
    if ((fileLen < 1) || (dcm.imageBytes < 1) || (fileLen < (dcm.imageBytes+dcm.imageStart))) {
        printMessage("File not large enough to store image data: %s\n", imgname);
        fclose(file);
        return NULL;
    }
	fseek(file, (long) dcm.imageStart, SEEK_SET);
	size_t imgsz = nii_ImgBytes(hdr);
    char *cImg = (char *)malloc(dcm.imageBytes); //compressed input
    size_t  sz = fread(cImg, 1, dcm.imageBytes, file);
	fclose(file);
	if (sz < (size_t)dcm.imageBytes) {
         printError("Only loaded %zu of %d bytes for %s\n", sz, dcm.imageBytes, imgname);
         free(cImg);
         return NULL;
    }
	if( imgsz == dcm.imageBytes ) {// Handle special case that data is not compressed:
		return (unsigned char *)cImg;
	}
	unsigned char *bImg = (unsigned char *)malloc(imgsz); //binary output
	// RLE pass: compressed -> temp (bImg -> tImg)
	char *tImg = (char *)malloc(imgsz); //temp output
	int o = 0;
	for(size_t i = 0; i < dcm.imageBytes; ++i) {
		if( cImg[i] == (char)0xa5 ) {
		  int repeat = (unsigned char)cImg[i+1] + 1;
		  char value = cImg[i+2];
		  while(repeat) {
			tImg[o] = value ;
			o ++;
			--repeat;
			}
		  i+=2;
		} else {
		  tImg[o] = cImg[i];
		  o ++;
		}
	} //for i
	free(cImg);
	int tempsize = o;
	//Looks like this RLE is pretty ineffective...
	// printMessage("RLE %d -> %d\n", dcm.imageBytes, o);
	//Delta encoding pass: temp -> output (tImg -> bImg)
	unsigned short delta = 0;
	o = 0;
	int n16 = (int) imgsz >> 1;
	unsigned short *bImg16 = (unsigned short *) bImg;
	for(size_t i = 0; i < tempsize; ++i) {
    	if( tImg[i] == (unsigned char)0x5a ) {
    		unsigned char v1 = (unsigned char)tImg[i+1];
      		unsigned char v2 = (unsigned char)tImg[i+2];
      		unsigned short value = (unsigned short)(v2 * 256 + v1);
      		if (o < n16)
      			bImg16[o] = value;
      		o ++;
      		delta = value;
      		i+=2;
      	} else {
      		unsigned short value = (unsigned short)(tImg[i] + delta);
      		if (o < n16)
      			bImg16[o] = value;
      		o ++;
      		delta = value;
      	}
    } //for i
	//printMessage("Delta %d -> %d (of %d)\n", tempsize, 2*(o-1), imgsz);
	free(tImg);
    return bImg;
} // nii_loadImgPMSCT_RLE1()

unsigned char * nii_loadImgRLE(char* imgname, struct nifti_1_header hdr, struct TDICOMdata dcm) {
//decompress PackBits run-length encoding https://en.wikipedia.org/wiki/PackBits
    if (dcm.imageBytes < 66 ) { //64 for header+ 2 byte minimum image
        printError("%d is not enough bytes for RLE compression '%s'\n", dcm.imageBytes, imgname);
        return NULL;
    }
    FILE *file = fopen(imgname , "rb");
	if (!file) {
         printError("Unable to open %s\n", imgname);
         return NULL;
    }
	fseek(file, 0, SEEK_END);
	long fileLen=ftell(file);
    if ((fileLen < 1) || (dcm.imageBytes < 1) || (fileLen < (dcm.imageBytes+dcm.imageStart))) {
        printMessage("File not large enough to store image data: %s\n", imgname);
        fclose(file);
        return NULL;
    }
	fseek(file, (long) dcm.imageStart, SEEK_SET);
	size_t imgsz = nii_ImgBytes(hdr);
    unsigned char *cImg = (unsigned char *)malloc(dcm.imageBytes); //compressed input
    size_t  sz = fread(cImg, 1, dcm.imageBytes, file);
	fclose(file);
	if (sz < (size_t)dcm.imageBytes) {
         printError("Only loaded %zu of %d bytes for %s\n", sz, dcm.imageBytes, imgname);
         free(cImg);
         return NULL;
    }
    //read header http://dicom.nema.org/dicom/2013/output/chtml/part05/sect_G.3.html
    bool swap = (dcm.isLittleEndian != littleEndianPlatform());
	int bytesPerSample = dcm.samplesPerPixel * (dcm.bitsAllocated / 8);
	uint32_t bytesPerSampleRLE = rleInt(0, cImg, swap);
	if ((bytesPerSample < 0) || (bytesPerSampleRLE != (uint32_t)bytesPerSample)) {
		printError("RLE header corrupted %d != %d\n", bytesPerSampleRLE, bytesPerSample);
		free(cImg);
		return NULL;
	}
	unsigned char *bImg = (unsigned char *)malloc(imgsz); //binary output
	for (size_t i = 0; i < imgsz; i++)
		bImg[i] = 0;
    for (int i = 0; i < bytesPerSample; i++) {
		uint32_t offset = rleInt(i+1, cImg, swap);
		if ((dcm.imageBytes < 0) || (offset > (uint32_t)dcm.imageBytes)) {
			printError("RLE header error\n");
			free(cImg);
			free(bImg);
			return NULL;
		}
		//save in platform's endian:
		// The first Segment is generated by stripping off the most significant byte of each Padded Composite Pixel Code...
		size_t vx = i;
		if  ((dcm.samplesPerPixel == 1) && (littleEndianPlatform())) //endian, except for RGB
			vx = (bytesPerSample-1) - i;
		while (vx < imgsz) {
			int8_t n = (int8_t)cImg[offset];
			offset++;
			//http://dicom.nema.org/dicom/2013/output/chtml/part05/sect_G.3.html
			//if ((n >= 0) && (n <= 127)) { //not needed: int8_t always <=127
			if (n >= 0) { //literal bytes
				int reps = 1 + (int)n;
				for (int r = 0; r < reps; r++) {
					int8_t v = cImg[offset];
					offset++;
					if (vx >= imgsz)
						;//printMessage("literal overflow %d %d\n", r, reps);
					else
						bImg[vx] = v;
					vx = vx + bytesPerSample;
				}
			} else if ((n <= -1) && (n >= -127)) { //repeated run
				int8_t v = cImg[offset];
				offset++;
				int reps = -(int)n + 1;
				for (int r = 0; r < reps; r++) {
					if (vx >= imgsz)
						;//printMessage("repeat overflow %d\n", reps);
					else
						bImg[vx] = v;
					vx = vx + bytesPerSample;
				}
			}; //n.b. we ignore -128!
		} //while vx < imgsz
	} //for i < bytesPerSample
	free(cImg);
    return bImg;
} // nii_loadImgRLE()

#ifdef myDisableOpenJPEG
 #ifndef myEnableJasper
  //avoid compiler warning, see https://stackoverflow.com/questions/3599160/unused-parameter-warnings-in-c
  #define UNUSED(x) (void)(x)
 #endif
#endif

#if defined(myEnableJPEGLS) || defined(myEnableJPEGLS1) //Support for JPEG-LS
//JPEG-LS: Transfer Syntaxes 1.2.840.10008.1.2.4.80 1.2.840.10008.1.2.4.81

#ifdef myEnableJPEGLS1 //use CharLS v1.* requires c++03
 //-std=c++03 -DmyEnableJPEGLS1  charls1/header.cpp charls1/jpegls.cpp charls1/jpegmarkersegment.cpp charls1/interface.cpp  charls1/jpegstreamwriter.cpp
 #include "charls1/interface.h"
#else //use latest release of CharLS: CharLS 2.x requires c++14
 //-std=c++14 -DmyEnableJPEGLS  charls/jpegls.cpp charls/jpegmarkersegment.cpp charls/interface.cpp  charls/jpegstreamwriter.cpp charls/jpegstreamreader.cpp
 #include "charls/charls.h"
#endif
#include "charls/publictypes.h"

unsigned char * nii_loadImgJPEGLS(char* imgname, struct nifti_1_header hdr, struct TDICOMdata dcm) {
	//load compressed data
    FILE *file = fopen(imgname , "rb");
	if (!file) {
         printError("Unable to open %s\n", imgname);
         return NULL;
    }
	fseek(file, 0, SEEK_END);
	long fileLen=ftell(file);
    if ((fileLen < 1) || (dcm.imageBytes < 1) || (fileLen < (dcm.imageBytes+dcm.imageStart))) {
        printMessage("File not large enough to store JPEG-LS data: %s\n", imgname);
        fclose(file);
        return NULL;
    }
	fseek(file, (long) dcm.imageStart, SEEK_SET);
	unsigned char *cImg = (unsigned char *)malloc(dcm.imageBytes); //compressed input
    size_t  sz = fread(cImg, 1, dcm.imageBytes, file);
	fclose(file);
    if (sz < (size_t)dcm.imageBytes) {
        printError("Only loaded %zu of %d bytes for %s\n", sz, dcm.imageBytes, imgname);
        free(cImg);
        return NULL;
    }
	//create buffer for uncompressed data
	size_t imgsz = nii_ImgBytes(hdr);
    unsigned char *bImg = (unsigned char *)malloc(imgsz); //binary output
	JlsParameters params = {};
	#ifdef myEnableJPEGLS1
	if(JpegLsReadHeader(cImg, dcm.imageBytes, &params) != OK ) {
	#else
	using namespace charls;
	if (JpegLsReadHeader(cImg, dcm.imageBytes, &params, nullptr) != ApiResult::OK) {
	#endif
		printMessage("CharLS failed to read header.\n");
		return NULL;
	}
	#ifdef myEnableJPEGLS1
	if (JpegLsDecode(&bImg[0], imgsz, &cImg[0], dcm.imageBytes, &params) != OK ) {
	#else
  	if (JpegLsDecode(&bImg[0], imgsz, &cImg[0], dcm.imageBytes, &params, nullptr) != ApiResult::OK) {
	#endif
		free(bImg);
		printMessage("CharLS failed to read image.\n");
		return NULL;
	}
	return (bImg);
}
#endif

unsigned char * nii_loadImgXL(char* imgname, struct nifti_1_header *hdr, struct TDICOMdata dcm, bool iVaries, int compressFlag, int isVerbose, struct TDTI4D *dti4D) {
//provided with a filename (imgname) and DICOM header (dcm), creates NIfTI header (hdr) and img
    if (headerDcm2Nii(dcm, hdr, true) == EXIT_FAILURE) return NULL; //TOFU
    unsigned char * img;
    if (dcm.compressionScheme == kCompress50)  {
    	#ifdef myDisableClassicJPEG
        	printMessage("Software not compiled to decompress classic JPEG DICOM images\n");
        	return NULL;
    	#else
        	//img = nii_loadImgJPEG50(imgname, *hdr, dcm);
    		img = nii_loadImgJPEG50(imgname, dcm);
    		if (hdr->datatype ==DT_RGB24) //convert to planar
        		img = nii_rgb2planar(img, hdr, dcm.isPlanarRGB);//do this BEFORE Y-Flip, or RGB order can be flipped
        #endif
    } else if (dcm.compressionScheme == kCompressJPEGLS) {
    	#if defined(myEnableJPEGLS) || defined(myEnableJPEGLS1)
    		img = nii_loadImgJPEGLS(imgname, *hdr, dcm);
    		if (hdr->datatype ==DT_RGB24) //convert to planar
        		img = nii_rgb2planar(img, hdr, dcm.isPlanarRGB);//do this BEFORE Y-Flip, or RGB order can be flipped
    	#else
        	printMessage("Software not compiled to decompress JPEG-LS DICOM images\n");
        	return NULL;
    	#endif
    } else if (dcm.compressionScheme == kCompressPMSCT_RLE1) {
    	img = nii_loadImgPMSCT_RLE1(imgname, *hdr, dcm);
    } else if (dcm.compressionScheme == kCompressRLE) {
    	img = nii_loadImgRLE(imgname, *hdr, dcm);
    	if (hdr->datatype ==DT_RGB24) //convert to planar
        	img = nii_rgb2planar(img, hdr, dcm.isPlanarRGB);//do this BEFORE Y-Flip, or RGB order can be flipped

    } else if (dcm.compressionScheme == kCompressC3)
    	img = nii_loadImgJPEGC3(imgname, *hdr, dcm, (isVerbose > 0));
    else
    #ifndef myDisableOpenJPEG
    if ( ((dcm.compressionScheme == kCompress50) || (dcm.compressionScheme == kCompressYes)) && (compressFlag != kCompressNone) )
        img = nii_loadImgCoreOpenJPEG(imgname, *hdr, dcm, compressFlag);
    else
    #else
       #ifdef myEnableJasper
        if ((dcm.compressionScheme == kCompressYes) && (compressFlag != kCompressNone) )
            img = nii_loadImgCoreJasper(imgname, *hdr, dcm, compressFlag);
        else
       #endif
    #endif
     if (dcm.compressionScheme == kCompressYes) {
        printMessage("Software not set up to decompress DICOM\n");
        return NULL;
    } else
    	img = nii_loadImgCore(imgname, *hdr, dcm.bitsAllocated);
    if (img == NULL) return img;
    if ((dcm.compressionScheme == kCompressNone) && (dcm.isLittleEndian != littleEndianPlatform()) && (hdr->bitpix > 8))
        img = nii_byteswap(img, hdr);
    if ((dcm.compressionScheme == kCompressNone) && (hdr->datatype ==DT_RGB24))
        img = nii_rgb2planar(img, hdr, dcm.isPlanarRGB);//do this BEFORE Y-Flip, or RGB order can be flipped
    dcm.isPlanarRGB = true;
    if (dcm.CSA.mosaicSlices > 1) {
        img = nii_demosaic(img, hdr, dcm.CSA.mosaicSlices, (dcm.manufacturer == kMANUFACTURER_UIH)); //, dcm.CSA.protocolSliceNumber1);
        /* we will do this in nii_dicom_batch #ifdef obsolete_mosaic_flip
         img = nii_flipImgY(img, hdr);
         #endif*/
    }
    if ((!dcm.isFloat) && (iVaries)) img = nii_iVaries(img, hdr);
    int nAcq = dcm.locationsInAcquisition;
    if ((nAcq > 1) && (hdr->dim[0] < 4) && ((hdr->dim[3]%nAcq)==0) && (hdr->dim[3]>nAcq) ) {
        hdr->dim[4] = hdr->dim[3]/nAcq;
        hdr->dim[3] = nAcq;
        hdr->dim[0] = 4;
    }
    //~ if ((hdr->dim[0] > 3) && (dcm.patientPositionSequentialRepeats > 1) && (dcm.sliceOrder == NULL)) //swizzle 3rd and 4th dimension (Philips stores time as 3rd dimension)
    //~     img = nii_XYTZ_XYZT(img, hdr,dcm.patientPositionSequentialRepeats);
	if ((dti4D != NULL) && (dti4D->sliceOrder[0] >= 0))
    	img = nii_reorderSlicesX(img, hdr, dti4D);
    //~
    /*if (((dcm.patientPositionSequentialRepeats * 2) == dcm.patientPositionRepeats) && (dcm.isHasPhase) && (dcm.isHasMagnitude)) {
    	hdr->dim[3] = hdr->dim[3] / 2;
		hdr->dim[4] = hdr->dim[4] * 2;
		hdr->dim[0] = 4;
		printMessage("Splitting Phase+Magnitude into two volumes for %d slices (Z) and %d volumes (T).\n",hdr->dim[3], hdr->dim[4]);
    }*/
    headerDcm2NiiSForm(dcm,dcm, hdr, false);
    return img;
} //nii_loadImgXL()

int isSQ(uint32_t groupElement) { //Detect sequence VR ("SQ") for implicit tags
    static const int array_size = 35;
    uint32_t array[array_size] = {0x2005+(uint32_t(0x140F)<<16), 0x0008+(uint32_t(0x1111)<<16), 0x0008+(uint32_t(0x1115)<<16), 0x0008+(uint32_t(0x1140)<<16), 0x0008+(uint32_t(0x1199)<<16), 0x0008+(uint32_t(0x2218)<<16), 0x0008+(uint32_t(0x9092)<<16), 0x0018+(uint32_t(0x9006)<<16), 0x0018+(uint32_t(0x9042)<<16), 0x0018+(uint32_t(0x9045)<<16), 0x0018+(uint32_t(0x9049)<<16), 0x0018+(uint32_t(0x9112)<<16), 0x0018+(uint32_t(0x9114)<<16), 0x0018+(uint32_t(0x9115)<<16), 0x0018+(uint32_t(0x9117)<<16), 0x0018+(uint32_t(0x9119)<<16), 0x0018+(uint32_t(0x9125)<<16), 0x0018+(uint32_t(0x9152)<<16), 0x0018+(uint32_t(0x9176)<<16), 0x0018+(uint32_t(0x9226)<<16), 0x0018+(uint32_t(0x9239)<<16), 0x0020+(uint32_t(0x9071)<<16), 0x0020+(uint32_t(0x9111)<<16), 0x0020+(uint32_t(0x9113)<<16), 0x0020+(uint32_t(0x9116)<<16), 0x0020+(uint32_t(0x9221)<<16), 0x0020+(uint32_t(0x9222)<<16), 0x0028+(uint32_t(0x9110)<<16), 0x0028+(uint32_t(0x9132)<<16), 0x0028+(uint32_t(0x9145)<<16), 0x0040+(uint32_t(0x0260)<<16), 0x0040+(uint32_t(0x0555)<<16), 0x0040+(uint32_t(0xa170)<<16), 0x5200+(uint32_t(0x9229)<<16), 0x5200+(uint32_t(0x9230)<<16)};
	for (int i = 0; i < array_size; i++) {
        //if (array[i] == groupElement) printMessage(" implicitSQ %04x,%04x\n",   groupElement & 65535,groupElement>>16);
        if (array[i] == groupElement)
            return 1;
    }
    return 0;
} //isSQ()

int isDICOMfile(const char * fname) { //0=NotDICOM, 1=DICOM, 2=Maybe(not Part 10 compliant)
    //Someday: it might be worthwhile to detect "IMGF" at offset 3228 to warn user if they attempt to convert Signa data
    FILE *fp = fopen(fname, "rb");
	if (!fp)  return 0;
	fseek(fp, 0, SEEK_END);
	long fileLen=ftell(fp);
    if (fileLen < 256) {
        fclose(fp);
        return 0;
    }
	fseek(fp, 0, SEEK_SET);
	unsigned char buffer[256];
	size_t sz = fread(buffer, 1, 256, fp);
	fclose(fp);
	if (sz < 256) return 0;
    if ((buffer[128] == 'D') && (buffer[129] == 'I')  && (buffer[130] == 'C') && (buffer[131] == 'M'))
    	return 1; //valid DICOM
    if ((buffer[0] == 8) && (buffer[1] == 0)  && (buffer[3] == 0))
    	return 2; //not valid Part 10 file, perhaps DICOM object
    return 0;
} //isDICOMfile()

//START RIR 12/2017 Robert I. Reid

// Gathering spot for all the info needed to get the b value and direction
// for a volume.
struct TVolumeDiffusion {
    struct TDICOMdata* pdd;  // The multivolume
    struct TDTI4D* pdti4D;   // permanent records.

    uint8_t manufacturer;            // kMANUFACTURER_UNKNOWN, kMANUFACTURER_SIEMENS, etc.

    //void set_manufacturer(const uint8_t m) {manufacturer = m; update();}  // unnecessary

    // Everything after this in the structure would be private if it were a C++
    // class, but it has been rewritten as a struct for C compatibility.  I am
    // using _ as a hint of that, although _ for privacy is not really a
    // universal convention in C.  Privacy is desired because immediately
    // any of these are updated _update_tvd() should be called.

    bool _isAtFirstPatientPosition;   // Limit b vals and vecs to 1 per volume.

    //float bVal0018_9087;      // kDiffusion_b_value, always present in Philips/Siemens.
    //float bVal2001_1003;        // kDiffusionBFactor
    // float dirRL2005_10b0;        // kDiffusionDirectionRL
    // float dirAP2005_10b1;        // kDiffusionDirectionAP
    // float dirFH2005_10b2;        // kDiffusionDirectionFH

    // Philips diffusion scans tend to have a "trace" (average of the diffusion
    // weighted volumes) volume tacked on, usually but not always at the end,
    // so b is > 0, but the direction is meaningless.  Most software versions
    // explicitly set the direction to 0, but version 3 does not, making (0x18,
    // 0x9075) necessary.
    bool _isPhilipsNonDirectional;

    //char _directionality0018_9075[16];   // DiffusionDirectionality, not in Philips 2.6.
    // float _orientation0018_9089[3];      // kDiffusionOrientation, always
    //                                      // present in Philips/Siemens for
    //                                      // volumes with a direction.
    //char _seq0018_9117[64];              // MRDiffusionSequence, not in Philips 2.6.

    float _dtiV[4];
    double _symBMatrix[6];
    //uint16_t numDti;
};
struct TVolumeDiffusion initTVolumeDiffusion(struct TDICOMdata* ptdd, struct TDTI4D* dti4D);
void clear_volume(struct TVolumeDiffusion* ptvd); // Blank the volume-specific members or set them to impossible values.
void set_directionality0018_9075(struct TVolumeDiffusion* ptvd, unsigned char* inbuf);
void set_orientation0018_9089(struct TVolumeDiffusion* ptvd, int lLength, unsigned char* inbuf,
                              bool isLittleEndian);
void set_isAtFirstPatientPosition_tvd(struct TVolumeDiffusion* ptvd, bool iafpp);
int set_bValGE(struct TVolumeDiffusion* ptvd, int lLength, unsigned char* inbuf);
void set_diffusion_directionPhilips(struct TVolumeDiffusion* ptvd, float vec, const int axis);
void set_diffusion_directionGE(struct TVolumeDiffusion* ptvd, int lLength, unsigned char* inbuf,  int axis);
void set_bVal(struct TVolumeDiffusion* ptvd, float b);
void set_bMatrix(struct TVolumeDiffusion* ptvd, float b, int component);
void _update_tvd(struct TVolumeDiffusion* ptvd);

struct TVolumeDiffusion initTVolumeDiffusion(struct TDICOMdata* ptdd, struct TDTI4D* dti4D) {
    struct TVolumeDiffusion tvd;
    tvd.pdd = ptdd;
    tvd.pdti4D = dti4D;
    clear_volume(&tvd);
    return tvd;
} //initTVolumeDiffusion()

void clear_volume(struct TVolumeDiffusion* ptvd) {
    ptvd->_isAtFirstPatientPosition = false;
    ptvd->manufacturer = kMANUFACTURER_UNKNOWN;
    //bVal0018_9087 = -1;
    //ptvd->_directionality0018_9075[0] = 0;
    //ptvd->seq0018_9117[0] = 0;
    //bVal2001_1003 = -1;
    // dirRL2005_10b0 = 2;
    // dirAP2005_10b1 = 2;
    // dirFH2005_10b2 = 2;
    ptvd->_isPhilipsNonDirectional = false;
    ptvd->_dtiV[0] = -1;
    for(int i = 1; i < 4; ++i)
        ptvd->_dtiV[i] = 2;
    for(int i = 1; i < 6; ++i)
        ptvd->_symBMatrix[i] = NAN;
    //numDti = 0;
}//clear_volume()

void set_directionality0018_9075(struct TVolumeDiffusion* ptvd, unsigned char* inbuf) {
    //if(!strncmp(( char*)(inbuf), "BMATRIX", 4)) printf("FOUND BMATRIX----%s\n",inbuf );
    //n.b. strncmp returns 0 if the contents of both strings are equal, for boolean 0 = false!
    //  elsewhere we use strstr() which returns 0/null if match is not present
    if(strncmp(( char*)(inbuf), "DIRECTIONAL", 11) &&  // strncmp = 0 for ==.
       //strncmp(( char*)(inbuf), "NONE", 4) && //issue 256
       strncmp(( char*)(inbuf), "BMATRIX", 7)){        // Siemens XA10
        ptvd->_isPhilipsNonDirectional = true;
        // Explicitly set the direction to 0 now, because there may
        // not be a 0018,9089 for this frame.
        for(int i = 1; i < 4; ++i)  // 1-3 is intentional.
            ptvd->_dtiV[i] = 0.0;
    }
    else{
        ptvd->_isPhilipsNonDirectional = false;
        // Wait for 0018,9089 to get the direction.
    }
    _update_tvd(ptvd);
} //set_directionality0018_9075()

int set_bValGE(struct TVolumeDiffusion* ptvd, int lLength, unsigned char* inbuf) {
    //see Series 16 https://github.com/nikadon/cc-dcm2bids-wrapper/tree/master/dicom-qa-examples/ge-mr750-dwi-b-vals#table b750 = 1000000750\8\0\0 b1500 = 1000001500\8\0\0
    int bVal = dcmStrInt(lLength, inbuf);
    bVal = (bVal % 10000);
    ptvd->_dtiV[0] = bVal;
    //printf("(0043,1039) '%s' Slop_int_6 -->%d  \n", inbuf, bVal);
    //dd.CSA.numDti = 1;   // Always true for GE.
    _update_tvd(ptvd);
    return bVal;
} //set_bValGE()

// axis: 0 -> x, 1 -> y , 2 -> z
void set_diffusion_directionPhilips(struct TVolumeDiffusion* ptvd, float vec, const int axis){
    ptvd->_dtiV[axis + 1] = vec;
	//printf("(2005,10b0..2) v[%d]=%g\n", axis, ptvd->_dtiV[axis + 1]);
    _update_tvd(ptvd);
}//set_diffusion_directionPhilips()

// axis: 0 -> x, 1 -> y , 2 -> z
void set_diffusion_directionGE(struct TVolumeDiffusion* ptvd, int lLength, unsigned char* inbuf, const int axis){
    ptvd->_dtiV[axis + 1] = dcmStrFloat(lLength, inbuf);
	//printf("(0019,10bb..d) v[%d]=%g\n", axis, ptvd->_dtiV[axis + 1]);
    _update_tvd(ptvd);
}//set_diffusion_directionGE()

void dcmMultiFloatDouble (size_t lByteLength, unsigned char lBuffer[], size_t lnFloats, float *lFloats, bool isLittleEndian) {
    size_t floatlen = lByteLength / lnFloats;
    for(size_t i = 0; i < lnFloats; ++i)
        lFloats[i] = dcmFloatDouble((int)floatlen, lBuffer + i * floatlen, isLittleEndian);
} //dcmMultiFloatDouble()


void set_orientation0018_9089(struct TVolumeDiffusion* ptvd, int lLength, unsigned char* inbuf, bool isLittleEndian) {
    if(ptvd->_isPhilipsNonDirectional){
        for(int i = 1; i < 4; ++i) // Deliberately ignore inbuf; it might be nonsense.
            ptvd->_dtiV[i] = 0.0;
    }
    else
        dcmMultiFloatDouble(lLength, inbuf, 3, ptvd->_dtiV + 1, isLittleEndian);
    _update_tvd(ptvd);
}//set_orientation0018_9089()

void set_bVal(struct TVolumeDiffusion* ptvd, const float b) {
    ptvd->_dtiV[0] = b;
    _update_tvd(ptvd);
}//set_bVal()

void set_bMatrix(struct TVolumeDiffusion* ptvd, double b, int idx) {
 if ((idx < 0) || (idx > 5)) return;
 ptvd->_symBMatrix[idx] = b;
 _update_tvd(ptvd);
}

void set_isAtFirstPatientPosition_tvd(struct TVolumeDiffusion* ptvd, const bool iafpp) {
    ptvd->_isAtFirstPatientPosition = iafpp;
    _update_tvd(ptvd);
}//set_isAtFirstPatientPosition_tvd()

// Update the diffusion info in dd and *pdti4D for a volume once all the
// diffusion info for that volume has been read into pvd.
//
// Note that depending on the scanner software the diffusion info can arrive in
// different tags, in different orders (because of enclosing sequence tags),
// and the values in some tags may be invalid, or may be essential, depending
// on the presence of other tags.  Thus it is best to gather all the diffusion
// info for a volume (frame) before taking action on it.
//
// On the other hand, dd and *pdti4D need to be updated as soon as the
// diffusion info is ready, before diffusion info for the next volume is read
// in.
void _update_tvd(struct TVolumeDiffusion* ptvd) {
    // Figure out if we have both the b value and direction (if any) for this
    // volume, and if isFirstPosition.

    // // GE (software version 27) is liable to NOT include kDiffusion_b_value for the
    // // slice if it is 0, but should still have kDiffusionBFactor, which comes
    // // after PatientPosition.
    // if(isAtFirstPatientPosition && manufacturer == kMANUFACTURER_GE && dtiV[0] < 0)
    //   dtiV[0] = 0;  // Implied 0.
	bool isReady = (ptvd->_isAtFirstPatientPosition && (ptvd->_dtiV[0] >= 0));
    if(!isReady) return; //no B=0
	if(isReady){
        for(int i = 1; i < 4; ++i){
            if(ptvd->_dtiV[i] > 1){
                isReady = false;
                break;
            }
        }
    }
    if(!isReady){ //bvecs NOT filled: see if symBMatrix filled
    	isReady = true;
    	for(int i = 1; i < 6; ++i)
    		if (isnan(ptvd->_symBMatrix[i])) isReady = false;
    	if(!isReady) return; //	symBMatrix not filled
		//START BRUKER KLUDGE
		//see issue 265: Bruker stores xx,xy,xz,yx,yy,yz instead of xx,xy,xz,yy,yz,zz
		// we can recover since xx+yy+zz = bval
		// since any value squared is positive, a negative diagonal reveals fault
		double xx = ptvd->_symBMatrix[0]; //x*x
		double xy = ptvd->_symBMatrix[1]; //x*y
		double xz = ptvd->_symBMatrix[2]; //x*z
		double yy = ptvd->_symBMatrix[3]; //y*y
		double yz = ptvd->_symBMatrix[4]; //y*z
		double zz = ptvd->_symBMatrix[5]; //z*z
		bool isBrukerBug = false;
		if ((xx < 0.0) || (yy < 0.0) || (zz < 0.0)) isBrukerBug = true;
		double sumDiag = ptvd->_symBMatrix[0]+ptvd->_symBMatrix[3]+ptvd->_symBMatrix[5]; //if correct xx+yy+zz = bval
		double bVecError = fabs(sumDiag - ptvd->pdd->CSA.dtiV[0]);
		if (bVecError > 0.5) isBrukerBug = true;
		//next: check diagonals
		double x = sqrt(xx);
		double y = sqrt(yy);
		double z = sqrt(zz);
		if ( (fabs((x*y)-xy)) > 0.5) isBrukerBug = true;
		if ( (fabs((x*z)-xz)) > 0.5) isBrukerBug = true;
		if ( (fabs((y*z)-yz)) > 0.5) isBrukerBug = true;
		if (isBrukerBug) printWarning("Fixing corrupt bmat (issue 265). [%g %g %g %g %g %g]\n", xx,xy,xz,yy,yz,zz);
		if (isBrukerBug) {
			ptvd->_symBMatrix[3] = ptvd->_symBMatrix[4];
			ptvd->_symBMatrix[4] = ptvd->_symBMatrix[5];
			//next: solve for zz given bvalue, xx, and yy
			ptvd->_symBMatrix[5] = ptvd->_dtiV[0] - ptvd->_symBMatrix[0] - ptvd->_symBMatrix[3];
			if ((ptvd->_symBMatrix[0] < 0.0) || (ptvd->_symBMatrix[5] < 0.0)) printError("DICOM BMatrix corrupt.\n");
		}
		//END BRUKER_KLUDGE
    	vec3 bVec = nifti_mat33_eig3(ptvd->_symBMatrix[0], ptvd->_symBMatrix[1], ptvd->_symBMatrix[2], ptvd->_symBMatrix[3], ptvd->_symBMatrix[4], ptvd->_symBMatrix[5]);
		ptvd->_dtiV[1] = bVec.v[0];
		ptvd->_dtiV[2] = bVec.v[1];
		ptvd->_dtiV[3] = bVec.v[2];
		//printf("bmat=[%g %g %g %g %g %g %g %g %g]\n", ptvd->_symBMatrix[0],ptvd->_symBMatrix[1],ptvd->_symBMatrix[2],  ptvd->_symBMatrix[1],ptvd->_symBMatrix[3],ptvd->_symBMatrix[4], ptvd->_symBMatrix[2],ptvd->_symBMatrix[4],ptvd->_symBMatrix[5]);
		//printf("bmats=[%g %g %g %g %g %g];\n", ptvd->_symBMatrix[0],ptvd->_symBMatrix[1],ptvd->_symBMatrix[2],ptvd->_symBMatrix[3],ptvd->_symBMatrix[4],ptvd->_symBMatrix[5]);
		//printf("bvec=[%g %g %g];\n", ptvd->_dtiV[1], ptvd->_dtiV[2], ptvd->_dtiV[3]);
		//printf("bval=%g;\n\n", ptvd->_dtiV[0]);
    }
    if(!isReady) return;
    // If still here, update dd and *pdti4D.
    ptvd->pdd->CSA.numDti++;
    if (ptvd->pdd->CSA.numDti == 2) {                  // First time we know that this is a 4D DTI dataset;
        for(int i = 0; i < 4; ++i)                       // Start *pdti4D before ptvd->pdd->CSA.dtiV
            ptvd->pdti4D->S[0].V[i] = ptvd->pdd->CSA.dtiV[i];    // is updated.
    }
    for(int i = 0; i < 4; ++i)                         // Update pdd
        ptvd->pdd->CSA.dtiV[i] = ptvd->_dtiV[i];
    if((ptvd->pdd->CSA.numDti > 1) && (ptvd->pdd->CSA.numDti < kMaxDTI4D)){   // Update *pdti4D
        //d.dti4D = (TDTI *)malloc(kMaxDTI4D * sizeof(TDTI));
        for(int i = 0; i < 4; ++i)
            ptvd->pdti4D->S[ptvd->pdd->CSA.numDti - 1].V[i] = ptvd->_dtiV[i];
    }
    clear_volume(ptvd); // clear the slate for the next volume.
}//_update_tvd()
//END RIR

struct TDCMdim { //DimensionIndexValues
  uint32_t dimIdx[MAX_NUMBER_OF_DIMENSIONS];
  uint32_t diskPos;
  float triggerDelayTime, TE, intenScale, intenIntercept, intenScalePhilips, RWVScale, RWVIntercept;
  float V[4];
  bool isPhase;
  bool isReal;
  bool isImaginary;
};

void getFileNameX( char *pathParent, const char *path, int maxLen) {//if path is c:\d1\d2 then filename is 'd2'
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
    strncpy(pathParent,filename, maxLen-1);
}

void getFileName( char *pathParent, const char *path) {//if path is c:\d1\d2 then filename is 'd2'
getFileNameX(pathParent, path, kDICOMStr);
}

#ifdef USING_R

// True iff dcm1 sorts *before* dcm2
bool compareTDCMdim (const TDCMdim &dcm1, const TDCMdim &dcm2) {
    for (int i=MAX_NUMBER_OF_DIMENSIONS-1; i >=0; i--){
        if(dcm1.dimIdx[i] < dcm2.dimIdx[i])
            return true;
        else if(dcm1.dimIdx[i] > dcm2.dimIdx[i])
            return false;
    }
    return false;
} //compareTDCMdim()

#else

int compareTDCMdim(void const *item1, void const *item2) {
	struct TDCMdim const *dcm1 = (const struct TDCMdim *)item1;
	struct TDCMdim const *dcm2 = (const struct TDCMdim *)item2;
	//for(int i=0; i < MAX_NUMBER_OF_DIMENSIONS; ++i){
	for(int i=MAX_NUMBER_OF_DIMENSIONS-1; i >=0; i--){

		if(dcm1->dimIdx[i] < dcm2->dimIdx[i])
		  return -1;
		else if(dcm1->dimIdx[i] > dcm2->dimIdx[i])
		  return 1;
	}
	return 0;
} //compareTDCMdim()

#endif // USING_R

struct TDICOMdata readDICOMv(char * fname, int isVerbose, int compressFlag, struct TDTI4D *dti4D) {
	struct TDICOMdata d = clear_dicom_data();
	d.imageNum = 0; //not set
    strcpy(d.protocolName, ""); //erase dummy with empty
    strcpy(d.protocolName, ""); //erase dummy with empty
    strcpy(d.seriesDescription, ""); //erase dummy with empty
    strcpy(d.sequenceName, ""); //erase dummy with empty
    //do not read folders - code specific to GCC (LLVM/Clang seems to recognize a small file size)
	dti4D->sliceOrder[0] = -1;
	struct TVolumeDiffusion volDiffusion = initTVolumeDiffusion(&d, dti4D);
    struct stat s;
    if( stat(fname,&s) == 0 ) {
        if( !(s.st_mode & S_IFREG) ){
            printMessage( "DICOM read fail: not a valid file (perhaps a directory) %s\n",fname);
            return d;
        }
    }
    bool isPart10prefix = true;
    int isOK = isDICOMfile(fname);
    if (isOK == 0) return d;
    if (isOK == 2) {
    	d.isExplicitVR = false;
    	isPart10prefix = false;
    }
    FILE *file = fopen(fname, "rb");
	if (!file) {
        printMessage("Unable to open file %s\n", fname);
		return d;
	}
	fseek(file, 0, SEEK_END);
	long fileLen=ftell(file); //Get file length
    if (fileLen < 256) {
        printMessage( "File too small to be a DICOM image %s\n", fname);
		return d;
	}
	//Since size of DICOM header is unknown, we will load it in 1mb segments
	//This uses less RAM and makes is faster for computers with slow disk access
	//Benefit is largest for 4D images.
	//To disable caching and load entire file to RAM, compile with "-dmyLoadWholeFileToReadHeader"
	//To implement the segments, we define these variables:
	// fileLen = size of file in bytes
	// MaxBufferSz = maximum size of buffer in bytes
	// Buffer = array with n elements, where n is smaller of fileLen or MaxBufferSz
	// lPos = position in Buffer (indexed from 0), 0..(n-1)
	// lFileOffset = offset of Buffer in file: true file position is lOffset+lPos (initially 0)
	#ifdef myLoadWholeFileToReadHeader
	size_t MaxBufferSz = fileLen;
	#else
	size_t MaxBufferSz = 1000000; //ideally size of DICOM header, but this varies from 2D to 4D files
	#endif
	if (MaxBufferSz > (size_t)fileLen)
		MaxBufferSz = fileLen;
	//printf("%d -> %d\n", MaxBufferSz, fileLen);
	long lFileOffset = 0;
	fseek(file, 0, SEEK_SET);
	//Allocate memory
	unsigned char *buffer=(unsigned char *)malloc(MaxBufferSz+1);
	if (!buffer) {
		printError( "Memory exhausted!");
        fclose(file);
		return d;
	}
	//Read file contents into buffer
	size_t sz = fread(buffer, 1, MaxBufferSz, file);
	if (sz < MaxBufferSz) {
         printError("Only loaded %zu of %zu bytes for %s\n", sz, MaxBufferSz, fname);
         fclose(file);
         return d;
    }
	#ifdef myLoadWholeFileToReadHeader
	fclose(file);
	#endif
    //DEFINE DICOM TAGS
#define  kUnused 0x0001+(0x0001 << 16 )
#define  kStart 0x0002+(0x0000 << 16 )
#define  kMediaStorageSOPClassUID 0x0002+(0x0002 << 16 )
#define  kMediaStorageSOPInstanceUID 0x0002+(0x0003 << 16 )
#define  kTransferSyntax 0x0002+(0x0010 << 16)
#define  kImplementationVersionName 0x0002+(0x0013 << 16)
#define  kSourceApplicationEntityTitle 0x0002+(0x0016 << 16 )
#define  kDirectoryRecordSequence 0x0004+(0x1220 << 16 )
//#define  kSpecificCharacterSet 0x0008+(0x0005 << 16 ) //someday we should handle foreign characters...
#define  kImageTypeTag 0x0008+(0x0008 << 16 )
//#define  kSOPInstanceUID 0x0008+(0x0018 << 16 ) //Philips inserts time as last item, e.g. ?.?.?.YYYYMMDDHHmmSS.SSSS
// not reliable  https://neurostars.org/t/heudiconv-no-extraction-of-slice-timing-data-based-on-philips-dicoms/2201/21
#define  kStudyDate 0x0008+(0x0020 << 16 )
#define  kAcquisitionDate 0x0008+(0x0022 << 16 )
#define  kAcquisitionDateTime 0x0008+(0x002A << 16 )
#define  kStudyTime 0x0008+(0x0030 << 16 )
#define  kAcquisitionTime 0x0008+(0x0032 << 16 ) //TM
//#define  kContentTime 0x0008+(0x0033 << 16 ) //TM
#define  kModality 0x0008+(0x0060 << 16 ) //CS
#define  kManufacturer 0x0008+(0x0070 << 16 )
#define  kInstitutionName 0x0008+(0x0080 << 16 )
#define  kInstitutionAddress 0x0008+(0x0081 << 16 )
#define  kReferringPhysicianName 0x0008+(0x0090 << 16 )
#define  kStationName 0x0008+(0x1010 << 16 )
#define  kSeriesDescription 0x0008+(0x103E << 16 ) // '0008' '103E' 'LO' 'SeriesDescription'
#define  kInstitutionalDepartmentName  0x0008+(0x1040 << 16 )
#define  kManufacturersModelName 0x0008+(0x1090 << 16 )
#define  kDerivationDescription 0x0008+(0x2111 << 16 )
#define  kComplexImageComponent (uint32_t) 0x0008+(0x9208 << 16 )//'0008' '9208' 'CS' 'ComplexImageComponent'
#define  kAcquisitionContrast (uint32_t) 0x0008+(0x9209 << 16 )//'0008' '9209' 'CS' 'AcquisitionContrast'
#define  kPatientName 0x0010+(0x0010 << 16 )
#define  kPatientID 0x0010+(0x0020 << 16 )
#define  kAccessionNumber 0x0008+(0x0050 << 16 )
#define  kPatientBirthDate 0x0010+(0x0030 << 16 )
#define  kPatientSex 0x0010+(0x0040 << 16 )
#define  kPatientAge 0x0010+(0x1010 << 16 )
#define  kPatientWeight 0x0010+(0x1030 << 16 )
#define  kAnatomicalOrientationType 0x0010+(0x2210 << 16 )
#define  kBodyPartExamined 0x0018+(0x0015 << 16)
#define  kScanningSequence 0x0018+(0x0020 << 16)
#define  kSequenceVariant 0x0018+(0x0021 << 16)
#define  kScanOptions 0x0018+(0x0022 << 16)
#define  kMRAcquisitionType 0x0018+(0x0023 << 16)
#define  kSequenceName 0x0018+(0x0024 << 16)
#define  kZThick  0x0018+(0x0050 << 16 )
#define  kTR  0x0018+(0x0080 << 16 )
#define  kTE  0x0018+(0x0081 << 16 )
#define  kNumberOfAverages 0x0018+(0x0083 << 16 ) //DS
#define  kImagingFrequency 0x0018+(0x0084 << 16 ) //DS
#define  kTriggerTime  0x0018+(0x1060 << 16 ) //DS
//#define  kEffectiveTE  0x0018+(0x9082 << 16 )
const uint32_t kEffectiveTE  = 0x0018+ (0x9082 << 16);
#define  kTI  0x0018+(0x0082 << 16) // Inversion time
#define  kEchoNum  0x0018+(0x0086 << 16 ) //IS
#define  kMagneticFieldStrength  0x0018+(0x0087 << 16 ) //DS
#define  kZSpacing  0x0018+(0x0088 << 16 ) //'DS' 'SpacingBetweenSlices'
#define  kPhaseEncodingSteps  0x0018+(0x0089 << 16 ) //'IS'
#define  kEchoTrainLength  0x0018+(0x0091 << 16 ) //IS
#define  kPhaseFieldofView  0x0018+(0x0094 << 16 ) //'DS'
#define  kPixelBandwidth  0x0018+(0x0095 << 16 ) //'DS' 'PixelBandwidth'
#define  kDeviceSerialNumber  0x0018+(0x1000 << 16 ) //LO
#define  kSoftwareVersions  0x0018+(0x1020 << 16 ) //LO
#define  kProtocolName  0x0018+(0x1030<< 16 )
#define  kRadionuclideTotalDose  0x0018+(0x1074<< 16 )
#define  kRadionuclideHalfLife  0x0018+(0x1075<< 16 )
#define  kRadionuclidePositronFraction  0x0018+(0x1076<< 16 )
#define  kGantryTilt  0x0018+(0x1120  << 16 )
#define  kXRayExposure  0x0018+(0x1152  << 16 )
#define  kReceiveCoilName  0x0018+(0x1250  << 16 ) // SH
#define  kAcquisitionMatrix  0x0018+(0x1310  << 16 ) //US
#define  kFlipAngle  0x0018+(0x1314  << 16 )
#define  kInPlanePhaseEncodingDirection  0x0018+(0x1312<< 16 ) //CS
#define  kSAR  0x0018+(0x1316 << 16 ) //'DS' 'SAR'
#define  kPatientOrient  0x0018+(0x5100<< 16 )    //0018,5100. patient orientation - 'HFS'
#define  kAcquisitionDuration  0x0018+uint32_t(0x9073<< 16 ) //FD
//#define  kFrameAcquisitionDateTime  0x0018+uint32_t(0x9074<< 16 ) //DT "20181019212528.232500"
#define  kDiffusionDirectionality  0x0018+uint32_t(0x9075<< 16 )   // NONE, ISOTROPIC, or DIRECTIONAL
//#define  kDiffusionBFactorSiemens  0x0019+(0x100C<< 16 ) //   0019;000C;SIEMENS MR HEADER  ;B_value
#define  kDiffusion_bValue  0x0018+uint32_t(0x9087<< 16 ) // FD
#define  kDiffusionOrientation  0x0018+uint32_t(0x9089<< 16 ) // FD, seen in enhanced
                                                              // DICOM from Philips 5.*
                                                              // and Siemens XA10.
#define  kImagingFrequency2 0x0018+uint32_t(0x9098 << 16 ) //FD
#define  kDiffusionBValueXX 0x0018+uint32_t(0x9602 << 16 ) //FD
#define  kDiffusionBValueXY 0x0018+uint32_t(0x9603 << 16 ) //FD
#define  kDiffusionBValueXZ 0x0018+uint32_t(0x9604 << 16 ) //FD
#define  kDiffusionBValueYY 0x0018+uint32_t(0x9605 << 16 ) //FD
#define  kDiffusionBValueYZ 0x0018+uint32_t(0x9606 << 16 ) //FD
#define  kDiffusionBValueZZ 0x0018+uint32_t(0x9607 << 16 ) //FD
#define  kMREchoSequence  0x0018+uint32_t(0x9114<< 16 ) //SQ
#define  kMRAcquisitionPhaseEncodingStepsInPlane  0x0018+uint32_t(0x9231<< 16 ) //US
#define  kNumberOfImagesInMosaic  0x0019+(0x100A<< 16 ) //US NumberOfImagesInMosaic
#define  kDwellTime  0x0019+(0x1018<< 16 ) //IS in NSec, see https://github.com/rordenlab/dcm2niix/issues/127
//https://nmrimaging.wordpress.com/2011/12/20/when-we-process/
//  https://nciphub.org/groups/qindicom/wiki/DiffusionrelatedDICOMtags:experienceacrosssites?action=pdf
#define  kDiffusionBValueSiemens  0x0019+(0x100C<< 16 ) //IS
#define  kSliceTimeSiemens 0x0019+(0x1029<< 16) ///FD
#define  kDiffusionGradientDirectionSiemens  0x0019+(0x100E<< 16 ) //FD
#define  kNumberOfDiffusionDirectionGE 0x0019+(0x10E0<< 16) ///DS NumberOfDiffusionDirection:UserData24
#define  kLastScanLoc  0x0019+(0x101B<< 16 )
#define  kDiffusionDirectionGEX  0x0019+(0x10BB<< 16 ) //DS phase diffusion direction
#define  kDiffusionDirectionGEY  0x0019+(0x10BC<< 16 ) //DS frequency diffusion direction
#define  kDiffusionDirectionGEZ  0x0019+(0x10BD<< 16 ) //DS slice diffusion direction
#define  kSharedFunctionalGroupsSequence  0x5200+uint32_t(0x9229<< 16 ) // SQ
#define  kPerFrameFunctionalGroupsSequence  0x5200+uint32_t(0x9230<< 16 ) // SQ
#define  kBandwidthPerPixelPhaseEncode  0x0019+(0x1028<< 16 ) //FD
//#define  kRawDataRunNumberGE  0x0019+(0x10a2<< 16 ) //SL
#define  kStudyID 0x0020+(0x0010 << 16 )
#define  kSeriesNum 0x0020+(0x0011 << 16 )
#define  kAcquNum 0x0020+(0x0012 << 16 )
#define  kImageNum 0x0020+(0x0013 << 16 )
#define  kStudyInstanceUID 0x0020+(0x000D << 16 )
#define  kSeriesInstanceUID 0x0020+(0x000E << 16 )
#define  kImagePositionPatient 0x0020+(0x0032 << 16 )   // Actually !
#define  kOrientationACR 0x0020+(0x0035 << 16 )
//#define  kTemporalPositionIdentifier 0x0020+(0x0100 << 16 ) //IS
#define  kOrientation 0x0020+(0x0037 << 16 )
#define  kImagesInAcquisition 0x0020+(0x1002 << 16 ) //IS
#define  kImageComments 0x0020+(0x4000<< 16 )// '0020' '4000' 'LT' 'ImageComments'
#define  kFrameContentSequence 0x0020+uint32_t(0x9111<< 16 ) //SQ
#define  kTriggerDelayTime 0x0020+uint32_t(0x9153<< 16 ) //FD
#define  kDimensionIndexValues 0x0020+uint32_t(0x9157<< 16 ) // UL n-dimensional index of frame.
#define  kInStackPositionNumber 0x0020+uint32_t(0x9057<< 16 ) // UL can help determine slices in volume
#define  kDimensionIndexPointer 0x0020+uint32_t(0x9165<< 16 )
//Private Group 21 as Used by Siemens:
#define  kSequenceVariant21 0x0021+(0x105B<< 16 )//CS
#define  kPATModeText 0x0021+(0x1009<< 16 )//LO, see kImaPATModeText
#define  kTimeAfterStart 0x0021+(0x1104<< 16 )//DS
#define  kPhaseEncodingDirectionPositive 0x0021+(0x111C<< 16 )//IS
//#define  kRealDwellTime 0x0021+(0x1142<< 16 )//IS
#define  kBandwidthPerPixelPhaseEncode21 0x0021+(0x1153<< 16 )//FD
#define  kCoilElements 0x0021+(0x114F<< 16 )//LO
#define  kAcquisitionMatrixText21  0x0021+(0x1158 << 16 ) //SH
//Private Group 21 as used by GE:
#define  kLocationsInAcquisitionGE 0x0021+(0x104F<< 16 )//SS 'LocationsInAcquisitionGE'
#define  kRTIA_timer 0x0021+(0x105E<< 16 )//DS
#define  kProtocolDataBlockGE 0x0025+(0x101B<< 16 )//OB
#define  kSamplesPerPixel 0x0028+(0x0002 << 16 )
#define  kPhotometricInterpretation 0x0028+(0x0004 << 16 )
#define  kPlanarRGB 0x0028+(0x0006 << 16 )
#define  kDim3 0x0028+(0x0008 << 16 ) //number of frames - for Philips this is Dim3*Dim4
#define  kDim2 0x0028+(0x0010 << 16 )
#define  kDim1 0x0028+(0x0011 << 16 )
#define  kXYSpacing  0x0028+(0x0030 << 16 ) //DS 'PixelSpacing'
#define  kBitsAllocated 0x0028+(0x0100 << 16 )
#define  kBitsStored 0x0028+(0x0101 << 16 )//US 'BitsStored'
#define  kIsSigned 0x0028+(0x0103 << 16 ) //PixelRepresentation
#define  kPixelPaddingValue 0x0028+(0x0120 << 16 ) // https://github.com/rordenlab/dcm2niix/issues/262
#define  kFloatPixelPaddingValue 0x0028+(0x0122 << 16 ) // https://github.com/rordenlab/dcm2niix/issues/262
#define  kIntercept 0x0028+(0x1052 << 16 )
#define  kSlope 0x0028+(0x1053 << 16 )
//#define  kSpectroscopyDataPointColumns 0x0028+(0x9002 << 16 ) //IS
#define  kGeiisFlag 0x0029+(0x0010 << 16 ) //warn user if dreaded GEIIS was used to process image
#define  kCSAImageHeaderInfo  0x0029+(0x1010 << 16 )
#define  kCSASeriesHeaderInfo 0x0029+(0x1020 << 16 )
#define  kStudyComments 0x0032+(0x4000<< 16 )//LT StudyComments
//#define  kObjectGraphics  0x0029+(0x1210 << 16 )    //0029,1210 syngoPlatformOOGInfo Object Oriented Graphics
#define  kProcedureStepDescription 0x0040+(0x0254 << 16 )
#define  kRealWorldIntercept  0x0040+uint32_t(0x9224 << 16 ) //IS dicm2nii's SlopInt_6_9
#define  kRealWorldSlope  0x0040+uint32_t(0x9225 << 16 ) //IS dicm2nii's SlopInt_6_9
#define  kUserDefineDataGE  0x0043+(0x102A << 16 ) //OB
#define  kEffectiveEchoSpacingGE  0x0043+(0x102C << 16 ) //SS
#define  kDiffusionBFactorGE  0x0043+(0x1039 << 16 ) //IS dicm2nii's SlopInt_6_9
#define  kAcquisitionMatrixText  0x0051+(0x100B << 16 ) //LO
#define  kCoilSiemens  0x0051+(0x100F << 16 )
#define  kImaPATModeText  0x0051+(0x1011 << 16 )
#define  kLocationsInAcquisition  0x0054+(0x0081 << 16 )
//ftp://dicom.nema.org/MEDICAL/dicom/2014c/output/chtml/part03/sect_C.8.9.4.html
//If ImageType is REPROJECTION we slice direction is reversed - need example to test
// #define  kSeriesType  0x0054+(0x1000 << 16 )
#define  kDoseCalibrationFactor  0x0054+(0x1322<< 16 )
#define  kPETImageIndex  0x0054+(0x1330<< 16 )
#define  kPEDirectionDisplayedUIH  0x0065+(0x1005<< 16 )//SH
#define  kDiffusion_bValueUIH  0x0065+(0x1009<< 16 ) //FD
#define  kParallelInformationUIH  0x0065+(0x100D<< 16 ) //SH
#define  kNumberOfImagesInGridUIH  0x0065+(0x1050<< 16 ) //DS
#define  kMRVFrameSequenceUIH  0x0065+(0x1050<< 16 ) //SQ
#define  kDiffusionGradientDirectionUIH  0x0065+(0x1037<< 16 ) //FD
#define  kIconImageSequence 0x0088+(0x0200 << 16 )
#define  kElscintIcon 0x07a3+(0x10ce << 16 ) //see kGeiisFlag and https://github.com/rordenlab/dcm2niix/issues/239
#define  kPMSCT_RLE1 0x07a1+(0x100a << 16 ) //Elscint/Philips compression
#define  kPrivateCreator  0x2001+(0x0010 << 16 )// LO
#define  kDiffusionBFactor  0x2001+(0x1003 << 16 )// FL
//#define  kDiffusionDirectionPhilips  0x2001+(0x1004 << 16 )//CS Diffusion Direction
#define  kSliceNumberMrPhilips 0x2001+(0x100A << 16 ) //IS Slice_Number_MR
#define  kSliceOrient  0x2001+(0x100B << 16 )//2001,100B Philips slice orientation (TRANSVERSAL, AXIAL, SAGITTAL)
#define  kNumberOfSlicesMrPhilips 0x2001+(0x1018 << 16 )//SL 0x2001, 0x1018 ), "Number_of_Slices_MR"
//#define  kNumberOfLocationsPhilips  0x2001+(0x1015 << 16 ) //SS
//#define  kStackSliceNumber  0x2001+(0x1035 << 16 )//? Potential way to determine slice order for Philips?
#define  kNumberOfDynamicScans  0x2001+(0x1081 << 16 )//'2001' '1081' 'IS' 'NumberOfDynamicScans'
#define  kMRAcquisitionTypePhilips 0x2005+(0x106F << 16)
#define  kAngulationAP 0x2005+(0x1071 << 16)//'2005' '1071' 'FL' 'MRStackAngulationAP'
#define  kAngulationFH 0x2005+(0x1072 << 16)//'2005' '1072' 'FL' 'MRStackAngulationFH'
#define  kAngulationRL 0x2005+(0x1073 << 16)//'2005' '1073' 'FL' 'MRStackAngulationRL'
#define  kMRStackOffcentreAP 0x2005+(0x1078 << 16)
#define  kMRStackOffcentreFH 0x2005+(0x1079 << 16)
#define  kMRStackOffcentreRL 0x2005+(0x107A << 16)
#define  kPhilipsSlope 0x2005+(0x100E << 16 )
#define  kDiffusionDirectionRL 0x2005+(0x10B0 << 16)
#define  kDiffusionDirectionAP 0x2005+(0x10B1 << 16)
#define  kDiffusionDirectionFH 0x2005+(0x10B2 << 16)
#define  kPrivatePerFrameSq 0x2005+(0x140F << 16)
#define  kMRImageDiffBValueNumber 0x2005+(0x1412 << 16) //IS
//#define  kMRImageGradientOrientationNumber 0x2005+(0x1413 << 16) //IS
#define  kWaveformSq 0x5400+(0x0100 << 16)
#define  kSpectroscopyData 0x5600+(0x0020 << 16) //OF
#define  kImageStart 0x7FE0+(0x0010 << 16 )
#define  kImageStartFloat 0x7FE0+(0x0008 << 16 )
#define  kImageStartDouble 0x7FE0+(0x0009 << 16 )
uint32_t kItemTag = 0xFFFE +(0xE000 << 16 );
uint32_t kItemDelimitationTag = 0xFFFE +(0xE00D << 16 );
uint32_t kSequenceDelimitationItemTag = 0xFFFE +(0xE0DD << 16 );
double TE = 0.0; //most recent echo time recorded
	bool is2005140FSQ = false;
	//double contentTime = 0.0;
	int philMRImageDiffBValueNumber = 0;
	int sqDepth = 0;
	int acquisitionTimesGE_UIH = 0;
    int sqDepth00189114 = -1;
    bool hasDwiDirectionality = false;
    //int numFirstPatientPosition = 0;
    int nDimIndxVal = -1; //tracks Philips kDimensionIndexValues
    int locationsInAcquisitionGE = 0;
    int PETImageIndex = 0;
    int inStackPositionNumber = 0;
    uint32_t dimensionIndexPointer[MAX_NUMBER_OF_DIMENSIONS];
    size_t dimensionIndexPointerCounter = 0;
    int maxInStackPositionNumber = 0;
    //int temporalPositionIdentifier = 0;
    int locationsInAcquisitionPhilips = 0;
    int imagesInAcquisition = 0;
    //int sumSliceNumberMrPhilips = 0;
    int sliceNumberMrPhilips = 0;
    int numberOfFrames = 0;
    //int MRImageGradientOrientationNumber = 0;
    //int minGradNum = kMaxDTI4D + 1;
    //int maxGradNum = -1;
    int numberOfDynamicScans = 0;
    uint32_t lLength;
    uint32_t groupElement;
    long lPos = 0;
    bool isPhilipsDerived = false;
    //bool isPhilipsDiffusion = false;
    if (isPart10prefix) { //for part 10 files, skip preamble and prefix
    	lPos = 128+4; //4-byte signature starts at 128
    	groupElement = buffer[lPos] | (buffer[lPos+1] << 8) | (buffer[lPos+2] << 16) | (buffer[lPos+3] << 24);
    	if (groupElement != kStart)
        	printMessage("DICOM appears corrupt: first group:element should be 0x0002:0x0000 '%s'\n",  fname);
    } else { //no isPart10prefix - need to work out if this is explicit VR!
    	if (isVerbose > 1)
    		printMessage("DICOM preamble and prefix missing: this is not a valid DICOM image.\n");
    	//See Toshiba Aquilion images from  https://www.aliza-dicom-viewer.com/download/datasets
    	lLength = buffer[4] | (buffer[5] << 8) | (buffer[6] << 16) | (buffer[7] << 24);
    	if (lLength > fileLen) {
    		if (isVerbose > 1)
    			printMessage("Guessing this is an explicit VR image.\n");
    		d.isExplicitVR = true;
    	}
    }
    char vr[2];
    //float intenScalePhilips = 0.0;
    char acquisitionDateTimeTxt[kDICOMStr] = "";
    bool isEncapsulatedData = false;
    int multiBandFactor = 0;
    int frequencyRows = 0;
    int numberOfImagesInMosaic = 0;
    int encapsulatedDataFragments = 0;
    int encapsulatedDataFragmentStart = 0; //position of first FFFE,E000 for compressed images
    int encapsulatedDataImageStart = 0; //position of 7FE0,0010 for compressed images (where actual image start should be start of first fragment)
    bool isOrient = false;
    //bool isDcm4Che = false;
    bool isMoCo = false;
    bool isPaletteColor = false;
    bool isInterpolated = false;
    bool isIconImageSequence = false;
    bool isSwitchToImplicitVR = false;
    bool isSwitchToBigEndian = false;
    bool isAtFirstPatientPosition = false; //for 3d and 4d files: flag is true for slices at same position as first slice
    bool isMosaic = false;
    int patientPositionNum = 0;
    float B0Philips = -1.0;
    float vRLPhilips = 0.0;
    float vAPPhilips = 0.0;
    float vFHPhilips = 0.0;
    bool isPhase = false;
    bool isReal = false;
    bool isImaginary = false;
    bool isMagnitude = false;
    d.seriesNum = -1;
    float patientPositionPrivate[4] = {NAN, NAN, NAN, NAN};
    float patientPosition[4] = {NAN, NAN, NAN, NAN}; //used to compute slice direction for Philips 4D
    //float patientPositionPublic[4] = {NAN, NAN, NAN, NAN}; //used to compute slice direction for Philips 4D
    float patientPositionEndPhilips[4] = {NAN, NAN, NAN, NAN};
    float patientPositionStartPhilips[4] = {NAN, NAN, NAN, NAN};
	//struct TDTI philDTI[kMaxDTI4D];
    //for (int i = 0; i < kMaxDTI4D; i++)
    //	philDTI[i].V[0] = -1;
    //array for storing DimensionIndexValues
	int numDimensionIndexValues = 0;
#ifdef USING_R
    // Allocating a large array on the stack, as below, vexes valgrind and may cause overflow
    std::vector<TDCMdim> dcmDim(kMaxSlice2D);
#else
    TDCMdim dcmDim[kMaxSlice2D];
#endif
    for (int i = 0; i < kMaxSlice2D; i++) {
    	dcmDim[i].diskPos = i;
    	for (int j = 0; j < MAX_NUMBER_OF_DIMENSIONS; j++)
    		dcmDim[i].dimIdx[j] = 0;
    }
    //http://dicom.nema.org/dicom/2013/output/chtml/part05/sect_7.5.html
    //The array nestPos tracks explicit lengths for Data Element Tag of Value (FFFE,E000)
    //a delimiter (fffe,e000) can have an explicit length, in which case there is no delimiter (fffe,e00d)
    // fffe,e000 can provide explicit lengths, to demonstrate ./dcmconv +ti ex.DCM im.DCM
    #define kMaxNestPost 128
    int nNestPos = 0;
    size_t nestPos[kMaxNestPost];
	while ((d.imageStart == 0) && ((lPos+8+lFileOffset) <  fileLen)) {
    	#ifndef myLoadWholeFileToReadHeader //read one segment at a time
    	if ((size_t)(lPos + 128) > MaxBufferSz) { //avoid overreading the file
    		lFileOffset = lFileOffset + lPos;
    		if ((lFileOffset+MaxBufferSz) > (size_t)fileLen) MaxBufferSz = fileLen - lFileOffset;
			fseek(file, lFileOffset, SEEK_SET);
			size_t sz = fread(buffer, 1, MaxBufferSz, file);
			if (sz < MaxBufferSz) {
         		printError("Only loaded %zu of %zu bytes for %s\n", sz, MaxBufferSz, fname);
         		fclose(file);
         		return d;
    		}
			lPos = 0;
    	}
    	#endif
        if (d.isLittleEndian)
            groupElement = buffer[lPos] | (buffer[lPos+1] << 8) | (buffer[lPos+2] << 16) | (buffer[lPos+3] << 24);
        else
            groupElement = buffer[lPos+1] | (buffer[lPos] << 8) | (buffer[lPos+3] << 16) | (buffer[lPos+2] << 24);
        if ((isSwitchToBigEndian) && ((groupElement & 0xFFFF) != 2)) {
            isSwitchToBigEndian = false;
            d.isLittleEndian = false;
            groupElement = buffer[lPos+1] | (buffer[lPos] << 8) | (buffer[lPos+3] << 16) | (buffer[lPos+2] << 24);
        }//transfer syntax requests switching endian after group 0002
        if ((isSwitchToImplicitVR) && ((groupElement & 0xFFFF) != 2)) {
            isSwitchToImplicitVR = false;
            d.isExplicitVR = false;
        } //transfer syntax requests switching VR after group 0001
        //uint32_t group = (groupElement & 0xFFFF);
        lPos += 4;
	if ((groupElement == kItemDelimitationTag) || (groupElement == kSequenceDelimitationItemTag)) isIconImageSequence = false;
    //if (groupElement == kItemTag) sqDepth++;
    bool unNest = false;
    while ((nNestPos > 0) && (nestPos[nNestPos] <= (lFileOffset+lPos))) {
			nNestPos--;
			sqDepth--;
			unNest = true;
	}
	if (groupElement == kItemDelimitationTag) { //end of item with undefined length
		sqDepth--;
		unNest = true;
	}
	if (unNest)  {
    	is2005140FSQ = false;
    	if (sqDepth < 0) sqDepth = 0; //should not happen, but protect for faulty anonymization
    	//if we leave the folder MREchoSequence 0018,9114
    	if (( nDimIndxVal > 0) && ((d.manufacturer == kMANUFACTURER_BRUKER) || (d.manufacturer == kMANUFACTURER_PHILIPS)) && (sqDepth00189114 >= sqDepth)) {
    		sqDepth00189114 = -1; //triggered
    		//printf("slice %d---> 0020,9157 = %d %d %d\n", inStackPositionNumber, d.dimensionIndexValues[0], d.dimensionIndexValues[1], d.dimensionIndexValues[2]);
			if (inStackPositionNumber > 0) {
				//for images without SliceNumberMrPhilips (2001,100A)
				int sliceNumber = inStackPositionNumber;
				//printf("slice %d \n", sliceNumber);
				if ((sliceNumber == 1) && (!isnan(patientPosition[1])) ) {
					for (int k = 0; k < 4; k++)
						patientPositionStartPhilips[k] = patientPosition[k];
				} else if ((sliceNumber == 1) && (!isnan(patientPositionPrivate[1]))) {
					for (int k = 0; k < 4; k++)
						patientPositionStartPhilips[k] = patientPositionPrivate[k];
				}
				if ((sliceNumber == maxInStackPositionNumber) && (!isnan(patientPosition[1])) ) {
					for (int k = 0; k < 4; k++)
						patientPositionEndPhilips[k] = patientPosition[k];
				} else if ((sliceNumber == maxInStackPositionNumber) && (!isnan(patientPositionPrivate[1])) ) {
					for (int k = 0; k < 4; k++)
						patientPositionEndPhilips[k] = patientPositionPrivate[k];
				}
				patientPosition[1] = NAN;
				patientPositionPrivate[1] = NAN;
			}
			inStackPositionNumber = 0;
			if (numDimensionIndexValues >= kMaxSlice2D) {
				printError("Too many slices to track dimensions. Only up to %d are supported\n", kMaxSlice2D);
				break;
			}
            uint32_t dimensionIndexOrder[MAX_NUMBER_OF_DIMENSIONS];
            for(size_t i = 0; i < nDimIndxVal; i++)
                dimensionIndexOrder[i] = i;

            // Bruker Enhanced MR IOD: reorder dimensions to ensure InStackPositionNumber corresponds to the first one
            // This will ensure correct ordering of slices in 4D datasets
            if (d.manufacturer == kMANUFACTURER_BRUKER) {
                for(size_t i = 1; i < dimensionIndexPointerCounter; i++){
                    if (dimensionIndexPointer[i] == kInStackPositionNumber){
                        //swap with first
                        dimensionIndexOrder[i] = 0;
                        dimensionIndexOrder[0] = i;
                    }
                }
            }
			int ndim = nDimIndxVal;
			for (int i = 0; i < ndim; i++)
				dcmDim[numDimensionIndexValues].dimIdx[i] = d.dimensionIndexValues[dimensionIndexOrder[i]];
			dcmDim[numDimensionIndexValues].TE = TE;
			dcmDim[numDimensionIndexValues].intenScale = d.intenScale;
			dcmDim[numDimensionIndexValues].intenIntercept = d.intenIntercept;
			dcmDim[numDimensionIndexValues].isPhase = isPhase;
			dcmDim[numDimensionIndexValues].isReal = isReal;
			dcmDim[numDimensionIndexValues].isImaginary = isImaginary;
			dcmDim[numDimensionIndexValues].intenScalePhilips = d.intenScalePhilips;
			dcmDim[numDimensionIndexValues].RWVScale = d.RWVScale;
			dcmDim[numDimensionIndexValues].RWVIntercept = d.RWVIntercept;
			dcmDim[numDimensionIndexValues].triggerDelayTime = d.triggerDelayTime;
			dcmDim[numDimensionIndexValues].V[0] = -1.0;
			#ifdef MY_DEBUG
			if (numDimensionIndexValues < 19) {
				printMessage("dimensionIndexValues0020x9157[%d] = [", numDimensionIndexValues);
				for (int i = 0; i < ndim; i++)
					printMessage("%d ", d.dimensionIndexValues[i]);
				printMessage("]\n");
				//printMessage("B0= %g  num=%d\n", B0Philips, gradNum);
			} else return d;
			#endif
			//next: add diffusion if reported
			if (B0Philips >= 0.0) { //diffusion parameters
				// Philips does not always provide 2005,1413 (MRImageGradientOrientationNumber) and sometimes after dimensionIndexValues
				/*int gradNum = 0;
				for (int i = 0; i < ndim; i++)
					if (d.dimensionIndexValues[i] > 0) gradNum = d.dimensionIndexValues[i];
				if (gradNum <= 0) break;
				With Philips 51.0 both ADC and B=0 are saved as same direction, though they vary in another dimension
				(0018,9075) CS [ISOTROPIC]
				(0020,9157) UL 1\2\1\33 << ADC MAP
				(0018,9075) CS [NONE]
				(0020,9157) UL 1\1\2\33
				next two lines attempt to skip ADC maps
				we could also increment gradNum for ADC if we wanted...
				*/
				if (isPhilipsDerived) {
					//gradNum ++;
					B0Philips = 2000.0;
					vRLPhilips = 0.0;
					vAPPhilips = 0.0;
					vFHPhilips = 0.0;
				}
				if (B0Philips == 0.0) {
					//printMessage(" DimensionIndexValues grad %d b=%g vec=%gx%gx%g\n", gradNum, B0Philips, vRLPhilips, vAPPhilips, vFHPhilips);
					vRLPhilips = 0.0;
					vAPPhilips = 0.0;
					vFHPhilips = 0.0;
				}
				//if ((MRImageGradientOrientationNumber > 0) && ((gradNum != MRImageGradientOrientationNumber)) break;
				/*if (gradNum < minGradNum) minGradNum = gradNum;
				if (gradNum >= maxGradNum) maxGradNum = gradNum;
				if (gradNum >= kMaxDTI4D) {
						printError("Number of DTI gradients exceeds 'kMaxDTI4D (%d).\n", kMaxDTI4D);
				} else {
					gradNum = gradNum - 1;//index from 0
					philDTI[gradNum].V[0] = B0Philips;
					philDTI[gradNum].V[1] = vRLPhilips;
					philDTI[gradNum].V[2] = vAPPhilips;
					philDTI[gradNum].V[3] = vFHPhilips;
				}*/
				dcmDim[numDimensionIndexValues].V[0] = B0Philips;
				dcmDim[numDimensionIndexValues].V[1] = vRLPhilips;
				dcmDim[numDimensionIndexValues].V[2] = vAPPhilips;
				dcmDim[numDimensionIndexValues].V[3] = vFHPhilips;
				isPhilipsDerived = false;
				//printMessage(" DimensionIndexValues grad %d b=%g vec=%gx%gx%g\n", gradNum, B0Philips, vRLPhilips, vAPPhilips, vFHPhilips);
				//!!! 16032018 : next line as well as definition of B0Philips may need to be set to zero if Philips omits DiffusionBValue tag for B=0
				B0Philips = -1.0; //Philips may skip reporting B-values for B=0 volumes, so zero these
				vRLPhilips = 0.0;
				vAPPhilips = 0.0;
				vFHPhilips = 0.0;
				//MRImageGradientOrientationNumber = 0;
			}//diffusion parameters
    		numDimensionIndexValues ++;
			nDimIndxVal = -1; //we need DimensionIndexValues
    	} //record dimensionIndexValues slice information
    } //groupElement == kItemDelimitationTag : delimit item exits folder
    if (groupElement == kItemTag) {
    	uint32_t slen = dcmInt(4,&buffer[lPos],d.isLittleEndian);
    	uint32_t kUndefinedLen = 0xFFFFFFFF;
    	if (slen != kUndefinedLen) {
    		nNestPos++;
    		if (nNestPos >= kMaxNestPost) nNestPos = kMaxNestPost - 1;
    		nestPos[nNestPos] = slen+lFileOffset+lPos;
    	}
    	lLength = 4;
    	sqDepth++;
    	//return d;
    } else if (( (groupElement == kItemDelimitationTag) || (groupElement == kSequenceDelimitationItemTag)) && (!isEncapsulatedData)) {
            vr[0] = 'N';
            vr[1] = 'A';
            lLength = 4;
        } else if (d.isExplicitVR) {
            vr[0] = buffer[lPos]; vr[1] = buffer[lPos+1];
            if (buffer[lPos+1] < 'A') {//implicit vr with 32-bit length
                if (d.isLittleEndian)
                    lLength = buffer[lPos] | (buffer[lPos+1] << 8) | (buffer[lPos+2] << 16) | (buffer[lPos+3] << 24);
                else
                    lLength = buffer[lPos+3] | (buffer[lPos+2] << 8) | (buffer[lPos+1] << 16) | (buffer[lPos] << 24);
                lPos += 4;
            } else if ( ((buffer[lPos] == 'U') && (buffer[lPos+1] == 'N'))
                       || ((buffer[lPos] == 'U') && (buffer[lPos+1] == 'C'))
                       || ((buffer[lPos] == 'U') && (buffer[lPos+1] == 'R'))
                       || ((buffer[lPos] == 'U') && (buffer[lPos+1] == 'T'))
                       || ((buffer[lPos] == 'U') && (buffer[lPos+1] == 'V'))
                       || ((buffer[lPos] == 'O') && (buffer[lPos+1] == 'B'))
                       || ((buffer[lPos] == 'O') && (buffer[lPos+1] == 'D'))
                       || ((buffer[lPos] == 'O') && (buffer[lPos+1] == 'F'))
                       || ((buffer[lPos] == 'O') && (buffer[lPos+1] == 'L'))
                       | ((buffer[lPos] == 'O') && (buffer[lPos+1] == 'V'))
                       || ((buffer[lPos] == 'O') && (buffer[lPos+1] == 'W'))
                       || ((buffer[lPos] == 'S') && (buffer[lPos+1] == 'V'))
                       ) { //VR= UN, OB, OW, SQ  || ((buffer[lPos] == 'S') && (buffer[lPos+1] == 'Q'))
                //for example of UC/UR/UV/OD/OF/OL/OV/SV see VR conformance test https://www.aliza-dicom-viewer.com/download/datasets
                lPos = lPos + 4;  //skip 2 byte VR string and 2 reserved bytes = 4 bytes
                if (d.isLittleEndian)
                    lLength = buffer[lPos] | (buffer[lPos+1] << 8) | (buffer[lPos+2] << 16) | (buffer[lPos+3] << 24);
                else
                    lLength = buffer[lPos+3] | (buffer[lPos+2] << 8) | (buffer[lPos+1] << 16) | (buffer[lPos] << 24);
                lPos = lPos + 4;  //skip 4 byte length
            } else if   ((buffer[lPos] == 'S') && (buffer[lPos+1] == 'Q')) {
                lLength = 8; //Sequence Tag
                //printMessage(" !!!SQ\t%04x,%04x\n",   groupElement & 65535,groupElement>>16);
            } else { //explicit VR with 16-bit length
                if ((d.isLittleEndian)  )
                    lLength = buffer[lPos+2] | (buffer[lPos+3] << 8);
                else
                    lLength = buffer[lPos+3] | (buffer[lPos+2] << 8);
                lPos += 4;  //skip 2 byte VR string and 2 length bytes = 4 bytes
            }
        } else { //implicit VR
            vr[0] = 'U';
            vr[1] = 'N';
            if (d.isLittleEndian)
                lLength = buffer[lPos] | (buffer[lPos+1] << 8) | (buffer[lPos+2] << 16) | (buffer[lPos+3] << 24);
            else
                lLength = buffer[lPos+3] | (buffer[lPos+2] << 8) | (buffer[lPos+1] << 16) | (buffer[lPos] << 24);
            lPos += 4;  //we have loaded the 32-bit length
            if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (isSQ(groupElement))) { //https://github.com/rordenlab/dcm2niix/issues/144
            	vr[0] = 'S';
            	vr[1] = 'Q';
            	lLength = 0; //Do not skip kItemTag - required to determine nesting of Philips Enhanced
            }
        } //if explicit else implicit VR
        if (lLength == 0xFFFFFFFF) {
            lLength = 8; //SQ (Sequences) use 0xFFFFFFFF [4294967295] to denote unknown length
            //09032018 - do not count these as SQs: Horos does not count even groups
            //uint32_t special = dcmInt(4,&buffer[lPos],d.isLittleEndian);

            //http://dicom.nema.org/dicom/2013/output/chtml/part05/sect_7.5.html

			//if (special != ksqDelim) {
            	vr[0] = 'S';
            	vr[1] = 'Q';
            //}
        }
        /* //Handle SQs: for explicit these have VR=SQ
        if   ((vr[0] == 'S') && (vr[1] == 'Q')) {
			//http://dicom.nema.org/dicom/2013/output/chtml/part05/sect_7.5.html
			uint32_t special = dcmInt(4,&buffer[lPos],d.isLittleEndian);
            uint32_t slen = dcmInt(4,&buffer[lPos+4],d.isLittleEndian);
            //if (d.isExplicitVR)
            //	slen = dcmInt(4,&buffer[lPos+8],d.isLittleEndian);
			uint32_t kUndefinedLen = 0xFFFFFFFF;
			//printError(" SPECIAL >>>>t%04x,%04x  %08x %08x\n",   groupElement & 65535,groupElement>>16, special, slen);
			//return d;
			is2005140FSQ = (groupElement == kPrivatePerFrameSq);
			//if (isNextSQis2005140FSQ) is2005140FSQ = true;
			//isNextSQis2005140FSQ = false;
			if (special == kSequenceDelimitationItemTag) {
				//unknown
			} else if (slen == kUndefinedLen) {
				sqDepth++;
				if ((sqDepthPrivate == 0) && ((groupElement & 65535) % 2))
					sqDepthPrivate = sqDepth; //in a private SQ: ignore contents
			} else if ((is2005140FSQ) || ((groupElement & 65535) % 2)) {//private SQ of known length - lets jump over this!
				slen = lFileOffset + lPos + slen;
				if ((sqEndPrivate < 0) || (slen > sqEndPrivate)) //sqEndPrivate is signed
					sqEndPrivate = slen; //if nested private SQs, remember the end address of the top parent SQ
			}
		}
        //next: look for required tags
        if ((groupElement == kItemTag) && (isEncapsulatedData)) {
            d.imageBytes = dcmInt(4,&buffer[lPos],d.isLittleEndian);
            printMessage("compressed data %d-> %ld\n",d.imageBytes, lPos);

            d.imageBytes = dcmInt(4,&buffer[lPos-4],d.isLittleEndian);
            printMessage("compressed data %d-> %ld\n",d.imageBytes, lPos);
            if (d.imageBytes > 128) {
            	encapsulatedDataFragments++;
   				if (encapsulatedDataFragmentStart == 0)
                	encapsulatedDataFragmentStart = (int)lPos + (int)lFileOffset;
            }
        }
        if ((sqEndPrivate > 0) && ((lFileOffset + lPos) > sqEndPrivate))
        	sqEndPrivate = -1; //end of private SQ with defined length
        if (groupElement == kSequenceDelimitationItemTag) { //end of private SQ with undefined length
        	sqDepth--;
        	if (sqDepth < sqDepthPrivate) {
        		sqDepthPrivate = 0; //no longer in a private SQ
        	}
        }
        if (sqDepth < 0) sqDepth = 0;*/
        if ((groupElement == kItemTag)  && (isEncapsulatedData)) { //use this to find image fragment for compressed datasets, e.g. JPEG transfer syntax
            d.imageBytes = dcmInt(4,&buffer[lPos],d.isLittleEndian);
            lPos = lPos + 4;
            lLength = d.imageBytes;
            if (d.imageBytes > 128) {
            	encapsulatedDataFragments++;
   				if (encapsulatedDataFragmentStart == 0)
                	encapsulatedDataFragmentStart = (int)lPos + (int)lFileOffset;
            }
        }
        if ((isIconImageSequence) && ((groupElement & 0x0028) == 0x0028 )) groupElement = kUnused; //ignore icon dimensions
        switch ( groupElement ) {
         	case kMediaStorageSOPClassUID: {
         		char mediaUID[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], mediaUID);
                //Philips "XX_" files
                //see https://github.com/rordenlab/dcm2niix/issues/328
                if (strstr(mediaUID, "1.2.840.10008.5.1.4.1.1.66") != NULL) d.isRawDataStorage = true;
                if (strstr(mediaUID, "1.3.46.670589.11.0.0.12.1") != NULL) d.isRawDataStorage = true; //Private MR Spectrum Storage
                if (strstr(mediaUID, "1.3.46.670589.11.0.0.12.2") != NULL) d.isRawDataStorage = true; //Private MR Series Data Storage
                if (strstr(mediaUID, "1.3.46.670589.11.0.0.12.4") != NULL) d.isRawDataStorage = true; //Private MR Examcard Storage
                if (d.isRawDataStorage) d.isDerived = true;
                if (d.isRawDataStorage) printMessage("Skipping non-image DICOM: %s\n", fname);
                //Philips "PS_" files
                if (strstr(mediaUID, "1.2.840.10008.5.1.4.1.1.11.1") != NULL) d.isGrayscaleSoftcopyPresentationState = true;
                if (d.isGrayscaleSoftcopyPresentationState) d.isDerived = true;
                break;
         	}
            case kMediaStorageSOPInstanceUID : {// 0002, 0003
            	//char SOPInstanceUID[kDICOMStr];
            	dcmStr (lLength, &buffer[lPos], d.instanceUID);
            	//printMessage(">>%s\n", d.seriesInstanceUID);
            	d.instanceUidCrc = mz_crc32X((unsigned char*) &d.instanceUID, strlen(d.instanceUID));
                break;
            }
            case kTransferSyntax: {
                char transferSyntax[kDICOMStr];
                strcpy(transferSyntax, "");
                dcmStr (lLength, &buffer[lPos], transferSyntax);
                if (strcmp(transferSyntax, "1.2.840.10008.1.2.1") == 0)
                    ; //default isExplicitVR=true; //d.isLittleEndian=true
                else if  (strcmp(transferSyntax, "1.2.840.10008.1.2.4.50") == 0) {
                    d.compressionScheme = kCompress50;
                    //printMessage("Lossy JPEG: please decompress with Osirix or dcmdjpg. %s\n", transferSyntax);
                    //d.imageStart = 1;//abort as invalid (imageStart MUST be >128)
                } else if (strcmp(transferSyntax, "1.2.840.10008.1.2.4.51") == 0) {
                        d.compressionScheme = kCompress50;
                        //printMessage("Lossy JPEG: please decompress with Osirix or dcmdjpg. %s\n", transferSyntax);
                        //d.imageStart = 1;//abort as invalid (imageStart MUST be >128)
                //uJPEG does not decode these: ..53 ...55
                // } else if (strcmp(transferSyntax, "1.2.840.10008.1.2.4.53") == 0) {
                //    d.compressionScheme = kCompress50;
                } else if (strcmp(transferSyntax, "1.2.840.10008.1.2.4.57") == 0) {
                    //d.isCompressed = true;
                    //https://www.medicalconnections.co.uk/kb/Transfer_Syntax should be SOF = 0xC3
                    d.compressionScheme = kCompressC3;
                    //printMessage("Ancient JPEG-lossless (SOF type 0xc3): please check conversion\n");
                } else if (strcmp(transferSyntax, "1.2.840.10008.1.2.4.70") == 0) {
                    d.compressionScheme = kCompressC3;
                } else if ((strcmp(transferSyntax, "1.2.840.10008.1.2.4.80") == 0)  || (strcmp(transferSyntax, "1.2.840.10008.1.2.4.81") == 0)){
                    #if defined(myEnableJPEGLS) || defined(myEnableJPEGLS1)
                    d.compressionScheme = kCompressJPEGLS;
                    #else
                    printWarning("Unsupported transfer syntax '%s' (decode with 'dcmdjpls jpg.dcm raw.dcm' or 'gdcmconv -w jpg.dcm raw.dcm', or recompile dcm2niix with JPEGLS support)\n",transferSyntax);
                    d.imageStart = 1;//abort as invalid (imageStart MUST be >128)
                    #endif
                } else if (strcmp(transferSyntax, "1.3.46.670589.33.1.4.1") == 0) {
                    d.compressionScheme = kCompressPMSCT_RLE1;
                    //printMessage("Unsupported transfer syntax '%s' (decode with rle2img)\n",transferSyntax);
                    //d.imageStart = 1;//abort as invalid (imageStart MUST be >128)
                } else if ((compressFlag != kCompressNone) && (strcmp(transferSyntax, "1.2.840.10008.1.2.4.90") == 0)) {
                    d.compressionScheme = kCompressYes;
                    //printMessage("JPEG2000 Lossless support is new: please validate conversion\n");
                } else if ((compressFlag != kCompressNone) && (strcmp(transferSyntax, "1.2.840.10008.1.2.4.91") == 0)) {
                    d.compressionScheme = kCompressYes;
                    //printMessage("JPEG2000 support is new: please validate conversion\n");
                } else if (strcmp(transferSyntax, "1.2.840.10008.1.2.5") == 0)
                    d.compressionScheme = kCompressRLE; //run length
                else if (strcmp(transferSyntax, "1.2.840.10008.1.2.2") == 0)
                    isSwitchToBigEndian = true; //isExplicitVR=true;
                else if (strcmp(transferSyntax, "1.2.840.10008.1.2") == 0)
                    isSwitchToImplicitVR = true; //d.isLittleEndian=true
                else {
                	if (lLength < 1) //"1.2.840.10008.1.2"
                    	printWarning("Missing transfer syntax: assuming default (1.2.840.10008.1.2)\n");
                    else {
                    	printWarning("Unsupported transfer syntax '%s' (see www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage)\n",transferSyntax);
                    	d.imageStart = 1;//abort as invalid (imageStart MUST be >128)
                    }
                }
                break;} //{} provide scope for variable 'transferSyntax
            /*case kImplementationVersionName: {
            	char impTxt[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], impTxt);
                int slen = (int) strlen(impTxt);
				if((slen < 6) || (strstr(impTxt, "OSIRIX") == NULL) ) break;
                printError("OSIRIX Detected\n");
            	break; }*/
            case kImplementationVersionName: {
                char impTxt[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], impTxt);
                int slen = (int) strlen(impTxt);
				//if ((slen > 5) && (strstr(impTxt, "dcm4che") != NULL) )
				//	isDcm4Che = true;
				if((slen < 5) || (strstr(impTxt, "XA10A") == NULL) ) break;
				d.isXA10A = true;
            	break; }
            case kSourceApplicationEntityTitle: {
            	char saeTxt[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], saeTxt);
                int slen = (int) strlen(saeTxt);
				if((slen < 5) || (strstr(saeTxt, "oasis") == NULL) ) break;
                d.isSegamiOasis = true;
            	break; }
            case kDirectoryRecordSequence: {
                d.isRawDataStorage = true;
                break;       
            }
            case kImageTypeTag: {
            	dcmStr (lLength, &buffer[lPos], d.imageType);
                int slen;
                slen = (int) strlen(d.imageType);
				if((slen > 5) && strstr(d.imageType, "_MOCO_") ) {
                	//d.isDerived = true; //this would have 'i- y' skip MoCo images
                	isMoCo = true;
                }
				if((slen > 5) && strstr(d.imageType, "_ADC_") )
                	d.isDerived = true;
				if((slen > 5) && strstr(d.imageType, "_TRACEW_") )
                	d.isDerived = true;
				if((slen > 5) && strstr(d.imageType, "_TRACE_") )
                	d.isDerived = true;
				if((slen > 5) && strstr(d.imageType, "_FA_") )
                	d.isDerived = true;
				//if (strcmp(transferSyntax, "ORIGINAL_PRIMARY_M_ND_MOSAIC") == 0)
                if((slen > 5) && !strcmp(d.imageType + slen - 6, "MOSAIC") )
                	isMosaic = true;
                //const char* prefix = "MOSAIC";
                const char *pos = strstr(d.imageType, "MOSAIC");
                //const char p = (const char *) d.imageType;
                //p = (const char) strstr(d.imageType, "MOSAIC");
                //const char* p = strstr(d.imageType, "MOSAIC");
                if (pos != NULL)
    				isMosaic = true;
                //isNonImage 0008,0008 = DERIVED,CSAPARALLEL,POSDISP
                // sometime ComplexImageComponent 0008,9208 is missing - see ADNI data
                // attempt to detect non-images, see https://github.com/scitran/data/blob/a516fdc39d75a6e4ac75d0e179e18f3a5fc3c0af/scitran/data/medimg/dcm/mr/siemens.py
                //For Philips combinations see Table 3-28 Table 3-28: Valid combinations of Image Type applied values
                //  http://incenter.medical.philips.com/doclib/enc/fetch/2000/4504/577242/577256/588723/5144873/5144488/5144982/DICOM_Conformance_Statement_Intera_R7%2c_R8_and_R9.pdf%3fnodeid%3d5147977%26vernum%3d-2
                if((slen > 3) && (strstr(d.imageType, "_R_") != NULL) ) {
                	d.isHasReal = true;
                	isReal = true;
                }
                if((slen > 3) && (strstr(d.imageType, "_M_") != NULL) ) {
                	d.isHasMagnitude = true;
                	isMagnitude = true;
                }
                if((slen > 3) && (strstr(d.imageType, "_I_") != NULL) ) {
                	d.isHasImaginary = true;
                	isImaginary = true;
                }
                if((slen > 3) && (strstr(d.imageType, "_P_") != NULL) ) {
                	d.isHasPhase = true;
                	isPhase = true;
                }
                if((slen > 6) && (strstr(d.imageType, "_REAL_") != NULL) ) {
                	d.isHasReal = true;
                	isReal = true;
                }
                if((slen > 11) && (strstr(d.imageType, "_MAGNITUDE_") != NULL) ) {
                	d.isHasMagnitude = true;
                	isMagnitude = true;
                }
                if((slen > 11) && (strstr(d.imageType, "_IMAGINARY_") != NULL) ) {
                	d.isHasImaginary = true;
                	isImaginary = true;
                }
				if((slen > 6) && (strstr(d.imageType, "PHASE") != NULL) ) {
                	d.isHasPhase = true;
                	isPhase = true;
                }
                if((slen > 6) && (strstr(d.imageType, "DERIVED") != NULL) )
                	d.isDerived = true;
                //if((slen > 4) && (strstr(typestr, "DIS2D") != NULL) )
                //	d.isNonImage = true;
                //not mutually exclusive: possible for Philips enhanced DICOM to store BOTH magnitude and phase in the same image
            	break; }
            case kAcquisitionDate:
            	char acquisitionDateTxt[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], acquisitionDateTxt);
                d.acquisitionDate = atof(acquisitionDateTxt);
            	break;
            case kAcquisitionDateTime:
            	//char acquisitionDateTimeTxt[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], acquisitionDateTimeTxt);
                //printMessage("%s\n",acquisitionDateTimeTxt);
            	break;
            case kStudyDate:
                dcmStr (lLength, &buffer[lPos], d.studyDate);
                break;
            case kModality:
                if (lLength < 2) break;
                if ((buffer[lPos]=='C') && (toupper(buffer[lPos+1]) == 'R'))
                	d.modality = kMODALITY_CR;
                else if ((buffer[lPos]=='C') && (toupper(buffer[lPos+1]) == 'T'))
                	d.modality = kMODALITY_CT;
                if ((buffer[lPos]=='M') && (toupper(buffer[lPos+1]) == 'R'))
                	d.modality = kMODALITY_MR;
                if ((buffer[lPos]=='P') && (toupper(buffer[lPos+1]) == 'T'))
                	d.modality = kMODALITY_PT;
                if ((buffer[lPos]=='U') && (toupper(buffer[lPos+1]) == 'S'))
                	d.modality = kMODALITY_US;
                break;
            case kManufacturer:
                d.manufacturer = dcmStrManufacturer (lLength, &buffer[lPos]);
                volDiffusion.manufacturer = d.manufacturer;
                break;
            case kInstitutionName:
            	dcmStr(lLength, &buffer[lPos], d.institutionName);
            	break;
            case kInstitutionAddress: //VR is "ST": 1024 chars maximum
            	dcmStr(lLength, &buffer[lPos], d.institutionAddress, true);
            	break;
            case kReferringPhysicianName:
            	dcmStr(lLength, &buffer[lPos], d.referringPhysicianName);
            	break;
            case kComplexImageComponent:
                if (is2005140FSQ) break; //see Maastricht DICOM data for magnitude data with this field set as REAL!  https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging
                if (lLength < 2) break;
                //issue 256: Philips files report real ComplexImageComponent but Magnitude ImageType https://github.com/rordenlab/dcm2niix/issues/256
                isPhase = false;
                isReal = false;
                isImaginary = false;
                isMagnitude = false;
                //see Table C.8-85 http://dicom.nema.org/medical/Dicom/2017c/output/chtml/part03/sect_C.8.13.3.html
                if ((buffer[lPos]=='R') && (toupper(buffer[lPos+1]) == 'E'))
                	isReal = true;
                if ((buffer[lPos]=='I') && (toupper(buffer[lPos+1]) == 'M'))
                	isImaginary = true;
                if ((buffer[lPos]=='P') && (toupper(buffer[lPos+1]) == 'H'))
                	isPhase = true;
                if ((buffer[lPos]=='M') && (toupper(buffer[lPos+1]) == 'A'))
                	isMagnitude = true;
                //not mutually exclusive: possible for Philips enhanced DICOM to store BOTH magnitude and phase in the same image
                if (isPhase) d.isHasPhase = true;
                if (isReal) d.isHasReal = true;
                if (isImaginary) d.isHasImaginary = true;
                if (isMagnitude) d.isHasMagnitude = true;
                break;
            case kAcquisitionContrast:
                char acqContrast[kDICOMStr];
                dcmStr(lLength, &buffer[lPos], acqContrast);
                if (((int) strlen(acqContrast) > 8) && (strstr(acqContrast, "DIFFUSION") != NULL))
                    d.isDiffusion = true;
                break;

            case kAcquisitionTime :
                char acquisitionTimeTxt[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], acquisitionTimeTxt);
                d.acquisitionTime = atof(acquisitionTimeTxt);
                if (d.manufacturer != kMANUFACTURER_UIH) break;
                //UIH slice timing- do not use for Siemens as Siemens de-identification can corrupt this field https://github.com/rordenlab/dcm2niix/issues/236
                d.CSA.sliceTiming[acquisitionTimesGE_UIH] = d.acquisitionTime;
                acquisitionTimesGE_UIH ++;
                break;
            //case kContentTime :
            //    char contentTimeTxt[kDICOMStr];
            //    dcmStr (lLength, &buffer[lPos], contentTimeTxt);
            //    contentTime = atof(contentTimeTxt);
            //    break;
            case kStudyTime :
                dcmStr (lLength, &buffer[lPos], d.studyTime);
                break;
            case kPatientName :
                dcmStr (lLength, &buffer[lPos], d.patientName);
                break;
            case kAnatomicalOrientationType: {
            	char aotTxt[kDICOMStr]; //ftp://dicom.nema.org/MEDICAL/dicom/2015b/output/chtml/part03/sect_C.7.6.2.html#sect_C.7.6.2.1.1
                dcmStr (lLength, &buffer[lPos], aotTxt);
                int slen = (int) strlen(aotTxt);
				if((slen < 9) || (strstr(aotTxt, "QUADRUPED") == NULL) ) break;
                printError("Anatomical Orientation Type (0010,2210) is QUADRUPED: rotate coordinates accordingly\n");
            	break; }
            case kPatientID :
                dcmStr (lLength, &buffer[lPos], d.patientID);
                break;
            case kAccessionNumber :
                dcmStr (lLength, &buffer[lPos], d.accessionNumber);
                break;
            case kPatientBirthDate :
              	dcmStr (lLength, &buffer[lPos], d.patientBirthDate);
              	break;
            case kPatientSex :
            	d.patientSex = toupper(buffer[lPos]); //first character is either 'R'ow or 'C'ol
                break;
            case kPatientAge :
                dcmStr (lLength, &buffer[lPos], d.patientAge);
                break;
        	case kPatientWeight :
                d.patientWeight = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kStationName :
                dcmStr (lLength, &buffer[lPos], d.stationName);
                break;
            case kSeriesDescription: {
                dcmStr (lLength, &buffer[lPos], d.seriesDescription);
                break; }
            case kInstitutionalDepartmentName:
            	dcmStr (lLength, &buffer[lPos], d.institutionalDepartmentName);
            	break;
            case kManufacturersModelName :
            	dcmStr (lLength, &buffer[lPos], d.manufacturersModelName);
            	break;
            case kDerivationDescription : {
                //strcmp(transferSyntax, "1.2.840.10008.1.2")
                char derivationDescription[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], derivationDescription);//strcasecmp, strcmp
                if (strcasecmp(derivationDescription, "MEDCOM_RESAMPLED") == 0) d.isResampled = true;
                break;
            }
            case kDeviceSerialNumber : {
            	dcmStr (lLength, &buffer[lPos], d.deviceSerialNumber);
            	break;
            }
            case kSoftwareVersions : {
            	dcmStr (lLength, &buffer[lPos], d.softwareVersions);
            	int slen = (int) strlen(d.softwareVersions);
				if((slen > 4) && (strstr(d.softwareVersions, "XA11") != NULL) )  d.isXA10A = true;
				if((slen < 5) || (strstr(d.softwareVersions, "XA10") == NULL) ) break;
                d.isXA10A = true;
            	break;
            }
            case kProtocolName : {
                //if ((strlen(d.protocolName) < 1) || (d.manufacturer != kMANUFACTURER_GE)) //GE uses a generic session name here: do not overwrite kProtocolNameGE
                dcmStr (lLength, &buffer[lPos], d.protocolName); //see also kSequenceName
                break; }
            case kPatientOrient :
                dcmStr (lLength, &buffer[lPos], d.patientOrient);
                break;
            case kAcquisitionDuration:
            	//n.b. used differently by different vendors https://github.com/rordenlab/dcm2niix/issues/225
            	d.acquisitionDuration = dcmFloatDouble(lLength, &buffer[lPos],d.isLittleEndian);
                break;
            //in theory, 0018,9074 could provide XA10 slice time information, but scrambled by XA10 de-identification: better to use 0021,1104
            //case kFrameAcquisitionDateTime: {
            // //(0018,9074) DT [20190621095516.140000] YYYYMMDDHHMMSS
            // //see https://github.com/rordenlab/dcm2niix/issues/303
            //	char dateTime[kDICOMStr];
            //	dcmStr (lLength, &buffer[lPos], dateTime);
            //	printf("%s\tkFrameAcquisitionDateTime\n", dateTime);
            //}
            case kDiffusionDirectionality : {// 0018, 9075
                set_directionality0018_9075(&volDiffusion, (&buffer[lPos]));
                if ((d.manufacturer != kMANUFACTURER_PHILIPS) || (lLength < 10)) break;
                char dir[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], dir);
                if (strcmp(dir, "ISOTROPIC") == 0)
                	isPhilipsDerived = true;
                break; }
            case kMREchoSequence :
            	if (d.manufacturer != kMANUFACTURER_BRUKER) break;
            	if (sqDepth == 0) sqDepth = 1; //should not happen, in case faulty anonymization
            	sqDepth00189114 = sqDepth - 1;
            	break;
            case kMRAcquisitionPhaseEncodingStepsInPlane :
            		d.phaseEncodingLines =  dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
            	break;
            case kNumberOfImagesInMosaic :
            	if (d.manufacturer == kMANUFACTURER_SIEMENS)
            		numberOfImagesInMosaic =  dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
            	break;
            case kDwellTime :
            	d.dwellTime  =  dcmStrInt(lLength, &buffer[lPos]);
            	break;
            case kDiffusionBValueSiemens :
            	if (d.manufacturer != kMANUFACTURER_SIEMENS) break;
            	d.CSA.dtiV[0] =  dcmStrInt(lLength, &buffer[lPos]);
            	d.CSA.numDti = 1;
            	break;
            case kSliceTimeSiemens : {//Array of FD (64-bit double)
            	if (d.manufacturer != kMANUFACTURER_SIEMENS) break;
            	if ((lLength < 8) || ((lLength % 8) != 0)) break;
            	int nSlicesTimes = lLength / 8;
            	if (nSlicesTimes > kMaxEPI3D) break;
            	d.CSA.mosaicSlices = nSlicesTimes;
            	//printf(">>>> %d\n", nSlicesTimes);
            	//issue 296: for images de-identified to remove readCSAImageHeader
            	for (int z = 0; z < nSlicesTimes; z++)
        			d.CSA.sliceTiming[z] = dcmFloatDouble(8, &buffer[lPos+(z*8)],d.isLittleEndian);
				//for (int z = 0; z < nSlicesTimes; z++)
        		//	printf("%d>>>%g\n", z+1, d.CSA.sliceTiming[z]);
				checkSliceTimes(&d.CSA, nSlicesTimes, isVerbose, d.is3DAcq);
            	//d.CSA.dtiV[0] =  dcmStrInt(lLength, &buffer[lPos]);
            	//d.CSA.numDti = 1;
            	break; }

            case kDiffusionGradientDirectionSiemens : {
            	if (d.manufacturer != kMANUFACTURER_SIEMENS) break;
				float v[4];
            	dcmMultiFloatDouble(lLength, &buffer[lPos], 3, v, d.isLittleEndian);
				//dcmMultiFloat(lLength, (char*)&buffer[lPos], 3, v);
                //printf(">>>%g %g %g\n", v[0], v[1], v[2]);
                d.CSA.dtiV[1] = v[0];
                d.CSA.dtiV[2] = v[1];
                d.CSA.dtiV[3] = v[2];
            	break; }
            case kNumberOfDiffusionDirectionGE : {
				if (d.manufacturer != kMANUFACTURER_GE) break;
            	float f = dcmStrFloat(lLength, &buffer[lPos]);
            	d.numberOfDiffusionDirectionGE = round(f);
            	break; }
            case kLastScanLoc :
                d.lastScanLoc = dcmStrFloat(lLength, &buffer[lPos]);
                break;
                /*case kDiffusionBFactorSiemens :
                 if (d.manufacturer == kMANUFACTURER_SIEMENS)
                 printMessage("last scan location %f\n,",dcmStrFloat(lLength, &buffer[lPos]));

                 break;*/
            case kDiffusionDirectionGEX :
                if (d.manufacturer == kMANUFACTURER_GE)
                  set_diffusion_directionGE(&volDiffusion, lLength, (&buffer[lPos]), 0);
                break;
            case kDiffusionDirectionGEY :
                if (d.manufacturer == kMANUFACTURER_GE)
                  set_diffusion_directionGE(&volDiffusion, lLength, (&buffer[lPos]), 1);
                break;
            case kDiffusionDirectionGEZ :
                if (d.manufacturer == kMANUFACTURER_GE)
                  set_diffusion_directionGE(&volDiffusion, lLength, (&buffer[lPos]), 2);
                break;
            case kBandwidthPerPixelPhaseEncode:
                d.bandwidthPerPixelPhaseEncode = dcmFloatDouble(lLength, &buffer[lPos],d.isLittleEndian);
                break;
            //GE bug: multiple echos can create identical instance numbers
            //  in theory, one could detect as kRawDataRunNumberGE varies
            //  sliceN of echoE will have the same value for all timepoints
            //  this value does not appear indexed
            //  different echoes record same echo time.
            //  use multiEchoSortGEDICOM.py to salvage
            //case kRawDataRunNumberGE :
            //	if (d.manufacturer != kMANUFACTURER_GE)
            //		break;
            //    d.rawDataRunNumberGE = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
            //    break;
            case kStudyInstanceUID : // 0020, 000D
                dcmStr (lLength, &buffer[lPos], d.studyInstanceUID);
                break;
            case kSeriesInstanceUID : // 0020, 000E
            	dcmStr (lLength, &buffer[lPos], d.seriesInstanceUID);
            	//printMessage(">>%s\n", d.seriesInstanceUID);
            	d.seriesUidCrc = mz_crc32X((unsigned char*) &d.seriesInstanceUID, strlen(d.seriesInstanceUID));
                break;
            case kImagePositionPatient : {
                if (is2005140FSQ) {
					dcmMultiFloat(lLength, (char*)&buffer[lPos], 3, &patientPositionPrivate[0]);
					break;
				}
				patientPositionNum++;
				isAtFirstPatientPosition = true;
				
				
				//char dx[kDICOMStr];
                //dcmStr (lLength, &buffer[lPos], dx);
				//printMessage("*%s*", dx);
				
				dcmMultiFloat(lLength, (char*)&buffer[lPos], 3, &patientPosition[0]); //slice position
				if (isnan(d.patientPosition[1])) {
					//dcmMultiFloat(lLength, (char*)&buffer[lPos], 3, &d.patientPosition[0]); //slice position
					for (int k = 0; k < 4; k++)
						d.patientPosition[k] = patientPosition[k];
				} else {
					//dcmMultiFloat(lLength, (char*)&buffer[lPos], 3, &d.patientPositionLast[0]); //slice direction for 4D
					for (int k = 0; k < 4; k++)
						d.patientPositionLast[k] = patientPosition[k];
					if ((isFloatDiff(d.patientPositionLast[1],d.patientPosition[1]))  ||
						(isFloatDiff(d.patientPositionLast[2],d.patientPosition[2]))  ||
						(isFloatDiff(d.patientPositionLast[3],d.patientPosition[3])) ) {
						isAtFirstPatientPosition = false; //this slice is not at position of 1st slice
						//if (d.patientPositionSequentialRepeats == 0) //this is the first slice with different position
						//	d.patientPositionSequentialRepeats = patientPositionNum-1;
					} //if different position from 1st slice in file
				} //if not first slice in file
				set_isAtFirstPatientPosition_tvd(&volDiffusion, isAtFirstPatientPosition);
				//if (isAtFirstPatientPosition) numFirstPatientPosition++;
				if (isVerbose > 0) //verbose > 1 will report full DICOM tag
					printMessage("   Patient Position 0020,0032 (#,@,X,Y,Z)\t%d\t%ld\t%g\t%g\t%g\n", patientPositionNum, lPos, patientPosition[1], patientPosition[2], patientPosition[3]);
				break; }
            case kInPlanePhaseEncodingDirection:
                d.phaseEncodingRC = toupper(buffer[lPos]); //first character is either 'R'ow or 'C'ol
                break;
            case kSAR:
            	d.SAR = dcmStrFloat(lLength, &buffer[lPos]);
            	break;
            case kStudyID:
            	dcmStr (lLength, &buffer[lPos], d.studyID);
            	break;
            case kSeriesNum:
                d.seriesNum =  dcmStrInt(lLength, &buffer[lPos]);
                break;
            case kAcquNum:
                d.acquNum = dcmStrInt(lLength, &buffer[lPos]);
                break;
            case kImageNum:
            	//Enhanced Philips also uses this in once per file SQ 0008,1111
            	//Enhanced Philips also uses this once per slice in SQ 2005,140f
                if (d.imageNum < 1) d.imageNum = dcmStrInt(lLength, &buffer[lPos]);  //Philips renames each image again in 2001,9000, which can lead to duplicates
				break;
			case kInStackPositionNumber:
				if ((d.manufacturer != kMANUFACTURER_HITACHI) && (d.manufacturer != kMANUFACTURER_UNKNOWN) && (d.manufacturer != kMANUFACTURER_PHILIPS) && (d.manufacturer != kMANUFACTURER_BRUKER)) break;
				inStackPositionNumber = dcmInt(4,&buffer[lPos],d.isLittleEndian);
				//if (inStackPositionNumber == 1) numInStackPositionNumber1 ++;
				//printf("<%d>\n",inStackPositionNumber);
				if (inStackPositionNumber > maxInStackPositionNumber) maxInStackPositionNumber = inStackPositionNumber;
				break;
            case kDimensionIndexPointer:
                dimensionIndexPointer[dimensionIndexPointerCounter++] = dcmAttributeTag(&buffer[lPos],d.isLittleEndian);
                break;
			case kFrameContentSequence :
            	//if (!(d.manufacturer == kMANUFACTURER_BRUKER)) break; //see https://github.com/rordenlab/dcm2niix/issues/241
            	if (sqDepth == 0) sqDepth = 1; //should not happen, in case faulty anonymization
            	sqDepth00189114 = sqDepth - 1;
            	break;
			case kTriggerDelayTime: { //0x0020+uint32_t(0x9153<< 16 ) //FD
				if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
				//if (isVerbose < 2) break;
				double trigger = dcmFloatDouble(lLength, &buffer[lPos],d.isLittleEndian);
				d.triggerDelayTime = trigger;
				if (isSameFloatGE(d.triggerDelayTime, 0.0)) d.triggerDelayTime = 0.0; //double to single
				break; }
            case kDimensionIndexValues: { // kImageNum is not enough for 4D series from Philips 5.*.
                if (lLength < 4) break;
                nDimIndxVal = lLength / 4;
                if(nDimIndxVal > MAX_NUMBER_OF_DIMENSIONS){
                    printError("%d is too many dimensions.  Only up to %d are supported\n", nDimIndxVal,
                               MAX_NUMBER_OF_DIMENSIONS);
                    nDimIndxVal = MAX_NUMBER_OF_DIMENSIONS;  // Truncate
                }
                dcmMultiLongs(4 * nDimIndxVal, &buffer[lPos], nDimIndxVal, d.dimensionIndexValues, d.isLittleEndian);
              	break; }
            case kPhotometricInterpretation: {
 				char interp[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], interp);
                if (strcmp(interp, "PALETTE_COLOR") == 0)
                	isPaletteColor = true;
                	//printError("Photometric Interpretation 'PALETTE COLOR' not supported\n");
            	break; }
            case kPlanarRGB:
                d.isPlanarRGB = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kDim3:
                d.xyzDim[3] = dcmStrInt(lLength, &buffer[lPos]);
                numberOfFrames = d.xyzDim[3];
                break;
            case kSamplesPerPixel:
                d.samplesPerPixel = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kDim2:
                d.xyzDim[2] = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kDim1:
                d.xyzDim[1] = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            //order is Row,Column e.g. YX
            case kXYSpacing:{
            	float yx[3];
            	dcmMultiFloat(lLength, (char*)&buffer[lPos], 2, yx);
                d.xyzMM[1] = yx[2];
            	d.xyzMM[2] = yx[1];
            	break; }
            //case kXYSpacing:
            //    dcmMultiFloat(lLength, (char*)&buffer[lPos], 2, d.xyzMM);
            //    break;
            case kImageComments:
                dcmStr (lLength, &buffer[lPos], d.imageComments, true);
                break;
            //group 21: siemens
            //g21
			case kPATModeText : { //e.g. Siemens iPAT x2 listed as "p2"
            	char accelStr[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], accelStr);
                char *ptr;
                dcmStrDigitsOnlyKey('p', accelStr); //e.g. if "p2s4" return "2", if "s4" return ""
                d.accelFactPE = (float)strtof(accelStr, &ptr);
                if (*ptr != '\0')
                	d.accelFactPE = 0.0;
                //between slice accel
                dcmStr (lLength, &buffer[lPos], accelStr);
                dcmStrDigitsOnlyKey('s', accelStr); //e.g. if "p2s4" return "4", if "p2" return ""
                multiBandFactor = (int)strtol(accelStr, &ptr, 10);
                if (*ptr != '\0')
                	multiBandFactor = 0.0;
                //printMessage("p%gs%d\n",  d.accelFactPE, multiBandFactor);
				break; }
			case kTimeAfterStart:
				//0021,1104 see  https://github.com/rordenlab/dcm2niix/issues/303
				// 0021,1104 6@159630 DS  4.635
				// 0021,1104 2@161164 DS  0
				if (d.manufacturer != kMANUFACTURER_SIEMENS) break;
				if (acquisitionTimesGE_UIH >= kMaxEPI3D) break;
				d.CSA.sliceTiming[acquisitionTimesGE_UIH] = dcmStrFloat(lLength, &buffer[lPos]);
				d.CSA.sliceTiming[acquisitionTimesGE_UIH] *= 1000.0; //convert sec to msec
                //printf("x\t%d\t%g\tkTimeAfterStart\n", acquisitionTimesGE_UIH, d.CSA.sliceTiming[acquisitionTimesGE_UIH]);
				acquisitionTimesGE_UIH ++;
            	break;
            case kPhaseEncodingDirectionPositive: {
            	if (d.manufacturer != kMANUFACTURER_SIEMENS) break;
            	int ph = dcmStrInt(lLength, &buffer[lPos]);
            	if (ph == 0) d.phaseEncodingGE = kGE_PHASE_ENCODING_POLARITY_FLIPPED;
            	if (ph == 1) d.phaseEncodingGE = kGE_PHASE_ENCODING_POLARITY_UNFLIPPED;
            	break; }
			//case kRealDwellTime : //https://github.com/rordenlab/dcm2niix/issues/240
            //	if (d.manufacturer != kMANUFACTURER_SIEMENS) break;
            //	d.dwellTime  =  dcmStrInt(lLength, &buffer[lPos]);
            //	break;
            case kBandwidthPerPixelPhaseEncode21:
            	if (d.manufacturer != kMANUFACTURER_SIEMENS) break;
                d.bandwidthPerPixelPhaseEncode = dcmFloatDouble(lLength, &buffer[lPos],d.isLittleEndian);
                break;
			case kCoilElements:
				if (d.manufacturer != kMANUFACTURER_SIEMENS) break;
            	dcmStr (lLength, &buffer[lPos], d.coilElements);
				break;
            //group 21: GE
            case kLocationsInAcquisitionGE:
                locationsInAcquisitionGE = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kRTIA_timer:
            	if (d.manufacturer != kMANUFACTURER_GE) break;
            	//see dicm2nii slice timing from 0021,105E DS RTIA_timer
                d.rtia_timerGE =  dcmStrFloat(lLength, &buffer[lPos]); //RefAcqTimes = t/10; end % in ms
                //printf("%s\t%g\n", fname, d.rtia_timerGE);
                break;
            case kProtocolDataBlockGE :
            	if (d.manufacturer != kMANUFACTURER_GE) break;
            	d.protocolBlockLengthGE = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
            	d.protocolBlockStartGE = (int)lPos+(int)lFileOffset+4;
            	//printError("ProtocolDataBlockGE %d  @ %d\n", d.protocolBlockLengthGE, d.protocolBlockStartGE);
            	break;
            case kDoseCalibrationFactor :
                d.doseCalibrationFactor = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kPETImageIndex :
            	PETImageIndex = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
            	break;
            case kPEDirectionDisplayedUIH :
            	if (d.manufacturer != kMANUFACTURER_UIH) break;
            	dcmStr (lLength, &buffer[lPos], d.phaseEncodingDirectionDisplayedUIH);
            	break;
            case kDiffusion_bValueUIH : {
            	if (d.manufacturer != kMANUFACTURER_UIH) break;
            	float v[4];
            	dcmMultiFloatDouble(lLength, &buffer[lPos], 1, v, d.isLittleEndian);
            	d.CSA.dtiV[0] = v[0];
            	d.CSA.numDti = 1;
            	//printf("%d>>>%g\n", lPos, v[0]);
            	break; }
            case kParallelInformationUIH: {//SENSE factor (0065,100d) SH [F:2S]
            	if (d.manufacturer != kMANUFACTURER_UIH) break;
            	char accelStr[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], accelStr);
                //char *ptr;
                dcmStrDigitsDotOnlyKey(':', accelStr); //e.g. if "p2s4" return "2", if "s4" return ""
				d.accelFactPE = atof(accelStr);
                break; }
            case kNumberOfImagesInGridUIH :
            	if (d.manufacturer != kMANUFACTURER_UIH) break;
            	d.numberOfImagesInGridUIH =  dcmStrFloat(lLength, &buffer[lPos]);
            	d.CSA.mosaicSlices = d.numberOfImagesInGridUIH;
            	break;
            case kDiffusionGradientDirectionUIH : { //0065,1037
            //0.03712929804225321\-0.5522387869760447\-0.8328587749392602
            	if (d.manufacturer != kMANUFACTURER_UIH) break;
            	float v[4];
            	dcmMultiFloatDouble(lLength, &buffer[lPos], 3, v, d.isLittleEndian);
				//dcmMultiFloat(lLength, (char*)&buffer[lPos], 3, v);
                //printf(">>>%g %g %g\n", v[0], v[1], v[2]);
                d.CSA.dtiV[1] = v[0];
                d.CSA.dtiV[2] = v[1];
                d.CSA.dtiV[3] = v[2];
                	//vRLPhilips = v[0];
					//vAPPhilips = v[1];
					//vFHPhilips = v[2];
            	break; }

            case kBitsAllocated :
                d.bitsAllocated = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kBitsStored :
                d.bitsStored = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kIsSigned : //http://dicomiseasy.blogspot.com/2012/08/chapter-12-pixel-data.html
                d.isSigned = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kPixelPaddingValue :
                // According to the DICOM standard, this can be either unsigned (US) or signed (SS). Currently this
                // is used only in nii_saveNII3Dtilt() which only allows DT_INT16, so treat it as signed.
                d.pixelPaddingValue = (float) (short) dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kFloatPixelPaddingValue :
                d.pixelPaddingValue = dcmFloat(lLength, &buffer[lPos], d.isLittleEndian);
                break;
            case kTR :
                d.TR = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kTE :
            	TE = dcmStrFloat(lLength, &buffer[lPos]);
            	if (d.TE <= 0.0)
            		d.TE = TE;
            	break;
            case kNumberOfAverages :
            	d.numberOfAverages = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kImagingFrequency :
            	d.imagingFrequency = dcmStrFloat(lLength, &buffer[lPos]);
                break;
           	case kTriggerTime:
				//untested method to detect slice timing for GE PSD “epi” with multiphase option
				// will not work for current PSD “epiRT” (BrainWave RT, fMRI/DTI package provided by Medical Numerics)
            	if (d.manufacturer != kMANUFACTURER_GE) break;            	
            	d.triggerDelayTime = dcmStrFloat(lLength, &buffer[lPos]); //???? issue 336
            	d.CSA.sliceTiming[acquisitionTimesGE_UIH] = d.triggerDelayTime;
                //printf("%g\n", d.CSA.sliceTiming[acquisitionTimesGE_UIH]);
				acquisitionTimesGE_UIH ++;
				break;
            case kEffectiveTE : {
            	TE = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
            	if (d.TE <= 0.0)
            		d.TE = TE;
            	break; }
            case kTI :
                d.TI = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kEchoNum :
                d.echoNum =  dcmStrInt(lLength, &buffer[lPos]);
                break;
            case kMagneticFieldStrength :
                d.fieldStrength =  dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kZSpacing :
                d.zSpacing = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kPhaseEncodingSteps :
                d.phaseEncodingSteps =  dcmStrInt(lLength, &buffer[lPos]);
                break;
            case kEchoTrainLength :
            	d.echoTrainLength  =  dcmStrInt(lLength, &buffer[lPos]);
            	break;
            case kPhaseFieldofView :
            	d.phaseFieldofView = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kPixelBandwidth :
				/*if (d.manufacturer == kMANUFACTURER_PHILIPS) {
					//Private SQs can report different (more precise?) pixel bandwidth values than in the main header!
					//  https://github.com/rordenlab/dcm2niix/issues/170
                	if (is2005140FSQ) break;
                	if ((lFileOffset + lPos) < sqEndPrivate) break; //inside private SQ, SQ has defined length
                	if (sqDepthPrivate > 0) break; //inside private SQ, SQ has undefined length
                }*/
            	d.pixelBandwidth = dcmStrFloat(lLength, &buffer[lPos]);
            	//printWarning(" PixelBandwidth (0018,0095)====> %g @%d\n", d.pixelBandwidth, lPos);
            	break;
        	case kAcquisitionMatrix :
				if (lLength == 8) {
                	uint16_t acquisitionMatrix[4];
                	dcmMultiShorts(lLength, &buffer[lPos], 4, &acquisitionMatrix[0],d.isLittleEndian); //slice position
            		//phaseEncodingLines stored in either image columns or rows
            		if (acquisitionMatrix[3] > 0)
            			d.phaseEncodingLines = acquisitionMatrix[3];
            		if (acquisitionMatrix[2] > 0)
            			d.phaseEncodingLines = acquisitionMatrix[2];
            		if (acquisitionMatrix[1] > 0)
            			frequencyRows = acquisitionMatrix[1];
            		if (acquisitionMatrix[0] > 0)
            			frequencyRows = acquisitionMatrix[0];
            	}
            	break;
            case kFlipAngle :
            	d.flipAngle = dcmStrFloat(lLength, &buffer[lPos]);
            	break;
            case kRadionuclideTotalDose :
                d.radionuclideTotalDose = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kRadionuclideHalfLife :
                d.radionuclideHalfLife = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kRadionuclidePositronFraction :
                d.radionuclidePositronFraction = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kGantryTilt :
                d.gantryTilt = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kXRayExposure : //CTs do not have echo times, we use this field to detect different exposures: https://github.com/neurolabusc/dcm2niix/pull/48
            	if (d.TE == 0) {// for CT we will use exposure (0018,1152) whereas for MR we use echo time (0018,0081)
                	d.isXRay = true;
            		d.TE = dcmStrFloat(lLength, &buffer[lPos]);
                }
            	break;
            case kReceiveCoilName :
                dcmStr (lLength, &buffer[lPos], d.coilName);
                if (strlen(d.coilName) < 1) break;
                d.coilCrc = mz_crc32X((unsigned char*) &d.coilName, strlen(d.coilName));
				break;
            case kSlope :
                d.intenScale = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            //case kSpectroscopyDataPointColumns :
           	//	d.xyzDim[4] =  dcmInt(4,&buffer[lPos],d.isLittleEndian);
			//	break;
            case kPhilipsSlope :
                if ((lLength == 4) && (d.manufacturer == kMANUFACTURER_PHILIPS))
                    d.intenScalePhilips = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
                break;
            case kIntercept :
                d.intenIntercept = dcmStrFloat(lLength, &buffer[lPos]);
                break;
            case kZThick :
                d.xyzMM[3] = dcmStrFloat(lLength, &buffer[lPos]);
                d.zThick = d.xyzMM[3];
                break;
            case kAcquisitionMatrixText21:
				//fall through to kAcquisitionMatrixText
            case kAcquisitionMatrixText : {
               if (d.manufacturer == kMANUFACTURER_SIEMENS) {
					char matStr[kDICOMStr];
					dcmStr (lLength, &buffer[lPos], matStr);
					char* pPosition = strchr(matStr, 'I');
					if (pPosition != NULL)
						isInterpolated = true;
            	}
               break; }
            case kCoilSiemens : {
                if (d.manufacturer == kMANUFACTURER_SIEMENS) {
                    //see if image from single coil "H12" or an array "HEA;HEP"
                    //char coilStr[kDICOMStr];
                    //int coilNum;
                    dcmStr (lLength, &buffer[lPos], d.coilName);
                    if (strlen(d.coilName) < 1) break;
                    //printf("-->%s\n", coilStr);
                    //d.coilName = coilStr;
                    //if (coilStr[0] == 'C') break; //kludge as Nova 32-channel defaults to "C:A32" https://github.com/rordenlab/dcm2niix/issues/187
                    //char *ptr;
                    //dcmStrDigitsOnly(coilStr);
                    //coilNum = (int)strtol(coilStr, &ptr, 10);
                    d.coilCrc = mz_crc32X((unsigned char*) &d.coilName, strlen(d.coilName));

                    //printf("%d:%s\n", d.coilNum, coilStr);
                    //if (*ptr != '\0')
                    //    d.coilNum = 0;
                }
                break; }
            case kImaPATModeText : { //e.g. Siemens iPAT x2 listed as "p2"
            	char accelStr[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], accelStr);
                char *ptr;
                dcmStrDigitsOnlyKey('p', accelStr); //e.g. if "p2s4" return "2", if "s4" return ""
                d.accelFactPE = (float)strtof(accelStr, &ptr);
                if (*ptr != '\0')
                	d.accelFactPE = 0.0;
                //between slice accel
                dcmStr (lLength, &buffer[lPos], accelStr);
                dcmStrDigitsOnlyKey('s', accelStr); //e.g. if "p2s4" return "4", if "p2" return ""
                multiBandFactor = (int)strtol(accelStr, &ptr, 10);
                if (*ptr != '\0')
                	multiBandFactor = 0.0;
                break; }
            case kLocationsInAcquisition :
                d.locationsInAcquisition = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kIconImageSequence:
                isIconImageSequence = true;
                break;
            /*case kStackSliceNumber: { //https://github.com/Kevin-Mattheus-Moerman/GIBBON/blob/master/dicomDict/PMS-R32-dict.txt
            	int stackSliceNumber = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
            	printMessage("StackSliceNumber %d\n",stackSliceNumber);
            	break;
			}*/
			case kNumberOfDynamicScans:
                //~d.numberOfDynamicScans =  dcmStrInt(lLength, &buffer[lPos]);
                numberOfDynamicScans =  dcmStrInt(lLength, &buffer[lPos]);

                break;
            case	kMRAcquisitionType: //detect 3D acquisition: we can reorient these without worrying about slice time correct or BVEC/BVAL orientation
            	if (lLength > 1) d.is2DAcq = (buffer[lPos]=='2') && (toupper(buffer[lPos+1]) == 'D');
                if (lLength > 1) d.is3DAcq = (buffer[lPos]=='3') && (toupper(buffer[lPos+1]) == 'D');
                //dcmStr (lLength, &buffer[lPos], d.mrAcquisitionType);
                break;
            case kBodyPartExamined : {
                dcmStr (lLength, &buffer[lPos], d.bodyPartExamined);
                break;
            }
            case kScanningSequence : {
                dcmStr (lLength, &buffer[lPos], d.scanningSequence);
                break;
            }
            case kSequenceVariant21 :
            	if (d.manufacturer != kMANUFACTURER_SIEMENS) break; //see GE dataset in dcm_qa_nih
            	//fall through...
            case kSequenceVariant : {
                dcmStr (lLength, &buffer[lPos], d.sequenceVariant);
                break;
            }
            case kScanOptions:
            	dcmStr (lLength, &buffer[lPos], d.scanOptions);
            	break;
            case kSequenceName : {
                //if (strlen(d.protocolName) < 1) //precedence given to kProtocolName and kProtocolNameGE
                dcmStr (lLength, &buffer[lPos], d.sequenceName);
                break;
            }
            case	kMRAcquisitionTypePhilips: //kMRAcquisitionType
                if (lLength > 1) d.is3DAcq = (buffer[lPos]=='3') && (toupper(buffer[lPos+1]) == 'D');
                break;
            case	kAngulationRL:
                d.angulation[1] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
                break;
            case	kAngulationAP:
                d.angulation[2] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
                break;
            case	kAngulationFH:
                d.angulation[3] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
                break;
            case	kMRStackOffcentreRL:
                d.stackOffcentre[1] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
                break;
            case	kMRStackOffcentreAP:
                d.stackOffcentre[2] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
                break;
            case	kMRStackOffcentreFH:
                d.stackOffcentre[3] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
                break;
            case	kSliceOrient: {
                char orientStr[kDICOMStr];
                orientStr[0] = 'X'; //avoid compiler warning: orientStr filled by dcmStr
                dcmStr (lLength, &buffer[lPos], orientStr);
                if (toupper(orientStr[0])== 'S')
                    d.sliceOrient = kSliceOrientSag; //sagittal
                else if (toupper(orientStr[0])== 'C')
                    d.sliceOrient = kSliceOrientCor; //coronal
                else
                    d.sliceOrient = kSliceOrientTra; //transverse (axial)
                break; }
            case kElscintIcon :
            	printWarning("Assuming icon SQ 07a3,10ce.\n");
                isIconImageSequence = true;
            	break;
			case kPMSCT_RLE1 :
			    //https://groups.google.com/forum/#!topic/comp.protocols.dicom/8HuP_aNy9Pc
				//https://discourse.slicer.org/t/fail-to-load-pet-ct-gemini/8158/3
				// d.compressionScheme = kCompressPMSCT_RLE1; //force RLE 
				if (d.compressionScheme != kCompressPMSCT_RLE1) break;
				d.imageStart = (int)lPos + (int)lFileOffset;
				d.imageBytes = lLength;
				break;
			case kPrivateCreator : {
				if (d.manufacturer != kMANUFACTURER_UNKNOWN) break;
                d.manufacturer = dcmStrManufacturer (lLength, &buffer[lPos]);
                volDiffusion.manufacturer = d.manufacturer;
                //printf(">>>>%d\n", d.manufacturer);
                break; }
            case kDiffusionBFactor :
            	if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
            	B0Philips = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
            	set_bVal(&volDiffusion, B0Philips);
            	break;
            // case	kDiffusionBFactor: // 2001,1003
            //     if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (isAtFirstPatientPosition)) {
            //         d.CSA.numDti++; //increment with BFactor: on Philips slices with B=0 have B-factor but no diffusion directions
            //         if (d.CSA.numDti == 2) { //First time we know that this is a 4D DTI dataset
            //             //d.dti4D = (TDTI *)malloc(kMaxDTI4D * sizeof(TDTI));
            //             dti4D->S[0].V[0] = d.CSA.dtiV[0];
            //             dti4D->S[0].V[1] = d.CSA.dtiV[1];
            //             dti4D->S[0].V[2] = d.CSA.dtiV[2];
            //             dti4D->S[0].V[3] = d.CSA.dtiV[3];
            //         }
            //         d.CSA.dtiV[0] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
            //         if ((d.CSA.numDti > 1) && (d.CSA.numDti < kMaxDTI4D))
            //             dti4D->S[d.CSA.numDti-1].V[0] = d.CSA.dtiV[0];
            //         /*if ((d.CSA.numDti > 0) && (d.CSA.numDti <= kMaxDTIv))
            //            d.CSA.dtiV[d.CSA.numDti-1][0] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);*/
            //     }
            //     break;

            /*
            case kDiffusionDirectionPhilips: {//
				//note not useful: does not report precise direction, both B=0 and Isotropic scans labelled "I" so does not tell us if image is oblique
				//http://incenter.medical.philips.com/doclib/enc/fetch/2000/4504/577242/577256/588723/5144873/5144488/5144982/DICOM_Conformance_Statement_Ingenia_R4.1.pdf%3fnodeid%3d8124182%26vernum%3d-2
				//CS: Possible values: P (PreparationDirection), M (MeasurementDirection),S (Selection Direction),O(Oblique Direction),I (Isotropic),Only applicable for diffusion scans.
				if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
            	char diffDir[kDICOMStr];
                dcmStr (lLength, &buffer[lPos], diffDir);
                printf(">>%s  %s\n", diffDir, fname);
				break;
            }
            */
            case kDiffusion_bValue:  // 0018,9087
            	if (d.manufacturer == kMANUFACTURER_UNKNOWN ) {
            		d.manufacturer = kMANUFACTURER_PHILIPS;
            		printWarning("Found 0018,9087 but manufacturer (0008,0070) unknown: assuming Philips.\n");
            	}

              // Note that this is ahead of kPatientPosition (0020,0032), so
              // isAtFirstPatientPosition is not necessarily set yet.
              // Philips uses this tag too, at least as of 5.1, but they also
              // use kDiffusionBFactor (see above), and we do not want to
              // double count.  More importantly, with Philips this tag
              // (sometimes?)  gets repeated in a nested sequence with the
              // value *unset*!
              // GE started using this tag in 27, and annoyingly, NOT including
              // the b value if it is 0 for the slice.

              //if((d.manufacturer != kMANUFACTURER_PHILIPS) || !is2005140FSQ){
                // d.CSA.numDti++;
                // if (d.CSA.numDti == 2) { //First time we know that this is a 4D DTI dataset
                //   //d.dti4D = (TDTI *)malloc(kMaxDTI4D * sizeof(TDTI));
                //   dti4D->S[0].V[0] = d.CSA.dtiV[0];
                //   dti4D->S[0].V[1] = d.CSA.dtiV[1];
                //   dti4D->S[0].V[2] = d.CSA.dtiV[2];
                //   dti4D->S[0].V[3] = d.CSA.dtiV[3];
                // }
                B0Philips = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
                //d.CSA.dtiV[0] = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
                set_bVal(&volDiffusion, dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian));
                // if ((d.CSA.numDti > 1) && (d.CSA.numDti < kMaxDTI4D))
                //   dti4D->S[d.CSA.numDti-1].V[0] = d.CSA.dtiV[0];
              //}
              break;
            case kDiffusionOrientation:  // 0018, 9089
              // Note that this is ahead of kPatientPosition (0020,0032), so
              // isAtFirstPatientPosition is not necessarily set yet.
              // Philips uses this tag too, at least as of 5.1, but they also
              // use kDiffusionDirectionRL, etc., and we do not want to double
              // count.  More importantly, with Philips this tag (sometimes?)
              // gets repeated in a nested sequence with the value *unset*!
              // if (((d.manufacturer == kMANUFACTURER_SIEMENS) ||
              //      ((d.manufacturer == kMANUFACTURER_PHILIPS) && !is2005140FSQ)) &&
              //     (isAtFirstPatientPosition || isnan(d.patientPosition[1])))

              //if((d.manufacturer == kMANUFACTURER_SIEMENS) || ((d.manufacturer == kMANUFACTURER_PHILIPS) && !is2005140FSQ))
              if((d.manufacturer == kMANUFACTURER_HITACHI) || (d.manufacturer == kMANUFACTURER_SIEMENS) || (d.manufacturer == kMANUFACTURER_PHILIPS)) {
                //for kMANUFACTURER_HITACHI see https://nciphub.org/groups/qindicom/wiki/StandardcompliantenhancedmultiframeDWI
                float v[4];
                //dcmMultiFloat(lLength, (char*)&buffer[lPos], 3, v);
                //dcmMultiFloatDouble(lLength, &buffer[lPos], 3, v, d.isLittleEndian);
                dcmMultiFloatDouble(lLength, &buffer[lPos], 3, v, d.isLittleEndian);
                	vRLPhilips = v[0];
					vAPPhilips = v[1];
					vFHPhilips = v[2];
				//printMessage("><>< 0018,9089:\t%g\t%g\t%g\n",  v[0], v[1], v[2]);
				//https://github.com/rordenlab/dcm2niix/issues/256
				//d.CSA.dtiV[1] = v[0];
				//d.CSA.dtiV[2] = v[1];
				//d.CSA.dtiV[3] = v[2];
				//printMessage("><>< 0018,9089: DWI bxyz %g %g %g %g\n", d.CSA.dtiV[0], d.CSA.dtiV[1], d.CSA.dtiV[2], d.CSA.dtiV[3]);
                hasDwiDirectionality = true;
                set_orientation0018_9089(&volDiffusion, lLength, &buffer[lPos], d.isLittleEndian);
              }
              break;
            // case kSharedFunctionalGroupsSequence:
            //   if ((d.manufacturer == kMANUFACTURER_SIEMENS) && isAtFirstPatientPosition) {
            //     break; // For now - need to figure out how to get the nested
            //            // part of buffer[lPos].
            //   }
            //   break;

			//case kSliceNumberMrPhilips :
			//	sliceNumberMrPhilips3D = dcmStrInt(lLength, &buffer[lPos]);
			//	break;
            case kImagingFrequency2 :
            	d.imagingFrequency = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
            	break;
            case kDiffusionBValueXX : {
            	if (!(d.manufacturer == kMANUFACTURER_BRUKER)) break; //other manufacturers provide bvec directly, rather than bmatrix
            	double bMat = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
            	set_bMatrix(&volDiffusion, bMat, 0);
            	break; }
            case kDiffusionBValueXY : {
            	if (!(d.manufacturer == kMANUFACTURER_BRUKER)) break; //other manufacturers provide bvec directly, rather than bmatrix
            	double bMat = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
            	set_bMatrix(&volDiffusion, bMat, 1);
            	break; }
            case kDiffusionBValueXZ : {
            	if (!(d.manufacturer == kMANUFACTURER_BRUKER)) break; //other manufacturers provide bvec directly, rather than bmatrix
            	double bMat = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
            	set_bMatrix(&volDiffusion, bMat, 2);
            	break; }
            case kDiffusionBValueYY : {
            	if (!(d.manufacturer == kMANUFACTURER_BRUKER)) break; //other manufacturers provide bvec directly, rather than bmatrix
            	double bMat = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
            	set_bMatrix(&volDiffusion, bMat, 3);
            	break; }
            case kDiffusionBValueYZ : {
            	if (!(d.manufacturer == kMANUFACTURER_BRUKER)) break; //other manufacturers provide bvec directly, rather than bmatrix
            	double bMat = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
            	set_bMatrix(&volDiffusion, bMat, 4);
            	break; }
            case kDiffusionBValueZZ : {
            	if (!(d.manufacturer == kMANUFACTURER_BRUKER)) break; //other manufacturers provide bvec directly, rather than bmatrix
            	double bMat = dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian);
            	set_bMatrix(&volDiffusion, bMat, 5);
            	d.isVectorFromBMatrix = true;
            	break; }
            case kSliceNumberMrPhilips : {
            	if (d.manufacturer != kMANUFACTURER_PHILIPS)
            		break;
				sliceNumberMrPhilips = dcmStrInt(lLength, &buffer[lPos]);
				int sliceNumber = sliceNumberMrPhilips;
            	//use public patientPosition if it exists - fall back to private patient position
            	if ((sliceNumber == 1) && (!isnan(patientPosition[1])) ) {
            		for (int k = 0; k < 4; k++)
						patientPositionStartPhilips[k] = patientPosition[k];
            	} else if ((sliceNumber == 1) && (!isnan(patientPositionPrivate[1]))) {
            		for (int k = 0; k < 4; k++)
						patientPositionStartPhilips[k] = patientPositionPrivate[k];
            	}
            	if ((sliceNumber == locationsInAcquisitionPhilips) && (!isnan(patientPosition[1])) ) {
            		for (int k = 0; k < 4; k++)
						patientPositionEndPhilips[k] = patientPosition[k];
            	} else if ((sliceNumber == locationsInAcquisitionPhilips) && (!isnan(patientPositionPrivate[1])) ) {
            		for (int k = 0; k < 4; k++)
						patientPositionEndPhilips[k] = patientPositionPrivate[k];
            	}
            	break; }
            case kNumberOfSlicesMrPhilips :
            	if (d.manufacturer != kMANUFACTURER_PHILIPS)
            		break;
                locationsInAcquisitionPhilips = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                //printMessage("====> locationsInAcquisitionPhilips\t%d\n", locationsInAcquisitionPhilips);
				break;
			case kDiffusionDirectionRL:
				if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
				vRLPhilips = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
				set_diffusion_directionPhilips(&volDiffusion, vRLPhilips, 0);
				break;
			case kDiffusionDirectionAP:
				if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
				vAPPhilips = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
				set_diffusion_directionPhilips(&volDiffusion, vAPPhilips, 1);
				break;
			case kDiffusionDirectionFH:
				if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
				vFHPhilips = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
				set_diffusion_directionPhilips(&volDiffusion, vFHPhilips, 2);
				break;
            // case    kDiffusionDirectionRL:
            //     if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (isAtFirstPatientPosition)) {
            //         d.CSA.dtiV[1] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
            //         if ((d.CSA.numDti > 1) && (d.CSA.numDti < kMaxDTI4D))
            //             dti4D->S[d.CSA.numDti-1].V[1] = d.CSA.dtiV[1];
            //     }
            //     /*if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (isAtFirstPatientPosition) && (d.CSA.numDti > 0) && (d.CSA.numDti <= kMaxDTIv))
            //         d.CSA.dtiV[d.CSA.numDti-1][1] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);*/
            //     break;
            // case kDiffusionDirectionAP:
            //     if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (isAtFirstPatientPosition)) {
            //         d.CSA.dtiV[2] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
            //         if ((d.CSA.numDti > 1) && (d.CSA.numDti < kMaxDTI4D))
            //             dti4D->S[d.CSA.numDti-1].V[2] = d.CSA.dtiV[2];
            //     }
            //     /*if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (isAtFirstPatientPosition) && (d.CSA.numDti > 0) && (d.CSA.numDti <= kMaxDTIv))
            //         d.CSA.dtiV[d.CSA.numDti-1][2] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);*/
            //     break;
            // case	kDiffusionDirectionFH:
            //     if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (isAtFirstPatientPosition)) {
            //         d.CSA.dtiV[3] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);
            //         if ((d.CSA.numDti > 1) && (d.CSA.numDti < kMaxDTI4D))
            //             dti4D->S[d.CSA.numDti-1].V[3] = d.CSA.dtiV[3];
            //         //printMessage("dti XYZ %g %g %g\n",d.CSA.dtiV[1],d.CSA.dtiV[2],d.CSA.dtiV[3]);
            //     }
            //     /*if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (isAtFirstPatientPosition) && (d.CSA.numDti > 0) && (d.CSA.numDti <= kMaxDTIv))
            //         d.CSA.dtiV[d.CSA.numDti-1][3] = dcmFloat(lLength, &buffer[lPos],d.isLittleEndian);*/
            //     //http://www.na-mic.org/Wiki/index.php/NAMIC_Wiki:DTI:DICOM_for_DWI_and_DTI
            //     break;
            //~~
            case kPrivatePerFrameSq :
            	if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
            	//if ((vr[0] == 'S') && (vr[1] == 'Q')) break;
            	//if (!is2005140FSQwarned)
            	//	printWarning("expected VR of 2005,140F to be 'SQ' (prior DICOM->DICOM conversion error?)\n");
            	is2005140FSQ = true;
            	//is2005140FSQwarned = true;
            //case kMRImageGradientOrientationNumber :
            //    if (d.manufacturer == kMANUFACTURER_PHILIPS)
            //       MRImageGradientOrientationNumber =  dcmStrInt(lLength, &buffer[lPos]);
                break;
            case kMRImageDiffBValueNumber:
            	if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
            	philMRImageDiffBValueNumber =  dcmStrInt(lLength, &buffer[lPos]);
                break;
            case kWaveformSq:
                d.imageStart = 1; //abort!!!
                printMessage("Skipping DICOM (audio not image) '%s'\n", fname);
                break;
            case kSpectroscopyData: //kSpectroscopyDataPointColumns
            	printMessage("Skipping Spectroscopy DICOM '%s'\n", fname);
                d.imageStart = (int)lPos + (int)lFileOffset;
                break;
            case kCSAImageHeaderInfo:
            	if ((lPos + lLength) > fileLen) break;
            	readCSAImageHeader(&buffer[lPos], lLength, &d.CSA, isVerbose, d.is3DAcq); //, dti4D);
                if (!d.isHasPhase)
                	d.isHasPhase = d.CSA.isPhaseMap;
                break;
                //case kObjectGraphics:
                //    printMessage("---->%d,",lLength);
                //    break;
            case kCSASeriesHeaderInfo:
            	if ((lPos + lLength) > fileLen) break;
            	d.CSA.SeriesHeader_offset = (int)lPos;
            	d.CSA.SeriesHeader_length = lLength;
            	break;
            case kRealWorldIntercept:
                if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
                d.RWVIntercept = dcmFloatDouble(lLength, &buffer[lPos],d.isLittleEndian);
                if (isSameFloat(0.0, d.intenIntercept)) //give precedence to standard value
                    d.intenIntercept = d.RWVIntercept;
                break;
            case kRealWorldSlope:
            	if (d.manufacturer != kMANUFACTURER_PHILIPS) break;
            	d.RWVScale = dcmFloatDouble(lLength, &buffer[lPos],d.isLittleEndian);
                //printMessage("RWVScale %g\n", d.RWVScale);
                if (isSameFloat(1.0, d.intenScale))  //give precedence to standard value
                    d.intenScale = d.RWVScale;
                break;
            case kUserDefineDataGE: { //0043,102A
            	if ((d.manufacturer != kMANUFACTURER_GE) || (lLength < 128)) break;
            	#define MY_DEBUG_GE // <- uncomment this to use following code to infer GE phase encoding direction
            	#ifdef MY_DEBUG_GE
            	int isVerboseX = isVerbose; //for debugging only - in standard release we will enable user defined "isVerbose"
            	//int isVerboseX = 2;
            	if (isVerboseX > 1) printMessage(" UserDefineDataGE file offset/length %ld %u\n", lFileOffset+lPos, lLength);
            	if (lLength < 916) { //minimum size is hdr_offset=0, read 0x0394
            		printMessage(" GE header too small to be valid  (A)\n");
            		break;
            	}
            	//debug code to export binary data
				/*
            	char str[kDICOMStr];
        		sprintf(str, "%s_ge.bin",fname);
            	FILE *pFile = fopen(str, "wb");
				fwrite(&buffer[lPos], 1, lLength, pFile);
            	fclose (pFile);
            	*/
            	if ((size_t)(lPos + lLength) > MaxBufferSz) {
            		//we could re-read the buffer in this case, however in practice GE headers are concise so we never see this issue
            		printMessage(" GE header overflows buffer\n");
            		break;
            	}
            	uint16_t hdr_offset = dcmInt(2,&buffer[lPos+24],true);
            	if (isVerboseX > 1) printMessage(" header offset: %d\n", hdr_offset);
            	if (lLength < (hdr_offset+916)) { //minimum size is hdr_offset=0, read 0x0394
            		printMessage(" GE header too small to be valid  (B)\n");
            		break;
            	}
            	//size_t hdr = lPos+hdr_offset;
            	float version = dcmFloat(4,&buffer[lPos + hdr_offset],true);
            	if (isVerboseX > 1) printMessage(" version %g\n", version);
            	if (version < 5.0 || version > 40.0) {
    				//printMessage(" GE header file format incorrect %g\n", version);
    				break;
    			}
    			//char const *hdr = &buffer[lPos + hdr_offset];
            	char *hdr = (char *)&buffer[lPos + hdr_offset];
            	int epi_chk_off = 0x003a;
    			int pepolar_off   = 0x0030;
    			int kydir_off   = 0x0394;
    			if (version >= 25.002) {
      				hdr       += 0x004c;
      				kydir_off -= 0x008c;
    			}
    			//int seqOrInter =dcmInt(2,(unsigned char*)(hdr + pepolar_off-638),true);
    			//int seqOrInter2 =dcmInt(2,(unsigned char*)(hdr + kydir_off-638),true);
     			//printf("%d %d<<<\n", seqOrInter,seqOrInter2);
    			//check if EPI
    			if (true) {
      				//int check = *(short const *)(hdr + epi_chk_off) & 0x800;
      				int check =dcmInt(2,(unsigned char*)hdr + epi_chk_off,true) & 0x800;
     				if (check == 0) {
						if (isVerboseX > 1) printMessage("%s: Warning: Data is not EPI\n", fname);
						break;
      				}
    			}
            	//Check for PE polarity
				// int flag1 = *(short const *)(hdr + pepolar_off) & 0x0004;
				//Check for ky direction (view order)
				// int flag2 = *(int const *)(hdr + kydir_off);
				int phasePolarityFlag = dcmInt(2,(unsigned char*)hdr + pepolar_off,true) & 0x0004;
				//Check for ky direction (view order)
				int sliceOrderFlag = dcmInt(2,(unsigned char*)hdr + kydir_off,true);
				if (isVerboseX > 1)
					printMessage(" GE phasePolarity/sliceOrder flags %d %d\n", phasePolarityFlag, sliceOrderFlag);
				if (phasePolarityFlag == kGE_PHASE_ENCODING_POLARITY_FLIPPED)
					d.phaseEncodingGE = kGE_PHASE_ENCODING_POLARITY_FLIPPED;
				if (phasePolarityFlag == kGE_PHASE_ENCODING_POLARITY_UNFLIPPED)
					d.phaseEncodingGE = kGE_PHASE_ENCODING_POLARITY_UNFLIPPED;
				if (sliceOrderFlag == kGE_SLICE_ORDER_BOTTOM_UP) {
					//https://cfmriweb.ucsd.edu/Howto/3T/operatingtips.html
					if (d.phaseEncodingGE == kGE_PHASE_ENCODING_POLARITY_UNFLIPPED)
						d.phaseEncodingGE = kGE_PHASE_ENCODING_POLARITY_FLIPPED;
					else
						d.phaseEncodingGE = kGE_PHASE_ENCODING_POLARITY_UNFLIPPED;
				}
				//if (sliceOrderFlag == kGE_SLICE_ORDER_TOP_DOWN)
				//	d.sliceOrderGE = kGE_SLICE_ORDER_TOP_DOWN;
				//if (sliceOrderFlag == kGE_SLICE_ORDER_BOTTOM_UP)
				//	d.sliceOrderGE = kGE_SLICE_ORDER_BOTTOM_UP;
				#endif
				break;
            }
            case kEffectiveEchoSpacingGE:
                if (d.manufacturer == kMANUFACTURER_GE) d.effectiveEchoSpacingGE = dcmInt(lLength,&buffer[lPos],d.isLittleEndian);
                break;
            case kDiffusionBFactorGE :
                if (d.manufacturer == kMANUFACTURER_GE) {
                  d.CSA.dtiV[0] = (float)set_bValGE(&volDiffusion, lLength, &buffer[lPos]);
                  d.CSA.numDti = 1;
                }
                break;
            case kGeiisFlag:
                if ((lLength > 4) && (buffer[lPos]=='G') && (buffer[lPos+1]=='E') && (buffer[lPos+2]=='I')  && (buffer[lPos+3]=='I')) {
                    //read a few digits, as bug is specific to GEIIS, while GEMS are fine
                    printWarning("GEIIS violates the DICOM standard. Inspect results and admonish your vendor.\n");
                    isIconImageSequence = true;
                    //geiisBug = true; //compressed thumbnails do not follow transfer syntax! GE should not re-use pulbic tags for these proprietary images http://sonca.kasshin.net/gdcm/Doc/GE_ImageThumbnails
                }
                break;
            case kStudyComments: {
            	//char commentStr[kDICOMStr];
                //dcmStr (lLength, &buffer[lPos], commentStr);
                //printf(">> %s\n", commentStr);
                break;
			}
            case kProcedureStepDescription:
                dcmStr (lLength, &buffer[lPos], d.procedureStepDescription);
                break;
            case kOrientationACR : //use in emergency if kOrientation is not present!
                if (!isOrient) dcmMultiFloat(lLength, (char*)&buffer[lPos], 6, d.orient);
                break;
            //case kTemporalPositionIdentifier :
            //	temporalPositionIdentifier =  dcmStrInt(lLength, &buffer[lPos]);
            //    break;
            case kOrientation : {
                if (isOrient) { //already read orient - read for this slice to see if it varies (localizer)
                	float orient[7];
                	dcmMultiFloat(lLength, (char*)&buffer[lPos], 6, orient);
                	if ((!isSameFloatGE(d.orient[1], orient[1]) || !isSameFloatGE(d.orient[2], orient[2]) ||  !isSameFloatGE(d.orient[3], orient[3]) ||
    						!isSameFloatGE(d.orient[4], orient[4]) || !isSameFloatGE(d.orient[5], orient[5]) ||  !isSameFloatGE(d.orient[6], orient[6]) ) ) {
						if (!d.isLocalizer)
							printMessage("slice orientation varies (localizer?) [%g %g %g %g %g %g] != [%g %g %g %g %g %g]\n",
							d.orient[1], d.orient[2], d.orient[3],d.orient[4], d.orient[5], d.orient[6],
							orient[1], orient[2], orient[3],orient[4], orient[5], orient[6]);
						d.isLocalizer = true;
               		}
                }
                dcmMultiFloat(lLength, (char*)&buffer[lPos], 6, d.orient);
                isOrient = true;
                break; }
            case kImagesInAcquisition :
                imagesInAcquisition =  dcmStrInt(lLength, &buffer[lPos]);
                break;
            case kImageStart:
                //if ((!geiisBug) && (!isIconImageSequence)) //do not exit for proprietary thumbnails
                if (isIconImageSequence) {
                	int imgBytes = (d.xyzDim[1] * d.xyzDim[2] * int(d.bitsAllocated / 8));
                	if (imgBytes == lLength)
                		isIconImageSequence = false;
					if ((isIconImageSequence) && (sqDepth < 1)) printWarning("Assuming 7FE0,0010 refers to an icon not the main image\n");

                }
                if ((d.compressionScheme == kCompressNone ) && (!isIconImageSequence)) //do not exit for proprietary thumbnails
                    d.imageStart = (int)lPos + (int)lFileOffset;
                //geiisBug = false;
                //http://www.dclunie.com/medical-image-faq/html/part6.html
                //unlike raw data, Encapsulated data is stored as Fragments contained in Items that are the Value field of Pixel Data
                if ((d.compressionScheme != kCompressNone) && (!isIconImageSequence)) {
                    lLength = 0;
                    isEncapsulatedData = true;
                    encapsulatedDataImageStart = (int)lPos + (int)lFileOffset;
                }
				isIconImageSequence = false;
                break;
            case kImageStartFloat:
                d.isFloat = true;
                if (!isIconImageSequence) //do not exit for proprietary thumbnails
                    d.imageStart = (int)lPos + (int)lFileOffset;
                isIconImageSequence = false;
                break;
            case kImageStartDouble:
                printWarning("Double-precision DICOM conversion untested: please provide samples to developer\n");
                d.isFloat = true;
                if (!isIconImageSequence) //do not exit for proprietary thumbnails
                    d.imageStart = (int)lPos + (int)lFileOffset;
                isIconImageSequence = false;
                break;
        } //switch/case for groupElement

#ifndef USING_R
        if (isVerbose > 1) {
        	//dcm2niix i fast because it does not use a dictionary.
        	// this is a very incomplete DICOM header report, and not a substitute for tools like dcmdump
        	// the purpose is to see how dcm2niix has parsed the image for diagnostics
        	// this section will report very little for implicit data
        	char str[kDICOMStr];
        	sprintf(str, "%*c%04x,%04x %u@%ld ", sqDepth+1, ' ',  groupElement & 65535,groupElement>>16, lLength, lFileOffset+lPos);
			bool isStr = false;
			if (d.isExplicitVR) {
				sprintf(str, "%s%c%c ", str, vr[0], vr[1]);
				if ((vr[0]=='F') && (vr[1]=='D')) sprintf(str, "%s%g ", str, dcmFloatDouble(lLength, &buffer[lPos], d.isLittleEndian));
				if ((vr[0]=='F') && (vr[1]=='L')) sprintf(str, "%s%g ", str, dcmFloat(lLength, &buffer[lPos], d.isLittleEndian));
				if ((vr[0]=='S') && (vr[1]=='S')) sprintf(str, "%s%d ", str, dcmInt(lLength, &buffer[lPos], d.isLittleEndian));
				if ((vr[0]=='S') && (vr[1]=='L')) sprintf(str, "%s%d ", str, dcmInt(lLength,&buffer[lPos],d.isLittleEndian));
				if ((vr[0]=='U') && (vr[1]=='S')) sprintf(str, "%s%d ", str, dcmInt(lLength, &buffer[lPos], d.isLittleEndian));
				if ((vr[0]=='U') && (vr[1]=='L')) sprintf(str, "%s%d ", str, dcmInt(lLength, &buffer[lPos], d.isLittleEndian));
				if ((vr[0]=='A') && (vr[1]=='E')) isStr = true;
				if ((vr[0]=='A') && (vr[1]=='S')) isStr = true;
				//if ((vr[0]=='A') && (vr[1]=='T')) isStr = xxx;
				if ((vr[0]=='C') && (vr[1]=='S')) isStr = true;
				if ((vr[0]=='D') && (vr[1]=='A')) isStr = true;
				if ((vr[0]=='D') && (vr[1]=='S')) isStr = true;
				if ((vr[0]=='D') && (vr[1]=='T')) isStr = true;
				if ((vr[0]=='I') && (vr[1]=='S')) isStr = true;
				if ((vr[0]=='L') && (vr[1]=='O')) isStr = true;
				if ((vr[0]=='L') && (vr[1]=='T')) isStr = true;
				//if ((vr[0]=='O') && (vr[1]=='B')) isStr = xxx;
				//if ((vr[0]=='O') && (vr[1]=='D')) isStr = xxx;
				//if ((vr[0]=='O') && (vr[1]=='F')) isStr = xxx;
				//if ((vr[0]=='O') && (vr[1]=='W')) isStr = xxx;
				if ((vr[0]=='P') && (vr[1]=='N')) isStr = true;
				if ((vr[0]=='S') && (vr[1]=='H')) isStr = true;
				if ((vr[0]=='S') && (vr[1]=='T')) isStr = true;
				if ((vr[0]=='T') && (vr[1]=='M')) isStr = true;
				if ((vr[0]=='U') && (vr[1]=='I')) isStr = true;
				if ((vr[0]=='U') && (vr[1]=='T')) isStr = true;
			} else
				isStr = (lLength > 12); //implicit encoding: not always true as binary vectors may exceed 12 bytes, but often true
        	if (lLength > 128) {
        		sprintf(str, "%s<%d bytes> ", str, lLength);
        		printMessage("%s\n", str);
        	} else if (isStr) { //if length is greater than 8 bytes (+4 hdr) the MIGHT be a string
        		char tagStr[kDICOMStr];
            	//tagStr[0] = 'X'; //avoid compiler warning: orientStr filled by dcmStr
                strcpy(tagStr,"");
                if (lLength > 0)
                	dcmStr (lLength, &buffer[lPos], tagStr);
                if (strlen(tagStr) > 1) {
                	for (size_t pos = 0; pos<strlen(tagStr); pos ++)
						if ((tagStr[pos] == '<') || (tagStr[pos] == '>') || (tagStr[pos] == ':')
            				|| (tagStr[pos] == '"') || (tagStr[pos] == '\\') || (tagStr[pos] == '/')
           					|| (tagStr[pos] == '^') || (tagStr[pos] < 33)
           					|| (tagStr[pos] == '*') || (tagStr[pos] == '|') || (tagStr[pos] == '?'))
            					tagStr[pos] = 'x';
				}
				printMessage("%s %s\n", str, tagStr);
            } else
            	printMessage("%s\n", str);
	    	//if (d.isExplicitVR) printMessage(" VR=%c%c\n", vr[0], vr[1]);
        }   //printMessage(" tag=%04x,%04x length=%u pos=%ld %c%c nest=%d\n",   groupElement & 65535,groupElement>>16, lLength, lPos,vr[0], vr[1], nest);
#endif
        lPos = lPos + (lLength);
        //printMessage("%d\n",d.imageStart);
    	//printMessage(" DWI bxyz %g %g %g %g %d\n", d.CSA.dtiV[0], d.CSA.dtiV[1], d.CSA.dtiV[2], d.CSA.dtiV[3], d.CSA.numDti);

    } //while d.imageStart == 0
    free (buffer);
    if (d.bitsStored < 0) d.isValid = false;
    if (d.bitsStored == 1) printWarning("1-bit binary DICOMs not supported\n"); //maybe not valid - no examples to test
    //printf("%d bval=%g bvec=%g %g %g<<<\n", d.CSA.numDti, d.CSA.dtiV[0], d.CSA.dtiV[1], d.CSA.dtiV[2], d.CSA.dtiV[3]);
    //printMessage("><>< DWI bxyz %g %g %g %g\n", d.CSA.dtiV[0], d.CSA.dtiV[1], d.CSA.dtiV[2], d.CSA.dtiV[3]);
    if (encapsulatedDataFragmentStart > 0) {
        if (encapsulatedDataFragments > 1) {
        	printError("Compressed image stored as %d fragments: decompress with gdcmconv, Osirix, dcmdjpeg or dcmjp2k %s\n", encapsulatedDataFragments, fname);
    	} else {
    		d.imageStart = encapsulatedDataFragmentStart;
        }
    } else if ((isEncapsulatedData) && (d.imageStart < 128)) {
    	//http://www.dclunie.com/medical-image-faq/html/part6.html
		//Uncompressed data (unencapsulated) is sent in DICOM as a series of raw bytes or words (little or big endian) in the Value field of the Pixel Data element (7FE0,0010). Encapsulated data on the other hand is sent not as raw bytes or words but as Fragments contained in Items that are the Value field of Pixel Data
    	printWarning("DICOM violation (contact vendor): compressed image without image fragments, assuming image offset defined by 0x7FE0,x0010: %s\n", fname);
    	d.imageStart = encapsulatedDataImageStart;
    }
    if ((d.modality == kMODALITY_PT) && (PETImageIndex > 0)) {
    	d.imageNum = PETImageIndex; //https://github.com/rordenlab/dcm2niix/issues/184
    	//printWarning("PET scan using 0054,1330 for image number %d\n", PETImageIndex);
    }
    //Recent Philips images include DateTime (0008,002A) but not separate date and time (0008,0022 and 0008,0032)
    #define kYYYYMMDDlen 8 //how many characters to encode year,month,day in "YYYYDDMM" format
    if ((strlen(acquisitionDateTimeTxt) > (kYYYYMMDDlen+5)) && (!isFloatDiff(d.acquisitionTime, 0.0f)) && (!isFloatDiff(d.acquisitionDate, 0.0f)) ) {
		// 20161117131643.80000 -> date 20161117 time 131643.80000
		//printMessage("acquisitionDateTime %s\n",acquisitionDateTimeTxt);
    	char acquisitionDateTxt[kDICOMStr];
        memcpy(acquisitionDateTxt, acquisitionDateTimeTxt, kYYYYMMDDlen);
		acquisitionDateTxt[kYYYYMMDDlen] = '\0'; // IMPORTANT!
        d.acquisitionDate = atof(acquisitionDateTxt);
        char acquisitionTimeTxt[kDICOMStr];
		int timeLen = (int)strlen(acquisitionDateTimeTxt) - kYYYYMMDDlen;
        strncpy(acquisitionTimeTxt, &acquisitionDateTimeTxt[kYYYYMMDDlen], timeLen);
		acquisitionTimeTxt[timeLen] = '\0'; // IMPORTANT!
		d.acquisitionTime = atof(acquisitionTimeTxt);
    }
    d.dateTime = (atof(d.studyDate)* 1000000) + atof(d.studyTime);
    //printMessage("slices in Acq %d %d\n",d.locationsInAcquisition,locationsInAcquisitionPhilips);
    if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (d.locationsInAcquisition == 0))
        d.locationsInAcquisition = locationsInAcquisitionPhilips;
    if ((d.manufacturer == kMANUFACTURER_GE) && (imagesInAcquisition > 0))
        d.locationsInAcquisition = imagesInAcquisition; //e.g. if 72 slices acquired but interpolated as 144
    if ((d.manufacturer == kMANUFACTURER_GE) && (d.locationsInAcquisition > 0)  &&  (locationsInAcquisitionGE > 0) && (d.locationsInAcquisition != locationsInAcquisitionGE) ) {
    	if (isVerbose)  printMessage("Check number of slices, discrepancy between tags (0020,1002; 0021,104F; 0054,0081) (%d vs %d) %s\n", locationsInAcquisitionGE, d.locationsInAcquisition, fname);
    	if (locationsInAcquisitionGE < d.locationsInAcquisition) d.locationsInAcquisition = locationsInAcquisitionGE;
    }
    if ((d.manufacturer == kMANUFACTURER_GE) && (d.locationsInAcquisition == 0))
        d.locationsInAcquisition = locationsInAcquisitionGE;
    if (d.zSpacing > 0.0)
    	d.xyzMM[3] = d.zSpacing; //use zSpacing if provided: depending on vendor, kZThick may or may not include a slice gap
    //printMessage("patientPositions = %d XYZT = %d slicePerVol = %d numberOfDynamicScans %d\n",patientPositionNum,d.xyzDim[3], d.locationsInAcquisition, d.numberOfDynamicScans);
    if ((d.manufacturer == kMANUFACTURER_PHILIPS) && (patientPositionNum > d.xyzDim[3]))
        printMessage("Please check slice thicknesses: Philips R3.2.2 bug can disrupt estimation (%d positions reported for %d slices)\n",patientPositionNum, d.xyzDim[3]); //Philips reported different positions for each slice!
    if ((d.imageStart > 144) && (d.xyzDim[1] > 1) && (d.xyzDim[2] > 1))
    	d.isValid = true;
    //if ((d.imageStart > 144) && (d.xyzDim[1] >= 1) && (d.xyzDim[2] >= 1) && (d.xyzDim[4] > 1)) //Spectroscopy
    //	d.isValid = true;
    if ((d.xyzMM[1] > FLT_EPSILON) && (d.xyzMM[2] < FLT_EPSILON)) {
    	printMessage("Please check voxel size\n");
        d.xyzMM[2] = d.xyzMM[1];
    }
    if ((d.xyzMM[2] > FLT_EPSILON) && (d.xyzMM[1] < FLT_EPSILON)) {
        printMessage("Please check voxel size\n");
        d.xyzMM[1] = d.xyzMM[2];
    }
    if ((d.xyzMM[3] < FLT_EPSILON)) {
        printMessage("Unable to determine slice thickness: please check voxel size\n");
        d.xyzMM[3] = 1.0;
    }
    //printMessage("Patient Position\t%g\t%g\t%g\tThick\t%g\n",d.patientPosition[1],d.patientPosition[2],d.patientPosition[3], d.xyzMM[3]);
    //printMessage("Patient Position\t%g\t%g\t%g\tThick\t%g\tStart\t%d\n",d.patientPosition[1],d.patientPosition[2],d.patientPosition[3], d.xyzMM[3], d.imageStart);
    // printMessage("ser %ld\n", d.seriesNum);
    //int kEchoMult = 100; //For Siemens/GE Series 1,2,3... save 2nd echo as 201, 3rd as 301, etc
    //if (d.seriesNum > 100)
    //    kEchoMult = 10; //For Philips data Saved as Series 101,201,301... save 2nd echo as 111, 3rd as 121, etc
    //if (coilNum > 0) //segment images with multiple coils
    //    d.seriesNum = d.seriesNum + (100*coilNum);
    //if (d.echoNum > 1) //segment images with multiple echoes
    //    d.seriesNum = d.seriesNum + (kEchoMult*d.echoNum);
    if (isPaletteColor) {
    	d.isValid = false;
    	d.isDerived = true; //to my knowledge, palette images always derived
    	printWarning("Photometric Interpretation 'PALETTE COLOR' not supported\n");
    }
    if ((d.compressionScheme == kCompress50) && (d.bitsAllocated > 8) ) {
        //dcmcjpg with +ee can create .51 syntax images that are 8,12,16,24-bit: we can only decode 8/24-bit
        printError("Unable to decode %d-bit images with Transfer Syntax 1.2.840.10008.1.2.4.51, decompress with dcmdjpg or gdcmconv\n", d.bitsAllocated);
        d.isValid = false;
    }
    
    if ((d.CSA.mosaicSlices < 1) && (numberOfImagesInMosaic < 1) && (!isInterpolated) && (d.phaseEncodingLines > 0)  && (frequencyRows > 0) && ((d.xyzDim[1] % frequencyRows) == 0) && ((d.xyzDim[1] / frequencyRows) > 2) && ((d.xyzDim[2] % d.phaseEncodingLines) == 0) && ((d.xyzDim[2] / d.phaseEncodingLines) > 2)  ) {
        //n.b. in future check if frequency is in row or column direction (and same with phase)
        // >2 avoids detecting interpolated as mosaic, in future perhaps check "isInterpolated"
        numberOfImagesInMosaic = (d.xyzDim[1]/frequencyRows) * (d.xyzDim[2]/d.phaseEncodingLines);
        printWarning("Guessing this is a mosaic up to %d slices (issue 337).\n",  numberOfImagesInMosaic);    
    }    
    if ((numberOfImagesInMosaic > 1) && (d.CSA.mosaicSlices < 1))
    	d.CSA.mosaicSlices = numberOfImagesInMosaic;
    if (d.isXA10A) d.manufacturer = kMANUFACTURER_SIEMENS; //XA10A mosaics omit Manufacturer 0008,0070!
    if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (isMosaic) && (d.CSA.mosaicSlices < 1) && (d.phaseEncodingSteps > 0) && ((d.xyzDim[1] % d.phaseEncodingSteps) == 0) && ((d.xyzDim[2] % d.phaseEncodingSteps) == 0) ) {
    	d.CSA.mosaicSlices = (d.xyzDim[1] / d.phaseEncodingSteps) * (d.xyzDim[2] / d.phaseEncodingSteps);
    	printWarning("Mosaic inferred without CSA header (check number of slices and spatial orientation)\n");
    }
    if ((d.isXA10A) && (isMosaic) && (d.CSA.mosaicSlices < 1))
    	d.CSA.mosaicSlices = -1; //mark as bogus DICOM
    if ((!d.isXA10A) && (isMosaic) && (d.CSA.mosaicSlices < 1)) //See Erlangen Vida dataset - never reports "XA10" but mosaics have no attributes
    	printWarning("0008,0008=MOSAIC but number of slices not specified: %s\n", fname);
    if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (d.CSA.dtiV[1] < -1.0) && (d.CSA.dtiV[2] < -1.0) && (d.CSA.dtiV[3] < -1.0))
    	d.CSA.dtiV[0] = 0; //SiemensTrio-Syngo2004A reports B=0 images as having impossible b-vectors.
    if ((d.manufacturer == kMANUFACTURER_GE) && (strlen(d.seriesDescription) > 1)) //GE uses a generic session name here: do not overwrite kProtocolNameGE
		strcpy(d.protocolName, d.seriesDescription);
    if ((strlen(d.protocolName) < 1) && (strlen(d.seriesDescription) > 1))
		strcpy(d.protocolName, d.seriesDescription);
	if ((strlen(d.protocolName) > 1) && (isMoCo))
		strcat (d.protocolName,"_MoCo"); //disambiguate MoCo https://github.com/neurolabusc/MRIcroGL/issues/31
    if ((strlen(d.protocolName) < 1) && (strlen(d.sequenceName) > 1) && (d.manufacturer != kMANUFACTURER_SIEMENS))
		strcpy(d.protocolName, d.sequenceName); //protocolName (0018,1030) optional, sequence name (0018,0024) is not a good substitute for Siemens as it can vary per volume: *ep_b0 *ep_b1000#1, *ep_b1000#2, etc https://www.nitrc.org/forum/forum.php?thread_id=8771&forum_id=4703

	//d.isValid = false;

	//     if (!isOrient) {
	//     	if (d.isNonImage)
	//     		printWarning("Spatial orientation ambiguous  (tag 0020,0037 not found) [probably not important: derived image]: %s\n", fname);
	//     	else if (((d.manufacturer == kMANUFACTURER_SIEMENS)) && (d.samplesPerPixel != 1))
	//     		printWarning("Spatial orientation ambiguous (tag 0020,0037 not found) [perhaps derived FA that is not required]: %s\n", fname);
	//     	else
	//     		printWarning("Spatial orientation ambiguous (tag 0020,0037 not found): %s\n", fname);
	//     }
/*if (d.isHasMagnitude)
	printError("=====> mag %d %d\n", d.patientPositionRepeats, d.patientPositionSequentialRepeats );
if (d.isHasPhase)
	printError("=====> phase %d %d\n", d.patientPositionRepeats, d.patientPositionSequentialRepeats );

	printError("=====> reps %d %d\n", d.patientPositionRepeats, d.patientPositionSequentialRepeats );
*/
	/*if ((patientPositionSequentialRepeats > 1) && ( (d.xyzDim[3] % patientPositionSequentialRepeats) == 0 )) {
		//will require Converting XYTZ to XYZT
		//~ d.numberOfDynamicScans = d.xyzDim[3] / d.patientPositionSequentialRepeats;
		//~ d.xyzDim[4] = d.xyzDim[3] / d.numberOfDynamicScans;
		numberOfDynamicScans = d.xyzDim[3] / patientPositionSequentialRepeats;
		d.xyzDim[4] = d.xyzDim[3] / numberOfDynamicScans;

		d.xyzDim[3] = d.numberOfDynamicScans;
	}*/
	if (numberOfFrames == 0) numberOfFrames = d.xyzDim[3];
	if ((locationsInAcquisitionPhilips > 0) && ((d.xyzDim[3] % locationsInAcquisitionPhilips) == 0)) {
		d.xyzDim[4] = d.xyzDim[3] / locationsInAcquisitionPhilips;
		d.xyzDim[3] = locationsInAcquisitionPhilips;
	} else if ((numberOfDynamicScans > 1) && (d.xyzDim[4] < 2) && (d.xyzDim[3] > 1) && ((d.xyzDim[3] % numberOfDynamicScans) == 0)) {
		d.xyzDim[3] = d.xyzDim[3] / numberOfDynamicScans;
		d.xyzDim[4] = numberOfDynamicScans;
	}
	if ((maxInStackPositionNumber > 1) && (d.xyzDim[4] < 2) && (d.xyzDim[3] > 1) && ((d.xyzDim[3] % maxInStackPositionNumber) == 0)) {
		d.xyzDim[4] = d.xyzDim[3] / maxInStackPositionNumber;
		d.xyzDim[3] = maxInStackPositionNumber;
	}
	if ((!isnan(patientPositionStartPhilips[1])) && (!isnan(patientPositionEndPhilips[1]))) {
			for (int k = 0; k < 4; k++) {
				d.patientPosition[k] = patientPositionStartPhilips[k];
				d.patientPositionLast[k] = patientPositionEndPhilips[k];
			}
    }
    if (!isnan(patientPositionStartPhilips[1])) //for Philips data without
		for (int k = 0; k < 4; k++)
			d.patientPosition[k] = patientPositionStartPhilips[k];
	if (isVerbose) {
        printMessage("DICOM file: %s\n", fname);
        printMessage(" patient position (0020,0032)\t%g\t%g\t%g\n", d.patientPosition[1],d.patientPosition[2],d.patientPosition[3]);
        if (!isnan(patientPositionEndPhilips[1]))
        	printMessage(" patient position end (0020,0032)\t%g\t%g\t%g\n", patientPositionEndPhilips[1],patientPositionEndPhilips[2],patientPositionEndPhilips[3]);
        printMessage(" orient (0020,0037)\t%g\t%g\t%g\t%g\t%g\t%g\n", d.orient[1],d.orient[2],d.orient[3], d.orient[4],d.orient[5],d.orient[6]);
        printMessage(" acq %d img %d ser %ld dim %dx%dx%dx%d mm %gx%gx%g offset %d loc %d valid %d ph %d mag %d nDTI %d 3d %d bits %d littleEndian %d echo %d coilCRC %d TE %g TR %g\n",d.acquNum,d.imageNum,d.seriesNum,d.xyzDim[1],d.xyzDim[2],d.xyzDim[3], d.xyzDim[4],d.xyzMM[1],d.xyzMM[2],d.xyzMM[3],d.imageStart, d.locationsInAcquisition, d.isValid, d.isHasPhase, d.isHasMagnitude, d.CSA.numDti, d.is3DAcq, d.bitsAllocated, d.isLittleEndian, d.echoNum, d.coilCrc, d.TE, d.TR);
        if (d.CSA.dtiV[0] > 0)
        	printMessage(" DWI bxyz %g %g %g %g\n", d.CSA.dtiV[0], d.CSA.dtiV[1], d.CSA.dtiV[2], d.CSA.dtiV[3]);
    }
    if ((d.isValid) && (d.xyzDim[1] > 1) && (d.xyzDim[2] > 1) && (d.imageStart < 132) && (!d.isRawDataStorage)) {
    	//20190524: Philips MR 55.1 creates non-image files that report kDim1/kDim2 - we can detect them since 0008,0016 reports "RawDataStorage"
    	//see https://neurostars.org/t/dcm2niix-error-from-philips-dicom-qsm-data-can-this-be-skipped/4883
    	printError("Conversion aborted due to corrupt file: %s %dx%d %d\n", fname, d.xyzDim[1], d.xyzDim[2], d.imageStart);
#ifdef USING_R
        Rf_error("Irrecoverable error during conversion");
#else
    	exit (kEXIT_CORRUPT_FILE_FOUND);
#endif
    }
    if ((numDimensionIndexValues > 1) && (numDimensionIndexValues == numberOfFrames)) {
    	//Philips enhanced datasets can have custom slice orders and pack images with different TE, Phase/Magnitude/Etc.
    	if (isVerbose > 1) { //
			int mn[MAX_NUMBER_OF_DIMENSIONS];
			int mx[MAX_NUMBER_OF_DIMENSIONS];
			for (int j = 0; j < MAX_NUMBER_OF_DIMENSIONS; j++) {
				mx[j] = dcmDim[0].dimIdx[j];
				mn[j] = mx[j];
				for (int i = 0; i < numDimensionIndexValues; i++) {
					if (mx[j] < dcmDim[i].dimIdx[j]) mx[j] = dcmDim[i].dimIdx[j];
					if (mn[j] > dcmDim[i].dimIdx[j]) mn[j] = dcmDim[i].dimIdx[j];
				}
			}
			printMessage(" DimensionIndexValues (0020,9157), dimensions with variability:\n");
			for (int i = 0; i < MAX_NUMBER_OF_DIMENSIONS; i++)
				if (mn[i] != mx[i])
					printMessage(" Dimension %d Range: %d..%d\n", i, mn[i], mx[i]);
    	} //verbose > 1
    	//sort dimensions
#ifdef USING_R
        std::sort(dcmDim.begin(), dcmDim.begin() + numberOfFrames, compareTDCMdim);
#else
        qsort(dcmDim, numberOfFrames, sizeof(struct TDCMdim), compareTDCMdim);
#endif
		//for (int i = 0; i < numberOfFrames; i++)
		//	printf("diskPos= %d dimIdx= %d  %d %d %d TE= %g\n", i,  dcmDim[i].diskPos, dcmDim[i].dimIdx[1], dcmDim[i].dimIdx[2], dcmDim[i].dimIdx[3], dti4D->TE[i]);
		for (int i = 0; i < numberOfFrames; i++)
			dti4D->sliceOrder[i] = dcmDim[i].diskPos;
		if ( !(d.manufacturer == kMANUFACTURER_BRUKER && d.isDiffusion) && (d.xyzDim[4] > 1) && (d.xyzDim[4] < kMaxDTI4D)) { //record variations in TE
			d.isScaleOrTEVaries = false;
			bool isTEvaries = false;
			bool isScaleVaries = false;
			//setting j = 1 in next few lines is a hack, just in case TE/scale/intercept listed AFTER dimensionIndexValues
			int j = 0;
			if (d.xyzDim[3] > 1) j = 1;
			for (int i = 0; i < d.xyzDim[4]; i++) {
				//dti4D->gradDynVol[i] = 0; //only PAR/REC
				dti4D->TE[i] =  dcmDim[j+(i * d.xyzDim[3])].TE;
				dti4D->intenScale[i] =  dcmDim[j+(i * d.xyzDim[3])].intenScale;
				dti4D->intenIntercept[i] =  dcmDim[j+(i * d.xyzDim[3])].intenIntercept;
				dti4D->isPhase[i] =  dcmDim[j+(i * d.xyzDim[3])].isPhase;
				dti4D->isReal[i] =  dcmDim[j+(i * d.xyzDim[3])].isReal;
				dti4D->isImaginary[i] =  dcmDim[j+(i * d.xyzDim[3])].isImaginary;
				dti4D->intenScalePhilips[i] =  dcmDim[j+(i * d.xyzDim[3])].intenScalePhilips;
				dti4D->RWVIntercept[i] =  dcmDim[j+(i * d.xyzDim[3])].RWVIntercept;
				dti4D->RWVScale[i] =  dcmDim[j+(i * d.xyzDim[3])].RWVScale;
				dti4D->triggerDelayTime[i] =  dcmDim[j+(i * d.xyzDim[3])].triggerDelayTime;
				dti4D->S[i].V[0] = dcmDim[j+(i * d.xyzDim[3])].V[0];
				dti4D->S[i].V[1] = dcmDim[j+(i * d.xyzDim[3])].V[1];
				dti4D->S[i].V[2] = dcmDim[j+(i * d.xyzDim[3])].V[2];
				dti4D->S[i].V[3] = dcmDim[j+(i * d.xyzDim[3])].V[3];
				if (dti4D->TE[i] != d.TE) isTEvaries = true;
				if (dti4D->intenScale[i] != d.intenScale) isScaleVaries = true;
				if (dti4D->intenIntercept[i] != d.intenIntercept) isScaleVaries = true;
				if (dti4D->isPhase[i] != isPhase) d.isScaleOrTEVaries = true;
				if (dti4D->triggerDelayTime[i] != d.triggerDelayTime) d.isScaleOrTEVaries = true;
				if (dti4D->isReal[i] != isReal) d.isScaleOrTEVaries = true;
				if (dti4D->isImaginary[i] != isImaginary) d.isScaleOrTEVaries = true;
			}
			if((isScaleVaries) || (isTEvaries)) d.isScaleOrTEVaries = true;
			if (isTEvaries) d.isMultiEcho = true;
			//if echoVaries,count number of echoes
			/*int echoNum = 1;
			for (int i = 1; i < d.xyzDim[4]; i++) {
				if (dti4D->TE[i-1] != dti4D->TE[i])
			}*/
			if ((isVerbose) && (d.isScaleOrTEVaries)) {
				printMessage("Parameters vary across 3D volumes packed in single DICOM file:\n");
				for (int i = 0; i < d.xyzDim[4]; i++)
					printMessage(" %d TE=%g Slope=%g Inter=%g PhilipsScale=%g Phase=%d\n", i, dti4D->TE[i], dti4D->intenScale[i], dti4D->intenIntercept[i], dti4D->intenScalePhilips[i], dti4D->isPhase[i] );
			}
		}
		if ((d.xyzDim[3] == maxInStackPositionNumber) && (maxInStackPositionNumber > 1) &&  (d.zSpacing <= 0.0)) {
			float dx =  sqrt( pow(d.patientPosition[1]-d.patientPositionLast[1],2)+
                pow(d.patientPosition[2]-d.patientPositionLast[2],2)+
                pow(d.patientPosition[3]-d.patientPositionLast[3],2));
            dx = dx / (maxInStackPositionNumber - 1);
			if ((dx > 0.0) && (!isSameFloatGE(dx, d.xyzMM[3])) ) //patientPosition has some rounding error
				d.xyzMM[3] = dx;
		} //d.zSpacing <= 0.0: Bruker does not populate 0018,0088 https://github.com/rordenlab/dcm2niix/issues/241
    } //if numDimensionIndexValues > 1 : enhanced DICOM
    /* //Attempt to append ADC
    printMessage("CXC grad %g %d %d\n", philDTI[0].V[0], maxGradNum, d.xyzDim[4]);
	if ((maxGradNum > 1) && ((maxGradNum+1) == d.xyzDim[4]) ) {
		//ADC map (non-zero b-value with zero vector length)
		if (isVerbose)
			printMessage("Final volume does not have an associated 0020,9157. Assuming final volume is an ADC/isotropic map\n", philDTI[0].V[0], maxGradNum, d.xyzDim[4]);
		philDTI[maxGradNum].V[0] = 1000.0;
		philDTI[maxGradNum].V[1] = 0.0;
		philDTI[maxGradNum].V[2] = 0.0;
		philDTI[maxGradNum].V[3] = 0.0;
		maxGradNum++;
	}*/
    /*if  ((minGradNum >= 1) && ((maxGradNum-minGradNum+1) == d.xyzDim[4])) {
    	//see ADNI DWI data for 018_S_4868 - the gradient numbers are in the range 2..37 for 36 volumes - no gradient number 1!
    	if (philDTI[minGradNum -1].V[0] >= 0) {
			if (isVerbose)
				printMessage("Using %d diffusion data directions coded by DimensionIndexValues\n", maxGradNum);
			int off = 0;
			if (minGradNum > 1) {
				off = minGradNum - 1;
				printWarning("DimensionIndexValues (0020,9157) is not indexed from 1 (range %d..%d). Please validate results\n", minGradNum, maxGradNum);
			}
			for (int i = 0; i < d.xyzDim[4]; i++) {
				dti4D->S[i].V[0] = philDTI[i+off].V[0];
				dti4D->S[i].V[1] = philDTI[i+off].V[1];
				dti4D->S[i].V[2] = philDTI[i+off].V[2];
				dti4D->S[i].V[3] = philDTI[i+off].V[3];
				if (isVerbose > 1)
					printMessage(" grad %d b=%g vec=%gx%gx%g\n", i, dti4D->S[i].V[0], dti4D->S[i].V[1], dti4D->S[i].V[2], dti4D->S[i].V[3]);
			}
			d.CSA.numDti = maxGradNum - off;
		}
	}*/
    if (d.CSA.numDti >= kMaxDTI4D) {
        printError("Unable to convert DTI [recompile with increased kMaxDTI4D] detected=%d, max = %d\n", d.CSA.numDti, kMaxDTI4D);
        d.CSA.numDti = 0;
    }
    if ((d.isValid) && (d.imageNum == 0)) { //Philips non-image DICOMs (PS_*, XX_*) are not valid images and do not include instance numbers
    	//TYPE 1 for MR image http://dicomlookup.com/lookup.asp?sw=Tnumber&q=(0020,0013)
    	// Only type 2 for some other DICOMs! Therefore, generate warning not error
    	printWarning("Instance number (0020,0013) not found: %s\n", fname);
    	d.imageNum = abs((int)d.instanceUidCrc) % 2147483647;//INT_MAX;
    	if (d.imageNum == 0) d.imageNum = 1; //https://github.com/rordenlab/dcm2niix/issues/341
    	//d.imageNum = 1; //not set
    }
    if ((numDimensionIndexValues < 1) && (d.manufacturer == kMANUFACTURER_PHILIPS) && (d.seriesNum > 99999) && (philMRImageDiffBValueNumber > 0)) {
    	//Ugly kludge to distinguish Philips classic DICOM dti
    	// images from a single sequence can have identical series number, instance number, gradient number
    	// the ONLY way to distinguish slices is using the private tag MRImageDiffBValueNumber
    	// confusingly, the same sequence can also generate MULTIPLE series numbers!
    	// for examples see https://www.nitrc.org/plugins/mwiki/index.php/dcm2nii:MainPage#Diffusion_Tensor_Imaging
    	d.seriesNum += (philMRImageDiffBValueNumber*1000);
    }
    //if (contentTime != 0.0) && (numDimensionIndexValues < (MAX_NUMBER_OF_DIMENSIONS - 1)){
    //	uint_32t timeCRC = mz_crc32X((unsigned char*) &contentTime, sizeof(double));
    //}
    //if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (strcmp(d.sequenceName, "fldyn3d1")== 0)) {
    if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (strstr(d.sequenceName, "fldyn3d1") != NULL)) {
    	//combine DCE series https://github.com/rordenlab/dcm2niix/issues/252
    	d.isStackableSeries = true;
		d.imageNum += (d.seriesNum * 1000);
		strcpy(d.seriesInstanceUID, d.studyInstanceUID);
		d.seriesUidCrc = mz_crc32X((unsigned char*) &d.protocolName, strlen(d.protocolName));
	}
    if ((d.manufacturer == kMANUFACTURER_SIEMENS) && (strstr(d.sequenceName, "_fl2d1") != NULL)) {
    	d.isLocalizer = true;
	}
	//printf(">>%s\n", d.sequenceName); d.isValid = false;
	// Andrey Fedorov has requested keeping GE bvalues, see issue 264
	//if ((d.CSA.numDti > 0) && (d.manufacturer == kMANUFACTURER_GE) && (d.numberOfDiffusionDirectionGE < 1))
	//	d.CSA.numDti = 0; //https://github.com/rordenlab/dcm2niix/issues/264
    if ((!d.isLocalizer) && (isInterpolated) && (d.imageNum <= 1))
    	printWarning("interpolated protocol '%s' may be unsuitable for dwidenoise/mrdegibbs. %s\n", d.protocolName, fname);
    if ((numDimensionIndexValues+2) < MAX_NUMBER_OF_DIMENSIONS)
    	d.dimensionIndexValues[MAX_NUMBER_OF_DIMENSIONS-3] = d.instanceUidCrc;
    if ((numDimensionIndexValues+1) < MAX_NUMBER_OF_DIMENSIONS)
    	d.dimensionIndexValues[MAX_NUMBER_OF_DIMENSIONS-2] = d.echoNum;
    if (numDimensionIndexValues < MAX_NUMBER_OF_DIMENSIONS) //https://github.com/rordenlab/dcm2niix/issues/221
    	d.dimensionIndexValues[MAX_NUMBER_OF_DIMENSIONS-1] = mz_crc32X((unsigned char*) &d.seriesInstanceUID, strlen(d.seriesInstanceUID));
    if ((d.isValid) && (d.seriesUidCrc == 0)) {
	    if (d.seriesNum < 1) 
	        d.seriesUidCrc = 1; //no series information
	    else
	        d.seriesUidCrc = d.seriesNum; //file does not have Series UID, use series number instead   
	}
    if (d.seriesNum < 1) //https://github.com/rordenlab/dcm2niix/issues/218
		d.seriesNum = mz_crc32X((unsigned char*) &d.seriesInstanceUID, strlen(d.seriesInstanceUID));
    getFileName(d.imageBaseName, fname);
    if (multiBandFactor > d.CSA.multiBandFactor)
    	d.CSA.multiBandFactor = multiBandFactor; //SMS reported in 0051,1011 but not CSA header
    #ifndef myLoadWholeFileToReadHeader
	fclose(file);
	#endif
	if (hasDwiDirectionality) d.isVectorFromBMatrix = false; //issue 265: Philips/Siemens have both directionality and bmatrix, Bruker only has bmatrix
    /*
    fixed 2/2019 by modifying to kDiffusionBFactor, kDiffusionDirectionRL, kDiffusionDirectionAP, kDiffusionDirectionFH
    if ((d.xyzDim[3] == 1) && (numDimensionIndexValues < 1) && (d.manufacturer == kMANUFACTURER_PHILIPS) && (B0Philips >= 0.0)) {
    	//Special case: old Philips Classic DWI storing vectors in 0019,10bb, 0019,10bc
    	//printf(">>>>%g  %g %g %g\n",B0Philips, vRLPhilips, vAPPhilips, vFHPhilips);
    	d.CSA.dtiV[0] = B0Philips;
    	d.CSA.dtiV[1] = vRLPhilips;
    	d.CSA.dtiV[2] = vAPPhilips;
    	d.CSA.dtiV[3] = vFHPhilips;
    	d.CSA.numDti = 1;
	}
	*/
	//printf("%s\t%s\t%s\t%s\t%s_%s\n",d.patientBirthDate, d.procedureStepDescription,d.patientName, fname, d.studyDate, d.studyTime);
	//d.isValid = false;
	//printf("%g\t\t%g\t%g\t%g\t%s\n", d.CSA.dtiV[0], d.CSA.dtiV[1], d.CSA.dtiV[2], d.CSA.dtiV[3], fname);
	//printMessage("buffer usage %d  %d  %d\n",d.imageStart, lPos+lFileOffset, MaxBufferSz);
	return d;
} // readDICOM()

struct TDICOMdata readDICOM(char * fname) {
    TDTI4D unused;
    return readDICOMv(fname, false, kCompressSupport, &unused);
} // readDICOM()

