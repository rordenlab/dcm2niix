// Reassemble a SINGLE-FRAME multi-fragment encapsulated DICOM pixel data into a contiguous codec bitstream.
//
// In encapsulated transfer syntaxes (JPEG Lossless 1.2.840.10008.1.2.4.70 / 57, JPEG-LS, JPEG2000, RLE, ...), the
// codec bitstream of one frame can be split across multiple (FFFE,E000) Item delimiters. This helper concatenates
// EVERY following fragment, starting at the first fragment's payload offset, until it sees the Sequence Delimitation
// Item (FFFE,E0DD), EOF, or a non-Item tag. It DOES NOT know where one frame ends and the next begins.
//
// CONTRACT: callers must guarantee the input is single-frame (e.g. numberOfFrames <= 1). For multi-frame encapsulated
// data with multiple fragments per frame, this helper would silently mix frames; the parser gate in nii_dicom.cpp
// rejects that layout. See issue #1017.
//
// Returns a malloc'd buffer holding the concatenated codec bitstream (caller frees) with `*outLen` set to its byte
// length. Returns NULL when the file actually holds a single fragment (caller should decode the original file with
// imageStart unchanged) OR on I/O error / malformed structure. The NULL-vs-error overload is acknowledged technical
// debt; a future tri-state API would distinguish them.
#ifndef _DICOM_FRAGMENTS_H_
#define _DICOM_FRAGMENTS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned char *reassembleEncapsulatedFragments(const char *fn, int firstFragmentDataOffset, size_t *outLen);

#ifdef __cplusplus
}
#endif

#endif // _DICOM_FRAGMENTS_H_
