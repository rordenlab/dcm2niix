// jpg_0XC3.cpp
// Only handles images encoded with Start Of Frame marker 0xFFC3
// aka "SOF3" Lossless (sequential)
// more common lossy jpeg requires a different library
// supports 8-bit, 16-bit and RGB (24-bit)
// see Annex H of ISO/IEC 10918-1 : 1993(E)
// Uses helpers in jpg_0XC3_helper.h.

#include "jpg_0XC3.h"
#include "jpg_0XC3_helper.h"
#include "print.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RETURN_PLANAR_DEFAULT 1
/* decode_frame: decode a single frame/plane into plane_out (uint16_t npix samples).
   Uses the scan component index scan_idx (index into d->scan.components array) to
   determine which Huffman/DC table to use while calling decode_unit().
   Returns HB_OK on success, HB_ERR_MARKER style >=0xFF00 when marker encountered, negative on error.
*/
static int decode_frame(Decoder *d, uint16_t *plane_out, int scan_idx) {
	d->output = plane_out;
	d->xLoc = d->yLoc = 0;
	int pred_arr[1];
	pred_arr[0] = (1 << (d->precision - 1));
	if (scan_idx == 0) {
		while ((d->yLoc < d->yDim) && (d->xLoc < d->xDim)) {
			int r = decode_unit(d, pred_arr);
			if (r >= 0xFF00)
				return r;
			if (r < 0)
				return r;
			++d->xLoc;
			if (d->xLoc >= d->xDim) {
				d->xLoc = 0;
				++d->yLoc;
			}
		}
		return HB_OK;
	} else {
		ScanComponent saved = d->scan.components[0];
		d->scan.components[0] = d->scan.components[scan_idx];
		d->xLoc = d->yLoc = 0;
		while ((d->yLoc < d->yDim) && (d->xLoc < d->xDim)) {
			int r = decode_unit(d, pred_arr);
			if (r >= 0xFF00) {
				d->scan.components[scan_idx] = d->scan.components[0];
				d->scan.components[0] = saved;
				return r;
			}
			if (r < 0) {
				d->scan.components[scan_idx] = d->scan.components[0];
				d->scan.components[0] = saved;
				return r;
			}
			++d->xLoc;
			if (d->xLoc >= d->xDim) {
				d->xLoc = 0;
				++d->yLoc;
			}
		}
		d->scan.components[scan_idx] = d->scan.components[0];
		d->scan.components[0] = saved;
		return HB_OK;
	}
}

