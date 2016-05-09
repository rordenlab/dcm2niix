
#include "nii_ostu_ml.h"
#include <stdint.h>
#include <math.h>
#include <stdlib.h> // pulls in declaration of malloc, free
#include <string.h> // pulls in declaration for strlen.
#include <stdio.h>

//Otsu's Method [Multilevel]
//Otsu N (1979) A threshold selection method from gray-level histograms. IEEE Trans. Sys., Man., Cyber. 9: 62-66.
//Lookup Tables as suggested by Liao, Chen and Chung (2001) A fast algorithm for multilevel thresholding
//note my "otsu" method is slightly faster and much simpler than otsu_ml if you only want bi-level output


//typedef double HistoRAd[256];
typedef double Histo2D[256][256];

void otsuLUT(int *H, Histo2D result) //H is histogram 0..255
{
    for (int u = 0; u < 256; u++)
        for (int v = 0; v < 256; v++)
            result[u][v] = 0;
    double sum,prob;
    sum = 0;
    for (int v = 0; v < 256; v++)
        sum = sum + H[v];
    if (sum <= 0) return;
    Histo2D P,S;
    P[0][0] = H[0];
    S[0][0] = H[0];
    for (int v = 1; v < 256; v++) {
        prob = H[v]/sum;
        P[0][v] = P[0][v-1]+prob;
        S[0][v] = S[0][v-1]+(v+1)*prob;
    }
    for (int u = 1; u < 256; u++) {
        for (int v = u; v < 256; v++) {
            P[u][v] = P[0][v]-P[0][u-1];
            S[u][v] = S[0][v]-S[0][u-1];
        }
    }

    //result is eq 29 from Liao
    for (int u = 0; u < 256; u++) {
        for (int v = u; v < 256; v++) {
            if (P[u][v] == 0)  //avoid divide by zero errors...
                result[u][v] = 0;
            else
                result[u][v] = pow(S[u][v],2) /P[u][v];
        }
    }
}


void otsuCostFunc4(int *lHisto, int *low, int *middle, int *high)
//call with lHisto &lo &hi
{
    double v,max;
    Histo2D h2d;
    otsuLUT(lHisto, h2d);
    int lo = 12;
    int mid = 128;
    int hi = 192;
    max = h2d[0][lo]+h2d[lo+1][mid]+h2d[mid+1][hi]+h2d[hi+1][255];
    //exhaustively search
    for (int l = 0; l < (255-2); l++) {
        for (int m = l+1; m < (255-1); m++ ) {
            for (int h = m+1; h < (255); h++) {
                v = h2d[0][l]+h2d[l+1][m]+h2d[m+1][h]+h2d[h+1][255];
                if (v > max) {
                    lo = l;
                    mid = m;
                    hi = h;
                    max = v;
                }//new max
            }//for h -> hi
        }//for mid
    }//for l -> low
    *low = lo;
    *middle = mid;
    *high = hi;
}//quad OtsuCostFunc4

int otsuCostFunc2(int *lHisto)
{
    double v,max;
    Histo2D h2d;
    otsuLUT(lHisto, h2d);
    //default solution - 128 is middle intensity of 0..255
    int  n = 128;
    max = h2d[0][n]+h2d[n+1][255];
    int result = n;
    //exhaustively search
    for (n = 0; n < 255; n++) {
        v = h2d[0][n]+h2d[n+1][255];
        if (v > max) {
            result = n;
            max = v;
        }//max
    } //for n
    return result;
} //bilevel otsuCostFunc2

void otsuCostFunc3(int *lHisto, int *low, int *high)
//call with lHisto &lo &hi
{
    double v,max;
    Histo2D h2d;
    otsuLUT(lHisto, h2d);
    int lo = 85;
    int hi = 170;
    //default solution
    max = h2d[0][lo]+h2d[lo+1][hi]+h2d[hi+1][255];
    //exhaustively search
    for (int l = 0; l < (255-1); l++) {
        for (int h = l+1; h < (255); h++ ) {
            v = h2d[0][l]+h2d[l+1][h]+h2d[h+1][255];
            if (v > max) {
                lo = l;
                hi = h;
                max = v;
            } //new max
        }//for h -> hi
    }//for l -> low
    *low = lo;
    *high = hi;
}; //trilevel OtsuCostFunc3

