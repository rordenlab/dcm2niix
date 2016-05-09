#include <stdint.h>


#ifndef nii_otsu_h
#define nii_otsu_h

#ifdef  __cplusplus
extern "C" {
#endif

    void maskBackground  ( 	uint8_t *img8bit, int lXi, int lYi, int lZi, int lOtsuLevels, float lDilateVox, bool lOneContiguousObject);
	void maskBackground16  (short *img16bit, int lXi, int lYi, int lZi, int lOtsuLevels, float lDilateVox, bool lOneContiguousObject);
	void maskBackgroundU16  (unsigned short *img16bit, int lXi, int lYi, int lZi, int lOtsuLevels, float lDilateVox, bool lOneContiguousObject);

#ifdef  __cplusplus
}
#endif

#endif