#include "nii_foreign.h"
#include "nii_io.h"
#include "nifti1.h"
#import "nifti1_io_core.h"
#include <stdio.h>
#import <Foundation/Foundation.h>
#import "zlib.h"
#import "nii_dicom.h"

#include <stdlib.h>  // for memory alloc/free

//#define MY_FORCE_SFORM //force usage of Q-Form if available (emulate VTK rather than SPM)

#undef  ERREX
#define ERREX(msg)                                           \
do{ fprintf(stderr,"** ERROR: nifti_convert_nhdr2nim: %s\n", (msg) ) ;  \
return NULL ; } while(0)

#undef IS_GOOD_FLOAT
#undef FIXED_FLOAT
#ifdef isfinite       // use isfinite() to check floats/doubles for goodness
#  define IS_GOOD_FLOAT(x) isfinite(x)       // check if x is a "good" float
#  define FIXED_FLOAT(x)   (isfinite(x) ? (x) : 0)           // fixed if bad
#else
#  define IS_GOOD_FLOAT(x) 1                               // don't check it
#  define FIXED_FLOAT(x)   (x)                               // don't fix it
#endif

/* nifti_type file codes */
#define NIFTI_FTYPE_ANALYZE   0
#define NIFTI_FTYPE_NIFTI1_1  1
#define NIFTI_FTYPE_NIFTI1_2  2
#define NIFTI_FTYPE_ASCII     3
#define NIFTI_MAX_FTYPE       3

#define LSB_FIRST 1
#define MSB_FIRST 2

void nifti_datatype_sizes( int datatype , int *nbyper, int *swapsize )
{
    int nb=0, ss=0 ;
    switch( datatype ){
        case DT_INT8:
        case DT_UINT8:       nb =  1 ; ss =  0 ; break ;
        case DT_INT16:
        case DT_UINT16:      nb =  2 ; ss =  2 ; break ;
        case DT_RGB24:       nb =  3 ; ss =  0 ; break ;
        case DT_RGBA32:      nb =  4 ; ss =  0 ; break ;
        case DT_INT32:
        case DT_UINT32:
        case DT_FLOAT32:     nb =  4 ; ss =  4 ; break ;
        case DT_COMPLEX64:   nb =  8 ; ss =  4 ; break ;
        case DT_FLOAT64:
        case DT_INT64:
        case DT_UINT64:      nb =  8 ; ss =  8 ; break ;
        case DT_FLOAT128:    nb = 16 ; ss = 16 ; break ;
        case DT_COMPLEX128:  nb = 16 ; ss =  8 ; break ;
        case DT_COMPLEX256:  nb = 32 ; ss = 16 ; break ;
    }
    ASSIF(nbyper,nb) ; ASSIF(swapsize,ss) ; return ;
}

int nifti_short_order(void)   // determine this CPU's byte order
{
    union { unsigned char bb[2] ;
        short ss ; } fred ;
    fred.bb[0] = 1 ; fred.bb[1] = 0 ;
    return (fred.ss == 1) ? LSB_FIRST : MSB_FIRST ;
}

#define REVERSE_ORDER(x) (3-(x))    // convert MSB_FIRST <--> LSB_FIRST