int findOtsu2 (uint8_t *img8bit, int nVox)
{
    int lHisto[256];
    if (nVox < 1) return 128;
    //create histogram
    for (int n = 0; n < 256; n++)
        lHisto[n] = 0;
    for (int n = 0; n < nVox; n++)
        lHisto[img8bit[n]]++;
    //now find minimum intraclass variance....
    return otsuCostFunc2(lHisto);
}

void findOtsu3 (uint8_t *img8bit, int nVox, int* lo, int* hi)
{
    int lHisto[256];
    *lo = 85;
    *hi = 170;
    if (nVox < 1) return;
    //create histogram
    for (int n = 0; n < 256; n++)
        lHisto[n] = 0;
    for (int n = 0; n < nVox; n++)
        lHisto[img8bit[n]]++;
    otsuCostFunc3(lHisto,lo,hi);
}

void findOtsu4 (uint8_t *img8bit, int nVox, int* lo, int* mid, int* hi)
{
    int lHisto[256];
    *lo = 64;
    *mid = 128;
    *hi = 192;
    if (nVox < 1) return;
    //create histogram
    for (int n = 0; n < 256; n++)
        lHisto[n] = 0;
    for (int n = 0; n < nVox; n++)
        lHisto[img8bit[n]]++;
    otsuCostFunc4(lHisto,lo,mid, hi);
}

void applyOtsu2 (uint8_t *img8bit, int nVox)
{
    if (nVox < 1) return;
    int result = findOtsu2(img8bit,nVox);
    for (int n = 0; n < nVox; n++) {
        if (img8bit[n] > result)
            img8bit[n] = 255;
        else
            img8bit[n] = 0;
    }
    return;
}//applyOtsu2

void applyOtsu3 (uint8_t *img8bit, int nVox)
{
    if (nVox < 1) return;
    int lo, hi;
    findOtsu3(img8bit,nVox, &lo, &hi);
    for (int n = 0; n < nVox; n++) {
        if (img8bit[n] <= lo)
            img8bit[n] = 0;
        else if (img8bit[n] >= hi)
            img8bit[n] = 255;
        else
            img8bit[n] = 128;
    }
    return;
}//applyOtsu3

void applyOtsu4 (uint8_t *img8bit, int nVox)
{
    if (nVox < 1) return;
    int lo, mid, hi;
    findOtsu4(img8bit,nVox, &lo, &mid, &hi);
    for (int n = 0; n < nVox; n++) {
        if (img8bit[n] <= lo)
            img8bit[n] = 0;
        else if (img8bit[n] <= mid)
            img8bit[n] = 85;
        else if (img8bit[n] <= hi)
            img8bit[n] = 170;
        else
            img8bit[n] = 255;
    }
    return;
}//applyOtsu4

void applyOtsuBinary (uint8_t *img8bit, int nVox, int levels)
//1=1/4, 2=1/3, 3=1/2, 4=2/3, 5=3/4
{
    if (nVox < 1) return;
    //for (int n = 0; n < nVox; n++)
    //    if (img8bit[n] < 10)
    //        img8bit[n] = 0;
    if ((levels <= 1) || (levels >= 5)) {
        applyOtsu4(img8bit,nVox);
    } else if ((levels = 2) || (levels = 4))
        applyOtsu3(img8bit,nVox);
    else //level = 3
        applyOtsu2(img8bit,nVox);
    int H[256];
    if (levels <= 3) { //make dark: all except 255 equal 0
        for (int n = 0; n < 256; n++)
            H[n] = 0;
        H[255] = 255;
    } else { //make bright: all except 0 equal 255
        for (int n = 0; n < 256; n++)
            H[n] = 255;
        H[0] = 0;
    }
    for (int n = 0; n < nVox; n++)
        img8bit[n] = H[img8bit[n]];
}

