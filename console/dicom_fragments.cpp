// Reassembly of multi-fragment encapsulated DICOM pixel data. See dicom_fragments.h.

#include "dicom_fragments.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Read a 32-bit little-endian item length from an 8-byte item header.
// Item header layout: tag (2 bytes, little-endian) + element (2 bytes, little-endian) + length (4 bytes, LE).
// FFFE,E000 little-endian on disk = FE FF 00 E0
static inline int isFragmentItem(const unsigned char *hdr) {
	return (hdr[0] == 0xFE) && (hdr[1] == 0xFF) && (hdr[2] == 0x00) && (hdr[3] == 0xE0);
}

static inline uint32_t itemLength(const unsigned char *hdr) {
	return (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
}

unsigned char *reassembleEncapsulatedFragments(const char *fn, int firstFragmentDataOffset, size_t *outLen) {
	if (outLen != NULL)
		*outLen = 0;
	if (fn == NULL)
		return NULL;
	FILE *src = fopen(fn, "rb");
	if (src == NULL)
		return NULL;
	// Bound `total` against the actual file length so a malformed declared `len` cannot cause us to allocate gigabytes before discovering the file is shorter.
	if (fseek(src, 0, SEEK_END) != 0) {
		fclose(src);
		return NULL;
	}
	long flen = ftell(src);
	if (flen < 0) {
		fclose(src);
		return NULL;
	}
	// firstFragmentDataOffset points at the first fragment's payload (matches dcm.imageStart); back up 8 to the item header.
	long fragHdrPos = (long)firstFragmentDataOffset - 8;
	if (fragHdrPos < 0 || fseek(src, fragHdrPos, SEEK_SET) != 0) {
		fclose(src);
		return NULL;
	}
	// Pass 1: count fragments and total payload bytes. Stops on FFFE,E0DD (Sequence Delimitation Item), EOF, or non-item tag. Per-fragment position+length must fit inside the file, AND overall total must not overflow size_t; both before allocation so a malformed declared length cannot trick us into a giant malloc only to fail in pass 2.
	int fragCount = 0;
	size_t total = 0;
	unsigned char hdr[8];
	while (fread(hdr, 1, 8, src) == 8) {
		if (!isFragmentItem(hdr))
			break;
		uint32_t len = itemLength(hdr);
		long payloadStart = ftell(src);
		if (payloadStart < 0) {
			fclose(src);
			return NULL;
		}
		// Position-based bound: this fragment's payload must end at or before EOF. Reject malformed item-length declarations before they inflate `total`.
		if ((long)len > flen - payloadStart) {
			fclose(src);
			return NULL;
		}
		// Addition-overflow guard on the running total (defence on 32-bit / malformed sums).
		if (total > ((size_t)flen - len)) {
			fclose(src);
			return NULL;
		}
		fragCount++;
		total += len;
		if (len > 0 && fseek(src, (long)len, SEEK_CUR) != 0)
			break;
	}
	if (fragCount <= 1 || total == 0) {
		fclose(src);
		return NULL; // single fragment (or empty): caller decodes original file
	}
	// Pass 2: allocate, rewind, copy payloads (stripping item headers).
	unsigned char *out = (unsigned char *)malloc(total);
	if (out == NULL) {
		fclose(src);
		return NULL;
	}
	if (fseek(src, fragHdrPos, SEEK_SET) != 0) {
		free(out);
		fclose(src);
		return NULL;
	}
	size_t written = 0;
	while (fread(hdr, 1, 8, src) == 8) {
		if (!isFragmentItem(hdr))
			break;
		uint32_t len = itemLength(hdr);
		if (len == 0)
			continue;
		if (written + len > total) {
			free(out);
			fclose(src);
			return NULL;
		}
		if (fread(out + written, 1, len, src) != len) {
			free(out);
			fclose(src);
			return NULL;
		}
		written += len;
	}
	fclose(src);
	if (written != total) {
		free(out);
		return NULL;
	}
	if (outLen != NULL)
		*outLen = total;
	return out;
}
