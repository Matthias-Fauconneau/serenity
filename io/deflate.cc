#include "deflate.h"
#include "string.h"
#include <string.h>

// Decompression flags used by decompress().
// FLAG_PARSE_ZLIB_HEADER: If set, the input has a valid zlib header and ends with an adler32 checksum (it's a valid zlib stream). Otherwise, the input is a raw deflate stream.
// FLAG_HAS_MORE_INPUT: If set, there are more input bytes available beyond the end of the supplied input buffer. If clear, the input buffer contains all remaining input.
// FLAG_USING_NON_WRAPPING_OUTPUT_BUF: If set, the output buffer is large enough to hold the entire decompressed stream. If clear, the output buffer is at least the size of the dictionary (typically 32KB).
// FLAG_COMPUTE_ADLER32: Force adler-32 checksum computation of the decompressed bytes.
enum {
	FLAG_PARSE_ZLIB_HEADER = 1,
	FLAG_HAS_MORE_INPUT = 2,
	FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4,
	FLAG_COMPUTE_ADLER32 = 8
};

// Return status.
enum status {
	STATUS_BAD_PARAM = -3, STATUS_ADLER32_MISMATCH = -2, STATUS_FAILED = -1,
	STATUS_OKAY, STATUS_DONE, STATUS_NEEDS_MORE_INPUT, STATUS_HAS_MORE_OUTPUT
};

const int MAX_HUFF_TABLES = 3, MAX_HUFF_SYMBOLS_0 = 288, MAX_HUFF_SYMBOLS_1 = 32, MAX_HUFF_SYMBOLS_2 = 19;
const uint LZ_DICT_SIZE = 32768;
const int LZ_DICT_SIZE_MASK = LZ_DICT_SIZE - 1, MIN_MATCH_LEN = 3, MAX_MATCH_LEN = 258;
const int FAST_LOOKUP_BITS = 10, FAST_LOOKUP_SIZE = 1 << FAST_LOOKUP_BITS;

struct huff_table {
	uint8 m_code_size[MAX_HUFF_SYMBOLS_0];
	int16 m_look_up[FAST_LOOKUP_SIZE], m_tree[MAX_HUFF_SYMBOLS_0 * 2];
};

struct decompressor {
	uint32 m_state, m_num_bits, m_zhdr0, m_zhdr1, m_z_adler32, m_final, m_type, m_check_adler32, m_dist, m_counter, m_num_extra, m_table_sizes[MAX_HUFF_TABLES];
	uint64 m_bit_buf;
	size_t m_dist_from_out_buf_start;
	huff_table m_tables[MAX_HUFF_TABLES];
	uint8 m_raw_header[4], m_len_codes[MAX_HUFF_SYMBOLS_0 + MAX_HUFF_SYMBOLS_1 + 137];
};

// Compression flags logically OR'd together (low 12 bits contain the max. number of probes per dictionary search):
// DEFAULT_MAX_PROBES: The compressor defaults to 128 dictionary probes per dictionary search. 0=Huffman only, 1=Huffman+LZ (fastest/crap compression), 4095=Huffman+LZ (slowest/best compression).
// WRITE_ZLIB_HEADER: If set, the compressor outputs a zlib header before the deflate data, and the Adler-32 of the source data at the end. Otherwise, you'll get raw deflate data.
// COMPUTE_ADLER32: Always compute the adler-32 of the input data (even when not writing zlib headers).
// GREEDY_PARSING_FLAG: Set to use faster greedy parsing, instead of more efficient lazy parsing.
// NONDETERMINISTIC_PARSING_FLAG: Enable to decrease the compressor's initialization time to the minimum, but the output may vary from run to run given the same input (depending on the contents of memory).
// RLE_MATCHES: Only look for RLE matches (matches with a distance of 1)
// FILTER_MATCHES: Discards matches <= 5 chars if enabled.
// FORCE_ALL_STATIC_BLOCKS: Disable usage of optimized Huffman tables.
// FORCE_ALL_RAW_BLOCKS: Only use raw (uncompressed) deflate blocks.
enum {
	MAX_PROBES = 0xFFF,
	WRITE_ZLIB_HEADER             = 0x01000,
	COMPUTE_ADLER32               = 0x02000,
	GREEDY_PARSING_FLAG           = 0x04000,
	NONDETERMINISTIC_PARSING_FLAG = 0x08000,
	RLE_MATCHES                   = 0x10000,
	FILTER_MATCHES                = 0x20000,
	FORCE_ALL_STATIC_BLOCKS       = 0x40000,
	FORCE_ALL_RAW_BLOCKS          = 0x80000
};

// Output stream interface. The compressor uses this interface to write compressed data. It'll typically be called OUT_BUF_SIZE at a time.
typedef bool (*put_buf_func_ptr)(const void* pBuf, int len, void *pUser);

// OUT_BUF_SIZE MUST be large enough to hold a single entire compressed output block (using static/fixed Huffman codes).
enum { LZ_CODE_BUF_SIZE = 64 * 1024, OUT_BUF_SIZE = (LZ_CODE_BUF_SIZE * 13 ) / 10, MAX_HUFF_SYMBOLS = 288, LZ_HASH_BITS = 15, LEVEL1_HASH_SIZE_MASK = 4095, LZ_HASH_SHIFT = (LZ_HASH_BITS + 2) / 3, LZ_HASH_SIZE = 1 << LZ_HASH_BITS };

// Must map to MZ_NO_FLUSH, MZ_SYNC_FLUSH, etc. enums
enum flush {
	NO_FLUSH = 0,
	SYNC_FLUSH = 2,
	FULL_FLUSH = 3,
	FINISH = 4
};

// tdefl's compression state structure.
struct compressor {
	put_buf_func_ptr m_pPut_buf_func;
	void *m_pPut_buf_user;
	uint m_flags, m_max_probes[2];
	int m_greedy_parsing;
	uint m_adler32, m_lookahead_pos, m_lookahead_size, m_dict_size;
	uint8 *m_pLZ_code_buf, *m_pLZ_flags, *m_pOutput_buf, *m_pOutput_buf_end;
	uint m_num_flags_left, m_total_lz_bytes, m_lz_code_buf_dict_pos, m_bits_in, m_bit_buffer;
	uint m_saved_match_dist, m_saved_match_len, m_saved_lit, m_output_flush_ofs, m_output_flush_remaining, m_finished, m_block_index, m_wants_to_finish;
	status m_prev_return_status;
	const void *m_pIn_buf;
	flush m_flush;
	const uint8 *m_pSrc;
	size_t m_src_buf_left, m_out_buf_ofs;
	uint8 m_dict[LZ_DICT_SIZE + MAX_MATCH_LEN - 1];
	uint16 m_huff_count[MAX_HUFF_TABLES][MAX_HUFF_SYMBOLS];
	uint16 m_huff_codes[MAX_HUFF_TABLES][MAX_HUFF_SYMBOLS];
	uint8 m_huff_code_sizes[MAX_HUFF_TABLES][MAX_HUFF_SYMBOLS];
	uint8 m_lz_code_buf[LZ_CODE_BUF_SIZE];
	uint16 m_next[LZ_DICT_SIZE];
	uint16 m_hash[LZ_HASH_SIZE];
	uint8 m_output_buf[OUT_BUF_SIZE];
};

template<Type T, size_t N> void clear(T (&a)[N]) { mref<T>(a).clear(); }

uint32 adler32(uint32 adler, const unsigned char *ptr, size_t buf_len) {
	uint32 i, s1 = (uint32)(adler & 0xffff), s2 = (uint32)(adler >> 16); size_t block_len = buf_len % 5552;
	if (!ptr) return 1;
	while (buf_len) {
		for (i = 0; i + 7 < block_len; i += 8, ptr += 8) {
			s1 += ptr[0], s2 += s1; s1 += ptr[1], s2 += s1; s1 += ptr[2], s2 += s1; s1 += ptr[3], s2 += s1;
			s1 += ptr[4], s2 += s1; s1 += ptr[5], s2 += s1; s1 += ptr[6], s2 += s1; s1 += ptr[7], s2 += s1;
		}
		for ( ; i < block_len; ++i) s1 += *ptr++, s2 += s1;
		s1 %= 65521U, s2 %= 65521U; buf_len -= block_len; block_len = 5552;
	}
	return (s2 << 16) + s1;
}

