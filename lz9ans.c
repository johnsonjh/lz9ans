/*
 * LZ9ANS
 * Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
 * Copyright (c) 2025-2026 The DPS8M Development Team
 * SPDX-License-Identifier: MIT
 * scspell-id: 12a507e4-d24b-11f1-8fe4-80ee73e9b8e7
 */

/*****************************************************************************/

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*****************************************************************************/

#include "crc32_hw.h"

/*****************************************************************************/

#undef LZ9VER
#define LZ9VER "v0.9.1 (2026-11-06)"

/*****************************************************************************/

#define BLOCK_N 32768U
#define WINDOW_DEFAULT 32768U
#define WINDOW_64K 65536U
#define FEATURE_WINDOW_64K 1U
#define FEATURE_PHASED_TABLES 2U
#define FEATURE_MIN_MATCH_2 4U
#define FEATURE_ALIGNED_DISTANCES 8U
#define FEATURE_AUTO_ALIGNED_DISTANCES 16U
#define FEATURE_KNOWN_MASK 31U
#define OPTION_ALIGNED_DISTANCES 4U
#define OPTION_AUTO_ALIGNED_DISTANCES 8U
#define BLOCK_FLAG_ALIGNED_DISTANCES 16U
#define HASH_N 8192U
#define HASH_MASK (HASH_N - 1U)
#define TABLE_LOG 10
#define TABLE_N (1U << TABLE_LOG)
#define SEQ_MAX (BLOCK_N / 2U + 1U)
#define MIN_MATCH_DEFAULT 3U
#define MIN_MATCH_SHORT 2U
#define PHASE_GROUPS_MAX 2U
#define PHASE_MIN_SYMBOLS 64U
#define DEC_TABLE_COUNT 11U
#define DEC_POISON_SLOT 10U
#define STAGE_MAX 81920U
#define TABLE_CACHE_COUNT 10U
#define TABLE_CACHE_BYTES (TABLE_CACHE_COUNT * 512U * sizeof (unsigned short))
#define STAGE_DATA_MAX ((STAGE_MAX - TABLE_CACHE_BYTES - 7UL) & ~7UL)
#define DESC_MAX 8192U
#define BLOCK_PREFIX_N 3U
#define LZ_BLOCK_HEADER_N 23U
#define STORED_BLOCK_HEADER_N 7U
#define FRAME_HEADER_PREFIX_N 13U
#define FRAME_HEADER_MIN_N 17U
#define FRAME_HEADER_MAX_N 4096U
#define FHEAD_N FRAME_HEADER_MIN_N
#define CRC_BUFFER_N 4096U

/*****************************************************************************/

#if !defined(LZ9ANS_EARLY_STORED_LITERAL_PERCENT)
# define LZ9ANS_EARLY_STORED_LITERAL_PERCENT 97U
#endif /* if !defined(LZ9ANS_EARLY_STORED_LITERAL_PERCENT) */

/*****************************************************************************/

#if LZ9ANS_EARLY_STORED_LITERAL_PERCENT > 100U
# error LZ9ANS_EARLY_STORED_LITERAL_PERCENT must not exceed 100
#endif /* if LZ9ANS_EARLY_STORED_LITERAL_PERCENT > 100U */

/*****************************************************************************/

#if !defined(LZ9ANS_L8_LAZY_MAX_LENGTH)
# define LZ9ANS_L8_LAZY_MAX_LENGTH 32U
#endif /* if !defined(LZ9ANS_L8_LAZY_MAX_LENGTH) */

/*****************************************************************************/

#if LZ9ANS_L8_LAZY_MAX_LENGTH < 4U
# error LZ9ANS_L8_LAZY_MAX_LENGTH must be at least 4
#endif /* if LZ9ANS_L8_LAZY_MAX_LENGTH < 4U */

/*****************************************************************************/

#define MODE_EMPTY 0
#define MODE_RAW 1
#define MODE_RLE 2
#define MODE_TANS 3
#define MODE_REUSE 4
#define MODE_PHASED 5

/*****************************************************************************/

#define BLOCK_STORED 0
#define BLOCK_LZ 1

/*****************************************************************************/

#define STREAM_LIT 0
#define STREAM_LL 1
#define STREAM_ML 2
#define STREAM_OF 3
#define STREAM_EXTRA 4
#define FORMAT_V9 9U
#define STREAM_COUNT 5U
#define ENTROPY_STREAM_COUNT 4U
#define OF_ALPHABET_32K 20U
#define OF_ALPHABET_64K 21U
#define COST_SCALE 256U
#define FRAME_EXT_UNKNOWN_COUNT 0x81U

/*****************************************************************************/

#if defined(__linux) || defined(__linux__) || defined(linux) || \
    defined(__gnu_linux__) /* Manually disable if using threads! */
# undef fgetc
# define fgetc(s) fgetc_unlocked(s) /* //-V1059 */
# undef fread
# define fread(p, s, n, f) fread_unlocked(p, s, n, f) /* //-V1059 */
# undef fwrite
# define fwrite(p, s, n, f) fwrite_unlocked(p, s, n, f) /* //-V1059 */
# undef fflush
# define fflush(f) fflush_unlocked(f) /* //-V1059 */
#endif /* if defined(__linux) || defined(__linux__) || defined(linux) \
          || defined(__gnu_linux__) */

/*****************************************************************************/

#if defined(_WIN32) || defined(_WIN64) /* Manually disable if using threads! */
# undef fgetc
# define fgetc(s) _fgetc_nolock(s) /* //-V1059 */
# undef fread
# define fread(p, s, n, f) _fread_nolock(p, s, n, f) /* //-V1059 */
# undef fwrite
# define fwrite(p, s, n, f) _fwrite_nolock(p, s, n, f) /* //-V1059 */
# undef ftell
# define ftell(f) _ftell_nolock(f) /* //-V1059 */
# undef fseek
# define fseek(f, n, p) _fseek_nolock(f, n, p) /* //-V1059 */
# undef fflush
# define fflush(f) _fflush_nolock(f) /* //-V1059 */
# undef fclose
# define fclose(f) _fclose_nolock(f) /* //-V1059 */
#endif /* if defined(_WIN32) || defined(_WIN64) */

/*****************************************************************************/

#if defined(__GNUC__) && !defined(__CPPCHECK__) && !defined(__LINT__)
# define LZ9_INLINE __inline__
#else
# define LZ9_INLINE
#endif /* if defined(__GNUC__) && !defined(__CPPCHECK__)
          && !defined(__LINT__)  */

/*****************************************************************************/

#undef HAS_ATTRIBUTE
#if defined __has_attribute && (defined(__clang__) || defined(__GNUC__))
# define HAS_ATTRIBUTE(atr) __has_attribute(atr)
#else
# define HAS_ATTRIBUTE(atr) 0
#endif /* if defined __has_attribute && (defined(__clang__)
          || defined(__GNUC__))  */

/*****************************************************************************/

#undef HOT
#if !defined(__CPPCHECK__) && !defined(__LINT__)
# if HAS_ATTRIBUTE(hot)
#  define HOT __attribute__((hot))
# endif /* if HAS_ATTRIBUTE(hot) */
#endif /* if !defined(__CPPCHECK__) && !defined(__LINT__) */

/*****************************************************************************/

#if !defined(HOT)
# define HOT /**/
#endif /* if !defined(HOT) */

/*****************************************************************************/

#if defined(__GNUC__) && !defined(__CPPCHECK__) && !defined(__LINT__)
__extension__ typedef unsigned long long lz9_u64;
typedef char lz9_u64_type_must_be_at_least_64_bits_compile_time_assertion[
  sizeof (lz9_u64) * CHAR_BIT >= 64 ? 1 : -1];
#endif /* if defined(__GNUC__) && !defined(__CPPCHECK__)
          && !defined(__LINT__) */

/*****************************************************************************/

typedef struct
{
  unsigned short ll, ml;
  unsigned int dist;
} Sequence;

/*****************************************************************************/

#define SEQ_DIST_MASK 0x1ffffU
#define SEQ_TAG_SHIFT 17U

/*****************************************************************************/

typedef struct
{
  unsigned short freq[512];
  unsigned short cum[512];
  unsigned short estate[TABLE_N];
  unsigned short threshold[512];
  unsigned char highbits[512];
  unsigned short alphabet;
} AnsTable;

/*****************************************************************************/

#define DEC_BASE_MASK 1023U
#define DEC_NB_SHIFT 10U
#define DEC_NB_MASK 15U
#define DEC_SYM_SHIFT 14U
#define DEC_POISON (1023U << DEC_SYM_SHIFT)

/*****************************************************************************/

typedef struct
{
  unsigned int e[TABLE_N];
  unsigned short alphabet;
} DecTable;

/*****************************************************************************/

typedef struct
{
  unsigned char groups;
  unsigned char gmap[4];
} PhasePartition;

/*****************************************************************************/

static const PhasePartition phase_partitions[ENTROPY_STREAM_COUNT][2]
    = {
        { { 2, { 0, 1, 0, 0 } },   /* LIT 0: 023|1 */
          { 2, { 0, 1, 0, 1 } } }, /* LIT 1: 02|13 */
        { { 2, { 0, 1, 0, 1 } },   /*  LL 0: 02|13 */
          { 0, { 0, 0, 0, 0 } } },
        { { 2, { 0, 0, 1, 1 } },   /*  ML 0: 01|23 */
          { 0, { 0, 0, 0, 0 } } },
        { { 0, { 0, 0, 0, 0 } },
          { 0, { 0, 0, 0, 0 } } }
      };

/*****************************************************************************/

static const unsigned char phase_partition_count[ENTROPY_STREAM_COUNT]
    = { 2, 1, 1, 0 };

/*****************************************************************************/

static const unsigned char phase_table_base[3] = { 4, 6, 8 };

/*****************************************************************************/

typedef struct
{
  unsigned char *p;
  unsigned long capacity_bits;
  unsigned long bits;
  int failed;
} BitWriter;

/*****************************************************************************/

typedef struct
{
  const unsigned char *p;
  unsigned long bits;
  unsigned long size;
  unsigned long position;
  int backwards;
  int failed;
} BitReader;

/*****************************************************************************/

typedef struct
{
  int mode;
  unsigned short rle;
  unsigned char width;
  unsigned long count;
  unsigned long bits;
  const unsigned char *data;
  BitReader br;
  unsigned short state;
  DecTable *table;
  const DecTable *ptab[4];
} Stream;

/*****************************************************************************/

typedef struct
{
  BitReader *raw;
  int failed;
} ExtraReader;

/*****************************************************************************/

typedef struct
{
  unsigned short *buffer;
  Sequence *seq;
  int *head, *link;
  unsigned char *stage, *desc;
  AnsTable *table;
  unsigned int counts[512];
  unsigned int hlen;
  unsigned int sn;
  unsigned int n;
  unsigned int lit_count;
  unsigned int last_lit;
  unsigned int max_literal;
  unsigned long stream_bits[STREAM_COUNT];
  unsigned long stream_bytes[STREAM_COUNT];
  unsigned long desc_len;
  unsigned long stage_len;
  unsigned int input_mode;
  unsigned int distance_flags;
  unsigned int window_n;
  unsigned int of_alphabet;
  unsigned int current_kind;
  unsigned int search_depth;
  int lazy;
  int level;
  int use_cost;
  unsigned int min_match;
  unsigned int features;
  unsigned long frame_pos;
  AnsTable *gtable;
  unsigned int reps[3];
  unsigned short costs[4][512];
  unsigned int pcounts[4][512];
  unsigned int prefix[BLOCK_N + 1U];
} Encoder;

/*****************************************************************************/

typedef char encoder_prefix_must_hold_table_cache_compile_time_assertion[
  sizeof (((Encoder *)0)->prefix) >= TABLE_CACHE_BYTES ? 1 : -1];

/*****************************************************************************/

typedef char stage_must_hold_maximum_stored_block_compile_time_assertion[
  STAGE_DATA_MAX >= (BLOCK_N * 9U + 7U) / 8U ? 1 : -1];

/*****************************************************************************/

typedef struct
{
  unsigned short *ring;
  unsigned int distance_flags;
  unsigned int window_n;
  unsigned int of_alphabet;
  unsigned int min_match;
  int phased;
  unsigned char outbuf[8192];
  unsigned int out_used;
  unsigned long produced;
  unsigned int in_block;
  unsigned char crc_buf[CRC_BUFFER_N];
  unsigned int out_acc, out_bits;
  int out_mode;
  FILE *out;
  unsigned int reps[3];
} Output;

/*****************************************************************************/

#define ENCODER_ALIGNED_DISTANCES(e) \
  (((e)->distance_flags & OPTION_ALIGNED_DISTANCES) != 0U)

#define OUTPUT_ALIGNED_DISTANCES(o) \
  (((o)->distance_flags & OPTION_ALIGNED_DISTANCES) != 0U)

#define OUTPUT_AUTO_ALIGNED_DISTANCES(o) \
  (((o)->distance_flags & OPTION_AUTO_ALIGNED_DISTANCES) != 0U)

/*****************************************************************************/

static unsigned int
sequence_distance (const Sequence *q)
{
  return q->dist & SEQ_DIST_MASK;
}

/*****************************************************************************/

static unsigned int
sequence_tag (const Sequence *q)
{
  return q->dist >> SEQ_TAG_SHIFT;
}

/*****************************************************************************/

static int write_exact (FILE *f, const void *p, size_t n);
static LZ9_INLINE unsigned int stream_get (Stream *s);
static LZ9_INLINE unsigned int extra_get (ExtraReader *x, unsigned int n);

/*****************************************************************************/

static unsigned short *
table_cache (const Encoder *e, unsigned int kind)
{
  return (unsigned short *)(void *)(e->stage + STAGE_DATA_MAX
                                    + kind * 512U
                                      * sizeof (unsigned short));
}

/*****************************************************************************/

static int
stream_is_tans (int mode)
{
  return mode == MODE_TANS || mode == MODE_REUSE;
}

/*****************************************************************************/

static int
stream_uses_ans (int mode)
{
  return mode == MODE_TANS || mode == MODE_REUSE || mode == MODE_PHASED;
}

/*****************************************************************************/

static int
output_octet (Output *o, unsigned int v)
{
  o->outbuf[o->out_used++] = (unsigned char)v;

  if (o->out_used == sizeof (o->outbuf))
    {
      if (!write_exact (o->out, o->outbuf, sizeof (o->outbuf)))
        {
          return 0;
        }

      o->out_used = 0;
    }

  return 1;
}

/*****************************************************************************/

static int
output_flush (Output *o)
{
  if (o->out_used && !write_exact (o->out, o->outbuf, o->out_used))
    {
      return 0;
    }

  o->out_used = 0;

  return 1;
}

/*****************************************************************************/

static unsigned int crc_table[8][256];

/*****************************************************************************/

typedef char crc_slice_table_needs_32_bit_unsigned_int_compile_time_assertion[
  UINT_MAX >= 0xffffffffUL ? 1 : -1];

/*****************************************************************************/

static void
init_crc (void)
{
  unsigned int i, j;

  for (i = 0; i != 256U; ++i)
    {
      unsigned int v = i;

      for (j = 0; j != 8U; ++j)
        {
          v = (v & 1U) ? ((v >> 1) ^ 0xedb88320U) : (v >> 1);
        }

      crc_table[0][i] = v;
    }

  for (j = 1; j != 8U; ++j)
    {
      for (i = 0; i != 256U; ++i)
        {
          unsigned int v = crc_table[j - 1U][i];

          crc_table[j][i]
              = crc_table[0][v & 255U] ^ (v >> 8);
        }
    }
}

/*****************************************************************************/

static unsigned long
crc_update (unsigned long crc, const unsigned char *p, unsigned int n)
{
  unsigned int v;

#if defined(LZ9_CRC32_ARM)
  if (n >= 128U && (crc & ~0xffffffffUL) == 0UL && lz9_crc32_arm_available ())
    {
      return (unsigned long)lz9_crc32_arm ((unsigned int)crc, p, (size_t)n);
    }
#endif /* if defined(LZ9_CRC32_ARM) */

#if defined(LZ9_CRC32_PCLMUL)
  if (n >= 256U && (crc & ~0xffffffffUL) == 0UL
      && lz9_crc32_pclmul_available ())
    {
      unsigned int bulk = n & ~15U;
      crc = (unsigned long)lz9_crc32_pclmul ((unsigned int)crc, p,
                                             (size_t)bulk);
      p += bulk;
      n -= bulk;

      if (!n)
        {
          return crc;
        }
    }
#endif /* if defined(LZ9_CRC32_PCLMUL) */

  v = (unsigned int)(crc ^ 0xffffffffUL);

  while (n >= 8U)
    {
      v ^= (unsigned int)p[0] | ((unsigned int)p[1] << 8)
           | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
      v = crc_table[7][v & 255U]
          ^ crc_table[6][(v >>  8) & 255U]
          ^ crc_table[5][(v >> 16) & 255U]
          ^ crc_table[4][v >> 24]
          ^ crc_table[3][p[4]] ^ crc_table[2][p[5]]
          ^ crc_table[1][p[6]] ^ crc_table[0][p[7]];
      p += 8;
      n -= 8U;
    }

  while (n)
    {
      v = (v >> 8) ^ crc_table[0][(v ^ *p++) & 255U];
      --n;
    }

  return (unsigned long)(v ^ 0xffffffffU);
}

/*****************************************************************************/

static unsigned long
crc_bytes (const unsigned char *p, unsigned int n)
{
  return crc_update (0UL, p, n);
}

/*****************************************************************************/

