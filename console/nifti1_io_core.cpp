//This unit uses a subset of the functions from the nifti1_io available from
//  https://sourceforge.net/projects/niftilib/files/nifticlib/
//These functions were extended by Chris Rorden (2014) and maintain the same license
/*****===================================================================*****/
/*****     Sample functions to deal with NIFTI-1 and ANALYZE files       *****/
/*****...................................................................*****/
/*****            This code is released to the public domain.            *****/
/*****...................................................................*****/
/*****  Author: Robert W Cox, SSCC/DIRP/NIMH/NIH/DHHS/USA/EARTH          *****/
/*****  Date:   August 2003                                              *****/
/*****...................................................................*****/
/*****  Neither the National Institutes of Health (NIH), nor any of its  *****/
/*****  employees imply any warranty of usefulness of this software for  *****/
/*****  any purpose, and do not assume any liability for damages,        *****/
/*****  incidental or otherwise, caused by any use of this document.     *****/
/*****===================================================================*****/

#include <stdbool.h> //requires VS 2015 or later
#include "nifti1_io_core.h"
#include "nifti1.h"
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>
#include <stddef.h>
#include <float.h>
#include <stdio.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "print.h"


#ifndef USING_R
void nifti_swap_8bytes( size_t n , void *ar )    // 4 bytes at a time
{
    size_t ii ;
    unsigned char * cp0 = (unsigned char *)ar, * cp1, * cp2 ;
    unsigned char tval ;
    for( ii=0 ; ii < n ; ii++ ){
        cp1 = cp0; cp2 = cp0+7;
        tval = *cp1;  *cp1 = *cp2;  *cp2 = tval;
        cp1++;  cp2--;
        tval = *cp1;  *cp1 = *cp2;  *cp2 = tval;
        cp1++;  cp2--;
        tval = *cp1;  *cp1 = *cp2;  *cp2 = tval;
        cp1++;  cp2--;
        tval = *cp1;  *cp1 = *cp2;  *cp2 = tval;
        cp0 += 8;
    }
    return ;
}

void nifti_swap_4bytes( size_t n , void *ar )    // 4 bytes at a time
{
    size_t ii ;
    unsigned char * cp0 = (unsigned char *)ar, * cp1, * cp2 ;
    unsigned char tval ;
    for( ii=0 ; ii < n ; ii++ ){
        cp1 = cp0; cp2 = cp0+3;
        tval = *cp1;  *cp1 = *cp2;  *cp2 = tval;
        cp1++;  cp2--;
        tval = *cp1;  *cp1 = *cp2;  *cp2 = tval;
        cp0 += 4;
    }
    return ;
}

void nifti_swap_2bytes( size_t n , void *ar )    // 2 bytes at a time
{
    size_t ii ;
    unsigned char * cp1 = (unsigned char *)ar, * cp2 ;
    unsigned char   tval;
    for( ii=0 ; ii < n ; ii++ ){
        cp2 = cp1 + 1;
        tval = *cp1;  *cp1 = *cp2;  *cp2 = tval;
        cp1 += 2;
    }
    return ;
}
#endif

/*-------------------------------------------------------------------------*/
/*! Byte swap NIFTI-1 file header in various places and ways.

    If is_nifti, swap all (even UNUSED) fields of NIfTI header.
    Else, swap as a nifti_analyze75 struct.
*//*---------------------------------------------------------------------- */
void swap_nifti_header( struct nifti_1_header *h  )
{

   /* otherwise, swap all NIFTI fields */

   nifti_swap_4bytes(1, &h->sizeof_hdr);
   nifti_swap_4bytes(1, &h->extents);
   nifti_swap_2bytes(1, &h->session_error);

   nifti_swap_2bytes(8, h->dim);
   nifti_swap_4bytes(1, &h->intent_p1);
   nifti_swap_4bytes(1, &h->intent_p2);
   nifti_swap_4bytes(1, &h->intent_p3);

   nifti_swap_2bytes(1, &h->intent_code);
   nifti_swap_2bytes(1, &h->datatype);
   nifti_swap_2bytes(1, &h->bitpix);
   nifti_swap_2bytes(1, &h->slice_start);

   nifti_swap_4bytes(8, h->pixdim);

   nifti_swap_4bytes(1, &h->vox_offset);
   nifti_swap_4bytes(1, &h->scl_slope);
   nifti_swap_4bytes(1, &h->scl_inter);
   nifti_swap_2bytes(1, &h->slice_end);

   nifti_swap_4bytes(1, &h->cal_max);
   nifti_swap_4bytes(1, &h->cal_min);
   nifti_swap_4bytes(1, &h->slice_duration);
   nifti_swap_4bytes(1, &h->toffset);
   nifti_swap_4bytes(1, &h->glmax);
   nifti_swap_4bytes(1, &h->glmin);

   nifti_swap_2bytes(1, &h->qform_code);
   nifti_swap_2bytes(1, &h->sform_code);

   nifti_swap_4bytes(1, &h->quatern_b);
   nifti_swap_4bytes(1, &h->quatern_c);
   nifti_swap_4bytes(1, &h->quatern_d);
   nifti_swap_4bytes(1, &h->qoffset_x);
   nifti_swap_4bytes(1, &h->qoffset_y);
   nifti_swap_4bytes(1, &h->qoffset_z);

   nifti_swap_4bytes(4, h->srow_x);
   nifti_swap_4bytes(4, h->srow_y);
   nifti_swap_4bytes(4, h->srow_z);

   return ;
}