void countClusterSize (uint8_t *lImg, int64_t *lClusterBuff, int lXi, int lYi, int lZi,  uint8_t lClusterValue)
{
    if ((lXi < 5) || (lYi < 5) || (lZi < 3)) return;
    int const kUndeterminedSize = -1;
    int const kCurrentSize = -2;
    //int lXY = lXi*lYi; //offset one slice
    int lXYZ =lXi*lYi*lZi;
    int64_t *lQra = (int64_t *) malloc(lXYZ * sizeof(int64_t));
    //initialize clusterbuffer: voxels either non part of clusters (cluster size 0), or part of cluster of undetermined size
    for (int v = 0; v < lXYZ; v++) {
        if (lImg[v] == lClusterValue)
            lClusterBuff[v] = kUndeterminedSize;  //target voxel - will be part of a cluster of size 1..XYZ
        else
            lClusterBuff[v] = 0;//not a target, not part of a cluser, size = 0
    }//for lInc
    int const nSearch = 6;
    int64_t *searchArray = (int64_t *) malloc(nSearch * sizeof(int64_t));
    searchArray[0] = -1; //left
    searchArray[1] = 1; //right
    searchArray[2] = -lXi; //posterior
    searchArray[3] = lXi; //anterior
    searchArray[4] = -lXi*lYi; //inferior
    searchArray[5] = lXi*lYi; //superior
    for (int v = 0; v < lXYZ; v++) {
        if (lClusterBuff[v] == kUndeterminedSize) {
            int lQCurr = 0;
            int lQMax = 0;
            lQra[lQCurr] = v;
            lClusterBuff[v] = kCurrentSize;
            do {
                for (int s = 0; s < nSearch; s++) {//search each neighbor to see if it is part of this cluster
                    int64_t vx = lQra[lQCurr] + searchArray[s];
                    if ((vx >= 0) && (vx < lXYZ) && (lClusterBuff[vx] == kUndeterminedSize) ) {
                        lClusterBuff[vx] = kCurrentSize;
                        lQMax++;
                        lQra[lQMax] = vx;
                    }
                }
                lQCurr++;
            } while (lQMax >= lQCurr);
            //set all voxels in this cluster to reflect their size
            for (int vx = 0; vx < lQCurr; vx++)
                lClusterBuff[ lQra[vx]  ] = lQCurr;
        }//if v part of new cluser
    } //for each voxel
    free(searchArray);
    //    selectClusters (lClusterBuff, -1, lXi, lXYZ, lQSz,  lTopSlice, lXY, lQra);
    free(lQra);
} //CountClusterSize


 //fillBubbles works but is typically not required - increases processing time ~20%
void fillBubbles (uint8_t *lImg, int lXi, int lYi, int lZi)
//output voxels only zero if they are connected to edge voxels by zeros....
{
    if ((lXi < 5) or (lYi < 5) or (lZi < 1)) return;
    int lXYZ = lXi *lYi * lZi;
    int const kUntouchedZero = 10;
    int const nSearch = 6;
    int64_t *searchArray = (int64_t *) malloc(nSearch * sizeof(int64_t));
    int64_t *lQra = (int64_t *) malloc(lXYZ * sizeof(int64_t));
    searchArray[0] = -1; //left
    searchArray[1] = 1; //right
    searchArray[2] = -lXi; //posterior
    searchArray[3] = lXi; //anterior
    searchArray[4] = -lXi*lYi; //inferior
    searchArray[5] = lXi*lYi; //superior
    for (int v = 0; v < lXYZ; v++) {//make binary
        if (lImg[v] != 0)
            lImg[v] = 255;
        else
            lImg[v] = kUntouchedZero; //zero - but not yet sure if connected to edge...
    } //for v each voxel
    for (int z = 1; z <= lZi; z++) {
        int zpos = (z-1) * lXi * lYi;
        for (int y = 1; y <= lYi; y++) {
            int ypos = (y-1) * lXi;
            for (int x = 1; x <= lXi; x++) {
                if ((x==1) || (y==1) || (z==1) || (x==lXi) || (y==lYi) || (z==lZi) ) { //voxel on edge
                    int v = zpos + ypos + x -1;
                    if (lImg[v] == kUntouchedZero) {
                        int lQCurr = 0;
                        int lQMax = 0;
                        lQra[lQCurr] = v;
                        lImg[v] = 0;
                        do {
                            for (int s = 0; s < nSearch; s++) {//search each neighbor to see if it is part of this cluster
                                int64_t vx = lQra[lQCurr] + searchArray[s];
                                if ((vx >= 0) && (vx < lXYZ) && (lImg[vx] == kUntouchedZero) ) {
                                    lImg[vx] = 0;
                                    lQMax++;
                                    lQra[lQMax] = vx;
                                }
                            }
                            lQCurr++;
                        } while (lQMax >= lQCurr);
                    }//untouched zero
                } //on edge
            } //for x
        } //for y
    } //for z
    free(searchArray);
    free(lQra);
    for (int v = 0; v < lXYZ; v++) // untouched voxels not connected to any edge
        if (lImg[v] == kUntouchedZero)
            lImg[v] = 255;
} //fillBubbles - fill holes inside object