static HOT LZ9_INLINE void
pack8_symbols (const unsigned short *v, unsigned char *p)
{
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__amd64__))
  lz9_u64 w0, w1, hi, hi_be;

  w0 = ((lz9_u64)v[0] << 27) | ((lz9_u64)v[1] << 18) | ((lz9_u64)v[2] << 9)
       | (lz9_u64)v[3];
  w1 = ((lz9_u64)v[4] << 27) | ((lz9_u64)v[5] << 18) | ((lz9_u64)v[6] << 9)
       | (lz9_u64)v[7];
  hi = (w0 << 28) | (w1 >> 8);
  hi_be = __builtin_bswap64 (hi);
  (void)memcpy (p, &hi_be, 8);
  p[8] = (unsigned char)w1;
#else
  p[0] = (unsigned char)(v[0] >> 1);
  p[1] = (unsigned char)(((v[0] & 1U) << 7) | (v[1] >> 2));
  p[2] = (unsigned char)(((v[1] & 3U) << 6) | (v[2] >> 3));
  p[3] = (unsigned char)(((v[2] & 7U) << 5) | (v[3] >> 4));
  p[4] = (unsigned char)(((v[3] & 15U) << 4) | (v[4] >> 5));
  p[5] = (unsigned char)(((v[4] & 31U) << 3) | (v[5] >> 6));
  p[6] = (unsigned char)(((v[5] & 63U) << 2) | (v[6] >> 7));
  p[7] = (unsigned char)(((v[6] & 127U) << 1) | (v[7] >> 8));
  p[8] = (unsigned char)v[7];
#endif /* if defined(__GNUC__) && (defined(__x86_64__)
          || defined(__amd64__)) */
}

/*****************************************************************************/

static unsigned int
pack9_tail (const unsigned short *v, unsigned int n, unsigned char *p)
{
  unsigned int i, pending = 0, nbits = 0, used = 0;

  for (i = 0; i < n; ++i)
    {
      pending = (pending << 9) | v[i];
      nbits += 9U;

      while (nbits >= 8U)
        {
          nbits -= 8U;
          p[used++] = (unsigned char)((pending >> nbits) & 255U);
          pending &= (1U << nbits) - 1U;
        }
    }

  if (nbits)
    {
      p[used++] = (unsigned char)(pending << (8U - nbits));
    }

  return used;
}

/*****************************************************************************/

static unsigned long
input_crc (const unsigned short *v, unsigned int n, unsigned char *packed)
{
  unsigned int i = 0, used = 0;
  unsigned long crc = 0UL;

  while (n - i >= 8U)
    {
      if (used > CRC_BUFFER_N - 9U)
        {
          crc = crc_update (crc, packed, used);
          used = 0;
        }

      pack8_symbols (v + i, packed + used);
      used += 9U;
      i += 8U;
    }

  if (i < n)
    {
      if (used > CRC_BUFFER_N - 8U)
        {
          crc = crc_update (crc, packed, used);
          used = 0;
        }

      used += pack9_tail (v + i, n - i, packed + used);
    }

  if (used)
    {
      crc = crc_update (crc, packed, used);
    }

  return crc;
}

/*****************************************************************************/

static void
store16 (unsigned char *p, unsigned int v)
{
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
}

/*****************************************************************************/

static void
store32 (unsigned char *p, unsigned long v)
{
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
}

/*****************************************************************************/