bool littleEndianPlatform ()
{
    uint32_t value = 1;
    return (*((char *) &value) == 1);
}

int isSameFloat (float a, float b) {
    return (fabs (a - b) <= FLT_EPSILON);
}

int isSameDouble (double a, double b) {
    return (fabs (a - b) <= DBL_EPSILON);
}

ivec3 setiVec3(int x, int y, int z)
{
    ivec3 v = {{x, y, z}};
    return v;
}

vec3 setVec3(float x, float y, float z)
{
    vec3 v = {{x, y, z}};
    return v;
}

vec4 setVec4(float x, float y, float z)
{
    vec4 v= {{x, y, z, 1}};
    return v;
}

vec3 crossProduct(vec3 u, vec3 v)
{
    return setVec3(u.v[1]*v.v[2] - v.v[1]*u.v[2],
                   -u.v[0]*v.v[2] + v.v[0]*u.v[2],
                   u.v[0]*v.v[1] - v.v[0]*u.v[1]);
}

float dotProduct(vec3 u, vec3 v)
{
    return (u.v[0]*v.v[0] + v.v[1]*u.v[1] + v.v[2]*u.v[2]);
}

vec3 nifti_vect33_norm (vec3 v) { //normalize vector length
    vec3 vO = v;
    float vLen = sqrt( (v.v[0]*v.v[0])
                      + (v.v[1]*v.v[1])
                      + (v.v[2]*v.v[2]));
    if (vLen <= FLT_EPSILON) return vO; //avoid divide by zero
    for (int i = 0; i < 3; i++)
        vO.v[i] = v.v[i]/vLen;
    return vO;
}

vec3 nifti_vect33mat33_mul(vec3 v, mat33 m ) { //multiply vector * 3x3matrix
    vec3 vO;
    for (int i=0; i<3; i++) { //multiply Pcrs * m
        vO.v[i] = 0;
        for(int j=0; j<3; j++)
            vO.v[i] += m.m[i][j]*v.v[j];
    }
    return vO;
}

vec4 nifti_vect44mat44_mul(vec4 v, mat44 m ) { //multiply vector * 4x4matrix
    vec4 vO;
    for (int i=0; i<4; i++) { //multiply Pcrs * m
        vO.v[i] = 0;
        for(int j=0; j<4; j++)
            vO.v[i] += m.m[i][j]*v.v[j];
    }
    return vO;
}

mat44 nifti_dicom2mat(float orient[7], float patientPosition[4], float xyzMM[4]) {
    //create NIfTI header based on values from DICOM header
    //note orient has 6 values, indexed from 1, patient position and xyzMM have 3 values indexed from 1
    mat33 Q, diagVox;
    Q.m[0][0] = orient[1]; Q.m[0][1] = orient[2] ; Q.m[0][2] = orient[3] ; // load Q
    Q.m[1][0] = orient[4]; Q.m[1][1] = orient[5] ; Q.m[1][2] = orient[6];
    //printMessage("Orient %g %g %g %g %g %g\n",orient[1],orient[2],orient[3],orient[4],orient[5],orient[6] );
    /* normalize row 1 */
    double val = Q.m[0][0]*Q.m[0][0] + Q.m[0][1]*Q.m[0][1] + Q.m[0][2]*Q.m[0][2] ;
    if( val > 0.0l ){
        val = 1.0l / sqrt(val) ;
        Q.m[0][0] *= (float)val ; Q.m[0][1] *= (float)val ; Q.m[0][2] *= (float)val ;
    } else {
        Q.m[0][0] = 1.0l ; Q.m[0][1] = 0.0l ; Q.m[0][2] = 0.0l ;
    }
    /* normalize row 2 */
    val = Q.m[1][0]*Q.m[1][0] + Q.m[1][1]*Q.m[1][1] + Q.m[1][2]*Q.m[1][2] ;
    if( val > 0.0l ){
        val = 1.0l / sqrt(val) ;
        Q.m[1][0] *= (float)val ; Q.m[1][1] *= (float)val ; Q.m[1][2] *= (float)val ;
    } else {
        Q.m[1][0] = 0.0l ; Q.m[1][1] = 1.0l ; Q.m[1][2] = 0.0l ;
    }
    /* row 3 is the cross product of rows 1 and 2*/
    Q.m[2][0] = Q.m[0][1]*Q.m[1][2] - Q.m[0][2]*Q.m[1][1] ;  /* cross */
    Q.m[2][1] = Q.m[0][2]*Q.m[1][0] - Q.m[0][0]*Q.m[1][2] ;  /* product */
    Q.m[2][2] = Q.m[0][0]*Q.m[1][1] - Q.m[0][1]*Q.m[1][0] ;
    Q = nifti_mat33_transpose(Q);
    if (nifti_mat33_determ(Q) < 0.0) {
        Q.m[0][2] = -Q.m[0][2];
        Q.m[1][2] = -Q.m[1][2];
        Q.m[2][2] = -Q.m[2][2];
    }
    //next scale matrix
    LOAD_MAT33(diagVox, xyzMM[1],0.0l,0.0l, 0.0l,xyzMM[2],0.0l, 0.0l,0.0l, xyzMM[3]);
    Q = nifti_mat33_mul(Q,diagVox);
    mat44 Q44; //4x4 matrix includes translations
    LOAD_MAT44(Q44, Q.m[0][0],Q.m[0][1],Q.m[0][2],patientPosition[1],
               Q.m[1][0],Q.m[1][1],Q.m[1][2],patientPosition[2],
               Q.m[2][0],Q.m[2][1],Q.m[2][2],patientPosition[3]);
    return Q44;
}