void preserveLargestCluster (uint8_t *lImg, int lXi, int lYi, int lZi, int lClusterValue, uint8_t ValueForSmallClusters)
{
    if ((lXi < 5) or (lYi < 5) or (lZi < 1)) return;
    int lXYZ,lX;
    int64_t lC;
    lXYZ =lXi*lYi*lZi;
    //ensure at least some voxels exist with clusterValue
    lC = 0;
    for (lX =0; lX< lXYZ; lX++)
        if (lImg[lX] == lClusterValue)
            lC++;
    if (lC < 2)
        return;//e.g. if lC = 1 then only a single voxel, which is in fact largest cluster
    int64_t *lTemp = (int64_t *) malloc(lXYZ * sizeof(int64_t));
    for (lX = 0; lX < lXYZ; lX++) lTemp[lX] = 0; //the only purpose of this loop is to hide a compiler warning - countClusterSize will fill this array...
    countClusterSize(lImg,lTemp,lXi,lYi,lZi,lClusterValue);
    lC = 0;
    for (lX = 0; lX < lXYZ; lX++)
        if (lTemp[lX] > lC)
            lC = lTemp[lX]; //volume of largest cluster
    if (ValueForSmallClusters == 0) {
        for (lX = 0; lX < lXYZ; lX++)
            if ((lTemp[lX] >= 0) && (lTemp[lX] < lC)) //cluster, but not biggest one...
                lImg[lX] = ValueForSmallClusters;
    } else {
        for (lX = 0; lX < lXYZ; lX++)
            if ((lTemp[lX] > 0) && (lTemp[lX] < lC)) //cluster, but not biggest one...
                lImg[lX] = ValueForSmallClusters;
    }
    free(lTemp);
}

void dilate (uint8_t *lImg, int lXi, int lYi, int lZi, int lCycles, int lChange)
//Dilates Diamonds - neighbor coefficient = 0
//Dilate if Change=1 then all voxels where intensity <> 1 but where any neighbors = 1 will become 1
//Erode  if Change=0 then all voxels where intensity <>0 but where any neighbors = 0 will become 0
//step is repeated  for lCycles
{
    if ((lXi < 5) || (lYi < 5) || (lZi < 1))
        return;
    int lX,lY,lZ, lXY,lXYZ,lPos,lOffset;
    lXY = lXi*lYi; //offset one slice
    lXYZ =lXY*lZi;
    uint8_t *lTemp = (uint8_t *) malloc(lXYZ);
    for (int lC = 0; lC < lCycles; lC++) {
        memcpy (lTemp, lImg,  lXYZ);
        for (lZ = 0; lZ < lZi; lZ++) {
            for (lY = 0; lY < lYi; lY++) {
                lOffset = (lY*lXi) + (lZ * lXY);
                for (lX = 0; lX < lXi; lX++) {
                    lPos = lOffset + lX;
                    if (lTemp[lPos] != lChange) {
                        if ((lX>0) && (lTemp[lPos-1] == lChange))
                            lImg[lPos] = lChange;
                        else if ((lX<(lXi-1)) && (lTemp[lPos+1] == lChange))
                            lImg[lPos] = lChange;
                        else if ((lY>0) && (lTemp[lPos-lXi] == lChange))
                            lImg[lPos] = lChange;
                        else if ((lY<(lYi-1)) && (lTemp[lPos+lXi] == lChange))
                            lImg[lPos] = lChange;
                        else if ((lZ>0) && (lTemp[lPos-lXY] == lChange))
                            lImg[lPos] = lChange;
                        else if ((lZ<(lZi-1)) && (lTemp[lPos+lXY] == lChange))
                            lImg[lPos] = lChange;
                    } //if not lChange
                } //for X
            } //for Y
        } //for Z
    } //for each cycle
    free(lTemp);
} //dilate