// Karl Malbrain's compact CRC-32. See "A compact CCITT crc16 and crc32 C implementation that balances processor cache usage against speed": http://www.geocities.com/malbrain/
uint32 crc32(uint32 crc, const uint8 *ptr, size_t buf_len) {
	static const uint32 s_crc32[16] = { 0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
										0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
	uint32 crcu32 = (uint32)crc;
	if (!ptr) return 0;
	crcu32 = ~crcu32; while (buf_len--) { uint8 b = *ptr++; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b & 0xF)]; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b >> 4)]; }
	return ~crcu32;
}

#define RETURN(state_index, result) { status = result; r->m_state = state_index; goto common_exit; case state_index:; }
#define RETURN_FOREVER(state_index, result) { for ( ; ; ) { RETURN(state_index, result); } }

// TODO: If the caller has indicated that there's no more input, and we attempt to read beyond the input buf, then something is wrong with the input because the inflator never
// reads ahead more than it needs to. Currently GET_BYTE() pads the end of the stream with 0's in this scenario.
#define GET_BYTE(state_index, c) { \
	if (pIn_buf_cur >= pIn_buf_end) { \
	for ( ; ; ) { \
	if (decomp_flags & FLAG_HAS_MORE_INPUT) { \
	RETURN(state_index, STATUS_NEEDS_MORE_INPUT); \
	if (pIn_buf_cur < pIn_buf_end) { \
	c = *pIn_buf_cur++; \
	break; \
	} \
	} else { \
	c = 0; \
	break; \
	} \
	} \
	} else c = *pIn_buf_cur++; }

#define NEED_BITS(state_index, n) do { uint c; GET_BYTE(state_index, c); bit_buf |= (((uint64)c) << num_bits); num_bits += 8; } while (num_bits < (uint)(n))
#define SKIP_BITS(state_index, n) { if (num_bits < (uint)(n)) { NEED_BITS(state_index, n); } bit_buf >>= (n); num_bits -= (n); }
#define GET_BITS(state_index, b, n) ({ if (num_bits < (uint)(n)) { NEED_BITS(state_index, n); } b = bit_buf & ((1 << (n)) - 1); bit_buf >>= (n); num_bits -= (n); })

// HUFF_BITBUF_FILL() is only used rarely, when the number of bytes remaining in the input buffer falls below 2.
// It reads just enough bytes from the input stream that are needed to decode the next Huffman code (and absolutely no more). It works by trying to fully decode a
// Huffman code by using whatever bits are currently present in the bit buffer. If this fails, it reads another byte, and tries again until it succeeds or until the
// bit buffer contains >=15 bits (deflate's max. Huffman code size).
#define HUFF_BITBUF_FILL(state_index, pHuff) \
	do { \
	temp = (pHuff)->m_look_up[bit_buf & (FAST_LOOKUP_SIZE - 1)]; \
	if (temp >= 0) { \
	code_len = temp >> 9; \
	if ((code_len) && (num_bits >= code_len)) \
	break; \
	} else if (num_bits > FAST_LOOKUP_BITS) { \
	code_len = FAST_LOOKUP_BITS; \
	do { \
	temp = (pHuff)->m_tree[~temp + ((bit_buf >> code_len++) & 1)]; \
	} while ((temp < 0) && (num_bits >= (code_len + 1))); if (temp >= 0) break; \
	} GET_BYTE(state_index, c); bit_buf |= (((uint64)c) << num_bits); num_bits += 8; \
	} while (num_bits < 15);

// HUFF_DECODE() decodes the next Huffman coded symbol. It's more complex than you would initially expect because the zlib API expects the decompressor to never read
// beyond the final byte of the deflate stream. (In other words, when this macro wants to read another byte from the input, it REALLY needs another byte in order to fully
// decode the next Huffman code.) Handling this properly is particularly important on raw deflate (non-zlib) streams, which aren't followed by a byte aligned adler-32.
// The slow path is only executed at the very end of the input buffer.
#define HUFF_DECODE(state_index, sym, pHuff) { \
	int temp; uint code_len, c; \
	if (num_bits < 15) { \
	if ((pIn_buf_end - pIn_buf_cur) < 2) { \
	HUFF_BITBUF_FILL(state_index, pHuff); \
	} else { \
	bit_buf |= (((uint64)pIn_buf_cur[0]) << num_bits) | (((uint64)pIn_buf_cur[1]) << (num_bits + 8)); pIn_buf_cur += 2; num_bits += 16; \
	} \
	} \
	if ((temp = (pHuff)->m_look_up[bit_buf & (FAST_LOOKUP_SIZE - 1)]) >= 0) \
	code_len = temp >> 9, temp &= 511; \
	else { \
	code_len = FAST_LOOKUP_BITS; do { temp = (pHuff)->m_tree[~temp + ((bit_buf >> code_len++) & 1)]; } while (temp < 0); \
	} sym = temp; bit_buf >>= code_len; num_bits -= code_len; }