#ifndef USING_R
float nifti_mat33_determ( mat33 R )   /* determinant of 3x3 matrix */
{
    double r11,r12,r13,r21,r22,r23,r31,r32,r33 ;
    /*  INPUT MATRIX:  */
    r11 = R.m[0][0]; r12 = R.m[0][1]; r13 = R.m[0][2];  /* [ r11 r12 r13 ] */
    r21 = R.m[1][0]; r22 = R.m[1][1]; r23 = R.m[1][2];  /* [ r21 r22 r23 ] */
    r31 = R.m[2][0]; r32 = R.m[2][1]; r33 = R.m[2][2];  /* [ r31 r32 r33 ] */
    return (float)(r11*r22*r33-r11*r32*r23-r21*r12*r33
                   +r21*r32*r13+r31*r12*r23-r31*r22*r13) ;
}

mat33 nifti_mat33_mul( mat33 A , mat33 B )  /* multiply 2 3x3 matrices */
//see http://nifti.nimh.nih.gov/pub/dist/src/niftilib/nifti1_io.c
{
    mat33 C ; int i,j ;
    for( i=0 ; i < 3 ; i++ )
        for( j=0 ; j < 3 ; j++ )
            C.m[i][j] =  A.m[i][0] * B.m[0][j]
            + A.m[i][1] * B.m[1][j]
            + A.m[i][2] * B.m[2][j] ;
    return C ;
}
#endif

mat44 nifti_mat44_mul( mat44 A , mat44 B )  /* multiply 2 3x3 matrices */
{
    mat44 C ; int i,j ;
    for( i=0 ; i < 4 ; i++ )
        for( j=0 ; j < 4; j++ )
            C.m[i][j] =  A.m[i][0] * B.m[0][j]
            + A.m[i][1] * B.m[1][j]
            + A.m[i][2] * B.m[2][j]
            + A.m[i][3] * B.m[3][j];
    return C ;
}

mat33 nifti_mat33_transpose( mat33 A )  /* transpose 3x3 matrix */
//see http://nifti.nimh.nih.gov/pub/dist/src/niftilib/nifti1_io.c
{
    mat33 B; int i,j ;
    for( i=0 ; i < 3 ; i++ )
        for( j=0 ; j < 3 ; j++ )
            B.m[i][j] =  A.m[j][i];
    return B;
}

#ifndef USING_R
mat33 nifti_mat33_inverse( mat33 R )   /* inverse of 3x3 matrix */
{
    double r11,r12,r13,r21,r22,r23,r31,r32,r33 , deti ;
    mat33 Q ;
    //  INPUT MATRIX:
    r11 = R.m[0][0]; r12 = R.m[0][1]; r13 = R.m[0][2];  // [ r11 r12 r13 ]
    r21 = R.m[1][0]; r22 = R.m[1][1]; r23 = R.m[1][2];  // [ r21 r22 r23 ]
    r31 = R.m[2][0]; r32 = R.m[2][1]; r33 = R.m[2][2];  // [ r31 r32 r33 ]
    deti = r11*r22*r33-r11*r32*r23-r21*r12*r33
    +r21*r32*r13+r31*r12*r23-r31*r22*r13 ;
    if( deti != 0.0l ) deti = 1.0l / deti ;
    Q.m[0][0] = deti*( r22*r33-r32*r23) ;
    Q.m[0][1] = deti*(-r12*r33+r32*r13) ;
    Q.m[0][2] = deti*( r12*r23-r22*r13) ;
    Q.m[1][0] = deti*(-r21*r33+r31*r23) ;
    Q.m[1][1] = deti*( r11*r33-r31*r13) ;
    Q.m[1][2] = deti*(-r11*r23+r21*r13) ;
    Q.m[2][0] = deti*( r21*r32-r31*r22) ;
    Q.m[2][1] = deti*(-r11*r32+r31*r12) ;
    Q.m[2][2] = deti*( r11*r22-r21*r12) ;
    return Q ;
}

