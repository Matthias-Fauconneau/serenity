// Public domain, Rich Geldreich <richgel99@gmail.com>
#include "inflate.h"

#if defined(_LP64) || defined(__LP64__)
#define USE_64BIT_BITBUF 1
#endif

// Decompression flags used by decompress().
// FLAG_PARSE_ZLIB_HEADER: If set, the input has a valid zlib header and ends with an adler32 checksum (it's a valid zlib stream). Otherwise, the input is a raw deflate stream.
// FLAG_HAS_MORE_INPUT: If set, there are more input bytes available beyond the end of the supplied input buffer. If clear, the input buffer contains all remaining input.
// FLAG_USING_NON_WRAPPING_OUTPUT_BUF: If set, the output buffer is large enough to hold the entire decompressed stream. If clear, the output buffer is at least the size of the dictionary (typically 32KB).
// FLAG_COMPUTE_ADLER32: Force adler-32 checksum computation of the decompressed bytes.
enum
{
  FLAG_PARSE_ZLIB_HEADER = 1,
  FLAG_HAS_MORE_INPUT = 2,
  FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4,
  FLAG_COMPUTE_ADLER32 = 8
};

struct decompressor_tag; typedef struct decompressor_tag decompressor;

// Max size of LZ dictionary.
#define LZ_DICT_SIZE 32768

// Return status.
typedef enum
{
  STATUS_BAD_PARAM = -3,
  STATUS_ADLER32_MISMATCH = -2,
  STATUS_FAILED = -1,
  STATUS_DONE = 0,
  STATUS_NEEDS_MORE_INPUT = 1,
  STATUS_HAS_MORE_OUTPUT = 2
} status;

// Initializes the decompressor to its initial state.
#define init(r) do { (r)->m_state = 0; }  while (0)
#define get_adler32(r) (r)->m_check_adler32

// Main low-level decompressor coroutine function. This is the only function actually needed for decompression. All the other functions are just high-level helpers for improved usability.
// This is a universal API, i.e. it can be used as a building block to build any desired higher level decompression API. In the limit case, it can be called once per every byte input or output.
status decompress(decompressor *r, const uint8 *pIn_buf_next, uint *pIn_buf_size, uint8 *pOut_buf_start, uint8 *pOut_buf_next, uint *pOut_buf_size, const bool zlib);

// Internal/private bits follow.
enum
{
  MAX_HUFF_TABLES = 3, MAX_HUFF_SYMBOLS_0 = 288, MAX_HUFF_SYMBOLS_1 = 32, MAX_HUFF_SYMBOLS_2 = 19,
  FAST_LOOKUP_BITS = 10, FAST_LOOKUP_SIZE = 1 << FAST_LOOKUP_BITS
};

typedef struct
{
  uint8 m_code_size[MAX_HUFF_SYMBOLS_0];
  int16 m_look_up[FAST_LOOKUP_SIZE], m_tree[MAX_HUFF_SYMBOLS_0 * 2];
} huff_table;

#if USE_64BIT_BITBUF
  typedef uint64 bit_buf_t;
#else
  typedef uint32 bit_buf_t;
#endif

struct decompressor_tag
{
  uint32 m_state, m_num_bits, m_zhdr0, m_zhdr1, m_z_adler32, m_final, m_type, m_check_adler32, m_dist, m_counter, m_num_extra, m_table_sizes[MAX_HUFF_TABLES];
  bit_buf_t m_bit_buf;
  uint m_dist_from_out_buf_start;
  huff_table m_tables[MAX_HUFF_TABLES];
  uint8 m_raw_header[4], m_len_codes[MAX_HUFF_SYMBOLS_0 + MAX_HUFF_SYMBOLS_1 + 137];
};

#define MZ_MAX(a,b) (((a)>(b))?(a):(b))
#define MZ_MIN(a,b) (((a)<(b))?(a):(b))
#define MZ_CLEAR_OBJ(obj) __builtin_memset(obj,0,sizeof(obj))

  #define MZ_READ_LE16(p) *((const uint16 *)(p))
  #define MZ_READ_LE32(p) *((const uint32 *)(p))

