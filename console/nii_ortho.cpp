#ifndef HAVE_R
#include "nifti1.h"
#endif
#include "nifti1_io_core.h"
#include "nii_ortho.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <stddef.h>
#include <float.h>
//#include <unistd.h>
#include <stdio.h>
#ifndef _MSC_VER

	#include <unistd.h>

#endif
//#define MY_DEBUG //verbose text reporting

#include "print.h"

typedef struct  {
    int v[3];
} vec3i;

mat33 matDotMul33 (mat33 a, mat33 b)
// in Matlab: ret = a'.*b
{
    mat33 ret;
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            ret.m[i][j] = a.m[i][j]*b.m[j][i];
        }
    }
    return ret;
}

mat33 matMul33 (mat33 a, mat33 b)
// mult = a * b
{
    mat33 mult;
    for(int i=0;i<3;i++)
    {
        for(int j=0;j<3;j++)
        {
            mult.m[j][i]=0;
            for(int k=0;k<3;k++)
                mult.m[j][i]+=a.m[j][k]*b.m[k][i];
        }
    }
    return mult;
}

float getOrthoResidual (mat33 orig, mat33 transform)
{
    mat33 mat = matDotMul33(orig, transform);
    float ret = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            ret = ret + (mat.m[i][j]);
        }
    }
    return ret;
}

mat33 getBestOrient(mat44 R, vec3i flipVec)
//flipVec reports flip: [1 1 1]=no flips, [-1 1 1] flip X dimension
{
    mat33 ret, newmat, orig;
    LOAD_MAT33(orig,R.m[0][0],R.m[0][1],R.m[0][2],
               R.m[1][0],R.m[1][1],R.m[1][2],
               R.m[2][0],R.m[2][1],R.m[2][2]);
    float best = 0;//FLT_MAX;
    float newval;
    for (int rot = 0; rot < 6; rot++) { //6 rotations
        switch (rot) {
            case 0: LOAD_MAT33(newmat,flipVec.v[0],0,0, 0,flipVec.v[1],0, 0,0,flipVec.v[2]); break;
            case 1: LOAD_MAT33(newmat,flipVec.v[0],0,0, 0,0,flipVec.v[1], 0,flipVec.v[2],0); break;
            case 2: LOAD_MAT33(newmat,0,flipVec.v[0],0, flipVec.v[1],0,0, 0,0,flipVec.v[2]); break;
            case 3: LOAD_MAT33(newmat,0,flipVec.v[0],0, 0,0,flipVec.v[1], flipVec.v[2],0,0); break;
            case 4: LOAD_MAT33(newmat,0,0,flipVec.v[0], flipVec.v[1],0,0, 0,flipVec.v[2],0); break;
            case 5: LOAD_MAT33(newmat,0,0,flipVec.v[0], 0,flipVec.v[1],0, flipVec.v[2],0,0); break;
        }
        newval = getOrthoResidual(orig, newmat);
        if (newval > best) {
            best = newval;
            ret = newmat;
        }
    }
    return ret;
}

bool isMat44Canonical(mat44 R)
//returns true if diagonals >0 and all others =0
//  no rotation is necessary - already in perfect orthogonal alignment
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if ((i == j) && (R.m[i][j] <= 0) ) return false;
            if ((i != j) && (R.m[i][j] != 0) ) return false;
        }//j
    }//i
    return true;
}

vec3i setOrientVec(mat33 m)
// Assumes isOrthoMat NOT computed on INVERSE, hence return INVERSE of solution...
//e.g. [-1,2,3] means reflect x axis, [2,1,3] means swap x and y dimensions
{
    vec3i ret = {{0, 0, 0}};
    //mat33 m = {-1,0,0, 0,1,0, 0,0,1};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (m.m[i][j] > 0) ret.v[j] = i+1;
            if (m.m[i][j] < 0) ret.v[j] = -(i+1);
        }//j
    }//i
    return ret;
}

