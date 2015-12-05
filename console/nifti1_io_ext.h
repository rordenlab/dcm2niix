#ifndef NIFTI1_IO_EXT_H
#define NIFTI1_IO_EXT_H

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

float dotProduct(vec3 u, vec3 v);
int isSameFloat (float a, float b) ;
mat33 nifti_mat33_transpose( mat33 A ) ;
mat44 nifti_dicom2mat(float orient[7], float patientPosition[4], float xyzMM[4]);
mat44 nifti_mat44_mul( mat44 A , mat44 B );
vec3 crossProduct(vec3 u, vec3 v);
vec3 nifti_vect33_norm (vec3 v);
//vec3 nifti_vect33_norm (vec3 v);
vec3 nifti_vect33mat33_mul(vec3 v, mat33 m );
//vec3 nifti_vect33mat33_mul(vec3 v, mat33 m );
vec3 setVec3(float x, float y, float z);
//vec4 nifti_vect44mat44_mul(vec4 v, mat44 m );
vec4 setVec4(float x, float y, float z);
vec4 nifti_vect44mat44_mul(vec4 v, mat44 m );
#endif // NIFTI1_IO_EXT_H
