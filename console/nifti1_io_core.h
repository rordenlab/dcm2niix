//this minimal set of nifti routines is based on nifti1_io without the dependencies (zlib) and a few extra functions
// http://nifti.nimh.nih.gov/pub/dist/src/niftilib/nifti1_io.h
// http://niftilib.sourceforge.net
#ifndef _NIFTI_IO_CORE_HEADER_
#define _NIFTI_IO_CORE_HEADER_

#ifdef HAVE_R
#define STRICT_R_HEADERS
#include "RNifti.h"
#endif

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdbool.h> //requires VS 2015 or later

#include <string.h>

#ifndef HAVE_R
typedef struct {                   /** 4x4 matrix struct **/
    float m[3][3] ;
} mat33 ;
typedef struct {                   /** 4x4 matrix struct **/
    float m[4][4] ;
} mat44 ;
#endif
typedef struct {                   /** x4 vector struct **/
    float v[4] ;
} vec4 ;
typedef struct {                   /** x3 vector struct **/
    float v[3] ;
} vec3 ;
typedef struct {                   /** x4 vector struct INTEGER**/
    int v[3] ;
} ivec3 ;

#define LOAD_MAT33(AA,a11,a12,a13 ,a21,a22,a23 ,a31,a32,a33)    \
( AA.m[0][0]=a11 , AA.m[0][1]=a12 , AA.m[0][2]=a13 ,   \
AA.m[1][0]=a21 , AA.m[1][1]=a22 , AA.m[1][2]=a23  ,   \
AA.m[2][0]=a31 , AA.m[2][1]=a32 , AA.m[2][2]=a33            )

#define LOAD_MAT44(AA,a11,a12,a13,a14,a21,a22,a23,a24,a31,a32,a33,a34)    \
( AA.m[0][0]=a11 , AA.m[0][1]=a12 , AA.m[0][2]=a13 , AA.m[0][3]=a14 ,   \
AA.m[1][0]=a21 , AA.m[1][1]=a22 , AA.m[1][2]=a23 , AA.m[1][3]=a24 ,   \
AA.m[2][0]=a31 , AA.m[2][1]=a32 , AA.m[2][2]=a33 , AA.m[2][3]=a34 ,   \
AA.m[3][0]=AA.m[3][1]=AA.m[3][2]=0.0f , AA.m[3][3]=1.0f            )

#undef  ASSIF  // assign v to *p, if possible
#define ASSIF(p,v) if( (p)!=NULL ) *(p) = (v)
float dotProduct(vec3 u, vec3 v);
float nifti_mat33_determ( mat33 R ) ;
int isSameFloat (float a, float b) ;
int isSameDouble (double a, double b) ;

mat33 nifti_mat33_inverse( mat33 R );
mat33 nifti_mat33_mul( mat33 A , mat33 B );
mat33 nifti_mat33_transpose( mat33 A ) ;
mat44 nifti_dicom2mat(float orient[7], float patientPosition[4], float xyzMM[4]);
mat44 nifti_mat44_inverse( mat44 R );
mat44 nifti_mat44_mul( mat44 A , mat44 B );
vec3 crossProduct(vec3 u, vec3 v);
vec3 nifti_vect33_norm (vec3 v);
vec3 nifti_vect33mat33_mul(vec3 v, mat33 m );
ivec3 setiVec3(int x, int y, int z);
vec3 setVec3(float x, float y, float z);
vec4 setVec4(float x, float y, float z);
vec4 nifti_vect44mat44_mul(vec4 v, mat44 m );
void nifti_swap_2bytes( size_t n , void *ar );    // 2 bytes at a time
void nifti_swap_4bytes( size_t n , void *ar );    // 4 bytes at a time
void nifti_swap_8bytes( size_t n , void *ar );    // 8 bytes at a time
void nifti_mat44_to_quatern( mat44 R ,
                            float *qb, float *qc, float *qd,
                            float *qx, float *qy, float *qz,
                            float *dx, float *dy, float *dz, float *qfac );
mat44 nifti_quatern_to_mat44( float qb, float qc, float qd,
                             float qx, float qy, float qz,
                             float dx, float dy, float dz, float qfac );

#ifdef  __cplusplus
}
#endif

#endif