// Core decoder operating on an already-loaded byte buffer. Caller owns `buf` and frees it on return.
// Issue 1017: the buffer-based core lets multi-fragment encapsulated pixel data be reassembled in RAM (see dicom_fragments.cpp + decode_JPEG_SOF_0XC3_mem below) without round-tripping through a temp file.
static unsigned char *decode_JPEG_SOF_0XC3_core(uint8_t *buf, size_t flen, int skipBytes, bool verbose,
												int *xDim, int *yDim, int *bits, int *frames, int diskBytes) {

	/* Pre-declare and initialize to satisfy goto/fail uses */
	const char *err = NULL;
	char errbuf[64];
	unsigned char *out = NULL;
	uint16_t *all_output = NULL;
	int bytes_per_sample = 0;
	size_t bytes_per_pixel = 0;
	size_t out_len = 0;
	size_t npix = 0;
	size_t total_samples = 0;
	uint16_t midpoint = 0;
	uint16_t marker = 0;
	bool do_interleaved = false;
	int lnHufTables = 0;
	Decoder d;
	memset(&d, 0, sizeof(d));

	if (buf == NULL)
		return NULL;

	d.ds.buf = buf;
	d.ds.len = flen;
	d.ds.idx = (size_t)skipBytes;
	d.precision = 16;
	d.numComp = 0;

	/* parse markers until SOF */
	marker = ds_get16(&d.ds);
	if (marker != 0xFFD8) {
		err = "missing JPEG signature (0xFFD8)";
		goto done;
	}
	marker = ds_get16(&d.ds);

	// Parse pre-SOF segments (DHT, DQT, APPn, COM, etc.) until a real SOF marker is reached
	while (!is_sof(marker)) {

		if (marker == 0xFFC4) {
			if (read_DHT_and_build(&d.ds, d.huffTables) < 0) {
				err = "Unable to read DHT 0xFFC4";
				goto done;
			}
		} else if (marker == 0xFFDB) {
			if (skip_segment(&d.ds) < 0) {
				err = "Unable to skip DQT (0xFFDB)";
				goto done;
			}
		} else if (marker == 0xFFCC) {
			err = "Unsupported tag 0xFFCC";
			goto done;
		} else if ((marker >= 0xFFE0) && (marker <= 0xFFEF)) {
			if (skip_segment(&d.ds) < 0) {
				err = "Unable to skip tag 0xFFE*";
				goto done;
			}
		} else if (marker == 0xFFFE) {
			if (skip_segment(&d.ds) < 0) {
				err = "Unable to skip tag 0xFFFE";
				goto done;
			}
		} else {
			if ((marker >> 8) != 0xFF) {
				err = "Invalid tag";
				goto done;
			}
		}
		if (d.ds.idx + 2 > d.ds.len) {
			err = "Unexpected EOF while scanning markers";
			goto done;
		}
		marker = ds_get16(&d.ds);
	}
	/* ensure SOF valid */
	if (read_frame_header(&d.ds, &d.frame) < 0) {
		err = "Corrupt frame header";
		goto done;
	}

	d.xDim = d.frame.xDim;
	d.yDim = d.frame.yDim;
	d.precision = d.frame.precision;
	d.numComp = d.frame.numComp;
	if (verbose)
		printMessage("jpeg ISO 10918-1:1994 T.81 %d×%d %d-bits %d components\n", d.xDim, d.yDim, d.precision, d.numComp);
	if (marker != 0xFFC3) {
		snprintf(errbuf, sizeof(errbuf),
				 "Unsupported compression (marker 0x%04X)", marker);
		err = errbuf;
		goto done;
	}
	/* parse until SOS */
	marker = ds_get16(&d.ds);
	while (marker != 0xFFDA) {
		if (marker == 0xFFC4) {
			if (read_DHT_and_build(&d.ds, d.huffTables) < 0) {
				err = "Unable to read DHT 0xFFC4";
				goto done;
			}
		} else if (marker == 0xFFDB) {
			if (skip_segment(&d.ds) < 0) {
				err = "Unable to skip DQT (0xFFDB)";
				goto done;
			}
		} else if ((marker >= 0xFFE0) && (marker <= 0xFFEF)) {
			if (skip_segment(&d.ds) < 0) {
				err = "Unable to skip tag 0xFFE*";
				goto done;
			}
		} else if (marker == 0xFFFE) {
			if (skip_segment(&d.ds) < 0) {
				err = "Unable to skip tag 0xFFFE";
				goto done;
			}
		} else {
			if ((marker >> 8) != 0xFF) {
				err = "Invalid tag";
				goto done;
			}
		}
		marker = ds_get16(&d.ds);
	}

	/* read scan header */
	if (read_scan_header(&d.ds, &d.scan) < 0) {
		err = "Corrupt scan header";
		goto done;
	}
	d.selection = d.scan.selection;
	/* sanity: at least one DC huff table must be present */
	lnHufTables = 0;
	for (int t = 0; t < 4; ++t)
		if (d.huffTables[t][0].num_vals > 0)
			++lnHufTables;

	if (lnHufTables == 0) {
		err = "No DC Huffman table (DHT) found";
		goto done;
	}
	/* allocate planar all_output */
	npix = (size_t)d.xDim * (size_t)d.yDim;
	total_samples = npix * (size_t)d.numComp;
	all_output = (uint16_t *)malloc(total_samples * sizeof(uint16_t));
	if (!all_output) {
		err = "Memory allocation failed";
		goto done;
	}

	midpoint = (uint16_t)(1 << (d.precision - 1));
	for (size_t i = 0; i < total_samples; ++i)
		all_output[i] = midpoint;

	/* init bitreader */
	br_init(&d.br, d.ds.buf, d.ds.len, d.ds.idx);
	/* Validate supported layouts */
	if (!((d.numComp == 1) || (d.numComp == 3 && d.precision == 8))) {
		err = "Only 1 and 3 component images supported";
		goto done;
	}

	/* If single SOS and multiple frame components, we will decode interleaved per-pixel */

	if (d.scan.numComp > 1) {
		/* Scan enumerates multiple components -> interleaved scan (typical). */
		do_interleaved = true;
	} else if (d.scan.numComp == d.numComp && d.scan.numComp > 1) {
		/* Single scan that lists all frame components -> interleaved. */
		do_interleaved = true;
	} else if (d.scan.numComp == 1 && d.numComp > 1) {
		/* Ambiguous: a single-scan entry but multiple frame components.
		   Heuristic: if there are fewer Huffman DC tables than components,
		   the encoder likely reused tables / interleaved — prefer interleaved.
		   If there are distinct tables for each component we can try per-plane. */
		if (lnHufTables < d.numComp)
			do_interleaved = true;
		else
			do_interleaved = false;
	} else {
		/* default fallback: per-plane decode */
		do_interleaved = false;
	}
	if (do_interleaved) {
		/* interleaved-per-pixel decode */
		/* prepare per-plane predictors (initialized to midpoint) */
		int pred_vals[3] = {0, 0, 0};
		for (int p = 0; p < d.numComp; ++p)
			pred_vals[p] = (1 << (d.precision - 1));

		/* map plane -> scan index (use 0 for fallback if not found) */
		int scan_idx_for_plane[3] = {0, 0, 0};
		for (int p = 0; p < d.numComp; ++p) {
			int comp_id = p + 1;
			int sidx = find_scan_comp_idx_for_component(&d, comp_id);
			if (sidx < 0)
				sidx = 0;
			scan_idx_for_plane[p] = sidx;
		}
		/* decode interleaved per-pixel */
		uint16_t *plane_ptrs[3];
		for (int p = 0; p < d.numComp; ++p)
			plane_ptrs[p] = all_output + (size_t)p * npix;
		for (int y = 0; y < d.yDim; ++y) {
			for (int x = 0; x < d.xDim; ++x) {
				for (int p = 0; p < d.numComp; ++p) {
					d.xLoc = x;
					d.yLoc = y;
					d.output = plane_ptrs[p];
					/* temporarily set scan.components[0] to the chosen scan entry */
					ScanComponent saved0 = d.scan.components[0];
					d.scan.components[0] = d.scan.components[scan_idx_for_plane[p]];
					int r = decode_unit(&d, &pred_vals[p]);
					/* restore */
					d.scan.components[0] = saved0;
					if (r >= 0xFF00) {
						goto decode_done;
						// err = "Marker encountered during decode";
						// goto done;
					}
					if (r < 0) {
						err = "Decode interleaved failed";
						goto done;
					}
				}
			}
		}
	} else {
		/* per-plane decode */
		for (int plane = 0; plane < d.numComp; ++plane) {
			int comp_id = plane + 1;
			int scan_idx = find_scan_comp_idx_for_component(&d, comp_id);
			bool used_temporary_component0 = false;
			ScanComponent saved0;

			if (scan_idx < 0) {
				if (d.scan.numComp == d.numComp && d.scan.numComp > 0) {
					scan_idx = plane;
				} else if (d.scan.numComp == 1) {
					scan_idx = 0;
				} else {
					int firstDHT = first_nonempty_dctable(&d);
					if (firstDHT >= 0) {
						saved0 = d.scan.components[0];
						d.scan.components[0].scanCompSel = comp_id;
						d.scan.components[0].dcTabSel = firstDHT;
						scan_idx = 0;
						used_temporary_component0 = true;
					} else {
						err = "No DC Huffman table (DHT) found";
						goto done;
					}
				}
			}
			uint16_t *plane_out = all_output + (size_t)plane * npix;
			int r = decode_frame(&d, plane_out, scan_idx);
			if (used_temporary_component0) {
				d.scan.components[0] = saved0;
			}
			if (r >= 0xFF00)
				break;
			if (r < 0) {
				err = "Decode planar failed";
				goto done;
			}
		}
	}
decode_done:
	/* Build final output */
	bytes_per_sample = (d.precision <= 8) ? 1 : 2;
	bytes_per_pixel = (size_t)bytes_per_sample * (size_t)d.numComp;
	out_len = npix * bytes_per_pixel;
	out = (unsigned char *)malloc(out_len ? out_len : 1);
	if (!out) {
		err = "Memory allocation failed";
		goto done;
	}
	if (bytes_per_sample == 1) {
		if (d.numComp == 1) {
			for (size_t pix = 0; pix < npix; ++pix) {
				uint16_t v16 = all_output[pix];
				if (v16 > 0xFF)
					v16 = 0xFF;
				out[pix] = (uint8_t)(v16 & 0xFF);
			}
		} else {
			if (RETURN_PLANAR_DEFAULT) {
				/* Planar layout: [plane0 ... plane0_end][plane1 ...][plane2 ...] */
				for (int c = 0; c < d.numComp; ++c) {
					size_t plane_base = (size_t)c * npix;
					size_t out_plane_base = (size_t)c * npix;
					for (size_t pix = 0; pix < npix; ++pix) {
						uint16_t v16 = all_output[plane_base + pix];
						if (v16 > 0xFF)
							v16 = 0xFF;
						out[out_plane_base + pix] = (uint8_t)(v16 & 0xFF);
					}
				}
			} else {
				/* Interleaved RGBRGB... */
				for (size_t pix = 0; pix < npix; ++pix) {
					size_t base = pix * bytes_per_pixel;
					for (int c = 0; c < d.numComp; ++c) {
						uint16_t v16 = all_output[(size_t)c * npix + pix];
						if (v16 > 0xFF)
							v16 = 0xFF;
						out[base + (size_t)c] = (uint8_t)(v16 & 0xFF);
					}
				}
			}
		}
	} else {
		memcpy(out, all_output, out_len);
	}

	if (xDim)
		*xDim = d.xDim;
	if (yDim)
		*yDim = d.yDim;
	if (bits)
		*bits = d.precision;
	if (frames)
		*frames = d.numComp;
done:
	d.output = NULL;
	decoder_free(&d);
	if (all_output) {
		free(all_output);
		all_output = NULL;
	}
	// Caller owns `buf`; do not free here.
	if (err) {
		if (out) {
			free(out);
			out = NULL;
		}
		printError("decode_JPEG_SOF_0XC3: %s\n", err);
		return NULL;
	}
	return out;
}

