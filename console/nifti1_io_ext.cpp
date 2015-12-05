#include "nifti1_io_core.h"
#include <float.h>
#include <math.h>

int isSameFloat (float a, float b) {
    return (fabs (a - b) <= FLT_EPSILON);
}

vec3 setVec3(float x, float y, float z)
{
    vec3 v = {x, y, z};
    return v;
}

vec4 setVec4(float x, float y, float z)
{
    vec4 v= {x, y, z, 1};
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
    //printf("Orient %g %g %g %g %g %g\n",orient[1],orient[2],orient[3],orient[4],orient[5],orient[6] );
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