float nifti_mat33_rownorm( mat33 A )  // max row norm of 3x3 matrix
{
    float r1,r2,r3 ;
    r1 = fabs(A.m[0][0])+fabs(A.m[0][1])+fabs(A.m[0][2]) ;
    r2 = fabs(A.m[1][0])+fabs(A.m[1][1])+fabs(A.m[1][2]) ;
    r3 = fabs(A.m[2][0])+fabs(A.m[2][1])+fabs(A.m[2][2]) ;
    if( r1 < r2 ) r1 = r2 ;
    if( r1 < r3 ) r1 = r3 ;
    return r1 ;
}

float nifti_mat33_colnorm( mat33 A )  // max column norm of 3x3 matrix
{
    float r1,r2,r3 ;
    r1 = fabs(A.m[0][0])+fabs(A.m[1][0])+fabs(A.m[2][0]) ;
    r2 = fabs(A.m[0][1])+fabs(A.m[1][1])+fabs(A.m[2][1]) ;
    r3 = fabs(A.m[0][2])+fabs(A.m[1][2])+fabs(A.m[2][2]) ;
    if( r1 < r2 ) r1 = r2 ;
    if( r1 < r3 ) r1 = r3 ;
    return r1 ;
}

mat33 nifti_mat33_polar( mat33 A )
{
    mat33 X , Y , Z ;
    float alp,bet,gam,gmi , dif=1.0 ;
    int k=0 ;
    X = A ;
    // force matrix to be nonsingular
    gam = nifti_mat33_determ(X) ;
    while( gam == 0.0 ){        // perturb matrix
        gam = 0.00001 * ( 0.001 + nifti_mat33_rownorm(X) ) ;
        X.m[0][0] += gam ; X.m[1][1] += gam ; X.m[2][2] += gam ;
        gam = nifti_mat33_determ(X) ;
    }
    while(1){
        Y = nifti_mat33_inverse(X) ;
        if( dif > 0.3 ){     // far from convergence
            alp = sqrt( nifti_mat33_rownorm(X) * nifti_mat33_colnorm(X) ) ;
            bet = sqrt( nifti_mat33_rownorm(Y) * nifti_mat33_colnorm(Y) ) ;
            gam = sqrt( bet / alp ) ;
            gmi = 1.0 / gam ;
        } else
            gam = gmi = 1.0 ;  // close to convergence
        Z.m[0][0] = 0.5 * ( gam*X.m[0][0] + gmi*Y.m[0][0] ) ;
        Z.m[0][1] = 0.5 * ( gam*X.m[0][1] + gmi*Y.m[1][0] ) ;
        Z.m[0][2] = 0.5 * ( gam*X.m[0][2] + gmi*Y.m[2][0] ) ;
        Z.m[1][0] = 0.5 * ( gam*X.m[1][0] + gmi*Y.m[0][1] ) ;
        Z.m[1][1] = 0.5 * ( gam*X.m[1][1] + gmi*Y.m[1][1] ) ;
        Z.m[1][2] = 0.5 * ( gam*X.m[1][2] + gmi*Y.m[2][1] ) ;
        Z.m[2][0] = 0.5 * ( gam*X.m[2][0] + gmi*Y.m[0][2] ) ;
        Z.m[2][1] = 0.5 * ( gam*X.m[2][1] + gmi*Y.m[1][2] ) ;
        Z.m[2][2] = 0.5 * ( gam*X.m[2][2] + gmi*Y.m[2][2] ) ;
        dif = fabs(Z.m[0][0]-X.m[0][0])+fabs(Z.m[0][1]-X.m[0][1])
        +fabs(Z.m[0][2]-X.m[0][2])+fabs(Z.m[1][0]-X.m[1][0])
        +fabs(Z.m[1][1]-X.m[1][1])+fabs(Z.m[1][2]-X.m[1][2])
        +fabs(Z.m[2][0]-X.m[2][0])+fabs(Z.m[2][1]-X.m[2][1])
        +fabs(Z.m[2][2]-X.m[2][2])                          ;
        k = k+1 ;
        if( k > 100 || dif < 3.e-6 ) break ;  // convergence or exhaustion
        X = Z ;
    }
    return Z ;
}