static unsigned int
load16 (const unsigned char *p)
{
  return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

/*****************************************************************************/

static unsigned long
load32 (const unsigned char *p)
{
  return (unsigned long)p[0] | ((unsigned long)p[1] << 8)
         | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
load32_be (const unsigned char *p)
{
  return ((unsigned int)p[0] << 24)
         | ((unsigned int)p[1] << 16)
         | ((unsigned int)p[2] << 8)
         | (unsigned int)p[3];
}

/*****************************************************************************/

static int
write_exact (FILE *f, const void *p, size_t n)
{
  /*LINTED E_RET_INT_IMPLICITLY*/
  return fwrite (p, 1, n, f) == n;
}

/*****************************************************************************/

static int
read_exact (FILE *f, void *p, size_t n)
{
  /*LINTED E_RET_INT_IMPLICITLY*/
  return fread (p, 1, n, f) == n;
}

/*****************************************************************************/

static void
bw_init (BitWriter *b, unsigned char *p, unsigned long bytes)
{
  b->p = p;
  b->capacity_bits = bytes * 8UL;
  b->bits = 0;
  b->failed = 0;
}

/*****************************************************************************/

static HOT LZ9_INLINE void
bw_add (BitWriter *b, unsigned int value, unsigned int bits)
{
  unsigned long byte;
  unsigned int occupied, space;

  if (!bits)
    {
      return;
    }

  if (b->bits > b->capacity_bits
      || (unsigned long)bits > b->capacity_bits - b->bits)
    {
      b->failed = 1;

      return;
    }

  byte = b->bits >> 3;
  occupied = (unsigned int)(b->bits & 7UL);
  space = 8U - occupied;

  if (!occupied)
    {
      b->p[byte] = 0;
    }

  if (bits <= space)
    {
      b->p[byte] |= (unsigned char)((value & ((1U << bits) - 1U))
                                    << (space - bits));
      b->bits += bits;

      return;
    }

  {
    unsigned int remaining = bits;

    remaining -= space;
    b->p[byte] |= (unsigned char)((value >> remaining) & ((1U << space) - 1U));
    b->bits += space;

    if (remaining >= 8U)
      {
        remaining -= 8U;
        ++byte;
        b->p[byte] = (unsigned char)((value >> remaining) & 255U);
        b->bits += 8U;
      }

    if (remaining)
      {
        ++byte;
        b->p[byte] = (unsigned char)(value << (8U - remaining));
        b->bits += remaining;
      }
  }
}

/*****************************************************************************/

static void
br_init (BitReader *b, const unsigned char *p, unsigned long bits,
         int backwards)
{
  b->p = p;
  b->bits = bits;
  b->size = (bits + 7UL) >> 3;
  b->position = 0;
  b->backwards = backwards;
  b->failed = 0;
}

/*****************************************************************************/

static HOT LZ9_INLINE unsigned int
br_get (BitReader *b, unsigned int n)
{
  unsigned int v, window, shift;
  unsigned long start, by;

  if (!n)
    {
      return 0;
    }

  if (n > 16 || b->position > b->bits
      || (unsigned long)n > b->bits - b->position)
    {
      b->failed = 1;

      return 0;
    }

  start = b->backwards ? (b->bits - b->position - n) : b->position;
  b->position += n;

  if (b->size >= 4UL)
    {
      by = start >> 3;

      if (by + 4UL > b->size)
        {
          by = b->size - 4UL;
        }

      window = load32_be (b->p + by);
      shift = 32U - (unsigned int)(start - (by << 3)) - n;
    }
  else
    {
      unsigned long w;

      by = start >> 3;
      w = (unsigned long)b->p[by] << 16;

      if (by + 1UL < b->size)
        {
          w |= (unsigned long)b->p[by + 1UL] << 8;
        }

      if (by + 2UL < b->size)
        {
          w |= (unsigned long)b->p[by + 2UL];
        }

      window = (unsigned int)w;
      shift = 24U - (unsigned int)(start & 7UL) - n;
    }

  v = (window >> shift) & ((1U << n) - 1U);

  return v;
}

/*****************************************************************************/

static unsigned int
floor_log2 (unsigned int n)
{
#if defined(__GNUC__) && !defined(__CPPCHECK__) && !defined(__LINT__)
  return n > 1U ? (unsigned int)(31 - (__builtin_clz (n))) : 0U;
#else
  unsigned int v = 0;

  while (n >>= 1)
    {
      ++v;
    }

  return v;
#endif /* if defined(__GNUC__) && !defined(__CPPCHECK__) \
          && !defined(__LINT__) */
}

/*****************************************************************************/

/*
 * Deterministic tANS.
 * Encoder inverse of the standard FSE-style decoder:
 * next_count = freq[s] ... 2*freq[s]-1;
 * nb = TABLE_LOG - floor_log2(next_count);
 * next_state = (next_count << nb)-TABLE_N + read_bits(nb).
 */

static int
ans_build (AnsTable *t, const unsigned short *freq, unsigned int alphabet)
{
  unsigned int s, x, pos = 0, cumulative = 0, step = 643U;
  unsigned short seen[512];
  unsigned short spread[TABLE_N];

  if (alphabet > 512)
    {
      return 0;
    }

  t->alphabet = (unsigned short)alphabet;

  for (s = 0; s < alphabet; ++s)
    {
      t->freq[s] = freq[s];
      t->cum[s] = (unsigned short)cumulative;
      cumulative += freq[s];
      seen[s] = 0;

      if (freq[s])
        {
          unsigned int logf = floor_log2 (freq[s]);
          unsigned int hi;

          if (logf > TABLE_LOG)
            {
              return 0;
            }

          hi = TABLE_LOG - logf;
          t->highbits[s] = (unsigned char)hi;
          t->threshold[s] = (unsigned short)(freq[s] << hi);
        }
    }

  if (cumulative != TABLE_N)
    {
      return 0;
    }

  for (s = 0; s < alphabet; ++s)
    {
      for (x = 0; x < freq[s]; ++x)
        {
          spread[pos] = (unsigned short)s;
          pos = (pos + step) & (TABLE_N - 1U);
        }
    }

  if (pos != 0)
    {
      return 0;
    }

  for (x = 0; x < TABLE_N; ++x)
    {
      unsigned int f, count, bits, base, logc;

      s = spread[x];
      f = t->freq[s];
      count = f + seen[s]++;
      logc = floor_log2 (count);

      if (logc > TABLE_LOG)
        {
          return 0;
        }

      bits = TABLE_LOG - logc;
      base = (count << bits) - TABLE_N;

      if (base >= TABLE_N || base + (1U << bits) > TABLE_N)
        {
          return 0;
        }

      t->estate[t->cum[s] + count - f] = (unsigned short)x;
    }

  return 1;
}

/*****************************************************************************/

static int
dec_build (DecTable *t, const unsigned short *freq, unsigned int alphabet)
{
  unsigned int s, x, pos = 0, cumulative = 0, step = 643U;
  unsigned short seen[512];
  unsigned short spread[TABLE_N];

  if (alphabet > 512)
    {
      return 0;
    }

  t->alphabet = (unsigned short)alphabet;

  for (s = 0; s < alphabet; ++s)
    {
      cumulative += freq[s];
      seen[s] = 0;

      if (freq[s] && floor_log2 (freq[s]) > TABLE_LOG)
        {
          return 0;
        }
    }

  if (cumulative != TABLE_N)
    {
      return 0;
    }

  for (s = 0; s < alphabet; ++s)
    {
      for (x = 0; x < freq[s]; ++x)
        {
          spread[pos] = (unsigned short)s;
          pos = (pos + step) & (TABLE_N - 1U);
        }
    }

  if (pos != 0)
    {
      return 0;
    }

  for (x = 0; x < TABLE_N; ++x)
    {
      unsigned int count, bits, base, logc;

      s = spread[x];
      count = freq[s] + seen[s]++;
      logc = floor_log2 (count);

      if (logc > TABLE_LOG)
        {
          return 0;
        }

      bits = TABLE_LOG - logc;
      base = (count << bits) - TABLE_N;

      if (base >= TABLE_N || base + (1U << bits) > TABLE_N)
        {
          return 0;
        }

      t->e[x] = base | (bits << DEC_NB_SHIFT) | (s << DEC_SYM_SHIFT);
    }

  return 1;
}

/*****************************************************************************/

static LZ9_INLINE void
ans_put (AnsTable *t, BitWriter *b, unsigned int sym, unsigned short *state)
{
  unsigned int f = t->freq[sym], y = TABLE_N + *state;
  unsigned int nb = t->highbits[sym], count;

  if (y < t->threshold[sym])
    {
      if (!nb)
        {
          b->failed = 1;

          return;
        }

      --nb;
    }

  count = y >> nb;
  bw_add (b, y & ((1U << nb) - 1U), nb);
  *state = t->estate[t->cum[sym] + count - f];
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
ans_get_with (Stream *s, const DecTable *t)
{
  unsigned int old = s->state, ent, nb;

  if (old >= TABLE_N)
    {
      s->br.failed = 1;

      return 0;
    }

  ent = t->e[old];
  nb = (ent >> DEC_NB_SHIFT) & DEC_NB_MASK;
  s->state = (unsigned short)((ent & DEC_BASE_MASK)
                              + (nb ? br_get (&s->br, nb) : 0U));

  return ent >> DEC_SYM_SHIFT;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
ans_get (Stream *s)
{
  return ans_get_with (s, s->table);
}

/*****************************************************************************/

static int
norm_active (const unsigned int *counts, const unsigned int *idx,
             unsigned int active, unsigned int total, unsigned short *freq)
{
  unsigned int k, remaining, used = 0;
  unsigned int key[512];

  if (active < 2U || active > 512U || !total || total > 0x3fffffU)
    {
      return 0;
    }

  remaining = TABLE_N - active;

  for (k = 0; k < active; ++k)
    {
      unsigned int i = idx[k];
      unsigned int q = counts[i] * remaining;
      freq[i] = (unsigned short)(1U + q / total);
      key[k] = ((q % total) << 9) | (511U - i);
      used += freq[i];
    }

  if (used < TABLE_N)
    {
      unsigned int lo = 0U, hi = total << 9;
      unsigned int deficit = TABLE_N - used;

      while (lo < hi)
        {
          unsigned int mid = lo + ((hi - lo + 1U) >> 1);
          unsigned int n = 0;

          for (k = 0; k < active; ++k)
            {
              n += (unsigned int)(key[k] >= mid);
            }

          if (n >= deficit)
            {
              lo = mid;
            }
          else
            {
              hi = mid - 1U;
            }
        }

      if (lo)
        {
          for (k = 0; k < active; ++k)
            {
              if (key[k] >= lo)
                {
                  ++freq[idx[k]];
                  ++used;
                }
            }
        }
    }

  return used == TABLE_N;
}

/*****************************************************************************/

static int
norm_counts (const unsigned int *counts, unsigned int alphabet,
             unsigned int total, unsigned short *freq)
{
  unsigned int i, active = 0;
  unsigned int idx[512];

  for (i = 0; i < alphabet; ++i)
    {
      freq[i] = 0;

      if (counts[i])
        {
          idx[active++] = i;
        }
    }

  if (active < 2U)
    {
      return 0;
    }

  return norm_active (counts, idx, active, total, freq);
}

/*****************************************************************************/

static int
put_var (unsigned char *p, unsigned long *off, unsigned long cap,
         unsigned int value)
{
  do
    {
      unsigned char b = (unsigned char)(value & 127U);

      value >>= 7;

      if (value)
        {
          b |= 128U;
        }

      if (*off >= cap)
        {
          return 0;
        }

      p[(*off)++] = b;
    }
  while (value);

  return 1;
}

/*****************************************************************************/

static int
table_cache_matches (const Encoder *e, unsigned int kind,
                     const unsigned short *freq, unsigned int alphabet)
{
  const unsigned short *cache
      = (const unsigned short *)(const void *)(e->stage + STAGE_DATA_MAX
                                                + kind * 512U
                                                  * sizeof (unsigned short));
  unsigned int i, sum = 0;

  for (i = 0; i < alphabet; ++i)
    {
      sum += cache[i];
    }

  return sum == TABLE_N
         && memcmp (cache, freq, alphabet * sizeof (unsigned short)) == 0;
}

/*****************************************************************************/

static unsigned int cost_q12[TABLE_N + 1U];

/*****************************************************************************/

static void
init_costs (void)
{
  unsigned int f, i;

  cost_q12[0] = 0;

  for (f = 1; f <= TABLE_N; ++f)
    {
      unsigned int k = floor_log2 (f), low = 1U << k;
      unsigned long z, z2, term, sum, log2m, c;

      if (f == low)
        {
          cost_q12[f] = (TABLE_LOG - k) << 12;

          continue;
        }

      z = (((unsigned long)(f - low)) << 16)
          / ((unsigned long)f + (unsigned long)low);
      z2 = (z * z) >> 16;
      term = z;
      sum = 0;

      for (i = 0; i < 8U; ++i)
        {
          sum += term / (2UL * i + 1UL);
          term = (term * z2) >> 16;
        }

      log2m = ((sum * 2UL) * 47274UL) >> 15;
      c = ((unsigned long)(TABLE_LOG - k) << 16) - log2m;
      cost_q12[f] = (unsigned int)((c + 8UL) >> 4);
    }
}

/*****************************************************************************/

static unsigned long
tans_freq_bits_list (unsigned int kind, const unsigned short *freq,
                     const unsigned int *idx, unsigned int used)
{
  unsigned int k;
  unsigned long bits = 0;

  if (kind == STREAM_LIT)
    {
      for (k = 0; k < used; ++k)
        {
          bits += 2UL * floor_log2 (freq[idx[k]]) + 1UL;
        }
    }
  else
    {
      unsigned int remaining = used;
      unsigned int assigned = 0;

      for (k = 0; k < used; ++k)
        {
          unsigned int f = freq[idx[k]];
          unsigned int range, value, log_range, cutoff;

          --remaining;
          range = TABLE_N - assigned - remaining;
          value = f - 1U;
          log_range = floor_log2 (range);
          cutoff = (1U << (log_range + 1U)) - range;
          bits += log_range + (value >= cutoff ? 1UL : 0UL);
          assigned += f;
        }
    }

  return bits;
}

/*****************************************************************************/

static unsigned long
tans_freq_bits (unsigned int kind, unsigned int alphabet,
                const unsigned short *freq)
{
  unsigned int i, used = 0;
  unsigned int idx[512];

  for (i = 0; i < alphabet; ++i)
    {
      if (freq[i])
        {
          idx[used++] = i;
        }
    }

  if (!used)
    {
      return 0UL;
    }

  return tans_freq_bits_list (kind, freq, idx, used);
}

/*****************************************************************************/

static unsigned long
compact_tans_bits (const Encoder *e, unsigned int kind,
                   unsigned int alphabet, const unsigned short *freq)
{
  unsigned int i, used = 0;

  if (table_cache_matches (e, kind, freq, alphabet))
    {
      return 0;
    }

  for (i = 0; i < alphabet; ++i)
    {
      if (freq[i])
        {
          ++used;
        }
    }

  if (used < 2U)
    {
      return 0;
    }

  return tans_freq_bits (kind, alphabet, freq);
}

/*****************************************************************************/

static unsigned long
compact_tans_desc_size (const Encoder *e, unsigned int kind,
                        unsigned int alphabet, const unsigned short *freq)
{
  unsigned long bits = compact_tans_bits (e, kind, alphabet, freq);

  if (!bits && table_cache_matches (e, kind, freq, alphabet))
    {
      return 1UL;
    }

  return 1UL + (alphabet + 7U) / 8U + (bits + 7UL) / 8UL;
}

/*****************************************************************************/

static void
put_gamma (BitWriter *w, unsigned int value)
{
  unsigned int bits = floor_log2 (value);

  bw_add (w, 0, bits);
  bw_add (w, value, bits + 1U);
}

/*****************************************************************************/

static void
put_truncated (BitWriter *w, unsigned int value, unsigned int range)
{
  unsigned int bits = floor_log2 (range);
  unsigned int cutoff = (1U << (bits + 1U)) - range;

  if (value < cutoff)
    {
      bw_add (w, value, bits);
    }
  else
    {
      bw_add (w, value + cutoff, bits + 1U);
    }
}

/*****************************************************************************/

static int
put_tans_model (Encoder *e, unsigned int kind, unsigned int alphabet,
                const unsigned short *freq)
{
  unsigned int i, used = 0;
  unsigned int bitmap_bytes = (alphabet + 7U) / 8U;
  unsigned long bitmap_start;
  BitWriter w;

  if (e->desc_len + 1UL + bitmap_bytes > DESC_MAX)
    {
      return 0;
    }

  e->desc[e->desc_len++] = MODE_TANS;
  bitmap_start = e->desc_len;
  (void)memset (e->desc + bitmap_start, 0, bitmap_bytes);
  e->desc_len += bitmap_bytes;

  for (i = 0; i < alphabet; ++i)
    {
      if (freq[i])
        {
          e->desc[bitmap_start + (i >> 3)]
              |= (unsigned char)(1U << (7U - (i & 7U)));
          ++used;
        }
    }

  bw_init (&w, e->desc + e->desc_len, DESC_MAX - e->desc_len);

  if (kind == STREAM_LIT)
    {
      for (i = 0; i < alphabet; ++i)
        {
          if (freq[i])
            {
              put_gamma (&w, freq[i]);
            }
        }
    }
  else
    {
      unsigned int assigned = 0, remaining = used;

      for (i = 0; i < alphabet; ++i)
        {
          if (freq[i])
            {
              unsigned int range;

              --remaining;
              range = TABLE_N - assigned - remaining;
              put_truncated (&w, (unsigned int)freq[i] - 1U, range);
              assigned += freq[i];
            }
        }
    }

  if (w.failed || used < 2U)
    {
      return 0;
    }

  e->desc_len += (w.bits + 7UL) / 8UL;

  return 1;
}

/*****************************************************************************/

static int
put_compact_tans_descriptor (Encoder *e, unsigned int kind,
                             unsigned int alphabet,
                             const unsigned short *freq)
{
  unsigned short *cache = table_cache (e, kind);

  if (table_cache_matches (e, kind, freq, alphabet))
    {
      if (e->desc_len >= DESC_MAX)
        {
          return 0;
        }

      e->desc[e->desc_len++] = MODE_REUSE;

      return 1;
    }

  if (!put_tans_model (e, kind, alphabet, freq))
    {
      return 0;
    }

  (void)memcpy (cache, freq, alphabet * sizeof (unsigned short));

  if (alphabet < 512U)
    {
      (void)memset (cache + alphabet, 0,
                    (512U - alphabet) * sizeof (unsigned short));
    }

  return 1;
}

/*****************************************************************************/

static int
get_var (const unsigned char *p, unsigned long *off, unsigned long len,
         unsigned int *value)
{
  unsigned int shift = 0, v = 0, i;

  for (i = 0; i < 3; ++i)
    {
      unsigned int b;

      if (*off >= len)
        {
          return 0;
        }

      b = p[(*off)++];
      v |= (b & 127U) << shift;

      if (!(b & 128U))
        {
          *value = v;

          return 1;
        }

      shift += 7;
    }

  return 0;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
len_code (unsigned int len, unsigned int bias, unsigned int *extra,
          unsigned int *bits)
{
  unsigned int t = len - bias, k;

  if (t < 16)
    {
      *extra = 0;
      *bits = 0;

      return t;
    }

  k = floor_log2 (t - 15);
  *bits = k;
  *extra = t - (15U + (1U << k));

  return 16U + k;
}

/*****************************************************************************/

static void
promote_rep (unsigned int *rep, unsigned int d)
{
  unsigned int i, j;

  for (i = 0; i < 3; ++i)
    {
      if (rep[i] == d)
        {
          break;
        }
    }

  if (i >= 3)
    {
      i = 2;
    }

  for (j = i; j > 0; --j)
    {
      rep[j] = rep[j - 1];
    }

  rep[0] = d;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
classify_offset (const Encoder *e, unsigned int d)
{
  unsigned int i;

#if !defined(LZ9ANS_DISABLE_RUN_CODE)
  if (d == 1)
    {
      for (i = 0; i < 3; ++i)
        {
          if (e->reps[i] == 1)
            {
              break;
            }
        }

      if (i == 3 || !e->use_cost
          || e->costs[STREAM_OF][3] <= e->costs[STREAM_OF][i])
        {
          return 3;
        }

      return i;
    }
#endif /* if !defined(LZ9ANS_DISABLE_RUN_CODE) */

  for (i = 0; i < 3; ++i)
    {
      if (e->reps[i] == d)
        {
          return i;
        }
    }

  if (d == 32768U)
    {
      return 19U;
    }

  if (d > 32768U)
    {
      return 20U;
    }

  return 4U + floor_log2 (d);
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
distance_code (unsigned int d, unsigned int tag, unsigned int window_n,
               unsigned int *extra, unsigned int *bits)
{
  unsigned int k;

#if !defined(LZ9ANS_DISABLE_RUN_CODE)
  if (d == 1 && !tag)
    {
      *extra = 0;
      *bits = 0;

      return 3;
    }
#endif /* if !defined(LZ9ANS_DISABLE_RUN_CODE) */

  if (tag)
    {
      *extra = 0;
      *bits = 0;

      return tag - 1U;
    }

  if (window_n == WINDOW_DEFAULT && d == WINDOW_DEFAULT)
    {
      *extra = 0;
      *bits = 0;

      return 19U;
    }

  if (window_n == WINDOW_64K && d == 32768U)
    {
      *extra = 0;
      *bits = 0;

      return 19U;
    }

  if (window_n == WINDOW_64K && d > 32768U)
    {
      *extra = d - 32769U;
      *bits = 15U;

      return 20U;
    }

  k = floor_log2 (d);
  *bits = k;
  *extra = d - (1U << k);

  return 4U + k;
}

/*****************************************************************************/

static int
aligned_distance_cost_wins (const Encoder *e)
{
  unsigned int i, eligible = 0, aligned = 0;

  for (i = 0; i < e->sn; ++i)
    {
      unsigned int d = sequence_distance (&e->seq[i]);

      if (!sequence_tag (&e->seq[i])
          && ((d >= 4U && d < WINDOW_DEFAULT)
              || (e->window_n == WINDOW_64K && d > WINDOW_DEFAULT)))
        {
          ++eligible;

          if ((d & 3U) == 0U)
            {
              ++aligned;
            }
        }
    }

  return aligned > eligible - aligned;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
extra_get (ExtraReader *x, unsigned int n)
{
  unsigned int value;

  if (!n)
    {
      return 0U;
    }

  value = br_get (x->raw, n);

  if (x->raw->failed)
    {
      x->failed = 1;
    }

  return value;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
decode_len (unsigned int code, unsigned int bias, ExtraReader *extra)
{
  if (code < 16)
    {
      return code + bias;
    }

  if (code > 31)
    {
      extra->failed = 1;

      return 0;
    }

  return bias + 15U + (1U << (code - 16)) + extra_get (extra, code - 16);
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
decode_dist (unsigned int code, ExtraReader *extra, unsigned int *rep,
             unsigned int window_n, unsigned int of_alphabet,
             int aligned_distances)
{
  unsigned int d, bucket;

  if (code < 3)
    {
      d = rep[code];

      if (!d)
        {
          extra->failed = 1;

          return 0;
        }

      promote_rep (rep, d);

      return d;
    }

  if (code == 3)
    {
      return 1;
    }

  if (code == 19)
    {
      promote_rep (rep, 32768U);

      return 32768U;
    }

  if (code == 20 && window_n == WINDOW_64K)
    {
      if (aligned_distances && extra_get (extra, 1U))
        {
          d = 32772U + (extra_get (extra, 13U) << 2);
        }
      else
        {
          d = 32769U + extra_get (extra, 15U);

          if (aligned_distances && (d & 3U) == 0U)
            {
              extra->failed = 1;

              return 0;
            }
        }

      if (d > WINDOW_64K)
        {
          extra->failed = 1;

          return 0;
        }

      promote_rep (rep, d);

      return d;
    }

  if (code >= of_alphabet)
    {
      extra->failed = 1;

      return 0;
    }

  bucket = code - 4U;

  if (aligned_distances && bucket >= 2U && code <= 18U)
    {
      if (extra_get (extra, 1U))
        {
          d = (1U << bucket) + (extra_get (extra, bucket - 2U) << 2);
        }
      else
        {
          d = (1U << bucket) + extra_get (extra, bucket);

          if ((d & 3U) == 0U)
            {
              extra->failed = 1;

              return 0;
            }
        }
    }
  else
    {
      d = (1U << bucket) + extra_get (extra, bucket);
    }

  if (d > window_n)
    {
      extra->failed = 1;

      return 0;
    }

  promote_rep (rep, d);

  return d;
}

/*****************************************************************************/

static HOT LZ9_INLINE unsigned int
hash3 (const unsigned short *p)
{
  unsigned int x = ((unsigned int)p[0] * 2654435761U)
                   ^ ((unsigned int)p[1] * 2246822519U)
                   ^ ((unsigned int)p[2] * 3266489917U);

  x ^= x >> 16;

  return x & HASH_MASK;
}

/*****************************************************************************/

static unsigned int
estimated_code_cost (const Encoder *e, unsigned int kind, unsigned int symbol)
{
  return e->costs[kind][symbol];
}

/*****************************************************************************/

static LZ9_INLINE unsigned long
literal_price (const Encoder *e, unsigned int start, unsigned int end)
{
  return (unsigned long)(e->prefix[end] - e->prefix[start]);
}

/*****************************************************************************/

static void
build_cost_prefix (Encoder *e)
{
  unsigned int i, acc = 0;
  const unsigned short *lit_costs = e->costs[STREAM_LIT];
  const unsigned short *src = e->buffer + e->window_n;

  e->prefix[0] = 0;

  for (i = 0; i < e->n; ++i)
    {
      acc += lit_costs[src[i]];
      e->prefix[i + 1U] = acc;
    }
}

/*****************************************************************************/

static unsigned long
ll_price (const Encoder *e, unsigned int ll)
{
  unsigned int ex, nb, lc;

  lc = len_code (ll, 0, &ex, &nb);

  return estimated_code_cost (e, STREAM_LL, lc)
         + (unsigned long)nb * COST_SCALE + COST_SCALE;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
offset_price (const Encoder *e, unsigned int distance)
{
  unsigned int oc = classify_offset (e, distance);
  unsigned long price = estimated_code_cost (e, STREAM_OF, oc);

  if (oc == 20U)
    {
      price += 15UL * COST_SCALE;
    }
  else if (oc >= 4U && oc != 19U)
    {
      price += (unsigned long)(oc - 4U) * COST_SCALE;
    }

  return (unsigned int)price;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
ml_price (const Encoder *e, unsigned int ml)
{
  unsigned int t = ml - e->min_match;

  if (t < 16U)
    {
      return estimated_code_cost (e, STREAM_ML, t);
    }
  else
    {
      unsigned int k = floor_log2 (t - 15U);

      return (unsigned int)(estimated_code_cost (e, STREAM_ML, 16U + k)
                            + (unsigned long)k * COST_SCALE);
    }
}

/*****************************************************************************/

static void
hash_insert (Encoder *e, unsigned int pos, unsigned int end)
{
  unsigned int h;

  if (pos + 2U >= end)
    {
      return;
    }

  h = hash3 (e->buffer + pos);
  e->link[pos & (e->window_n - 1U)] = e->head[h];
  e->head[h] = (int)pos;
}

/*****************************************************************************/

static HOT LZ9_INLINE void
consider_match (const Encoder *e, unsigned int pos, unsigned int max,
                unsigned long base_ll_price,
                unsigned int cand, unsigned int p0, unsigned int p1,
                unsigned int p2, unsigned int *best, unsigned int *distance,
                long *best_gain)
{
  const unsigned short *buf = e->buffer;
  unsigned int k;

  if (buf[cand] != p0 || buf[cand + 1U] != p1 || buf[cand + 2U] != p2)
    {
      return;
    }

  k = 3;

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__amd64__))
  while (k + 4U <= max)
    {
      lz9_u64 a, b, diff;

      (void)memcpy (&a, buf + cand + k, sizeof (a));
      (void)memcpy (&b, buf + pos + k, sizeof (b));
      diff = a ^ b;

      if (diff)
        {
          k += (unsigned int)(__builtin_ctzll (diff) >> 4);

          goto match_done;
        }

      k += 4U;
    }
#endif /* if defined(__GNUC__) && (defined(__x86_64__) \
          || defined(__amd64__)) */

  while (k < max && buf[cand + k] == buf[pos + k])
    {
      ++k;
    }

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__amd64__))
match_done:
#endif /* if defined(__GNUC__) && (defined(__x86_64__) \
          || defined(__amd64__)) */

  if (e->use_cost)
    {
      unsigned int trial[3], t, trial_count = e->level >= 8 ? 3U : 1U;
      unsigned int dist = pos - cand;
      unsigned int pos_rel = pos - e->window_n;
      unsigned long base_cost = base_ll_price + offset_price (e, dist);

      trial[0] = k;
      trial[1] = k > 3U ? k - 1U : k;
      trial[2] = k > e->min_match + 15U ? e->min_match + 15U : k;

      for (t = 0; t < trial_count; ++t)
        {
          unsigned int length_here = trial[t];
          long saving;

          if (t && length_here == trial[t - 1U])
            {
              continue;
            }

          saving
              = (long)literal_price (e, pos_rel, pos_rel + length_here)
                - (long)(base_cost + ml_price (e, length_here));

          if (saving > *best_gain
              || (saving == *best_gain && saving > 0
                  && length_here > *best))
            {
              *best_gain = saving;
              *best = length_here;
              *distance = dist;
            }
        }
    }
  else if (k > *best)
    {
      *best = k;
      *distance = pos - cand;
    }
}

/*****************************************************************************/

static void
consider_short (const Encoder *e, unsigned int pos, unsigned int pos_rel,
                unsigned long base_ll_price, unsigned int dist,
                unsigned int *best, unsigned int *distance, long *best_gain)
{
  const unsigned short *buf = e->buffer;
  long saving;

  if (buf[pos - dist] != buf[pos] || buf[pos - dist + 1U] != buf[pos + 1U])
    {
      return;
    }

  saving = (long)literal_price (e, pos_rel, pos_rel + 2U)
           - (long)(base_ll_price + offset_price (e, dist)
                    + ml_price (e, 2U));

  if (saving > *best_gain)
    {
      *best_gain = saving;
      *best = 2U;
      *distance = dist;
    }
}

/*****************************************************************************/

static void
find_match (Encoder *e, unsigned int pos, unsigned int end,
            unsigned int pending, unsigned int pos_hash,
            unsigned int *length, unsigned int *distance, long *gain)
{
  int cand, checks = 0;
  unsigned int best = 0, d = 0;
  long best_gain = 0;
  unsigned int max;
  unsigned long base_ll_price = 0;
  const unsigned short *buf;
  unsigned int p0, p1, p2, i;
  unsigned int history_start = e->window_n - e->hlen;

  if (pos + 2U >= end)
    {
      *length = 0;
      *distance = 0;
      *gain = 0;

      return;
    }

  max = end - pos;

  if (max > BLOCK_N)
    {
      max = BLOCK_N;
    }

  buf = e->buffer;
  p0 = buf[pos];
  p1 = buf[pos + 1U];
  p2 = buf[pos + 2U];

  if (e->use_cost)
    {
      base_ll_price = ll_price (e, pending);
    }

  for (i = 0; i < 3U; ++i)
    {
      unsigned int rep = e->reps[i];

      if (rep && rep <= pos - history_start)
        {
          consider_match (e, pos, max, base_ll_price, pos - rep,
                          p0, p1, p2, &best, &d, &best_gain);
        }
    }

  cand = e->head[pos_hash];

  while (cand >= 0 && checks < (int)e->search_depth && (unsigned int)cand < pos
         && pos <= (unsigned int)cand + e->window_n)
    {
      if (best >= 3U && best < max
          && buf[(unsigned int)cand + best] != buf[pos + best])
        {
          cand = e->link[((unsigned int)cand) & (e->window_n - 1U)];
          ++checks;

          continue;
        }

      consider_match (e, pos, max, base_ll_price, (unsigned int)cand,
                      p0, p1, p2, &best, &d, &best_gain);

      if (!e->use_cost && best == max)
        {
          break;
        }

      cand = e->link[((unsigned int)cand) & (e->window_n - 1U)];
      ++checks;
    }

  if (best < 3U && e->use_cost && e->min_match == MIN_MATCH_SHORT)
    {
      unsigned int pos_rel = pos - e->window_n;

      for (i = 0; i < 3U; ++i)
        {
          unsigned int rep = e->reps[i];

          if (rep && rep <= pos - history_start)
            {
              consider_short (e, pos, pos_rel, base_ll_price, rep,
                              &best, &d, &best_gain);
            }
        }

      if (pos - history_start >= 1U)
        {
          consider_short (e, pos, pos_rel, base_ll_price, 1U,
                          &best, &d, &best_gain);
        }
    }

  *length = best;
  *distance = d;
  *gain = best_gain;
}

/*****************************************************************************/

static int
parse_block (Encoder *e)
{
  const unsigned int max_lazy = e->level == 8 ? LZ9ANS_L8_LAZY_MAX_LENGTH
                                              : 64U;
  unsigned int pos = e->window_n, end = e->window_n + e->n;
  unsigned int litstart = pos, k, length, distance, j;
  unsigned int pos_hash, next_hash = 0, cached_hash = 0;
  int have_cached_hash = 0;
  long gain = 0;

  e->sn = 0;
  e->lit_count = 0;
  e->max_literal = 0;

  for (j = 0; j < HASH_N; ++j)
    {
      e->head[j] = -1;
    }

  for (j = e->window_n - e->hlen; j < e->window_n; ++j)
    {
      hash_insert (e, j, end);
    }

  while (pos < end)
    {
      int have_next_hash;

      if (have_cached_hash)
        {
          pos_hash = cached_hash;
          have_cached_hash = 0;
        }
      else if (pos + 2U < end)
        {
          pos_hash = hash3 (e->buffer + pos);
        }
      else
        {
          pos_hash = 0;
        }

      have_next_hash = 0;
      find_match (e, pos, end, pos - litstart, pos_hash, &length, &distance,
                  &gain);

      if (e->lazy && length >= 3 && length < max_lazy && pos + 3U < end)
        {
          unsigned int nextlen, nextdist;
          long nextgain = 0;

          next_hash = hash3 (e->buffer + pos + 1U);
          have_next_hash = 1;
          find_match (e, pos + 1U, end, pos + 1U - litstart, next_hash,
                      &nextlen, &nextdist, &nextgain);

          if ((e->use_cost && nextgain > gain)
              || (!e->use_cost
                  && (nextlen > length + 1U
                      || (nextlen == length + 1U
                          && nextdist * 2U < distance))))
            {
              if (pos + 2U < end)
                {
                  e->link[pos & (e->window_n - 1U)] = e->head[pos_hash];
                  e->head[pos_hash] = (int)pos;
                }

              ++pos;
              cached_hash = next_hash;
              have_cached_hash = 1;

              continue;
            }
        }

      if (length >= 2)
        {
          if (e->sn >= SEQ_MAX)
            {
              return 0;
            }

          e->seq[e->sn].ll = (unsigned short)(pos - litstart);
          e->seq[e->sn].ml = (unsigned short)length;

          {
            unsigned int of = classify_offset (e, distance);
            unsigned int tag = of < 3 ? of + 1U : 0;

            e->seq[e->sn].dist = distance | (tag << SEQ_TAG_SHIFT);

            if (of != 3)
              {
                promote_rep (e->reps, distance);
              }
          }

          ++e->sn;
          e->lit_count += pos - litstart;

          if (pos + 2U < end)
            {
              unsigned int h = pos_hash;

              e->link[pos & (e->window_n - 1U)] = e->head[h];
              e->head[h] = (int)pos;
            }

          if (pos + 3U < end)
            {
              unsigned int at = pos + 1U;
              unsigned int h = have_next_hash ? next_hash
                                              : hash3 (e->buffer + at);

              e->link[at & (e->window_n - 1U)] = e->head[h];
              e->head[h] = (int)at;
            }

          for (k = 2U; k < length; ++k)
            {
              unsigned int at = pos + k;

              if (at + 2U >= end)
                {
                  break;
                }

              {
                unsigned int h = hash3 (e->buffer + at);

                e->link[at & (e->window_n - 1U)] = e->head[h];
                e->head[h] = (int)at;
              }
            }

          pos += length;
          litstart = pos;
        }
      else
        {
          if (pos + 2U < end)
            {
              e->link[pos & (e->window_n - 1U)] = e->head[pos_hash];
              e->head[pos_hash] = (int)pos;
            }

          ++pos;
        }
    }

  e->last_lit = end - litstart;
  e->lit_count += e->last_lit;

  return 1;
}

/*****************************************************************************/

static unsigned int
stream_alphabet (unsigned int kind, unsigned int of_alphabet)
{
  if (kind == STREAM_LIT)
    {
      return 512U;
    }

  return kind == STREAM_OF ? of_alphabet : 32U;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
symbol_of_seq (const Encoder *e, unsigned int kind, const Sequence *q)
{
  if (kind == STREAM_LL)
    {
      unsigned int t = q->ll;

      return t < 16U ? t : 16U + floor_log2 (t - 15U);
    }

  if (kind == STREAM_ML)
    {
      unsigned int t = q->ml - e->min_match;

      return t < 16U ? t : 16U + floor_log2 (t - 15U);
    }

  {
    unsigned int tag = sequence_tag (q);
    unsigned int d;

    if (tag)
      {
        return tag - 1U;
      }

    d = sequence_distance (q);

#if !defined(LZ9ANS_DISABLE_RUN_CODE)
    if (d == 1U)
      {
        return 3U;
      }
#endif /* if !defined(LZ9ANS_DISABLE_RUN_CODE) */

    if (d == 32768U)
      {
        return 19U;
      }

    if (e->window_n == WINDOW_64K && d > 32768U)
      {
        return 20U;
      }

    return 4U + floor_log2 (d);
  }
}

/*****************************************************************************/

static unsigned int
literal_scan (Encoder *e, int count_only, BitWriter *w, AnsTable *t,
              unsigned short *state)
{
  unsigned int i, j, pos, observed = 0;

  if (count_only)
    {
      unsigned int v;
      pos = e->window_n;

      for (i = 0; i < e->sn; ++i)
        {
          for (j = 0; j < e->seq[i].ll; ++j)
            {
              v = e->buffer[pos + j];
              ++e->counts[v];

              if (v > e->max_literal)
                {
                  e->max_literal = v;
                }

              ++observed;
            }

          pos += e->seq[i].ll + e->seq[i].ml;
        }

      while (pos < e->window_n + e->n)
        {
          v = e->buffer[pos++];
          ++e->counts[v];

          if (v > e->max_literal)
            {
              e->max_literal = v;
            }

          ++observed;
        }
    }
  else if (t && state && w)
    {
      pos = e->window_n + e->n;

      for (j = 0; j < e->last_lit; ++j)
        {
          ans_put (t, w, e->buffer[--pos], state);
        }

      for (i = e->sn; i; --i)
        {
          pos -= e->seq[i - 1].ml;

          for (j = 0; j < e->seq[i - 1].ll; ++j)
            {
              ans_put (t, w, e->buffer[--pos], state);
            }
        }
    }
  else if (w)
    {
      pos = e->window_n;

      for (i = 0; i < e->sn; ++i)
        {
          for (j = 0; j < e->seq[i].ll; ++j)
            {
              bw_add (w, e->buffer[pos + j], e->current_kind);
            }

          pos += e->seq[i].ll + e->seq[i].ml;
        }

      while (pos < e->window_n + e->n)
        {
          bw_add (w, e->buffer[pos], e->current_kind);
          ++pos;
        }
    }

  return observed;
}

/*****************************************************************************/

static unsigned int
count_stream (Encoder *e, unsigned int kind)
{
  unsigned int i, total = 0;

  (void)memset (e->counts, 0, sizeof (e->counts));

  if (kind == STREAM_LIT)
    {
      return literal_scan (e, 1, (BitWriter *)0, (AnsTable *)0,
                           (unsigned short *)0);
    }

  for (i = 0; i < e->sn; ++i)
    {
      ++e->counts[symbol_of_seq (e, kind, &e->seq[i])];
      ++total;
    }

  return total;
}

/*****************************************************************************/

static void
encode_values (Encoder *e, unsigned int kind, BitWriter *w, AnsTable *t,
               unsigned int width)
{
  unsigned int i;
  unsigned short state = 0;

  if (kind == STREAM_LIT)
    {
      e->current_kind = width;
      (void)literal_scan (e, 0, w, t, &state);
    }
  else if (t)
    {
      for (i = e->sn; i; --i)
        {
          ans_put (t, w, symbol_of_seq (e, kind, &e->seq[i - 1]), &state);
        }
    }
  else
    {
      for (i = 0; i < e->sn; ++i)
        {
          bw_add (w, symbol_of_seq (e, kind, &e->seq[i]), width);
        }
    }

  if (t)
    {
      bw_add (w, state, TABLE_LOG);
    }
}

/*****************************************************************************/

static unsigned int
raw_width (unsigned int kind, unsigned int maxlit)
{
  if (kind == STREAM_LIT)
    {
      return maxlit < 128 ? 7U : (maxlit < 256 ? 8U : 9U);
    }

  return 5U;
}

/*****************************************************************************/

/*
 * Integer-approximate -log2(frequency/1024) in Q8 fixed point.
 * Uses floor_log2 and a single-step linear interpolation between
 * powers of two, avoiding all floating-point math on the hot path.
 *
 * For frequency f (1..1024):
 *   log2(1024/f) = 10 - log2(f)
 *
 * We decompose log2(f) = k + frac where k = floor_log2(f) and
 *   frac ~= (f - 2^k) / 2^k  (linear approx in [2^k, 2^(k+1)))
 *
 * Result in Q8: (10 - k) * 256 - frac * 256
 *            = (10 - k) * 256 - ((f - 2^k) * 256) / 2^k
 */

static unsigned int
entropy_cost (unsigned int frequency)
{
  unsigned int k, low, result;

  if (!frequency)
    {
      return 16U * COST_SCALE;
    }

  if (frequency >= TABLE_N)
    {
      return 0;
    }

  k = floor_log2 (frequency);
  low = 1U << k;

  /* (10 - k) * 256 - ((frequency - low) * 256) / low */
  result = ((TABLE_LOG - k) * COST_SCALE)
           - ((frequency - low) * COST_SCALE) / low;

  return result > 65535U ? 65535U : result;
}

/*****************************************************************************/

static int
early_stored_block (Encoder *e, unsigned int stored_width)
{
#if LZ9ANS_EARLY_STORED_LITERAL_PERCENT == 0
  (void)e;
  (void)stored_width;

  return 0;
#else
  unsigned int i, total, used = 0;
  unsigned int width;
  unsigned short *freq = e->costs[STREAM_LIT];
  unsigned long raw, best;

  total = count_stream (e, STREAM_LIT);

  if (!total
      || (unsigned long)total * 100UL
             < (unsigned long)e->n
                   * LZ9ANS_EARLY_STORED_LITERAL_PERCENT)
    {
      return 0;
    }

  width = raw_width (STREAM_LIT, e->max_literal);
  raw = (unsigned long)total * width * COST_SCALE;
  best = raw;

  for (i = 0; i < 512U; ++i)
    {
      if (e->counts[i])
        {
          ++used;
        }
    }

  if (used == 1U)
    {
      best = 24UL * COST_SCALE;
    }
  else if (used >= 2U && norm_counts (e->counts, 512U, total, freq))
    {
      unsigned int prev = 0;
      unsigned long predicted = 0, header = 2;

      for (i = 0; i < 512U; ++i)
        {
          if (freq[i])
            {
              unsigned int gap = i - prev;

              header += (gap >= 128U ? 2UL : 1UL)
                        + (freq[i] >= 128U ? 2UL : 1UL);
              prev = i + 1U;
              predicted
                  += (unsigned long)e->counts[i] * entropy_cost (freq[i]);
            }
        }

      predicted += (header * 8UL + TABLE_LOG) * COST_SCALE;

      if (predicted < best)
        {
          best = predicted;
        }
    }

  return best >= (unsigned long)e->n * stored_width * COST_SCALE;
#endif /* if LZ9ANS_EARLY_STORED_LITERAL_PERCENT == 0 */
}

/*****************************************************************************/

static void
estimate_costs (Encoder *e)
{
  unsigned int kind, i;
  unsigned short freq[512];

  for (kind = 0; kind < 4; ++kind)
    {
      unsigned int alphabet = stream_alphabet (kind, e->of_alphabet), lone = 0;
      unsigned int used, width, total;
      unsigned long raw, header = 0;
      int use_tans = 0;

      total = count_stream (e, kind);
      used = 0;
      width = raw_width (kind, e->max_literal);

      for (i = 0; i < alphabet; ++i)
        {
          if (e->counts[i])
            {
              ++used;
              lone = i;
            }
        }

      raw = (unsigned long)total * width * COST_SCALE;

      if (used >= 2 && norm_counts (e->counts, alphabet, total, freq))
        {
          unsigned int prev = 0;
          unsigned long predicted = 0;
          header = 2;

          for (i = 0; i < alphabet; ++i)
            {
              if (freq[i])
                {
                  unsigned int gap = i - prev;

                  header += (gap >= 128 ? 2UL : 1UL)
                            + (freq[i] >= 128 ? 2UL : 1UL);
                  prev = i + 1U;
                  predicted
                      += (unsigned long)e->counts[i] * entropy_cost (freq[i]);
                }
            }

          predicted += (header * 8UL + TABLE_LOG) * COST_SCALE;

          if (predicted < raw)
            {
              use_tans = 1;
            }
        }

      for (i = 0; i < alphabet; ++i)
        {
          unsigned int c = width * COST_SCALE;

          if (used == 1)
            {
              c = (i == lone) ? (unsigned int)((24UL * COST_SCALE) / total)
                              : (width + 2U) * COST_SCALE;
            }
          else if (use_tans)
            {
              c = freq[i] ? entropy_cost (freq[i])
                                + (unsigned int)((header * 8UL * COST_SCALE)
                                                 / total)
                          : (width + 2U) * COST_SCALE;
            }

          if (c > 65535U)
            {
              c = 65535U;
            }

          e->costs[kind][i] = (unsigned short)c;
        }
    }

  build_cost_prefix (e);
}

/*****************************************************************************/

#define CHOICE_FRESH 0U
#define CHOICE_REUSE 1U
#define CHOICE_PHASED 2U
#define PHASE_MARGIN_BITS 16UL

/*****************************************************************************/

static unsigned int
count_stream_phased (Encoder *e, unsigned int kind)
{
  unsigned int i, v, pos, ph;
  unsigned int base = (unsigned int)(e->frame_pos & 3UL);

  if (kind == STREAM_LIT)
    {
      unsigned int total = 0;

      (void)memset (e->pcounts, 0, sizeof (e->pcounts));
      pos = e->window_n;

      for (i = 0; i < e->sn; ++i)
        {
          unsigned int j;

          ph = (base + pos) & 3U;

          for (j = 0; j < e->seq[i].ll; ++j)
            {
              ++e->pcounts[ph][e->buffer[pos + j]];
              ph = (ph + 1U) & 3U;
            }

          total += e->seq[i].ll;
          pos += e->seq[i].ll + e->seq[i].ml;
        }

      ph = (base + pos) & 3U;

      while (pos < e->window_n + e->n)
        {
          ++e->pcounts[ph][e->buffer[pos++]];
          ph = (ph + 1U) & 3U;
          ++total;
        }

      for (v = 0; v < 512U; ++v)
        {
          e->counts[v] = e->pcounts[0][v] + e->pcounts[1][v]
                         + e->pcounts[2][v] + e->pcounts[3][v];

          if (e->counts[v] && v > e->max_literal)
            {
              e->max_literal = v;
            }
        }

      return total;
    }

  for (ph = 0; ph < 4U; ++ph)
    {
      (void)memset (e->pcounts[ph], 0, sizeof (e->pcounts[ph]));
    }

  pos = base;

  for (i = 0; i < e->sn; ++i)
    {
      const Sequence *q = &e->seq[i];

      if (kind == STREAM_LL)
        {
          ++e->pcounts[pos & 3U][symbol_of_seq (e, kind, q)];
        }

      pos += q->ll;

      if (kind == STREAM_ML)
        {
          ++e->pcounts[pos & 3U][symbol_of_seq (e, kind, q)];
        }

      pos += q->ml;
    }

  for (v = 0; v < 512U; ++v)
    {
      e->counts[v] = 0;
    }

  for (v = 0; v < 32U; ++v)
    {
      e->counts[v] = e->pcounts[0][v] + e->pcounts[1][v]
                     + e->pcounts[2][v] + e->pcounts[3][v];
    }

  return e->sn;
}

/*****************************************************************************/

static unsigned long
phase_group_eval (unsigned int kind, unsigned int alphabet,
                  const unsigned int *hist, const unsigned short *slot,
                  unsigned short *freq, unsigned char *is_empty,
                  unsigned char *is_reuse, unsigned long *desc_bytes)
{
  unsigned int i, k, total = 0, used = 0, slot_sum = 0;
  unsigned int idx[512];
  unsigned long payload = 0, bytes, bits;

  for (i = 0; i < alphabet; ++i)
    {
      slot_sum += slot[i];

      if (hist[i])
        {
          idx[used++] = i;
          total += hist[i];
        }
    }

  *is_reuse = 0U;

  if (!used)
    {
      *is_empty = 1U;
      *desc_bytes = 1UL;

      return 8UL;
    }

  *is_empty = 0U;
  (void)memset (freq, 0, alphabet * sizeof (unsigned short));

  if (used == 1U)
    {
      unsigned int tmp[512];
      unsigned int only = idx[0], dummy = only == 0U ? 1U : 0U;

      (void)memcpy (tmp, hist, alphabet * sizeof (unsigned int));
      tmp[dummy] = 1U;
      idx[0] = only < dummy ? only : dummy;
      idx[1] = only < dummy ? dummy : only;

      if (!norm_active (tmp, idx, 2U, total + 1U, freq))
        {
          return ~0UL;
        }

      used = 2U;
    }
  else if (!norm_active (hist, idx, used, total, freq))
    {
      return ~0UL;
    }

  for (k = 0; k < used; ++k)
    {
      payload += (unsigned long)hist[idx[k]] * cost_q12[freq[idx[k]]];
    }

  bytes = 1UL + (alphabet + 7U) / 8U
          + (tans_freq_bits_list (kind, freq, idx, used) + 7UL) / 8UL;
  bits = bytes * 8UL + (payload >> 12);
  *desc_bytes = bytes;

  if (slot_sum == TABLE_N)
    {
      unsigned long reuse_payload = 0;

      for (k = 0; k < used; ++k)
        {
          i = idx[k];

          if (hist[i])
            {
              if (!slot[i])
                {
                  return bits;
                }

              reuse_payload += (unsigned long)hist[i] * cost_q12[slot[i]];
            }
        }

      if (8UL + (reuse_payload >> 12) < bits)
        {
          *is_reuse = 1U;
          *desc_bytes = 1UL;

          return 8UL + (reuse_payload >> 12);
        }
    }

  return bits;
}

/*****************************************************************************/

static int
choose_phased (const Encoder *e, unsigned int kind, unsigned int alphabet,
               unsigned long best_bits, unsigned int *pid_out,
               unsigned short freqs[PHASE_GROUPS_MAX][512],
               unsigned char *empty_out, unsigned char *reuse_out,
               unsigned long *desc_bytes_out)
{
  unsigned int pid, g, ph, i;
  int found = 0;
  unsigned int hist[PHASE_GROUPS_MAX][512];
  unsigned short tf[PHASE_GROUPS_MAX][512];

  for (pid = 0; pid < phase_partition_count[kind]; ++pid)
    {
      const PhasePartition *pp = &phase_partitions[kind][pid];
      unsigned long bits = TABLE_LOG + 16UL, dbytes = 2UL;
      unsigned char tmp_empty[PHASE_GROUPS_MAX];
      unsigned char tmp_reuse[PHASE_GROUPS_MAX];
      int ok = 1;

      for (g = 0; g < pp->groups; ++g)
        {
          (void)memset (hist[g], 0, alphabet * sizeof (unsigned int));
        }

      for (ph = 0; ph < 4U; ++ph)
        {
          unsigned int *h = hist[pp->gmap[ph]];

          for (i = 0; i < alphabet; ++i)
            {
              h[i] += e->pcounts[ph][i];
            }
        }

      for (g = 0; g < pp->groups; ++g)
        {
          unsigned long gb, db;

          gb = phase_group_eval (kind, alphabet, hist[g],
                                 table_cache (e, phase_table_base[kind] + g),
                                 tf[g], &tmp_empty[g], &tmp_reuse[g], &db);

          if (gb == ~0UL)
            {
              ok = 0;

              break;
            }

          bits += gb;
          dbytes += db;
        }

      if (ok && bits + PHASE_MARGIN_BITS < best_bits)
        {
          best_bits = bits;
          found = 1;
          *pid_out = pid;
          *desc_bytes_out = dbytes;
          (void)memcpy (freqs, tf, sizeof (tf));
          (void)memcpy (empty_out, tmp_empty, sizeof (tmp_empty));
          (void)memcpy (reuse_out, tmp_reuse, sizeof (tmp_reuse));
        }
    }

  return found;
}

/*****************************************************************************/

static int
encode_phased (Encoder *e, unsigned int kind, BitWriter *w,
               unsigned int alphabet, unsigned int pid,
               unsigned short freqs[PHASE_GROUPS_MAX][512],
               const unsigned char *empty, const unsigned char *reuse)
{
  const PhasePartition *pp = &phase_partitions[kind][pid];
  AnsTable *pt[4];
  unsigned int g, ph, i, pos;
  unsigned int base = (unsigned int)(e->frame_pos & 3UL);
  unsigned short state = 0;

  for (g = 0; g < pp->groups; ++g)
    {
      const unsigned short *source
          = reuse[g] ? table_cache (e, phase_table_base[kind] + g) : freqs[g];

      if (!empty[g] && !ans_build (&e->gtable[g], source, alphabet))
        {
          return 0;
        }
    }

  for (ph = 0; ph < 4U; ++ph)
    {
      pt[ph] = &e->gtable[pp->gmap[ph]];
    }

  if (kind == STREAM_LIT)
    {
      unsigned int j;

      pos = e->window_n + e->n;

      for (j = 0; j < e->last_lit; ++j)
        {
          --pos;
          ans_put (pt[(base + pos) & 3U], w, e->buffer[pos], &state);
        }

      for (i = e->sn; i; --i)
        {
          pos -= e->seq[i - 1].ml;

          for (j = 0; j < e->seq[i - 1].ll; ++j)
            {
              --pos;
              ans_put (pt[(base + pos) & 3U], w, e->buffer[pos], &state);
            }
        }
    }
  else
    {
      pos = base + e->n - e->last_lit;

      for (i = e->sn; i; --i)
        {
          const Sequence *q = &e->seq[i - 1];
          unsigned int mstart = pos - q->ml, sstart = mstart - q->ll;

          ans_put (pt[(kind == STREAM_LL ? sstart : mstart) & 3U], w,
                   symbol_of_seq (e, kind, q), &state);
          pos = sstart;
        }
    }

  bw_add (w, state, TABLE_LOG);

  return 1;
}

/*****************************************************************************/

static int
put_phased_descriptor (Encoder *e, unsigned int kind, unsigned int alphabet,
                       unsigned int pid,
                       unsigned short freqs[PHASE_GROUPS_MAX][512],
                       const unsigned char *empty, const unsigned char *reuse)
{
  const PhasePartition *pp = &phase_partitions[kind][pid];
  unsigned int g;

  if (e->desc_len + 2UL > DESC_MAX)
    {
      return 0;
    }

  e->desc[e->desc_len++] = MODE_PHASED;
  e->desc[e->desc_len++] = (unsigned char)pid;

  for (g = 0; g < pp->groups; ++g)
    {
      unsigned short *slot = table_cache (e, phase_table_base[kind] + g);

      if (empty[g] || reuse[g])
        {
          if (e->desc_len >= DESC_MAX)
            {
              return 0;
            }

          e->desc[e->desc_len++] = empty[g] ? MODE_EMPTY : MODE_REUSE;
        }
      else
        {
          if (!put_tans_model (e, kind, alphabet, freqs[g]))
            {
              return 0;
            }

          (void)memcpy (slot, freqs[g], alphabet * sizeof (unsigned short));

          if (alphabet < 512U)
            {
              (void)memset (slot + alphabet, 0,
                            (512U - alphabet) * sizeof (unsigned short));
            }
        }
    }

  return 1;
}

/*****************************************************************************/

/*
 * The cached REUSE model can code the block only when every symbol in the
 * current stream has a non-zero cached frequency.
 */

static int
table_cache_covers (const Encoder *e, unsigned int kind,
                    unsigned int alphabet)
{
  const unsigned short *cache = table_cache (e, kind);
  unsigned int i, sum = 0;

  for (i = 0; i < alphabet; ++i)
    {
      sum += cache[i];
    }

  if (sum != TABLE_N)
    {
      return 0;
    }

  for (i = 0; i < alphabet; ++i)
    {
      if (e->counts[i] && !cache[i])
        {
          return 0;
        }
    }

  return 1;
}

/*****************************************************************************/

static int
encode_one_stream (Encoder *e, unsigned int kind)
{
  unsigned int i, total, single = 0,
                         alphabet = stream_alphabet (kind, e->of_alphabet);
  unsigned int width, mode = MODE_RAW, stream_max = 0;
  unsigned int choice = CHOICE_FRESH, pid = 0;
  unsigned short freq[512];
  unsigned short pfreq[PHASE_GROUPS_MAX][512];
  unsigned char pempty[PHASE_GROUPS_MAX];
  unsigned char preuse[PHASE_GROUPS_MAX];
  unsigned long start = e->stage_len;
  unsigned long ansbits = 0;
  int phase_ok = (e->features & FEATURE_PHASED_TABLES) != 0U
                 && kind <= STREAM_ML;
  BitWriter w;

  total = phase_ok ? count_stream_phased (e, kind) : count_stream (e, kind);

  for (i = 0; i < alphabet; ++i)
    {
      if (e->counts[i] && i > stream_max)
        {
          stream_max = i;
        }
    }

  e->stream_bits[kind] = 0;
  e->stream_bytes[kind] = 0;

  if (!total)
    {
      mode = MODE_EMPTY;
    }
  else
    {
      unsigned int used = 0;

      for (i = 0; i < alphabet; ++i)
        {
          if (e->counts[i])
            {
              ++used;
              single = i;
            }
        }

      if (used == 1)
        {
          mode = MODE_RLE;
        }
    }

  width = raw_width (kind, stream_max);

  if (mode == MODE_RAW)
    {
      if (norm_counts (e->counts, alphabet, total, freq))
        {
          unsigned long fresh_cost = compact_tans_desc_size (e, kind,
                                                             alphabet, freq);
          unsigned long desc_cost = fresh_cost;
          int built;

          built = ans_build (e->table, freq, alphabet);
          bw_init (&w, e->stage + start, STAGE_DATA_MAX - start);

          if (built)
            {
              encode_values (e, kind, &w, e->table, width);
            }

          if (built && !w.failed)
            {
              unsigned long fresh_bytes, best_bits, phased_bytes = 0;

              ansbits = w.bits;
              fresh_bytes = (ansbits + 7UL) / 8UL;
              best_bits = (fresh_bytes + fresh_cost) * 8UL + TABLE_LOG;

              if (e->level >= 6
                  && !table_cache_matches (e, kind, freq, alphabet)
                  && table_cache_covers (e, kind, alphabet))
                {
                  AnsTable reuse_table;
                  BitWriter reuse_writer;

                  bw_init (&reuse_writer, e->stage + start,
                           STAGE_DATA_MAX - start);

                  if (ans_build (&reuse_table, table_cache (e, kind),
                                 alphabet))
                    {
                      encode_values (e, kind, &reuse_writer, &reuse_table,
                                     width);

                      if (!reuse_writer.failed)
                        {
                          unsigned long reuse_bytes
                              = (reuse_writer.bits + 7UL) / 8UL;

                          if (reuse_bytes + 1UL < fresh_bytes + fresh_cost)
                            {
                              choice = CHOICE_REUSE;
                              desc_cost = 1UL;
                              ansbits = reuse_writer.bits;
                              best_bits = (reuse_bytes + 1UL) * 8UL
                                          + TABLE_LOG;
                              w = reuse_writer;
                            }
                          else
                            {
                              bw_init (&w, e->stage + start,
                                       STAGE_DATA_MAX - start);
                              built = ans_build (e->table, freq, alphabet);

                              if (built)
                                {
                                  encode_values (e, kind, &w, e->table, width);
                                }
                            }
                        }
                      else
                        {
                          bw_init (&w, e->stage + start,
                                   STAGE_DATA_MAX - start);
                          built = ans_build (e->table, freq, alphabet);

                          if (built)
                            {
                              encode_values (e, kind, &w, e->table, width);
                            }
                        }
                    }
                }

              if (phase_ok && total >= PHASE_MIN_SYMBOLS
                  && choose_phased (e, kind, alphabet, best_bits, &pid,
                                    pfreq, pempty, preuse, &phased_bytes))
                {
                  choice = CHOICE_PHASED;
                  desc_cost = phased_bytes;
                  bw_init (&w, e->stage + start, STAGE_DATA_MAX - start);
                  built = encode_phased (e, kind, &w, alphabet, pid, pfreq,
                                         pempty, preuse);
                  if (built)
                    {
                      ansbits = w.bits;
                    }
                }
            }

          if (built && !w.failed)
            {
              ansbits = w.bits;

              if (((ansbits + 7UL) / 8UL) + desc_cost
                  < ((unsigned long)total * width + 7UL) / 8UL + 2UL)
                {
                  mode = MODE_TANS;
                }
            }
        }
    }

  if (mode == MODE_EMPTY)
    {
      if (e->desc_len >= DESC_MAX)
        {
          return 0;
        }

      e->desc[e->desc_len++] = MODE_EMPTY;
    }
  else if (mode == MODE_RLE)
    {
      if (e->desc_len >= DESC_MAX)
        {
          return 0;
        }

      e->desc[e->desc_len++] = MODE_RLE;

      if (!put_var (e->desc, &e->desc_len, DESC_MAX, single))
        {
          return 0;
        }
    }
  else if (mode == MODE_TANS)
    {
      if (choice == CHOICE_PHASED)
        {
          if (!put_phased_descriptor (e, kind, alphabet, pid, pfreq, pempty,
                                      preuse))
            {
              return 0;
            }
        }
      else if (choice == CHOICE_REUSE)
        {
          if (e->desc_len >= DESC_MAX)
            {
              return 0;
            }

          e->desc[e->desc_len++] = MODE_REUSE;
        }
      else if (!put_compact_tans_descriptor (e, kind, alphabet, freq))
        {
          return 0;
        }

      e->stream_bits[kind] = ansbits;
      e->stream_bytes[kind] = (ansbits + 7UL) / 8UL;
      e->stage_len += e->stream_bytes[kind];
    }
  else
    {
      if (e->desc_len + 2 > DESC_MAX)
        {
          return 0;
        }

      e->desc[e->desc_len++] = MODE_RAW;
      e->desc[e->desc_len++] = (unsigned char)width;
      bw_init (&w, e->stage + start, STAGE_DATA_MAX - start);
      encode_values (e, kind, &w, (AnsTable *)0, width);

      if (w.failed)
        {
          return 0;
        }

      e->stream_bits[kind] = w.bits;
      e->stream_bytes[kind] = (w.bits + 7UL) / 8UL;
      e->stage_len += e->stream_bytes[kind];
    }

  return 1;
}

/*****************************************************************************/

static void
extra_parts (const Encoder *e, const Sequence *q, unsigned int values[4],
             unsigned int widths[4])
{
  unsigned int code, d = sequence_distance (q);

  (void)len_code (q->ll, 0, &values[0], &widths[0]);
  (void)len_code (q->ml, e->min_match, &values[1], &widths[1]);
  values[2] = 0U;
  widths[2] = 0U;
  code = distance_code (d, sequence_tag (q), e->window_n,
                        &values[3], &widths[3]);

  if (ENCODER_ALIGNED_DISTANCES (e)
      && ((code >= 6U && code <= 18U)
          || (code == 20U && e->window_n == WINDOW_64K)))
    {
      widths[2] = 1U;

      if ((d & 3U) == 0U)
        {
          values[2] = 1U;

          if (code == 20U)
            {
              values[3] = (d - 32772U) >> 2;
              widths[3] = 13U;
            }
          else
            {
              unsigned int bucket = code - 4U;

              values[3] = (d - (1U << bucket)) >> 2;
              widths[3] = bucket - 2U;
            }
        }
    }
}

/*****************************************************************************/

static void
write_extra_raw (Encoder *e, BitWriter *w)
{
  unsigned int i, part, values[4], widths[4];

  for (i = 0; i < e->sn; ++i)
    {
      extra_parts (e, &e->seq[i], values, widths);

      for (part = 0; part < 4; ++part)
        {
          bw_add (w, values[part], widths[part]);
        }
    }
}

/*****************************************************************************/

static int
encode_extras (Encoder *e)
{
  BitWriter w;

  if (e->stage_len > STAGE_DATA_MAX)
    {
      return 0;
    }

  bw_init (&w, e->stage + e->stage_len,
           STAGE_DATA_MAX - e->stage_len);
  write_extra_raw (e, &w);

  if (w.failed)
    {
      return 0;
    }

  e->stream_bits[STREAM_EXTRA] = w.bits;
  e->stream_bytes[STREAM_EXTRA] = (w.bits + 7UL) / 8UL;
  e->stage_len += e->stream_bytes[STREAM_EXTRA];

  return 1;
}

/*****************************************************************************/

static void
make_block_header (unsigned char *hdr, unsigned int type,
                   unsigned int n, unsigned int q, unsigned long dl,
                   const unsigned long *bits, unsigned long crc,
                   int aligned_distances)
{
  unsigned int i, padding = 0;

  (void)memset (hdr, 0, LZ_BLOCK_HEADER_N);

  hdr[0] = (unsigned char)(type
                           | (aligned_distances
                                  ? BLOCK_FLAG_ALIGNED_DISTANCES : 0U));

  store16 (hdr + 1, n);
  store16 (hdr + 3, q);
  store16 (hdr + 5, (unsigned int)dl);

  for (i = 0; i < STREAM_COUNT; ++i)
    {
      unsigned long bytes = (bits[i] + 7UL) / 8UL;
      unsigned int pad = (unsigned int)((8UL - (bits[i] & 7UL)) & 7UL);

      store16 (hdr + 7 + 2U * i, (unsigned int)bytes);
      padding |= pad << (3U * i);
    }

  store16 (hdr + 17, padding);
  store32 (hdr + 19, crc);
}

/*****************************************************************************/

static int
write_stored (FILE *out, Encoder *e, unsigned long crc, unsigned int width)
{
  unsigned int i;
  unsigned long nbytes;
  unsigned char h[STORED_BLOCK_HEADER_N];
  BitWriter bw;

  nbytes = ((unsigned long)e->n * width + 7UL) / 8UL;

  if (nbytes > STAGE_MAX)
    {
      return 0;
    }

  bw_init (&bw, e->stage, STAGE_MAX);

  for (i = 0; i < e->n; ++i)
    {
      bw_add (&bw, e->buffer[e->window_n + i], width);
    }

  if (bw.failed)
    {
      return 0;
    }

  (void)memset (h, 0, sizeof (h));
  h[0] = (unsigned char)(BLOCK_STORED | (width << 2));
  store16 (h + 1, e->n);
  store32 (h + 3, crc);

  return write_exact (out, h, sizeof (h))
         && write_exact (out, e->stage, (size_t)nbytes);
}

/*****************************************************************************/

static int
encode_block (FILE *out, Encoder *e)
{
  unsigned long crc = input_crc (e->buffer + e->window_n, e->n, e->stage);
  unsigned int i, width = 7;
  unsigned int saved_search_depth;
  int saved_lazy;
  int compressed_ok = 1, block_has_9bit = 0, block_aligned = 0;
  unsigned long stored, compressed;
  unsigned char h[LZ_BLOCK_HEADER_N];
  unsigned int saved_reps[3];

  (void)memcpy (saved_reps, e->reps, sizeof (saved_reps));
  saved_search_depth = e->search_depth;
  saved_lazy = e->lazy;

  for (i = 0; i < e->n; ++i)
    {
      unsigned int value = e->buffer[e->window_n + i];

      if (value >= 256U)
        {
          width = 9U;
          block_has_9bit = 1;

          break;
        }

      if (value >= 128U)
        {
          width = 8U;
        }
    }

  e->use_cost = 0;

  if (e->level >= 4)
    {
      e->search_depth = 2U;
      e->lazy = 0;
    }

  if (!parse_block (e))
    {
      return 0;
    }

  e->search_depth = saved_search_depth;
  e->lazy = saved_lazy;

  if (e->level >= 4)
    {
      if (early_stored_block (e, width))
        {
          (void)memcpy (e->reps, saved_reps, sizeof (saved_reps));

          return write_stored (out, e, crc, width);
        }

      estimate_costs (e);
      (void)memcpy (e->reps, saved_reps, sizeof (saved_reps));
      e->use_cost = 1;

      if (!parse_block (e))
        {
          return 0;
        }

      if (e->level == 9)
        {
          estimate_costs (e);
          (void)memcpy (e->reps, saved_reps, sizeof (saved_reps));

          if (!parse_block (e))
            {
              return 0;
            }
        }
    }

  e->distance_flags &= ~OPTION_ALIGNED_DISTANCES;

  if (block_has_9bit && aligned_distance_cost_wins (e))
    {
      e->distance_flags |= OPTION_ALIGNED_DISTANCES;
    }

  block_aligned = ENCODER_ALIGNED_DISTANCES (e);

  (void)memcpy (e->prefix, /* //-V1086 */
    e->stage + STAGE_DATA_MAX, TABLE_CACHE_BYTES);

  e->desc_len = 0;
  e->stage_len = 0;

  if (!encode_one_stream (e, STREAM_LIT)
      || !encode_one_stream (e, STREAM_LL)
      || !encode_one_stream (e, STREAM_ML)
      || !encode_one_stream (e, STREAM_OF))
    {
      compressed_ok = 0;
    }

  if (compressed_ok && !encode_extras (e))
    {
      compressed_ok = 0;
    }

  for (i = 0; compressed_ok && i < STREAM_COUNT; ++i)
    {
      if (e->stream_bytes[i] > 65535UL)
        {
          compressed_ok = 0;
        }
    }

  stored = ((unsigned long)e->n * width + 7UL) / 8UL;
  compressed = e->desc_len + e->stage_len;

  if (!compressed_ok || e->stage_len > STAGE_DATA_MAX
      || e->desc_len > DESC_MAX
      || compressed >= stored
      || e->stream_bits[STREAM_EXTRA] > STAGE_DATA_MAX * 8UL)
    {
      (void)memcpy (e->reps, saved_reps, sizeof (saved_reps));
      (void)memcpy (e->stage + STAGE_DATA_MAX, e->prefix, /* //-V1086 */
                    TABLE_CACHE_BYTES);

      return write_stored (out, e, crc, width);
    }

  make_block_header (h, BLOCK_LZ, e->n, e->sn, e->desc_len,
                     e->stream_bits, crc, block_aligned);

  return write_exact (out, h, sizeof (h))
         && write_exact (out, e->desc, (size_t)e->desc_len)
         && write_exact (out, e->stage, (size_t)e->stage_len);
}

/*****************************************************************************/

static int
parse_tans_model (DecTable *t, unsigned int kind, unsigned int alphabet,
                  const unsigned char *p, unsigned long *offset,
                  unsigned long len)
{
  unsigned int nused = 0, i, sum = 0, bitmap_bytes;
  unsigned short freq[512];
  const unsigned char *bitmap;
  BitReader r;

  (void)memset (freq, 0, sizeof (freq));

  bitmap_bytes = (alphabet + 7U) / 8U;

  if (*offset + bitmap_bytes > len)
    {
      return 0;
    }

  bitmap = p + *offset;
  *offset += bitmap_bytes;

  if ((alphabet & 7U)
      && (bitmap[bitmap_bytes - 1U]
          & ((1U << (8U - (alphabet & 7U))) - 1U)))
    {
      return 0;
    }

  for (i = 0; i < alphabet; ++i)
    {
      if (bitmap[i >> 3] & (1U << (7U - (i & 7U))))
        {
          ++nused;
        }
    }

  if (nused < 2U)
    {
      return 0;
    }

  br_init (&r, p + *offset, (len - *offset) * 8UL, 0);

  {
    unsigned int remaining = nused;

    for (i = 0; i < alphabet; ++i)
      {
        if (bitmap[i >> 3] & (1U << (7U - (i & 7U))))
          {
            unsigned int f;

            if (kind == STREAM_LIT)
              {
                unsigned int zeros = 0, bit;

                do
                  {
                    bit = br_get (&r, 1U);

                    if (r.failed)
                      {
                        return 0;
                      }

                    if (!bit)
                      {
                        ++zeros;

                        if (zeros > TABLE_LOG)
                          {
                            return 0;
                          }
                      }
                  }
                while (!bit);

                f = (1U << zeros) | br_get (&r, zeros);
              }
            else
              {
                unsigned int range, bits, cutoff, value;

                --remaining;
                range = TABLE_N - sum - remaining;
                bits = floor_log2 (range);
                cutoff = (1U << (bits + 1U)) - range;
                value = br_get (&r, bits);

                if (value >= cutoff)
                  {
                    value = (value << 1) | br_get (&r, 1U);
                    value -= cutoff;
                  }

                f = value + 1U;
              }

            if (r.failed || !f || sum > TABLE_N || f > TABLE_N - sum)
              {
                return 0;
              }

            freq[i] = (unsigned short)f;
            sum += f;
          }
      }
    }

  if (sum != TABLE_N)
    {
      return 0;
    }

  {
    unsigned int padding = (unsigned int)((8UL - (r.position & 7UL))
                                           & 7UL);

    if (padding && br_get (&r, padding) != 0U)
      {
        return 0;
      }
  }

  if (r.failed || (r.position & 7UL) || !dec_build (t, freq, alphabet))
    {
      return 0;
    }

  *offset += r.position / 8UL;

  return 1;
}

/*****************************************************************************/

static void
dec_poison (DecTable *t, unsigned int alphabet)
{
  unsigned int x;

  t->alphabet = (unsigned short)alphabet;

  for (x = 0; x < TABLE_N; ++x)
    {
      t->e[x] = DEC_POISON;
    }
}

/*****************************************************************************/

static int
parse_descriptor (Stream *s, DecTable *tables, unsigned int kind,
                  unsigned int of_alphabet, int phased,
                  const unsigned char *p, unsigned long *offset,
                  unsigned long len)
{
  DecTable *t = tables + kind;
  unsigned int alphabet = stream_alphabet (kind, of_alphabet);
  unsigned int v;

  if (*offset >= len)
    {
      return 0;
    }

  s->mode = p[(*offset)++];
  s->table = t;
  s->rle = 0;
  s->width = 0;

  if (s->mode == MODE_RAW)
    {
      if (*offset >= len)
        {
          return 0;
        }

      s->width = p[(*offset)++];

      if (s->width != raw_width (kind, 511)
          && !(kind == STREAM_LIT && (s->width == 7U || s->width == 8U)))
        {
          return 0;
        }
    }
  else if (s->mode == MODE_RLE)
    {
      if (!get_var (p, offset, len, &v) || v >= alphabet)
        {
          return 0;
        }

      s->rle = (unsigned short)v;
    }
  else if (s->mode == MODE_TANS)
    {
      if (!parse_tans_model (t, kind, alphabet, p, offset, len))
        {
          return 0;
        }
    }
  else if (s->mode == MODE_PHASED)
    {
      unsigned int pid, g, ph, tans_groups = 0;
      const PhasePartition *pp;
      const DecTable *gp[PHASE_GROUPS_MAX];
      DecTable *gt;

      if (!phased || kind > STREAM_ML || *offset >= len)
        {
          return 0;
        }

      pid = p[(*offset)++];

      if (pid >= phase_partition_count[kind])
        {
          return 0;
        }

      pp = &phase_partitions[kind][pid];
      gt = tables + phase_table_base[kind];

      for (g = 0; g < pp->groups; ++g)
        {
          unsigned int gmode;

          if (*offset >= len)
            {
              return 0;
            }

          gmode = p[(*offset)++];
          gp[g] = gt + g;

          if (gmode == MODE_TANS)
            {
              if (!parse_tans_model (gt + g, kind, alphabet, p, offset, len))
                {
                  return 0;
                }

              ++tans_groups;
            }
          else if (gmode == MODE_REUSE)
            {
              if (gt[g].alphabet != alphabet)
                {
                  return 0;
                }

              ++tans_groups;
            }
          else if (gmode == MODE_EMPTY)
            {
              gp[g] = tables + DEC_POISON_SLOT;
            }
          else
            {
              return 0;
            }
        }

      if (!tans_groups)
        {
          return 0;
        }

      for (ph = 0; ph < 4U; ++ph)
        {
          s->ptab[ph] = gp[pp->gmap[ph]];
        }
    }
  else if (s->mode == MODE_REUSE)
    {
      if (t->alphabet != alphabet)
        {
          return 0;
        }
    }
  else if (s->mode != MODE_EMPTY)
    {
      return 0;
    }

  return 1;
}

/*****************************************************************************/

static int
stream_begin (Stream *s, unsigned long bits, const unsigned char *p)
{
  s->bits = bits;
  s->data = p;
  br_init (&s->br, p, bits, stream_uses_ans (s->mode));

  if (stream_uses_ans (s->mode))
    {
      if (bits < TABLE_LOG)
        {
          return 0;
        }

      s->state = (unsigned short)br_get (&s->br, TABLE_LOG);
    }
  else if (s->mode == MODE_EMPTY || s->mode == MODE_RLE)
    {
      if (bits)
        {
          return 0;
        }

      s->state = 0;
    }
  else
    {
      s->state = 0;
    }

  return !s->br.failed;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
stream_get (Stream *s)
{
  if (s->mode == MODE_RAW)
    {
      return br_get (&s->br, s->width);
    }

  if (s->mode == MODE_RLE)
    {
      return s->rle;
    }

  if (stream_is_tans (s->mode))
    {
      return ans_get (s);
    }

  s->br.failed = 1;

  return 0;
}

/*****************************************************************************/

static LZ9_INLINE unsigned int
stream_get_ph (Stream *s, unsigned int phase)
{
  if (s->mode == MODE_PHASED)
    {
      return ans_get_with (s, s->ptab[phase]);
    }

  return stream_get (s);
}

/*****************************************************************************/

static int
stream_end (const Stream *s)
{
  if (s->br.failed || s->br.position != s->br.bits)
    {
      return 0;
    }

  if (stream_uses_ans (s->mode) && s->state != 0)
    {
      return 0;
    }

  return 1;
}

/*****************************************************************************/

static int
extra_end (ExtraReader *x)
{
  return !x->failed && !x->raw->failed && x->raw->position == x->raw->bits;
}

/*****************************************************************************/

static LZ9_INLINE int
store_symbol (Output *o, unsigned int v)
{
  if (v > 511U || (o->out_mode == 8 && v > 255U))
    {
      return 0;
    }

  o->ring[o->produced & (o->window_n - 1U)] = (unsigned short)v;
  ++o->produced;
  ++o->in_block;

  return 1;
}

/*****************************************************************************/

static void
store_match (Output *o, unsigned int d, unsigned int ml)
{
  const unsigned int mask = o->window_n - 1U;
  unsigned short * const ring = o->ring;
  const unsigned long prod = o->produced;
  unsigned int src, dst;

  src = (unsigned int)((prod - d) & mask);
  dst = (unsigned int)(prod & mask);

  if (d >= ml && d <= o->window_n - ml)
    {
      if (dst + ml <= o->window_n && src + ml <= o->window_n)
        {
          (void)memcpy (ring + dst, ring + src,
                        (size_t)ml * sizeof (unsigned short));
        }
      else
        {
          unsigned int remaining = ml;

          while (remaining)
            {
              unsigned int count = remaining;
              unsigned int src_left = o->window_n - src;
              unsigned int dst_left = o->window_n - dst;

              if (count > src_left)
                {
                  count = src_left;
                }

              if (count > dst_left)
                {
                  count = dst_left;
                }

              (void)memcpy (ring + dst, ring + src,
                            (size_t)count * sizeof (unsigned short));
              src = (src + count) & mask;
              dst = (dst + count) & mask;
              remaining -= count;
            }
        }
    }
  else if (d == 1U)
    {
      unsigned short v = ring[src];
      unsigned int remaining = ml;

      while (remaining)
        {
          unsigned int count = remaining;
          unsigned int dst_left = o->window_n - dst;
          unsigned int i;

          if (count > dst_left)
            {
              count = dst_left;
            }

          for (i = 0; i < count; ++i)
            {
              ring[dst + i] = v;
            }

          dst = (dst + count) & mask;
          remaining -= count;
        }
    }
  else if (dst + ml <= o->window_n && src + ml <= o->window_n)
    {
      unsigned int i;

      for (i = 0; i < ml; ++i)
        {
          ring[dst + i] = ring[src + i];
        }
    }
  else
    {
      unsigned int i;

      for (i = 0; i < ml; ++i)
        {
          unsigned int v = ring[src];

          ring[dst] = (unsigned short)v;
          src = (src + 1U) & mask;
          dst = (dst + 1U) & mask;
        }
    }

  o->produced += ml;
  o->in_block += ml;
}

/*****************************************************************************/

static int
output_symbol9 (Output *o, unsigned int v)
{
  o->out_acc = (o->out_acc << 9) | v;
  o->out_bits += 9U;

  if (o->out_bits >= 16U)
    {
      o->out_bits -= 8U;

      if (!output_octet (o, (o->out_acc >> o->out_bits) & 255U))
        {
          return 0;
        }

      o->out_acc &= (1U << o->out_bits) - 1U;
      o->out_bits -= 8U;

      if (!output_octet (o, (o->out_acc >> o->out_bits) & 255U))
        {
          return 0;
        }

      o->out_acc &= (1U << o->out_bits) - 1U;
    }
  else
    {
      o->out_bits -= 8U;

      if (!output_octet (o, (o->out_acc >> o->out_bits) & 255U))
        {
          return 0;
        }

      o->out_acc &= (1U << o->out_bits) - 1U;
    }

  return 1;
}

/*****************************************************************************/

static int
output_packed9 (Output *o, const unsigned char *packed)
{
  unsigned int i, bits = o->out_bits, carry = o->out_acc;

  if (!bits)
    {
      if (o->out_used + 9U <= sizeof (o->outbuf))
        {
          (void)memcpy (o->outbuf + o->out_used, packed, 9);
          o->out_used += 9U;

          if (o->out_used == sizeof (o->outbuf))
            {
              if (!write_exact (o->out, o->outbuf, sizeof (o->outbuf)))
                {
                  return 0;
                }

              o->out_used = 0;
            }

          return 1;
        }

      for (i = 0; i < 9U; ++i)
        {
          if (!output_octet (o, packed[i]))
            {
              return 0;
            }
        }

      return 1;
    }

  {
    unsigned int mask = (1U << bits) - 1U;

    for (i = 0; i < 9U; ++i)
      {
        unsigned int value
            = (carry << (8U - bits)) | ((unsigned int)packed[i] >> bits);

        if (!output_octet (o, value & 255U))
          {
            return 0;
          }

        carry = (unsigned int)packed[i] & mask;
      }
  }

  o->out_acc = carry;

  return 1;
}

/*****************************************************************************/

static int
output_decoded_block (Output *o, unsigned long start, unsigned int n)
{
  const unsigned int mask = o->window_n - 1U;
  unsigned int i = 0;

  if (o->out_mode == 8)
    {
      unsigned int at = (unsigned int)(start & mask);

      while (n)
        {
          unsigned int seg = o->window_n - at;
          const unsigned short *src;

          if (seg > n)
            {
              seg = n;
            }

          src = o->ring + at;

          while (seg)
            {
              unsigned int avail
                  = (unsigned int)(sizeof (o->outbuf) - o->out_used);
              unsigned int chunk = seg < avail ? seg : avail;
              unsigned int k;

              for (k = 0; k < chunk; ++k)
                {
                  o->outbuf[o->out_used + k] = (unsigned char)src[k];
                }

              o->out_used += chunk;
              src += chunk;
              seg -= chunk;
              n -= chunk;

              if (o->out_used == sizeof (o->outbuf))
                {
                  if (!write_exact (o->out, o->outbuf, sizeof (o->outbuf)))
                    {
                      return 0;
                    }

                  o->out_used = 0;
                }
            }

          at = 0;
        }

      return 1;
    }

  while (n - i >= 8U)
    {
      unsigned int at = (unsigned int)((start + i) & mask);
      unsigned short values[8];
      const unsigned short *source;
      unsigned char packed[9];

      if (at <= mask - 7U)
        {
          source = o->ring + at;
        }
      else
        {
          unsigned int j;

          for (j = 0; j < 8U; ++j)
            {
              values[j] = o->ring[(at + j) & mask];
            }

          source = values;
        }

      pack8_symbols (source, packed);

      if (!output_packed9 (o, packed))
        {
          return 0;
        }

      i += 8U;
    }

  while (i < n)
    {
      unsigned int v = o->ring[(start + i) & mask];

      if (!output_symbol9 (o, v))
        {
          return 0;
        }

      ++i;
    }

  return 1;
}

/*****************************************************************************/

static void
block_begin (Output *o)
{
  o->in_block = 0;
}

/*****************************************************************************/

static int
block_crc_end (Output *o, unsigned int n, unsigned long expected)
{
  unsigned long start, crc = 0UL;
  unsigned int i = 0, used = 0;
  const unsigned int mask = o->window_n - 1U;

  if (o->in_block != n || o->produced < n)
    {
      return 0;
    }

  start = o->produced - n;

  while (n - i >= 8U)
    {
      unsigned int at = (unsigned int)((start + i) & mask);
      unsigned short values[8];
      const unsigned short *source;

      if (used > CRC_BUFFER_N - 9U)
        {
          crc = crc_update (crc, o->crc_buf, used);
          used = 0;
        }

      if (at <= mask - 7U)
        {
          source = o->ring + at;
        }
      else
        {
          unsigned int j;

          for (j = 0; j < 8U; ++j)
            {
              values[j] = o->ring[(at + j) & mask];
            }

          source = values;
        }

      pack8_symbols (source, o->crc_buf + used);
      used += 9U;
      i += 8U;
    }

  if (i < n)
    {
      unsigned int tail = n - i;
      unsigned short values[7] = { 0U };
      unsigned int j, at = (unsigned int)((start + i) & mask);

      if (used > CRC_BUFFER_N - 8U)
        {
          crc = crc_update (crc, o->crc_buf, used);
          used = 0;
        }

      for (j = 0; j < tail; ++j)
        {
          values[j] = o->ring[(at + j) & mask];
        }

      used += pack9_tail (values, tail, o->crc_buf + used);
    }

  if (used)
    {
      crc = crc_update (crc, o->crc_buf, used);
    }

  if (crc != expected)
    {
      return 0;
    }

  return output_decoded_block (o, start, n);
}

/*****************************************************************************/

static int
decode_phased_literals (Output *o, Stream *s, unsigned long count)
{
  const DecTable *pt[4];
  unsigned int ph = (unsigned int)o->produced & 3U, i;

  for (i = 0; i < 4U; ++i)
    {
      pt[i] = s->ptab[(ph + i) & 3U];
    }

  while (count >= 4UL)
    {
      for (i = 0; i < 4U; ++i)
        {
          if (!store_symbol (o, ans_get_with (s, pt[i])))
            {
              return 0;
            }
        }

      count -= 4UL;
    }

  for (i = 0; i < (unsigned int)count; ++i)
    {
      if (!store_symbol (o, ans_get_with (s, pt[i])))
        {
          return 0;
        }
    }

  return 1;
}

/*****************************************************************************/

static int
decode_block (FILE *in, Output *o, unsigned int max_n, unsigned char *stage,
              unsigned char *desc, DecTable *tables)
{
  unsigned char h[LZ_BLOCK_HEADER_N];
  unsigned long bitlen[STREAM_COUNT], lengths[STREAM_COUNT], total = 0;
  unsigned long desc_len, expected;
  unsigned int i, n, q, type, width;
  unsigned int padding;
  int aligned_distances;
  unsigned int min_match = o->min_match;
  unsigned long off = 0, pos = 0;
  Stream s[ENTROPY_STREAM_COUNT];
  BitReader raw_extra;
  ExtraReader extra;

  if (!read_exact (in, h, BLOCK_PREFIX_N))
    {
      return 0;
    }

  type = h[0] & 3U;

  if (type == BLOCK_STORED)
    {
      unsigned long stored_bits, stored_bytes;
      BitReader r;

      width = h[0] >> 2;

      if (width != 7U && width != 8U && width != 9U)
        {
          return 0;
        }

      if (!read_exact (in, h + BLOCK_PREFIX_N,
                       STORED_BLOCK_HEADER_N - BLOCK_PREFIX_N))
        {
          return 0;
        }

      n = load16 (h + 1);
      expected = load32 (h + 3);

      if (!n || n > max_n || n > BLOCK_N)
        {
          return 0;
        }

      stored_bits = (unsigned long)n * width;
      stored_bytes = (stored_bits + 7UL) / 8UL;

      if (!read_exact (in, stage, (size_t)stored_bytes))
        {
          return 0;
        }

      if ((stored_bits & 7UL)
          && (stage[stored_bytes - 1UL]
              & ((1U << (8U - (stored_bits & 7UL))) - 1U)))
        {
          return 0;
        }

      block_begin (o);
      br_init (&r, stage, stored_bits, 0);

      for (i = 0; i < n; ++i)
        {
          if (!store_symbol (o, br_get (&r, width)))
            {
              return 0;
            }
        }

      return !r.failed && r.position == r.bits && o->in_block == n
             && block_crc_end (o, n, expected);
    }

  if (type != BLOCK_LZ
      || !read_exact (in, h + BLOCK_PREFIX_N,
                      LZ_BLOCK_HEADER_N - BLOCK_PREFIX_N))
    {
      return 0;
    }

  width = (h[0] >> 2) & 3U;
  aligned_distances
      = OUTPUT_AUTO_ALIGNED_DISTANCES (o)
            ? ((h[0] & BLOCK_FLAG_ALIGNED_DISTANCES) != 0U)
            : OUTPUT_ALIGNED_DISTANCES (o);
  n = load16 (h + 1);
  q = load16 (h + 3);
  desc_len = load16 (h + 5);
  padding = load16 (h + 17);
  expected = load32 (h + 19);

  if ((h[0] & 0xe0U)
      || ((h[0] & BLOCK_FLAG_ALIGNED_DISTANCES)
          && !OUTPUT_AUTO_ALIGNED_DISTANCES (o))
      || width != 0U || !n || n > max_n || n > BLOCK_N || q > n / min_match
      || desc_len > DESC_MAX || (padding & 0x8000U))
    {
      return 0;
    }

  for (i = 0; i < STREAM_COUNT; ++i)
    {
      unsigned int pad = (padding >> (3U * i)) & 7U;
      unsigned long max_bits;

      lengths[i] = load16 (h + 7 + 2U * i);

      if ((!lengths[i] && pad) || pad > lengths[i] * 8UL)
        {
          return 0;
        }

      bitlen[i] = lengths[i] * 8UL - pad;

      if (i == STREAM_LIT)
        {
          max_bits = (unsigned long)BLOCK_N * 10UL + TABLE_LOG;
        }
      else if (i < STREAM_EXTRA)
        {
          max_bits = (unsigned long)(BLOCK_N / min_match) * 10UL + TABLE_LOG;
        }
      else
        {
          max_bits = (unsigned long)(BLOCK_N / min_match) * 46UL;
        }

      if (bitlen[i] > max_bits || total + lengths[i] > STAGE_MAX)
        {
          return 0;
        }

      total += lengths[i];
    }

  if (total > STAGE_MAX
      || !read_exact (in, desc, (size_t)desc_len)
      || !read_exact (in, stage, (size_t)total))
    {
      return 0;
    }

  for (i = 0; i < STREAM_COUNT; ++i)
    {
      unsigned int pad = (padding >> (3U * i)) & 7U;

      if (pad && lengths[i]
          && (stage[pos + lengths[i] - 1UL] & ((1U << pad) - 1U)))
        {
          return 0;
        }

      pos += lengths[i];
    }

  block_begin (o);
  pos = 0;

  for (i = 0; i < ENTROPY_STREAM_COUNT; ++i)
    {
      unsigned int kind = i;

      if (!parse_descriptor (&s[i], tables, kind, o->of_alphabet, o->phased,
                             desc, &off, desc_len))
        {
          return 0;
        }

      if (i != STREAM_LIT && ((s[i].mode == MODE_EMPTY) != (q == 0U)))
        {
          return 0;
        }
    }

  if (off != desc_len)
    {
      return 0;
    }

  for (i = 0; i < ENTROPY_STREAM_COUNT; ++i)
    {
      if (!stream_begin (&s[i], bitlen[i], stage + pos))
        {
          return 0;
        }

      pos += lengths[i];
    }

  br_init (&raw_extra, stage + pos, bitlen[STREAM_EXTRA], 0);
  extra.raw = &raw_extra;
  extra.failed = 0;

  {
    unsigned int produced = 0, literals = 0;

    for (i = 0; i < q; ++i)
      {
        unsigned int j, lr, ml, d, code;

        code = stream_get_ph (&s[STREAM_LL], (unsigned int)o->produced & 3U);
        lr = decode_len (code, 0, &extra);

        if (extra.failed || lr > n - produced)
          {
            return 0;
          }

        if (s[STREAM_LIT].mode == MODE_PHASED)
          {
            if (!decode_phased_literals (o, &s[STREAM_LIT], lr))
              {
                return 0;
              }
          }
        else if (stream_is_tans (s[STREAM_LIT].mode))
          {
            for (j = 0; j < lr; ++j)
              {
                unsigned int v = ans_get (&s[STREAM_LIT]);

                if (!store_symbol (o, v))
                  {
                    return 0;
                  }
              }
          }
        else
          {
            for (j = 0; j < lr; ++j)
              {
                unsigned int v = stream_get (&s[STREAM_LIT]);

                if (v >= 512 || !store_symbol (o, v))
                  {
                    return 0;
                  }
              }
          }

        produced += lr;
        literals += lr;
        code = stream_get_ph (&s[STREAM_ML], (unsigned int)o->produced & 3U);
        ml = decode_len (code, min_match, &extra);
        code = stream_get (&s[STREAM_OF]);
        d = decode_dist (code, &extra, o->reps, o->window_n,
                         o->of_alphabet, aligned_distances);

        {
          unsigned int max_d
              = (o->produced < o->window_n) ? (unsigned int)o->produced
                                            : o->window_n;

          if (extra.failed || ml < min_match || ml > n - produced
              || (d - 1U) >= max_d)
            {
              return 0;
            }
        }

        store_match (o, d, ml);
        produced += ml;
      }

    if (s[STREAM_LIT].mode == MODE_PHASED)
      {
        unsigned long rest = n - produced;

        if (!decode_phased_literals (o, &s[STREAM_LIT], rest))
          {
            return 0;
          }

        literals += rest;
      }
    else if (stream_is_tans (s[STREAM_LIT].mode))
      {
        while (produced < n)
          {
            unsigned int v = ans_get (&s[STREAM_LIT]);

            if (!store_symbol (o, v))
              {
                return 0;
              }

            ++produced;
            ++literals;
          }
      }
    else
      {
        while (produced < n)
          {
            unsigned int v = stream_get (&s[STREAM_LIT]);

            if (v >= 512 || !store_symbol (o, v))
              {
                return 0;
              }

            ++produced;
            ++literals;
          }
      }

    if ((s[STREAM_LIT].mode == MODE_EMPTY) != (literals == 0U)
        || !extra_end (&extra))
      {
        return 0;
      }
  }

  for (i = 0; i < ENTROPY_STREAM_COUNT; ++i)
    {
      if (!stream_end (&s[i]))
        {
          return 0;
        }
    }

  return o->in_block == n && block_crc_end (o, n, expected);
}

/*****************************************************************************/

static int
allocated_encoder (Encoder *e, unsigned int window_n)
{
  (void)memset (e, 0, sizeof (*e));
  e->window_n = window_n;
  e->of_alphabet = window_n == WINDOW_64K ? OF_ALPHABET_64K : OF_ALPHABET_32K;
  e->buffer = (unsigned short *)calloc (window_n + BLOCK_N,
                                         sizeof (unsigned short));
  e->seq = (Sequence *)malloc (SEQ_MAX * sizeof (Sequence));
  e->head = (int *)malloc (HASH_N * sizeof (int));
  e->link = (int *)malloc (window_n * sizeof (int));
  e->stage = (unsigned char *)malloc (STAGE_MAX);
  e->desc = (unsigned char *)malloc (DESC_MAX);
  e->table = (AnsTable *)malloc (sizeof (AnsTable));
  e->gtable = (AnsTable *)malloc (PHASE_GROUPS_MAX * sizeof (AnsTable));

  if (e->stage)
    {
      (void)memset (e->stage + STAGE_DATA_MAX, 0, TABLE_CACHE_BYTES);
    }

  if (e->link)
    {
      (void)memset (e->link, 0, window_n * sizeof (int));
    }

  return e->buffer && e->seq && e->head && e->link && e->stage && e->desc
         && e->table && e->gtable;
}

/*****************************************************************************/

static void
free_encoder (Encoder *e)
{
  free (e->buffer);
  free (e->seq);
  free (e->head);
  free (e->link);
  free (e->stage);
  free (e->desc);
  free (e->table);
  free (e->gtable);
}

/*****************************************************************************/

static int
encode_file (FILE *in, FILE *out, unsigned int mode, unsigned long elements,
             int level, unsigned int window_n, unsigned int features)
{
  static const unsigned short depth[9] = { 2, 4, 8, 12, 24, 48, 96, 192, 512 };
  Encoder e;
  unsigned long done = 0;
  unsigned int acc = 0, nbits = 0;
  unsigned char hdr[FHEAD_N];

  if (!allocated_encoder (&e, window_n))
    {
      free_encoder (&e);

      return 0;
    }

  e.search_depth = depth[level - 1];
  e.lazy = level >= 5;
  e.level = level;
  e.of_alphabet = window_n == WINDOW_64K ? OF_ALPHABET_64K
                                         : OF_ALPHABET_32K;
  e.input_mode = mode;
  e.features = features;
  e.min_match = (features & FEATURE_MIN_MATCH_2) ? MIN_MATCH_SHORT
                                                 : MIN_MATCH_DEFAULT;
  (void)memcpy (hdr, "LZ9A", 4);
  hdr[4] = FORMAT_V9;
  hdr[5] = (unsigned char)mode;
  store16 (hdr + 6, FHEAD_N);
  store32 (hdr + 8, elements);
  hdr[12] = (unsigned char)((window_n == WINDOW_64K ? FEATURE_WINDOW_64K : 0U)
                            | (features & (FEATURE_PHASED_TABLES
                                           | FEATURE_MIN_MATCH_2))
                            | FEATURE_ALIGNED_DISTANCES
                            | FEATURE_AUTO_ALIGNED_DISTANCES);
  store32 (hdr + 13, crc_bytes (hdr, 13));

  if (!write_exact (out, hdr, sizeof (hdr)))
    {
      free_encoder (&e);

      return 0;
    }

  while (done < elements)
    {
      unsigned long remaining = elements - done;
      unsigned int i,
          n = (unsigned int)(remaining > BLOCK_N ? BLOCK_N : remaining);
      unsigned int hist_new;

      e.n = n;
      e.frame_pos = done;

      if (mode == 8)
        {
          if (!read_exact (in, e.stage, (size_t)n))
            {
              free_encoder (&e);

              return 0;
            }

          for (i = 0; i < n; ++i)
            {
              e.buffer[e.window_n + i] = (unsigned short)e.stage[i];
            }
        }
      else
        {
          unsigned int needed_bits = n * 9U;
          unsigned int needed_bytes;
          unsigned int byte_pos = 0;

          if (needed_bits <= nbits)
            {
              needed_bytes = 0;
            }
          else
            {
              needed_bytes = (needed_bits - nbits + 7U) / 8U;
            }

          if (needed_bytes && !read_exact (in, e.stage, (size_t)needed_bytes))
            {
              free_encoder (&e);

              return 0;
            }

          i = 0;

          if (nbits == 0)
            {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__amd64__))
              while (i + 8U <= n)
                {
                  const unsigned char *p = e.stage + byte_pos;
                  unsigned short *dst = e.buffer + e.window_n + i;
                  lz9_u64 hi_be, hi, w0, w1;

                  (void)memcpy (&hi_be, p, 8);
                  hi = __builtin_bswap64 (hi_be);
                  w0 = hi >> 28;
                  w1 = ((hi & (lz9_u64)0x0fffffffUL) << 8)
                       | (unsigned int)p[8];

                  dst[0] = (unsigned short)((w0 >> 27) & 511U);
                  dst[1] = (unsigned short)((w0 >> 18) & 511U);
                  dst[2] = (unsigned short)((w0 >> 9) & 511U);
                  dst[3] = (unsigned short)(w0 & 511U);
                  dst[4] = (unsigned short)((w1 >> 27) & 511U);
                  dst[5] = (unsigned short)((w1 >> 18) & 511U);
                  dst[6] = (unsigned short)((w1 >> 9) & 511U);
                  dst[7] = (unsigned short)(w1 & 511U);

                  byte_pos += 9U;
                  i += 8U;
                }
#else
              while (i + 8U <= n)
                {
                  const unsigned char *p = e.stage + byte_pos;
                  unsigned short *dst = e.buffer + e.window_n + i;

                  dst[0] = (unsigned short)(((unsigned int)p[0] << 1)
                                            | ((unsigned int)p[1] >> 7));
                  dst[1] = (unsigned short)((((unsigned int)p[1] & 0x7fU) << 2)
                                            | ((unsigned int)p[2] >> 6));
                  dst[2] = (unsigned short)((((unsigned int)p[2] & 0x3fU) << 3)
                                            | ((unsigned int)p[3] >> 5));
                  dst[3] = (unsigned short)((((unsigned int)p[3] & 0x1fU) << 4)
                                            | ((unsigned int)p[4] >> 4));
                  dst[4] = (unsigned short)((((unsigned int)p[4] & 0x0fU) << 5)
                                            | ((unsigned int)p[5] >> 3));
                  dst[5] = (unsigned short)((((unsigned int)p[5] & 0x07U) << 6)
                                            | ((unsigned int)p[6] >> 2));
                  dst[6] = (unsigned short)((((unsigned int)p[6] & 0x03U) << 7)
                                            | ((unsigned int)p[7] >> 1));
                  dst[7] = (unsigned short)((((unsigned int)p[7] & 0x01U) << 8)
                                            | (unsigned int)p[8]);

                  byte_pos += 9U;
                  i += 8U;
                }
#endif /* if defined(__GNUC__) && (defined(__x86_64__) \
          || defined(__amd64__)) */
            }

          while (i < n)
            {
              while (nbits < 9U)
                {
                  acc = (acc << 8) | (unsigned int)e.stage[byte_pos++];
                  nbits += 8U;
                }

              nbits -= 9U;
              e.buffer[e.window_n + i]
                  = (unsigned short)((acc >> nbits) & 511U);
              acc &= (1U << nbits) - 1U;
              ++i;
            }
        }

      if (!encode_block (out, &e))
        {
          free_encoder (&e);

          return 0;
        }

      hist_new = e.hlen + n;

      if (hist_new > e.window_n)
        {
          hist_new = e.window_n;
        }

      (void)memmove (e.buffer + e.window_n - hist_new,
                     e.buffer + e.window_n + n - hist_new,
                     hist_new * sizeof (unsigned short));
      e.hlen = hist_new;
      done += n;
    }

  if (mode == 9 && acc != 0)
    {
      free_encoder (&e);

      return 0;
    }

  /*LINTED E_RET_INT_IMPLICITLY*/
  if (fgetc (in) != EOF || ferror (in) || ferror (out))
    {
      free_encoder (&e);

      return 0;
    }

  free_encoder (&e);

  return 1;
}

/*****************************************************************************/

static int
parse_frame_extensions (const unsigned char *hdr, unsigned int header_len,
                        unsigned long total, int *unknown_count)
{
  unsigned char seen[128];
  unsigned int pos = FRAME_HEADER_PREFIX_N;
  unsigned int end = header_len - 4U;

  (void)memset (seen, 0, sizeof (seen));
  *unknown_count = 0;

  while (pos < end)
    {
      unsigned int id, length, index;

      if (end - pos < 3U)
        {
          return 0;
        }

      id = hdr[pos++];
      length = load16 (hdr + pos);
      pos += 2U;

      if (!(id & 0x7fU) || length > end - pos)
        {
          return 0;
        }

      index = id & 0x7fU;

      if (seen[index])
        {
          return 0;
        }

      seen[index] = 1;

      if (id == FRAME_EXT_UNKNOWN_COUNT)
        {
          if (length != 0U || total != 0UL)
            {
              return 0;
            }

          *unknown_count = 1;
        }
      else if (id & 0x80U)
        {
          return 0;
        }

      pos += length;
    }

  return pos == end;
}

/*****************************************************************************/

static int
decode_file (FILE *in, FILE *out)
{
  unsigned char hdr[FRAME_HEADER_MAX_N];
  unsigned long total, done = 0;
  unsigned int feature_flags, window_n, header_len;
  unsigned char *stage = (unsigned char *)0, *desc = (unsigned char *)0;
  DecTable *tables = (DecTable *)0;
  Output *o = (Output *)0;
  int okay = 0, unknown_count = 0;

  if (!read_exact (in, hdr, FRAME_HEADER_PREFIX_N)
      || memcmp (hdr, "LZ9A", 4) != 0 || hdr[4] != FORMAT_V9
      || (hdr[5] != 8 && hdr[5] != 9))
    {
      return 0;
    }

  header_len = load16 (hdr + 6);

  if (header_len < FRAME_HEADER_MIN_N
      || header_len > FRAME_HEADER_MAX_N
      || !read_exact (in, hdr + FRAME_HEADER_PREFIX_N,
                      (size_t)(header_len - FRAME_HEADER_PREFIX_N))
      || load32 (hdr + header_len - 4U)
             != crc_bytes (hdr, header_len - 4U))
    {
      return 0;
    }

  feature_flags = (unsigned int)hdr[12];
  total = load32 (hdr + 8);

  if ((feature_flags & ~FEATURE_KNOWN_MASK) != 0U
      || ((feature_flags & FEATURE_AUTO_ALIGNED_DISTANCES)
          && !(feature_flags & FEATURE_ALIGNED_DISTANCES))
      || !parse_frame_extensions (hdr, header_len, total, &unknown_count)
      || unknown_count)
    {
      return 0;
    }

  window_n
      = (feature_flags & FEATURE_WINDOW_64K) ? WINDOW_64K : WINDOW_DEFAULT;
  stage = (unsigned char *)malloc (STAGE_MAX);
  desc = (unsigned char *)malloc (DESC_MAX);
  tables = (DecTable *)malloc (DEC_TABLE_COUNT * sizeof (DecTable));
  o = (Output *)calloc (1U, sizeof (Output));

  if (!stage || !desc || !tables || !o)
    {
      goto cleanup;
    }

  (void)memset (tables, 0, DEC_TABLE_COUNT * sizeof (DecTable));
  dec_poison (tables + DEC_POISON_SLOT, 512U);

  o->out = out;
  o->out_mode = hdr[5];
  o->distance_flags
      = ((feature_flags & FEATURE_ALIGNED_DISTANCES)
         ? OPTION_ALIGNED_DISTANCES : 0U)
        | ((feature_flags & FEATURE_AUTO_ALIGNED_DISTANCES)
           ? OPTION_AUTO_ALIGNED_DISTANCES : 0U);
  o->window_n = window_n;
  o->of_alphabet = window_n == WINDOW_64K ? OF_ALPHABET_64K
                                          : OF_ALPHABET_32K;
  o->min_match = (feature_flags & FEATURE_MIN_MATCH_2) ? MIN_MATCH_SHORT
                                                       : MIN_MATCH_DEFAULT;
  o->phased = (feature_flags & FEATURE_PHASED_TABLES) != 0U;
  o->ring = (unsigned short *)calloc (window_n, sizeof (unsigned short));

  if (!o->ring)
    {
      goto cleanup;
    }

  while (done < total)
    {
      unsigned long left = total - done;
      unsigned int remaining
          = (unsigned int)(left > BLOCK_N ? BLOCK_N : left);

      if (!decode_block (in, o, remaining, stage, desc, tables))
        {
          goto cleanup;
        }

      done += o->in_block;
    }

  if (o->out_mode == 9 && o->out_bits)
    {
      if (!output_octet (o, o->out_acc << (8 - o->out_bits)))
        {
          goto cleanup;
        }
    }

  if (!output_flush (o) || fgetc (in) != EOF || ferror (in) || ferror (out))
    {
      goto cleanup;
    }

  okay = 1;

cleanup:
  free (stage);
  free (desc);
  free (tables);

  if (o)
    {
      free (o->ring);
    }

  free (o);

  return okay;
}

/*****************************************************************************/

static int
file_size (FILE *in, unsigned long *size)
{
  long bytes;

  if (fseek (in, 0, SEEK_END) != 0)
    {
      return 0;
    }

  bytes = ftell (in);

  if (bytes < 0 || fseek (in, 0, SEEK_SET) != 0)
    {
      return 0;
    }

  *size = (unsigned long)bytes;

  return 1;
}

/*****************************************************************************/

static int
input_count (FILE *in, unsigned int mode, unsigned long *count)
{
  unsigned long bytes, check;

  if (!file_size (in, &bytes))
    {
      return 0;
    }

  if (mode == 8)
    {
      *count = bytes;

      return 1;
    }

  /*
   * ceil(9*N/8) == bytes.
   * All byte positions aren't valid for N.
   */

  *count = (bytes / 9UL) * 8UL + ((bytes % 9UL) * 8UL) / 9UL;
  check = (*count / 8UL) * 9UL + (((*count % 8UL) * 9UL + 7UL) / 8UL);

  if (check != bytes)
    {
      return 0;
    }

  return 1;
}

/*****************************************************************************/

static unsigned int
default_features (int level)
{
  return level >= 6 ? FEATURE_PHASED_TABLES | FEATURE_MIN_MATCH_2 : 0U;
}

/*****************************************************************************/

int
main (int argc, char **argv)
{
  FILE *in, *out;
  unsigned long count = 0, input_bytes = 0, output_bytes = 0;
  unsigned int mode = 0;
  int level = 5, verbose = 0, level_seen = 0, window_seen = 0, i, okay;
  unsigned int window_n = WINDOW_DEFAULT;
  unsigned int features;
  clock_t start, finish;

  if ((argc == 2 && !strcmp (argv[1], "--memory"))
      || (argc == 2 && !strcmp (argv[1], "--memory-32k"))
      || (argc == 2 && !strcmp (argv[1], "--memory-64k")))
    {
      unsigned int win
          = !strcmp (argv[1], "--memory-64k") ? WINDOW_64K : WINDOW_DEFAULT;
      unsigned long enc
          = (unsigned long)sizeof (Encoder)
            + (unsigned long)(win + BLOCK_N) * sizeof (unsigned short)
            + (unsigned long)SEQ_MAX * sizeof (Sequence)
            + (unsigned long)HASH_N * sizeof (int)
            + (unsigned long)win * sizeof (int) + STAGE_MAX + DESC_MAX
            + (unsigned long)sizeof (AnsTable)
            + (unsigned long)PHASE_GROUPS_MAX * sizeof (AnsTable);
      unsigned long dec = (unsigned long)sizeof (Output)
                          + (unsigned long)win * sizeof (unsigned short)
                          + STAGE_MAX + DESC_MAX
                          + (unsigned long)DEC_TABLE_COUNT
                            * sizeof (DecTable)
                          + (unsigned long)ENTROPY_STREAM_COUNT
                            * sizeof (Stream);
      (void)printf (
              "window: %u symbols\n", win);
      (void)printf (
              "static CRC lookup table: %lu bytes\n",
              (unsigned long)sizeof (crc_table));
      (void)printf (
              "encoder allocated state: %lu bytes"
              " (excludes helper stack temporaries)\n", enc);
      (void)printf (
              "decoder allocated state: %lu bytes"
              " (plus small stack temporaries)\n", dec);

      return 0;
    }

  if (argc < 4 || argc > 14
      || (strcmp (argv[1], "c8") != 0 && strcmp (argv[1], "c9") != 0
          && strcmp (argv[1], "d") != 0))
    {
      (void)fprintf (stderr,
              "LZ9ANS reference implementation " LZ9VER "\n\n");
      (void)fprintf (stderr,
              "Usage:\n"
              "  lz9ans <c8|c9> <input> <output> [-1..-9] [-v]"
              " [--window=N]\n");
      (void)fprintf (stderr,
              "  lz9ans d <input> <output> [-v]\n\n");
      (void)fprintf (stderr,
              "  c8: 8-bit input;  c9: mxload packed 9-bit input; ");
      (void)fprintf (stderr,
              " default level -5\n\n");
      (void)fprintf (stderr,
              "  -v:       "
              "           report file size and compression ratio details\n");
      (void)fprintf (stderr,
              "  --window=<32k|64k>:"
              "  use 32k or 64k symbol history window (default: 32k)\n");
      return 2;
    }

  if (argv[1][0] == 'c')
    {
      mode = (unsigned int)(argv[1][1] - '0');
    }

  for (i = 4; i < argc; ++i)
    {
      if (!strcmp (argv[i], "-v"))
        {
          if (verbose)
            {
              (void)fprintf (stderr, "Duplicate -v option.\n");

              return 2;
            }

          verbose = 1;
        }
      else if (argv[1][0] == 'c'
               && (!strcmp (argv[i], "--window=64k")
                   || !strcmp (argv[i], "--window=32k")))
        {
          if (window_seen)
            {
              (void)fprintf (stderr, "Specify at most one --window option.\n");

              return 2;
            }

          window_seen = 1;
          window_n = !strcmp (argv[i], "--window=64k") ? WINDOW_64K
                                                       : WINDOW_DEFAULT;
        }
      else if (argv[1][0] == 'c' && argv[i][0] == '-' && argv[i][1] >= '1'
               && argv[i][1] <= '9' && argv[i][2] == '\0')
        {
          if (level_seen)
            {
              (void)fprintf (stderr,
                      "Specify at most one compression level.\n");

              return 2;
            }

          level = argv[i][1] - '0';
          level_seen = 1;
        }
      else
        {
          (void)fprintf (stderr, "Invalid option: %s\n", argv[i]);

          return 2;
        }
    }

  /*LINTED E_CONSTANT_CONDITION*/
  if (sizeof (unsigned short) != 2 || sizeof (int) != 4 || CHAR_BIT != 8)
    {
      (void)fprintf (stderr,
              "This reference implementation requires 8-bit bytes, 16-bit"
              " short, 32-bit int.\n");

      return 2;
    }

  if (!strcmp (argv[2], argv[3]))
    {
      (void)fprintf (stderr, "Input and output paths must differ.\n");

      return 2;
    }

  in = fopen (argv[2], "rb");

  if (!in)
    {
      perror (argv[2]);

      return 2;
    }

  if (verbose && !file_size (in, &input_bytes))
    {
      (void)fprintf (stderr,
              "Cannot determine input size for -v reporting.\n");
      (void)fclose (in);

      return 2;
    }

  if (mode && (!input_count (in, mode, &count) || count > 0xffffffffUL))
    {
      (void)fprintf (stderr, "Invalid input size or unseekable input.\n");
      (void)fclose (in);

      return 2;
    }

  out = fopen (argv[3], "wb");

  if (!out)
    {
      perror (argv[3]);
      (void)fclose (in);

      return 2;
    }

  features = default_features (level);
  init_crc ();
  init_costs ();
  start = clock ();

  if (mode)
    {
      okay = encode_file (in, out, mode, count, level, window_n, features);
    }
  else
    {
      okay = decode_file (in, out);
    }

  finish = clock ();

  if (verbose)
    {
      long bytes;

      /*LINTED E_RET_INT_IMPLICITLY*/
      if (fflush (out) != 0 || fseek (out, 0, SEEK_END) != 0
          || (bytes = ftell (out)) < 0)
        {
          okay = 0;
        }
      else
        {
          output_bytes = (unsigned long)bytes;
        }
    }

  if (fclose (out) != 0)
    {
      okay = 0;
    }

  if (fclose (in) != 0)
    {
      okay = 0;
    }

  if (!okay)
    {
      (void)fprintf (stderr,
                     "LZ9ANS: invalid stream, I/O error, or capacity limit\n");

      return 1;
    }

  (void)fprintf (stderr, "LZ9ANS %s: %.3f CPU seconds\n",
                 mode ? "compress" : "decompress",
                 (double)(finish - start) / (double)CLOCKS_PER_SEC);

  if (verbose)
    {
      if (mode)
        {
          (void)fprintf (stderr, "Input size: %lu bytes\n", input_bytes);
          (void)fprintf (stderr, "Output size: %lu bytes\n", output_bytes);

          if (input_bytes)
            {
              (void)fprintf (stderr,
                      "Compression ratio (output/input): %.2f%%\n",
                      100.0 * (double)output_bytes / (double)input_bytes);
              (void)fprintf (stderr,
                      "Compression factor (input/output): %.3f:1\n",
                      output_bytes ? (double)input_bytes / (double)output_bytes
                                   : 0.0);
            }
          else
            {
              (void)fprintf (stderr, "Compression ratio: n/a (empty input)\n");
            }
        }
      else
        {
          (void)fprintf (stderr, "Input size: %lu bytes\n", input_bytes);
          (void)fprintf (stderr, "Output size: %lu bytes\n", output_bytes);

          if (input_bytes)
            {
              (void)fprintf (stderr,
                      "Output/input size ratio: %.2f%%\n",
                      100.0 * (double)output_bytes / (double)input_bytes);
            }
          else
            {
              (void)fprintf (stderr,
                      "Output/input size ratio: n/a (empty input)\n");
            }
        }
    }

  return 0;
}

/*****************************************************************************/

/*
 * Local Variables:
 * mode: c
 * tab-width: 2
 * fill-column: 80
 * eval: (setq-local display-fill-column-indicator-column 80)
 * eval: (display-fill-column-indicator-mode 1)
 * End:
 */

/*****************************************************************************/
/* vim: set ft=c ts=2 sw=2 tw=0 ai expandtab cc=80 : */
/*****************************************************************************/
