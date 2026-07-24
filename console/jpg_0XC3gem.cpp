#include "jpg_0XC3.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Internal Constants & Macros for Speed
// -----------------------------------------------------------------------------

#define MARKER_SOI 0xFFD8
#define MARKER_SOF3 0xFFC3 // Lossless Huffman
#define MARKER_DHT 0xFFC4
#define MARKER_SOS 0xFFDA
#define MARKER_DRI 0xFFDD
#define MARKER_EOI 0xFFD9

#define HUFF_LOOKAHEAD 8
#define HUFF_LUT_SIZE (1 << HUFF_LOOKAHEAD)

typedef struct {
	uint8_t bits[17];
	uint8_t huffval[256];
	uint16_t mincode[17];
	int32_t maxcode[18];
	int32_t valptr[17];
	// Fast lookup tables
	uint8_t lut_len[HUFF_LUT_SIZE];
	uint8_t lut_sym[HUFF_LUT_SIZE];
	int has_tables;
} HuffmanTable;

typedef struct {
	const uint8_t *data;
	size_t size;
	size_t pos;
} JpegStream;

// -----------------------------------------------------------------------------
// Stream Helper Functions (Inlined for speed where possible)
// -----------------------------------------------------------------------------

static uint8_t read_byte(JpegStream *stream) {
	if (stream->pos >= stream->size)
		return 0xFF; // Padding
	return stream->data[stream->pos++];
}

static uint16_t read_word(JpegStream *stream) {
	uint8_t b1 = read_byte(stream);
	uint8_t b2 = read_byte(stream);
	return (b1 << 8) | b2;
}

// -----------------------------------------------------------------------------
// Huffman Table Building
// -----------------------------------------------------------------------------

static void build_huffman_table(HuffmanTable *ht, const uint8_t *bits, const uint8_t *vals) {
	memcpy(ht->bits, bits, 16);
	int count = 0;
	for (int i = 0; i < 16; i++)
		count += bits[i];
	memcpy(ht->huffval, vals, count);

	int32_t huffcode[257];
	int huffsize[257];
	int p = 0;
	int code = 0;

	// Generate codes and sizes
	for (int l = 1; l <= 16; l++) {
		for (int i = 1; i <= bits[l - 1]; i++) {
			huffsize[p] = l;
			huffcode[p++] = code++;
		}
		code <<= 1;
	}
	huffsize[p] = 0;

	// Generate Min/Max/ValPtr for slow path
	int i = 0;
	int j = 0;
	for (int l = 1; l <= 16; l++) {
		if (bits[l - 1] == 0) {
			ht->maxcode[l] = -1;
		} else {
			ht->valptr[l] = j;
			ht->mincode[l] = huffcode[j];
			j += bits[l - 1];
			ht->maxcode[l] = huffcode[j - 1];
		}
		ht->maxcode[17] = 0xFFFFF; // Terminator
	}

	// Generate Fast LUT
	memset(ht->lut_len, 0, sizeof(ht->lut_len));
	for (i = 0; i < p; i++) {
		int len = huffsize[i];
		if (len <= HUFF_LOOKAHEAD) {
			int c = huffcode[i] << (HUFF_LOOKAHEAD - len);
			int num_entries = 1 << (HUFF_LOOKAHEAD - len);
			for (int k = 0; k < num_entries; k++) {
				ht->lut_len[c + k] = len;
				ht->lut_sym[c + k] = ht->huffval[i];
			}
		}
	}
	ht->has_tables = 1;
}

// -----------------------------------------------------------------------------
// Optimized Decoding Macros
// -----------------------------------------------------------------------------