mat44 setMat44Vec(mat33 m33, vec3 Translations)
//convert a 3x3 rotation matrix to a 4x4 matrix where the last column stores translations and the last row is 0 0 0 1
{
    mat44 m44;
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            m44.m[i][j] = m33.m[i][j];
        }
    }
    m44.m[0][3] = Translations.v[0];
    m44.m[1][3] = Translations.v[1];
    m44.m[2][3] = Translations.v[2];
    m44.m[3][0] = 0;
    m44.m[3][1] = 0;
    m44.m[3][2] = 0;
    m44.m[3][3] = 1;
    return m44;
}

mat44 sFormMat(struct nifti_1_header *h) {
    mat44 s;
    s.m[0][0]=h->srow_x[0];
    s.m[0][1]=h->srow_x[1];
    s.m[0][2]=h->srow_x[2];
    s.m[0][3]=h->srow_x[3];
    s.m[1][0]=h->srow_y[0];
    s.m[1][1]=h->srow_y[1];
    s.m[1][2]=h->srow_y[2];
    s.m[1][3]=h->srow_y[3];
    s.m[2][0]=h->srow_z[0];
    s.m[2][1]=h->srow_z[1];
    s.m[2][2]=h->srow_z[2];
    s.m[2][3]=h->srow_z[3];
    s.m[3][0] = 0 ;
    s.m[3][1] = 0 ;
    s.m[3][2] = 0 ;
    s.m[3][3] = 1 ;
    return s;
}

void mat2sForm (struct nifti_1_header *h, mat44 s) {
    h->srow_x[0] = s.m[0][0];
    h->srow_x[1] = s.m[0][1];
    h->srow_x[2] = s.m[0][2];
    h->srow_x[3] = s.m[0][3];
    h->srow_y[0] = s.m[1][0];
    h->srow_y[1] = s.m[1][1];
    h->srow_y[2] = s.m[1][2];
    h->srow_y[3] = s.m[1][3];
    h->srow_z[0] = s.m[2][0];
    h->srow_z[1] = s.m[2][1];
    h->srow_z[2] = s.m[2][2];
    h->srow_z[3] = s.m[2][3];
}

size_t* orthoOffsetArray(int dim, int stepBytesPerVox) {
    //return lookup table of length dim with values incremented by stepBytesPerVox
    // e.g. if Dim=10 and stepBytes=2: 0,2,4..18, is stepBytes=-2 18,16,14...0
    size_t *lut= (size_t *)malloc(dim*sizeof(size_t));
    if (stepBytesPerVox > 0)
        lut[0] = 0;
    else
        lut[0] = -stepBytesPerVox  *(dim-1);
    if (dim > 1)
        for (int i=1; i < dim; i++) lut[i] = lut[i-1] + (size_t)stepBytesPerVox;
    return lut;
} //orthoOffsetArray()

//void  reOrientImg( unsigned char * restrict img, vec3i outDim, vec3i outInc, int bytePerVox, int nvol) {
void  reOrientImg( unsigned char *  img, vec3i outDim, vec3i outInc, int bytePerVox, int nvol) {
    //reslice data to new orientation
    //generate look up tables
    size_t* xLUT =orthoOffsetArray(outDim.v[0], bytePerVox*outInc.v[0]);
    size_t* yLUT =orthoOffsetArray(outDim.v[1], bytePerVox*outInc.v[1]);
    size_t* zLUT =orthoOffsetArray(outDim.v[2], bytePerVox*outInc.v[2]);
    //convert data
    size_t bytePerVol = bytePerVox*outDim.v[0]*outDim.v[1]*outDim.v[2]; //number of voxels in spatial dimensions [1,2,3]
    size_t o = 0; //output address
    uint8_t *inbuf = (uint8_t *) malloc(bytePerVol); //we convert 1 volume at a time
    uint8_t *outbuf = (uint8_t *) img; //source image
    for (int vol= 0; vol < nvol; vol++) {
        memcpy(&inbuf[0], &outbuf[vol*bytePerVol], bytePerVol); //copy source volume
        for (int z = 0; z < outDim.v[2]; z++)
            for (int y = 0; y < outDim.v[1]; y++)
                for (int x = 0; x < outDim.v[0]; x++) {
                    memcpy(&outbuf[o], &inbuf[xLUT[x]+yLUT[y]+zLUT[z]], bytePerVox);
                    o = o+ bytePerVox;
                } //for each x
    } //for each volume
    //free arrays
    free(inbuf);
    free(xLUT);
    free(yLUT);
    free(zLUT);
} //reOrientImg