void nifti_mat44_to_quatern( mat44 R ,
                            float *qb, float *qc, float *qd,
                            float *qx, float *qy, float *qz,
                            float *dx, float *dy, float *dz, float *qfac )
{
    double r11,r12,r13 , r21,r22,r23 , r31,r32,r33 ;
    double xd,yd,zd , a,b,c,d ;
    mat33 P,Q ;
    // offset outputs are read write out of input matrix
    ASSIF(qx,R.m[0][3]) ; ASSIF(qy,R.m[1][3]) ; ASSIF(qz,R.m[2][3]) ;
    // load 3x3 matrix into local variables */
    r11 = R.m[0][0] ; r12 = R.m[0][1] ; r13 = R.m[0][2] ;
    r21 = R.m[1][0] ; r22 = R.m[1][1] ; r23 = R.m[1][2] ;
    r31 = R.m[2][0] ; r32 = R.m[2][1] ; r33 = R.m[2][2] ;
    // compute lengths of each column; these determine grid spacings
    xd = sqrt( r11*r11 + r21*r21 + r31*r31 ) ;
    yd = sqrt( r12*r12 + r22*r22 + r32*r32 ) ;
    zd = sqrt( r13*r13 + r23*r23 + r33*r33 ) ;
    // if a column length is zero, patch the trouble
    if( xd == 0.0l ){ r11 = 1.0l ; r21 = r31 = 0.0l ; xd = 1.0l ; }
    if( yd == 0.0l ){ r22 = 1.0l ; r12 = r32 = 0.0l ; yd = 1.0l ; }
    if( zd == 0.0l ){ r33 = 1.0l ; r13 = r23 = 0.0l ; zd = 1.0l ; }
    // assign the output lengths */
    ASSIF(dx,xd) ; ASSIF(dy,yd) ; ASSIF(dz,zd) ;
    // normalize the columns */
    r11 /= xd ; r21 /= xd ; r31 /= xd ;
    r12 /= yd ; r22 /= yd ; r32 /= yd ;
    r13 /= zd ; r23 /= zd ; r33 /= zd ;
    /* At this point, the matrix has normal columns, but we have to allow
     for the fact that the hideous user may not have given us a matrix
     with orthogonal columns.
     So, now find the orthogonal matrix closest to the current matrix.
     One reason for using the polar decomposition to get this
     orthogonal matrix, rather than just directly orthogonalizing
     the columns, is so that inputting the inverse matrix to R
     will result in the inverse orthogonal matrix at this point.
     If we just orthogonalized the columns, this wouldn't necessarily hold. */
    Q.m[0][0] = r11 ; Q.m[0][1] = r12 ; Q.m[0][2] = r13 ; // load Q
    Q.m[1][0] = r21 ; Q.m[1][1] = r22 ; Q.m[1][2] = r23 ;
    Q.m[2][0] = r31 ; Q.m[2][1] = r32 ; Q.m[2][2] = r33 ;
    P = nifti_mat33_polar(Q) ;  // P is orthog matrix closest to Q
    r11 = P.m[0][0] ; r12 = P.m[0][1] ; r13 = P.m[0][2] ; // unload
    r21 = P.m[1][0] ; r22 = P.m[1][1] ; r23 = P.m[1][2] ;
    r31 = P.m[2][0] ; r32 = P.m[2][1] ; r33 = P.m[2][2] ;
    //                            [ r11 r12 r13 ]
    // at this point, the matrix  [ r21 r22 r23 ] is orthogonal
    //                            [ r31 r32 r33 ]
    // compute the determinant to determine if it is proper
    zd = r11*r22*r33-r11*r32*r23-r21*r12*r33
    +r21*r32*r13+r31*r12*r23-r31*r22*r13 ;  // should be -1 or 1
    if( zd > 0 ){             // proper
        ASSIF(qfac,1.0) ;
    } else {                  // improper ==> flip 3rd column
        ASSIF(qfac,-1.0) ;
        r13 = -r13 ; r23 = -r23 ; r33 = -r33 ;
    }
    // now, compute quaternion parameters
    a = r11 + r22 + r33 + 1.0l ;
    if( a > 0.5l ){                // simplest case
        a = 0.5l * sqrt(a) ;
        b = 0.25l * (r32-r23) / a ;
        c = 0.25l * (r13-r31) / a ;
        d = 0.25l * (r21-r12) / a ;
    } else {                       // trickier case
        xd = 1.0 + r11 - (r22+r33) ;  // 4*b*b
        yd = 1.0 + r22 - (r11+r33) ;  // 4*c*c
        zd = 1.0 + r33 - (r11+r22) ;  // 4*d*d
        if( xd > 1.0 ){
            b = 0.5l * sqrt(xd) ;
            c = 0.25l* (r12+r21) / b ;
            d = 0.25l* (r13+r31) / b ;
            a = 0.25l* (r32-r23) / b ;
        } else if( yd > 1.0 ){
            c = 0.5l * sqrt(yd) ;
            b = 0.25l* (r12+r21) / c ;
            d = 0.25l* (r23+r32) / c ;
            a = 0.25l* (r13-r31) / c ;
        } else {
            d = 0.5l * sqrt(zd) ;
            b = 0.25l* (r13+r31) / d ;
            c = 0.25l* (r23+r32) / d ;
            a = 0.25l* (r21-r12) / d ;
        }
        //        if( a < 0.0l ){ b=-b ; c=-c ; d=-d; a=-a; }
        if( a < 0.0l ){ b=-b ; c=-c ; d=-d; } //a discarded...
    }
    ASSIF(qb,b) ; ASSIF(qc,c) ; ASSIF(qd,d) ;
    return ;
}