status decompress(decompressor *r, const uint8 *pIn_buf_next, size_t *pIn_buf_size, uint8 *pOut_buf_start, uint8 *pOut_buf_next, size_t *pOut_buf_size, const uint32 decomp_flags) {
	static const int s_length_base[31] = { 3,4,5,6,7,8,9,10,11,13, 15,17,19,23,27,31,35,43,51,59, 67,83,99,115,131,163,195,227,258,0,0 };
	static const int s_length_extra[31]= { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0 };
	static const int s_dist_base[32] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193, 257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,0,0};
	static const int s_dist_extra[32] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
	static const uint8 s_length_dezigzag[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
	static const int s_min_table_sizes[3] = { 257, 1, 4 };

	status status = STATUS_FAILED; uint32 num_bits, dist, counter, num_extra; uint64 bit_buf;
	const uint8 *pIn_buf_cur = pIn_buf_next, *const pIn_buf_end = pIn_buf_next + *pIn_buf_size;
	uint8 *pOut_buf_cur = pOut_buf_next, *const pOut_buf_end = pOut_buf_next + *pOut_buf_size;
	size_t out_buf_size_mask = (decomp_flags & FLAG_USING_NON_WRAPPING_OUTPUT_BUF) ? (size_t)-1 : ((pOut_buf_next - pOut_buf_start) + *pOut_buf_size) - 1, dist_from_out_buf_start;

	// Ensure the output buffer's size is a power of 2, unless the output buffer is large enough to hold the entire output file (in which case it doesn't matter).
	if (((out_buf_size_mask + 1) & out_buf_size_mask) || (pOut_buf_next < pOut_buf_start)) { *pIn_buf_size = *pOut_buf_size = 0; return STATUS_BAD_PARAM; }

	num_bits = r->m_num_bits; bit_buf = r->m_bit_buf; dist = r->m_dist; counter = r->m_counter; num_extra = r->m_num_extra; dist_from_out_buf_start = r->m_dist_from_out_buf_start;
	switch(r->m_state) { case 0:

		bit_buf = num_bits = dist = counter = num_extra = r->m_zhdr0 = r->m_zhdr1 = 0; r->m_z_adler32 = r->m_check_adler32 = 1;
		if (decomp_flags & FLAG_PARSE_ZLIB_HEADER) {
			GET_BYTE(1, r->m_zhdr0); GET_BYTE(2, r->m_zhdr1);
			counter = (((r->m_zhdr0 * 256 + r->m_zhdr1) % 31 != 0) || (r->m_zhdr1 & 32) || ((r->m_zhdr0 & 15) != 8));
			if (!(decomp_flags & FLAG_USING_NON_WRAPPING_OUTPUT_BUF)) counter |= (((1U << (8U + (r->m_zhdr0 >> 4))) > 32768U) || ((out_buf_size_mask + 1) < (size_t)(1U << (8U + (r->m_zhdr0 >> 4)))));
			if (counter) { RETURN_FOREVER(36, STATUS_FAILED); }
		}

		do {
			GET_BITS(3, r->m_final, 3); r->m_type = r->m_final >> 1;
			if (r->m_type == 0) {
				SKIP_BITS(5, num_bits & 7);
				for(counter = 0; counter < 4; ++counter) {
					if(num_bits) GET_BITS(6, r->m_raw_header[counter], 8);
					else GET_BYTE(7, r->m_raw_header[counter]);
				}
				if ((counter = (r->m_raw_header[0] | (r->m_raw_header[1] << 8))) != (uint)(0xFFFF ^ (r->m_raw_header[2] | (r->m_raw_header[3] << 8)))) { RETURN_FOREVER(39, STATUS_FAILED); }
				while ((counter) && (num_bits)) {
					GET_BITS(51, dist, 8);
					while (pOut_buf_cur >= pOut_buf_end) { RETURN(52, STATUS_HAS_MORE_OUTPUT); }
					*pOut_buf_cur++ = (uint8)dist;
					counter--;
				}
				while (counter) {
					size_t n; while (pOut_buf_cur >= pOut_buf_end) { RETURN(9, STATUS_HAS_MORE_OUTPUT); }
					while (pIn_buf_cur >= pIn_buf_end) {
						if (decomp_flags & FLAG_HAS_MORE_INPUT) {
							RETURN(38, STATUS_NEEDS_MORE_INPUT);
						}
						else {
							RETURN_FOREVER(40, STATUS_FAILED);
						}
					}
					n = min(min(size_t(pOut_buf_end - pOut_buf_cur), size_t(pIn_buf_end - pIn_buf_cur)), size_t(counter));
					mref<uint8>(pOut_buf_cur, n).copy(ref<uint8>(pIn_buf_cur, n)); pIn_buf_cur += n; pOut_buf_cur += n; counter -= (uint)n;
				}
			}
			else if (r->m_type == 3) {
				RETURN_FOREVER(10, STATUS_FAILED);
			}
			else {
				if (r->m_type == 1) {
					uint8 *p = r->m_tables[0].m_code_size; uint i;
					r->m_table_sizes[0] = 288; r->m_table_sizes[1] = 32; mref<uint8>(r->m_tables[1].m_code_size, 32).clear(5);
					for ( i = 0; i <= 143; ++i) *p++ = 8; for ( ; i <= 255; ++i) *p++ = 9; for ( ; i <= 279; ++i) *p++ = 7; for ( ; i <= 287; ++i) *p++ = 8;
				}
				else {
					for (counter = 0; counter < 3; counter++) { GET_BITS(11, r->m_table_sizes[counter], "\05\05\04"[counter]); r->m_table_sizes[counter] += s_min_table_sizes[counter]; }
					clear(r->m_tables[2].m_code_size); for (counter = 0; counter < r->m_table_sizes[2]; counter++) { uint s; GET_BITS(14, s, 3); r->m_tables[2].m_code_size[s_length_dezigzag[counter]] = (uint8)s; }
					r->m_table_sizes[2] = 19;
				}
				for ( ; (int)r->m_type >= 0; r->m_type--) {
					int tree_next, tree_cur; huff_table *pTable;
					uint i, j, used_syms, total, sym_index, next_code[17], total_syms[16]; pTable = &r->m_tables[r->m_type]; clear(total_syms); clear(pTable->m_look_up); clear(pTable->m_tree);
					for (i = 0; i < r->m_table_sizes[r->m_type]; ++i) total_syms[pTable->m_code_size[i]]++;
					used_syms = 0, total = 0; next_code[0] = next_code[1] = 0;
					for (i = 1; i <= 15; ++i) { used_syms += total_syms[i]; next_code[i + 1] = (total = ((total + total_syms[i]) << 1)); }
					if ((65536 != total) && (used_syms > 1)) {
						RETURN_FOREVER(35, STATUS_FAILED);
					}
					for (tree_next = -1, sym_index = 0; sym_index < r->m_table_sizes[r->m_type]; ++sym_index) {
						uint rev_code = 0, l, cur_code, code_size = pTable->m_code_size[sym_index]; if (!code_size) continue;
						cur_code = next_code[code_size]++; for (l = code_size; l > 0; l--, cur_code >>= 1) rev_code = (rev_code << 1) | (cur_code & 1);
						if (code_size <= FAST_LOOKUP_BITS) { int16 k = (int16)((code_size << 9) | sym_index); while (rev_code < FAST_LOOKUP_SIZE) { pTable->m_look_up[rev_code] = k; rev_code += (1 << code_size); } continue; }
						if (0 == (tree_cur = pTable->m_look_up[rev_code & (FAST_LOOKUP_SIZE - 1)])) { pTable->m_look_up[rev_code & (FAST_LOOKUP_SIZE - 1)] = (int16)tree_next; tree_cur = tree_next; tree_next -= 2; }
						rev_code >>= (FAST_LOOKUP_BITS - 1);
						for (j = code_size; j > (FAST_LOOKUP_BITS + 1); j--) {
							tree_cur -= ((rev_code >>= 1) & 1);
							if (!pTable->m_tree[-tree_cur - 1]) { pTable->m_tree[-tree_cur - 1] = (int16)tree_next; tree_cur = tree_next; tree_next -= 2; } else tree_cur = pTable->m_tree[-tree_cur - 1];
						}
						tree_cur -= ((rev_code >>= 1) & 1); pTable->m_tree[-tree_cur - 1] = (int16)sym_index;
					}
					if (r->m_type == 2) {
						for (counter = 0; counter < (r->m_table_sizes[0] + r->m_table_sizes[1]); ) {
							uint s; HUFF_DECODE(16, dist, &r->m_tables[2]); if (dist < 16) { r->m_len_codes[counter++] = (uint8)dist; continue; }
							if ((dist == 16) && (!counter)) {
								RETURN_FOREVER(17, STATUS_FAILED);
							}
							num_extra = "\02\03\07"[dist - 16]; GET_BITS(18, s, num_extra); s += "\03\03\013"[dist - 16];
							__builtin___memset_chk(r->m_len_codes + counter, (dist == 16) ? r->m_len_codes[counter - 1] : 0, s, __bos0(r->m_len_codes + counter));
							counter += s;
						}
						if ((r->m_table_sizes[0] + r->m_table_sizes[1]) != counter) {
							RETURN_FOREVER(21, STATUS_FAILED);
						}
						memcpy(r->m_tables[0].m_code_size, r->m_len_codes, r->m_table_sizes[0]); memcpy(r->m_tables[1].m_code_size, r->m_len_codes + r->m_table_sizes[0], r->m_table_sizes[1]);
					}
				}
				for ( ; ; ) {
					uint8 *pSrc;
					for ( ; ; ) {
						if (((pIn_buf_end - pIn_buf_cur) < 4) || ((pOut_buf_end - pOut_buf_cur) < 2)) {
							HUFF_DECODE(23, counter, &r->m_tables[0]);
							if (counter >= 256)
								break;
							while (pOut_buf_cur >= pOut_buf_end) { RETURN(24, STATUS_HAS_MORE_OUTPUT); }
							*pOut_buf_cur++ = (uint8)counter;
						}
						else {
							int sym2; uint code_len;
							if (num_bits < 30) { bit_buf |= (((uint64)*((const uint32 *)(pIn_buf_cur))) << num_bits); pIn_buf_cur += 4; num_bits += 32; }
							if ((sym2 = r->m_tables[0].m_look_up[bit_buf & (FAST_LOOKUP_SIZE - 1)]) >= 0)
								code_len = sym2 >> 9;
							else {
								code_len = FAST_LOOKUP_BITS; do { sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)]; } while (sym2 < 0);
							}
							counter = sym2; bit_buf >>= code_len; num_bits -= code_len;
							if (counter & 256)
								break;

							if ((sym2 = r->m_tables[0].m_look_up[bit_buf & (FAST_LOOKUP_SIZE - 1)]) >= 0)
								code_len = sym2 >> 9;
							else {
								code_len = FAST_LOOKUP_BITS; do { sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)]; } while (sym2 < 0);
							}
							bit_buf >>= code_len; num_bits -= code_len;

							pOut_buf_cur[0] = (uint8)counter;
							if (sym2 & 256) {
								pOut_buf_cur++;
								counter = sym2;
								break;
							}
							pOut_buf_cur[1] = (uint8)sym2;
							pOut_buf_cur += 2;
						}
					}
					if ((counter &= 511) == 256) break;

					num_extra = s_length_extra[counter - 257]; counter = s_length_base[counter - 257];
					if (num_extra) { uint extra_bits; GET_BITS(25, extra_bits, num_extra); counter += extra_bits; }

					HUFF_DECODE(26, dist, &r->m_tables[1]);
					num_extra = s_dist_extra[dist]; dist = s_dist_base[dist];
					if (num_extra) { uint extra_bits; GET_BITS(27, extra_bits, num_extra); dist += extra_bits; }

					dist_from_out_buf_start = pOut_buf_cur - pOut_buf_start;
					if ((dist > dist_from_out_buf_start) && (decomp_flags & FLAG_USING_NON_WRAPPING_OUTPUT_BUF)) {
						RETURN_FOREVER(37, STATUS_FAILED);
					}

					pSrc = pOut_buf_start + ((dist_from_out_buf_start - dist) & out_buf_size_mask);

					if ((max(pOut_buf_cur, pSrc) + counter) > pOut_buf_end) {
						while (counter--) {
							while (pOut_buf_cur >= pOut_buf_end) { RETURN(53, STATUS_HAS_MORE_OUTPUT); }
							*pOut_buf_cur++ = pOut_buf_start[(dist_from_out_buf_start++ - dist) & out_buf_size_mask];
						}
						continue;
					}
					else if ((counter >= 9) && (counter <= dist)) {
						const uint8 *pSrc_end = pSrc + (counter & ~7);
						do {
							((uint32 *)pOut_buf_cur)[0] = ((const uint32 *)pSrc)[0];
							((uint32 *)pOut_buf_cur)[1] = ((const uint32 *)pSrc)[1];
							pOut_buf_cur += 8;
						} while ((pSrc += 8) < pSrc_end);
						if ((counter &= 7) < 3) {
							if (counter) {
								pOut_buf_cur[0] = pSrc[0];
								if (counter > 1)
									pOut_buf_cur[1] = pSrc[1];
								pOut_buf_cur += counter;
							}
							continue;
						}
					}
					do {
						pOut_buf_cur[0] = pSrc[0];
						pOut_buf_cur[1] = pSrc[1];
						pOut_buf_cur[2] = pSrc[2];
						pOut_buf_cur += 3; pSrc += 3;
					} while ((int)(counter -= 3) > 2);
					if ((int)counter > 0) {
						pOut_buf_cur[0] = pSrc[0];
						if ((int)counter > 1)
							pOut_buf_cur[1] = pSrc[1];
						pOut_buf_cur += counter;
					}
				}
			}
		} while (!(r->m_final & 1));
		if (decomp_flags & FLAG_PARSE_ZLIB_HEADER) {
			SKIP_BITS(32, num_bits & 7); for (counter = 0; counter < 4; ++counter) { uint s; if (num_bits) GET_BITS(41, s, 8); else GET_BYTE(42, s); r->m_z_adler32 = (r->m_z_adler32 << 8) | s; }
		}
		RETURN_FOREVER(34, STATUS_DONE);
	}