void dilateSphere (uint8_t *lImg, int lXi, int lYi, int lZi, float lVoxDistance, int lChange)
//INPUT: Img is array of bytes 0..(X*Y*Z-1) that represents 3D volume, lXi,lYi,lZi are number of voxels in each dimension
//             lVoxDistance is search radius (in voxels)
//            lChange is the intensity to be changed - if background color: erosion, if foreground color: dilation
//OUTPUT: Eroded/Dilated Img
{
    if (lVoxDistance < 1) return;
    if (lVoxDistance == 1) { //much faster to use classic neighbor dilation
        dilate(lImg,lXi,lYi,lZi,1,lChange);
        return;
    }
    if ((lXi < 3) || (lYi < 3) || (lZi < 3))
        return;
    float lDx;
    int lXY = lXi*lYi; //voxels per slice
    int lXYZ = lXi*lYi*lZi; //voxels per volume
    //next: make 1D array of all voxels within search sphere: store offset from center
    int lDxI = trunc(lVoxDistance); //no voxel will be searched further than DxI from center
    int64_t *lSearch = (int64_t *) malloc( ((lDxI *2)+1)*((lDxI *2)+1)*((lDxI *2)+1) *sizeof(int64_t) );
    int lVoxOK = 0;
    for (int lZ = -lDxI; lZ <= lDxI; lZ++) {
        for (int lY = -lDxI; lY <= lDxI; lY++) {
            for (int lX = -lDxI; lX <= lDxI; lX++) {
                lDx = sqrt( (lX*lX)+ (lY*lY)+ (lZ*lZ)  );
                if ((lDx < lVoxDistance) && (lDx > 0)) {
                    lSearch[lVoxOK] = lX + (lY*lXi) + (lZ * lXY); //offset to center
                    lVoxOK++;
                } //in range, not center
            } //lX
        } //lY
    }//lZ
    uint8_t *lTemp = (uint8_t *) malloc(lXYZ);
    memcpy (lTemp, lImg,  lXYZ);
    for (int lVox = 0; lVox < lXYZ; lVox++)
        if (lTemp[lVox] != lChange) { //voxel not a survivor
            for (int s = 0; s < lVoxOK; s++) { //make voxel a survivor if neighbor is a survivor
                int64_t t = lVox +lSearch[s];
                if ((t >= 0) && (t < lXYZ) && (lTemp[t] == lChange)) {
                    lImg[lVox] = lChange;
                    break;
                } //if neighbor
            } //for s: search for neighboring survivrs
        } //if lTemp[lVox] not a survivor
    free(lTemp); //free temporary buffer
    free(lSearch); //free 1D search space
}//proc DilateSphere