mat44 nifti_quatern_to_mat44( float qb, float qc, float qd,
                             float qx, float qy, float qz,
                             float dx, float dy, float dz, float qfac )
{
    mat44 R ;
    double a,b=qb,c=qc,d=qd , xd,yd,zd ;

    /* last row is always [ 0 0 0 1 ] */

    R.m[3][0]=R.m[3][1]=R.m[3][2] = 0.0f ; R.m[3][3]= 1.0f ;

    /* compute a parameter from b,c,d */

    a = 1.0l - (b*b + c*c + d*d) ;
    if( a < 1.e-7l ){                   /* special case */
        a = 1.0l / sqrt(b*b+c*c+d*d) ;
        b *= a ; c *= a ; d *= a ;        /* normalize (b,c,d) vector */
        a = 0.0l ;                        /* a = 0 ==> 180 degree rotation */
    } else{
        a = sqrt(a) ;                     /* angle = 2*arccos(a) */
    }

    /* load rotation matrix, including scaling factors for voxel sizes */

    xd = (dx > 0.0) ? dx : 1.0l ;       /* make sure are positive */
    yd = (dy > 0.0) ? dy : 1.0l ;
    zd = (dz > 0.0) ? dz : 1.0l ;

    if( qfac < 0.0 ) zd = -zd ;         /* left handedness? */

    R.m[0][0] = (float)( (a*a+b*b-c*c-d*d) * xd) ;
    R.m[0][1] = 2.0l * (b*c-a*d        ) * yd ;
    R.m[0][2] = 2.0l * (b*d+a*c        ) * zd ;
    R.m[1][0] = 2.0l * (b*c+a*d        ) * xd ;
    R.m[1][1] = (float)( (a*a+c*c-b*b-d*d) * yd) ;
    R.m[1][2] = 2.0l * (c*d-a*b        ) * zd ;
    R.m[2][0] = 2.0l * (b*d-a*c        ) * xd ;
    R.m[2][1] = 2.0l * (c*d+a*b        ) * yd ;
    R.m[2][2] = (float)( (a*a+d*d-c*c-b*b) * zd) ;

    /* load offsets */

    R.m[0][3] = qx ; R.m[1][3] = qy ; R.m[2][3] = qz ;

    return R ;
}

mat44 nifti_mat44_inverse( mat44 R )
{
    double r11,r12,r13,r21,r22,r23,r31,r32,r33,v1,v2,v3 , deti ;
    mat44 Q ;
    /*  INPUT MATRIX IS:  */
    r11 = R.m[0][0]; r12 = R.m[0][1]; r13 = R.m[0][2];  // [ r11 r12 r13 v1 ]
    r21 = R.m[1][0]; r22 = R.m[1][1]; r23 = R.m[1][2];  // [ r21 r22 r23 v2 ]
    r31 = R.m[2][0]; r32 = R.m[2][1]; r33 = R.m[2][2];  // [ r31 r32 r33 v3 ]
    v1  = R.m[0][3]; v2  = R.m[1][3]; v3  = R.m[2][3];  // [  0   0   0   1 ]
    deti = r11*r22*r33-r11*r32*r23-r21*r12*r33
    +r21*r32*r13+r31*r12*r23-r31*r22*r13 ;
    if( deti != 0.0l ) deti = 1.0l / deti ;
    Q.m[0][0] = deti*( r22*r33-r32*r23) ;
    Q.m[0][1] = deti*(-r12*r33+r32*r13) ;
    Q.m[0][2] = deti*( r12*r23-r22*r13) ;
    Q.m[0][3] = deti*(-r12*r23*v3+r12*v2*r33+r22*r13*v3
                      -r22*v1*r33-r32*r13*v2+r32*v1*r23) ;
    Q.m[1][0] = deti*(-r21*r33+r31*r23) ;
    Q.m[1][1] = deti*( r11*r33-r31*r13) ;
    Q.m[1][2] = deti*(-r11*r23+r21*r13) ;
    Q.m[1][3] = deti*( r11*r23*v3-r11*v2*r33-r21*r13*v3
                      +r21*v1*r33+r31*r13*v2-r31*v1*r23) ;
    Q.m[2][0] = deti*( r21*r32-r31*r22) ;
    Q.m[2][1] = deti*(-r11*r32+r31*r12) ;
    Q.m[2][2] = deti*( r11*r22-r21*r12) ;
    Q.m[2][3] = deti*(-r11*r22*v3+r11*r32*v2+r21*r12*v3
                      -r21*r32*v1-r31*r12*v2+r31*r22*v1) ;
    Q.m[3][0] = Q.m[3][1] = Q.m[3][2] = 0.0l ;
    Q.m[3][3] = (deti == 0.0l) ? 0.0l : 1.0l ; // failure flag if deti == 0
    return Q ;
}
#endif

