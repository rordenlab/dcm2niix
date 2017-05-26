#include "nii_foreign.h"
#include "nii_dicom.h"
#include "nifti1_io_core.h"
#include "nii_dicom_batch.h"
#include "nifti1.h"
//#include "nifti1_io_core.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "print.h"
#ifdef _MSC_VER
	#include <direct.h>
	#define getcwd _getcwd
	#define chdir _chrdir
	#include "io.h"
	#include <math.h>
	//#define snprintMessage _snprintMessage
	//#define vsnprintMessage _vsnprintMessage
	#define strcasecmp _stricmp
	#define strncasecmp _strnicmp
#else
	#include <unistd.h>
#endif

#ifndef Float32
	#define Float32 float
#endif
#ifndef uint32
	#define uint32 uint32_t
#endif

#ifdef __GNUC__
    #define PACK(...) __VA_ARGS__ __attribute__((__packed__))
#else
    #define PACK(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
#endif


/*nii_readEcat7 2017 by Chris Rorden, BSD license
 http://www.turkupetcentre.net/software/libdoc/libtpcimgio/ecat7_8h_source.html#l00060
 http://www.turkupetcentre.net/software/libdoc/libtpcimgio/ecat7r_8c_source.html#l00717
 http://xmedcon.sourcearchive.com/documentation/0.10.7-1build1/ecat7_8h_source.html
 https://github.com/BIC-MNI/minc-tools/tree/master/conversion/ecattominc
 https://github.com/nipy/nibabel/blob/ec4567fb09b4472c5a4bb9a13dbcc9eb0a63d875/nibabel/ecat.py
*/

void strClean(char * cString) {
	int len = (int)strlen(cString);
	if (len < 1) return;
	for (int i = 0; i < len; i++) {
		char c = cString[i];
		if ( (c < char(32)) || (c == char(127)) || (c == char(255)) ) cString[i] = 0;
		if ( (c==' ') || (c==',') || (c=='^') || (c=='/') || (c=='\\')  || (c=='%') || (c=='*')) cString[i] = '_';
	}
}