void smoothFWHM2Vox (uint8_t *lImg, int lXi, int lYi, int lZi)
{
    if ((lXi < 5) || (lYi < 5)  || (lZi < 1)) return;
    int k0=240;//weight of center voxel
    int k1=120;//weight of nearest neighbors
    int k2=15;//weight of subsequent neighbors
    int kTot=k0+k1+k1+k2+k2; //weight of center plus all neighbors within 2 voxels
    int kWid = 2; //we will look +/- 2 voxels from center
    int lyPos,lPos,lWSum,lXi2,lXY,lXY2;
    lXY = lXi*lYi; //offset one slice
    int lXYZ = lXY * lZi;
    lXY2 = lXY * 2; //offset two slices
    lXi2 = lXi*2;//offset to voxel two lines above or below
    uint8_t *lTemp = (uint8_t *) malloc(lXYZ);
    memcpy (lTemp, lImg,  lXYZ);
    //smooth horizontally
    for (int lZ = 0; lZ < lZi; lZ++) {
        for (int lY = 0; lY < lYi; lY++) {
            lyPos = (lY*lXi) + (lZ*lXY) ;
            for (int lX = kWid; lX < (lXi-kWid); lX++)  {
                lPos = lyPos + lX;
                lWSum = lImg[lPos-2]*k2 +lImg[lPos-1]*k1 +lImg[lPos]*k0 +lImg[lPos+1]*k1 +lImg[lPos+2]*k2;
                lTemp[lPos] = lWSum / kTot;
            } //for lX
        } //for lY
    } //for lZi
    //smooth vertically
    memcpy (lImg,lTemp,  lXYZ);
    for (int lZ = 0; lZ < lZi; lZ++) {
        for (int lX = 0; lX < lXi; lX++) {
            for (int lY = kWid; lY < (lYi-kWid); lY++) {
                lPos = ((lY*lXi) + lX + (lZ*lXY) ) ;
                lWSum = lTemp[lPos-lXi2]*k2+lTemp[lPos-lXi]*k1+lTemp[lPos]*k0+lTemp[lPos+lXi]*k1+lTemp[lPos+lXi2]*k2;
                lImg[lPos] = lWSum / kTot;
            }//for Y
        } //for X
    } //for Z
    //if 3rd dimension....
    if (lZi >= 5) {
        //smooth across slices
        memcpy (lTemp, lImg, lXYZ);
        for (int lZ = kWid; lZ < (lZi-kWid); lZ++) {
            for (int lY = 0; lY < lYi; lY++) {
                lyPos = (lY*lXi) + (lZ*lXY) ;
                for (int lX = 0; lX < lXi; lX++)  {
                    lPos = lyPos + lX;
                    lWSum = lImg[lPos-lXY2]*k2+lImg[lPos-lXY]*k1+lImg[lPos]*k0+lImg[lPos+lXY]*k1+lImg[lPos+lXY2]*k2;
                    lTemp[lPos] = lWSum / kTot;
                }//for lX
            }//for lY
        } //for lZi
        memcpy ( lImg, lTemp, lXYZ);
    }//if Z: at least 5 slices...
    free(lTemp);
} //smoothFWHM2Vox

void maskBackground  (uint8_t *img8bit, int lXi, int lYi, int lZi, int lOtsuLevels, float lDilateVox, bool lOneContiguousObject)
{
    if ((lXi < 3) or (lYi < 3) or (lZi < 1)) return;
    int lXYZ = lXi * lYi * lZi;
    //countSurvivors(img8bit, lXYZ);
    smoothFWHM2Vox(img8bit, lXi,lYi,lZi);
    applyOtsuBinary (img8bit, lXYZ,lOtsuLevels);
    //clip edges to prevent wrap
    int lV=0;
    for (int lZ = 1; lZ <= lZi; lZ++)
        for (int lY = 1; lY <= lYi; lY++)
            for (int lX = 1; lX <= lXi; lX++) {
                if ((lX==1) || (lX==lXi) || (lY==1) || (lY==lYi) || (lZ==1) || (lZ==lZi))
                    img8bit[lV] = 0;
                lV++;
            } //for lX
    if (lOneContiguousObject) {
        preserveLargestCluster (img8bit, lXi,lYi,lZi,255,0 ); //only preserve largest single object
        if (lDilateVox > 0)
            dilateSphere (img8bit, lXi,lYi,lZi,lDilateVox,255 );
    } else {
        if (lDilateVox > 0)
            dilateSphere (img8bit, lXi,lYi,lZi,lDilateVox,255 );
        preserveLargestCluster (img8bit, lXi,lYi,lZi,0,255 ); //only erase outside air
    }
    fillBubbles(img8bit, lXi,lYi,lZi); //<- optional
    //my sense is filling air bubbles will help SPM's mixture of gaussians detect air. Without this air will have an artificially low variance
}

