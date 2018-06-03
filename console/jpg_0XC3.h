//Decode DICOM Transfer Syntax 1.2.840.10008.1.2.4.70 and 1.2.840.10008.1.2.4.57
// JPEG Lossless, Nonhierarchical
// see ISO/IEC 10918-1 / ITU T.81
//  specifically, format with 'Start of Frame' (SOF) code 0xC3
//  http://www.w3.org/Graphics/JPEG/itu-t81.pdf
// This code decodes data with 1..16 bits per pixel
// It appears unique to medical imaging, and is not supported by most JPEG libraries
// http://www.dicomlibrary.com/dicom/transfer-syntax/
// https://en.wikipedia.org/wiki/Lossless_JPEG#Lossless_mode_of_operation
#ifndef _JPEG_SOF_0XC3_
#define _JPEG_SOF_0XC3_

#ifdef  __cplusplus
extern "C" {
#endif

unsigned char *  decode_JPEG_SOF_0XC3 (const char *fn, int skipBytes, bool verbose, int *dimX, int *dimY, int *bits, int *frames, int diskBytes);

#ifdef  __cplusplus
}
#endif

#endif