#define MEMCPY(d, s, l) copy((byte*)(d), (byte*)(s), l)
#define MEMSET(p, c, l) __builtin_memset((byte*)(p), (byte)(c), l)

#define CR_BEGIN switch(r->m_state) { case 0:
#define CR_RETURN(state_index, result) do { status = result; r->m_state = state_index; goto common_exit; case state_index:; } while (0)
#define CR_RETURN_FOREVER(state_index, result) do { for ( ; ; ) { CR_RETURN(state_index, result); } } while (0)
#define CR_FINISH }

// TODO: If the caller has indicated that there's no more input, and we attempt to read beyond the input buf, then something is wrong with the input because the inflator never
// reads ahead more than it needs to. Currently GET_BYTE() pads the end of the stream with 0's in this scenario.
#define GET_BYTE(state_index, c) do { \
  if (pIn_buf_cur >= pIn_buf_end) { \
    for ( ; ; ) { \
{ \
        c = 0; \
        break; \
      } \
    } \
  } else c = *pIn_buf_cur++; } while (0)

#define NEED_BITS(state_index, n) do { uint c; GET_BYTE(state_index, c); bit_buf |= (((bit_buf_t)c) << num_bits); num_bits += 8; } while (num_bits < (uint)(n))
#define SKIP_BITS(state_index, n) do { if (num_bits < (uint)(n)) { NEED_BITS(state_index, n); } bit_buf >>= (n); num_bits -= (n); } while (0)
#define GET_BITS(state_index, b, n) do { if (num_bits < (uint)(n)) { NEED_BITS(state_index, n); } b = bit_buf & ((1 << (n)) - 1); bit_buf >>= (n); num_bits -= (n); } while (0)

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
    } GET_BYTE(state_index, c); bit_buf |= (((bit_buf_t)c) << num_bits); num_bits += 8; \
  } while (num_bits < 15);

// HUFF_DECODE() decodes the next Huffman coded symbol. It's more complex than you would initially expect because the zlib API expects the decompressor to never read
// beyond the final byte of the deflate stream. (In other words, when this macro wants to read another byte from the input, it REALLY needs another byte in order to fully
// decode the next Huffman code.) Handling this properly is particularly important on raw deflate (non-zlib) streams, which aren't followed by a byte aligned adler-32.
// The slow path is only executed at the very end of the input buffer.
#define HUFF_DECODE(state_index, sym, pHuff) do { \
  int temp; uint code_len, c; \
  if (num_bits < 15) { \
    if ((pIn_buf_end - pIn_buf_cur) < 2) { \
       HUFF_BITBUF_FILL(state_index, pHuff); \
    } else { \
       bit_buf |= (((bit_buf_t)pIn_buf_cur[0]) << num_bits) | (((bit_buf_t)pIn_buf_cur[1]) << (num_bits + 8)); pIn_buf_cur += 2; num_bits += 16; \
    } \
  } \
  if ((temp = (pHuff)->m_look_up[bit_buf & (FAST_LOOKUP_SIZE - 1)]) >= 0) \
    code_len = temp >> 9, temp &= 511; \
  else { \
    code_len = FAST_LOOKUP_BITS; do { temp = (pHuff)->m_tree[~temp + ((bit_buf >> code_len++) & 1)]; } while (temp < 0); \
  } sym = temp; bit_buf >>= code_len; num_bits -= code_len; } while (0)