common_exit:
	r->m_num_bits = num_bits; r->m_bit_buf = bit_buf; r->m_dist = dist; r->m_counter = counter; r->m_num_extra = num_extra; r->m_dist_from_out_buf_start = dist_from_out_buf_start;
	*pIn_buf_size = pIn_buf_cur - pIn_buf_next; *pOut_buf_size = pOut_buf_cur - pOut_buf_next;
	if ((decomp_flags & (FLAG_PARSE_ZLIB_HEADER | FLAG_COMPUTE_ADLER32)) && (status >= 0)) {
		const uint8 *ptr = pOut_buf_next; size_t buf_len = *pOut_buf_size;
		uint32 i, s1 = r->m_check_adler32 & 0xffff, s2 = r->m_check_adler32 >> 16; size_t block_len = buf_len % 5552;
		while (buf_len) {
			for (i = 0; i + 7 < block_len; i += 8, ptr += 8) {
				s1 += ptr[0], s2 += s1; s1 += ptr[1], s2 += s1; s1 += ptr[2], s2 += s1; s1 += ptr[3], s2 += s1;
				s1 += ptr[4], s2 += s1; s1 += ptr[5], s2 += s1; s1 += ptr[6], s2 += s1; s1 += ptr[7], s2 += s1;
			}
			for ( ; i < block_len; ++i) s1 += *ptr++, s2 += s1;
			s1 %= 65521U, s2 %= 65521U; buf_len -= block_len; block_len = 5552;
		}
		r->m_check_adler32 = (s2 << 16) + s1; if ((status == STATUS_DONE) && (decomp_flags & FLAG_PARSE_ZLIB_HEADER) && (r->m_check_adler32 != r->m_z_adler32)) status = STATUS_ADLER32_MISMATCH;
	}
	return status;
}

