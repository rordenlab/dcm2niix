/*
 * huffman_bitreader.h  (with optional fast lookup)
 *
 * Header-only combined JPEG-aware bitreader and canonical Huffman decoder.
 *
 * This version adds an optional fast prefix lookup (HB_FAST_LOOKUP_BITS, default 9).
 * See the comment above for usage and tradeoffs.
 *
 * Based on the user's header in the workspace. See: :contentReference[oaicite:2]{index=2}
 */

#ifndef HUFFMAN_BITREADER_H
#define HUFFMAN_BITREADER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public error codes */
#define HB_OK 0
#define HB_ERR_EOF -1
#define HB_ERR_MARKER -2
#define HB_ERR_BADARG -3
#define HB_ERR_NOMEM -4
#define HB_ERR_FORMAT -5

/* Fast lookup width (tuneable). Default 9 -> 512 entries -> 2048 bytes per table (int32_t entries). */
#ifndef HB_FAST_LOOKUP_BITS
#define HB_FAST_LOOKUP_BITS 9
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Simple DataStream helpers (used by convenience DHT parser if desired) */
typedef struct {
	const uint8_t *buf;
	size_t len;
	size_t idx;
} DataStream;

/* BitReader structure */
typedef struct {
	const uint8_t *buf;
	size_t len;
	size_t idx;
	uint32_t bitbuf;
	int bits_in_buf;
	int marker;
} BitReader;

/* Frame / Scan headers (minimal) */
typedef struct {
	int acTabSel;
	int dcTabSel;
	int scanCompSel;
} ScanComponent;

/* Huffman table (canonical representation) */
typedef struct {
	uint16_t min_code[17];
	uint16_t max_code[17];
	int val_ptr[17];
	uint8_t *vals;
	int num_vals;

	/* fast lookup */
	int fast_bits; /* HB_FAST_LOOKUP_BITS or 0 if unused */
	int fast_size; /* 1<<fast_bits */
	int32_t *fast; /* array[fast_size] storing (len<<24)|symbol or -1 if no match */
} HuffTable;

typedef struct {
	int hSamp;
	int quantTableSel;
	int vSamp;
} ComponentSpec;

typedef struct {
	int xDim, yDim, numComp, precision;
	// components array (index 1..numComp) allocated when needed
	ComponentSpec *components;
} FrameHeader;

typedef struct {
	int numComp;
	int selection, spectralEnd, ah, al;
	ScanComponent *components; // allocated length numComp
} ScanHeader;

/* Minimal decoder state */
typedef struct {
	DataStream ds;
	FrameHeader frame;
	ScanHeader scan;
	HuffTable huffTables[4][2]; // t (0..3) x class (0=DC,1=AC)
	BitReader br;

	int xDim, yDim;
	int xLoc, yLoc;
	int precision;
	int numComp;
	uint16_t *output; // 16-bit per sample
	int selection;
} Decoder;

/* BitReader API (declarations) */
static inline void br_init(BitReader *br, const uint8_t *buf, size_t len, size_t start_idx);
static inline int br_get_bits(BitReader *br, int n, uint32_t *out);
static inline int br_get_bit(BitReader *br, int *out_bit);
static inline void br_byte_align(BitReader *br);
static inline int br_get_marker(BitReader *br, uint16_t *marker_out);
static inline int br_read16_be(BitReader *br, uint16_t *out16);

/* Huffman API (declarations) */
static inline int hb_huff_build_from_dht(HuffTable *h, const uint8_t bits[17], const uint8_t *vals, int num_vals);
static inline int hb_huff_build_from_dht_block(HuffTable *h, const uint8_t *dht_bits16, const uint8_t *dht_vals, int num_vals);
static inline void hb_huff_free(HuffTable *h);
static inline int hb_huff_decode_symbol(HuffTable *h, BitReader *br, int *sym);

#ifdef __cplusplus
}
#endif