status decompress(decompressor *r, const uint8 *pIn_buf_next, uint *pIn_buf_size, uint8 *pOut_buf_start, uint8 *pOut_buf_next, uint *pOut_buf_size, const bool zlib)
{
  static const int s_length_base[31] = { 3,4,5,6,7,8,9,10,11,13, 15,17,19,23,27,31,35,43,51,59, 67,83,99,115,131,163,195,227,258,0,0 };
  static const int s_length_extra[31]= { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0 };
  static const int s_dist_base[32] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193, 257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,0,0};
  static const int s_dist_extra[32] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
  static const uint8 s_length_dezigzag[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
  static const int s_min_table_sizes[3] = { 257, 1, 4 };

  status status = STATUS_FAILED; uint32 num_bits, dist, counter, num_extra; bit_buf_t bit_buf;
  const uint8 *pIn_buf_cur = pIn_buf_next, *const pIn_buf_end = pIn_buf_next + *pIn_buf_size;
  uint8 *pOut_buf_cur = pOut_buf_next, *const pOut_buf_end = pOut_buf_next + *pOut_buf_size;
  uint out_buf_size_mask = (uint)-1;

  // Ensure the output buffer's size is a power of 2, unless the output buffer is large enough to hold the entire output file (in which case it doesn't matter).
  if (((out_buf_size_mask + 1) & out_buf_size_mask) || (pOut_buf_next < pOut_buf_start)) { *pIn_buf_size = *pOut_buf_size = 0; return STATUS_BAD_PARAM; }

  num_bits = r->m_num_bits; bit_buf = r->m_bit_buf; dist = r->m_dist; counter = r->m_counter; num_extra = r->m_num_extra;
  uint dist_from_out_buf_start = r->m_dist_from_out_buf_start;
  CR_BEGIN

  bit_buf = num_bits = dist = counter = num_extra = r->m_zhdr0 = r->m_zhdr1 = 0; r->m_z_adler32 = r->m_check_adler32 = 1;
  if (zlib)
  {
    GET_BYTE(1, r->m_zhdr0); GET_BYTE(2, r->m_zhdr1);
    counter = (((r->m_zhdr0 * 256 + r->m_zhdr1) % 31 != 0) || (r->m_zhdr1 & 32) || ((r->m_zhdr0 & 15) != 8));
    if (counter) { CR_RETURN_FOREVER(36, STATUS_FAILED); }
  }

  do
  {
    GET_BITS(3, r->m_final, 3); r->m_type = r->m_final >> 1;
    if (r->m_type == 0)
    {
      SKIP_BITS(5, num_bits & 7);
      for (counter = 0; counter < 4; ++counter) { if (num_bits) GET_BITS(6, r->m_raw_header[counter], 8); else GET_BYTE(7, r->m_raw_header[counter]); }
      if ((counter = (r->m_raw_header[0] | (r->m_raw_header[1] << 8))) != (uint)(0xFFFF ^ (r->m_raw_header[2] | (r->m_raw_header[3] << 8)))) { CR_RETURN_FOREVER(39, STATUS_FAILED); }
      while ((counter) && (num_bits))
      {
        GET_BITS(51, dist, 8);
        while (pOut_buf_cur >= pOut_buf_end) { CR_RETURN(52, STATUS_HAS_MORE_OUTPUT); }
        *pOut_buf_cur++ = (uint8)dist;
        counter--;
      }
      while (counter)
      {
        uint n; while (pOut_buf_cur >= pOut_buf_end) { CR_RETURN(9, STATUS_HAS_MORE_OUTPUT); }
        while (pIn_buf_cur >= pIn_buf_end)
        {
            CR_RETURN_FOREVER(40, STATUS_FAILED);
        }
        n = MZ_MIN(MZ_MIN((uint)(pOut_buf_end - pOut_buf_cur), (uint)(pIn_buf_end - pIn_buf_cur)), counter);
        MEMCPY(pOut_buf_cur, pIn_buf_cur, n); pIn_buf_cur += n; pOut_buf_cur += n; counter -= (uint)n;
      }
    }
    else if (r->m_type == 3)
    {
      CR_RETURN_FOREVER(10, STATUS_FAILED);
    }
    else
    {
      if (r->m_type == 1)
      {
        uint8 *p = r->m_tables[0].m_code_size; uint i;
        r->m_table_sizes[0] = 288; r->m_table_sizes[1] = 32; MEMSET(r->m_tables[1].m_code_size, 5, 32);
        for ( i = 0; i <= 143; ++i) *p++ = 8; for ( ; i <= 255; ++i) *p++ = 9; for ( ; i <= 279; ++i) *p++ = 7; for ( ; i <= 287; ++i) *p++ = 8;
      }
      else
      {
        for (counter = 0; counter < 3; counter++) { GET_BITS(11, r->m_table_sizes[counter], "\05\05\04"[counter]); r->m_table_sizes[counter] += s_min_table_sizes[counter]; }
        MZ_CLEAR_OBJ(r->m_tables[2].m_code_size); for (counter = 0; counter < r->m_table_sizes[2]; counter++) { uint s; GET_BITS(14, s, 3); r->m_tables[2].m_code_size[s_length_dezigzag[counter]] = (uint8)s; }
        r->m_table_sizes[2] = 19;
      }
      for ( ; (int)r->m_type >= 0; r->m_type--)
      {
        int tree_next, tree_cur; huff_table *pTable;
        uint i, j, used_syms, total, sym_index, next_code[17], total_syms[16]; pTable = &r->m_tables[r->m_type]; MZ_CLEAR_OBJ(total_syms); MZ_CLEAR_OBJ(pTable->m_look_up); MZ_CLEAR_OBJ(pTable->m_tree);
        for (i = 0; i < r->m_table_sizes[r->m_type]; ++i) total_syms[pTable->m_code_size[i]]++;
        used_syms = 0, total = 0; next_code[0] = next_code[1] = 0;
        for (i = 1; i <= 15; ++i) { used_syms += total_syms[i]; next_code[i + 1] = (total = ((total + total_syms[i]) << 1)); }
        if ((65536 != total) && (used_syms > 1))
        {
          CR_RETURN_FOREVER(35, STATUS_FAILED);
        }
        for (tree_next = -1, sym_index = 0; sym_index < r->m_table_sizes[r->m_type]; ++sym_index)
        {
          uint rev_code = 0, l, cur_code, code_size = pTable->m_code_size[sym_index]; if (!code_size) continue;
          cur_code = next_code[code_size]++; for (l = code_size; l > 0; l--, cur_code >>= 1) rev_code = (rev_code << 1) | (cur_code & 1);
          if (code_size <= FAST_LOOKUP_BITS) { int16 k = (int16)((code_size << 9) | sym_index); while (rev_code < FAST_LOOKUP_SIZE) { pTable->m_look_up[rev_code] = k; rev_code += (1 << code_size); } continue; }
          if (0 == (tree_cur = pTable->m_look_up[rev_code & (FAST_LOOKUP_SIZE - 1)])) { pTable->m_look_up[rev_code & (FAST_LOOKUP_SIZE - 1)] = (int16)tree_next; tree_cur = tree_next; tree_next -= 2; }
          rev_code >>= (FAST_LOOKUP_BITS - 1);
          for (j = code_size; j > (FAST_LOOKUP_BITS + 1); j--)
          {
            tree_cur -= ((rev_code >>= 1) & 1);
            if (!pTable->m_tree[-tree_cur - 1]) { pTable->m_tree[-tree_cur - 1] = (int16)tree_next; tree_cur = tree_next; tree_next -= 2; } else tree_cur = pTable->m_tree[-tree_cur - 1];
          }
          tree_cur -= ((rev_code >>= 1) & 1); pTable->m_tree[-tree_cur - 1] = (int16)sym_index;
        }
        if (r->m_type == 2)
        {
          for (counter = 0; counter < (r->m_table_sizes[0] + r->m_table_sizes[1]); )
          {
            uint s; HUFF_DECODE(16, dist, &r->m_tables[2]); if (dist < 16) { r->m_len_codes[counter++] = (uint8)dist; continue; }
            if ((dist == 16) && (!counter))
            {
              CR_RETURN_FOREVER(17, STATUS_FAILED);
            }
            num_extra = "\02\03\07"[dist - 16]; GET_BITS(18, s, num_extra); s += "\03\03\013"[dist - 16];
            MEMSET(r->m_len_codes + counter, (dist == 16) ? r->m_len_codes[counter - 1] : 0, s); counter += s;
          }
          if ((r->m_table_sizes[0] + r->m_table_sizes[1]) != counter)
          {
            CR_RETURN_FOREVER(21, STATUS_FAILED);
          }
          MEMCPY(r->m_tables[0].m_code_size, r->m_len_codes, r->m_table_sizes[0]); MEMCPY(r->m_tables[1].m_code_size, r->m_len_codes + r->m_table_sizes[0], r->m_table_sizes[1]);
        }
      }
      for ( ; ; )
      {
        uint8 *pSrc;
        for ( ; ; )
        {
          if (((pIn_buf_end - pIn_buf_cur) < 4) || ((pOut_buf_end - pOut_buf_cur) < 2))
          {
            HUFF_DECODE(23, counter, &r->m_tables[0]);
            if (counter >= 256)
              break;
            while (pOut_buf_cur >= pOut_buf_end) { CR_RETURN(24, STATUS_HAS_MORE_OUTPUT); }
            *pOut_buf_cur++ = (uint8)counter;
          }
          else
          {
            int sym2; uint code_len;
#if USE_64BIT_BITBUF
            if (num_bits < 30) { bit_buf |= (((bit_buf_t)MZ_READ_LE32(pIn_buf_cur)) << num_bits); pIn_buf_cur += 4; num_bits += 32; }
#else
            if (num_bits < 15) { bit_buf |= (((bit_buf_t)MZ_READ_LE16(pIn_buf_cur)) << num_bits); pIn_buf_cur += 2; num_bits += 16; }
#endif
            if ((sym2 = r->m_tables[0].m_look_up[bit_buf & (FAST_LOOKUP_SIZE - 1)]) >= 0)
              code_len = sym2 >> 9;
            else
            {
              code_len = FAST_LOOKUP_BITS; do { sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)]; } while (sym2 < 0);
            }
            counter = sym2; bit_buf >>= code_len; num_bits -= code_len;
            if (counter & 256)
              break;

#if !USE_64BIT_BITBUF
            if (num_bits < 15) { bit_buf |= (((bit_buf_t)MZ_READ_LE16(pIn_buf_cur)) << num_bits); pIn_buf_cur += 2; num_bits += 16; }
#endif
            if ((sym2 = r->m_tables[0].m_look_up[bit_buf & (FAST_LOOKUP_SIZE - 1)]) >= 0)
              code_len = sym2 >> 9;
            else
            {
              code_len = FAST_LOOKUP_BITS; do { sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)]; } while (sym2 < 0);
            }
            bit_buf >>= code_len; num_bits -= code_len;

            pOut_buf_cur[0] = (uint8)counter;
            if (sym2 & 256)
            {
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
        if (dist > dist_from_out_buf_start)
        {
          CR_RETURN_FOREVER(37, STATUS_FAILED);
        }

        pSrc = pOut_buf_start + ((dist_from_out_buf_start - dist) & out_buf_size_mask);

        if ((MZ_MAX(pOut_buf_cur, pSrc) + counter) > pOut_buf_end)
        {
          while (counter--)
          {
            while (pOut_buf_cur >= pOut_buf_end) { CR_RETURN(53, STATUS_HAS_MORE_OUTPUT); }
            *pOut_buf_cur++ = pOut_buf_start[(dist_from_out_buf_start++ - dist) & out_buf_size_mask];
          }
          continue;
        }
        else if ((counter >= 9) && (counter <= dist))
        {
          const uint8 *pSrc_end = pSrc + (counter & ~7);
          do
          {
            ((uint32 *)pOut_buf_cur)[0] = ((const uint32 *)pSrc)[0];
            ((uint32 *)pOut_buf_cur)[1] = ((const uint32 *)pSrc)[1];
            pOut_buf_cur += 8;
          } while ((pSrc += 8) < pSrc_end);
          if ((counter &= 7) < 3)
          {
            if (counter)
            {
              pOut_buf_cur[0] = pSrc[0];
              if (counter > 1)
                pOut_buf_cur[1] = pSrc[1];
              pOut_buf_cur += counter;
            }
            continue;
          }
        }
        do
        {
          pOut_buf_cur[0] = pSrc[0];
          pOut_buf_cur[1] = pSrc[1];
          pOut_buf_cur[2] = pSrc[2];
          pOut_buf_cur += 3; pSrc += 3;
        } while ((int)(counter -= 3) > 2);
        if ((int)counter > 0)
        {
          pOut_buf_cur[0] = pSrc[0];
          if ((int)counter > 1)
            pOut_buf_cur[1] = pSrc[1];
          pOut_buf_cur += counter;
        }
      }
    }
  } while (!(r->m_final & 1));
  if(zlib) {
    SKIP_BITS(32, num_bits & 7); for (counter = 0; counter < 4; ++counter) { uint s; if (num_bits) GET_BITS(41, s, 8); else GET_BYTE(42, s); r->m_z_adler32 = (r->m_z_adler32 << 8) | s; }
  }
  CR_RETURN_FOREVER(34, STATUS_DONE);
  CR_FINISH