void *decompress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len, size_t *pOut_len, int flags) {
	decompressor decomp; void *pBuf = nullptr, *pNew_buf; size_t src_buf_ofs = 0, out_buf_capacity = 0;
	*pOut_len = 0;
	decomp.m_state = 0;
	for ( ; ; ) {
		size_t src_buf_size = src_buf_len - src_buf_ofs, dst_buf_size = out_buf_capacity - *pOut_len, new_out_buf_capacity;
		status status = decompress(&decomp, (const uint8*)pSrc_buf + src_buf_ofs, &src_buf_size, (uint8*)pBuf, pBuf ? (uint8*)pBuf + *pOut_len : nullptr, &dst_buf_size,
								   (flags & ~FLAG_HAS_MORE_INPUT) | FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
		if ((status < 0) || (status == STATUS_NEEDS_MORE_INPUT)) {
			free(pBuf); *pOut_len = 0; return nullptr;
		}
		src_buf_ofs += src_buf_size;
		*pOut_len += dst_buf_size;
		if (status == STATUS_DONE) break;
		new_out_buf_capacity = out_buf_capacity * 2; if (new_out_buf_capacity < 128) new_out_buf_capacity = 128;
		pNew_buf = realloc(pBuf, new_out_buf_capacity);
		if (!pNew_buf) {
			free(pBuf); *pOut_len = 0; return nullptr;
		}
		pBuf = pNew_buf; out_buf_capacity = new_out_buf_capacity;
	}
	return pBuf;
}

buffer<byte> inflate(const ref<byte> source, bool zlib) {
	buffer<byte> data; size_t size;
	data.data = (byte*)decompress_mem_to_heap(source.data, source.size, &size, zlib?FLAG_PARSE_ZLIB_HEADER:0);
	data.capacity=data.size=size;
	return data;
}

/// DEFLATE

// Purposely making these tables static for faster init and thread safety.
static const uint16 s_len_sym[256] = {
	257,258,259,260,261,262,263,264,265,265,266,266,267,267,268,268,269,269,269,269,270,270,270,270,271,271,271,271,272,272,272,272,
	273,273,273,273,273,273,273,273,274,274,274,274,274,274,274,274,275,275,275,275,275,275,275,275,276,276,276,276,276,276,276,276,
	277,277,277,277,277,277,277,277,277,277,277,277,277,277,277,277,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,
	279,279,279,279,279,279,279,279,279,279,279,279,279,279,279,279,280,280,280,280,280,280,280,280,280,280,280,280,280,280,280,280,
	281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,
	282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,
	283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,
	284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,285 };

static const uint8 s_len_extra[256] = {
	0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
	4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
	5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
	5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,0 };

static const uint8 s_small_dist_sym[512] = {
	0,1,2,3,4,4,5,5,6,6,6,6,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,11,11,11,11,11,11,
	11,11,11,11,11,11,11,11,11,11,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,13,
	13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,14,14,14,14,14,14,14,14,14,14,14,14,
	14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
	14,14,14,14,14,14,14,14,14,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,16,16,16,16,16,16,16,16,16,16,16,16,16,
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
	17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
	17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
	17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17 };

static const uint8 s_small_dist_extra[512] = {
	0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,
	5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7 };

static const uint8 s_large_dist_sym[128] = {
	0,0,18,19,20,20,21,21,22,22,22,22,23,23,23,23,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,25,26,26,26,26,26,26,26,26,26,26,26,26,
	26,26,26,26,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,
	28,28,28,28,28,28,28,28,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29 };

static const uint8 s_large_dist_extra[128] = {
	0,0,8,8,9,9,9,9,10,10,10,10,10,10,10,10,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
	13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13 };

// Radix sorts sym_freq[] array by 16-bit key m_key. Returns ptr to sorted values.
struct sym_freq { uint16 m_key, m_sym_index; };
static sym_freq* radix_sort_syms(uint num_syms, sym_freq* pSyms0, sym_freq* pSyms1) {
	uint32 total_passes = 2, pass_shift, pass, i, hist[256 * 2]; sym_freq* pCur_syms = pSyms0, *pNew_syms = pSyms1; clear(hist);
	for (i = 0; i < num_syms; i++) { uint freq = pSyms0[i].m_key; hist[freq & 0xFF]++; hist[256 + ((freq >> 8) & 0xFF)]++; }
	while ((total_passes > 1) && (num_syms == hist[(total_passes - 1) * 256])) total_passes--;
	for (pass_shift = 0, pass = 0; pass < total_passes; pass++, pass_shift += 8) {
		const uint32* pHist = &hist[pass << 8];
		uint offsets[256], cur_ofs = 0;
		for (i = 0; i < 256; i++) { offsets[i] = cur_ofs; cur_ofs += pHist[i]; }
		for (i = 0; i < num_syms; i++) pNew_syms[offsets[(pCur_syms[i].m_key >> pass_shift) & 0xFF]++] = pCur_syms[i]; { sym_freq* t = pCur_syms; pCur_syms = pNew_syms; pNew_syms = t; }
	}
	return pCur_syms;
}

// calculate_minimum_redundancy() originally written by: Alistair Moffat, alistair@cs.mu.oz.au, Jyrki Katajainen, jyrki@diku.dk, November 1996.
static void calculate_minimum_redundancy(sym_freq *A, int n) {
	int root, leaf, next, avbl, used, dpth;
	if (n==0) return; else if (n==1) { A[0].m_key = 1; return; }
	A[0].m_key += A[1].m_key; root = 0; leaf = 2;
	for (next=1; next < n-1; next++) {
		if (leaf>=n || A[root].m_key<A[leaf].m_key) { A[next].m_key = A[root].m_key; A[root++].m_key = (uint16)next; } else A[next].m_key = A[leaf++].m_key;
		if (leaf>=n || (root<next && A[root].m_key<A[leaf].m_key)) { A[next].m_key = (uint16)(A[next].m_key + A[root].m_key); A[root++].m_key = (uint16)next; } else A[next].m_key = (uint16)(A[next].m_key + A[leaf++].m_key);
	}
	A[n-2].m_key = 0; for (next=n-3; next>=0; next--) A[next].m_key = A[A[next].m_key].m_key+1;
	avbl = 1; used = dpth = 0; root = n-2; next = n-1;
	while (avbl>0) {
		while (root>=0 && (int)A[root].m_key==dpth) { used++; root--; }
		while (avbl>used) { A[next--].m_key = (uint16)(dpth); avbl--; }
		avbl = 2*used; dpth++; used = 0;
	}
}

// Limits canonical Huffman code table's max code size.
enum { MAX_SUPPORTED_HUFF_CODESIZE = 32 };
static void huffman_enforce_max_code_size(int *pNum_codes, int code_list_len, int max_code_size) {
	int i; uint32 total = 0; if (code_list_len <= 1) return;
	for (i = max_code_size + 1; i <= MAX_SUPPORTED_HUFF_CODESIZE; i++) pNum_codes[max_code_size] += pNum_codes[i];
	for (i = max_code_size; i > 0; i--) total += (((uint32)pNum_codes[i]) << (max_code_size - i));
	while (total != (1UL << max_code_size)) {
		pNum_codes[max_code_size]--;
		for (i = max_code_size - 1; i > 0; i--) if (pNum_codes[i]) { pNum_codes[i]--; pNum_codes[i + 1] += 2; break; }
		total--;
	}
}

static void optimize_huffman_table(compressor *d, int table_num, int table_len, int code_size_limit, int static_table) {
	int i, j, l, num_codes[1 + MAX_SUPPORTED_HUFF_CODESIZE]; uint next_code[MAX_SUPPORTED_HUFF_CODESIZE + 1]; clear(num_codes);
	if (static_table) {
		for (i = 0; i < table_len; i++) num_codes[d->m_huff_code_sizes[table_num][i]]++;
	}
	else {
		sym_freq syms0[MAX_HUFF_SYMBOLS], syms1[MAX_HUFF_SYMBOLS], *pSyms;
		int num_used_syms = 0;
		const uint16 *pSym_count = &d->m_huff_count[table_num][0];
		for (i = 0; i < table_len; i++) if (pSym_count[i]) { syms0[num_used_syms].m_key = (uint16)pSym_count[i]; syms0[num_used_syms++].m_sym_index = (uint16)i; }

		pSyms = radix_sort_syms(num_used_syms, syms0, syms1); calculate_minimum_redundancy(pSyms, num_used_syms);

		for (i = 0; i < num_used_syms; i++) num_codes[pSyms[i].m_key]++;

		huffman_enforce_max_code_size(num_codes, num_used_syms, code_size_limit);

		clear(d->m_huff_code_sizes[table_num]); clear(d->m_huff_codes[table_num]);
		for (i = 1, j = num_used_syms; i <= code_size_limit; i++)
			for (l = num_codes[i]; l > 0; l--) d->m_huff_code_sizes[table_num][pSyms[--j].m_sym_index] = (uint8)(i);
	}

	next_code[1] = 0; for (j = 0, i = 2; i <= code_size_limit; i++) next_code[i] = j = ((j + num_codes[i - 1]) << 1);

	for (i = 0; i < table_len; i++) {
		uint rev_code = 0, code, code_size; if ((code_size = d->m_huff_code_sizes[table_num][i]) == 0) continue;
		code = next_code[code_size]++; for (l = code_size; l > 0; l--, code >>= 1) rev_code = (rev_code << 1) | (code & 1);
		d->m_huff_codes[table_num][i] = (uint16)rev_code;
	}
}

#define PUT_BITS(b, l) ({ \
	uint bits = b; uint len = l; assert(bits <= ((1U << len) - 1U)); \
	d->m_bit_buffer |= (bits << d->m_bits_in); d->m_bits_in += len; \
	while (d->m_bits_in >= 8) { \
	if (d->m_pOutput_buf < d->m_pOutput_buf_end) \
	*d->m_pOutput_buf++ = (uint8)(d->m_bit_buffer); \
	d->m_bit_buffer >>= 8; \
	d->m_bits_in -= 8; \
	} \
	})

#define RLE_PREV_CODE_SIZE() { if (rle_repeat_count) { \
	if (rle_repeat_count < 3) { \
	d->m_huff_count[2][prev_code_size] = (uint16)(d->m_huff_count[2][prev_code_size] + rle_repeat_count); \
	while (rle_repeat_count--) packed_code_sizes[num_packed_code_sizes++] = prev_code_size; \
	} else { \
	d->m_huff_count[2][16] = (uint16)(d->m_huff_count[2][16] + 1); packed_code_sizes[num_packed_code_sizes++] = 16; packed_code_sizes[num_packed_code_sizes++] = (uint8)(rle_repeat_count - 3); \
	} rle_repeat_count = 0; } }

#define RLE_ZERO_CODE_SIZE() { if (rle_z_count) { \
	if (rle_z_count < 3) { \
	d->m_huff_count[2][0] = (uint16)(d->m_huff_count[2][0] + rle_z_count); while (rle_z_count--) packed_code_sizes[num_packed_code_sizes++] = 0; \
	} else if (rle_z_count <= 10) { \
	d->m_huff_count[2][17] = (uint16)(d->m_huff_count[2][17] + 1); packed_code_sizes[num_packed_code_sizes++] = 17; packed_code_sizes[num_packed_code_sizes++] = (uint8)(rle_z_count - 3); \
	} else { \
	d->m_huff_count[2][18] = (uint16)(d->m_huff_count[2][18] + 1); packed_code_sizes[num_packed_code_sizes++] = 18; packed_code_sizes[num_packed_code_sizes++] = (uint8)(rle_z_count - 11); \
	} rle_z_count = 0; } }

static uint8 s_packed_code_size_syms_swizzle[] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