// Eigen decomposition for symmetric 3x3 matrices, port of public domain Java Matrix library JAMA.
// Connelly Barnes http://barnesc.blogspot.com/2007/02/eigenvectors-of-3x3-symmetric-matrix.html
// see also https://www.geometrictools.com/Documentation/RobustEigenSymmetric3x3.pdf
//begin: Connelly Barnes code
#include <math.h>

#ifdef MAX
#undef MAX
#endif

#define MAX(a, b) ((a)>(b)?(a):(b))

#define n 3

static double hypot2(double x, double y) {
  return sqrt(x*x+y*y);
}

// Symmetric Householder reduction to tridiagonal form.

static void tred2(double V[n][n], double d[n], double e[n]) {
//  This is derived from the Algol procedures tred2 by
//  Bowdler, Martin, Reinsch, and Wilkinson, Handbook for
//  Auto. Comp., Vol.ii-Linear Algebra, and the corresponding
//  Fortran subroutine in EISPACK.
  for (int j = 0; j < n; j++) {
    d[j] = V[n-1][j];
  }
  // Householder reduction to tridiagonal form.
  for (int i = n-1; i > 0; i--) {
    // Scale to avoid under/overflow.
    double scale = 0.0;
    double h = 0.0;
    for (int k = 0; k < i; k++) {
      scale = scale + fabs(d[k]);
    }
    if (scale == 0.0) {
      e[i] = d[i-1];
      for (int j = 0; j < i; j++) {
        d[j] = V[i-1][j];
        V[i][j] = 0.0;
        V[j][i] = 0.0;
      }
    } else {
      // Generate Householder vector.
      for (int k = 0; k < i; k++) {
        d[k] /= scale;
        h += d[k] * d[k];
      }
      double f = d[i-1];
      double g = sqrt(h);
      if (f > 0) {
        g = -g;
      }
      e[i] = scale * g;
      h = h - f * g;
      d[i-1] = f - g;
      for (int j = 0; j < i; j++) {
        e[j] = 0.0;
      }
      // Apply similarity transformation to remaining columns.
      for (int j = 0; j < i; j++) {
        f = d[j];
        V[j][i] = f;
        g = e[j] + V[j][j] * f;
        for (int k = j+1; k <= i-1; k++) {
          g += V[k][j] * d[k];
          e[k] += V[k][j] * f;
        }
        e[j] = g;
      }
      f = 0.0;
      for (int j = 0; j < i; j++) {
        e[j] /= h;
        f += e[j] * d[j];
      }
      double hh = f / (h + h);
      for (int j = 0; j < i; j++) {
        e[j] -= hh * d[j];
      }
      for (int j = 0; j < i; j++) {
        f = d[j];
        g = e[j];
        for (int k = j; k <= i-1; k++) {
          V[k][j] -= (f * e[k] + g * d[k]);
        }
        d[j] = V[i-1][j];
        V[i][j] = 0.0;
      }
    }
    d[i] = h;
  }
  // Accumulate transformations.
  for (int i = 0; i < n-1; i++) {
    V[n-1][i] = V[i][i];
    V[i][i] = 1.0;
    double h = d[i+1];
    if (h != 0.0) {
      for (int k = 0; k <= i; k++) {
        d[k] = V[k][i+1] / h;
      }
      for (int j = 0; j <= i; j++) {
        double g = 0.0;
        for (int k = 0; k <= i; k++) {
          g += V[k][i+1] * V[k][j];
        }
        for (int k = 0; k <= i; k++) {
          V[k][j] -= g * d[k];
        }
      }
    }
    for (int k = 0; k <= i; k++) {
      V[k][i+1] = 0.0;
    }
  }
  for (int j = 0; j < n; j++) {
    d[j] = V[n-1][j];
    V[n-1][j] = 0.0;
  }
  V[n-1][n-1] = 1.0;
  e[0] = 0.0;
}

// Symmetric tridiagonal QL algorithm.