// Refill the bit buffer. We aim to keep 'nbits' >= specified count.
// 'bitbuf' stores current bits, 'nbits' is count. 'src' is JpegStream.
// This handles the byte stuffing (0xFF 0x00 -> 0xFF).
#define REFILL_BITS(nbits_needed)                                                               \
	{                                                                                           \
		while (nbits < (nbits_needed)) {                                                        \
			uint8_t b = *stream.data;                                                           \
			stream.data++;                                                                      \
			stream.pos++;                                                                       \
			if (b == 0xFF) {                                                                    \
				uint8_t b2 = *stream.data;                                                      \
				if (b2 == 0x00) {                                                               \
					stream.data++;                                                              \
					stream.pos++;                                                               \
				} else { /* Marker detected, treat as padding here, handled by restart logic */ \
				}                                                                               \
			}                                                                                   \
			bitbuf = (bitbuf << 8) | b;                                                         \
			nbits += 8;                                                                         \
		}                                                                                       \
	}

// Peek N bits without consuming
#define PEEK_BITS(res, n)                                   \
	{                                                       \
		REFILL_BITS(n);                                     \
		res = (bitbuf >> (nbits - (n))) & ((1 << (n)) - 1); \
	}

// Consume N bits
#define DROP_BITS(n)  \
	{                 \
		nbits -= (n); \
	}

// Get N bits (consume)
#define GET_BITS(res, n)       \
	{                          \
		if ((n) > 0) {         \
			PEEK_BITS(res, n); \
			DROP_BITS(n);      \
		} else {               \
			res = 0;           \
		}                      \
	}

// Decode one Huffman symbol
// Uses LUT for speed, falls back to tree for long codes
#define DECODE_HUFFMAN(ht, result)                                            \
	{                                                                         \
		REFILL_BITS(HUFF_LOOKAHEAD);                                          \
		int idx = (bitbuf >> (nbits - HUFF_LOOKAHEAD)) & (HUFF_LUT_SIZE - 1); \
		int len = ht->lut_len[idx];                                           \
		if (len) {                                                            \
			DROP_BITS(len);                                                   \
			result = ht->lut_sym[idx];                                        \
		} else {                                                              \
			/* Slow path */                                                   \
			int code = 0;                                                     \
			len = 0;                                                          \
			for (int l = 1; l <= 16; l++) {                                   \
				GET_BITS(idx, 1);                                             \
				code = (code << 1) | idx;                                     \
				if (code <= ht->maxcode[l]) {                                 \
					int val_idx = ht->valptr[l] + (code - ht->mincode[l]);    \
					result = ht->huffval[val_idx];                            \
					break;                                                    \
				}                                                             \
			}                                                                 \
		}                                                                     \
	}

// -----------------------------------------------------------------------------
// Main Decoder
// -----------------------------------------------------------------------------