int compare_short (const void *a, const void *b)
{
  const short *da = (const short *) a;
  const short *db = (const short *) b;
  return (*da > *db) - (*da < *db);
}


void maskBackground16  (short *img16bit, int lXi, int lYi, int lZi, int lOtsuLevels, float lDilateVox, bool lOneContiguousObject) {
    if ((lXi < 3) or (lYi < 3) or (lZi < 1)) return;
    int lXYZ = lXi * lYi * lZi;
    if (lXYZ < 100) return;
    //find image intensity range
    short * imgSort = (short *) malloc(sizeof(short)*lXYZ);
	memcpy(&imgSort[0], &img16bit[0], lXYZ * sizeof(short)); //memcpy( dest, src, bytes)
    qsort (imgSort, lXYZ, sizeof (short), compare_short);
    int pctLo = imgSort[(int)((float)lXYZ * 0.02)];
    int pctHi = imgSort[(int)((float)lXYZ * 0.98)];
	free(imgSort);
	if (pctLo >= pctHi) return; //no variability
	//re-scale data 0..255
	float scale = 255.0/(float)(pctHi - pctLo);
	uint8_t *img8bit = (uint8_t *) malloc(sizeof(uint8_t)*lXYZ);
	for (int i = 0; i < lXYZ; i++) {
		if (img16bit[i] >= pctHi)
			img8bit[i] = 255;
		else if (img16bit[i] <= pctLo)
			img8bit[i] = 0;
		else
			img8bit[i] = (int)((img16bit[i] - pctLo) * scale);
	}
	//mask 8-bit image
	maskBackground  (img8bit, lXi, lYi, lZi, lOtsuLevels, lDilateVox, lOneContiguousObject);
	for (int i = 0; i < lXYZ; i++)
		if (img8bit[i] == 0)
			img16bit[i] = 0;
}

void maskBackgroundU16  (unsigned short *img16bit, int lXi, int lYi, int lZi, int lOtsuLevels, float lDilateVox, bool lOneContiguousObject) {
    if ((lXi < 3) or (lYi < 3) or (lZi < 1)) return;
    int lXYZ = lXi * lYi * lZi;
    if (lXYZ < 100) return;
    //find image intensity range
    unsigned short * imgSort = (unsigned short *) malloc(sizeof(unsigned short)*lXYZ);
	memcpy(&imgSort[0], &img16bit[0], lXYZ * sizeof(short)); //memcpy( dest, src, bytes)
    qsort (imgSort, lXYZ, sizeof (short), compare_short);
    int pctLo = imgSort[(int)((float)lXYZ * 0.02)];
    int pctHi = imgSort[(int)((float)lXYZ * 0.98)];
	free(imgSort);
	if (pctLo >= pctHi) return; //no variability
	//re-scale data 0..255
	float scale = 255.0/(float)(pctHi - pctLo);
	uint8_t *img8bit = (uint8_t *) malloc(sizeof(uint8_t)*lXYZ);
	for (int i = 0; i < lXYZ; i++) {
		if (img16bit[i] >= pctHi)
			img8bit[i] = 255;
		else if (img16bit[i] <= pctLo)
			img8bit[i] = 0;
		else
			img8bit[i] = (int)((img16bit[i] - pctLo) * scale);
	}
	//mask 8-bit image
	maskBackground  (img8bit, lXi, lYi, lZi, lOtsuLevels, lDilateVox, lOneContiguousObject);
	for (int i = 0; i < lXYZ; i++)
		if (img8bit[i] == 0)
			img16bit[i] = 0;
}