/* Implementation */
#ifdef __cplusplus
extern "C" {
#endif

/* Small DataStream helpers used by the DHT convenience parser */
static inline uint16_t ds_get16(DataStream *s) {
	if (s->idx + 2 > s->len)
		return 0;
	uint16_t v = (s->buf[s->idx] << 8) | s->buf[s->idx + 1];
	s->idx += 2;
	return v;
}
static inline uint8_t ds_get8(DataStream *s) {
	if (s->idx + 1 > s->len)
		return 0;
	return s->buf[s->idx++];
}

/* ---- BitReader implementation ---- */

static inline void br_init(BitReader *br, const uint8_t *buf, size_t len, size_t start_idx) {
	if (!br)
		return;
	br->buf = buf;
	br->len = len;
	br->idx = start_idx;
	br->bitbuf = 0;
	br->bits_in_buf = 0;
	br->marker = 0;
}

static inline int br_read_byte_jpeg(BitReader *br, uint8_t *byte) {
	if (!br || !byte)
		return 0;
	if (br->marker)
		return -1;
	if (br->idx >= br->len)
		return 0;
	uint8_t v = br->buf[br->idx++];
	if (v != 0xFF) {
		*byte = v;
		return 1;
	}
	if (br->idx >= br->len)
		return 0;
	uint8_t next = br->buf[br->idx++];
	if (next == 0x00) {
		*byte = 0xFF;
		return 1;
	}
	br->marker = 0xFF00 | (uint16_t)next;
	return -1;
}

static inline int br_ensure_bits(BitReader *br, int need) {
	while (br->bits_in_buf < need) {
		uint8_t b;
		int r = br_read_byte_jpeg(br, &b);
		if (r == 1) {
			br->bitbuf = (br->bitbuf << 8) | b;
			br->bits_in_buf += 8;
			continue;
		} else if (r == 0)
			return HB_ERR_EOF;
		else
			return HB_ERR_MARKER;
	}
	return HB_OK;
}

static inline int br_get_bits(BitReader *br, int n, uint32_t *out) {
	if (!br || !out || n <= 0 || n > 16)
		return HB_ERR_BADARG;
	int r = br_ensure_bits(br, n);
	if (r != HB_OK)
		return r;
	int shift = br->bits_in_buf - n;
	uint32_t mask = ((1u << n) - 1u);
	uint32_t value = (br->bitbuf >> shift) & mask;
	br->bits_in_buf -= n;
	if (br->bits_in_buf == 0)
		br->bitbuf = 0;
	else
		br->bitbuf &= ((1u << br->bits_in_buf) - 1u);
	*out = value;
	return HB_OK;
}

static inline int br_get_bit(BitReader *br, int *out_bit) {
	uint32_t v;
	int r = br_get_bits(br, 1, &v);
	if (r != HB_OK)
		return r;
	if (out_bit)
		*out_bit = (int)v;
	return HB_OK;
}

static inline void br_byte_align(BitReader *br) {
	if (!br)
		return;
	int drop = br->bits_in_buf % 8;
	if (drop > 0) {
		br->bits_in_buf -= drop;
		if (br->bits_in_buf == 0)
			br->bitbuf = 0;
		else
			br->bitbuf &= ((1u << br->bits_in_buf) - 1u);
	}
}

static inline int br_get_marker(BitReader *br, uint16_t *marker_out) {
	if (!br)
		return 0;
	if (br->marker) {
		if (marker_out)
			*marker_out = (uint16_t)br->marker;
		return 1;
	}
	return 0;
}

static inline int br_read16_be(BitReader *br, uint16_t *out16) {
	if (!br || !out16)
		return HB_ERR_BADARG;
	br_byte_align(br);
	uint8_t b1, b2;
	int r = br_read_byte_jpeg(br, &b1);
	if (r == 1) {
		r = br_read_byte_jpeg(br, &b2);
		if (r == 1) {
			*out16 = (uint16_t)((b1 << 8) | b2);
			return HB_OK;
		} else if (r == -1)
			return HB_ERR_MARKER;
		else
			return HB_ERR_EOF;
	} else if (r == -1)
		return HB_ERR_MARKER;
	else
		return HB_ERR_EOF;
}

/* ---- Huffman table build & decode (canonical) ---- */

/* Read DHT and build HuffTable[t][class] via hb_huff_build_from_dht_block.
   Keeps the same t/c mapping as earlier code: low nibble = table id (t), high nibble = class (c). */
static inline int read_DHT_and_build(DataStream *ds, HuffTable out[4][2]) {
	uint16_t len = ds_get16(ds);
	size_t start = ds->idx;
	while ((ds->idx - start) < (len - 2)) {
		int temp = ds_get8(ds);
		int t = temp & 0x0F;
		int c = (temp >> 4) & 0x0F;
		uint8_t counts16[16];
		int total = 0;
		for (int i = 0; i < 16; ++i) {
			counts16[i] = ds_get8(ds);
			total += counts16[i];
		}
		uint8_t *vals = NULL;
		if (total > 0) {
			vals = (uint8_t *)malloc((size_t)total);
			if (!vals)
				return -1;
			int p = 0;
			for (int i = 0; i < 16; ++i)
				for (int j = 0; j < counts16[i]; ++j)
					vals[p++] = ds_get8(ds);
		}
		// hb_huff_build_from_dht_block expects a 16-byte counts array and vals
		int rc = hb_huff_build_from_dht_block(&out[t][c], counts16, vals, total);
		if (vals) {
			free(vals);
			vals = NULL;
		}
		if (rc != HB_OK)
			return -1;
	}
	return 0;
}

/* Internal helper: build fast lookup table for canonical codes.
   fast_bits = HB_FAST_LOOKUP_BITS (or 0 to disable)
   fast array entries are int32: (len << 24) | (symbol & 0xFF)
   -1 indicates no fast match (use progressive fallback)
*/
static inline int build_fast_table(HuffTable *h, int fast_bits) {
	if (!h)
		return HB_ERR_BADARG;
	if (fast_bits <= 0) {
		h->fast_bits = 0;
		h->fast = NULL;
		h->fast_size = 0;
		return HB_OK;
	}
	int size = 1 << fast_bits;
	int32_t *tbl = (int32_t *)malloc((size_t)size * sizeof(int32_t));
	if (!tbl)
		return HB_ERR_NOMEM;
	for (int i = 0; i < size; ++i)
		tbl[i] = -1;
	// For each code length L <= fast_bits, expand numeric codes to prefix ranges
	for (int L = 1; L <= fast_bits && L <= 16; ++L) {
		if (h->min_code[L] == 0xFFFF)
			continue;
		uint32_t base = h->min_code[L];
		int count = (int)(h->max_code[L] - h->min_code[L] + 1);
		int ptr = h->val_ptr[L];
		for (int k = 0; k < count; ++k) {
			uint32_t code = base + (uint32_t)k;
			// code is L bits; expand to fast_bits by shifting left
			uint32_t start = code << (fast_bits - L);
			uint32_t end = ((code + 1u) << (fast_bits - L)) - 1u;
			if (end >= (uint32_t)size)
				end = (uint32_t)size - 1u;
			int32_t packed = (L << 24) | (h->vals[ptr + k] & 0xFF);
			for (uint32_t idx = start; idx <= end; ++idx)
				tbl[idx] = packed;
		}
	}
	h->fast_bits = fast_bits;
	h->fast_size = size;
	h->fast = tbl;
	return HB_OK;
}

/* hb_huff_build_from_dht: build canonical table from bits[1..16] and vals[] */
static inline int hb_huff_build_from_dht(HuffTable *h, const uint8_t bits[17], const uint8_t *vals, int num_vals) {
	if (!h || !bits || (num_vals > 0 && !vals))
		return HB_ERR_BADARG;
	memset(h, 0, sizeof(*h));
	int total = 0;
	for (int L = 1; L <= 16; ++L)
		total += bits[L];
	if (total != num_vals)
		return HB_ERR_FORMAT;
	if (num_vals > 0) {
		h->vals = (uint8_t *)malloc((size_t)num_vals);
		if (!h->vals)
			return HB_ERR_NOMEM;
		memcpy(h->vals, vals, (size_t)num_vals);
	} else {
		h->vals = NULL;
	}
	h->num_vals = num_vals;
	int idx = 0;
	for (int L = 1; L <= 16; ++L) {
		h->val_ptr[L] = idx;
		idx += bits[L];
	}
	uint32_t code = 0;
	for (int L = 1; L <= 16; ++L) {
		code <<= 1;
		if (bits[L] == 0) {
			h->min_code[L] = 0xFFFF;
			h->max_code[L] = 0xFFFF;
		} else {
			h->min_code[L] = (uint16_t)code;
			h->max_code[L] = (uint16_t)(code + bits[L] - 1);
			code += bits[L];
		}
	}
	if (code > (1u << 16)) {
		hb_huff_free(h);
		return HB_ERR_FORMAT;
	}
	// build fast table if configured
#if HB_FAST_LOOKUP_BITS > 0
	if (build_fast_table(h, HB_FAST_LOOKUP_BITS) != HB_OK) {
		// failed to build fast table -> free and continue without fast
		if (h->fast) {
			free(h->fast);
			h->fast = NULL;
			h->fast_bits = 0;
			h->fast_size = 0;
		}
	}
#endif
	return HB_OK;
}

/* Convenience: build from DHT block of 16 counts + vals */
static inline int hb_huff_build_from_dht_block(HuffTable *h, const uint8_t *dht_bits16, const uint8_t *dht_vals, int num_vals) {
	if (!dht_bits16)
		return HB_ERR_BADARG;
	uint8_t bits[17];
	bits[0] = 0;
	for (int i = 0; i < 16; ++i)
		bits[i + 1] = dht_bits16[i];
	return hb_huff_build_from_dht(h, bits, dht_vals, num_vals);
}

/* Free HuffTable resources (including fast table) */
static inline void hb_huff_free(HuffTable *h) {
	if (!h)
		return;
	if (h->vals) {
		free(h->vals);
		h->vals = NULL;
	}
	if (h->fast) {
		free(h->fast);
		h->fast = NULL;
		h->fast_bits = 0;
		h->fast_size = 0;
	}
	memset(h, 0, sizeof(*h));
}

/* Decode symbol: try fast lookup first (if present) then fallback to progressive decode */
static inline int hb_huff_decode_symbol(HuffTable *h, BitReader *br, int *sym) {
	if (!h || !br || !sym)
		return HB_ERR_BADARG;
	if (h->num_vals == 0 || !h->vals)
		return HB_ERR_FORMAT;
	// Fast-path: if fast table exists and we have at least 1 bit (we will request up to fast_bits)
#if HB_FAST_LOOKUP_BITS > 0
	int need = h->fast_bits;
	int r = br_ensure_bits(br, need);
	if (r == HB_OK) {
		int shift = br->bits_in_buf - need;
		uint32_t prefix = (br->bitbuf >> shift) & ((1u << need) - 1u);
		int32_t packed = h->fast[prefix];
		if (packed != -1) {
			int len = (packed >> 24) & 0xFF;
			int val = packed & 0xFF;
			// consume 'len' bits
			br->bits_in_buf -= len;
			if (br->bits_in_buf == 0)
				br->bitbuf = 0;
			else
				br->bitbuf &= ((1u << br->bits_in_buf) - 1u);
			*sym = val;
			return HB_OK;
		}
		// else fallthrough to progressive path (do not consume bits)
#else
	// r can be HB_ERR_EOF or HB_ERR_MARKER -> return it
	return r;
#endif
	}

	// Progressive decode: read bit-by-bit and test ranges (existing implementation)
	uint32_t code = 0;
	for (int L = 1; L <= 16; ++L) {
		int bit;
		int r = br_get_bit(br, &bit);
		if (r != HB_OK)
			return r;
		code = (code << 1) | (uint32_t)bit;
		uint16_t minc = h->min_code[L];
		if (minc == 0xFFFF)
			continue;
		uint16_t maxc = h->max_code[L];
		if (code >= minc && code <= maxc) {
			int index = h->val_ptr[L] + (int)(code - minc);
			if (index < 0 || index >= h->num_vals)
				return HB_ERR_FORMAT;
			*sym = (int)h->vals[index];
			return HB_OK;
		}
	}
	return HB_ERR_FORMAT;
}

/* Utility: free allocated frame/scan arrays + HuffTables + output */
static void decoder_free(Decoder *d) {
	if (!d)
		return;
	if (d->frame.components) {
		free(d->frame.components);
		d->frame.components = NULL;
	}
	if (d->scan.components) {
		free(d->scan.components);
		d->scan.components = NULL;
	}
	for (int t = 0; t < 4; ++t)
		for (int c = 0; c < 2; ++c)
			hb_huff_free(&d->huffTables[t][c]);
	if (d->output) {
		free(d->output);
		d->output = NULL;
	}
}

/* Read frame (SOF) - simplified: allocate components array sized by numComp */
static int read_frame_header(DataStream *ds, FrameHeader *f) {
	uint16_t len = ds_get16(ds);
	(void)len;
	if (ds->idx >= ds->len)
		return -1;
	f->precision = ds_get8(ds);
	f->yDim = ds_get16(ds);
	f->xDim = ds_get16(ds);
	f->numComp = ds_get8(ds);
	f->components = (ComponentSpec *)calloc(f->numComp + 1, sizeof(ComponentSpec));
	if (!f->components)
		return -1;
	for (int i = 1; i <= f->numComp; ++i) {
		int cid = ds_get8(ds);
		int tmp = ds_get8(ds);
		f->components[cid].hSamp = (tmp >> 4) & 0xF;
		f->components[cid].vSamp = tmp & 0xF;
		f->components[cid].quantTableSel = ds_get8(ds);
	}
	return 0;
}

/* Read scan (SOS) - simplified */
static int read_scan_header(DataStream *ds, ScanHeader *s) {
	uint16_t len = ds_get16(ds);
	(void)len;
	if (ds->idx >= ds->len)
		return -1;
	s->numComp = ds_get8(ds);
	s->components = (ScanComponent *)calloc(s->numComp, sizeof(ScanComponent));
	if (!s->components)
		return -1;
	for (int i = 0; i < s->numComp; ++i) {
		s->components[i].scanCompSel = ds_get8(ds);
		int tmp = ds_get8(ds);
		s->components[i].dcTabSel = (tmp >> 4) & 0xF;
		s->components[i].acTabSel = tmp & 0xF;
	}
	s->selection = ds_get8(ds);
	s->spectralEnd = ds_get8(ds);
	int tmp = ds_get8(ds);
	s->ah = (tmp >> 4) & 0xF;
	s->al = tmp & 0xF;
	return 0;
}

/* Skip APPn/COM */
static int skip_segment(DataStream *ds) {
	uint16_t len = ds_get16(ds);
	if (len < 2)
		return -1;
	ds->idx += (len - 2);
	if (ds->idx > ds->len)
		return -1;
	return 0;
}

// JPEG sign-extend helper
static inline int jpeg_extend(uint32_t v, int n) {
	uint32_t half = 1u << (n - 1);
	if (v < half) {
		return (int)(v + (((-1) << n) + 1));
	}
	return (int)v;
}

static inline int getn_with_special(Decoder *d, int pred, int n, int *out) {
	if (n == 0) {
		if (out)
			*out = 0;
		return HB_OK;
	}
	if (n < 0 || n > 16)
		return HB_ERR_BADARG;
	if (n == 16) {
		// original behavior: depending on sign of predictor
		if (pred >= 0) {
			if (out)
				*out = -32768;
			return HB_OK;
		} else {
			if (out)
				*out = 32768;
			return HB_OK;
		}
	}
	uint32_t v;
	int r = br_get_bits(&d->br, n, &v);
	if (r != HB_OK)
		return r;
	if (out)
		*out = jpeg_extend(v, n);
	return HB_OK;
}

/* Previous samples helpers (same logic, compact) */
static inline int get_prevX(Decoder *d) {
	if (d->xLoc > 0)
		return (int16_t)d->output[d->yLoc * d->xDim + d->xLoc - 1];
	if (d->yLoc > 0)
		return (int16_t)d->output[(d->yLoc - 1) * d->xDim + d->xLoc];
	return (1 << (d->precision - 1));
}
static inline int get_prevY(Decoder *d) {
	if (d->yLoc > 0)
		return (int16_t)d->output[(d->yLoc - 1) * d->xDim + d->xLoc];
	return get_prevX(d);
}
static inline int get_prevXY(Decoder *d) {
	if (d->xLoc > 0 && d->yLoc > 0)
		return (int16_t)d->output[(d->yLoc - 1) * d->xDim + d->xLoc - 1];
	return get_prevY(d);
}

/* Single-component decode unit (DC lossless) */
static inline int decode_unit(Decoder *d, int *pred) {
	// predictor selection
	switch (d->selection) {
	case 2:
		pred[0] = get_prevY(d);
		break;
	case 3:
		pred[0] = get_prevXY(d);
		break;
	case 4:
		pred[0] = get_prevX(d) + get_prevY(d) - get_prevXY(d);
		break;
	case 5:
		pred[0] = get_prevX(d) + ((get_prevY(d) - get_prevXY(d)) >> 1);
		break;
	case 6:
		pred[0] = get_prevY(d) + ((get_prevX(d) - get_prevXY(d)) >> 1);
		break;
	case 7:
		pred[0] = (get_prevX(d) + get_prevY(d)) / 2;
		break;
	default:
		pred[0] = get_prevX(d);
		break;
	}
	// DC table index from scan component 0
	int dcIndex = d->scan.components[0].dcTabSel;
	HuffTable *ht = &d->huffTables[dcIndex][0]; // class 0 = DC
	int sym;
	int r = hb_huff_decode_symbol(ht, &d->br, &sym);
	if (r != HB_OK) {
		if (r == HB_ERR_MARKER) {
			uint16_t m;
			br_get_marker(&d->br, &m);
			return 0xFF00 | (m & 0xFF);
		}
		return -1;
	}
	int diff;
	r = getn_with_special(d, pred[0], sym, &diff);
	if (r != HB_OK) {
		if (r == HB_ERR_MARKER) {
			uint16_t m;
			br_get_marker(&d->br, &m);
			return 0xFF00 | (m & 0xFF);
		}
		return -1;
	}
	pred[0] += diff;
	if (d->scan.al > 0)
		pred[0] >>= d->scan.al;
	// store (clamp to 16-bit unsigned representation)
	int v = pred[0] & 0xFFFF;
	d->output[d->yLoc * d->xDim + d->xLoc] = (uint16_t)v;
	return 0;
}

/* helper: find scan component index that matches component id (1-based), or -1 */
static inline int find_scan_comp_idx_for_component(Decoder *d, int comp_id) {
	for (int s = 0; s < d->scan.numComp; ++s) {
		if (d->scan.components[s].scanCompSel == comp_id)
			return s;
	}
	return -1;
}

/* helper: find index of first non-empty DC Huffman table (0..3) or -1 */
static inline int first_nonempty_dctable(Decoder *d) {
	for (int t = 0; t < 4; ++t)
		if (d->huffTables[t][0].num_vals > 0)
			return t;
	return -1;
}

static inline int is_sof(uint16_t marker) {
	if (marker < 0xFFC0 || marker > 0xFFCF)
		return 0;
	if (marker == 0xFFC4)
		return 0; /* DHT */
	if (marker == 0xFFCC)
		return 0; /* DAC (treat unsupported) */
	return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* HUFFMAN_BITREADER_H */
