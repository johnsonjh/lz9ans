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

#undef LZ9VER
#define LZ9VER "v0.3.2"

/*****************************************************************************/

#define BLOCK_N 32768U
#define WINDOW_DEFAULT 32768U
#define WINDOW_64K 65536U
#define FEATURE_CONTEXT_LITERALS 1U
#define FEATURE_WINDOW_64K 2U
#define FEATURE_DISTANCE_CONTEXT 4U
#define FEATURE_EXTRA_ANS 8U
#define DISTANCE_CONTEXT_THRESHOLD 19U
#define HASH_N 8192U
#define HASH_MASK (HASH_N - 1U)
#define TABLE_LOG 10
#define TABLE_N (1U << TABLE_LOG)
#define SEQ_MAX (BLOCK_N / 3U + 1U)
#define STAGE_MAX 81920U
#define DESC_MAX 8192U
#define BHEAD_V5_N 56U
#define FHEAD_N 20

/*****************************************************************************/

#define MODE_EMPTY 0
#define MODE_RAW 1
#define MODE_RLE 2
#define MODE_TANS 3
#define BLOCK_STORED 0
#define BLOCK_LZ 1

/*****************************************************************************/

#define STREAM_LIT 0
#define STREAM_LL 1
#define STREAM_ML 2
#define STREAM_OF 3
#define STREAM_EXTRA 4
#define STREAM_LIT_HI 5
#define STREAM_LIT_LO 6
#define STREAM_OF_SHORT 7
#define STREAM_OF_LONG 8
#define FORMAT_V3 3U
#define FORMAT_V4 4U
#define OF_ALPHABET_32K 20U
#define OF_ALPHABET_64K 21U
#define COST_SCALE 256U
#define COST_STRIDE 16U

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
  unsigned short dsym[TABLE_N];
  unsigned short dbase[TABLE_N];
  unsigned short estate[TABLE_N];
  unsigned short threshold[512];
  unsigned char dbits[TABLE_N];
  unsigned char highbits[512];
  unsigned short alphabet;
} AnsTable;

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
  AnsTable *table;
} Stream;

/*****************************************************************************/

typedef struct
{
  Stream *stream;
  BitReader *raw;
  unsigned long used;
  int entropy;
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
  unsigned long stream_bits[7];
  unsigned long stream_bytes[7];
  unsigned long desc_len;
  unsigned long stage_len;
  unsigned char modes[7];
  unsigned int extra_index;
  unsigned int format_version;
  unsigned int input_mode;
  int context_literals;
  int distance_context;
  int extra_ans;
  unsigned int window_n;
  unsigned int of_alphabet;
  unsigned int feature_flags;
  unsigned int current_kind;
  unsigned int search_depth;
  int lazy;
  int level;
  int use_cost;
  unsigned int reps[3];
  unsigned short costs[4][512];
  unsigned int prefix[BLOCK_N / COST_STRIDE + 1U];
} Encoder;

/*****************************************************************************/

typedef struct
{
  unsigned short *ring;
  unsigned int format_version;
  int context_literals;
  int distance_context;
  int extra_ans;
  unsigned int window_n;
  unsigned int of_alphabet;
  unsigned int feature_flags;
  unsigned char outbuf[8192];
  unsigned int out_used;
  unsigned long produced;
  unsigned int in_block;
  unsigned long crc;
  unsigned int crc_acc, crc_bits;
  unsigned int out_acc, out_bits;
  int out_mode;
  FILE *out;
  unsigned int reps[3];
} Output;

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
static unsigned int stream_get (Stream *s);
static unsigned int extra_get (ExtraReader *x, unsigned int n);

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

static unsigned long crc_table[256];

/*****************************************************************************/

static void
init_crc (void)
{
  unsigned long i, j;

  for (i = 0; i != 256; ++i)
    {
      unsigned long v = i;

      for (j = 0; j != 8; ++j)
        {
          v = (v & 1UL) ? ((v >> 1) ^ 0xedb88320UL) : (v >> 1);
        }

      crc_table[i] = v;
    }
}

/*****************************************************************************/

static unsigned long
crc_byte (unsigned long crc, unsigned int b)
{
  return (crc >> 8) ^ crc_table[(unsigned int)((crc ^ b) & 255UL)];
}

/*****************************************************************************/

static unsigned long
crc_bytes (const unsigned char *p, unsigned int n)
{
  unsigned int i;
  unsigned long v = 0xffffffffUL;

  for (i = 0; i < n; ++i)
    {
      v = crc_byte (v, p[i]);
    }

  return (v ^ 0xffffffffUL) & 0xffffffffUL;
}

/*****************************************************************************/

