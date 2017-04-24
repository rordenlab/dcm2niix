#include "nii_foreign.h"
#include "nifti1.h"
#include "nifti1_io_core.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "print.h"

#ifndef Float32
	#define Float32 float
#endif
#ifndef uint32
	#define uint32 uint32_t
#endif



/*nii_readEcat7 2017 by Chris Rorden, BSD license
 http://www.turkupetcentre.net/software/libdoc/libtpcimgio/ecat7_8h_source.html#l00060
 http://www.turkupetcentre.net/software/libdoc/libtpcimgio/ecat7r_8c_source.html#l00717
 http://xmedcon.sourcearchive.com/documentation/0.10.7-1build1/ecat7_8h_source.html
 https://github.com/BIC-MNI/minc-tools/tree/master/conversion/ecattominc
 https://github.com/nipy/nibabel/blob/ec4567fb09b4472c5a4bb9a13dbcc9eb0a63d875/nibabel/ecat.py
*/

//TO DO:
// currently only reads first 3D volume: for multiple volumes we need to handle num_frames num_bed_pos
// SForm and QForm copy SPM and do not account for slice angulation or orientation
int nii_readEcat7(const char * fname, struct nifti_1_header *nhdr, bool * swapEndian) {
//data type
#define	ECAT7_BYTE 1
#define	ECAT7_VAXI2 2
#define ECAT7_VAXI4 3
#define ECAT7_VAXR4 4
#define ECAT7_IEEER4 5
#define	ECAT7_SUNI2 6
#define	ECAT7_SUNI4 7
//file types
//#define ECAT7_UNKNOWN 0
#define ECAT7_2DSCAN 1
#define ECAT7_IMAGE16 2
#define ECAT7_ATTEN 3
#define ECAT7_2DNORM 4
#define ECAT7_POLARMAP 5
#define ECAT7_VOLUME8 6
#define ECAT7_VOLUME16 7
#define ECAT7_PROJ 8
#define ECAT7_PROJ16 9
#define ECAT7_IMAGE8 10
#define ECAT7_3DSCAN 11
#define ECAT7_3DSCAN8 12
#define ECAT7_3DNORM 13
#define ECAT7_3DSCANFIT 14
    typedef struct __attribute__((packed)) {
        char magic[14],original_filename[32];
        uint16_t sw_version, system_type, file_type;
        char serial_number[10];
        uint32 scan_start_time;
        char isotope_name[8];
        Float32 isotope_halflife;
        char radiopharmaceutical[32];
        Float32 gantry_tilt, gantry_rotation, bed_elevation, intrinsic_tilt;
        int16_t wobble_speed, transm_source_type;
        Float32 distance_scanned, transaxial_fov;
        uint16_t angular_compression, coin_samp_mode, axial_samp_mode;
        Float32 ecat_calibration_factor;
        uint16_t calibration_unitS, calibration_units_type, compression_code;
        char study_type[12], patient_id[16], patient_name[32], patient_sex, patient_dexterity;
        Float32 patient_age, patient_height, patient_weight;
        uint32 patient_birth_date;
        char physician_name[32], operator_name[32], study_description[32];
        uint16_t acquisition_type, patient_orientation;
        char facility_name[20];
        uint16_t num_planes, num_frames, num_gates, num_bed_pos;
        Float32 init_bed_position;
        Float32 bed_position[15];
        Float32 plane_separation;
        uint16_t lwr_sctr_thres, lwr_true_thres, upr_true_thres;
        char user_process_code[10];
        uint16_t acquisition_mode;
        Float32 bin_size, branching_fraction;
        uint32 dose_start_time;
        Float32 dosage, well_counter_corr_factor;
        char data_units[32];
        uint16_t septa_state;
        char fill[12];
    } ecat_main_hdr;
    typedef struct __attribute__((packed)) {
        int16_t data_type, num_dimensions, x_dimension, y_dimension, z_dimension;
        Float32 x_offset, y_offset, z_offset, recon_zoom, scale_factor;
        int16_t image_min, image_max;
        Float32 x_pixel_size, y_pixel_size, z_pixel_size;
        int32_t frame_duration, frame_start_time;
        int16_t filter_code;
        Float32 x_resolution, y_resolution, z_resolution, num_r_elements, num_angles, z_rotation_angle, decay_corr_fctr;
        int32_t processing_code, gate_duration, r_wave_offset, num_accepted_beats;
        Float32 filter_cutoff_frequenc, filter_resolution, filter_ramp_slope;
        int16_t filter_order;
        Float32 filter_scatter_fraction, filter_scatter_slope;
        char annotation[40];
        Float32 mtx[9], rfilter_cutoff, rfilter_resolution;
        int16_t rfilter_code, rfilter_order;
        Float32 zfilter_cutoff, zfilter_resolution;
        int16_t zfilter_code, zfilter_order;
        Float32 mtx_1_4, mtx_2_4, mtx_3_4;
        int16_t scatter_type, recon_type, recon_views, fill_cti[87], fill_user[49];
    } ecat_img_hdr;
    typedef struct __attribute__((packed)) {
        int32_t hdr[4],
        r01[4],r02[4],r03[4],r04[4],r05[4],r06[4],r07[4],r08[4],r09[4],r10[4],
        r11[4],r12[4],r13[4],r14[4],r15[4],r16[4],r17[4],r18[4],r19[4],r20[4],
        r21[4],r22[4],r23[4],r24[4],r25[4],r26[4],r27[4],r28[4],r29[4],r30[4],
        r31[4];
    } ecat_list_hdr;
    * swapEndian = false;
    size_t n;
    FILE *f;
    ecat_main_hdr mhdr;
    f = fopen(fname, "rb");
    if (f)
        n = fread(&mhdr, sizeof(mhdr), 1, f);
    if(!f || n!=1) {
        printf("Problem reading ECAT7 file!\n");
        fclose(f);
        return EXIT_FAILURE;
    }
    if ((mhdr.magic[0] != 'M') || (mhdr.magic[1] != 'A') || (mhdr.magic[2] != 'T')
        || (mhdr.magic[3] != 'R') || (mhdr.magic[4] != 'I') || (mhdr.magic[5] != 'X') ) {
        printf("Signature not 'MATRIX' (ECAT7)\n");
        fclose(f);
        return EXIT_FAILURE;
    }
    * swapEndian = mhdr.file_type > 255;
    if (*swapEndian) {
        nifti_swap_2bytes(1, &mhdr.sw_version);
        nifti_swap_2bytes(1, &mhdr.file_type);
        nifti_swap_2bytes(1, &mhdr.num_frames);
        nifti_swap_4bytes(1, &mhdr.ecat_calibration_factor);
    }
    if ((mhdr.file_type < ECAT7_2DSCAN) || (mhdr.file_type > ECAT7_3DSCANFIT)) {
        printf("Unknown ECAT file type %d\n", mhdr.file_type);
        fclose(f);
        return EXIT_FAILURE;
    }
    if ((mhdr.num_frames > 1) || (mhdr.num_bed_pos > 1)) //to do: multi-volume files
        printf("Only reading first volume from ECAT file with %d frames and %d bed positions\n", mhdr.num_frames, mhdr.num_bed_pos);
    //read list matrix
    fseek(f, 512, SEEK_SET);
    ecat_list_hdr lhdr;
    fread(&lhdr, sizeof(lhdr), 1, f);
    if (*swapEndian) {
        nifti_swap_4bytes(128, &lhdr.hdr[0]);
    }
    //offset to first image
    int img1_StartBytes = lhdr.r01[1] * 512;
    //load image header for first image
    fseek(f, img1_StartBytes - 512, SEEK_SET); //image header is block immediately before image
    ecat_img_hdr ihdr;
    fread(&ihdr, sizeof(ihdr), 1, f);
    if (*swapEndian) {
        nifti_swap_2bytes(5, &ihdr.data_type);
        nifti_swap_4bytes(7, &ihdr.x_resolution);
        nifti_swap_4bytes(3, &ihdr.x_pixel_size);
    }
    if ((ihdr.data_type != ECAT7_BYTE) && (ihdr.data_type != ECAT7_SUNI2) && (ihdr.data_type != ECAT7_SUNI4)) {
        printf("Unknown or unsupported ECAT data type %d\n", ihdr.data_type);
        fclose(f);
        return EXIT_FAILURE;
    }
    //cm -> mm
    ihdr.x_pixel_size *= 10.0;
    ihdr.y_pixel_size *= 10.0;
    ihdr.z_pixel_size *= 10.0;
    fclose(f);
    nhdr->dim[0]=3;//3D
    nhdr->dim[1]=ihdr.x_dimension;
    nhdr->dim[2]=ihdr.y_dimension;
    nhdr->dim[3]=ihdr.z_dimension; //slices per volume
    nhdr->dim[4]=1; //volumes
    nhdr->pixdim[1]=ihdr.x_pixel_size;
    nhdr->pixdim[2]=ihdr.y_pixel_size;
    nhdr->pixdim[3]=ihdr.z_pixel_size; //TO DO: slice gap?
    nhdr->datatype = DT_INT16;
    if (ihdr.data_type == ECAT7_BYTE) nhdr->datatype = DT_UINT8;
    if (ihdr.data_type == ECAT7_SUNI4) nhdr->datatype = DT_INT32;
    nhdr->vox_offset = img1_StartBytes;
    nhdr->sform_code = NIFTI_XFORM_UNKNOWN; //NIFTI_XFORM_SCANNER_ANAT;
    nhdr->scl_slope = ihdr.scale_factor * mhdr.ecat_calibration_factor;
    //direct clone of spm_ecat2nifti: seems off by one (indexed from zero)
    vec3 start = setVec3((ihdr.x_dimension-2.0)/2.0*ihdr.x_pixel_size, (ihdr.y_dimension-2.0)/2.0*ihdr.y_pixel_size, (ihdr.z_dimension-2.0)/2.0*ihdr.z_pixel_size);
    nhdr->srow_x[0]=-nhdr->pixdim[1];nhdr->srow_x[1]=0.0f;nhdr->srow_x[2]=0.0f;nhdr->srow_x[3]=start.v[0];
    nhdr->srow_y[0]=0.0f;nhdr->srow_y[1]=-nhdr->pixdim[2];nhdr->srow_y[2]=0.0f;nhdr->srow_y[3]=start.v[1];
    nhdr->srow_z[0]=0.0f;nhdr->srow_z[1]=0.0f;nhdr->srow_z[2]=-nhdr->pixdim[3];nhdr->srow_z[3]=start.v[2];
    fclose(f);
    //convertForeignToNifti(nhdr);
    return EXIT_SUCCESS;
}



int  open_foreign (const char *fn){
	printf("--> %s\n", fn);
	struct nifti_1_header nhdr;
	bool swapEndian;
	int ret = nii_readEcat7(fn, &nhdr, &swapEndian);
    if (ret == EXIT_SUCCESS)
    	printf("!\n");

    return ret;
}// open_foreign()