static void tql2(double V[n][n], double d[n], double e[n]) {
//  This is derived from the Algol procedures tql2, by
//  Bowdler, Martin, Reinsch, and Wilkinson, Handbook for
//  Auto. Comp., Vol.ii-Linear Algebra, and the corresponding
//  Fortran subroutine in EISPACK.
  for (int i = 1; i < n; i++) {
    e[i-1] = e[i];
  }
  e[n-1] = 0.0;
  double f = 0.0;
  double tst1 = 0.0;
  double eps = pow(2.0,-52.0);
  for (int l = 0; l < n; l++) {
    // Find small subdiagonal element
    tst1 = MAX(tst1,fabs(d[l]) + fabs(e[l]));
    int m = l;
    while (m < n) {
      if (fabs(e[m]) <= eps*tst1) {
        break;
      }
      m++;
    }
    // If m == l, d[l] is an eigenvalue,
    // otherwise, iterate.
    if (m > l) {
      int iter = 0;
      do {
        iter = iter + 1;  // (Could check iteration count here.)
        // Compute implicit shift
        double g = d[l];
        double p = (d[l+1] - g) / (2.0 * e[l]);
        double r = hypot2(p,1.0);
        if (p < 0) {
          r = -r;
        }
        d[l] = e[l] / (p + r);
        d[l+1] = e[l] * (p + r);
        double dl1 = d[l+1];
        double h = g - d[l];
        for (int i = l+2; i < n; i++) {
          d[i] -= h;
        }
        f = f + h;
        // Implicit QL transformation.
        p = d[m];
        double c = 1.0;
        double c2 = c;
        double c3 = c;
        double el1 = e[l+1];
        double s = 0.0;
        double s2 = 0.0;
        for (int i = m-1; i >= l; i--) {
          c3 = c2;
          c2 = c;
          s2 = s;
          g = c * e[i];
          h = c * p;
          r = hypot2(p,e[i]);
          e[i+1] = s * r;
          s = e[i] / r;
          c = p / r;
          p = c * d[i] - s * g;
          d[i+1] = h + s * (c * g + s * d[i]);
          // Accumulate transformation.
          for (int k = 0; k < n; k++) {
            h = V[k][i+1];
            V[k][i+1] = s * V[k][i] + c * h;
            V[k][i] = c * V[k][i] - s * h;
          }
        }
        p = -s * s2 * c3 * el1 * e[l] / dl1;
        e[l] = s * p;
        d[l] = c * p;
        // Check for convergence.
      } while (fabs(e[l]) > eps*tst1);
    }
    d[l] = d[l] + f;
    e[l] = 0.0;
  }
  // Sort eigenvalues and corresponding vectors.
  for (int i = 0; i < n-1; i++) {
    int k = i;
    double p = d[i];
    for (int j = i+1; j < n; j++) {
      if (d[j] < p) {
        k = j;
        p = d[j];
      }
    }
    if (k != i) {
      d[k] = d[i];
      d[i] = p;
      for (int j = 0; j < n; j++) {
        p = V[j][i];
        V[j][i] = V[j][k];
        V[j][k] = p;
      }
    }
  }
}

void eigen_decomposition(double A[n][n], double V[n][n], double d[n]) {
  double e[n];
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      V[i][j] = A[i][j];
    }
  }
  tred2(V, d, e);
  tql2(V, d, e);
}
//end: Connelly Barnes code

/*void printMat(char ch, double A[3][3]) {
	printf("%c=[%g %g %g; %g %g %g; %g %g %g];\n", ch,
		A[0][0],A[0][1],A[0][2],
		A[1][0],A[1][1],A[1][2],
		A[2][0],A[2][1],A[2][2]);
}*/

vec3 nifti_mat33_eig3(double bxx, double bxy, double bxz, double byy, double byz, double bzz) {
    double A[3][3];
    A[0][0] = bxx;
    A[0][1] = bxy;
    A[0][2] = bxz;
    A[1][0] = bxy;
    A[1][1] = byy;
    A[1][2] = byz;
    A[2][0] = bxz;
    A[2][1] = byz;
    A[2][2] = bzz;
	double V[3][3];
	double d[3];
    eigen_decomposition(A,V,d);
    //printMat('A',A);
    //printf("[V,D] = eig(A) %%where A*V = V*D\n");
    //printMat('V',V);
    //printf("D = [%g 0 0; 0 %g 0; 0 0 %g]\n", d[0], d[1], d[2]);
    vec3 v3;
    v3.v[0] = V[0][2];
    v3.v[1] = V[1][2];
    v3.v[2] = V[2][2];
	if (v3.v[0] < 0.0) {
		//B-matrix underspecified to describe B-vector
		// Describes direction but not polarity of B-vector
		// e.g. We get an eigenvector, but eigenvalue can be + or -
		//  https://github.com/rordenlab/dcm2niix/issues/265
		// Like B2q We set the vector to have a positive x component by convention.
		//  https://raw.githubusercontent.com/matthew-brett/nibabel/master/nibabel/nicom/dwiparams.py
		// This ensures eddy will see this as a half shell and not attempt to interpret arbitrary signs
		v3.v[0] = -v3.v[0];
		v3.v[1] = -v3.v[1];
		v3.v[2] = -v3.v[2];
	}
    //printf("bvec = [%g 0 0; 0 %g 0; 0 0 %g]\n", v3.v[0], v3.v[1], v3.v[2]);
    return v3;
}