static void start_dynamic_block(compressor *d) {
	int num_lit_codes, num_dist_codes, num_bit_lengths; uint i, total_code_sizes_to_pack, num_packed_code_sizes, rle_z_count, rle_repeat_count, packed_code_sizes_index;
	uint8 code_sizes_to_pack[MAX_HUFF_SYMBOLS_0 + MAX_HUFF_SYMBOLS_1], packed_code_sizes[MAX_HUFF_SYMBOLS_0 + MAX_HUFF_SYMBOLS_1], prev_code_size = 0xFF;

	d->m_huff_count[0][256] = 1;

	optimize_huffman_table(d, 0, MAX_HUFF_SYMBOLS_0, 15, false);
	optimize_huffman_table(d, 1, MAX_HUFF_SYMBOLS_1, 15, false);

	for (num_lit_codes = 286; num_lit_codes > 257; num_lit_codes--) if (d->m_huff_code_sizes[0][num_lit_codes - 1]) break;
	for (num_dist_codes = 30; num_dist_codes > 1; num_dist_codes--) if (d->m_huff_code_sizes[1][num_dist_codes - 1]) break;

	memcpy(code_sizes_to_pack, &d->m_huff_code_sizes[0][0], num_lit_codes);
	memcpy(code_sizes_to_pack + num_lit_codes, &d->m_huff_code_sizes[1][0], num_dist_codes);
	total_code_sizes_to_pack = num_lit_codes + num_dist_codes; num_packed_code_sizes = 0; rle_z_count = 0; rle_repeat_count = 0;

	memset(&d->m_huff_count[2][0], 0, sizeof(d->m_huff_count[2][0]) * MAX_HUFF_SYMBOLS_2);
	for (i = 0; i < total_code_sizes_to_pack; i++) {
		uint8 code_size = code_sizes_to_pack[i];
		if (!code_size) {
			RLE_PREV_CODE_SIZE();
			if (++rle_z_count == 138) { RLE_ZERO_CODE_SIZE(); }
		}
		else {
			RLE_ZERO_CODE_SIZE();
			if (code_size != prev_code_size) {
				RLE_PREV_CODE_SIZE();
				d->m_huff_count[2][code_size] = (uint16)(d->m_huff_count[2][code_size] + 1); packed_code_sizes[num_packed_code_sizes++] = code_size;
			}
			else if (++rle_repeat_count == 6) {
				RLE_PREV_CODE_SIZE();
			}
		}
		prev_code_size = code_size;
	}
	if (rle_repeat_count) { RLE_PREV_CODE_SIZE(); } else { RLE_ZERO_CODE_SIZE(); }

	optimize_huffman_table(d, 2, MAX_HUFF_SYMBOLS_2, 7, false);

	PUT_BITS(2, 2);

	PUT_BITS(num_lit_codes - 257, 5);
	PUT_BITS(num_dist_codes - 1, 5);

	for (num_bit_lengths = 18; num_bit_lengths >= 0; num_bit_lengths--) if (d->m_huff_code_sizes[2][s_packed_code_size_syms_swizzle[num_bit_lengths]]) break;
	num_bit_lengths = max(4, (num_bit_lengths + 1)); PUT_BITS(num_bit_lengths - 4, 4);
	for (i = 0; (int)i < num_bit_lengths; i++) PUT_BITS(d->m_huff_code_sizes[2][s_packed_code_size_syms_swizzle[i]], 3);

	for (packed_code_sizes_index = 0; packed_code_sizes_index < num_packed_code_sizes; ) {
		uint code = packed_code_sizes[packed_code_sizes_index++]; assert(code < MAX_HUFF_SYMBOLS_2);
		PUT_BITS(d->m_huff_codes[2][code], d->m_huff_code_sizes[2][code]);
		if (code >= 16) PUT_BITS(packed_code_sizes[packed_code_sizes_index++], "\02\03\07"[code - 16]);
	}
}

static void start_static_block(compressor *d) {
	uint i;
	uint8 *p = &d->m_huff_code_sizes[0][0];

	for (i = 0; i <= 143; ++i) *p++ = 8;
	for ( ; i <= 255; ++i) *p++ = 9;
	for ( ; i <= 279; ++i) *p++ = 7;
	for ( ; i <= 287; ++i) *p++ = 8;

	memset(d->m_huff_code_sizes[1], 5, 32);

	optimize_huffman_table(d, 0, 288, 15, true);
	optimize_huffman_table(d, 1, 32, 15, true);

	PUT_BITS(1, 2);
}

static const uint bitmasks[17] = { 0x0000, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F, 0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF };

static bool compress_lz_codes(compressor *d) {
	uint flags;
	uint8 *pLZ_codes;
	uint8 *pOutput_buf = d->m_pOutput_buf;
	uint8 *pLZ_code_buf_end = d->m_pLZ_code_buf;
	uint64 bit_buffer = d->m_bit_buffer;
	uint bits_in = d->m_bits_in;

#define PUT_BITS_FAST(b, l) { bit_buffer |= (((uint64)(b)) << bits_in); bits_in += (l); }

	flags = 1;
	for (pLZ_codes = d->m_lz_code_buf; pLZ_codes < pLZ_code_buf_end; flags >>= 1) {
		if (flags == 1)
			flags = *pLZ_codes++ | 0x100;

		if (flags & 1) {
			uint s0, s1, n0, n1, sym, num_extra_bits;
			uint match_len = pLZ_codes[0], match_dist = *(const uint16 *)(pLZ_codes + 1); pLZ_codes += 3;

			assert(d->m_huff_code_sizes[0][s_len_sym[match_len]]);
			PUT_BITS_FAST(d->m_huff_codes[0][s_len_sym[match_len]], d->m_huff_code_sizes[0][s_len_sym[match_len]]);
			PUT_BITS_FAST(match_len & bitmasks[s_len_extra[match_len]], s_len_extra[match_len]);

			// This sequence coaxes MSVC into using cmov's vs. jmp's.
			s0 = s_small_dist_sym[match_dist & 511];
			n0 = s_small_dist_extra[match_dist & 511];
			s1 = s_large_dist_sym[match_dist >> 8];
			n1 = s_large_dist_extra[match_dist >> 8];
			sym = (match_dist < 512) ? s0 : s1;
			num_extra_bits = (match_dist < 512) ? n0 : n1;

			assert(d->m_huff_code_sizes[1][sym]);
			PUT_BITS_FAST(d->m_huff_codes[1][sym], d->m_huff_code_sizes[1][sym]);
			PUT_BITS_FAST(match_dist & bitmasks[num_extra_bits], num_extra_bits);
		}
		else {
			uint lit = *pLZ_codes++;
			assert(d->m_huff_code_sizes[0][lit]);
			PUT_BITS_FAST(d->m_huff_codes[0][lit], d->m_huff_code_sizes[0][lit]);

			if (((flags & 2) == 0) && (pLZ_codes < pLZ_code_buf_end)) {
				flags >>= 1;
				lit = *pLZ_codes++;
				assert(d->m_huff_code_sizes[0][lit]);
				PUT_BITS_FAST(d->m_huff_codes[0][lit], d->m_huff_code_sizes[0][lit]);

				if (((flags & 2) == 0) && (pLZ_codes < pLZ_code_buf_end)) {
					flags >>= 1;
					lit = *pLZ_codes++;
					assert(d->m_huff_code_sizes[0][lit]);
					PUT_BITS_FAST(d->m_huff_codes[0][lit], d->m_huff_code_sizes[0][lit]);
				}
			}
		}

		if (pOutput_buf >= d->m_pOutput_buf_end)
			return false;

		*(uint64*)pOutput_buf = bit_buffer;
		pOutput_buf += (bits_in >> 3);
		bit_buffer >>= (bits_in & ~7);
		bits_in &= 7;
	}

#undef PUT_BITS_FAST

	d->m_pOutput_buf = pOutput_buf;
	d->m_bits_in = 0;
	d->m_bit_buffer = 0;

	while (bits_in) {
		uint32 n = min(bits_in, 16u);
		PUT_BITS((uint)bit_buffer & bitmasks[n], n);
		bit_buffer >>= n;
		bits_in -= n;
	}

	PUT_BITS(d->m_huff_codes[0][256], d->m_huff_code_sizes[0][256]);

	return (d->m_pOutput_buf < d->m_pOutput_buf_end);
}

static bool compress_block(compressor *d, bool static_block) {
	if (static_block)
		start_static_block(d);
	else
		start_dynamic_block(d);
	return compress_lz_codes(d);
}

