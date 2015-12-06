//this minimal set of nifti routines is based on nifti1_io with the dependencies (zlib) and a few extra functions
// http://nifti.nimh.nih.gov/pub/dist/src/niftilib/nifti1_io.h
// http://niftilib.sourceforge.net
#ifndef _NIFTI_IO_CORE_HEADER_
#define _NIFTI_IO_CORE_HEADER_

#ifdef  __cplusplus
extern "C" {
#endif

#include "config.h"

#ifdef NIFTI_FOUND
#include <nifti1_io.h>
#else

#include <stdbool.h>
#include <string.h>
    
typedef struct {                   /** 4x4 matrix struct **/
    float m[3][3] ;
} mat33 ;
typedef struct {                   /** 4x4 matrix struct **/
    float m[4][4] ;
} mat44 ;

#undef  ASSIF  // assign v to *p, if possible
#define ASSIF(p,v) if( (p)!=NULL ) *(p) = (v)
float nifti_mat33_determ( mat33 R ) ;
mat33 nifti_mat33_inverse( mat33 R );
mat33 nifti_mat33_mul( mat33 A , mat33 B );
mat33 nifti_mat33_mul( mat33 A , mat33 B );
mat44 nifti_mat44_inverse( mat44 R );
mat44 nifti_mat44_inverse( mat44 R );
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

#endif // NIFTI_FOUND

#include "nifti1_io_ext.h"

#ifdef  __cplusplus
}
#endif

#endif