common_exit:
  r->m_num_bits = num_bits; r->m_bit_buf = bit_buf; r->m_dist = dist; r->m_counter = counter; r->m_num_extra = num_extra; r->m_dist_from_out_buf_start = dist_from_out_buf_start;
  *pIn_buf_size = pIn_buf_cur - pIn_buf_next; *pOut_buf_size = pOut_buf_cur - pOut_buf_next;
  if (zlib && (status >= 0))
  {
    const uint8 *ptr = pOut_buf_next; uint buf_len = *pOut_buf_size;
    uint32 i, s1 = r->m_check_adler32 & 0xffff, s2 = r->m_check_adler32 >> 16; uint block_len = buf_len % 5552;
    while (buf_len)
    {
      for (i = 0; i + 7 < block_len; i += 8, ptr += 8)
      {
        s1 += ptr[0], s2 += s1; s1 += ptr[1], s2 += s1; s1 += ptr[2], s2 += s1; s1 += ptr[3], s2 += s1;
        s1 += ptr[4], s2 += s1; s1 += ptr[5], s2 += s1; s1 += ptr[6], s2 += s1; s1 += ptr[7], s2 += s1;
      }
      for ( ; i < block_len; ++i) s1 += *ptr++, s2 += s1;
      s1 %= 65521U, s2 %= 65521U; buf_len -= block_len; block_len = 5552;
    }
    r->m_check_adler32 = (s2 << 16) + s1; if ((status == STATUS_DONE) && zlib && (r->m_check_adler32 != r->m_z_adler32)) status = STATUS_ADLER32_MISMATCH;
  }
  return status;
}

array<byte> inflate(const ref<byte>& source, bool zlib) {
    decompressor decomp;
    uint8* buffer = 0;
    uint src_buf_ofs = 0, out_buf_capacity = 0;
    uint pOut_len = 0;
    decomp.m_state=0;
    for(;;) {
      uint src_buf_size = source.size - src_buf_ofs, dst_buf_size = out_buf_capacity - pOut_len, new_out_buf_capacity;
      int status = decompress(&decomp, (const uint8*)source.data + src_buf_ofs, &src_buf_size, buffer, buffer ? buffer + pOut_len : 0, &dst_buf_size, zlib);
      if(status<0 || status==STATUS_NEEDS_MORE_INPUT) error("inflate error"_);
      src_buf_ofs += src_buf_size;
      pOut_len += dst_buf_size;
      if (status == STATUS_DONE) break;
      new_out_buf_capacity = out_buf_capacity * 2; if (new_out_buf_capacity < 4096) new_out_buf_capacity = 4096;
      reallocate(buffer, new_out_buf_capacity);
      out_buf_capacity = new_out_buf_capacity;
    }
    array<byte> data; data.data = (byte*)buffer; data.size=pOut_len; data.capacity=out_buf_capacity; return data;
}