static int flush_block(compressor *d, int flush) {
	uint saved_bit_buf, saved_bits_in;
	uint8 *pSaved_output_buf;
	bool comp_block_succeeded = false;
	int n, use_raw_block = ((d->m_flags & FORCE_ALL_RAW_BLOCKS) != 0) && (d->m_lookahead_pos - d->m_lz_code_buf_dict_pos) <= d->m_dict_size;
	uint8 *pOutput_buf_start = d->m_output_buf;

	d->m_pOutput_buf = pOutput_buf_start;
	d->m_pOutput_buf_end = d->m_pOutput_buf + OUT_BUF_SIZE - 16;

	assert(!d->m_output_flush_remaining);
	d->m_output_flush_ofs = 0;
	d->m_output_flush_remaining = 0;

	*d->m_pLZ_flags = (uint8)(*d->m_pLZ_flags >> d->m_num_flags_left);
	d->m_pLZ_code_buf -= (d->m_num_flags_left == 8);

	if ((d->m_flags & WRITE_ZLIB_HEADER) && (!d->m_block_index)) {
		PUT_BITS(0x78, 8); PUT_BITS(0x01, 8);
	}

	PUT_BITS(flush == FINISH, 1);

	pSaved_output_buf = d->m_pOutput_buf; saved_bit_buf = d->m_bit_buffer; saved_bits_in = d->m_bits_in;

	if (!use_raw_block)
		comp_block_succeeded = compress_block(d, (d->m_flags & FORCE_ALL_STATIC_BLOCKS) || (d->m_total_lz_bytes < 48));

	// If the block gets expanded, forget the current contents of the output buffer and send a raw block instead.
	if ( ((use_raw_block) || ((d->m_total_lz_bytes) && ((d->m_pOutput_buf - pSaved_output_buf + 1U) >= d->m_total_lz_bytes))) &&
		 ((d->m_lookahead_pos - d->m_lz_code_buf_dict_pos) <= d->m_dict_size) ) {
		uint i; d->m_pOutput_buf = pSaved_output_buf; d->m_bit_buffer = saved_bit_buf, d->m_bits_in = saved_bits_in;
		PUT_BITS(0, 2);
		if (d->m_bits_in) { PUT_BITS(0, 8 - d->m_bits_in); }
		for (i = 2; i; --i, d->m_total_lz_bytes ^= 0xFFFF) {
			PUT_BITS(d->m_total_lz_bytes & 0xFFFF, 16);
		}
		for (i = 0; i < d->m_total_lz_bytes; ++i) {
			PUT_BITS(d->m_dict[(d->m_lz_code_buf_dict_pos + i) & LZ_DICT_SIZE_MASK], 8);
		}
	}
	// Check for the extremely unlikely (if not impossible) case of the compressed block not fitting into the output buffer when using dynamic codes.
	else if (!comp_block_succeeded) {
		d->m_pOutput_buf = pSaved_output_buf; d->m_bit_buffer = saved_bit_buf, d->m_bits_in = saved_bits_in;
		compress_block(d, true);
	}

	if (flush) {
		if (flush == FINISH) {
			if (d->m_bits_in) { PUT_BITS(0, 8 - d->m_bits_in); }
			if (d->m_flags & WRITE_ZLIB_HEADER) { uint i, a = d->m_adler32; for (i = 0; i < 4; i++) { PUT_BITS((a >> 24) & 0xFF, 8); a <<= 8; } }
		}
		else {
			uint i, z = 0; PUT_BITS(0, 3); if (d->m_bits_in) { PUT_BITS(0, 8 - d->m_bits_in); } for (i = 2; i; --i, z ^= 0xFFFF) { PUT_BITS(z & 0xFFFF, 16); }
		}
	}

	assert(d->m_pOutput_buf < d->m_pOutput_buf_end);

	memset(&d->m_huff_count[0][0], 0, sizeof(d->m_huff_count[0][0]) * MAX_HUFF_SYMBOLS_0);
	memset(&d->m_huff_count[1][0], 0, sizeof(d->m_huff_count[1][0]) * MAX_HUFF_SYMBOLS_1);

	d->m_pLZ_code_buf = d->m_lz_code_buf + 1; d->m_pLZ_flags = d->m_lz_code_buf; d->m_num_flags_left = 8; d->m_lz_code_buf_dict_pos += d->m_total_lz_bytes; d->m_total_lz_bytes = 0; d->m_block_index++;

	if ((n = (int)(d->m_pOutput_buf - pOutput_buf_start)) != 0) {
		d->m_pPut_buf_func(d->m_output_buf, n, d->m_pPut_buf_user);
	} else if (pOutput_buf_start == d->m_output_buf) {
		error("");
	}
	else {
		d->m_out_buf_ofs += n;
	}

	return d->m_output_flush_remaining;
}

#define READ_UNALIGNED_WORD(p) *(const uint16*)(p)
static bool compress_fast(compressor *d) {
	// Faster, minimally featured LZRW1-style match+parse loop with better register utilization. Intended for applications where raw throughput is valued more highly than ratio.
	uint lookahead_pos = d->m_lookahead_pos, lookahead_size = d->m_lookahead_size, dict_size = d->m_dict_size, total_lz_bytes = d->m_total_lz_bytes, num_flags_left = d->m_num_flags_left;
	uint8 *pLZ_code_buf = d->m_pLZ_code_buf, *pLZ_flags = d->m_pLZ_flags;
	uint cur_pos = lookahead_pos & LZ_DICT_SIZE_MASK;

	while ((d->m_src_buf_left) || ((d->m_flush) && (lookahead_size))) {
		const uint COMP_FAST_LOOKAHEAD_SIZE = 4096;
		uint dst_pos = (lookahead_pos + lookahead_size) & LZ_DICT_SIZE_MASK;
		uint num_bytes_to_process = min(uint(d->m_src_buf_left), COMP_FAST_LOOKAHEAD_SIZE - lookahead_size);
		d->m_src_buf_left -= num_bytes_to_process;
		lookahead_size += num_bytes_to_process;

		while (num_bytes_to_process) {
			uint32 n = min(LZ_DICT_SIZE - dst_pos, num_bytes_to_process);
			memcpy(d->m_dict + dst_pos, d->m_pSrc, n);
			if (dst_pos < (MAX_MATCH_LEN - 1))
				memcpy(d->m_dict + LZ_DICT_SIZE + dst_pos, d->m_pSrc, min(n, (MAX_MATCH_LEN - 1) - dst_pos));
			d->m_pSrc += n;
			dst_pos = (dst_pos + n) & LZ_DICT_SIZE_MASK;
			num_bytes_to_process -= n;
		}

		dict_size = min(LZ_DICT_SIZE - lookahead_size, dict_size);
		if ((!d->m_flush) && (lookahead_size < COMP_FAST_LOOKAHEAD_SIZE)) break;

		while (lookahead_size >= 4) {
			uint cur_match_dist, cur_match_len = 1;
			uint8 *pCur_dict = d->m_dict + cur_pos;
			uint first_trigram = (*(const uint32 *)pCur_dict) & 0xFFFFFF;
			uint hash = (first_trigram ^ (first_trigram >> (24 - (LZ_HASH_BITS - 8)))) & LEVEL1_HASH_SIZE_MASK;
			uint probe_pos = d->m_hash[hash];
			d->m_hash[hash] = (uint16)lookahead_pos;

			if (((cur_match_dist = (uint16)(lookahead_pos - probe_pos)) <= dict_size) &&
					((*(const uint32 *)(d->m_dict + (probe_pos &= LZ_DICT_SIZE_MASK)) & 0xFFFFFF) == first_trigram)) {
				const uint16 *p = (const uint16 *)pCur_dict;
				const uint16 *q = (const uint16 *)(d->m_dict + probe_pos);
				uint32 probe_len = 32;
				do { } while ( (READ_UNALIGNED_WORD(++p) == READ_UNALIGNED_WORD(++q)) && (READ_UNALIGNED_WORD(++p) == READ_UNALIGNED_WORD(++q)) &&
							   (READ_UNALIGNED_WORD(++p) == READ_UNALIGNED_WORD(++q)) && (READ_UNALIGNED_WORD(++p) == READ_UNALIGNED_WORD(++q)) && (--probe_len > 0) );
				cur_match_len = ((uint)(p - (const uint16 *)pCur_dict) * 2) + (uint)(*(const uint8 *)p == *(const uint8 *)q);
				if (!probe_len)
					cur_match_len = cur_match_dist ? MAX_MATCH_LEN : 0;

				if ((cur_match_len < MIN_MATCH_LEN) || ((cur_match_len == MIN_MATCH_LEN) && (cur_match_dist >= 8U*1024U))) {
					cur_match_len = 1;
					*pLZ_code_buf++ = (uint8)first_trigram;
					*pLZ_flags = (uint8)(*pLZ_flags >> 1);
					d->m_huff_count[0][(uint8)first_trigram]++;
				}
				else {
					uint32 s0, s1;
					cur_match_len = min(cur_match_len, lookahead_size);

					assert((cur_match_len >= MIN_MATCH_LEN) && (cur_match_dist >= 1) && (cur_match_dist <= LZ_DICT_SIZE));

					cur_match_dist--;

					pLZ_code_buf[0] = (uint8)(cur_match_len - MIN_MATCH_LEN);
					*(uint16 *)(&pLZ_code_buf[1]) = (uint16)cur_match_dist;
					pLZ_code_buf += 3;
					*pLZ_flags = (uint8)((*pLZ_flags >> 1) | 0x80);

					s0 = s_small_dist_sym[cur_match_dist & 511];
					s1 = s_large_dist_sym[cur_match_dist >> 8];
					d->m_huff_count[1][(cur_match_dist < 512) ? s0 : s1]++;

					d->m_huff_count[0][s_len_sym[cur_match_len - MIN_MATCH_LEN]]++;
				}
			}
			else {
				*pLZ_code_buf++ = (uint8)first_trigram;
				*pLZ_flags = (uint8)(*pLZ_flags >> 1);
				d->m_huff_count[0][(uint8)first_trigram]++;
			}

			if (--num_flags_left == 0) { num_flags_left = 8; pLZ_flags = pLZ_code_buf++; }

			total_lz_bytes += cur_match_len;
			lookahead_pos += cur_match_len;
			dict_size = min(dict_size + cur_match_len, LZ_DICT_SIZE);
			cur_pos = (cur_pos + cur_match_len) & LZ_DICT_SIZE_MASK;
			assert(lookahead_size >= cur_match_len);
			lookahead_size -= cur_match_len;

			if (pLZ_code_buf > &d->m_lz_code_buf[LZ_CODE_BUF_SIZE - 8]) {
				int n;
				d->m_lookahead_pos = lookahead_pos; d->m_lookahead_size = lookahead_size; d->m_dict_size = dict_size;
				d->m_total_lz_bytes = total_lz_bytes; d->m_pLZ_code_buf = pLZ_code_buf; d->m_pLZ_flags = pLZ_flags; d->m_num_flags_left = num_flags_left;
				if ((n = flush_block(d, 0)) != 0)
					return (n < 0) ? false : true;
				total_lz_bytes = d->m_total_lz_bytes; pLZ_code_buf = d->m_pLZ_code_buf; pLZ_flags = d->m_pLZ_flags; num_flags_left = d->m_num_flags_left;
			}
		}

		while (lookahead_size) {
			uint8 lit = d->m_dict[cur_pos];

			total_lz_bytes++;
			*pLZ_code_buf++ = lit;
			*pLZ_flags = (uint8)(*pLZ_flags >> 1);
			if (--num_flags_left == 0) { num_flags_left = 8; pLZ_flags = pLZ_code_buf++; }

			d->m_huff_count[0][lit]++;

			lookahead_pos++;
			dict_size = min(dict_size + 1, LZ_DICT_SIZE);
			cur_pos = (cur_pos + 1) & LZ_DICT_SIZE_MASK;
			lookahead_size--;

			if (pLZ_code_buf > &d->m_lz_code_buf[LZ_CODE_BUF_SIZE - 8]) {
				int n;
				d->m_lookahead_pos = lookahead_pos; d->m_lookahead_size = lookahead_size; d->m_dict_size = dict_size;
				d->m_total_lz_bytes = total_lz_bytes; d->m_pLZ_code_buf = pLZ_code_buf; d->m_pLZ_flags = pLZ_flags; d->m_num_flags_left = num_flags_left;
				if ((n = flush_block(d, 0)) != 0)
					return (n < 0) ? false : true;
				total_lz_bytes = d->m_total_lz_bytes; pLZ_code_buf = d->m_pLZ_code_buf; pLZ_flags = d->m_pLZ_flags; num_flags_left = d->m_num_flags_left;
			}
		}
	}

	d->m_lookahead_pos = lookahead_pos; d->m_lookahead_size = lookahead_size; d->m_dict_size = dict_size;
	d->m_total_lz_bytes = total_lz_bytes; d->m_pLZ_code_buf = pLZ_code_buf; d->m_pLZ_flags = pLZ_flags; d->m_num_flags_left = num_flags_left;
	return true;
}