// File-based public entry: reads the whole file into a buffer and forwards to the core. Preserves the existing API.
unsigned char *decode_JPEG_SOF_0XC3(const char *fn, int skipBytes, bool verbose,
									int *xDim, int *yDim, int *bits, int *frames, int diskBytes) {
	if (fn == NULL)
		return NULL;
	FILE *f = fopen(fn, "rb");
	if (f == NULL) {
		printError("Cannot open %s\n", fn);
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long tlen = ftell(f);
	if (tlen < 0) {
		fclose(f);
		return NULL;
	}
	size_t flen = (size_t)tlen;
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	uint8_t *buf = (uint8_t *)malloc(flen ? flen : 1);
	if (buf == NULL) {
		fclose(f);
		return NULL;
	}
	if (fread(buf, 1, flen, f) != flen) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	unsigned char *out = decode_JPEG_SOF_0XC3_core(buf, flen, skipBytes, verbose, xDim, yDim, bits, frames, diskBytes);
	free(buf);
	return out;
}

// Buffer-based public entry (issue 1017). Caller owns `buf` and frees it on return.
unsigned char *decode_JPEG_SOF_0XC3_mem(uint8_t *buf, size_t flen, int skipBytes, bool verbose,
										int *xDim, int *yDim, int *bits, int *frames, int diskBytes) {
	return decode_JPEG_SOF_0XC3_core(buf, flen, skipBytes, verbose, xDim, yDim, bits, frames, diskBytes);
}