void swap_nifti_header( struct nifti_1_header *h , int is_nifti )
{
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

static int need_nhdr_swap( short dim0, int hdrsize )
{
    short d0    = dim0;     // so we won't have to swap them on the stack
    int   hsize = hdrsize;
    
    if( d0 != 0 ){     // then use it for the check
        if( d0 > 0 && d0 <= 7 ) return 0;
        nifti_swap_2bytes(1, &d0);        // swap?
        if( d0 > 0 && d0 <= 7 ) return 1;
        return -1;        // bad, naughty d0
    } 
    // dim[0] == 0 should not happen, but could, so try hdrsize
    if( hsize == sizeof(nifti_1_header) ) return 0;
    nifti_swap_4bytes(1, &hsize);     // swap?
    if( hsize == sizeof(nifti_1_header) ) return 1;
    return -2;     // bad, naughty hsize
}

nifti_image* nifti_convert_nhdr2nim(struct nifti_1_header nhdr, const char * fname )
{    
    int   ii , doswap , ioff ;
    int   is_nifti , is_onefile ;
    nifti_image *nim;
    nim = (nifti_image *)calloc( 1 , sizeof(nifti_image) ) ;
    if( !nim ) ERREX("failed to allocate nifti image");
    // be explicit with pointers
    nim->fname = NULL;
    nim->iname = NULL;
    nim->data = NULL;
    // check if we must swap bytes
    doswap = need_nhdr_swap(nhdr.dim[0], nhdr.sizeof_hdr); // swap data flag
    if( doswap < 0 ){
        free(nim);
        if( doswap == -1 ) ERREX("This is not a valid NIfTI header (check dim[0])");
        ERREX("This is not a valid NIfTI header (first two bytes should be '348')") ;
    }
    // determine if this is a NIFTI-1 compliant header
    is_nifti = NIFTI_VERSION(nhdr) ;
    //before swapping header, record the Analyze75 orient code
    if(!is_nifti) {
        printf("Unsupported format: Analyze format instead of NIfTI? Try MRIcro or MRIcron.\n");
//        unsigned char c = *((char *)(&nhdr.qform_code));
//        nim->analyze75_orient = (analyze_75_orient_code)c;
    }
    if( doswap )
        swap_nifti_header( &nhdr , is_nifti ) ;
//    if ( g_opts.debug > 2 ) disp_nifti_1_header("-d nhdr2nim : ", &nhdr);
    if( nhdr.datatype == DT_BINARY || nhdr.datatype == DT_UNKNOWN_DT  )  {
        NSLog(@"unknown or unsupported datatype (%d). Will attempt to view as unsigned 8-bit (assuming ImageJ export)", nhdr.datatype);
        nhdr.datatype =DT_UNSIGNED_CHAR;
        
        //ERREX("bad datatype") ;
    }
    if( nhdr.dim[1] <= 0 ) {
        free(nim);
        ERREX("bad dim[1]") ;
    }
    // fix bad dim[] values in the defined dimension range
    for( ii=2 ; ii <= nhdr.dim[0] ; ii++ )
        if( nhdr.dim[ii] <= 0 ) nhdr.dim[ii] = 1 ;
    // fix any remaining bad dim[] values, so garbage does not propagate
    // (only values 0 or 1 seem rational, otherwise set to arbirary 1)
    for( ii=nhdr.dim[0]+1 ; ii <= 7 ; ii++ )
        if( nhdr.dim[ii] != 1 && nhdr.dim[ii] != 0) nhdr.dim[ii] = 1 ;
    //optional: set 4th dim to have all the volumes
    /*int nVol = 1;
    for (ii = 4; ii <= 7; ii++)
        if( nhdr.dim[ii] > 1) nVol *= nhdr.dim[ii];
    if (nVol == 1)
        nhdr.dim[0] = 3;
    else {
        nhdr.dim[0] = 4;
        nhdr.dim[4] = nVol;
        for (ii = 5; ii <= 7; ii++)
            nhdr.dim[ii] = 1;
    }
    */
    
    // set bad grid spacings to 1.0
    for( ii=1 ; ii <= nhdr.dim[0] ; ii++ ){
        if( nhdr.pixdim[ii] == 0.0         ||
           !IS_GOOD_FLOAT(nhdr.pixdim[ii])  ) nhdr.pixdim[ii] = 1.0 ;
    }
    is_onefile = is_nifti && NIFTI_ONEFILE(nhdr) ;
    if (is_nifti)
        nim->nifti_type = (is_onefile) ? NIFTI_FTYPE_NIFTI1_1: NIFTI_FTYPE_NIFTI1_2 ;
    else
        nim->nifti_type = NIFTI_FTYPE_ANALYZE ;
    ii = nifti_short_order() ;
    if( doswap )
        nim->byteorder = REVERSE_ORDER(ii) ;
    else
        nim->byteorder = ii ;
    // set dimensions of data array
    nim->ndim = nim->dim[0] = nhdr.dim[0];
    nim->nx   = nim->dim[1] = nhdr.dim[1];
    nim->ny   = nim->dim[2] = nhdr.dim[2];
    nim->nz   = nim->dim[3] = nhdr.dim[3];
    nim->nt   = nim->dim[4] = nhdr.dim[4];
    nim->nu   = nim->dim[5] = nhdr.dim[5];
    nim->nv   = nim->dim[6] = nhdr.dim[6];
    nim->nw   = nim->dim[7] = nhdr.dim[7];
    for( ii=1, nim->nvox=1; ii <= nhdr.dim[0]; ii++ )
        nim->nvox *= nhdr.dim[ii];
    // set the type of data in voxels and how many bytes per voxel
    nim->datatype = nhdr.datatype ;
    nifti_datatype_sizes( nim->datatype , &(nim->nbyper) , &(nim->swapsize) ) ;
    if( nim->nbyper == 0 ){
        free(nim);
        NSLog(@"unknown or unsupported datatype %d", nhdr.datatype);
        ERREX("bad datatype");
    }
    // set the grid spacings
    nim->dx = nim->pixdim[1] = nhdr.pixdim[1] ;
    nim->dy = nim->pixdim[2] = nhdr.pixdim[2] ;
    nim->dz = nim->pixdim[3] = nhdr.pixdim[3] ;
    nim->dt = nim->pixdim[4] = nhdr.pixdim[4] ;
    nim->du = nim->pixdim[5] = nhdr.pixdim[5] ;
    nim->dv = nim->pixdim[6] = nhdr.pixdim[6] ;
    nim->dw = nim->pixdim[7] = nhdr.pixdim[7] ;
    // compute qto_xyz transformation from pixel indexes (i,j,k) to (x,y,z)
    if( !is_nifti || nhdr.qform_code <= 0 ){
        // if not nifti or qform_code <= 0, use grid spacing for qto_xyz
        nim->qto_xyz.m[0][0] = nim->dx ;  // grid spacings
        nim->qto_xyz.m[1][1] = nim->dy ;  // along diagonal
        nim->qto_xyz.m[2][2] = nim->dz ;
        // off diagonal is zero
        nim->qto_xyz.m[0][1]=nim->qto_xyz.m[0][2]=nim->qto_xyz.m[0][3] = 0.0;
        nim->qto_xyz.m[1][0]=nim->qto_xyz.m[1][2]=nim->qto_xyz.m[1][3] = 0.0;
        nim->qto_xyz.m[2][0]=nim->qto_xyz.m[2][1]=nim->qto_xyz.m[2][3] = 0.0;
        // last row is always [ 0 0 0 1 ]
        nim->qto_xyz.m[3][0]=nim->qto_xyz.m[3][1]=nim->qto_xyz.m[3][2] = 0.0;
        nim->qto_xyz.m[3][3]= 1.0 ;
        nim->qform_code = NIFTI_XFORM_UNKNOWN ;
        #ifdef MY_DEBUG //from nii_io.
        NSLog(@"-d no qform provided\n");
        #endif
    } else {
        /**- else NIFTI: use the quaternion-specified transformation */
        nim->quatern_b = FIXED_FLOAT( nhdr.quatern_b ) ;
        nim->quatern_c = FIXED_FLOAT( nhdr.quatern_c ) ;
        nim->quatern_d = FIXED_FLOAT( nhdr.quatern_d ) ;
        nim->qoffset_x = FIXED_FLOAT(nhdr.qoffset_x) ;
        nim->qoffset_y = FIXED_FLOAT(nhdr.qoffset_y) ;
        nim->qoffset_z = FIXED_FLOAT(nhdr.qoffset_z) ;
        nim->qfac = (nhdr.pixdim[0] < 0.0) ? -1.0 : 1.0 ;  // left-handedness?
        nim->qto_xyz = nifti_quatern_to_mat44(
                                              nim->quatern_b, nim->quatern_c, nim->quatern_d,
                                              nim->qoffset_x, nim->qoffset_y, nim->qoffset_z,
                                              nim->dx       , nim->dy       , nim->dz       ,
                                              nim->qfac                                      ) ;
        nim->qform_code = nhdr.qform_code ;
//        if( g_opts.debug > 1 )
//            nifti_disp_matrix_orient("-d qform orientations:\n", nim->qto_xyz);
    }
    /**- load inverse transformation (x,y,z) -> (i,j,k) */
    nim->qto_ijk = nifti_mat44_inverse( nim->qto_xyz ) ;
    /**- load sto_xyz affine transformation, if present */
    #ifdef MY_FORCE_SFORM
    if (nhdr.sform_code != NIFTI_XFORM_UNKNOWN) {
        nhdr.sform_code = NIFTI_XFORM_UNKNOWN;
        printf("Warning: using Q-Form instead of S-Form");
    }
    #endif
    if ((nhdr.sform_code == NIFTI_XFORM_UNKNOWN) && (nhdr.qform_code != NIFTI_XFORM_UNKNOWN)) {
        nhdr.sform_code = nhdr.qform_code;
        nhdr.srow_x[0] = nim->qto_xyz.m[0][0];
        nhdr.srow_x[1] = nim->qto_xyz.m[0][1];
        nhdr.srow_x[2] = nim->qto_xyz.m[0][2];
        nhdr.srow_x[3] = nim->qto_xyz.m[0][3];
        nhdr.srow_y[0] = nim->qto_xyz.m[1][0];
        nhdr.srow_y[1] = nim->qto_xyz.m[1][1];
        nhdr.srow_y[2] = nim->qto_xyz.m[1][2];
        nhdr.srow_y[3] = nim->qto_xyz.m[1][3];
        nhdr.srow_z[0] = nim->qto_xyz.m[2][0];
        nhdr.srow_z[1] = nim->qto_xyz.m[2][1];
        nhdr.srow_z[2] = nim->qto_xyz.m[2][2];
        nhdr.srow_z[3] = nim->qto_xyz.m[2][3];
    }
    if( !is_nifti || nhdr.sform_code <= 0 ){
        /**- if not nifti or sform_code <= 0, then no sto transformation */
        nim->sform_code = NIFTI_XFORM_UNKNOWN ;
        #ifdef MY_DEBUG //from nii_io.
        NSLog(@"-d no sform provided\n");
        #endif
    } else {
        // else set the sto transformation from srow_*[]
        nim->sto_xyz.m[0][0] = nhdr.srow_x[0] ;
        nim->sto_xyz.m[0][1] = nhdr.srow_x[1] ;
        nim->sto_xyz.m[0][2] = nhdr.srow_x[2] ;
        nim->sto_xyz.m[0][3] = nhdr.srow_x[3] ;
        nim->sto_xyz.m[1][0] = nhdr.srow_y[0] ;
        nim->sto_xyz.m[1][1] = nhdr.srow_y[1] ;
        nim->sto_xyz.m[1][2] = nhdr.srow_y[2] ;
        nim->sto_xyz.m[1][3] = nhdr.srow_y[3] ;
        nim->sto_xyz.m[2][0] = nhdr.srow_z[0] ;
        nim->sto_xyz.m[2][1] = nhdr.srow_z[1] ;
        nim->sto_xyz.m[2][2] = nhdr.srow_z[2] ;
        nim->sto_xyz.m[2][3] = nhdr.srow_z[3] ;
        // last row is always [ 0 0 0 1 ]
        nim->sto_xyz.m[3][0]=nim->sto_xyz.m[3][1]=nim->sto_xyz.m[3][2] = 0.0;
        nim->sto_xyz.m[3][3]= 1.0 ;
        nim->sto_ijk = nifti_mat44_inverse( nim->sto_xyz ) ;
        nim->sform_code = nhdr.sform_code ;
//        if( g_opts.debug > 1 )
//            nifti_disp_matrix_orient("-d sform orientations:\n", nim->sto_xyz);
    }
    // set miscellaneous NIFTI stuff
    if( is_nifti ){
        nim->scl_slope   = FIXED_FLOAT( nhdr.scl_slope ) ;
        nim->scl_inter   = FIXED_FLOAT( nhdr.scl_inter ) ;
        nim->intent_code = nhdr.intent_code ;
        nim->intent_p1 = FIXED_FLOAT( nhdr.intent_p1 ) ;
        nim->intent_p2 = FIXED_FLOAT( nhdr.intent_p2 ) ;
        nim->intent_p3 = FIXED_FLOAT( nhdr.intent_p3 ) ;
        nim->toffset   = FIXED_FLOAT( nhdr.toffset ) ;
        memcpy(nim->intent_name,nhdr.intent_name,15); nim->intent_name[15] = '\0';
        nim->xyz_units  = XYZT_TO_SPACE(nhdr.xyzt_units) ;
        nim->time_units = XYZT_TO_TIME (nhdr.xyzt_units) ;
        nim->freq_dim  = DIM_INFO_TO_FREQ_DIM ( nhdr.dim_info ) ;
        nim->phase_dim = DIM_INFO_TO_PHASE_DIM( nhdr.dim_info ) ;
        nim->slice_dim = DIM_INFO_TO_SLICE_DIM( nhdr.dim_info ) ;
        nim->slice_code     = nhdr.slice_code  ;
        nim->slice_start    = nhdr.slice_start ;
        nim->slice_end      = nhdr.slice_end   ;
        nim->slice_duration = FIXED_FLOAT(nhdr.slice_duration) ;
    }
    // set Miscellaneous ANALYZE stuff
    nim->cal_min = FIXED_FLOAT(nhdr.cal_min) ;
    nim->cal_max = FIXED_FLOAT(nhdr.cal_max) ;
    memcpy(nim->descrip ,nhdr.descrip ,79) ; nim->descrip [79] = '\0' ;
    memcpy(nim->aux_file,nhdr.aux_file,23) ; nim->aux_file[23] = '\0' ;
    // set ioff from vox_offset (but at least sizeof(header))
    is_onefile = is_nifti && NIFTI_ONEFILE(nhdr) ;
    if( is_onefile ){
        ioff = (int)nhdr.vox_offset ;
        //if( ioff < (int) sizeof(nhdr) ) ioff = (int) sizeof(nhdr) ; //2014 <- commented out for MHA support
    } else {
        ioff = (int)nhdr.vox_offset ;
    }
    nim->iname_offset = ioff ;
    // deal with file names if set
    if (fname!=NULL) {
        nim->fname = NULL;  
        nim->iname = NULL;
    } else { 
        nim->fname = NULL;  
        nim->iname = NULL; 
    }
    return nim;
}

void FslSetInit(FSLIO* fslio)
{
    fslio->niftiptr = NULL;
}

FSLIO *FslInit(void)
{
    FSLIO *fslio;
    fslio = (FSLIO *) calloc(1,sizeof(FSLIO));
    FslSetInit(fslio);
    return fslio;
}

int FslClose(FSLIO *fslio)
{
    if  (fslio->niftiptr != NULL) {
        if  (fslio->niftiptr->data != NULL)
            free(fslio->niftiptr->data);
        free(fslio->niftiptr);
        fslio->niftiptr = NULL;
        fslio->niftiptr = NULL;
    }
    free(fslio);
    fslio = NULL;
    return EXIT_SUCCESS;
}

int nii_readhdr(NSString * fname, struct nifti_1_header *niiHdr, long * gzBytes)
{
    NSData *hdrdata;
    if (([[fname pathExtension] rangeOfString:@"GZ" options:NSCaseInsensitiveSearch].location != NSNotFound)
        || ([[fname pathExtension] rangeOfString:@"VOI" options:NSCaseInsensitiveSearch].location != NSNotFound)) {
        NSData *data = [NSData dataWithContentsOfFile:fname];
        if (!data) return EXIT_FAILURE;
        hdrdata = ungz(data, sizeof(nifti_1_header));
        *gzBytes = K_gzBytes_headercompressed;
    } else {
        NSFileHandle *fileHandle = [NSFileHandle fileHandleForReadingAtPath:fname];
        hdrdata = [fileHandle readDataOfLength:sizeof(nifti_1_header)];
        *gzBytes = 0;
    }
    if((!hdrdata) || (hdrdata.length < sizeof(nifti_1_header))) return EXIT_FAILURE;
    [hdrdata getBytes:niiHdr length:sizeof(struct nifti_1_header)];
    return EXIT_SUCCESS;
}

NSData * ungz(NSData* data, NSInteger DecompBytes)
//set DecompByte =  NSIntegerMax to decompress entire file  
{
    if ([data length] == 0) return data;
    unsigned full_length = (unsigned)[data length];
    unsigned half_length = (unsigned)[data length] / 2;
    if (half_length > DecompBytes) half_length = (unsigned)DecompBytes;
    NSMutableData *decompressed = [NSMutableData dataWithLength: full_length + half_length];
    BOOL done = NO;
    int status;
    z_stream strm;
    strm.next_in = (Bytef *)[data bytes];
    strm.avail_in = (uInt)[data length];
    strm.total_out = 0;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    if (inflateInit2(&strm, (15+32)) != Z_OK) return nil;
    while (!done) {
        // Make sure we have enough room and reset the lengths.
        if (strm.total_out >= [decompressed length])
            [decompressed increaseLengthBy: half_length];
        //strm.next_out = [decompressed mutableBytes] + strm.total_out;
        strm.next_out =  (Bytef *)[decompressed mutableBytes] + (uInt)strm.total_out;
        strm.avail_out = (uInt)[decompressed length] - (uInt)strm.total_out;
        // Inflate another chunk.
        status = inflate (&strm, Z_SYNC_FLUSH);
        if (status == Z_STREAM_END) done = YES;
        if (strm.total_out >= DecompBytes) done = YES;
        else if (status != Z_OK) break;
    }
    if (inflateEnd (&strm) != Z_OK) return nil;
    // Set real length.
    if (done) {
        if (strm.total_out >= DecompBytes)
            [decompressed setLength: DecompBytes];
        else
            [decompressed setLength: strm.total_out];
        return [NSData dataWithData: decompressed];
    }
    return nil;
}

void char2uchar (FSLIO* fslio)
{
    if (fslio->niftiptr->datatype != DT_INT8) return;
    fslio->niftiptr->datatype = DT_UINT8;
    //NSLog(@"char2uchar untested"); //2014
    size_t nvox = fslio->niftiptr->nvox;
    THIS_INT8 *ch = (THIS_INT8 *) fslio->niftiptr->data;
    THIS_UINT8 *uch = (THIS_UINT8 *) fslio->niftiptr->data;
    for (size_t v = 0; v < nvox; v++)
        uch[v] = ch[v]+128;
}

void swapByteOrder (FSLIO* fslio) {
    if (fslio->niftiptr->nbyper == 1) return; //byte order does not matter for one byte image
    //NSLog(@"Swap?");
    if (fslio->niftiptr->byteorder == nifti_short_order()) return; //already native byte order
    //NSLog(@"Swap!");
    
    if (fslio->niftiptr->datatype == DT_RGBA32) return;
    if ( fslio->niftiptr->datatype == DT_RGB24) return;
    size_t nvox = fslio->niftiptr->nvox;
    if (fslio->niftiptr->nbyper == 1)
        return;
    else if (fslio->niftiptr->nbyper == 2)
        nifti_swap_2bytes(nvox,fslio->niftiptr->data);
    else if (fslio->niftiptr->nbyper == 4)
        nifti_swap_4bytes(nvox,fslio->niftiptr->data);
    else if (fslio->niftiptr->nbyper == 8)
        nifti_swap_8bytes(nvox,fslio->niftiptr->data);
    else
        NSLog(@"swapByteOrder: Unsupported data type!");
}

NSString * NewFileExt(NSString *oldname, NSString *newx) {
    NSString* newname = [oldname stringByDeletingPathExtension];
    newname = [newname stringByAppendingString: newx];
    return newname;
}

bool checkSandAccessX (NSString *file_name) {
    bool result = (!access([file_name UTF8String], R_OK) );
    if (result) return result; //already have access
    NSOpenPanel *openPanel  = [NSOpenPanel openPanel];
    [openPanel setDirectoryURL: [[NSURL alloc] initWithString:file_name]];
    //NSLog(@"selecting : %@",[FName lastPathComponent] ); // [FName lastPathComponent]
    openPanel.title = [@"Select file " stringByAppendingString:[file_name lastPathComponent]];
    NSString *Ext = [file_name pathExtension];
    NSArray *fileTypes = [NSArray arrayWithObjects: Ext, nil];
    [openPanel setAllowedFileTypes:fileTypes];
    [openPanel runModal];
    result = (!access([file_name UTF8String], R_OK) );
    if (result) return result; //already have access
    /*
     NSAlert *alert = [[NSAlert alloc] init];
     [alert setMessageText:[@"You do not have access to the file " stringByAppendingString:[file_name lastPathComponent]] ];
     [alert runModal];*/
    NSBeginAlertSheet(@"Unable to open image", @"OK",NULL,NULL, [[NSApplication sharedApplication] keyWindow], NULL,//self,
                      NULL, NULL, NULL,
                      @"%@"
                      , [@"You do not have access to the file " stringByAppendingString:[file_name lastPathComponent]]);
    return result; //no access
}

bool checkSandAccess3 (NSString *file_name)
{
    if (file_name.length < 3) return true;
    //NSLog(@"sanbox : %@",file_name ); // [FName lastPathComponent]
    bool result = checkSandAccessX (file_name);
    //bool result = checkSandAccess(file_name);
    if (!result) return result; //no access to primary file
    NSString *Ext = [file_name pathExtension];
    if ([Ext caseInsensitiveCompare:@"HDR"]== NSOrderedSame )
        Ext = @"img"; //hdr file requires img
    else if([Ext caseInsensitiveCompare:@"IMG"]== NSOrderedSame )
        Ext = @"hdr"; //img file requires hdr
    else  return result; //no secondary file
    NSString *FName = [NSString stringWithFormat:@"%@.%@", [file_name stringByDeletingPathExtension], Ext];
    return checkSandAccessX(FName);
}

void nii_zeroHdr(struct nifti_1_header *nhdr)
{
    for (int i = 0; i < 8; i++)
        nhdr->dim[i] = 0;
}

/*THIS_UINT8 * nii_rgb2Planar(THIS_UINT8 *bImg, struct nifti_1_header *hdr, int isPlanar) {
    //DICOM data saved in triples RGBRGBRGB, NIfTI RGB saved in planes RRR..RGGG..GBBBB..B
    if (hdr->datatype != DT_RGB24) return bImg;
    if (isPlanar == 1) return bImg;//return nii_bgr2rgb(bImg,hdr);
    int dim3to7 = 1;
    for (int i = 3; i < 8; i++)
        if (hdr->dim[i] > 1) dim3to7 = dim3to7 * hdr->dim[i];
    int sliceBytes24 = hdr->dim[1]*hdr->dim[2] * hdr->bitpix/8;
    int sliceBytes8 = hdr->dim[1]*hdr->dim[2];
    unsigned char  slice24[ sliceBytes24 ];
    int sliceOffsetR = 0;
    for (int sl = 0; sl < dim3to7; sl++) { //for each 2D slice
        memcpy(&slice24, &bImg[sliceOffsetR], sliceBytes24);
        int sliceOffsetG = sliceOffsetR + sliceBytes8;
        int sliceOffsetB = sliceOffsetR + 2*sliceBytes8;
        int i = 0;
        int j = 0;
        for (int rgb = 0; rgb < sliceBytes8; rgb++) {
            bImg[sliceOffsetR+j] =slice24[i];
            i++;
            bImg[sliceOffsetG+j] =slice24[i];
            i++;
            bImg[sliceOffsetB+j] =slice24[i];
            i++;
            j++;
        }
        sliceOffsetR += sliceBytes24;
    } //for each slice
    return bImg;
} //nii_rgb2Planar()*/

struct TDICOMdata nii_readParRecV(char * fname) {
    TDTI4D unused;
    return nii_readParRec(fname, false, &unused);
} // readDICOM()


int FslReadVolumes(FSLIO* fslio, char* filename, int skipVol, int loadVol, bool dicomWarn)
//returns volumes loaded, 0 when unable to load images
// FslReadVolumes(fslio,"~/img.nii",0,0) determine number of volumes (returns -n where n are volumes in image)
// FslReadVolumes(fslio,"~/img.nii",0,1) load volume 1 (returns 1)
// FslReadVolumes(fslio,"~/img.nii",2,10) loads volumes 3..13 (returns 10)
// FslReadVolumes(fslio,"~/img.nii",0,INT_MAX) loads all volumes (returns number of volumes in image)
{
    //char lastchar = upperlastchar(filename); //detect file name .nii -> I, .hdr -> R, .gz -> Z, .img -> G
    NSString* basename = [NSString stringWithFormat:@"%s" , filename];
    if (![[NSFileManager defaultManager] fileExistsAtPath:basename]) {
        NSLog(@"Unable to find a file named %@", basename);
        return 0;
    }
    NSString *ext =[basename pathExtension];
    /*if ([ext caseInsensitiveCompare:@"GZ"] == NSOrderedSame) //ONLY "GZ", not "MGZ"!
        return FslReadVolumesGZ(fslio, filename, skipVol, loadVol); //read compressed GZ
    if ([ext rangeOfString:@"VOI" options:NSCaseInsensitiveSearch].location != NSNotFound)
        return FslReadVolumesGZ(fslio, filename, skipVol, loadVol); //read compressed Volume of Interest*/
    NSString* fname;
    //NSLog(@"ext=%@",ext);
    if ([ext rangeOfString:@"REC" options:NSCaseInsensitiveSearch].location != NSNotFound)
        fname = NewFileExt(basename, @".par"); // Philips Par/rec
    else if (([ext rangeOfString:@"BVAL" options:NSCaseInsensitiveSearch].location != NSNotFound) || ([ext rangeOfString:@"BVEC" options:NSCaseInsensitiveSearch].location != NSNotFound) ) {
        fname = NewFileExt(basename, @".nii");
        if (![[NSFileManager defaultManager] fileExistsAtPath:fname]) fname = NewFileExt(basename, @".hdr");
        if (![[NSFileManager defaultManager] fileExistsAtPath:fname]) fname = NewFileExt(basename, @".nii.gz");
        basename = fname;
        ext =[basename pathExtension];
    } else if ([ext rangeOfString:@"IMG" options:NSCaseInsensitiveSearch].location != NSNotFound)
        fname = NewFileExt(basename, @".hdr");
    else
        fname = basename;
    NSString* imgname = basename;
    struct nifti_1_header niiHdr;
    int OK = EXIT_FAILURE;
    long gzBytes = 0;
    bool swapEndian = false;
    unsigned char * dicomImg = NULL;
    nii_zeroHdr(&niiHdr);
    //bool isPlanarRGB = true; //only for 24-bit red-green-blue data
    //NSImage * myNextImage = [[NSImage alloc] initWithContentsOfFile:basename];
    /*if ([ext rangeOfString:@"HEAD" options:NSCaseInsensitiveSearch].location != NSNotFound)
        OK = afni_readhead(fname, &imgname, &niiHdr, &gzBytes, &swapEndian);
    else if ([ext rangeOfString:@"PIC" options:NSCaseInsensitiveSearch].location != NSNotFound)
       OK = nii_readpic(fname, &niiHdr);
    else if (([ext rangeOfString:@"MHA" options:NSCaseInsensitiveSearch].location != NSNotFound) || ([ext rangeOfString:@"MHD" options:NSCaseInsensitiveSearch].location != NSNotFound))
        OK = nii_readmha(fname, &imgname, &niiHdr, &gzBytes, &swapEndian);
    else if (([ext rangeOfString:@"NHDR" options:NSCaseInsensitiveSearch].location != NSNotFound) || ([ext rangeOfString:@"NRRD" options:NSCaseInsensitiveSearch].location != NSNotFound))
        OK = nii_readnrrd(fname, &imgname, &niiHdr, &gzBytes, &swapEndian);
    else if (([ext rangeOfString:@"MGH" options:NSCaseInsensitiveSearch].location != NSNotFound) || ([ext rangeOfString:@"MGZ" options:NSCaseInsensitiveSearch].location != NSNotFound))
        OK = nii_readmgh(fname,  &niiHdr, &gzBytes, &swapEndian);
    //else if ([ext rangeOfString:@"DF3" options:NSCaseInsensitiveSearch].location != NSNotFound)
    //         OK = nii_readDf3(fname,  &niiHdr, &gzBytes, &swapEndian);
    else*/
    //|| ([ext rangeOfString:@"GZ" options:NSCaseInsensitiveSearch].location != NSNotFound) // <- MGZ
    //([ext rangeOfString:@"HDR" options:NSCaseInsensitiveSearch].location != NSNotFound)||  //<- NHDR
    if ( [ext caseInsensitiveCompare:@"GZ"] == NSOrderedSame ) {
        NSString *ext2 =[[basename stringByDeletingPathExtension] pathExtension];
        if ( [ext2 caseInsensitiveCompare:@"BRIK"] == NSOrderedSame ) {//this is a BRIK.gz file, not .nii.gz
            ext = @"BRIK";
            fname = NewFileExt([basename stringByDeletingPathExtension], @".HEAD");
        }
    }
    
    if (([ext rangeOfString:@"IMG" options:NSCaseInsensitiveSearch].location != NSNotFound)
             || ([ext rangeOfString:@"NII" options:NSCaseInsensitiveSearch].location != NSNotFound)
             || ([ext rangeOfString:@"VOI" options:NSCaseInsensitiveSearch].location != NSNotFound)
             || ( [ext caseInsensitiveCompare:@"HDR"] == NSOrderedSame )
             || ( [ext caseInsensitiveCompare:@"GZ"] == NSOrderedSame )
             || ([ext rangeOfString:@"NII.GZ" options:NSCaseInsensitiveSearch].location != NSNotFound))
        OK = nii_readhdr(fname, &niiHdr, &gzBytes);
    else if (true) {//([[NSImage alloc] initWithContentsOfFile:basename] != NULL) //use OS to import image - method of (almost) last resort!
        dicomImg = nii_readForeign(fname,  &niiHdr, skipVol, loadVol);
        if (dicomImg != NULL) OK = EXIT_SUCCESS;
        gzBytes = K_gzBytes_skipRead;
        //dicomImg = nii_readTIFF(fname,  &niiHdr, &gzBytes, &swapEndian);
    }
    if (OK == EXIT_FAILURE) { //if all else fails, assume DICOM
        char fnameC[1024] = {""};
        strcat(fnameC,[fname cStringUsingEncoding:1]);
        struct TDICOMdata d;
        if ([ext rangeOfString:@"PAR" options:NSCaseInsensitiveSearch].location != NSNotFound) {
            d =nii_readParRecV(fnameC);
            imgname = [NSString stringWithCString:fnameC encoding:NSASCIIStringEncoding];
            if (!checkSandAccess3(imgname)) {
                NSLog(@"You do not have permission to open %@", imgname);
                return 0;
            }
        } else {
            d =readDICOM(fnameC);
            
            if (dicomWarn) {
                NSAlert *alert = [[NSAlert alloc] init];
                #ifndef STRIP_DCM2NII // /BuildSettings/PreprocessorMacros/STRIP_DCM2NII
                NSDictionary* environ = [[NSProcessInfo processInfo] environment];
                BOOL inSandbox = (nil != [environ objectForKey:@"APP_SANDBOX_CONTAINER_ID"]);
                if (inSandbox)
                    [alert setMessageText:@"Please convert DICOM images to NIfTI (solution: use the free dcm2nii tool)" ];
                else
                    [alert setMessageText:@"For improved rendering convert DICOM images to NIfTI (solution: use the 'Import' menu)" ];
                #else
                [alert setMessageText:@"Please convert DICOM images to NIfTI (solution: use the free dcm2nii tool)" ];
                #endif
                [alert runModal];
            }
            
        }
        if (d.isValid) {
            dicomImg = nii_loadImgXL(fnameC, &niiHdr,d, false, kCompressNone);
            gzBytes = K_gzBytes_skipRead;
            OK = EXIT_SUCCESS;
        } else
            OK = EXIT_FAILURE;
        //OK = nii_readDICOM(fnameC,&niiHdr, &gzBytes, &swapEndian, &isPlanarRGB);
        //convertForeignToNifti(&niiHdr);
    }
    if (OK == EXIT_FAILURE) {
        NSLog(@"Error loading file %@",fname);
        return 0;
    }
    //convert nifti header to FSLIO format, set native byte order if required
    nifti_image *nim ;
    nim = nifti_convert_nhdr2nim(niiHdr,filename);
    if ((nim == NULL) || (nim->dim[1]*nim->dim[2]*nim->dim[3] < 1)) {
        NSLog(@"Error reading file %@",fname);
        return 0;
    }
    int nVol = (int) (nim->nvox/(nim->dim[1]*nim->dim[2]*nim->dim[3]));
    if  ((loadVol+skipVol) > nVol) //read remaining volumes
        loadVol = nVol - skipVol;
    if (loadVol < 1) return -nVol;
    nim->nt = nim->dim[4] = loadVol; ////we will collapse all non-spatial dimensions to dim[4]
    nim->nu = nim->dim[5] = 1;
    nim->nv = nim->dim[6] = 1;
    nim->nw = nim->dim[7] = 1;
    nim->nvox = nim->dim[1]*nim->dim[2]*nim->dim[3]*loadVol;
    fslio->niftiptr = nim;
    #ifdef MY_DEBUG //from nii_io.h
    printf("Header %s XYZT %dx%dx%dx%d bpp= %d offset= %lld\n",filename, nim->dim[1],nim->dim[2],nim->dim[3],nim->dim[4],nim->nbyper, nim->iname_offset);
    #endif
    //read image
    if ([[imgname pathExtension] rangeOfString:@"HDR" options:NSCaseInsensitiveSearch].location != NSNotFound)
        imgname = NewFileExt(basename, @".img");
    long long d1 = nim->dim[1];
    long long d2 = nim->dim[2];
    long long d3 = nim->dim[3];
    long long d4 = loadVol;
    long long d5 = nim->nbyper;
    //long long imgsz = nim->dim[1] * nim->dim[2] * nim->dim[3] * loadVol * nim->nbyper;
    long long imgsz = d1*d2*d3*d4*d5;
    if (gzBytes == K_gzBytes_skipRead) {
        free(fslio->niftiptr->data);
        fslio->niftiptr->data = dicomImg;
        return loadVol;
    }
    #ifdef MY_DEBUG //from nii_io.h
    NSLog(@"load %lld volumes from %@ requiring bytes %lld of image data", d4, fname, imgsz);
    #endif
    if (![[NSFileManager defaultManager] fileExistsAtPath:imgname]) { //if basename is /path/img.mhd and imgname is img.raw, set imgname to /path/img.raw
        imgname = [[basename stringByDeletingLastPathComponent] stringByAppendingPathComponent: [imgname lastPathComponent]];
        if (![[NSFileManager defaultManager] fileExistsAtPath:imgname]) {
            NSLog(@"Unable to find a file named %@", imgname);
            return 0;
        }
    }
    if (!checkSandAccess3(imgname)) {
        NSLog(@"You do not have permission to open %@", imgname);
        return 0;
    }
    FILE *pFile;
    //pFile=fopen(imgname,"rb");
    pFile= fopen([imgname fileSystemRepresentation], "rb");
    fseek (pFile, 0, SEEK_END); //int FileSz=ftell (ptr_myfile);
    long long fsz = ftell (pFile);
    if (nim->iname_offset < 0) { //mha and mhd format use a -1 for HeaderSize to indicate that the image is the last bytes of the file
        nim->iname_offset = fsz - (nim->dim[1] * nim->dim[2] * nim->dim[3] * nim->dim[4] * nim->nbyper);
        if (gzBytes > 0) nim->iname_offset = fsz - gzBytes;
        if (nim->iname_offset < 0) {
            NSLog(@"File is smaller than required image data %@", imgname);
            return 0;
        }
    }
        long long skipBytes = nim->iname_offset+(skipVol *nim->dim[1] * nim->dim[2] * nim->dim[3]* nim->nbyper);     //skip initial headers
        THIS_UINT8 *outbuf = (THIS_UINT8 *) malloc(imgsz);
        #ifdef MY_DEBUG //from nii_io.h
        NSLog(@"nii_io 2 malloc size %lld",imgsz);
        #endif
        size_t num = 1;
        if (gzBytes == 0) {
            fseek(pFile, 0, SEEK_SET);
            fseek(pFile, skipBytes, SEEK_SET);
            if (fsz < (skipBytes+imgsz)) {
                NSLog(@"File %@ too small (%lld) to be NIfTI image with %dx%dx%dx%d voxels (%d byte per, %lld offset)", imgname, fsz,nim->dim[1],nim->dim[2],nim->dim[3],nim->dim[4], nim->nbyper,nim->iname_offset );
                free(outbuf);
                return 0;
            }
            num = fread( outbuf, imgsz, 1, pFile);
           fclose(pFile);
        } else {
            fclose(pFile);
            if ((gzBytes > 0) && (fsz < (skipBytes+gzBytes)) ) {
                NSLog(@"File %@ too small (%lld) to be NIfTI image", fname, fsz);
                free(outbuf);
                return 0;
            }
            NSFileHandle *fileHandle = [NSFileHandle fileHandleForReadingAtPath:imgname];
            if (gzBytes == K_gzBytes_headercompressed )
                gzBytes = fsz;
            else {
                [fileHandle seekToFileOffset:skipBytes];
                skipBytes = 0;
            }
            if (gzBytes == K_gzBytes_headeruncompressed)
                gzBytes = fsz-skipBytes;
            NSData *data = [fileHandle readDataOfLength:gzBytes];
            if (!data)
                num = -1;
            else {
                data = ungz(data, imgsz+skipBytes);
                [data getBytes:outbuf range:NSMakeRange(skipBytes,imgsz)];
            }
        } //if image data gz compressed
    free(fslio->niftiptr->data);
    fslio->niftiptr->data = outbuf;
    if (num < 1) return 0;
    if (swapEndian) fslio->niftiptr->byteorder = REVERSE_ORDER(nifti_short_order());//force swap for MHA and MHD files without native byte order
    swapByteOrder (fslio);
    if (fslio->niftiptr->datatype == DT_INT8)
        char2uchar (fslio);
    return loadVol;
}

void* FslReadAllVolumes(FSLIO* fslio, char* filename, int maxNumVolumes, bool dicomWarn)
{
    //if (FslReadVolumes(fslio, filename, 0, INT_MAX) < 1)

    if (FslReadVolumes(fslio, filename, 0, maxNumVolumes, dicomWarn) < 1)
        return NULL;
    return fslio->niftiptr->data;
}

/*void nifti_image_infodump( const nifti_image *nim )
{
    printf("Image size is %dx%dx%d\n",nim->dim[1], nim->dim[2],nim->dim[3]);
}*/