unsigned char * readEcat7(const char *fname, struct TDICOMdata *dcm, struct nifti_1_header *hdr, struct TDCMopts opts, bool isWarnIfNotEcat) {
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
    PACK( typedef struct  {
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
    }) ecat_main_hdr;
PACK( typedef struct {
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
}) ecat_img_hdr;
    PACK( typedef struct  {
        int32_t hdr[4], r[31][4];
    }) ecat_list_hdr;
    bool swapEndian = false;
    size_t n;
    FILE *f;
    ecat_main_hdr mhdr;
    f = fopen(fname, "rb");
    if (f)
        n = fread(&mhdr, sizeof(mhdr), 1, f);
    if(!f || n!=1) {
        printMessage("Problem reading ECAT7 file!\n");
        fclose(f);
        return NULL;
    }
    if ((mhdr.magic[0] != 'M') || (mhdr.magic[1] != 'A') || (mhdr.magic[2] != 'T')
        || (mhdr.magic[3] != 'R') || (mhdr.magic[4] != 'I') || (mhdr.magic[5] != 'X') ) {
        if (isWarnIfNotEcat)
        	printMessage("Signature not 'MATRIX' (ECAT7): '%s'\n", fname);
        fclose(f);
        return NULL;
    }
    swapEndian = mhdr.file_type > 255;
    if (swapEndian) {
        nifti_swap_2bytes(2, &mhdr.sw_version);
        nifti_swap_2bytes(1, &mhdr.file_type);
        //nifti_swap_2bytes(1, &mhdr.num_frames);
        nifti_swap_4bytes(1, &mhdr.ecat_calibration_factor);
        nifti_swap_4bytes(1, &mhdr.isotope_halflife);
        nifti_swap_4bytes(2, &mhdr.dosage);
    }
    if ((mhdr.file_type < ECAT7_2DSCAN) || (mhdr.file_type > ECAT7_3DSCANFIT)) {
        printMessage("Unknown ECAT file type %d\n", mhdr.file_type);
        fclose(f);
        return NULL;
    }
    //read list matrix
    ecat_list_hdr lhdr;
    fseek(f, 512, SEEK_SET);
    fread(&lhdr, sizeof(lhdr), 1, f);
    if (swapEndian) nifti_swap_4bytes(128, &lhdr.hdr[0]);
    //offset to first image
    int img_StartBytes = lhdr.r[0][1] * 512;
    //load image header for first image
    fseek(f, img_StartBytes - 512, SEEK_SET); //image header is block immediately before image
    ecat_img_hdr ihdr;
    fread(&ihdr, sizeof(ihdr), 1, f);
    if (swapEndian) {
    	nifti_swap_2bytes(5, &ihdr.data_type);
        nifti_swap_4bytes(5, &ihdr.x_offset);
        nifti_swap_2bytes(2, &ihdr.image_min);
        nifti_swap_4bytes(5, &ihdr.x_pixel_size);
        nifti_swap_2bytes(1, &ihdr.filter_code);
        nifti_swap_4bytes(14, &ihdr.x_resolution);
        nifti_swap_2bytes(1, &ihdr.filter_order);
        nifti_swap_4bytes(2, &ihdr.filter_scatter_fraction);
        nifti_swap_4bytes(11, &ihdr.mtx);
        nifti_swap_2bytes(2, &ihdr.rfilter_code);
        nifti_swap_4bytes(2, &ihdr.zfilter_cutoff);
        nifti_swap_2bytes(2, &ihdr.zfilter_code);
        nifti_swap_4bytes(3, &ihdr.mtx_1_4);
        nifti_swap_2bytes(3, &ihdr.scatter_type);
    }
    if ((ihdr.data_type != ECAT7_BYTE) && (ihdr.data_type != ECAT7_SUNI2) && (ihdr.data_type != ECAT7_SUNI4)) {
        printMessage("Unknown or unsupported ECAT data type %d\n", ihdr.data_type);
        fclose(f);
        return NULL;
    }
    int bytesPerVoxel = 2;
	if (ihdr.data_type == ECAT7_BYTE) bytesPerVoxel = 1;
    if (ihdr.data_type == ECAT7_SUNI4) bytesPerVoxel = 4;
    //next: read offsets for each volume: data not saved sequentially (each volume preceded by its own ecat_img_hdr)
    int num_vol = 0;
    bool isAbort = false;
    bool isScaleFactorVaries = false;
    #define kMaxVols 16000
	size_t * imgOffsets = (size_t *)malloc(sizeof(size_t) * (kMaxVols));
    float * imgSlopes = (float *)malloc(sizeof(float) * (kMaxVols));
    ecat_img_hdr ihdrN;
    while ((lhdr.hdr[0]+lhdr.hdr[3]) == 31) { //while valid list
    	if (num_vol > 0) { //read the next list
    		fseek(f, 512 * (lhdr.hdr[1] -1), SEEK_SET);
    		fread(&lhdr, 512, 1, f);
    		if (swapEndian) nifti_swap_4bytes(128, &lhdr.hdr[0]);
    	}
		if ((lhdr.hdr[0]+lhdr.hdr[3]) != 31) break; //if valid list
		if (lhdr.hdr[3] < 1) break;
		for (int k = 0; k < lhdr.hdr[3]; k++) {
    		//check images' ecat_img_hdr matches first
    		fseek(f, (lhdr.r[k][1]-1) * 512, SEEK_SET); //image header is block immediately before image
    		fread(&ihdrN, sizeof(ihdrN), 1, f);
    		if (swapEndian) {
				nifti_swap_2bytes(5, &ihdrN.data_type);
				nifti_swap_4bytes(5, &ihdrN.x_offset);
				nifti_swap_2bytes(2, &ihdrN.image_min);
				nifti_swap_4bytes(5, &ihdrN.x_pixel_size);
				nifti_swap_2bytes(1, &ihdrN.filter_code);
				nifti_swap_4bytes(14, &ihdrN.x_resolution);
				nifti_swap_2bytes(1, &ihdrN.filter_order);
				nifti_swap_4bytes(2, &ihdrN.filter_scatter_fraction);
				nifti_swap_4bytes(11, &ihdrN.mtx);
				nifti_swap_2bytes(2, &ihdrN.rfilter_code);
				nifti_swap_4bytes(2, &ihdrN.zfilter_cutoff);
				nifti_swap_2bytes(2, &ihdrN.zfilter_code);
				nifti_swap_4bytes(3, &ihdrN.mtx_1_4);
				nifti_swap_2bytes(3, &ihdrN.scatter_type);
			}
    		if (ihdr.scale_factor != ihdrN.scale_factor)
    			isScaleFactorVaries = true;
    		if ((ihdr.data_type != ihdrN.data_type) || (ihdr.x_dimension != ihdrN.x_dimension) || (ihdr.y_dimension != ihdrN.y_dimension) || (ihdr.z_dimension != ihdrN.z_dimension)) {
    			printError("Error: ECAT volumes have varying image dimensions\n");
    			isAbort = true;
    		}
    		if (num_vol < kMaxVols) {
    			imgOffsets[num_vol]	= lhdr.r[k][1];
    			imgSlopes[num_vol] = ihdrN.scale_factor;
    		}
    		num_vol ++;
    	}
    	if ((lhdr.hdr[0] > 0) || (isAbort)) break; //this list contains empty volumes: all lists have been read
    } //read all image offsets
    //report error reading image offsets
    if ((num_vol < 1) || (isAbort) || (num_vol >= kMaxVols)) {
        printMessage("Failure to extract ECAT7 images\n");
        if (num_vol >= kMaxVols) printMessage("Increase kMaxVols");
        fclose(f);
        free (imgOffsets);
        free(imgSlopes);
        return NULL;
    }
    if ((isScaleFactorVaries) && (bytesPerVoxel != 2)) {
    	printError("ECAT scale factor varies between volumes (check for updates) '%s'\n", fname);
        fclose(f);
        free (imgOffsets);
        free(imgSlopes);
        return NULL;
	}
	//load image data
	unsigned char * img = NULL;
	if ((isScaleFactorVaries) && (bytesPerVoxel == 2)) { //we need to convert volumes from 16-bit to 32-bit to preserve scaling factors
		int num_vox = ihdr.x_dimension * ihdr.y_dimension * ihdr.z_dimension;
		size_t bytesPerVolumeIn = num_vox * bytesPerVoxel; //bytesPerVoxel == 2
		unsigned char * imgIn = (unsigned char*)malloc(bytesPerVolumeIn);
		int16_t * img16i = (int16_t*) imgIn;
		bytesPerVoxel = 4;
		size_t bytesPerVolume = num_vox * bytesPerVoxel;
		img = (unsigned char*)malloc(bytesPerVolume * num_vol);
		float * img32 = (float*) img;
		for (int v = 0; v < num_vol; v++) {
			fseek(f, imgOffsets[v] * 512, SEEK_SET);
			fread( &imgIn[0], 1, bytesPerVolumeIn, f);
			if (swapEndian)
				nifti_swap_2bytes(num_vox, imgIn);
			int volOffset = v * num_vox;
			float scale = imgSlopes[v] * mhdr.ecat_calibration_factor;
			for (int i = 0; i < num_vox; i++)
				img32[i+volOffset] = (img16i[i] * scale);
		}
		//we have applied the scale factors to the data, so eliminate them
		ihdr.scale_factor = 1.0;
		mhdr.ecat_calibration_factor = 1.0;

	} else { //if isScaleFactorVaries else simple conversion
		size_t bytesPerVolume = ihdr.x_dimension * ihdr.y_dimension * ihdr.z_dimension * bytesPerVoxel;
		img = (unsigned char*)malloc(bytesPerVolume * num_vol);
		for (int v = 0; v < num_vol; v++) {
			fseek(f, imgOffsets[v] * 512, SEEK_SET);
			size_t  sz = fread( &img[v * bytesPerVolume], 1, bytesPerVolume, f);
            if (sz != bytesPerVolume) {
                free(img);
                return NULL;
            }
		}
		if ((swapEndian) && (bytesPerVoxel == 2)) nifti_swap_2bytes(ihdr.x_dimension * ihdr.y_dimension * ihdr.z_dimension * num_vol, img);
		if ((swapEndian) && (bytesPerVoxel == 4)) nifti_swap_4bytes(ihdr.x_dimension * ihdr.y_dimension * ihdr.z_dimension * num_vol, img);
	}
	printWarning("ECAT support VERY experimental (Spatial transforms unknown)\n");
    free (imgOffsets);
    free(imgSlopes);
    fclose(f);
    //fill DICOM header
    float timeBetweenVolumes = ihdr.frame_duration;
    if (num_vol > 1)
    	timeBetweenVolumes = (float)(ihdrN.frame_start_time- ihdr.frame_start_time)/(float)(num_vol-1);
    //copy and clean strings (ECAT can use 0x0D as a string terminator)
    strncpy(dcm->patientName, mhdr.patient_name, 32);
    strncpy(dcm->patientID, mhdr.patient_id, 16);
    strncpy(dcm->seriesDescription, mhdr.study_description, 32);
    strncpy(dcm->protocolName, mhdr.study_type, 12);
    strncpy(dcm->imageComments, mhdr.isotope_name, 8);
	strncpy(dcm->procedureStepDescription, mhdr.radiopharmaceutical, 32);
	strClean(dcm->patientName);
	strClean(dcm->patientID);
	strClean(dcm->seriesDescription);
	strClean(dcm->protocolName);
	strClean(dcm->imageComments);
	strClean(dcm->procedureStepDescription);
	dcm->ecat_dosage = mhdr.dosage;
	dcm->ecat_isotope_halflife = mhdr.isotope_halflife;
    if (opts.isVerbose) {
    	printMessage("ECAT7 details for '%s'\n", fname);
    	printMessage(" Software version %d\n", mhdr.sw_version);
    	printMessage(" System Type %d\n", mhdr.system_type);
    	printMessage(" Frame duration %dms\n", ihdr.frame_duration);
    	printMessage(" Time between volumes %gms\n", timeBetweenVolumes );
    	printMessage(" Patient name '%s'\n", dcm->patientName);
    	printMessage(" Patient ID '%s'\n", dcm->patientID);
    	printMessage(" Study description '%s'\n", dcm->seriesDescription);
    	printMessage(" Study type '%s'\n", dcm->protocolName);
    	printMessage(" Isotope name '%s'\n", dcm->imageComments);
    	printMessage(" Isotope halflife %gs\n", mhdr.isotope_halflife);
    	printMessage(" Radiopharmaceutical '%s'\n", dcm->procedureStepDescription);
    	printMessage(" Dosage %gbequerels/cc\n", mhdr.dosage);
    	if (!isScaleFactorVaries) {
    		printMessage(" Scale factor %12.12g\n", ihdr.scale_factor);
    		printMessage(" ECAT calibration factor %8.12g\n", mhdr.ecat_calibration_factor);
    	}
    	printMessage(" NIfTI scale slope %12.12g\n",ihdr.scale_factor * mhdr.ecat_calibration_factor);
    }
	dcm->manufacturer = kMANUFACTURER_SIEMENS;
	//dcm->manufacturersModelName = itoa(mhdr.system_type);
	sprintf(dcm->manufacturersModelName, "%d", mhdr.system_type);
    dcm->bitsAllocated = bytesPerVoxel * 8;
    if (isScaleFactorVaries) dcm->isFloat = true;
    dcm->bitsStored = 15; //ensures 16-bit images saved as INT16 not UINT16
	dcm->samplesPerPixel = 1;
	dcm->xyzMM[1] = ihdr.x_pixel_size * 10.0; //cm -> mm
	dcm->xyzMM[2] = ihdr.y_pixel_size * 10.0; //cm -> mm
	dcm->xyzMM[3] = ihdr.z_pixel_size * 10.0; //cm -> mm
	dcm->TR = timeBetweenVolumes;
	dcm->xyzDim[1] = ihdr.x_dimension;
	dcm->xyzDim[2] = ihdr.y_dimension;
	dcm->xyzDim[3] = ihdr.z_dimension;
    dcm->xyzDim[4] = num_vol;
    //create a NIfTI header
	headerDcm2Nii(*dcm, hdr, false);
	//here we mimic SPM's spatial starting estimate SForm
	mat44 m44;
	LOAD_MAT44(m44, -hdr->pixdim[1], 0.0f, 0.0f, ((float)dcm->xyzDim[1]-2.0)/2.0*dcm->xyzMM[1],
		0.0f, -hdr->pixdim[2], 0.0f, ((float)dcm->xyzDim[2]-2.0)/2.0*dcm->xyzMM[2],
		0.0f, 0.0f, -hdr->pixdim[3], ((float)dcm->xyzDim[3]-2.0)/2.0*dcm->xyzMM[3]);
	setQSForm(hdr, m44, false);
	//make sure image does not include a spatial matrix
	bool isMatrix = false;
	for (int i = 0; i < 9; i++)
		if (ihdr.mtx[i] != 0.0) isMatrix = true;
	if (isMatrix)
		printWarning("ECAT volume appears to store spatial transformation matrix (please check for updates)\n");
	hdr->scl_slope = ihdr.scale_factor * mhdr.ecat_calibration_factor;
    if (mhdr.gantry_tilt != 0.0) printMessage("Warning: ECAT gantry tilt not supported %g\n", mhdr.gantry_tilt);
    return img;
}

int  convert_foreign (const char *fn, struct TDCMopts opts){
	struct nifti_1_header hdr;
	struct TDICOMdata dcm = clear_dicom_data();
	unsigned char * img = NULL;
	img = readEcat7(fn, &dcm, &hdr, opts, false); //false: silent, do not report if file is not ECAT format
	if (!img) return EXIT_FAILURE;
	char niiFilename[1024];
	int ret = nii_createFilename(dcm, niiFilename, opts);
	printMessage("Saving ECAT as '%s'\n", niiFilename);
	if (ret != EXIT_SUCCESS) return ret;
	struct TDTI4D dti4D;
	dti4D.S[0].sliceTiming = -1.0;
	nii_SaveBIDS(niiFilename, dcm, opts, &dti4D, &hdr, fn);
	ret = nii_saveNII(niiFilename, hdr, img, opts);
	free(img);
    return ret;
}// convert_foreign()