unsigned char *  reOrient(unsigned char* img, struct nifti_1_header *h, vec3i orientVec, mat33 orient, vec3 minMM)
//e.g. [-1,2,3] means reflect x axis, [2,1,3] means swap x and y dimensions
{
    size_t nvox = h->dim[1] * h->dim[2] * h->dim[3];
    if (nvox < 1) return img;
    vec3i outDim= {{0,0,0}};
    vec3i outInc= {{0,0,0}};
    for (int i = 0; i < 3; i++) { //set dimension, pixdim and
        outDim.v[i] =  h->dim[abs(orientVec.v[i])];
        if (abs(orientVec.v[i]) == 1) outInc.v[i] = 1;
        if (abs(orientVec.v[i]) == 2) outInc.v[i] = h->dim[1];
        if (abs(orientVec.v[i]) == 3) outInc.v[i] = h->dim[1]*h->dim[2];
        if (orientVec.v[i] < 0) outInc.v[i] = -outInc.v[i]; //flip
    } //for each dimension
    int nvol = 1; //convert all non-spatial volumes from source to destination
    for (int vol = 4; vol < 8; vol++) {
        if (h->dim[vol] > 1)
            nvol = nvol * h->dim[vol];
    }
    reOrientImg(img, outDim, outInc, h->bitpix / 8,  nvol);
    //now change the header....
    vec3 outPix= {{h->pixdim[abs(orientVec.v[0])],h->pixdim[abs(orientVec.v[1])],h->pixdim[abs(orientVec.v[2])]}};
    for (int i = 0; i < 3; i++) {
        h->dim[i+1] = outDim.v[i];
        h->pixdim[i+1] = outPix.v[i];
    }
    mat44 s = sFormMat(h);
    mat33 mat; //computer transform
    LOAD_MAT33(mat, s.m[0][0],s.m[0][1],s.m[0][2],
                         s.m[1][0],s.m[1][1],s.m[1][2],
                         s.m[2][0],s.m[2][1],s.m[2][2]);
    mat = matMul33(  mat, orient);
    s = setMat44Vec(mat, minMM); //add offset
    mat2sForm(h,s);
    h->qform_code = h->sform_code; //apply to the quaternion as well
    float dumdx, dumdy, dumdz;
    nifti_mat44_to_quatern( s , &h->quatern_b, &h->quatern_c, &h->quatern_d,&h->qoffset_x, &h->qoffset_y, &h->qoffset_z, &dumdx, &dumdy, &dumdz,&h->pixdim[0]) ;
    return img;
} //reOrient()

float getDistance (vec3 v, vec3 min)
//scalar distance between two 3D points - Pythagorean theorem
{
    return sqrt(pow((v.v[0]-min.v[0]),2)+pow((v.v[1]-min.v[1]),2)+pow((v.v[2]-min.v[2]),2)  );
}

vec3 xyz2mm (mat44 R, vec3 v)
{
    vec3 ret;
    for (int i = 0; i < 3; i++) {
        ret.v[i] = ( (R.m[i][0]*v.v[0])+(R.m[i][1]*v.v[1])+ (R.m[i][2]*v.v[2])+R.m[i][3] );
    }
    return ret;
}