status compress(compressor *d, const void *pIn_buf, const size_t pIn_buf_size, flush flush) {
	d->m_pIn_buf = pIn_buf;
	d->m_pSrc = (const uint8 *)(pIn_buf); d->m_src_buf_left = pIn_buf_size;
	d->m_out_buf_ofs = 0;
	d->m_flush = flush;
	d->m_wants_to_finish |= (flush == FINISH);

	if ((d->m_output_flush_remaining) || (d->m_finished))
		return (d->m_prev_return_status = ((d->m_finished && !d->m_output_flush_remaining) ? STATUS_DONE : STATUS_OKAY));

	if (((d->m_flags & GREEDY_PARSING_FLAG) != 0) &&
			((d->m_flags & (FILTER_MATCHES | FORCE_ALL_RAW_BLOCKS | RLE_MATCHES)) == 0)) {
		compress_fast(d);
	}
	else error(d->m_flags);

	if ((d->m_flags & (WRITE_ZLIB_HEADER | COMPUTE_ADLER32)) && (pIn_buf))
		d->m_adler32 = adler32(d->m_adler32, (const uint8 *)pIn_buf, d->m_pSrc - (const uint8 *)pIn_buf);

	if ((flush) && (!d->m_lookahead_size) && (!d->m_src_buf_left) && (!d->m_output_flush_remaining)) {
		flush_block(d, flush);
		d->m_finished = (flush == FINISH);
		if (flush == FULL_FLUSH) { clear(d->m_hash); clear(d->m_next); d->m_dict_size = 0; }
	}
	return STATUS_DONE;
}

status init(compressor *d, put_buf_func_ptr pPut_buf_func, void *pPut_buf_user, int flags) {
	d->m_pPut_buf_func = pPut_buf_func; d->m_pPut_buf_user = pPut_buf_user;
	d->m_flags = (uint)(flags); d->m_max_probes[0] = 1 + ((flags & 0xFFF) + 2) / 3; d->m_greedy_parsing = (flags & GREEDY_PARSING_FLAG) != 0;
	d->m_max_probes[1] = 1 + (((flags & 0xFFF) >> 2) + 2) / 3;
	if (!(flags & NONDETERMINISTIC_PARSING_FLAG)) clear(d->m_hash);
	d->m_lookahead_pos = d->m_lookahead_size = d->m_dict_size = d->m_total_lz_bytes = d->m_lz_code_buf_dict_pos = d->m_bits_in = 0;
	d->m_output_flush_ofs = d->m_output_flush_remaining = d->m_finished = d->m_block_index = d->m_bit_buffer = d->m_wants_to_finish = 0;
	d->m_pLZ_code_buf = d->m_lz_code_buf + 1; d->m_pLZ_flags = d->m_lz_code_buf; d->m_num_flags_left = 8;
	d->m_pOutput_buf = d->m_output_buf; d->m_pOutput_buf_end = d->m_output_buf; d->m_prev_return_status = STATUS_OKAY;
	d->m_saved_match_dist = d->m_saved_match_len = d->m_saved_lit = 0; d->m_adler32 = 1;
	d->m_pIn_buf = nullptr;
	d->m_flush = NO_FLUSH; d->m_pSrc = nullptr; d->m_src_buf_left = 0; d->m_out_buf_ofs = 0;
	memset(&d->m_huff_count[0][0], 0, sizeof(d->m_huff_count[0][0]) * MAX_HUFF_SYMBOLS_0);
	memset(&d->m_huff_count[1][0], 0, sizeof(d->m_huff_count[1][0]) * MAX_HUFF_SYMBOLS_1);
	return STATUS_OKAY;
}

struct output_buffer {
	size_t m_size, m_capacity;
	uint8 *m_pBuf;
	bool m_expandable;
};

static bool output_buffer_putter(const void *pBuf, int len, void *pUser) {
	output_buffer *p = (output_buffer *)pUser;
	size_t new_size = p->m_size + len;
	if (new_size > p->m_capacity) {
		size_t new_capacity = p->m_capacity; uint8 *pNew_buf; if (!p->m_expandable) return false;
		do { new_capacity = max(128UL, new_capacity << 1U); } while (new_size > new_capacity);
		pNew_buf = (uint8*)realloc(p->m_pBuf, new_capacity); if (!pNew_buf) return false;
		p->m_pBuf = pNew_buf; p->m_capacity = new_capacity;
	}
	memcpy((uint8*)p->m_pBuf + p->m_size, pBuf, len); p->m_size = new_size;
	return true;
}

buffer<byte> deflate(const ref<byte> source, bool zlib) {
	buffer<byte> data; size_t size=0;
	assert(source.data && source.size);
	output_buffer out_buf = {0,0,0,0};
	out_buf.m_expandable = true;
	compressor pComp;
	init(&pComp, output_buffer_putter, &out_buf, GREEDY_PARSING_FLAG|(zlib?WRITE_ZLIB_HEADER:0));
	compress(&pComp, source.data, source.size, FINISH);
	size = out_buf.m_size;
	data.data = (byte*)out_buf.m_pBuf;
	data.capacity=data.size=size;
	assert(data, data.size, data.data, data.capacity, size);
	return data;
}