static unsigned long
input_crc (const unsigned short *v, unsigned int n)
{
  unsigned int i, pending = 0, nbits = 0;
  unsigned long crc = 0xffffffffUL;

  for (i = 0; i < n; ++i)
    {
      pending = (pending << 9) | v[i];
      nbits += 9;

      while (nbits >= 8)
        {
          nbits -= 8;
          crc = crc_byte (crc, (pending >> nbits) & 255U);
          pending &= (1U << nbits) - 1U;
        }
    }

  if (nbits)
    {
      crc = crc_byte (crc, pending << (8 - nbits));
    }

  return (crc ^ 0xffffffffUL) & 0xffffffffUL;
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

static unsigned int
stream_count_for_features (unsigned int context_literals,
                           unsigned int distance_context)
{
  return 5U + context_literals + distance_context;
}

/*****************************************************************************/

static unsigned int
entropy_count_for_features (unsigned int context_literals,
                            unsigned int distance_context)
{
  return stream_count_for_features (context_literals, distance_context) - 1U;
}

/*****************************************************************************/

static unsigned int
block_header_size (unsigned int context_literals,
                   unsigned int distance_context)
{
  return 28U
         + 4U * stream_count_for_features (context_literals, distance_context);
}

/*****************************************************************************/

static unsigned int
logical_kind_for_wire (unsigned int context_literals,
                       unsigned int distance_context, unsigned int wire)
{
  static const unsigned char base_kind[4]
      = { STREAM_LIT, STREAM_LL, STREAM_ML, STREAM_OF };

  static const unsigned char context_kind[5]
      = { STREAM_LIT_LO, STREAM_LIT_HI, STREAM_LL, STREAM_ML, STREAM_OF };

  static const unsigned char distance_kind[5]
      = { STREAM_LIT, STREAM_LL, STREAM_ML, STREAM_OF_SHORT, STREAM_OF_LONG };

  static const unsigned char both_kind[6]
      = { STREAM_LIT_LO, STREAM_LIT_HI,   STREAM_LL,
          STREAM_ML,     STREAM_OF_SHORT, STREAM_OF_LONG };

  if (context_literals && distance_context)
    {
      return both_kind[wire];
    }

  if (context_literals)
    {
      return context_kind[wire];
    }

  if (distance_context)
    {
      return distance_kind[wire];
    }

  return base_kind[wire];
}

/*****************************************************************************/

static int
split_stream_for_features (unsigned int context_literals,
                           unsigned int distance_context, unsigned int wire)
{
  unsigned int of_index = 3U + (context_literals ? 1U : 0U);

  if (context_literals && wire < 2U)
    {
      return 1;
    }

  if (distance_context && (wire == of_index || wire == of_index + 1U))
    {
      return 1;
    }

  return 0;
}

/*****************************************************************************/

static unsigned int
literal_context_value (unsigned int symbol, unsigned int mode)
{
  return mode == 8U ? ((symbol & 128U) != 0U) : ((symbol & 256U) != 0U);
}

/*****************************************************************************/

static unsigned int
literal_context_at (const Encoder *e, unsigned int pos)
{
  unsigned int previous;

  if (pos > e->window_n)
    {
      previous = e->buffer[pos - 1U];
    }
  else if (pos == e->window_n && e->hlen)
    {
      previous = e->buffer[e->window_n - 1U];
    }
  else
    {
      previous = 0U;
    }

  return literal_context_value (previous, e->input_mode);
}

/*****************************************************************************/

static int
write_exact (FILE *f, const void *p, size_t n)
{
  return fwrite (p, 1, n, f) == n;
}

/*****************************************************************************/

static int
read_exact (FILE *f, void *p, size_t n)
{
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

static void
bw_add (BitWriter *b, unsigned int value, unsigned int bits)
{
  unsigned int remaining = bits;

  if (b->bits > b->capacity_bits
      || (unsigned long)bits > b->capacity_bits - b->bits)
    {
      b->failed = 1;

      return;
    }

  while (remaining)
    {
      unsigned long byte = b->bits >> 3;
      unsigned int occupied = (unsigned int)(b->bits & 7UL);
      unsigned int take = 8U - occupied;
      unsigned int mask, part;

      if (take > remaining)
        {
          take = remaining;
        }

      mask = (1U << take) - 1U;
      part = (value >> (remaining - take)) & mask;

      if (!occupied)
        {
          b->p[byte] = 0;
        }

      b->p[byte] |= (unsigned char)(part << (8U - occupied - take));
      b->bits += take;
      remaining -= take;
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

static unsigned int
br_get (BitReader *b, unsigned int n)
{
  unsigned int v, by, shift;
  unsigned long start, window;

  if (n > 16 || b->position > b->bits
      || (unsigned long)n > b->bits - b->position)
    {
      b->failed = 1;

      return 0;
    }

  if (!n)
    {
      return 0;
    }

  start = b->backwards ? (b->bits - b->position - n) : b->position;
  b->position += n;
  by = (unsigned int)(start >> 3);
  window = (unsigned long)b->p[by] << 16;

  if ((unsigned long)by + 1UL < b->size)
    {
      window |= (unsigned long)b->p[by + 1] << 8;
    }

  if ((unsigned long)by + 2UL < b->size)
    {
      window |= (unsigned long)b->p[by + 2];
    }

  shift = 24U - (unsigned int)(start & 7UL) - n;
  v = (unsigned int)((window >> shift) & ((1UL << n) - 1UL));

  return v;
}

/*****************************************************************************/

static unsigned int
floor_log2 (unsigned int n)
{
  unsigned int v = 0;

  while (n >>= 1)
    {
      ++v;
    }

  return v;
}

/*****************************************************************************/

/*
 * Deterministic tANS. Encoder inverse of the standard FSE-style decoder:
 * next_count = freq[s] ... 2*freq[s]-1;
 * nb = TABLE_LOG - floor_log2(next_count);
 * next_state = (next_count << nb)-TABLE_N + read_bits(nb).
 */

static int
ans_build (AnsTable *t, const unsigned short *freq, unsigned int alphabet)
{
  unsigned int s, x, pos = 0, cumulative = 0, step = 643U;
  unsigned short seen[512];

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
          t->dsym[pos] = (unsigned short)s;
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

      s = t->dsym[x];
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

      t->dbits[x] = (unsigned char)bits;
      t->dbase[x] = (unsigned short)base;
      t->estate[t->cum[s] + count - f] = (unsigned short)x;
    }

  return 1;
}

/*****************************************************************************/

static void
ans_put (AnsTable *t, BitWriter *b, unsigned int sym, unsigned short *state)
{
  unsigned int f = t->freq[sym], y = TABLE_N + *state;
  unsigned int nb = t->highbits[sym], count;

  if (y < t->threshold[sym])
    {
      --nb;
    }

  count = y >> nb;
  bw_add (b, y & ((1U << nb) - 1U), nb);
  *state = t->estate[t->cum[sym] + count - f];
}

/*****************************************************************************/

static unsigned int
ans_get (Stream *s)
{
  unsigned int old = s->state, sym, nb;

  if (old >= TABLE_N)
    {
      s->br.failed = 1;

      return 0;
    }

  sym = s->table->dsym[old];
  nb = s->table->dbits[old];
  s->state = (unsigned short)(s->table->dbase[old] + br_get (&s->br, nb));

  return sym;
}

/*****************************************************************************/

static int
norm_counts (const unsigned int *counts, unsigned int alphabet,
             unsigned int total, unsigned short *freq)
{
  unsigned int i, active = 0, remaining, used = 0;
  unsigned long residue[512];

  for (i = 0; i < alphabet; ++i)
    {
      if (counts[i])
        {
          ++active;
        }
    }

  if (active < 2 || active > TABLE_N || !total)
    {
      return 0;
    }

  remaining = TABLE_N - active;

  for (i = 0; i < alphabet; ++i)
    {
      if (counts[i])
        {
          unsigned long q
              = (unsigned long)counts[i] * (unsigned long)remaining;

          freq[i] = (unsigned short)(1U + (unsigned int)(q / total));
          residue[i] = q % total;
          used += freq[i];
        }
      else
        {
          freq[i] = 0;
          residue[i] = 0;
        }
    }

  while (used < TABLE_N)
    {
      unsigned int best = 0;
      unsigned long maximum = 0;

      for (i = 0; i < alphabet; ++i)
        {
          if (counts[i]
              && (residue[i] > maximum
                  || (best > i && residue[i] == maximum)))
            {
              maximum = residue[i];
              best = i;
            }
        }

      ++freq[best];
      residue[best] = 0;
      ++used;
    }

  return used == TABLE_N;
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

static unsigned int
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

/*
 * Offset codes 0..2 repeat, 3 RUN, 4..18 explicit distance buckets,
 * 19 the exact 32768 endpoint. Repeat-slot tag is stored separately.
 */

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

static unsigned int
classify_offset (const Encoder *e, unsigned int d)
{
  unsigned int i;

#ifndef LZ9ANS_DISABLE_RUN_CODE
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
#endif /* ifndef LZ9ANS_DISABLE_RUN_CODE */

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

static unsigned int
distance_code (unsigned int d, unsigned int tag, unsigned int window_n,
               unsigned int *extra, unsigned int *bits)
{
  unsigned int k;

#ifndef LZ9ANS_DISABLE_RUN_CODE
  if (d == 1 && !tag)
    {
      *extra = 0;
      *bits = 0;

      return 3;
    }
#endif /* ifndef LZ9ANS_DISABLE_RUN_CODE */

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

static unsigned int
extra_get (ExtraReader *x, unsigned int n)
{
  unsigned int v;

  if (!n)
    {
      return 0U;
    }

  if (!x->entropy)
    {
      v = br_get (x->raw, n);
      x->used += n;

      if (x->raw->failed)
        {
          x->failed = 1;
        }

      return v;
    }

  v = 0U;

  while (n--)
    {
      unsigned int bit = stream_get (x->stream);

      if (x->stream->br.failed)
        {
          x->failed = 1;
        }

      v = (v << 1) | bit;
      ++x->used;
    }

  return v;
}

/*****************************************************************************/

static unsigned int
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

static unsigned int
decode_dist (unsigned int code, ExtraReader *extra, unsigned int *rep,
             unsigned int window_n, unsigned int of_alphabet)
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
      d = 32769U + extra_get (extra, 15U);

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
  d = (1U << bucket) + extra_get (extra, bucket);

  if (d > window_n)
    {
      extra->failed = 1;

      return 0;
    }

  promote_rep (rep, d);

  return d;
}

/*****************************************************************************/

/*
 * Forward declarations used by the cost estimator below
 */

/*****************************************************************************/

static unsigned int
hash3 (const unsigned short *p)
{
  unsigned long x = ((unsigned long)p[0] * 2654435761UL)
                    ^ ((unsigned long)p[1] * 2246822519UL)
                    ^ ((unsigned long)p[2] * 3266489917UL);

  x &= 0xffffffffUL; /* same hash with 32-bit and 64-bit unsigned long */
  x ^= x >> 16;

  return (unsigned int)x & HASH_MASK;
}

/*****************************************************************************/

static unsigned int
estimated_code_cost (const Encoder *e, unsigned int kind, unsigned int symbol)
{
  return e->costs[kind][symbol];
}

/*****************************************************************************/

static unsigned long
literal_price (const Encoder *e, unsigned int start, unsigned int end)
{
  unsigned long sum = 0;
  const unsigned short *src = e->buffer + e->window_n;

  while (start < end && (start & (COST_STRIDE - 1U)))
    {
      sum += e->costs[STREAM_LIT][src[start++]];
    }

  if (end - start >= COST_STRIDE)
    {
      unsigned int last = end & ~(COST_STRIDE - 1U);

      sum += (unsigned long)e->prefix[last / COST_STRIDE]
             - e->prefix[start / COST_STRIDE];
      start = last;
    }

  while (start < end)
    {
      sum += e->costs[STREAM_LIT][src[start++]];
    }

  return sum;
}

/*****************************************************************************/

static void
build_cost_prefix (Encoder *e)
{
  unsigned int i, acc = 0;

  e->prefix[0] = 0;

  for (i = 0; i < e->n; ++i)
    {
      acc += e->costs[STREAM_LIT][e->buffer[e->window_n + i]];

      if ((i & (COST_STRIDE - 1U)) == COST_STRIDE - 1U)
        {
          e->prefix[(i + 1U) / COST_STRIDE] = acc;
        }
    }

  if (e->n & (COST_STRIDE - 1U))
    {
      e->prefix[(e->n + COST_STRIDE - 1U) / COST_STRIDE] = acc;
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

static unsigned int
match_price (const Encoder *e, unsigned int ml, unsigned int distance)
{
  unsigned int ex, nb, mc, oc;
  unsigned long price = 0;

  mc = len_code (ml, 3, &ex, &nb);
  price += estimated_code_cost (e, STREAM_ML, mc)
           + (unsigned long)nb * COST_SCALE;

  oc = classify_offset (e, distance);
  price += estimated_code_cost (e, STREAM_OF, oc);

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

static void
find_match (Encoder *e, unsigned int pos, unsigned int end,
            unsigned int pending, unsigned int *length, unsigned int *distance,
            long *gain)
{
  int cand, checks = 0;
  unsigned int best = 0, d = 0;
  long best_gain = 0;
  unsigned int max;
  unsigned long base_ll_price = 0;
  const unsigned short *buf;
  unsigned int p0, p1, p2;

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
  p1 = buf[pos + 1];
  p2 = buf[pos + 2];

  if (e->use_cost)
    {
      base_ll_price = ll_price (e, pending);
    }

  cand = e->head[hash3 (buf + pos)];

  while (cand >= 0 && checks < (int)e->search_depth && (unsigned int)cand < pos
         && pos <= (unsigned int)cand + e->window_n)
    {
      if (!e->use_cost && best >= 3
          && buf[(unsigned int)cand + best] != buf[pos + best])
        {
          cand = e->link[((unsigned int)cand) & (e->window_n - 1U)];
          ++checks;

          continue;
        }

      if (buf[cand] == p0 && buf[cand + 1] == p1 && buf[cand + 2] == p2)
        {
          unsigned int k = 3;

          while (k < max && buf[(unsigned int)cand + k] == buf[pos + k])
            {
              ++k;
            }

          if (e->use_cost)
            {
              unsigned int trial[3], t, trial_count = e->level >= 8 ? 3U : 1U;

              trial[0] = k;
              trial[1] = k > 3 ? k - 1 : k;
              trial[2] = k >= 19 ? 18 : k;

              for (t = 0; t < trial_count; ++t)
                {
                  unsigned int length_here = trial[t];
                  long saving;

                  if (t && length_here == trial[t - 1])
                    {
                      continue;
                    }

                  saving
                      = (long)literal_price (e, pos - e->window_n,
                                             pos - e->window_n + length_here)
                        - (long)(base_ll_price
                                 + match_price (e, length_here,
                                                pos - (unsigned int)cand));

                  if (saving > best_gain
                      || (saving == best_gain && saving > 0
                          && length_here > best))
                    {
                      best_gain = saving;
                      best = length_here;
                      d = pos - (unsigned int)cand;
                    }
                }
            }
          else if (k > best)
            {
              best = k;
              d = pos - (unsigned int)cand;
            }

          if (!e->use_cost && best == max)
            {
              break;
            }
        }

      cand = e->link[((unsigned int)cand) & (e->window_n - 1U)];
      ++checks;
    }

  *length = best;
  *distance = d;
  *gain = best_gain;
}

/*****************************************************************************/

static int
parse_block (Encoder *e)
{
  unsigned int pos = e->window_n, end = e->window_n + e->n;
  unsigned int litstart = pos, k, length, distance, j;
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
      find_match (e, pos, end, pos - litstart, &length, &distance, &gain);

      if (e->lazy && length >= 3 && length < 64 && pos + 3U < end)
        {
          unsigned int nextlen, nextdist;
          long nextgain = 0;

          find_match (e, pos + 1U, end, pos + 1U - litstart, &nextlen,
                      &nextdist, &nextgain);

          if ((e->use_cost && nextgain > gain)
              || (!e->use_cost
                  && (nextlen > length + 1U
                      || (nextlen == length + 1U
                          && nextdist * 2U < distance))))
            {
              hash_insert (e, pos, end);
              ++pos;

              continue;
            }
        }

      if (length >= 3)
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

          for (k = 0; k < length; ++k)
            {
              hash_insert (e, pos + k, end);
            }

          pos += length;
          litstart = pos;
        }
      else
        {
          hash_insert (e, pos, end);
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
  if (kind == STREAM_LIT || kind == STREAM_LIT_LO || kind == STREAM_LIT_HI)
    {
      return 512U;
    }

  if (kind == STREAM_EXTRA)
    {
      return 2U;
    }

  return (kind == STREAM_OF || kind == STREAM_OF_SHORT
          || kind == STREAM_OF_LONG) ? of_alphabet
                                     : 32U;
}

/*****************************************************************************/

static unsigned int
symbol_of_seq (const Encoder *e, unsigned int kind, const Sequence *q)
{
  unsigned int extra, bits;

  if (kind == STREAM_LL)
    {
      return len_code (q->ll, 0, &extra, &bits);
    }

  if (kind == STREAM_ML)
    {
      return len_code (q->ml, 3, &extra, &bits);
    }

  return distance_code (sequence_distance (q), sequence_tag (q), e->window_n,
                        &extra, &bits);
}

/*****************************************************************************/

static int
distance_kind_matches (unsigned int kind, const Sequence *q)
{
  if (kind == STREAM_OF_SHORT)
    {
      return q->ml < DISTANCE_CONTEXT_THRESHOLD;
    }

  if (kind == STREAM_OF_LONG)
    {
      return q->ml >= DISTANCE_CONTEXT_THRESHOLD;
    }

  return 1;
}

/*****************************************************************************/

static int
literal_kind_matches (const Encoder *e, unsigned int kind, unsigned int pos)
{
  if (!e->context_literals)
    {
      return kind != STREAM_LIT_HI && kind != STREAM_LIT_LO;
    }

  if (kind == STREAM_LIT)
    {
      return 1;
    }

  if (kind == STREAM_LIT_LO)
    {
      return literal_context_at (e, pos) == 0U;
    }

  if (kind == STREAM_LIT_HI)
    {
      return literal_context_at (e, pos) == 1U;
    }

  return 0;
}

/*****************************************************************************/

static unsigned int
literal_scan (Encoder *e, unsigned int kind, int count_only, BitWriter *w,
              AnsTable *t, unsigned short *state)
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

              if (literal_kind_matches (e, kind, pos + j))
                {
                  ++e->counts[v];

                  if (v > e->max_literal)
                    {
                      e->max_literal = v;
                    }

                  ++observed;
                }
            }

          pos += e->seq[i].ll + e->seq[i].ml;
        }

      while (pos < e->window_n + e->n)
        {
          v = e->buffer[pos];

          if (literal_kind_matches (e, kind, pos))
            {
              ++e->counts[v];

              if (v > e->max_literal)
                {
                  e->max_literal = v;
                }

              ++observed;
            }

          ++pos;
        }
    }
  else if (t && state && w)
    {
      pos = e->window_n + e->n;

      for (j = 0; j < e->last_lit; ++j)
        {
          unsigned int at = pos - 1U;

          if (literal_kind_matches (e, kind, at))
            {
              ans_put (t, w, e->buffer[at], state);
            }

          --pos;
        }

      for (i = e->sn; i; --i)
        {
          pos -= e->seq[i - 1].ml;

          for (j = 0; j < e->seq[i - 1].ll; ++j)
            {
              unsigned int at = pos - 1U;

              if (literal_kind_matches (e, kind, at))
                {
                  ans_put (t, w, e->buffer[at], state);
                }

              --pos;
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
              if (literal_kind_matches (e, kind, pos + j))
                {
                  bw_add (w, e->buffer[pos + j], e->current_kind);
                }
            }

          pos += e->seq[i].ll + e->seq[i].ml;
        }

      while (pos < e->window_n + e->n)
        {
          if (literal_kind_matches (e, kind, pos))
            {
              bw_add (w, e->buffer[pos], e->current_kind);
            }

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

  if (kind == STREAM_LIT || kind == STREAM_LIT_LO || kind == STREAM_LIT_HI)
    {
      return literal_scan (e, kind, 1, (BitWriter *)0, (AnsTable *)0,
                           (unsigned short *)0);
    }

  for (i = 0; i < e->sn; ++i)
    {
      if (distance_kind_matches (kind, &e->seq[i]))
        {
          ++e->counts[symbol_of_seq (e, kind, &e->seq[i])];
          ++total;
        }
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

  if (kind == STREAM_LIT || kind == STREAM_LIT_LO || kind == STREAM_LIT_HI)
    {
      e->current_kind = width;
      (void)literal_scan (e, kind, 0, w, t, &state);
    }
  else if (t)
    {
      for (i = e->sn; i; --i)
        {
          if (distance_kind_matches (kind, &e->seq[i - 1]))
            {
              ans_put (t, w, symbol_of_seq (e, kind, &e->seq[i - 1]), &state);
            }
        }
    }
  else
    {
      for (i = 0; i < e->sn; ++i)
        {
          if (distance_kind_matches (kind, &e->seq[i]))
            {
              bw_add (w, symbol_of_seq (e, kind, &e->seq[i]), width);
            }
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
  if (kind == STREAM_LIT || kind == STREAM_LIT_LO || kind == STREAM_LIT_HI)
    {
      return maxlit < 128 ? 7U : (maxlit < 256 ? 8U : 9U);
    }

  if (kind == STREAM_EXTRA)
    {
      return 1U;
    }

  return 5U;
}

/*****************************************************************************/

static unsigned int
entropy_cost (unsigned int frequency)
{
  double bits, scaled;

  if (!frequency)
    {
      return 16U * COST_SCALE;
    }

  bits = (log ((double)TABLE_N) - log ((double)frequency)) / log (2.0);
  scaled = bits * (double)COST_SCALE + 0.5;

  if (scaled < 0.0)
    {
      return 0;
    }

  if (scaled > 65535.0)
    {
      return 65535U;
    }

  return (unsigned int)scaled;
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

static int
encode_one_stream (Encoder *e, unsigned int kind, unsigned int wire)
{
  unsigned int i, total, used = 0, single = 0,
                         alphabet = stream_alphabet (kind, e->of_alphabet);
  unsigned int width, mode = MODE_RAW, stream_max = 0;
  unsigned short freq[512];
  unsigned long start = e->stage_len, dbegin = e->desc_len;
  unsigned long ansbits = 0;
  BitWriter w;

  total = count_stream (e, kind);

  for (i = 0; i < alphabet; ++i)
    {
      if (e->counts[i] && i > stream_max)
        {
          stream_max = i;
        }
    }

  e->stream_bits[wire] = 0;
  e->stream_bytes[wire] = 0;

  if (!total)
    {
      mode = MODE_EMPTY;
    }
  else
    {
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
      if (norm_counts (e->counts, alphabet, total, freq)
          && ans_build (e->table, freq, alphabet))
        {
          bw_init (&w, e->stage + start, STAGE_MAX - start);
          encode_values (e, kind, &w, e->table, width);

          if (!w.failed)
            {
              unsigned long pairbytes = 0, prev = 0, desc_cost;

              for (i = 0; i < alphabet; ++i)
                {
                  if (freq[i])
                    {
                      unsigned int gap = i - (unsigned int)prev;

                      pairbytes += (gap >= 128 ? 2UL : 1UL)
                                   + (freq[i] >= 128 ? 2UL : 1UL);
                      prev = i + 1U;
                    }
                }

              desc_cost = 1UL + (used >= 128 ? 2UL : 1UL) + pairbytes;
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
      if (e->desc_len >= DESC_MAX)
        {
          return 0;
        }

      e->desc[e->desc_len++] = MODE_TANS;

      if (!put_var (e->desc, &e->desc_len, DESC_MAX, used))
        {
          return 0;
        }

      {
        unsigned int prev = 0;

        for (i = 0; i < alphabet; ++i)
          {
            if (freq[i])
              {
                if (!put_var (e->desc, &e->desc_len, DESC_MAX, i - prev)
                    || !put_var (e->desc, &e->desc_len, DESC_MAX, freq[i]))
                  {
                    return 0;
                  }

                prev = i + 1U;
              }
          }
      }

      e->stream_bits[wire] = ansbits;
      e->stream_bytes[wire] = (ansbits + 7UL) / 8UL;
      e->stage_len += e->stream_bytes[wire];
    }
  else
    {
      if (e->desc_len + 2 > DESC_MAX)
        {
          return 0;
        }

      e->desc[e->desc_len++] = MODE_RAW;
      e->desc[e->desc_len++] = (unsigned char)width;
      bw_init (&w, e->stage + start, STAGE_MAX - start);
      encode_values (e, kind, &w, (AnsTable *)0, width);

      if (w.failed)
        {
          return 0;
        }

      e->stream_bits[wire] = w.bits;
      e->stream_bytes[wire] = (w.bits + 7UL) / 8UL;
      e->stage_len += e->stream_bytes[wire];
    }

  e->modes[wire] = (unsigned char)mode;
  (void)dbegin;

  return 1;
}

/*****************************************************************************/

static void
extra_parts (const Encoder *e, const Sequence *q, unsigned int values[3],
             unsigned int widths[3])
{
  unsigned int code;

  (void)len_code (q->ll, 0, &values[0], &widths[0]);
  (void)len_code (q->ml, 3, &values[1], &widths[1]);
  code = distance_code (sequence_distance (q), sequence_tag (q), e->window_n,
                        &values[2], &widths[2]);
  (void)code;
}

/*****************************************************************************/

static unsigned long
count_extra_bits (Encoder *e, unsigned int counts[2])
{
  unsigned int i, j, part, values[3], widths[3];
  unsigned long total = 0;

  counts[0] = counts[1] = 0;

  for (i = 0; i < e->sn; ++i)
    {
      extra_parts (e, &e->seq[i], values, widths);

      for (part = 0; part < 3; ++part)
        {
          for (j = widths[part]; j; --j)
            {
              unsigned int bit = (values[part] >> (j - 1U)) & 1U;

              ++counts[bit];
              ++total;
            }
        }
    }

  return total;
}

/*****************************************************************************/

static void
write_extra_raw (Encoder *e, BitWriter *w)
{
  unsigned int i, part, values[3], widths[3];

  for (i = 0; i < e->sn; ++i)
    {
      extra_parts (e, &e->seq[i], values, widths);

      for (part = 0; part < 3; ++part)
        {
          bw_add (w, values[part], widths[part]);
        }
    }
}

/*****************************************************************************/

static void
write_extra_tans (Encoder *e, BitWriter *w, AnsTable *t, unsigned short *state)
{
  unsigned int i, j, part, values[3], widths[3];

  for (i = e->sn; i; --i)
    {
      extra_parts (e, &e->seq[i - 1], values, widths);

      for (part = 3; part; --part)
        {
          unsigned int p = part - 1U;

          for (j = 0; j < widths[p]; ++j)
            {
              ans_put (t, w, (values[p] >> j) & 1U, state);
            }
        }
    }
}

/*****************************************************************************/

static int
encode_extras (Encoder *e)
{
  BitWriter w;

  if (!e->extra_ans)
    {
      unsigned int i, nb, value;

      if (e->stage_len > STAGE_MAX)
        {
          return 0;
        }

      bw_init (&w, e->stage + e->stage_len, STAGE_MAX - e->stage_len);

      for (i = 0; i < e->sn; ++i)
        {
          (void)len_code (e->seq[i].ll, 0, &value, &nb);
          bw_add (&w, value, nb);

          (void)len_code (e->seq[i].ml, 3, &value, &nb);
          bw_add (&w, value, nb);

          (void)distance_code (sequence_distance (&e->seq[i]),
                               sequence_tag (&e->seq[i]), e->window_n, &value,
                               &nb);

          bw_add (&w, value, nb);
        }

      if (w.failed)
        {
          return 0;
        }

      e->stream_bits[e->extra_index] = w.bits;
      e->stream_bytes[e->extra_index] = (w.bits + 7UL) / 8UL;
      e->stage_len += e->stream_bytes[e->extra_index];

      return 1;
    }

  {
    unsigned int counts[2], used = 2U;
    unsigned short freq[512];
    unsigned long start = e->stage_len, desc_start = e->desc_len, raw_bits;
    unsigned long raw_bytes, tans_bytes, tans_desc;
    unsigned short state = 0;

    raw_bits = count_extra_bits (e, counts);

    if (!raw_bits)
      {
        if (e->desc_len >= DESC_MAX)
          {
            return 0;
          }

        e->desc[e->desc_len++] = MODE_EMPTY;
        e->stream_bits[e->extra_index] = 0;
        e->stream_bytes[e->extra_index] = 0;
        e->modes[e->extra_index] = MODE_EMPTY;

        return 1;
      }

    raw_bytes = (raw_bits + 7UL) / 8UL;

    if (!counts[0] || !counts[1])
      {
        if (e->desc_len >= DESC_MAX)
          {
            return 0;
          }

        e->desc[e->desc_len++] = MODE_RLE;

        if (!put_var (e->desc, &e->desc_len, DESC_MAX, counts[1] ? 1U : 0U))
          {
            return 0;
          }

        e->stream_bits[e->extra_index] = 0;
        e->stream_bytes[e->extra_index] = 0;
        e->modes[e->extra_index] = MODE_RLE;

        return 1;
      }

    (void)memset (e->counts, 0, sizeof (e->counts));
    e->counts[0] = counts[0];
    e->counts[1] = counts[1];

    if (!norm_counts (e->counts, 2U, counts[0] + counts[1], freq)
        || !ans_build (e->table, freq, 2U))
      {
        return 0;
      }

    bw_init (&w, e->stage + start, STAGE_MAX - start);
    write_extra_tans (e, &w, e->table, &state);
    bw_add (&w, state, TABLE_LOG);

    if (w.failed)
      {
        return 0;
      }

    tans_bytes = (w.bits + 7UL) / 8UL;

    if (e->desc_len >= DESC_MAX)
      {
        return 0;
      }

    e->desc[e->desc_len++] = MODE_TANS;

    if (!put_var (e->desc, &e->desc_len, DESC_MAX, used))
      {
        return 0;
      }

    {
      unsigned int prev = 0, i;

      for (i = 0; i < 2U; ++i)
        {
          if (freq[i])
            {
              if (!put_var (e->desc, &e->desc_len, DESC_MAX, i - prev)
                  || !put_var (e->desc, &e->desc_len, DESC_MAX, freq[i]))
                {
                  return 0;
                }

              prev = i + 1U;
            }
        }
    }

    tans_desc = e->desc_len - desc_start;

    if (tans_desc + tans_bytes >= 2UL + raw_bytes)
      {
        e->desc_len = desc_start;

        if (e->desc_len + 2UL > DESC_MAX)
          {
            return 0;
          }

        e->desc[e->desc_len++] = MODE_RAW;
        e->desc[e->desc_len++] = 1U;
        bw_init (&w, e->stage + start, STAGE_MAX - start);
        write_extra_raw (e, &w);

        if (w.failed)
          {
            return 0;
          }

        e->stream_bits[e->extra_index] = w.bits;
        e->stream_bytes[e->extra_index] = (w.bits + 7UL) / 8UL;
        e->stage_len = start + e->stream_bytes[e->extra_index];
        e->modes[e->extra_index] = MODE_RAW;
      }
    else
      {
        e->stream_bits[e->extra_index] = w.bits;
        e->stream_bytes[e->extra_index] = tans_bytes;
        e->stage_len = start + tans_bytes;
        e->modes[e->extra_index] = MODE_TANS;
      }
  }

  return 1;
}

/*****************************************************************************/

static void
make_block_header (unsigned char *hdr, unsigned int version,
                   unsigned int context_literals,
                   unsigned int distance_context, unsigned int type,
                   unsigned int width, unsigned int n, unsigned int q,
                   unsigned int lc, unsigned long dl,
                   const unsigned long *bits, unsigned long crc)
{
  unsigned int i,
      header_n = block_header_size (context_literals, distance_context);
  unsigned int stream_n
      = stream_count_for_features (context_literals, distance_context);

  (void)memset (hdr, 0, header_n);
  (void)memcpy (hdr, "LZ9B", 4);

  hdr[4] = (unsigned char)version;
  hdr[5] = (unsigned char)(type | (width << 2));

  store16 (hdr + 6, header_n);
  store32 (hdr + 8, n);
  store32 (hdr + 12, q);
  store32 (hdr + 16, lc);
  store32 (hdr + 20, dl);

  for (i = 0; i < stream_n; ++i)
    {
      store32 (hdr + 24 + 4 * i, bits[i]);
    }

  store32 (hdr + 24 + 4 * stream_n, crc);
}

/*****************************************************************************/

static int
write_stored (FILE *out, Encoder *e, unsigned long crc)
{
  unsigned int i, width = 7;
  unsigned long bits[7], nbytes;
  unsigned char h[BHEAD_V5_N];
  BitWriter bw;

  for (i = 0; i < e->n; ++i)
    {
      if (e->buffer[e->window_n + i] >= 256)
        {
          width = 9;

          break;
        }

      if (e->buffer[e->window_n + i] >= 128)
        {
          width = 8;
        }
    }

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

  for (i = 0; i < 7; ++i)
    {
      bits[i] = 0;
    }

  bits[0] = bw.bits;
  make_block_header (h, e->format_version, (unsigned int)e->context_literals,
                     (unsigned int)e->distance_context, BLOCK_STORED, width,
                     e->n, 0, e->n, 0, bits, crc);

  return write_exact (out, h,
                      block_header_size ((unsigned int)e->context_literals,
                                         (unsigned int)e->distance_context))
         && write_exact (out, e->stage, (size_t)nbytes);
}

/*****************************************************************************/

static int
encode_block (FILE *out, Encoder *e)
{
  unsigned long crc = input_crc (e->buffer + e->window_n, e->n);
  unsigned int i, width = 7, wire = 0;
  int compressed_ok = 1;
  unsigned long stored, compressed;
  unsigned char h[BHEAD_V5_N];
  unsigned int saved_reps[3];

  (void)memcpy (saved_reps, e->reps, sizeof (saved_reps));
  e->use_cost = 0;

  if (!parse_block (e))
    {
      return 0;
    }

  if (e->level >= 4)
    {
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

  e->desc_len = 0;
  e->stage_len = 0;

  if (e->context_literals)
    {
      if (!encode_one_stream (e, STREAM_LIT_LO, wire++))
        {
          compressed_ok = 0;
        }

      if (compressed_ok && !encode_one_stream (e, STREAM_LIT_HI, wire++))
        {
          compressed_ok = 0;
        }
    }
  else
    {
      if (!encode_one_stream (e, STREAM_LIT, wire++))
        {
          compressed_ok = 0;
        }
    }

  if (compressed_ok && !encode_one_stream (e, STREAM_LL, wire++))
    {
      compressed_ok = 0;
    }

  if (compressed_ok && !encode_one_stream (e, STREAM_ML, wire++))
    {
      compressed_ok = 0;
    }

  if (compressed_ok && e->distance_context)
    {
      if (!encode_one_stream (e, STREAM_OF_SHORT, wire++))
        {
          compressed_ok = 0;
        }

      if (compressed_ok && !encode_one_stream (e, STREAM_OF_LONG, wire++))
        {
          compressed_ok = 0;
        }
    }
  else if (compressed_ok)
    {
      if (!encode_one_stream (e, STREAM_OF, wire++))
        {
          compressed_ok = 0;
        }
    }

  e->extra_index = wire;

  if (compressed_ok && !encode_extras (e))
    {
      compressed_ok = 0;
    }

  for (i = 0; i < e->n; ++i)
    {
      if (e->buffer[e->window_n + i] >= 256)
        {
          width = 9;

          break;
        }

      if (e->buffer[e->window_n + i] >= 128)
        {
          width = 8;
        }
    }

  stored = ((unsigned long)e->n * width + 7UL) / 8UL;
  compressed = e->desc_len + e->stage_len;

  if (!compressed_ok || e->stage_len > STAGE_MAX || e->desc_len > DESC_MAX
      || compressed >= stored
      || e->stream_bits[e->extra_index] > STAGE_MAX * 8UL)
    {
      (void)memcpy (e->reps, saved_reps, sizeof (saved_reps));

      return write_stored (out, e, crc);
    }

  make_block_header (h, e->format_version, (unsigned int)e->context_literals,
                     (unsigned int)e->distance_context, BLOCK_LZ, 0, e->n,
                     e->sn, e->lit_count, e->desc_len, e->stream_bits, crc);

  return write_exact (out, h,
                      block_header_size ((unsigned int)e->context_literals,
                                         (unsigned int)e->distance_context))
         && write_exact (out, e->desc, (size_t)e->desc_len)
         && write_exact (out, e->stage, (size_t)e->stage_len);
}

/*****************************************************************************/

static int
parse_descriptor (Stream *s, AnsTable *t, unsigned int kind,
                  unsigned int of_alphabet, const unsigned char *p,
                  unsigned long *offset, unsigned long len)
{
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
          && !((kind == STREAM_LIT || kind == STREAM_LIT_LO
                || kind == STREAM_LIT_HI)
               && (s->width == 7 || s->width == 8)))
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
      unsigned int nused = 0, i, sym = 0;
      unsigned short freq[512];
      unsigned long sum = 0;

      (void)memset (freq, 0, sizeof (freq));

      if (!get_var (p, offset, len, &nused) || nused < 2 || nused > alphabet)
        {
          return 0;
        }

      for (i = 0; i < nused; ++i)
        {
          unsigned int gap, f;

          if (!get_var (p, offset, len, &gap) || !get_var (p, offset, len, &f))
            {
              return 0;
            }

          if (gap > alphabet || sym + gap >= alphabet || f == 0 || f > TABLE_N)
            {
              return 0;
            }

          sym += gap;
          freq[sym] = (unsigned short)f;
          sum += f;
          sym += 1;
        }

      if (sum != TABLE_N || !ans_build (t, freq, alphabet))
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
  br_init (&s->br, p, bits, s->mode == MODE_TANS);

  if (s->mode == MODE_TANS)
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

static unsigned int
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

  if (s->mode == MODE_TANS)
    {
      return ans_get (s);
    }

  s->br.failed = 1;

  return 0;
}

/*****************************************************************************/

static int
stream_end (const Stream *s)
{
  if (s->br.failed || s->br.position != s->br.bits)
    {
      return 0;
    }

  if (s->mode == MODE_TANS && s->state != 0)
    {
      return 0;
    }

  return 1;
}

/*****************************************************************************/

static int
extra_end (ExtraReader *x)
{
  if (x->failed)
    {
      return 0;
    }

  if (x->entropy)
    {
      return stream_end (x->stream)
             && ((x->used == 0UL) == (x->stream->mode == MODE_EMPTY));
    }

  return !x->raw->failed && x->raw->position == x->raw->bits;
}

/*****************************************************************************/

static int
output_symbol (Output *o, unsigned int v)
{
  if (v > 511 || (o->out_mode == 8 && v > 255))
    {
      return 0;
    }

  o->ring[o->produced & (o->window_n - 1U)] = (unsigned short)v;
  ++o->produced;
  ++o->in_block;
  o->crc_acc = (o->crc_acc << 9) | v;
  o->crc_bits += 9;

  while (o->crc_bits >= 8)
    {
      o->crc_bits -= 8;
      o->crc = crc_byte (o->crc, (o->crc_acc >> o->crc_bits) & 255U);
      o->crc_acc &= (1U << o->crc_bits) - 1U;
    }

  if (o->out_mode == 8)
    {
      return output_octet (o, v);
    }

  o->out_acc = (o->out_acc << 9) | v;
  o->out_bits += 9;

  while (o->out_bits >= 8)
    {
      o->out_bits -= 8;

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
output_match (Output *o, unsigned int d, unsigned int ml)
{
  unsigned int j, mask = o->window_n - 1U;
  unsigned short *ring = o->ring;
  unsigned long prod = o->produced;

  for (j = 0; j < ml; ++j)
    {
      unsigned int v = ring[(prod - d + j) & mask];

      ring[(prod + j) & mask] = (unsigned short)v;
      o->crc_acc = (o->crc_acc << 9) | v;
      o->crc_bits += 9;

      while (o->crc_bits >= 8)
        {
          o->crc_bits -= 8;
          o->crc = crc_byte (o->crc, (o->crc_acc >> o->crc_bits) & 255U);
          o->crc_acc &= (1U << o->crc_bits) - 1U;
        }

      if (o->out_mode == 8)
        {
          if (!output_octet (o, v))
            {
              return 0;
            }
        }
      else
        {
          o->out_acc = (o->out_acc << 9) | v;
          o->out_bits += 9;

          while (o->out_bits >= 8)
            {
              o->out_bits -= 8;

              if (!output_octet (o, (o->out_acc >> o->out_bits) & 255U))
                {
                  return 0;
                }

              o->out_acc &= (1U << o->out_bits) - 1U;
            }
        }
    }

  o->produced += ml;
  o->in_block += ml;

  return 1;
}

/*****************************************************************************/

static void
block_crc_begin (Output *o)
{
  o->in_block = 0;
  o->crc = 0xffffffffUL;
  o->crc_acc = 0;
  o->crc_bits = 0;
}

/*****************************************************************************/

static int
block_crc_end (Output *o, unsigned long expected)
{
  if (o->crc_bits)
    {
      o->crc = crc_byte (o->crc, o->crc_acc << (8 - o->crc_bits));
    }

  return ((o->crc ^ 0xffffffffUL) & 0xffffffffUL) == expected;
}

/*****************************************************************************/

static unsigned int
output_literal_context (const Output *o)
{
  unsigned int previous;

  if (!o->produced)
    {
      previous = 0U;
    }
  else
    {
      previous = o->ring[(o->produced - 1UL) & (o->window_n - 1U)];
    }

  return literal_context_value (previous, (unsigned int)o->out_mode);
}

/*****************************************************************************/

static int
decode_block (FILE *in, Output *o, unsigned int max_n, unsigned char *stage,
              unsigned char *desc, AnsTable *tables)
{
  unsigned char h[BHEAD_V5_N];
  unsigned long bitlen[7], lengths[7], total = 0, desc_len, expected;
  unsigned int i, n, q, lc, type, width;
  unsigned int header_n, stream_n, entropy_n, extra_index;
  unsigned int context_literals = (unsigned int)o->context_literals;
  unsigned int distance_context = (unsigned int)o->distance_context;
  unsigned int extra_ans = (unsigned int)o->extra_ans;
  unsigned long off = 0, pos = 0;
  Stream s[6], extra_stream;
  BitReader raw_extra;
  ExtraReader extra;

  if (!read_exact (in, h, 8U) || memcmp (h, "LZ9B", 4) != 0
      || h[4] != o->format_version)
    {
      return 0;
    }

  header_n = load16 (h + 6);

  if (header_n != block_header_size (context_literals, distance_context)
      || !read_exact (in, h + 8, header_n - 8U))
    {
      return 0;
    }

  stream_n = stream_count_for_features (context_literals, distance_context);
  entropy_n = entropy_count_for_features (context_literals, distance_context);
  extra_index = stream_n - 1U;
  type = h[5] & 3U;
  width = h[5] >> 2;
  n = (unsigned int)load32 (h + 8);
  q = (unsigned int)load32 (h + 12);
  lc = (unsigned int)load32 (h + 16);
  desc_len = load32 (h + 20);
  expected = load32 (h + 24 + 4U * stream_n);

  if (!n || n > max_n || n > BLOCK_N || q > n / 3U || lc > n
      || desc_len > DESC_MAX || type > BLOCK_LZ)
    {
      return 0;
    }

  for (i = 0; i < stream_n; ++i)
    {
      bitlen[i] = load32 (h + 24 + 4U * i);

      if (bitlen[i] > STAGE_MAX * 8UL)
        {
          return 0;
        }

      lengths[i] = (bitlen[i] + 7UL) / 8UL;

      if (total + lengths[i] > STAGE_MAX)
        {
          return 0;
        }

      total += lengths[i];
    }

  if (total > STAGE_MAX)
    {
      return 0;
    }

  if (!read_exact (in, desc, (size_t)desc_len)
      || !read_exact (in, stage, (size_t)total))
    {
      return 0;
    }

  for (i = 0; i < stream_n; ++i)
    {
      if ((bitlen[i] & 7UL) && lengths[i]
          && (stage[pos + lengths[i] - 1UL]
              & ((1U << (8 - (bitlen[i] & 7UL))) - 1U)))
        {
          return 0;
        }

      pos += lengths[i];
    }

  block_crc_begin (o);

  if (type == BLOCK_STORED)
    {
      BitReader r;

      if (desc_len || q || lc != n || (width != 7 && width != 8 && width != 9)
          || bitlen[0] != (unsigned long)n * width)
        {
          return 0;
        }

      for (i = 1; i < stream_n; ++i)
        {
          if (bitlen[i])
            {
              return 0;
            }
        }

      br_init (&r, stage, bitlen[0], 0);

      for (i = 0; i < n; ++i)
        {
          if (!output_symbol (o, br_get (&r, width)))
            {
              return 0;
            }
        }

      if (r.failed || r.position != r.bits)
        {
          return 0;
        }
    }
  else
    {
      unsigned int produced = 0, literals = 0;
      unsigned int observed_count[6] = { 0U, 0U, 0U, 0U, 0U, 0U };

      if (width != 0 || (q == 0 && lc != n))
        {
          return 0;
        }

      for (i = 0; i < entropy_n; ++i)
        {
          unsigned int kind
              = logical_kind_for_wire (context_literals, distance_context, i);

          if (!parse_descriptor (&s[i], &tables[i], kind, o->of_alphabet, desc,
                                 &off, desc_len))
            {
              return 0;
            }

          if (split_stream_for_features (context_literals, distance_context,
                                         i))
            {
              s[i].count = 0;
            }
          else
            {
              s[i].count = (kind == STREAM_LIT ? lc : q);

              if ((s[i].mode == MODE_EMPTY) != (s[i].count == 0))
                {
                  return 0;
                }
            }
        }

      if (extra_ans
          && !parse_descriptor (&extra_stream, &tables[entropy_n],
                                STREAM_EXTRA, o->of_alphabet, desc, &off,
                                desc_len))
        {
          return 0;
        }

      if (off != desc_len)
        {
          return 0;
        }

      pos = 0;

      for (i = 0; i < entropy_n; ++i)
        {
          if (!stream_begin (&s[i], bitlen[i], stage + pos))
            {
              return 0;
            }

          pos += lengths[i];
        }

      (void)memset (&extra, 0, sizeof (extra));

      if (extra_ans)
        {
          if (!stream_begin (&extra_stream, bitlen[extra_index], stage + pos))
            {
              return 0;
            }

          extra.stream = &extra_stream;
          extra.entropy = 1;
        }
      else
        {
          br_init (&raw_extra, stage + pos, bitlen[extra_index], 0);
          extra.raw = &raw_extra;
          extra.entropy = 0;
        }

      {
        unsigned int ll_index = context_literals ? 2U : 1U;
        unsigned int ml_index = ll_index + 1U;
        unsigned int of_index = ml_index + 1U;

        for (i = 0; i < q; ++i)
          {
            unsigned int j, lr, ml, d, code;

            code = stream_get (&s[ll_index]);
            lr = decode_len (code, 0, &extra);

            if (extra.failed || lr > n - produced || lr > lc - literals)
              {
                return 0;
              }

            for (j = 0; j < lr; ++j)
              {
                unsigned int ctx
                    = context_literals ? output_literal_context (o) : 0U;
                unsigned int lit_wire = context_literals ? ctx : 0U;
                unsigned int v = stream_get (&s[lit_wire]);

                if (v >= 512 || !output_symbol (o, v))
                  {
                    return 0;
                  }

                if (split_stream_for_features (context_literals,
                                               distance_context, lit_wire))
                  {
                    ++observed_count[lit_wire];
                  }
              }

            produced += lr;
            literals += lr;
            code = stream_get (&s[ml_index]);
            ml = decode_len (code, 3, &extra);

            if (distance_context)
              {
                unsigned int of_wire
                    = of_index + (ml >= DISTANCE_CONTEXT_THRESHOLD ? 1U : 0U);

                code = stream_get (&s[of_wire]);
                ++observed_count[of_wire];
              }
            else
              {
                code = stream_get (&s[of_index]);
              }

            d = decode_dist (code, &extra, o->reps, o->window_n,
                             o->of_alphabet);

            if (extra.failed || ml < 3 || ml > n - produced || d < 1
                || d > o->window_n || d > o->produced)
              {
                return 0;
              }

            if (!output_match (o, d, ml))
              {
                return 0;
              }

            produced += ml;
          }
      }

      if (n - produced != lc - literals)
        {
          return 0;
        }

      while (produced < n)
        {
          unsigned int ctx
              = context_literals ? output_literal_context (o) : 0U;
          unsigned int lit_wire = context_literals ? ctx : 0U;
          unsigned int v = stream_get (&s[lit_wire]);

          if (v >= 512 || !output_symbol (o, v))
            {
              return 0;
            }

          if (split_stream_for_features (context_literals, distance_context,
                                         lit_wire))
            {
              ++observed_count[lit_wire];
            }

          ++produced;
          ++literals;
        }

      if (literals != lc || !extra_end (&extra))
        {
          return 0;
        }

      for (i = 0; i < entropy_n; ++i)
        {
          if (split_stream_for_features (context_literals, distance_context, i)
              && ((observed_count[i] == 0U) != (s[i].mode == MODE_EMPTY)))
            {
              return 0;
            }
        }

      for (i = 0; i < entropy_n; ++i)
        {
          if (!stream_end (&s[i]))
            {
              return 0;
            }
        }
    }

  return o->in_block == n && block_crc_end (o, expected);
}

/*****************************************************************************/

static int
allocated_encoder (Encoder *e, unsigned int window_n)
{
  (void)memset (e, 0, sizeof (*e));
  e->window_n = window_n;
  e->of_alphabet = window_n == WINDOW_64K ? OF_ALPHABET_64K : OF_ALPHABET_32K;
  e->buffer = (unsigned short *)malloc ((window_n + BLOCK_N)
                                        * sizeof (unsigned short));
  e->seq = (Sequence *)malloc (SEQ_MAX * sizeof (Sequence));
  e->head = (int *)malloc (HASH_N * sizeof (int));
  e->link = (int *)malloc (window_n * sizeof (int));
  e->stage = (unsigned char *)malloc (STAGE_MAX);
  e->desc = (unsigned char *)malloc (DESC_MAX);
  e->table = (AnsTable *)malloc (sizeof (AnsTable));

  return e->buffer && e->seq && e->head && e->link && e->stage && e->desc
         && e->table;
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
}

/*****************************************************************************/

static int
read_one9 (FILE *in, unsigned int *acc, unsigned int *nbits,
           unsigned int *value)
{
  while (*nbits < 9)
    {
      int c = fgetc (in);

      if (c == EOF)
        {
          return 0;
        }

      *acc = (*acc << 8) | (unsigned int)c;
      *nbits += 8;
    }

  *nbits -= 9;
  *value = (*acc >> *nbits) & 511U;
  *acc &= (1U << *nbits) - 1U;

  return 1;
}

/*****************************************************************************/

static int
encode_file (FILE *in, FILE *out, unsigned int mode, unsigned long elements,
             int level, int context_literals, int distance_context,
             int extra_ans, unsigned int window_n)
{
  static const unsigned char depth[9] = { 1, 2, 4, 8, 12, 16, 24, 32, 48 };
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
  e.context_literals = context_literals;
  e.distance_context = distance_context;
  e.extra_ans = extra_ans;
  e.input_mode = mode;
  e.feature_flags = (context_literals ? FEATURE_CONTEXT_LITERALS : 0U)
                    | (window_n == WINDOW_64K ? FEATURE_WINDOW_64K : 0U)
                    | (distance_context ? FEATURE_DISTANCE_CONTEXT : 0U)
                    | (extra_ans ? FEATURE_EXTRA_ANS : 0U);
  e.format_version = e.feature_flags ? FORMAT_V4 : FORMAT_V3;
  e.extra_index = context_literals ? 5U : 4U;
  (void)memcpy (hdr, "LZ9A", 4);
  hdr[4] = (unsigned char)e.format_version;
  hdr[5] = (unsigned char)mode;
  store16 (hdr + 6, FHEAD_N);
  store32 (hdr + 8, elements);
  store32 (hdr + 12, e.feature_flags);
  store32 (hdr + 16, crc_bytes (hdr, 16));

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

      for (i = 0; i < n; ++i)
        {
          unsigned int v;

          if (mode == 8)
            {
              int c = fgetc (in);

              if (c == EOF)
                {
                  free_encoder (&e);

                  return 0;
                }

              v = (unsigned int)c;
            }
          else if (!read_one9 (in, &acc, &nbits, &v))
            {
              free_encoder (&e);

              return 0;
            }

          e.buffer[e.window_n + i] = (unsigned short)v;
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

  if (fgetc (in) != EOF || ferror (out))
    {
      free_encoder (&e);

      return 0;
    }

  free_encoder (&e);

  return 1;
}

/*****************************************************************************/

static int
decode_file (FILE *in, FILE *out)
{
  unsigned char hdr[FHEAD_N];
  unsigned long total, done = 0;
  unsigned int feature_flags, window_n;
  unsigned char *stage = (unsigned char *)0, *desc = (unsigned char *)0;
  AnsTable *tables = (AnsTable *)0;
  Output *o = (Output *)0;
  int okay = 0;

  if (!read_exact (in, hdr, sizeof (hdr)) || memcmp (hdr, "LZ9A", 4) != 0
      || (hdr[4] != FORMAT_V3 && hdr[4] != FORMAT_V4)
      || (hdr[5] != 8 && hdr[5] != 9) || load16 (hdr + 6) != FHEAD_N
      || load32 (hdr + 16) != crc_bytes (hdr, 16))
    {
      return 0;
    }

  feature_flags = (unsigned int)load32 (hdr + 12);

  if ((feature_flags
       & ~(FEATURE_CONTEXT_LITERALS | FEATURE_WINDOW_64K
           | FEATURE_DISTANCE_CONTEXT | FEATURE_EXTRA_ANS))
          != 0U
      || (hdr[4] == FORMAT_V3 && feature_flags != 0U)
      || (hdr[4] == FORMAT_V4 && feature_flags == 0U))
    {
      return 0;
    }

  window_n
      = (feature_flags & FEATURE_WINDOW_64K) ? WINDOW_64K : WINDOW_DEFAULT;
  total = load32 (hdr + 8);
  stage = (unsigned char *)malloc (STAGE_MAX);
  desc = (unsigned char *)malloc (DESC_MAX);
  tables = (AnsTable *)malloc (7U * sizeof (AnsTable));
  o = (Output *)malloc (sizeof (Output));

  if (!stage || !desc || !tables || !o)
    {
      goto cleanup;
    }

  (void)memset (o, 0, sizeof (*o));
  o->out = out;
  o->out_mode = hdr[5];
  o->format_version = hdr[4];
  o->feature_flags = feature_flags;
  o->context_literals = (feature_flags & FEATURE_CONTEXT_LITERALS) != 0U;
  o->distance_context = (feature_flags & FEATURE_DISTANCE_CONTEXT) != 0U;
  o->extra_ans = (feature_flags & FEATURE_EXTRA_ANS) != 0U;
  o->window_n = window_n;
  o->of_alphabet = window_n == WINDOW_64K ? OF_ALPHABET_64K : OF_ALPHABET_32K;
  o->ring = (unsigned short *)malloc (window_n * sizeof (unsigned short));

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

  if (!output_flush (o) || fgetc (in) != EOF || ferror (out))
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

int
main (int argc, char **argv)
{
  FILE *in, *out;
  unsigned long count = 0, input_bytes = 0, output_bytes = 0;
  unsigned int mode = 0;
  int level = 6, verbose = 0, level_seen = 0, context_literals = 0,
      distance_context = 0, distance_seen = 0, extra_ans = 0, extra_seen = 0,
      window_seen = 0, i, okay;
  unsigned int window_n = WINDOW_DEFAULT;
  clock_t start, finish;

  if ((argc == 2 && !strcmp (argv[1], "--memory"))
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
            + (unsigned long)sizeof (AnsTable);
      unsigned long dec = (unsigned long)sizeof (Output)
                          + (unsigned long)win * sizeof (unsigned short)
                          + STAGE_MAX + DESC_MAX
                          + 7UL * (unsigned long)sizeof (AnsTable)
                          + 7UL * (unsigned long)sizeof (Stream);

      (void)printf (
              "window: %u symbols\n", win);
      (void)printf (
              "encoder allocated state: %lu bytes (excludes helper stack "
              "temporaries)\n", enc);
      (void)printf (
              "decoder allocated state: %lu bytes (plus small stack "
              "temporaries)\n", dec);

      return 0;
    }

  if (argc < 4 || argc > 10
      || (strcmp (argv[1], "c8") != 0 && strcmp (argv[1], "c9") != 0
          && strcmp (argv[1], "c8l") != 0 && strcmp (argv[1], "c9l") != 0
          && strcmp (argv[1], "c8f") != 0 && strcmp (argv[1], "c9f") != 0
          && strcmp (argv[1], "d") != 0))
    {
      (void)fprintf (stderr,
              "LZ9ANS - experimental reference implementation " LZ9VER "\n\n");
      (void)fprintf (stderr,
              "Usage:\n"
              "  lz9ans [c8|c9] <input> <output> [-1..-9] [-v] "
              "[--context-literals]\n"
              "         [--distance-context] [--extra-ans]"
              " [--window=64k]\n");
      (void)fprintf (stderr,
              "  lz9ans d <input> <output> [-v]\n\n");
      (void)fprintf (stderr,
              "  c8: 8-bit input;  c9: mxload packed 9-bit input; ");
      (void)fprintf (stderr,
              " default level -6\n\n"
              "  --context-literals:  enables context literals\n");
      (void)fprintf (stderr,
              "  --window=64k:"
              "        enables the experimental 64K-symbol history\n");
      (void)fprintf (stderr,
              "  --distance-context:"
              "  splits offset models at match length 19\n");
      (void)fprintf (stderr,
              "  --extra-ans:"
              "         entropy-codes bypass bits with a binary tANS model\n");
      (void)fprintf (stderr,
              "  -v:"
              "                  report file sizes and compression ratio\n\n");

      return 2;
    }

  if (argv[1][0] == 'c')
    {
      mode = (unsigned int)(argv[1][1] - '0');

      if (argv[1][2] == 'l')
        {
          level = 7;
        }

      if (argv[1][2] == 'f')
        {
          level = 1;
        }
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
      else if (argv[1][0] == 'c' && !strcmp (argv[i], "--context-literals"))
        {
          if (context_literals)
            {
              (void)fprintf (stderr, "Duplicate --context-literals option.\n");

              return 2;
            }

          context_literals = 1;
        }
      else if (argv[1][0] == 'c' && !strcmp (argv[i], "--distance-context"))
        {
          if (distance_seen)
            {
              (void)fprintf (stderr, "Duplicate --distance-context option.\n");

              return 2;
            }

          distance_seen = 1;
          distance_context = 1;
        }
      else if (argv[1][0] == 'c' && !strcmp (argv[i], "--extra-ans"))
        {
          if (extra_seen)
            {
              (void)fprintf (stderr, "Duplicate --extra-ans option.\n");

              return 2;
            }

          extra_seen = 1;
          extra_ans = 1;
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
              "This host prototype requires 8-bit octets, 16-bit "
              "short, 32-bit int.\n");

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

  init_crc ();
  start = clock ();

  if (mode)
    {
      okay = encode_file (in, out, mode, count, level, context_literals,
                          distance_context, extra_ans, window_n);
    }
  else
    {
      okay = decode_file (in, out);
    }

  finish = clock ();

  if (verbose)
    {
      long bytes;

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
 *
 * vim: set ft=c expandtab ts=2 sw=2 tw=0 ai cc=80 :
 */

/*****************************************************************************/