vec3 minCornerFlip (struct nifti_1_header *h, vec3i* flipVec)
//orthogonal rotations and reflections applied as 3x3 matrices will cause the origin to shift
// a simple solution is to first compute the most left, posterior, inferior voxel in the source image
// this voxel will be at location i,j,k = 0,0,0, so we can simply use this as the offset for the final 4x4 matrix...
{
    int i,j, minIndex;
    vec3i flipVecs[8];
    vec3 corner[8], min;
    mat44 s = sFormMat(h);
    for (int i = 0; i < 8; i++) {
        if (i & 1) flipVecs[i].v[0] = -1; else flipVecs[i].v[0] = 1;
        if (i & 2) flipVecs[i].v[1] = -1; else flipVecs[i].v[1] = 1;
        if (i & 4) flipVecs[i].v[2] = -1; else flipVecs[i].v[2] = 1;
        corner[i] = setVec3(0,0,0); //assume no reflections
        if ((flipVecs[i].v[0]) < 1) corner[i].v[0] = h->dim[1]-1; //reflect X
        if ((flipVecs[i].v[1]) < 1) corner[i].v[1] = h->dim[2]-1; //reflect Y
        if ((flipVecs[i].v[2]) < 1) corner[i].v[2] = h->dim[3]-1; //reflect Z
        corner[i] = xyz2mm(s,corner[i]);
    }
    //find extreme edge from ALL corners....
    min = corner[0];
    for (i = 1; i < 8; i++) {
        for (j = 0; j < 3; j++) {
            if (corner[i].v[j]< min.v[j]) min.v[j] = corner[i].v[j];
        }
    }
    float dx; //observed distance from corner
    float min_dx = getDistance (corner[0], min);
    minIndex = 0; //index of corner closest to min
    //see if any corner is closer to absmin than the first one...
    for (i = 1; i < 8; i++) {
        dx = getDistance (corner[i], min);
        if (dx < min_dx) {
            min_dx = dx;
            minIndex = i;
        }
    }
    min = corner[minIndex]; //this is the single corner closest to min from all
    *flipVec= flipVecs[minIndex];
    return min;
}

#ifdef MY_DEBUG
void reportMat44o(char *str, mat44 A) {
    printMessage("%s = [%g %g %g %g; %g %g %g %g; %g %g %g %g; 0 0 0 1]\n",str,
           A.m[0][0],A.m[0][1],A.m[0][2],A.m[0][3],
           A.m[1][0],A.m[1][1],A.m[1][2],A.m[1][3],
           A.m[2][0],A.m[2][1],A.m[2][2],A.m[2][3]);
}
#endif

unsigned char *  nii_setOrtho(unsigned char* img, struct nifti_1_header *h) {
    if ((h->dim[1] < 1) || (h->dim[2] < 1) || (h->dim[3] < 1)) return img;
    if ((h->sform_code == NIFTI_XFORM_UNKNOWN) && (h->qform_code != NIFTI_XFORM_UNKNOWN)) { //only q-form provided
        mat44 q = nifti_quatern_to_mat44(h->quatern_b, h->quatern_c, h->quatern_d,
                                         h->qoffset_x, h->qoffset_y, h->qoffset_z,
                                         h->pixdim[1], h->pixdim[2], h->pixdim[3],h->pixdim[0]);
        mat2sForm(h,q); //convert q-form to s-form
        h->sform_code = h->qform_code;
    }
    if (h->sform_code == NIFTI_XFORM_UNKNOWN) {
         #ifdef MY_DEBUG
         printMessage("No Q or S spatial transforms - assuming canonical orientation");
         #endif
         return img;
    }
    mat44 s = sFormMat(h);
    if (isMat44Canonical( s)) {
        #ifdef MY_DEBUG
        printMessage("Image in perfect alignment: no need to reorient");
        #endif
        return img;
    }
    vec3i  flipV;
    vec3 minMM = minCornerFlip(h, &flipV);
    mat33 orient = getBestOrient(s, flipV);
    vec3i orientVec = setOrientVec(orient);
    if ((orientVec.v[0]==1) && (orientVec.v[1]==2) && (orientVec.v[2]==3) ) {
        #ifdef MY_DEBUG
        printMessage("Image already near best orthogonal alignment: no need to reorient\n");
        #endif
        return img;
    }
    bool is24 = false;
    if (h->bitpix == 24 ) { //RGB stored as planar data. treat as 3 8-bit slices
        return img;
        is24 = true;
        h->bitpix = 8;
        h->dim[3] = h->dim[3] * 3;
    }
    img = reOrient(img, h,orientVec, orient, minMM);
    if (is24 ) {
        h->bitpix = 24;
        h->dim[3] = h->dim[3] / 3;
    }
    #ifdef MY_DEBUG
    printMessage("NewRotation= %d %d %d\n", orientVec.v[0],orientVec.v[1],orientVec.v[2]);
    printMessage("MinCorner= %.2f %.2f %.2f\n", minMM.v[0],minMM.v[1],minMM.v[2]);
    reportMat44o((char*)"input",s);
    s = sFormMat(h);
    reportMat44o((char*)"output",s);
    #endif
    return img;
}