unsigned char *decode_JPEG_SOF_0XC3(const char *fn, int skipBytes, bool verbose, int *dimX, int *dimY, int *bits, int *frames, int diskBytes) {
	FILE *f = fopen(fn, "rb");
	if (!f)
		return NULL;

	// Read entire file (or limit to diskBytes if provided and non-zero)
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	if (diskBytes > 0 && diskBytes < fsize)
		fsize = diskBytes;

	// Allocate buffer for file content + padding for safety
	uint8_t *file_buf = (uint8_t *)malloc(fsize + 8);
	if (!file_buf) {
		fclose(f);
		return NULL;
	}

	fseek(f, 0, SEEK_SET);
	size_t bytes_read = fread(file_buf, 1, fsize, f);
	fclose(f);

	// Safety padding for bit reader
	memset(file_buf + bytes_read, 0, 8);

	JpegStream stream = {0};
	stream.data = file_buf;
	stream.size = bytes_read;
	stream.pos = skipBytes;

	// Decoding State
	int width = 0, height = 0, precision = 0, components = 0;
	int restart_interval = 0;
	HuffmanTable huff_tables[4]; // DC tables 0..3
	memset(huff_tables, 0, sizeof(huff_tables));

	int predictor = 0, point_transform = 0;

	// Pointers to active tables for components
	HuffmanTable *cur_ht[4] = {NULL, NULL, NULL, NULL};

	// Parse Headers
	while (stream.pos < stream.size) {
		// Find 0xFF
		while (stream.pos < stream.size && stream.data[stream.pos] != 0xFF)
			stream.pos++;
		if (stream.pos >= stream.size - 1)
			break;

		uint8_t m = stream.data[stream.pos + 1];
		stream.pos += 2; // Skip Marker

		if (m == 0xD8)
			continue; // SOI
		if (m == 0xD9)
			break; // EOI

		if (m == 0xC3) { // SOF3
			uint16_t len = read_word(&stream);
			precision = read_byte(&stream);
			height = read_word(&stream);
			width = read_word(&stream);
			components = read_byte(&stream);

			// Skip component specifics in SOF (quant table selectors usually 0 for lossless)
			stream.pos += (components * 3);

			if (dimX)
				*dimX = width;
			if (dimY)
				*dimY = height;
			if (bits)
				*bits = precision;
			if (frames)
				*frames = components;

			if (verbose)
				printf("SOF3: %dx%d %d-bit %d channels\n", width, height, precision, components);
		} else if (m == 0xC4) { // DHT
			uint16_t len = read_word(&stream);
			uint16_t end = stream.pos + len - 2;
			while (stream.pos < end) {
				uint8_t info = read_byte(&stream);
				int tc = (info >> 4) & 0x0F; // Table class (0=DC, 1=AC) - Lossless uses DC
				int id = info & 0x0F;		 // Table ID

				uint8_t bits_list[16];
				uint8_t vals_list[256];

				for (int i = 0; i < 16; i++)
					bits_list[i] = read_byte(&stream);
				int count = 0;
				for (int i = 0; i < 16; i++)
					count += bits_list[i];
				for (int i = 0; i < count; i++)
					vals_list[i] = read_byte(&stream);

				if (tc == 0 && id < 4) { // Only care about DC tables
					build_huffman_table(&huff_tables[id], bits_list, vals_list);
				}
			}
		} else if (m == 0xDD) { // DRI
			read_word(&stream); // length
			restart_interval = read_word(&stream);
		} else if (m == 0xDA) { // SOS - Start of Scan
			uint16_t len = read_word(&stream);
			int ns = read_byte(&stream); // Num components in scan

			for (int i = 0; i < ns; i++) {
				read_byte(&stream); // Comp ID
				uint8_t td_ta = read_byte(&stream);
				int dc_tbl = (td_ta >> 4) & 0x0F;
				// Assign table to component
				cur_ht[i] = &huff_tables[dc_tbl];
			}

			predictor = read_byte(&stream); // Ss start of spectral selection = Predictor in lossless
			read_byte(&stream);				// Se
			uint8_t ah_al = read_byte(&stream);
			point_transform = ah_al & 0x0F; // Al = Point transform

			// Start Decompression
			break;
		} else {
			// Skip other markers
			uint16_t len = read_word(&stream);
			stream.pos += len - 2;
		}
	}

	if (width == 0 || height == 0) {
		free(file_buf);
		return NULL;
	}

	// Allocate output (Raw image data)
	// DICOM 16-bit is usually stored as unsigned short (2 bytes), even if actual bits stored < 16
	size_t out_size = (size_t)width * height * components * 2;
	uint16_t *img_data = (uint16_t *)malloc(out_size);
	if (!img_data) {
		free(file_buf);
		return NULL;
	}

	// -------------------------------------------------------------------------
	// THE HOT LOOP
	// -------------------------------------------------------------------------

	// Local bit accumulator for speed
	uint32_t bitbuf = 0;
	int nbits = 0;

	// Pointers for current scan line and previous scan line
	uint16_t *prev_row = NULL;
	uint16_t *curr_row = img_data;

	// Initial prediction value (Annex H)
	// "At the beginning of the first line and beginning of each restart interval... 2^(P-Pt-1)"
	int P = precision;
	int Pt = point_transform;
	int start_val = 1 << (P - Pt - 1);

	int restart_count = restart_interval;

	// Adjust JpegStream to point inside local buffer logic
	// We update stream.pos manually when we refill buffer

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {

			// Handle Restart Intervals
			if (restart_interval > 0 && restart_count == 0) {
				// Byte align
				nbits &= ~7; // discard remaining bits
				// Look for marker
				while (1) {
					uint8_t b = stream.data[stream.pos];
					if (b == 0xFF) {
						uint8_t m = stream.data[stream.pos + 1];
						if (m >= 0xD0 && m <= 0xD7) {
							stream.pos += 2;
							break;
						}
						stream.pos++; // not a restart marker, keep looking (rare)
					} else {
						stream.pos++;
					}
				}
				restart_count = restart_interval;
				// Reset prediction state logic: Handled by checking x==0 logic below mostly,
				// but specific reset happens naturally at x=0
				bitbuf = 0;
				nbits = 0; // Reset bit buffer
			}

			for (int c = 0; c < components; c++) {
				HuffmanTable *ht = cur_ht[c];
				int s, diff;

				// 1. Decode Huffman Symbol (Difference Category)
				DECODE_HUFFMAN(ht, s);

				// 2. Decode Difference Value
				if (s == 0) {
					diff = 0;
				} else if (s == 16) {
					diff = 32768;
				} else {
					int extra;
					GET_BITS(extra, s);
					if (extra < (1 << (s - 1))) {
						diff = extra + (-1 << s) + 1;
					} else {
						diff = extra;
					}
				}

				// 3. Predictor Logic (Annex H)
				int pred;

				if (x == 0 && y == 0) {
					// Very first pixel
					pred = start_val;
				} else if (x == 0) {
					// First pixel of a row (use Rb i.e., pixel above)
					// Note: Restart interval logic essentially treats next pixel as x=0, y=0 context
					// But standard JPEG structure usually aligns restarts to MCUs.
					// In Lossless, MCU is one sample.
					// If we just restarted, we usually default to start_val logic again?
					// Annex H says: "At the beginning of the first line AND the beginning of each restart interval the prediction value is..."
					if (restart_interval > 0 && restart_count == restart_interval) {
						pred = start_val;
					} else {
						// Predictor 2 (Rb) is used for the start of lines (Annex H)
						pred = prev_row[x * components + c];
					}
				} else if (y == 0) {
					// First row, not first pixel (use Ra i.e., pixel left)
					if (restart_interval > 0 && restart_count == restart_interval) {
						pred = start_val;
					} else {
						// Predictor 1 (Ra) used for first line
						pred = curr_row[(x - 1) * components + c];
					}
				} else {
					// Inner loop neighbors
					int Ra = curr_row[(x - 1) * components + c];
					int Rb = prev_row[x * components + c];
					int Rc = prev_row[(x - 1) * components + c];

					if (restart_interval > 0 && restart_count == restart_interval) {
						pred = start_val;
					} else {
						switch (predictor) {
						case 1:
							pred = Ra;
							break;
						case 2:
							pred = Rb;
							break;
						case 3:
							pred = Rc;
							break;
						case 4:
							pred = Ra + Rb - Rc;
							break;
						case 5:
							pred = Ra + ((Rb - Rc) >> 1);
							break;
						case 6:
							pred = Rb + ((Ra - Rc) >> 1);
							break;
						case 7:
							pred = (Ra + Rb) >> 1;
							break;
						default:
							pred = Ra;
							break; // Fallback
						}
					}
				}

				// 4. Reconstruct
				// Modulo 65536 arithmetic (implicitly handled by int math -> uint16_t cast)
				uint16_t result = (uint16_t)(pred + diff);

				// 5. Store & Point Transform
				// The stored value is the prediction residual. DICOM standard says output = (pred + diff) << Pt
				curr_row[x * components + c] = result << Pt;
			}

			if (restart_interval > 0)
				restart_count--;
		}

		// Advance row pointers
		prev_row = curr_row;
		curr_row += width * components;
	}

	free(file_buf);

	// Return raw buffer cast to char*
	return (unsigned char *)img_data;
}