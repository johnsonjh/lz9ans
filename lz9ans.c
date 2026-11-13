/*
 * LZ9ANS
 * Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
 * Copyright (c) 2025-2026 The DPS8M Development Team
 * SPDX-License-Identifier: MIT
 * scspell-id: 12a507e4-d24b-11f1-8fe4-80ee73e9b8e7
 */

/*****************************************************************************/

#undef LZ9VER
#define LZ9VER "v0.9.16-dev (2026-11-13)"

/*****************************************************************************/

#if !defined(_WIN32) && !defined(_WIN64) \
    && (defined(__unix__) || defined(__APPLE__) || defined(__linux__)) \
    && !defined(_FILE_OFFSET_BITS)
# define _FILE_OFFSET_BITS 64
#endif /* if !defined(_WIN32) && !defined(_WIN64) \
          && (defined(__unix__) || defined(__APPLE__) || defined(__linux__)) \
          && !defined(_FILE_OFFSET_BITS) */

/*****************************************************************************/

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
# include <pthread.h>
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

#if defined(_WIN32) || defined(_WIN64)
# if !defined(WIN32_LEAN_AND_MEAN)
#  define WIN32_LEAN_AND_MEAN 1
# endif /* if !defined(WIN32_LEAN_AND_MEAN) */
# include <windows.h>
# include <fcntl.h>
# include <io.h>
# include <stdint.h>
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
# include <fcntl.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <unistd.h>
#endif /* if defined(_WIN32) || defined(_WIN64) */

/*****************************************************************************/

#if defined(_WIN32) || defined(_WIN64)
# if defined(_MSC_VER)
typedef unsigned __int64 LZ9FileSize;
typedef __int64 LZ9FileOffset;
# else
__extension__ typedef unsigned long long LZ9FileSize;
__extension__ typedef long long LZ9FileOffset;
# endif /* if defined(_MSC_VER) */
# define LZ9_HAS_64BIT_FILE_SIZE 1
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
typedef off_t LZ9FileSize;
typedef off_t LZ9FileOffset;
# define LZ9_HAS_64BIT_FILE_SIZE 1
#else
typedef unsigned long LZ9FileSize;
typedef long LZ9FileOffset;
#endif /* if defined(_WIN32) || defined(_WIN64) */

/*****************************************************************************/

#if defined(LZ9_HAS_64BIT_FILE_SIZE)
typedef char lz9_file_size_type_must_be_64_bits_compile_time_assertion[
  sizeof (LZ9FileSize) >= 8U ? 1 : -1];
#endif /* if defined(LZ9_HAS_64BIT_FILE_SIZE) */

/*****************************************************************************/

#if !defined(_WIN32) && !defined(_WIN64) \
    && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
typedef char lz9_file_offset_type_must_be_64_bits_compile_time_assertion[
  sizeof (off_t) >= 8U ? 1 : -1];
#endif /* if !defined(_WIN32) && !defined(_WIN64) \
          && (defined(__unix__) || defined(__APPLE__) || defined(__linux__)) */

/*****************************************************************************/

#include "crc32_hw.h"

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
#define MIN_MATCH_DEFAULT 3U
#define MIN_MATCH_SHORT 2U
#define PHASE_GROUPS_MAX 2U
#define PHASE_MIN_SYMBOLS 64U
#define DEC_BASE_TABLE_COUNT 5U
#define DEC_PHASE_TABLE_COUNT 6U
#define DEC_POISON_SLOT 4U
#define STAGE_MAX 81920U
#define TABLE_CACHE_COUNT 10U
#define TABLE_CACHE_BYTES (TABLE_CACHE_COUNT * 512U * sizeof (unsigned short))
#define STAGE_DATA_MAX ((STAGE_MAX - TABLE_CACHE_BYTES - 7UL) & ~7UL)
#define DESC_MAX 8192U
#define BLOCK_PREFIX_N 3U
#define LZ_BLOCK_HEADER_N 23U

#if defined(LZ9ANS_PTHREADS)
# define BLOCK_OUTPUT_MAX (LZ_BLOCK_HEADER_N + DESC_MAX + STAGE_DATA_MAX)
#endif /* if defined(LZ9ANS_PTHREADS) */

#define STORED_BLOCK_HEADER_N 7U
#define FRAME_HEADER_PREFIX_N 13U
#define FRAME_HEADER_MIN_N 17U
#define FRAME_HEADER_MAX_N 4096U
#define FHEAD_N FRAME_HEADER_MIN_N
#define CRC_BUFFER_N 4096U

/*****************************************************************************/

#define THREAD_LIMIT_32BIT 32U
#define THREAD_LIMIT_64BIT 128U

/*****************************************************************************/

static unsigned int
thread_limit_maximum (void)
{
  return sizeof (void *) <= 4U ? THREAD_LIMIT_32BIT : THREAD_LIMIT_64BIT;
}

/*****************************************************************************/

static unsigned int
clamp_thread_limit (unsigned int limit)
{
  unsigned int maximum = thread_limit_maximum ();

  return limit > maximum ? maximum : limit;
}

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
#define BLOCK_END_CONTROL 0x02U
#define BLOCK_END_EXT_CONTROL 0x06U

/*****************************************************************************/

#if !defined(LZ9ANS_PTHREADS) && (defined(__linux) || defined(__linux__) \
    || defined(linux) || defined(__gnu_linux__))
# undef fgetc
# define fgetc(s) fgetc_unlocked(s) /* //-V1059 */
# undef fread
# define fread(p, s, n, f) fread_unlocked(p, s, n, f) /* //-V1059 */
# if !defined(__clang__) || !defined(__GLIBC__) || \
     !defined(_FORTIFY_SOURCE) || (_FORTIFY_SOURCE <= 0)
#  undef fwrite
#  define fwrite(p, s, n, f) fwrite_unlocked(p, s, n, f) /* //-V1059 */
# endif /* if !defined(__clang__) || !defined(__GLIBC__)
           || !defined(_FORTIFY_SOURCE) || (_FORTIFY_SOURCE <= 0) */
# undef fflush
# define fflush(f) fflush_unlocked(f) /* //-V1059 */
# undef feof
# define feof(f) feof_unlocked(f) /* //-V1059 */
# undef ferror
# define ferror(f) ferror_unlocked(f) /* //-V1059 */
#endif /* if !defined(LZ9ANS_PTHREADS) && (defined(__linux) \
          || defined(__linux__) || defined(linux) || defined(__gnu_linux__)) */

/*****************************************************************************/

#if !defined(LZ9ANS_PTHREADS) && (defined(_WIN32) || defined(_WIN64))
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
#endif /* if !defined(LZ9ANS_PTHREADS) && (defined(_WIN32)
          || defined(_WIN64)) */

/*****************************************************************************/

#if defined(__GNUC__) && !defined(__CPPCHECK__) && !defined(__LINT__)
# define LZ9_INLINE __inline__
# define LZ9_ALWAYS_INLINE __inline__ __attribute__ ((always_inline))
#else
# define LZ9_INLINE
# define LZ9_ALWAYS_INLINE LZ9_INLINE
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

/* The 64-bit lane comparison assumes little-endian x86-64 symbol storage. */
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__amd64__)) \
    && !defined(__CPPCHECK__) && !defined(__LINT__) \
    && !defined(LZ9ANS_DISABLE_X86_64_MATCH_FASTPATH)
# define LZ9ANS_X86_64_MATCH_FASTPATH 1
#endif /* if defined(__GNUC__) && x86-64 */

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

static unsigned int
sequence_capacity (unsigned int min_match)
{
  return BLOCK_N / min_match + 1U;
}

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
static unsigned int
decoder_symbol_capacity (unsigned int min_match)
{
  return BLOCK_N / min_match;
}
#endif /* if defined(LZ9ANS_PTHREADS) */

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
static const unsigned char decoder_phase_table_base[3] = { 0, 2, 4 };

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
  unsigned int seq_capacity;
  int *head, *link;
  unsigned char *stage, *desc;
  AnsTable *table;
  unsigned int counts[512];
  unsigned int hlen;
  unsigned int base;
  int chain_state;
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
#if defined(LZ9ANS_PTHREADS)
  unsigned int *prefix;
#else
  unsigned int prefix[BLOCK_N + 1U];
#endif /* if defined(LZ9ANS_PTHREADS) */
} Encoder;

/*****************************************************************************/

#if !defined(LZ9ANS_PTHREADS)
typedef char encoder_prefix_must_hold_table_cache_compile_time_assertion[
  sizeof (((Encoder *)0)->prefix) >= TABLE_CACHE_BYTES ? 1 : -1];
#endif /* if !defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

typedef char stage_must_hold_maximum_stored_block_compile_time_assertion[
  STAGE_DATA_MAX >= (BLOCK_N * 9U + 7U) / 8U ? 1 : -1];

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
# define THREAD_STACK_BYTES (256UL * 1024UL)

/*****************************************************************************/

typedef void (*ThreadTaskFunction) (void *);

/*****************************************************************************/

typedef struct
{
  ThreadTaskFunction function;
  void *context;
} ThreadTask;

/*****************************************************************************/

typedef struct ThreadPool ThreadPool;

/*****************************************************************************/

typedef struct
{
  ThreadPool *pool;
  unsigned long generation;
} ThreadWorker;

/*****************************************************************************/

struct ThreadPool
{
  pthread_mutex_t mutex;
  pthread_cond_t work_ready;
  pthread_cond_t work_done;
  pthread_t *threads;
  ThreadWorker *workers;
  ThreadTask *tasks;
  unsigned long generation;
  unsigned int task_count;
  unsigned int next_task;
  unsigned int pending;
  unsigned int participants;
  unsigned int worker_count;
  unsigned int task_capacity;
  int stopping;
  int mutex_ready;
  int work_ready_ready;
  int work_done_ready;
};

/*****************************************************************************/

#else
typedef struct
{
  unsigned int participants;
  unsigned int task_capacity;
} ThreadPool;
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
typedef struct
{
  Encoder encoder;
  unsigned char *stage;
  unsigned char *desc;
  AnsTable *table;
  AnsTable *gtable;
  unsigned int kind;
  int okay;
} ThreadEncoderWorkspace;

/*****************************************************************************/

typedef struct
{
  Stream stream;
  unsigned char *symbols;
  unsigned int count;
  int okay;
} ThreadDecodeWorkspace;

/*****************************************************************************/

#else
typedef void ThreadEncoderWorkspace;
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

typedef struct
{
  unsigned char *symbols[ENTROPY_STREAM_COUNT - 1U];
#if defined(LZ9ANS_PTHREADS)
  unsigned int capacity;
#endif /* if defined(LZ9ANS_PTHREADS) */
} ThreadDecoderBuffers;

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
typedef struct
{
  Encoder encoder;
  unsigned char *output;
  size_t output_capacity;
  size_t output_size;
  int okay;
} ThreadBlockJob;
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

typedef struct
{
  FILE *file;
  unsigned char *buffer;
  size_t capacity;
  size_t size;
} BlockOutput;

/*****************************************************************************/

typedef struct
{
  unsigned short *ring;
  unsigned char *ring8;
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

#if defined(LZ9ANS_PTHREADS)
static unsigned int
available_processors (void)
{
# if defined(_WIN32) || defined(_WIN64)
  SYSTEM_INFO info;

  (void)memset (&info, 0, sizeof (info));
  GetSystemInfo (&info);

  return info.dwNumberOfProcessors ? (unsigned int)info.dwNumberOfProcessors
                                   : 1U;
# else
  long count = sysconf (_SC_NPROCESSORS_ONLN);

  return count > 0L ? (unsigned int)count : 1U;
# endif /* if defined(_WIN32) || defined(_WIN64) */
}

/*****************************************************************************/

static unsigned int
compression_participants (unsigned int limit)
{
  unsigned int cpus = available_processors ();
  unsigned int participants = limit ? limit : cpus;
  unsigned int maximum = thread_limit_maximum ();

  if (participants > maximum)
    {
      participants = maximum;
    }

  if (participants > cpus)
    {
      participants = cpus;
    }

  return participants ? participants : 1U;
}

/*****************************************************************************/

static void *
thread_worker_main (void *arg)
{
  ThreadWorker *worker = (ThreadWorker *)arg;
  ThreadPool *pool = worker->pool;

  (void)pthread_mutex_lock (&pool->mutex);

  for (;;)
    {
      while (!pool->stopping && worker->generation == pool->generation)
        {
          (void)pthread_cond_wait (&pool->work_ready, &pool->mutex);
        }

      if (pool->stopping)
        {
          break;
        }

      worker->generation = pool->generation;

      while (pool->next_task < pool->task_count)
        {
          unsigned int index = pool->next_task++;
          ThreadTask task = pool->tasks[index];

          (void)pthread_mutex_unlock (&pool->mutex);
          task.function (task.context);
          (void)pthread_mutex_lock (&pool->mutex);

          if (pool->pending)
            {
              --pool->pending;
            }

          if (!pool->pending)
            {
              (void)pthread_cond_broadcast (&pool->work_done);
            }
        }
    }

  (void)pthread_mutex_unlock (&pool->mutex);

  return (void *)0;
}
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

static int
thread_pool_init (ThreadPool *pool, unsigned int limit,
                  unsigned int max_participants)
{
  (void)memset (pool, 0, sizeof (*pool));

  if (!max_participants)
    {
      max_participants = 1U;
    }

#if defined(LZ9ANS_PTHREADS)
  {
    unsigned int desired, cpus, i;
    pthread_attr_t attr;
    int attr_ready = 0;

    cpus = available_processors ();
    desired = limit ? limit : cpus;

    if (desired > thread_limit_maximum ())
      {
        desired = thread_limit_maximum ();
      }

    if (desired > cpus)
      {
        desired = cpus;
      }

    if (desired > max_participants)
      {
        desired = max_participants;
      }

    if (!desired)
      {
        desired = 1U;
      }

    pool->participants = desired;
    pool->task_capacity = max_participants;

    if (desired == 1U)
      {
        return 1;
      }

    pool->threads = (pthread_t *)calloc (desired - 1U, sizeof (pthread_t));
    pool->workers = (ThreadWorker *)calloc (desired - 1U,
                                             sizeof (ThreadWorker));
    pool->tasks = (ThreadTask *)calloc (max_participants,
                                        sizeof (ThreadTask));

    if (!pool->threads || !pool->workers || !pool->tasks)
      {
        goto init_failed;
      }

    if (pthread_mutex_init (&pool->mutex, (const pthread_mutexattr_t *)0) != 0)
      {
        goto init_failed;
      }

    pool->mutex_ready = 1;

    if (pthread_cond_init (&pool->work_ready, (const pthread_condattr_t *)0)
        != 0)
      {
        goto init_failed;
      }

    pool->work_ready_ready = 1;

    if (pthread_cond_init (&pool->work_done, (const pthread_condattr_t *)0)
        != 0)
      {
        goto init_failed;
      }

    pool->work_done_ready = 1;

    if (pthread_attr_init (&attr) != 0)
      {
        goto init_failed;
      }

    attr_ready = 1;

    if (pthread_attr_setstacksize (&attr, (size_t)THREAD_STACK_BYTES) != 0)
      {
        goto init_failed;
      }

    for (i = 0; i < desired - 1U; ++i)
      {
        pool->workers[i].pool = pool;
        pool->workers[i].generation = 0UL;

        if (pthread_create (&pool->threads[i], &attr, thread_worker_main,
                            &pool->workers[i]) != 0)
          {
            goto init_failed;
          }

        ++pool->worker_count;
      }

    if (attr_ready) /* //-V547 */
      {
        (void)pthread_attr_destroy (&attr);
      }

    return 1;

init_failed:
    if (attr_ready)
      {
        (void)pthread_attr_destroy (&attr);
      }

    if (pool->mutex_ready)
      {
        (void)pthread_mutex_lock (&pool->mutex);
        pool->stopping = 1;

        if (pool->work_ready_ready)
          {
            (void)pthread_cond_broadcast (&pool->work_ready);
          }

        (void)pthread_mutex_unlock (&pool->mutex);
      }

    if (pool->threads)
      {
        for (i = 0; i < pool->worker_count; ++i)
          {
            (void)pthread_join (pool->threads[i], (void **)0);
          }
      }

    if (pool->work_done_ready)
      {
        (void)pthread_cond_destroy (&pool->work_done);
      }

    if (pool->work_ready_ready)
      {
        (void)pthread_cond_destroy (&pool->work_ready);
      }

    if (pool->mutex_ready)
      {
        (void)pthread_mutex_destroy (&pool->mutex);
      }

    free (pool->threads);
    free (pool->workers);
    free (pool->tasks);
    (void)memset (pool, 0, sizeof (*pool));

    return 0;
  }
#else
  (void)limit;
  pool->participants = 1U;
  pool->task_capacity = max_participants;

  return 1;
#endif /* if defined(LZ9ANS_PTHREADS) */
}

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
static int
thread_pool_run (ThreadPool *pool, const ThreadTask *tasks,
                 unsigned int task_count)
{
  unsigned int i;

  if (!pool || !tasks
      || task_count > pool->task_capacity)
    {
      return 0;
    }

  if (pool->worker_count && task_count)
    {
      (void)pthread_mutex_lock (&pool->mutex);

      for (i = 0; i < task_count; ++i)
        {
          pool->tasks[i] = tasks[i];
        }

      pool->task_count = task_count;
      pool->next_task = 0U;
      pool->pending = task_count;
      ++pool->generation;
      (void)pthread_cond_broadcast (&pool->work_ready);

      while (pool->next_task < pool->task_count)
        {
          unsigned int index = pool->next_task++;
          ThreadTask task = pool->tasks[index];
          (void)pthread_mutex_unlock (&pool->mutex);
          task.function (task.context);
          (void)pthread_mutex_lock (&pool->mutex);
          --pool->pending;

          if (!pool->pending)
            {
              (void)pthread_cond_broadcast (&pool->work_done);
            }
        }

      while (pool->pending)
        {
          (void)pthread_cond_wait (&pool->work_done, &pool->mutex);
        }

      (void)pthread_mutex_unlock (&pool->mutex);

      return 1;
    }

  for (i = 0; i < task_count; ++i)
    {
      tasks[i].function (tasks[i].context);
    }

  return 1;
}
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

static int
thread_pool_destroy (ThreadPool *pool)
{
#if defined(LZ9ANS_PTHREADS)
  int okay = 1;

  if (pool->worker_count)
    {
      unsigned int i;

      (void)pthread_mutex_lock (&pool->mutex);
      pool->stopping = 1;
      (void)pthread_cond_broadcast (&pool->work_ready);
      (void)pthread_mutex_unlock (&pool->mutex);

      for (i = 0; i < pool->worker_count; ++i)
        {
          if (pthread_join (pool->threads[i], (void **)0) != 0)
            {
              okay = 0;
            }
        }
    }

  if (pool->work_done_ready
      && pthread_cond_destroy (&pool->work_done) != 0)
    {
      okay = 0;
    }

  if (pool->work_ready_ready
      && pthread_cond_destroy (&pool->work_ready) != 0)
    {
      okay = 0;
    }

  if (pool->mutex_ready && pthread_mutex_destroy (&pool->mutex) != 0)
    {
      okay = 0;
    }

  free (pool->threads);
  free (pool->workers);
  free (pool->tasks);
  (void)memset (pool, 0, sizeof (*pool));

  return okay;
#else
  (void)pool;

  return 1;
#endif /* if defined(LZ9ANS_PTHREADS) */
}

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

typedef char crc_slice_needs_32_bit_unsigned_int_compile_time_assertion[
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

static HOT LZ9_INLINE void
pack8_bytes (const unsigned char *v, unsigned char *p)
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
  p[7] = (unsigned char)((v[6] & 127U) << 1);
  p[8] = v[7];
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

  while (n - i >= 8U)
    {
      pack8_symbols (v + i, packed + used);
      used += 9U;
      i += 8U;
    }

  if (i < n)
    {
      used += pack9_tail (v + i, n - i, packed + used);
    }

  return used ? crc_update (0UL, packed, used) : 0UL;
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
  return fwrite (p, 1, n, f) == n;
}

/*****************************************************************************/

static int
read_exact (FILE *f, void *p, size_t n)
{
  return fread (p, 1, n, f) == n;
}

/*****************************************************************************/

static int
read_some (FILE *f, unsigned char *p, size_t n, size_t *used)
{
  size_t done = 0;

  while (done < n)
    {
      size_t got = fread (p + done, 1, n - done, f);

      done += got;

      if (!got)
        {
          if (ferror (f))
            {
              return 0;
            }

          if (feof (f))
            {
              break;
            }

          return 0;
        }
    }

  *used = done;

  return 1;
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

#if defined(LZ9ANS_TEST_NORMALIZER)
static int
norm_active_legacy ( /*cppcheck-suppress unusedFunction*/
                    const unsigned int *counts, const unsigned int *idx,
                    unsigned int active, unsigned int total,
                    unsigned short *freq)
{
  unsigned int k, remaining, used = 0U;
  unsigned int key[512];

  if (active < 2U || active > 512U || !total || total > 0x3fffffU)
    {
      return 0;
    }

  remaining = TABLE_N - active;

  for (k = 0U; k < active; ++k)
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
          unsigned int n = 0U;

          for (k = 0U; k < active; ++k)
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
          for (k = 0U; k < active; ++k)
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
#endif /* if defined(LZ9ANS_TEST_NORMALIZER) */

/*****************************************************************************/

#if defined(LZ9ANS_TEST_NORMALIZER)
static void
norm_u32_sort_small (unsigned int *values, unsigned int n)
{
  unsigned int i;

  for (i = 1U; i < n; ++i)
    {
      unsigned int value = values[i];
      unsigned int j = i;

      while (j && value < values[j - 1U])
        {
          values[j] = values[j - 1U];
          --j;
        }

      values[j] = value;
    }
}

/*****************************************************************************/

static unsigned int
norm_u32_select (unsigned int *values, unsigned int n, unsigned int kth)
{
  while (n > 5U)
    {
      unsigned int i, medians = 0U;
      unsigned int lt, gt, pivot;

      for (i = 0U; i < n; i += 5U)
        {
          unsigned int group_n = n - i;
          unsigned int median, tmp;

          if (group_n > 5U)
            {
              group_n = 5U;
            }

          norm_u32_sort_small (values + i, group_n);
          median = i + group_n / 2U;
          tmp = values[medians];
          values[medians] = values[median];
          values[median] = tmp;
          ++medians;
        }

      pivot = norm_u32_select (values, medians, medians / 2U);
      lt = 0U;
      gt = n;
      i = 0U;

      while (i < gt)
        {
          if (values[i] < pivot)
            {
              unsigned int tmp = values[lt];

              values[lt] = values[i];
              values[i] = tmp;
              ++lt;
              ++i;
            }
          else if (values[i] > pivot)
            {
              unsigned int tmp;

              --gt;
              tmp = values[gt];
              values[gt] = values[i];
              values[i] = tmp;
            }
          else
            {
              ++i;
            }
        }

      if (kth < lt)
        {
          n = lt;
        }
      else if (kth < gt)
        {
          return pivot;
        }
      else
        {
          values += gt;
          kth -= gt;
          n -= gt;
        }
    }

  norm_u32_sort_small (values, n);

  return values[kth];
}

/*****************************************************************************/

static int
norm_active_legacy_select ( /*cppcheck-suppress unusedFunction*/
                           const unsigned int *counts,
                           const unsigned int *idx, unsigned int active,
                           unsigned int total, unsigned short *freq)
{
  unsigned int k, remaining, used = 0U;
  unsigned int key[512], select_key[512];

  if (active < 2U || active > 512U || !total || total > 0x3fffffU)
    {
      return 0;
    }

  remaining = TABLE_N - active;

  for (k = 0U; k < active; ++k)
    {
      unsigned int i = idx[k];
      unsigned int q = counts[i] * remaining;

      freq[i] = (unsigned short)(1U + q / total);
      key[k] = ((q % total) << 9) | (511U - i);
      used += freq[i];
    }

  if (used < TABLE_N)
    {
      unsigned int deficit = TABLE_N - used;
      unsigned int threshold;

      (void)memcpy (select_key, key, active * sizeof (unsigned int));
      threshold = norm_u32_select (select_key, active, active - deficit);

      for (k = 0U; k < active; ++k)
        {
          if (key[k] >= threshold)
            {
              ++freq[idx[k]];
              ++used;
            }
        }
    }

  return used == TABLE_N;
}
#endif /* if defined(LZ9ANS_TEST_NORMALIZER) */

/*****************************************************************************/

#define NORM_TICKET_MAX (4U * 512U - 4U)
#define NORM_TICKET_MAX_J (TABLE_N + 512U - 2U)

/*****************************************************************************/

typedef struct
{
  unsigned int hi;
  unsigned int lo;
} NormLogQ64;

/*****************************************************************************/

typedef struct
{
  unsigned int cost_hi;
  unsigned int cost_mid;
  unsigned int cost_lo;
  unsigned short symbol;
  unsigned short j;
} NormTicket;

/*****************************************************************************/

typedef struct
{
  NormTicket tickets[NORM_TICKET_MAX];
#if !defined(NORM_KL_BFPRT)
  unsigned short equal_idx[NORM_TICKET_MAX];
#endif /* if !defined(NORM_KL_BFPRT) */
} NormScratch;

/*****************************************************************************/

typedef char norm_scratch_must_fit_encoder_link_assertion[
  sizeof (NormScratch) <= WINDOW_DEFAULT * sizeof (int) ? 1 : -1];

/*****************************************************************************/

typedef char norm_scratch_must_fit_thread_stage_assertion[
  sizeof (NormScratch) <= STAGE_DATA_MAX ? 1 : -1];

/*****************************************************************************/

typedef struct
{
  unsigned int hi;
  unsigned int mid;
  unsigned int lo;
} NormTicketCost;

/*****************************************************************************/

static const NormLogQ64 norm_log_q64[NORM_TICKET_MAX_J + 1U] =
{
  {          0UL,          0UL },
  {          0UL,          0UL },
  { 0xb17217f7UL, 0xd1cf79acUL }, /* 2 */
  { 0x67cc8fb2UL, 0xfe612fcbUL }, /* 3 */
  { 0x49a58844UL, 0xd36e49e1UL }, /* 4 */
  { 0x391fef8fUL, 0x35344358UL }, /* 5 */
  { 0x2eaca023UL, 0xc92cec73UL }, /* 6 */
  { 0x27766b40UL, 0x83a55225UL }, /* 7 */
  { 0x222f1d04UL, 0x4fc8f7bcUL }, /* 8 */
  { 0x1e27076eUL, 0x2af2e5eaUL }, /* 9 */
  { 0x1af8e821UL, 0x0a415d6eUL }, /* 10 */
  { 0x18663f79UL, 0x3c46c69cUL }, /* 11 */
  { 0x164660aaUL, 0x8ce625d7UL }, /* 12 */
  { 0x147dadcbUL, 0xbdba8303UL }, /* 13 */
  { 0x12f8bd74UL, 0xc5eacf22UL }, /* 14 */
  { 0x11a9844eUL, 0xb18ef134UL }, /* 15 */
  { 0x108598b5UL, 0x9e3a0689UL }, /* 16 */
  { 0x0f851860UL, 0x08b15331UL }, /* 17 */
  { 0x0ea1ef0eUL, 0x224192b9UL }, /* 18 */
  { 0x0dd75973UL, 0x2434c1a7UL }, /* 19 */
  { 0x0d218eadUL, 0xe60c9bc7UL }, /* 20 */
  { 0x0c7d831fUL, 0x7963f4b6UL }, /* 21 */
  { 0x0be8bc59UL, 0xc2e2d1e6UL }, /* 22 */
  { 0x0b6130d2UL, 0x7dc5107fUL }, /* 23 */
  { 0x0ae52fd8UL, 0x0f211557UL }, /* 24 */
  { 0x0a734f6bUL, 0x6c0756e6UL }, /* 25 */
  { 0x0a0a5e60UL, 0x51b32c1dUL }, /* 26 */
  { 0x09a959a2UL, 0x6d3862e7UL }, /* 27 */
  { 0x094f63d2UL, 0x58b26c3bUL }, /* 28 */
  { 0x08fbbea6UL, 0xf67f6f0eUL }, /* 29 */
  { 0x08adc5a7UL, 0xbb0f8225UL }, /* 30 */
  { 0x0864e9f0UL, 0xaa97e451UL }, /* 31 */
  { 0x0820aec4UL, 0xf3a22238UL }, /* 32 */
  { 0x07e0a6c3UL, 0x9e0cc013UL }, /* 33 */
  { 0x07a4719cUL, 0x6aa4931eUL }, /* 34 */
  { 0x076bba2aUL, 0xdcb9f86bUL }, /* 35 */
  { 0x073634e3UL, 0x45879a4eUL }, /* 36 */
  { 0x07039e82UL, 0x14f760aeUL }, /* 37 */
  { 0x06d3baf1UL, 0x0f3d60f9UL }, /* 38 */
  { 0x06a65458UL, 0x9985c15cUL }, /* 39 */
  { 0x067b3a55UL, 0x4c86da6cUL }, /* 40 */
  { 0x0652414bUL, 0x874811c2UL }, /* 41 */
  { 0x062b41d3UL, 0xf21be2f4UL }, /* 42 */
  { 0x0606183cUL, 0xd5dc8a62UL }, /* 43 */
  { 0x05e2a41cUL, 0xed064783UL }, /* 44 */
  { 0x05c0c7f4UL, 0xeeac1f4eUL }, /* 45 */
  { 0x05a068ddUL, 0x8f18f131UL }, /* 46 */
  { 0x05816e40UL, 0x15c95063UL }, /* 47 */
  { 0x0563c197UL, 0xf957c4f4UL }, /* 48 */
  { 0x05474e3cUL, 0x33dc5a68UL }, /* 49 */
  { 0x052c012fUL, 0x382afc7eUL }, /* 50 */
  { 0x0511c8f4UL, 0x9ca9fc4bUL }, /* 51 */
  { 0x04f8956bUL, 0xb5092fd2UL }, /* 52 */
  { 0x04e057aeUL, 0x726c4367UL }, /* 53 */
  { 0x04c901f3UL, 0xfacc1f80UL }, /* 54 */
  { 0x04b28776UL, 0x7d5b3798UL }, /* 55 */
  { 0x049cdc5bUL, 0xdb5734a3UL }, /* 56 */
  { 0x0487f5a0UL, 0xcb82556cUL }, /* 57 */
  { 0x0473c906UL, 0x2afd19a2UL }, /* 58 */
  { 0x04604d00UL, 0x38cadc50UL }, /* 59 */
  { 0x044d78a7UL, 0x8244a5d5UL }, /* 60 */
  { 0x043b43abUL, 0x4e6064eeUL }, /* 61 */
  { 0x0429a645UL, 0x5c377f63UL }, /* 62 */
  { 0x0418992eUL, 0xcecc1066UL }, /* 63 */
  { 0x04081596UL, 0x24d611d2UL }, /* 64 */
  { 0x03f81516UL, 0x1f807c7aUL }, /* 65 */
  { 0x03e891adUL, 0x7e8c4399UL }, /* 66 */
  { 0x03d985b7UL, 0x7b61be10UL }, /* 67 */
  { 0x03caebe4UL, 0xef42d50eUL }, /* 68 */
  { 0x03bcbf36UL, 0x13207d62UL }, /* 69 */
  { 0x03aefaf4UL, 0xc9997b09UL }, /* 70 */
  { 0x03a19aafUL, 0x656a17c9UL }, /* 71 */
  { 0x03949a33UL, 0xe01d8285UL }, /* 72 */
  { 0x0387f58bUL, 0x7629cce3UL }, /* 73 */
  { 0x037ba8f6UL, 0x9ecd93cbUL }, /* 74 */
  { 0x036fb0e9UL, 0x570ff638UL }, /* 75 */
  { 0x03640a07UL, 0xb82d6ac1UL }, /* 76 */
  { 0x0358b122UL, 0xd28a6aa7UL }, /* 77 */
  { 0x034da335UL, 0xc6fb56b5UL }, /* 78 */
  { 0x0342dd63UL, 0x18d27cc5UL }, /* 79 */
  { 0x03385cf2UL, 0x33b45da7UL }, /* 80 */
  { 0x032e1f4dUL, 0x20b1887cUL }, /* 81 */
  { 0x032421feUL, 0x66968947UL }, /* 82 */
  { 0x031a62afUL, 0x11c73863UL }, /* 83 */
  { 0x0310df24UL, 0xe054aa91UL }, /* 84 */
  { 0x03079540UL, 0x8f4d5e7bUL }, /* 85 */
  { 0x02fe82fcUL, 0x468f2be8UL }, /* 86 */
  { 0x02f5a66aUL, 0x20a2e4acUL }, /* 87 */
  { 0x02ecfdb2UL, 0xcc6362d7UL }, /* 88 */
  { 0x02e48714UL, 0x4665aa33UL }, /* 89 */
  { 0x02dc40e0UL, 0xa846751bUL }, /* 90 */
  { 0x02d4297dUL, 0x0c2b91cfUL }, /* 91 */
  { 0x02cc3f60UL, 0x82ed5f62UL }, /* 92 */
  { 0x02c48113UL, 0x1b7ef31fUL }, /* 93 */
  { 0x02bced2cUL, 0xfa4a5d44UL }, /* 94 */
  { 0x02b58255UL, 0x7f528013UL }, /* 95 */
  { 0x02ae3f42UL, 0x7a0544e2UL }, /* 96 */
  { 0x02a722b7UL, 0x69bff66aUL }, /* 97 */
  { 0x02a02b84UL, 0xca1c63feUL }, /* 98 */
  { 0x02995887UL, 0x6a3065abUL }, /* 99 */
  { 0x0292a8a7UL, 0xcdfa96d2UL }, /* 100 */
  { 0x028c1ad9UL, 0x9946ced7UL }, /* 101 */
  { 0x0285ae1bUL, 0x03632d74UL }, /* 102 */
  { 0x027f6174UL, 0x530b9c92UL }, /* 103 */
  { 0x027933f7UL, 0x61fd933fUL }, /* 104 */
  { 0x027324bfUL, 0x27b0c899UL }, /* 105 */
  { 0x026d32efUL, 0x4abb7aceUL }, /* 106 */
  { 0x02675db3UL, 0xb8720c3eUL }, /* 107 */
  { 0x0261a440UL, 0x425a1342UL }, /* 108 */
  { 0x025c05d0UL, 0x4110a23aUL }, /* 109 */
  { 0x025681a6UL, 0x3c4a955dUL }, /* 110 */
  { 0x0251170bUL, 0x979c2916UL }, /* 111 */
  { 0x024bc550UL, 0x43bb0b8cUL }, /* 112 */
  { 0x02468bcaUL, 0x73f39228UL }, /* 113 */
  { 0x024169d6UL, 0x578ec345UL }, /* 114 */
  { 0x023c5ed5UL, 0xd6eb8670UL }, /* 115 */
  { 0x02376a30UL, 0x54119332UL }, /* 116 */
  { 0x02328b52UL, 0x6e88a7baUL }, /* 117 */
  { 0x022dc1adUL, 0xca423497UL }, /* 118 */
  { 0x02290cb8UL, 0xd96707d2UL }, /* 119 */
  { 0x02246beeUL, 0xa8dd9e03UL }, /* 120 */
  { 0x021fdeceUL, 0xaf60a0c5UL }, /* 121 */
  { 0x021b64dcUL, 0x9effc429UL }, /* 122 */
  { 0x0216fda0UL, 0x38e7acd4UL }, /* 123 */
  { 0x0212a8a5UL, 0x234fd28eUL }, /* 124 */
  { 0x020e657aUL, 0xc16f7295UL }, /* 125 */
  { 0x020a33b4UL, 0x0d5c9dd0UL }, /* 126 */
  { 0x020612e7UL, 0x73ba43adUL }, /* 127 */
  { 0x020202aeUL, 0xb11bce25UL }, /* 128 */
  { 0x01fe02a6UL, 0xb1067890UL }, /* 129 */
  { 0x01fa126fUL, 0x6e7a03eaUL }, /* 130 */
  { 0x01f631abUL, 0xd5ebca30UL }, /* 131 */
  { 0x01f26001UL, 0xa8a07969UL }, /* 132 */
  { 0x01ee9d19UL, 0x6151efc1UL }, /* 133 */
  { 0x01eae89eUL, 0x1a0fce4fUL }, /* 134 */
  { 0x01e7423dUL, 0x734a613eUL }, /* 135 */
  { 0x01e3a9a7UL, 0x7bf873cfUL }, /* 136 */
  { 0x01e01e8eUL, 0x9ac98db8UL }, /* 137 */
  { 0x01dca0a7UL, 0x7856efaaUL }, /* 138 */
  { 0x01d92fa8UL, 0xea466ed7UL }, /* 139 */
  { 0x01d5cb4bUL, 0xdf530c33UL }, /* 140 */
  { 0x01d2734bUL, 0x4c2fd55aUL }, /* 141 */
  { 0x01cf2764UL, 0x193a426fUL }, /* 142 */
  { 0x01cbe755UL, 0x10f1dfb1UL }, /* 143 */
  { 0x01c8b2deUL, 0xcf2ba2d4UL }, /* 144 */
  { 0x01c589c3UL, 0xb0f7d4c0UL }, /* 145 */
  { 0x01c26bc7UL, 0xc531f823UL }, /* 146 */
  { 0x01bf58b0UL, 0xbdb28d85UL }, /* 147 */
  { 0x01bc5045UL, 0xe11b0646UL }, /* 148 */
  { 0x01b9524fUL, 0xfd34a1e0UL }, /* 149 */
  { 0x01b65e99UL, 0x59db5458UL }, /* 150 */
  { 0x01b374edUL, 0xac6e3276UL }, /* 151 */
  { 0x01b0951aUL, 0x0bbf384bUL }, /* 152 */
  { 0x01adbeecUL, 0xe47c918aUL }, /* 153 */
  { 0x01aaf235UL, 0xee0dd91dUL }, /* 154 */
  { 0x01a82ec6UL, 0x1fe00ee8UL }, /* 155 */
  { 0x01a5746fUL, 0xa71b47ccUL }, /* 156 */
  { 0x01a2c305UL, 0xdcbd5d46UL }, /* 157 */
  { 0x01a01a5dUL, 0x3c151f7fUL }, /* 158 */
  { 0x019d7a4bUL, 0x5999c6a2UL }, /* 159 */
  { 0x019ae2a6UL, 0xda1a9704UL }, /* 160 */
  { 0x01985347UL, 0x6a42df5fUL }, /* 161 */
  { 0x0195cc05UL, 0xb66ea91dUL }, /* 162 */
  { 0x01934cbbUL, 0x62cca0d0UL }, /* 163 */
  { 0x0190d543UL, 0x03c9e876UL }, /* 164 */
  { 0x018e6578UL, 0x16c4ae51UL }, /* 165 */
  { 0x018bfd36UL, 0xfb028a12UL }, /* 166 */
  { 0x01899c5cUL, 0xeae7c6e5UL }, /* 167 */
  { 0x018742c7UL, 0xf56ce3acUL }, /* 168 */
  { 0x0184f056UL, 0xf7cfb3e1UL }, /* 169 */
  { 0x0182a4e9UL, 0x977daa9aUL }, /* 170 */
  { 0x01806060UL, 0x3c34f6f2UL }, /* 171 */
  { 0x017e229cUL, 0x0a5a34f6UL }, /* 172 */
  { 0x017beb7eUL, 0xdd8090e8UL }, /* 173 */
  { 0x0179baebUL, 0x432253c4UL }, /* 174 */
  { 0x017790c4UL, 0x7587e7d7UL }, /* 175 */
  { 0x01756ceeUL, 0x56db7b00UL }, /* 176 */
  { 0x01734f4dUL, 0x6c677979UL }, /* 177 */
  { 0x017137c6UL, 0xd9fe30baUL }, /* 178 */
  { 0x016f2640UL, 0x5d88fd2bUL }, /* 179 */
  { 0x016d1aa0UL, 0x4abd77f0UL }, /* 180 */
  { 0x016b14cdUL, 0x86f72b7cUL }, /* 181 */
  { 0x016914afUL, 0x85346653UL }, /* 182 */
  { 0x01671a2eUL, 0x4234d31fUL }, /* 183 */
  { 0x01652532UL, 0x40b88c43UL }, /* 184 */
  { 0x016335a4UL, 0x85de6f7dUL }, /* 185 */
  { 0x01614b6eUL, 0x95a083a2UL }, /* 186 */
  { 0x015f667aUL, 0x6f6d4f92UL }, /* 187 */
  { 0x015d86b2UL, 0x8add0db2UL }, /* 188 */
  { 0x015bac01UL, 0xd481b322UL }, /* 189 */
  { 0x0159d653UL, 0xaad0ccf1UL }, /* 190 */
  { 0x01580593UL, 0xdb264045UL }, /* 191 */
  { 0x015639aeUL, 0x9edf049cUL }, /* 192 */
  { 0x01547290UL, 0x988af8e5UL }, /* 193 */
  { 0x0152b026UL, 0xd134fd85UL }, /* 194 */
  { 0x0150f25eUL, 0xb5c08610UL }, /* 195 */
  { 0x014f3926UL, 0x145bddeeUL }, /* 196 */
  { 0x014d846bUL, 0x1a066333UL }, /* 197 */
  { 0x014bd41cUL, 0x502a0278UL }, /* 198 */
  { 0x014a2828UL, 0x9a4745f1UL }, /* 199 */
  { 0x0148807fUL, 0x33b350e2UL }, /* 200 */
  { 0x0146dd0fUL, 0xad67273dUL }, /* 201 */
  { 0x01453dc9UL, 0xebdfa79aUL }, /* 202 */
  { 0x0143a29eUL, 0x250da3baUL }, /* 203 */
  { 0x01420b7cUL, 0xde5589baUL }, /* 204 */
  { 0x01407856UL, 0xea9e1577UL }, /* 205 */
  { 0x013ee91dUL, 0x686d871bUL }, /* 206 */
  { 0x013d5dc1UL, 0xc014e0cfUL }, /* 207 */
  { 0x013bd635UL, 0xa1e8b270UL }, /* 208 */
  { 0x013a526bUL, 0x0486feb8UL }, /* 209 */
  { 0x0138d254UL, 0x2329c9e2UL }, /* 210 */
  { 0x013755e3UL, 0x7c05e5f0UL }, /* 211 */
  { 0x0135dd0bUL, 0xceb594dfUL }, /* 212 */
  { 0x013467c0UL, 0x1aae9cfbUL }, /* 213 */
  { 0x0132f5f3UL, 0x9dc36f43UL }, /* 214 */
  { 0x01318799UL, 0xd2af0356UL }, /* 215 */
  { 0x01301ca6UL, 0x6fab0fecUL }, /* 216 */
  { 0x012eb50dUL, 0x65104a03UL }, /* 217 */
  { 0x012d50c2UL, 0xdc005838UL }, /* 218 */
  { 0x012befbbUL, 0x35192aa9UL }, /* 219 */
  { 0x012a91ebUL, 0x07316ab4UL }, /* 220 */
  { 0x01293747UL, 0x1e1db8b2UL }, /* 221 */
  { 0x0127dfc4UL, 0x797e7065UL }, /* 222 */
  { 0x01268b58UL, 0x4b95af82UL }, /* 223 */
  { 0x012539f7UL, 0xf8255c0bUL }, /* 224 */
  { 0x0123eb99UL, 0x1354eaabUL }, /* 225 */
  { 0x0122a031UL, 0x609ea77cUL }, /* 226 */
  { 0x012157b6UL, 0xd1c445c9UL }, /* 227 */
  { 0x0120121fUL, 0x85ca7d7cUL }, /* 228 */
  { 0x011ecf61UL, 0xc7fb7ee7UL }, /* 229 */
  { 0x011d8f74UL, 0x0ef00789UL }, /* 230 */
  { 0x011c524cUL, 0xfb9ee437UL }, /* 231 */
  { 0x011b17e3UL, 0x5872aefbUL }, /* 232 */
  { 0x0119e02eUL, 0x18659892UL }, /* 233 */
  { 0x0118ab24UL, 0x56230f27UL }, /* 234 */
  { 0x011778bdUL, 0x532f1578UL }, /* 235 */
  { 0x011648f0UL, 0x77131f1fUL }, /* 236 */
  { 0x01151bb5UL, 0x4e90482eUL }, /* 237 */
  { 0x0113f103UL, 0x8ad6bfa4UL }, /* 238 */
  { 0x0112c8d3UL, 0x00c23dbaUL }, /* 239 */
  { 0x0111a31bUL, 0xa81b6049UL }, /* 240 */
  { 0x01107fd5UL, 0x9addc8b0UL }, /* 241 */
  { 0x010f5ef9UL, 0x1482d815UL }, /* 242 */
  { 0x010e407eUL, 0x7150e7b6UL }, /* 243 */
  { 0x010d245eUL, 0x2daedc73UL }, /* 244 */
  { 0x010c0a90UL, 0xe57bf57aUL }, /* 245 */
  { 0x010af30fUL, 0x536bb75aUL }, /* 246 */
  { 0x0109ddd2UL, 0x5065d579UL }, /* 247 */
  { 0x0108cad2UL, 0xd2e9fd15UL }, /* 248 */
  { 0x0107ba09UL, 0xee7765d5UL }, /* 249 */
  { 0x0106ab70UL, 0xd2f80cc0UL }, /* 250 */
  { 0x01059f00UL, 0xcc2f7f7cUL }, /* 251 */
  { 0x010494b3UL, 0x412d1e55UL }, /* 252 */
  { 0x01038c81UL, 0xb3c1bc8eUL }, /* 253 */
  { 0x01028665UL, 0xbff8871fUL }, /* 254 */
  { 0x01018259UL, 0x1b931acdUL }, /* 255 */
  { 0x01008055UL, 0x9588b358UL }, /* 256 */
  { 0x00ff8055UL, 0x15885e02UL }, /* 257 */
  { 0x00fe8251UL, 0x9b7e1a8dUL }, /* 258 */
  { 0x00fd8645UL, 0x3f1ad64cUL }, /* 259 */
  { 0x00fc8c2aUL, 0x2f5f2d9eUL }, /* 260 */
  { 0x00fb93faUL, 0xb228e0c2UL }, /* 261 */
  { 0x00fa9db1UL, 0x23c2e96eUL }, /* 262 */
  { 0x00f9a947UL, 0xf6781f55UL }, /* 263 */
  { 0x00f8b6b9UL, 0xb2285a14UL }, /* 264 */
  { 0x00f7c600UL, 0xf3dfffceUL }, /* 265 */
  { 0x00f6d718UL, 0x6d71eff3UL }, /* 266 */
  { 0x00f5e9faUL, 0xe513ba72UL }, /* 267 */
  { 0x00f4fea3UL, 0x34fc13ddUL }, /* 268 */
  { 0x00f4150cUL, 0x4b03779cUL }, /* 269 */
  { 0x00f32d31UL, 0x2846e9a3UL }, /* 270 */
  { 0x00f2470cUL, 0xe0ccc9a8UL }, /* 271 */
  { 0x00f1629aUL, 0x9b2baa28UL }, /* 272 */
  { 0x00f07fd5UL, 0x90331dffUL }, /* 273 */
  { 0x00ef9eb9UL, 0x0a966fb8UL }, /* 274 */
  { 0x00eebf40UL, 0x66993611UL }, /* 275 */
  { 0x00ede167UL, 0x11bdb99aUL }, /* 276 */
  { 0x00ed0528UL, 0x8a751fa9UL }, /* 277 */
  { 0x00ec2a80UL, 0x5fd14f2dUL }, /* 278 */
  { 0x00eb516aUL, 0x31388449UL }, /* 279 */
  { 0x00ea79e1UL, 0xae1a87eaUL }, /* 280 */
  { 0x00e9a3e2UL, 0x95a780e8UL }, /* 281 */
  { 0x00e8cf68UL, 0xb6885471UL }, /* 282 */
  { 0x00e7fc6fUL, 0xee988be6UL }, /* 283 */
  { 0x00e72af4UL, 0x2aa1b689UL }, /* 284 */
  { 0x00e65af1UL, 0x66183da3UL }, /* 285 */
  { 0x00e58c63UL, 0xaad9a20eUL }, /* 286 */
  { 0x00e4bf47UL, 0x10ec1a48UL }, /* 287 */
  { 0x00e3f397UL, 0xbe3f888cUL }, /* 288 */
  { 0x00e32951UL, 0xe66fc078UL }, /* 289 */
  { 0x00e26071UL, 0xca881449UL }, /* 290 */
  { 0x00e198f3UL, 0xb8c821aaUL }, /* 291 */
  { 0x00e0d2d4UL, 0x0c69d679UL }, /* 292 */
  { 0x00e00e0fUL, 0x2d68a604UL }, /* 293 */
  { 0x00df4aa1UL, 0x9049e781UL }, /* 294 */
  { 0x00de8887UL, 0xb5e656a9UL }, /* 295 */
  { 0x00ddc7beUL, 0x2b34af9dUL }, /* 296 */
  { 0x00dd0841UL, 0x89155f65UL }, /* 297 */
  { 0x00dc4a0eUL, 0x741f427aUL }, /* 298 */
  { 0x00db8d21UL, 0x9c6d6b1eUL }, /* 299 */
  { 0x00dad177UL, 0xbd6de93aUL }, /* 300 */
  { 0x00da170dUL, 0x9db18de5UL }, /* 301 */
  { 0x00d95de0UL, 0x0ebca491UL }, /* 302 */
  { 0x00d8a5ebUL, 0xecd89c61UL }, /* 303 */
  { 0x00d7ef2eUL, 0x1ee69beaUL }, /* 304 */
  { 0x00d739a3UL, 0x9632fa2dUL }, /* 305 */
  { 0x00d68549UL, 0x4e49975dUL }, /* 306 */
  { 0x00d5d21cUL, 0x4ccb1079UL }, /* 307 */
  { 0x00d52019UL, 0xa142c8a4UL }, /* 308 */
  { 0x00d46f3eUL, 0x64fdc375UL }, /* 309 */
  { 0x00d3bf87UL, 0xbae24b73UL }, /* 310 */
  { 0x00d310f2UL, 0xcf486034UL }, /* 311 */
  { 0x00d2637cUL, 0xd7d2e798UL }, /* 312 */
  { 0x00d1b723UL, 0x13499dbfUL }, /* 313 */
  { 0x00d10be2UL, 0xc973bf87UL }, /* 314 */
  { 0x00d061b9UL, 0x4af36b53UL }, /* 315 */
  { 0x00cfb8a3UL, 0xf121b42cUL }, /* 316 */
  { 0x00cf10a0UL, 0x1deb632bUL }, /* 317 */
  { 0x00ce69abUL, 0x3bae6377UL }, /* 318 */
  { 0x00cdc3c2UL, 0xbd17d4f2UL }, /* 319 */
  { 0x00cd1ee4UL, 0x1d02c212UL }, /* 320 */
  { 0x00cc7b0cUL, 0xde57753aUL }, /* 321 */
  { 0x00cbd83aUL, 0x8beb6a25UL }, /* 322 */
  { 0x00cb366aUL, 0xb861d80bUL }, /* 323 */
  { 0x00ca959aUL, 0xfe0cd112UL }, /* 324 */
  { 0x00c9f5c8UL, 0xfecef3feUL }, /* 325 */
  { 0x00c956f2UL, 0x63fdacd2UL }, /* 326 */
  { 0x00c8b914UL, 0xde44016aUL }, /* 327 */
  { 0x00c81c2eUL, 0x2585e70cUL }, /* 328 */
  { 0x00c7803bUL, 0xf8c41e00UL }, /* 329 */
  { 0x00c6e53cUL, 0x1e009051UL }, /* 330 */
  { 0x00c64b2cUL, 0x62233109UL }, /* 331 */
  { 0x00c5b20aUL, 0x98df5909UL }, /* 332 */
  { 0x00c519d4UL, 0x9c999f04UL }, /* 333 */
  { 0x00c48288UL, 0x4e4e27e1UL }, /* 334 */
  { 0x00c3ec23UL, 0x95776d19UL }, /* 335 */
  { 0x00c356a4UL, 0x5ff57693UL }, /* 336 */
  { 0x00c2c208UL, 0xa1f5858fUL }, /* 337 */
  { 0x00c22e4eUL, 0x55da2e52UL }, /* 338 */
  { 0x00c19b73UL, 0x7c23de47UL }, /* 339 */
  { 0x00c10976UL, 0x1b59cc53UL }, /* 340 */
  { 0x00c07854UL, 0x3ff35133UL }, /* 341 */
  { 0x00bfe80bUL, 0xfc41a5bfUL }, /* 342 */
  { 0x00bf589bUL, 0x685a04fcUL }, /* 343 */
  { 0x00beca00UL, 0xa2002ffaUL }, /* 344 */
  { 0x00be3c39UL, 0xcc91517aUL }, /* 345 */
  { 0x00bdaf45UL, 0x10ef3f6eUL }, /* 346 */
  { 0x00bd2320UL, 0x9d6c1870UL }, /* 347 */
  { 0x00bc97caUL, 0xa5b63b53UL }, /* 348 */
  { 0x00bc0d41UL, 0x62c49700UL }, /* 349 */
  { 0x00bb8383UL, 0x12c350d7UL }, /* 350 */
  { 0x00bafa8dUL, 0xf900bfe2UL }, /* 351 */
  { 0x00ba7260UL, 0x5ddabb1eUL }, /* 352 */
  { 0x00b9eaf8UL, 0x8eac393cUL }, /* 353 */
  { 0x00b96454UL, 0xddbb403dUL }, /* 354 */
  { 0x00b8de73UL, 0xa2272350UL }, /* 355 */
  { 0x00b85953UL, 0x37d70d6aUL }, /* 356 */
  { 0x00b7d4f1UL, 0xff68d718UL }, /* 357 */
  { 0x00b7514eUL, 0x5e202613UL }, /* 358 */
  { 0x00b6ce66UL, 0xbdd5d51fUL }, /* 359 */
  { 0x00b64c39UL, 0x8ce7a2d1UL }, /* 360 */
  { 0x00b5cac5UL, 0x3e2825e0UL }, /* 361 */
  { 0x00b54a08UL, 0x48cf059cUL }, /* 362 */
  { 0x00b4ca01UL, 0x2869754aUL }, /* 363 */
  { 0x00b44aaeUL, 0x5ccaf10aUL }, /* 364 */
  { 0x00b3cc0eUL, 0x69fe3b14UL }, /* 365 */
  { 0x00b34e1fUL, 0xd836980bUL }, /* 366 */
  { 0x00b2d0e1UL, 0x33c14926UL }, /* 367 */
  { 0x00b25451UL, 0x0cf7431dUL }, /* 368 */
  { 0x00b1d86dUL, 0xf82f2091UL }, /* 369 */
  { 0x00b15d36UL, 0x8daf4eecUL }, /* 370 */
  { 0x00b0e2a9UL, 0x69a07488UL }, /* 371 */
  { 0x00b068c5UL, 0x2c000f1aUL }, /* 372 */
  { 0x00afef88UL, 0x78934944UL }, /* 373 */
  { 0x00af76f1UL, 0xf6da064eUL }, /* 374 */
  { 0x00aeff00UL, 0x52022303UL }, /* 375 */
  { 0x00ae87b2UL, 0x38daeaafUL }, /* 376 */
  { 0x00ae1106UL, 0x5dc8bf49UL }, /* 377 */
  { 0x00ad9afbUL, 0x76b8f3d9UL }, /* 378 */
  { 0x00ad2590UL, 0x3d15d827UL }, /* 379 */
  { 0x00acb0c3UL, 0x6dbaf4caUL }, /* 380 */
  { 0x00ac3c93UL, 0xc8e976bdUL }, /* 381 */
  { 0x00abc900UL, 0x123cc989UL }, /* 382 */
  { 0x00ab5607UL, 0x109f5f40UL }, /* 383 */
  { 0x00aae3a7UL, 0x8e3fa55cUL }, /* 384 */
  { 0x00aa71e0UL, 0x588525c5UL }, /* 385 */
  { 0x00aa00b0UL, 0x4005d320UL }, /* 386 */
  { 0x00a99016UL, 0x187b7fabUL }, /* 387 */
  { 0x00a92010UL, 0xb8b97ddbUL }, /* 388 */
  { 0x00a8b09eUL, 0xfaa269fbUL }, /* 389 */
  { 0x00a841bfUL, 0xbb1e1c15UL }, /* 390 */
  { 0x00a7d371UL, 0xda0fc15fUL }, /* 391 */
  { 0x00a765b4UL, 0x3a4c1c8fUL }, /* 392 */
  { 0x00a6f885UL, 0xc18fec42UL }, /* 393 */
  { 0x00a68be5UL, 0x587676f1UL }, /* 394 */
  { 0x00a61fd1UL, 0xea703ba4UL }, /* 395 */
  { 0x00a5b44aUL, 0x65b9c6d4UL }, /* 396 */
  { 0x00a5494dUL, 0xbb52aad5UL }, /* 397 */
  { 0x00a4dedaUL, 0xdef49b1cUL }, /* 398 */
  { 0x00a474f0UL, 0xc70aa9d1UL }, /* 399 */
  { 0x00a40b8eUL, 0x6ca8a711UL }, /* 400 */
  { 0x00a3a2b2UL, 0xcb82a145UL }, /* 401 */
  { 0x00a33a5cUL, 0xe1e485f8UL }, /* 402 */
  { 0x00a2d28bUL, 0xb0a9e2a7UL }, /* 403 */
  { 0x00a26b3eUL, 0x3b35c4f2UL }, /* 404 */
  { 0x00a20473UL, 0x876ab9a5UL }, /* 405 */
  { 0x00a19e2aUL, 0x9da2ea15UL }, /* 406 */
  { 0x00a13862UL, 0x88a8574bUL }, /* 407 */
  { 0x00a0d31aUL, 0x55ad326fUL }, /* 408 */
  { 0x00a06e51UL, 0x14445206UL }, /* 409 */
  { 0x00a00a05UL, 0xd659c371UL }, /* 410 */
  { 0x009fa637UL, 0xb02b7840UL }, /* 411 */
  { 0x009f42e5UL, 0xb8420edbUL }, /* 412 */
  { 0x009ee00fUL, 0x0769b604UL }, /* 413 */
  { 0x009e7db2UL, 0xb8ab2accUL }, /* 414 */
  { 0x009e1bcfUL, 0xe944d079UL }, /* 415 */
  { 0x009dba65UL, 0xb8a3e1f7UL }, /* 416 */
  { 0x009d5973UL, 0x485dbc67UL }, /* 417 */
  { 0x009cf8f7UL, 0xbc294251UL }, /* 418 */
  { 0x009c98f2UL, 0x39d8572cUL }, /* 419 */
  { 0x009c3961UL, 0xe95172b5UL }, /* 420 */
  { 0x009bda45UL, 0xf4894bc2UL }, /* 421 */
  { 0x009b7b9dUL, 0x877c9a2dUL }, /* 422 */
  { 0x009b1d67UL, 0xd029ef6aUL }, /* 423 */
  { 0x009abfa3UL, 0xfe8ba574UL }, /* 424 */
  { 0x009a6251UL, 0x4491e3acUL }, /* 425 */
  { 0x009a056eUL, 0xd61cb94fUL }, /* 426 */
  { 0x0099a8fbUL, 0xe8f64d25UL }, /* 427 */
  { 0x00994cf7UL, 0xb4cd221eUL }, /* 428 */
  { 0x0098f161UL, 0x732e706eUL }, /* 429 */
  { 0x00989638UL, 0x5f8092e8UL }, /* 430 */
  { 0x00983b7bUL, 0xb6fd883aUL }, /* 431 */
  { 0x0097e12aUL, 0xb8ad87b2UL }, /* 432 */
  { 0x00978744UL, 0xa561a94fUL }, /* 433 */
  { 0x00972dc8UL, 0xbfaea0b3UL }, /* 434 */
  { 0x0096d4b6UL, 0x4be78abeUL }, /* 435 */
  { 0x00967c0cUL, 0x9018cd7aUL }, /* 436 */
  { 0x009623caUL, 0xd4030a15UL }, /* 437 */
  { 0x0095cbf0UL, 0x61162094UL }, /* 438 */
  { 0x0095747cUL, 0x826c44fdUL }, /* 439 */
  { 0x00951d6eUL, 0x84c525b8UL }, /* 440 */
  { 0x0094c6c5UL, 0xb68122d0UL }, /* 441 */
  { 0x00947081UL, 0x679c95e1UL }, /* 442 */
  { 0x00941aa0UL, 0xe9ab2a5fUL }, /* 443 */
  { 0x0093c523UL, 0x8fd34605UL }, /* 444 */
  { 0x00937008UL, 0xaec9811dUL }, /* 445 */
  { 0x00931b4fUL, 0x9ccc2e65UL }, /* 446 */
  { 0x0092c6f7UL, 0xb19ef25eUL }, /* 447 */
  { 0x00927300UL, 0x468669adUL }, /* 448 */
  { 0x00921f68UL, 0xb643de71UL }, /* 449 */
  { 0x0091cc30UL, 0x5d110c3aUL }, /* 450 */
  { 0x00917956UL, 0x989bf274UL }, /* 451 */
  { 0x009126daUL, 0xc802b508UL }, /* 452 */
  { 0x0090d4bcUL, 0x4bcf8afaUL }, /* 453 */
  { 0x009082faUL, 0x85f4bacfUL }, /* 454 */
  { 0x00903194UL, 0xd9c8a489UL }, /* 455 */
  { 0x008fe08aUL, 0xac01d8f2UL }, /* 456 */
  { 0x008f8fdbUL, 0x62b33e1cUL }, /* 457 */
  { 0x008f3f86UL, 0x654840cbUL }, /* 458 */
  { 0x008eef8bUL, 0x1c8112a3UL }, /* 459 */
  { 0x008e9fe8UL, 0xf26ef4e6UL }, /* 460 */
  { 0x008e509fUL, 0x52708f92UL }, /* 461 */
  { 0x008e01adUL, 0xa92e54a5UL }, /* 462 */
  { 0x008db313UL, 0x6496ef6aUL }, /* 463 */
  { 0x008d64cfUL, 0xf3dbbf91UL }, /* 464 */
  { 0x008d16e2UL, 0xc76d5fedUL }, /* 465 */
  { 0x008cc94bUL, 0x50f838a5UL }, /* 466 */
  { 0x008c7c09UL, 0x03611cb1UL }, /* 467 */
  { 0x008c2f1bUL, 0x52c1f277UL }, /* 468 */
  { 0x008be281UL, 0xb466675bUL }, /* 469 */
  { 0x008b963bUL, 0x9ec8ae1cUL }, /* 470 */
  { 0x008b4a48UL, 0x898e47ceUL }, /* 471 */
  { 0x008afea7UL, 0xed84d751UL }, /* 472 */
  { 0x008ab359UL, 0x449eff17UL }, /* 473 */
  { 0x008a685cUL, 0x09f14918UL }, /* 474 */
  { 0x008a1dafUL, 0xb9af18c5UL }, /* 475 */
  { 0x0089d353UL, 0xd127a6dfUL }, /* 476 */
  { 0x00898947UL, 0xcec306ffUL }, /* 477 */
  { 0x00893f8bUL, 0x31ff36bcUL }, /* 478 */
  { 0x0088f61dUL, 0x7b6d363aUL }, /* 479 */
  { 0x0088acfeUL, 0x2cae2a0eUL }, /* 480 */
  { 0x0088642cUL, 0xc8708642UL }, /* 481 */
  { 0x00881ba8UL, 0xd26d426eUL }, /* 482 */
  { 0x0087d371UL, 0xcf6516aeUL }, /* 483 */
  { 0x00878b87UL, 0x451dc166UL }, /* 484 */
  { 0x008743e8UL, 0xba5f55a5UL }, /* 485 */
  { 0x0086fc95UL, 0xb6f19211UL }, /* 486 */
  { 0x0086b58dUL, 0xc399403bUL }, /* 487 */
  { 0x00866ed0UL, 0x6a159c37UL }, /* 488 */
  { 0x0086285dUL, 0x351dc45eUL }, /* 489 */
  { 0x0085e233UL, 0xb05e311cUL }, /* 490 */
  { 0x00859c53UL, 0x687634aeUL }, /* 491 */
  { 0x008556bbUL, 0xeaf582acUL }, /* 492 */
  { 0x0085116cUL, 0xc659bf49UL }, /* 493 */
  { 0x0084cc65UL, 0x8a0c1630UL }, /* 494 */
  { 0x008487a5UL, 0xc65ed8d8UL }, /* 495 */
  { 0x0084432dUL, 0x0c8b243dUL }, /* 496 */
  { 0x0083fefaUL, 0xeeae8de1UL }, /* 497 */
  { 0x0083bb0eUL, 0xffc8d7f4UL }, /* 498 */
  { 0x00837768UL, 0xd3b9ac9bUL }, /* 499 */
  { 0x00833407UL, 0xff3e6025UL }, /* 500 */
  { 0x0082f0ecUL, 0x17efba24UL }, /* 501 */
  { 0x0082ae14UL, 0xb43fc557UL }, /* 502 */
  { 0x00826b81UL, 0x6b77a636UL }, /* 503 */
  { 0x00822931UL, 0xd5b5781fUL }, /* 504 */
  { 0x0081e725UL, 0x8bea3106UL }, /* 505 */
  { 0x0081a55cUL, 0x27d78b88UL }, /* 506 */
  { 0x008163d5UL, 0x440df752UL }, /* 507 */
  { 0x00812290UL, 0x7bea8fcdUL }, /* 508 */
  { 0x0080e18dUL, 0x6b9518e4UL }, /* 509 */
  { 0x0080a0cbUL, 0xaffe01e9UL }, /* 510 */
  { 0x0080604aUL, 0xe6dc6e69UL }, /* 511 */
  { 0x0080200aUL, 0xaeac44efUL }, /* 512 */
  { 0x007fe00aUL, 0xa6ac439aUL }, /* 513 */
  { 0x007fa04aUL, 0x6edc1a68UL }, /* 514 */
  { 0x007f60c9UL, 0xa7fa8b38UL }, /* 515 */
  { 0x007f2187UL, 0xf3838f55UL }, /* 516 */
  { 0x007ee284UL, 0xf3ae828fUL }, /* 517 */
  { 0x007ea3c0UL, 0x4b6c53bdUL }, /* 518 */
  { 0x007e6539UL, 0x9e65ba9dUL }, /* 519 */
  { 0x007e26f0UL, 0x90f97302UL }, /* 520 */
  { 0x007de8e4UL, 0xc83a7d37UL }, /* 521 */
  { 0x007dab15UL, 0xe9ee638aUL }, /* 522 */
  { 0x007d6d83UL, 0x9c8b84e9UL }, /* 523 */
  { 0x007d302dUL, 0x87376485UL }, /* 524 */
  { 0x007cf313UL, 0x51c4fe69UL }, /* 525 */
  { 0x007cb634UL, 0xa4b320ecUL }, /* 526 */
  { 0x007c7991UL, 0x292acafaUL }, /* 527 */
  { 0x007c3d28UL, 0x88fd8f1bUL }, /* 528 */
  { 0x007c00faUL, 0x6ea3fb28UL }, /* 529 */
  { 0x007bc506UL, 0x853c04a6UL }, /* 530 */
  { 0x007b894cUL, 0x788779abUL }, /* 531 */
  { 0x007b4dcbUL, 0xf4ea7648UL }, /* 532 */
  { 0x007b1284UL, 0xa769de68UL }, /* 533 */
  { 0x007ad776UL, 0x3da9dc0aUL }, /* 534 */
  { 0x007a9ca0UL, 0x65ec61d9UL }, /* 535 */
  { 0x007a6202UL, 0xcf0fb204UL }, /* 536 */
  { 0x007a279dUL, 0x288ce94eUL }, /* 537 */
  { 0x0079ed6fUL, 0x22768e4dUL }, /* 538 */
  { 0x0079b378UL, 0x6d7724bdUL }, /* 539 */
  { 0x007979b8UL, 0xbacfc4e6UL }, /* 540 */
  { 0x0079402fUL, 0xbc56b705UL }, /* 541 */
  { 0x007906ddUL, 0x247612a3UL }, /* 542 */
  { 0x0078cdc0UL, 0xa62a61d4UL }, /* 543 */
  { 0x007894d9UL, 0xf5014854UL }, /* 544 */
  { 0x00785c28UL, 0xc5182e6bUL }, /* 545 */
  { 0x007823acUL, 0xcb1aef94UL }, /* 546 */
  { 0x0077eb65UL, 0xbc428cdbUL }, /* 547 */
  { 0x0077b353UL, 0x4e53e2ddUL }, /* 548 */
  { 0x00777b75UL, 0x379e6367UL }, /* 549 */
  { 0x007743cbUL, 0x2efad2aaUL }, /* 550 */
  { 0x00770c54UL, 0xebca07eaUL }, /* 551 */
  { 0x0076d512UL, 0x25f3b1b0UL }, /* 552 */
  { 0x00769e02UL, 0x95e51d63UL }, /* 553 */
  { 0x00766725UL, 0xf4900247UL }, /* 554 */
  { 0x0076307bUL, 0xfb694fd4UL }, /* 555 */
  { 0x0075fa04UL, 0x6467ff5aUL }, /* 556 */
  { 0x0075c3beUL, 0xea03e8e6UL }, /* 557 */
  { 0x00758dabUL, 0x47349b63UL }, /* 558 */
  { 0x007557c9UL, 0x377037e0UL }, /* 559 */
  { 0x00752218UL, 0x76aa500aUL }, /* 560 */
  { 0x0074ec98UL, 0xc152c7a8UL }, /* 561 */
  { 0x0074b749UL, 0xd454b940UL }, /* 562 */
  { 0x0074822bUL, 0x6d155dafUL }, /* 563 */
  { 0x00744d3dUL, 0x4972f6c3UL }, /* 564 */
  { 0x0074187fUL, 0x27c3bcceUL }, /* 565 */
  { 0x0073e3f0UL, 0xc6d4cf18UL }, /* 566 */
  { 0x0073af91UL, 0xe5e9273cUL }, /* 567 */
  { 0x00737b62UL, 0x44b88f4eUL }, /* 568 */
  { 0x00734761UL, 0xa36e9ae0UL }, /* 569 */
  { 0x0073138fUL, 0xc2a9a2c3UL }, /* 570 */
  { 0x0072dfecUL, 0x6379c37fUL }, /* 571 */
  { 0x0072ac77UL, 0x475fde8fUL }, /* 572 */
  { 0x00727930UL, 0x304c9e38UL }, /* 573 */
  { 0x00724616UL, 0xe09f7c11UL }, /* 574 */
  { 0x0072132bUL, 0x1b25ca1aUL }, /* 575 */
  { 0x0071e06cUL, 0xa319be72UL }, /* 576 */
  { 0x0071addbUL, 0x3c21818bUL }, /* 577 */
  { 0x00717b76UL, 0xaa4e3eedUL }, /* 578 */
  { 0x0071493eUL, 0xb21b386eUL }, /* 579 */
  { 0x00711733UL, 0x186cdbdbUL }, /* 580 */
  { 0x0070e553UL, 0xa28fdb17UL }, /* 581 */
  { 0x0070b3a0UL, 0x16384693UL }, /* 582 */
  { 0x00708218UL, 0x3980aa29UL }, /* 583 */
  { 0x007050bbUL, 0xd2e92c50UL }, /* 584 */
  { 0x00701f8aUL, 0xa956af97UL }, /* 585 */
  { 0x006fee84UL, 0x8411f66dUL }, /* 586 */
  { 0x006fbda9UL, 0x2ac6c92cUL }, /* 587 */
  { 0x006f8cf8UL, 0x65831e55UL }, /* 588 */
  { 0x006f5c71UL, 0xfcb64507UL }, /* 589 */
  { 0x006f2c15UL, 0xb93011a2UL }, /* 590 */
  { 0x006efbe3UL, 0x64200c8bUL }, /* 591 */
  { 0x006ecbdaUL, 0xc714a313UL }, /* 592 */
  { 0x006e9bfbUL, 0xabfa5a76UL }, /* 593 */
  { 0x006e6c45UL, 0xdd1b04efUL }, /* 594 */
  { 0x006e3cb9UL, 0x251cf8cfUL }, /* 595 */
  { 0x006e0d55UL, 0x4f0249abUL }, /* 596 */
  { 0x006dde1aUL, 0x26280376UL }, /* 597 */
  { 0x006daf07UL, 0x764567a7UL }, /* 598 */
  { 0x006d801dUL, 0x0b6b2c47UL }, /* 599 */
  { 0x006d515aUL, 0xb202bcf3UL }, /* 600 */
  { 0x006d22c0UL, 0x36cd7dc5UL }, /* 601 */
  { 0x006cf44dUL, 0x66e41020UL }, /* 602 */
  { 0x006cc602UL, 0x0fb59959UL }, /* 603 */
  { 0x006c97ddUL, 0xff070b39UL }, /* 604 */
  { 0x006c69e1UL, 0x02f26e4fUL }, /* 605 */
  { 0x006c3c0aUL, 0xe9e62e12UL }, /* 606 */
  { 0x006c0e5bUL, 0x82a466c5UL }, /* 607 */
  { 0x006be0d2UL, 0x9c423525UL }, /* 608 */
  { 0x006bb370UL, 0x062707cfUL }, /* 609 */
  { 0x006b8633UL, 0x900bf25dUL }, /* 610 */
  { 0x006b591dUL, 0x09fb023bUL }, /* 611 */
  { 0x006b2c2cUL, 0x444e9522UL }, /* 612 */
  { 0x006aff61UL, 0x0fb0b14aUL }, /* 613 */
  { 0x006ad2bbUL, 0x3d1a5f2eUL }, /* 614 */
  { 0x006aa63aUL, 0x9dd304ffUL }, /* 615 */
  { 0x006a79dfUL, 0x036fc3a6UL }, /* 616 */
  { 0x006a4da8UL, 0x3fd2d567UL }, /* 617 */
  { 0x006a2196UL, 0x252aee0eUL }, /* 618 */
  { 0x0069f5a8UL, 0x85f29ca8UL }, /* 619 */
  { 0x0069c9dfUL, 0x34efaecbUL }, /* 620 */
  { 0x00699e3aUL, 0x0532955cUL }, /* 621 */
  { 0x006972b8UL, 0xca15cad8UL }, /* 622 */
  { 0x0069475bUL, 0x573d3b16UL }, /* 623 */
  { 0x00691c21UL, 0x8095ac82UL }, /* 624 */
  { 0x0068f10bUL, 0x1a542ac9UL }, /* 625 */
  { 0x0068c617UL, 0xf8f572f6UL }, /* 626 */
  { 0x00689b47UL, 0xf13d60f8UL }, /* 627 */
  { 0x0068709aUL, 0xd8365e8eUL }, /* 628 */
  { 0x00684610UL, 0x8330d396UL }, /* 629 */
  { 0x00681ba8UL, 0xc7c297bdUL }, /* 630 */
  { 0x0067f163UL, 0x7bc66585UL }, /* 631 */
  { 0x0067c740UL, 0x755b4ea7UL }, /* 632 */
  { 0x00679d3fUL, 0x8ae431c4UL }, /* 633 */
  { 0x00677360UL, 0x93073168UL }, /* 634 */
  { 0x006749a3UL, 0x64ad2c56UL }, /* 635 */
  { 0x00672007UL, 0xd7013721UL }, /* 636 */
  { 0x0066f68dUL, 0xc1701701UL }, /* 637 */
  { 0x0066cd34UL, 0xfba7bdf1UL }, /* 638 */
  { 0x0066a3fdUL, 0x5d96c809UL }, /* 639 */
  { 0x00667ae6UL, 0xbf6bfa09UL }, /* 640 */
  { 0x006651f0UL, 0xf995c12aUL }, /* 641 */
  { 0x0066291bUL, 0xe4c1b410UL }, /* 642 */
  { 0x00660067UL, 0x59dc14feUL }, /* 643 */
  { 0x0065d7d3UL, 0x320f5527UL }, /* 644 */
  { 0x0065af5fUL, 0x46c39931UL }, /* 645 */
  { 0x0065870bUL, 0x719e3edaUL }, /* 646 */
  { 0x00655ed7UL, 0x8c8163c1UL }, /* 647 */
  { 0x006536c3UL, 0x718b6d51UL }, /* 648 */
  { 0x00650eceUL, 0xfb1691c3UL }, /* 649 */
  { 0x0064e6faUL, 0x03b8623cUL }, /* 650 */
  { 0x0064bf44UL, 0x66415604UL }, /* 651 */
  { 0x006497adUL, 0xfdbc56ceUL }, /* 652 */
  { 0x00647036UL, 0xa56e4e0dUL }, /* 653 */
  { 0x006448deUL, 0x38d5b35dUL }, /* 654 */
  { 0x006421a4UL, 0x93aa1bf4UL }, /* 655 */
  { 0x0063fa89UL, 0x91dbcb18UL }, /* 656 */
  { 0x0063d38dUL, 0x0f93439cUL }, /* 657 */
  { 0x0063acaeUL, 0xe930da63UL }, /* 658 */
  { 0x006385eeUL, 0xfb4c49daUL }, /* 659 */
  { 0x00635f4dUL, 0x22b44677UL }, /* 660 */
  { 0x006338c9UL, 0x3c6e142eUL }, /* 661 */
  { 0x00631263UL, 0x25b51cdbUL }, /* 662 */
  { 0x0062ec1aUL, 0xbbfa87a9UL }, /* 663 */
  { 0x0062c5efUL, 0xdce4d161UL }, /* 664 */
  { 0x00629fe2UL, 0x664f65afUL }, /* 665 */
  { 0x006279f2UL, 0x364a3955UL }, /* 666 */
  { 0x0062541fUL, 0x2b196543UL }, /* 667 */
  { 0x00622e69UL, 0x2334c29dUL }, /* 668 */
  { 0x006208cfUL, 0xfd4787a1UL }, /* 669 */
  { 0x0061e353UL, 0x982fe578UL }, /* 670 */
  { 0x0061bdf3UL, 0xd2fea6deUL }, /* 671 */
  { 0x006198b0UL, 0x8cf6cfb5UL }, /* 672 */
  { 0x00617389UL, 0xa58d3d66UL }, /* 673 */
  { 0x00614e7eUL, 0xfc684829UL }, /* 674 */
  { 0x00612990UL, 0x715f651cUL }, /* 675 */
  { 0x006104bdUL, 0xe47ac936UL }, /* 676 */
  { 0x0060e007UL, 0x35f30d04UL }, /* 677 */
  { 0x0060bb6cUL, 0x4630d143UL }, /* 678 */
  { 0x006096ecUL, 0xf5cc6443UL }, /* 679 */
  { 0x00607289UL, 0x258d6810UL }, /* 680 */
  { 0x00604e40UL, 0xb66a7976UL }, /* 681 */
  { 0x00602a13UL, 0x8988d7bdUL }, /* 682 */
  { 0x00600601UL, 0x803c0d36UL }, /* 683 */
  { 0x005fe20aUL, 0x7c059889UL }, /* 684 */
  { 0x005fbe2eUL, 0x5e9496c6UL }, /* 685 */
  { 0x005f9a6dUL, 0x09c56e36UL }, /* 686 */
  { 0x005f76c6UL, 0x5fa179ebUL }, /* 687 */
  { 0x005f533aUL, 0x425eb60fUL }, /* 688 */
  { 0x005f2fc8UL, 0x945f6ce6UL }, /* 689 */
  { 0x005f0c71UL, 0x3831e494UL }, /* 690 */
  { 0x005ee934UL, 0x10900d93UL }, /* 691 */
  { 0x005ec611UL, 0x005f31dbUL }, /* 692 */
  { 0x005ea307UL, 0xeaafa4c9UL }, /* 693 */
  { 0x005e8018UL, 0xb2bc73a8UL }, /* 694 */
  { 0x005e5d43UL, 0x3beb16f8UL }, /* 695 */
  { 0x005e3a87UL, 0x69cb245bUL }, /* 696 */
  { 0x005e17e5UL, 0x2016012fUL }, /* 697 */
  { 0x005df55cUL, 0x42ae95d1UL }, /* 698 */
  { 0x005dd2ecUL, 0xb5a10192UL }, /* 699 */
  { 0x005db096UL, 0x5d224f45UL }, /* 700 */
  { 0x005d8e59UL, 0x1d902a7cUL }, /* 701 */
  { 0x005d6c34UL, 0xdb709567UL }, /* 702 */
  { 0x005d4a29UL, 0x7b719f53UL }, /* 703 */
  { 0x005d2836UL, 0xe2691bcbUL }, /* 704 */
  { 0x005d065cUL, 0xf5545a5aUL }, /* 705 */
  { 0x005ce49bUL, 0x9957dee2UL }, /* 706 */
  { 0x005cc2f2UL, 0xb3bf1a9bUL }, /* 707 */
  { 0x005ca162UL, 0x29fc25a2UL }, /* 708 */
  { 0x005c7fe9UL, 0xe1a77926UL }, /* 709 */
  { 0x005c5e89UL, 0xc07faa2aUL }, /* 710 */
  { 0x005c3d41UL, 0xac6924deUL }, /* 711 */
  { 0x005c1c11UL, 0x8b6de88cUL }, /* 712 */
  { 0x005bfaf9UL, 0x43bd4414UL }, /* 713 */
  { 0x005bd9f8UL, 0xbbab9304UL }, /* 714 */
  { 0x005bb90fUL, 0xd9b1fb2fUL }, /* 715 */
  { 0x005b983eUL, 0x846e2ae4UL }, /* 716 */
  { 0x005b7784UL, 0xa2a217a7UL }, /* 717 */
  { 0x005b56e2UL, 0x1b33bd78UL }, /* 718 */
  { 0x005b3656UL, 0xd52cdea6UL }, /* 719 */
  { 0x005b15e2UL, 0xb7bac42bUL }, /* 720 */
  { 0x005af585UL, 0xaa2dfe8fUL }, /* 721 */
  { 0x005ad53fUL, 0x93fa2750UL }, /* 722 */
  { 0x005ab510UL, 0x5cb5a2d1UL }, /* 723 */
  { 0x005a94f7UL, 0xec1962cbUL }, /* 724 */
  { 0x005a74f6UL, 0x2a00a945UL }, /* 725 */
  { 0x005a550aUL, 0xfe68cc05UL }, /* 726 */
  { 0x005a3536UL, 0x5170f88aUL }, /* 727 */
  { 0x005a1578UL, 0x0b59f87fUL }, /* 728 */
  { 0x0059f5d0UL, 0x1485f6adUL }, /* 729 */
  { 0x0059d63eUL, 0x55784468UL }, /* 730 */
  { 0x0059b6c2UL, 0xb6d51f7cUL }, /* 731 */
  { 0x0059975dUL, 0x2161788fUL }, /* 732 */
  { 0x0059780dUL, 0x7e02b9fcUL }, /* 733 */
  { 0x005958d3UL, 0xb5be8f2aUL }, /* 734 */
  { 0x005939afUL, 0xb1baac54UL }, /* 735 */
  { 0x00591aa1UL, 0x5b3c96c9UL }, /* 736 */
  { 0x0058fba8UL, 0x9ba96da4UL }, /* 737 */
  { 0x0058dcc5UL, 0x5c85b2edUL }, /* 738 */
  { 0x0058bdf7UL, 0x8775153aUL }, /* 739 */
  { 0x00589f3fUL, 0x063a39b2UL }, /* 740 */
  { 0x0058809bUL, 0xc2b6868dUL }, /* 741 */
  { 0x0058620dUL, 0xa6e9edfbUL }, /* 742 */
  { 0x00584394UL, 0x9cf2b97bUL }, /* 743 */
  { 0x00582530UL, 0x8f0d559fUL }, /* 744 */
  { 0x005806e1UL, 0x67941e3dUL }, /* 745 */
  { 0x0057e8a7UL, 0x10ff2b07UL }, /* 746 */
  { 0x0057ca81UL, 0x75e41c91UL }, /* 747 */
  { 0x0057ac70UL, 0x80f5e9bdUL }, /* 748 */
  { 0x00578e74UL, 0x1d04ad91UL }, /* 749 */
  { 0x0057708cUL, 0x34fd7572UL }, /* 750 */
  { 0x005752b8UL, 0xb3ea0fc2UL }, /* 751 */
  { 0x005734f9UL, 0x84f0daedUL }, /* 752 */
  { 0x0057174eUL, 0x935494cdUL }, /* 753 */
  { 0x0056f9b7UL, 0xca742a7cUL }, /* 754 */
  { 0x0056dc35UL, 0x15ca887fUL }, /* 755 */
  { 0x0056bec6UL, 0x60ee6b5aUL }, /* 756 */
  { 0x0056a16bUL, 0x97923081UL }, /* 757 */
  { 0x00568424UL, 0xa583a7a6UL }, /* 758 */
  { 0x005666f1UL, 0x76abe468UL }, /* 759 */
  { 0x005649d1UL, 0xf70f1062UL }, /* 760 */
  { 0x00562cc6UL, 0x12cc3d95UL }, /* 761 */
  { 0x00560fcdUL, 0xb61d3928UL }, /* 762 */
  { 0x0055f2e8UL, 0xcd565e8dUL }, /* 763 */
  { 0x0055d617UL, 0x44e66afcUL }, /* 764 */
  { 0x0055b959UL, 0x09565144UL }, /* 765 */
  { 0x00559caeUL, 0x07490dfcUL }, /* 766 */
  { 0x00558016UL, 0x2b7b7c01UL }, /* 767 */
  { 0x00556391UL, 0x62c4295bUL }, /* 768 */
  { 0x0055471fUL, 0x9a132c66UL }, /* 769 */
  { 0x00552ac0UL, 0xbe71f95fUL }, /* 770 */
  { 0x00550e74UL, 0xbd03383dUL }, /* 771 */
  { 0x0054f23bUL, 0x83029ae3UL }, /* 772 */
  { 0x0054d614UL, 0xfdc4b3a1UL }, /* 773 */
  { 0x0054ba01UL, 0x1ab6cc0aUL }, /* 774 */
  { 0x00549dffUL, 0xc75ebc1eUL }, /* 775 */
  { 0x00548210UL, 0xf15ac1bdUL }, /* 776 */
  { 0x00546634UL, 0x86615871UL }, /* 777 */
  { 0x00544a6aUL, 0x7441118aUL }, /* 778 */
  { 0x00542eb2UL, 0xa8e06c7bUL }, /* 779 */
  { 0x0054130dUL, 0x123daf99UL }, /* 780 */
  { 0x0053f779UL, 0x9e6ec114UL }, /* 781 */
  { 0x0053dbf8UL, 0x3ba1004bUL }, /* 782 */
  { 0x0053c088UL, 0xd8191f62UL }, /* 783 */
  { 0x0053a52bUL, 0x6232fd2cUL }, /* 784 */
  { 0x005389dfUL, 0xc8617f58UL }, /* 785 */
  { 0x00536ea5UL, 0xf92e6ceaUL }, /* 786 */
  { 0x0053537dUL, 0xe33a4905UL }, /* 787 */
  { 0x00533867UL, 0x753c2dedUL }, /* 788 */
  { 0x00531d62UL, 0x9e01a864UL }, /* 789 */
  { 0x0053026fUL, 0x4c6e9340UL }, /* 790 */
  { 0x0052e78dUL, 0x6f7cf351UL }, /* 791 */
  { 0x0052ccbcUL, 0xf63cd384UL }, /* 792 */
  { 0x0052b1fdUL, 0xcfd42155UL }, /* 793 */
  { 0x0052974fUL, 0xeb7e8980UL }, /* 794 */
  { 0x00527cb3UL, 0x388d54f9UL }, /* 795 */
  { 0x00526227UL, 0xa6674623UL }, /* 796 */
  { 0x005247adUL, 0x24887650UL }, /* 797 */
  { 0x00522d43UL, 0xa2823381UL }, /* 798 */
  { 0x005212ebUL, 0x0ffade68UL }, /* 799 */
  { 0x0051f8a3UL, 0x5cadc8a9UL }, /* 800 */
  { 0x0051de6cUL, 0x786b1360UL }, /* 801 */
  { 0x0051c446UL, 0x53178de5UL }, /* 802 */
  { 0x0051aa30UL, 0xdcac94ccUL }, /* 803 */
  { 0x0051902cUL, 0x0537f12dUL }, /* 804 */
  { 0x00517637UL, 0xbcdbb821UL }, /* 805 */
  { 0x00515c53UL, 0xf3ce2a86UL }, /* 806 */
  { 0x00514280UL, 0x9a5994f4UL }, /* 807 */
  { 0x005128bdUL, 0xa0dc2ffeUL }, /* 808 */
  { 0x00510f0aUL, 0xf7c800a3UL }, /* 809 */
  { 0x0050f568UL, 0x8fa2b902UL }, /* 810 */
  { 0x0050dbd6UL, 0x59059946UL }, /* 811 */
  { 0x0050c254UL, 0x449d50cfUL }, /* 812 */
  { 0x0050a8e2UL, 0x4329df93UL }, /* 813 */
  { 0x00508f80UL, 0x457e77b8UL }, /* 814 */
  { 0x0050762eUL, 0x3c815f70UL }, /* 815 */
  { 0x00505cecUL, 0x192bd2ffUL }, /* 816 */
  { 0x005043b9UL, 0xcc89e706UL }, /* 817 */
  { 0x00502a97UL, 0x47ba6b00UL }, /* 818 */
  { 0x00501184UL, 0x7beecbf9UL }, /* 819 */
  { 0x004ff881UL, 0x5a6af778UL }, /* 820 */
  { 0x004fdf8dUL, 0xd4853ea4UL }, /* 821 */
  { 0x004fc6a9UL, 0xdba6399cUL }, /* 822 */
  { 0x004fadd5UL, 0x6148ab07UL }, /* 823 */
  { 0x004f9510UL, 0x56f963d4UL }, /* 824 */
  { 0x004f7c5aUL, 0xae572736UL }, /* 825 */
  { 0x004f63b4UL, 0x59128eceUL }, /* 826 */
  { 0x004f4b1dUL, 0x48edef0cUL }, /* 827 */
  { 0x004f3295UL, 0x6fbd3bc0UL }, /* 828 */
  { 0x004f1a1cUL, 0xbf65ece5UL }, /* 829 */
  { 0x004f01b3UL, 0x29dee394UL }, /* 830 */
  { 0x004ee958UL, 0xa1304f30UL }, /* 831 */
  { 0x004ed10dUL, 0x177392c7UL }, /* 832 */
  { 0x004eb8d0UL, 0x7ed32a96UL }, /* 833 */
  { 0x004ea0a2UL, 0xc98a91d0UL }, /* 834 */
  { 0x004e8883UL, 0xe9e62887UL }, /* 835 */
  { 0x004e7073UL, 0xd24319caUL }, /* 836 */
  { 0x004e5872UL, 0x750f41f8UL }, /* 837 */
  { 0x004e407fUL, 0xc4c91535UL }, /* 838 */
  { 0x004e289bUL, 0xb3ff861dUL }, /* 839 */
  { 0x004e10c6UL, 0x3551ec99UL }, /* 840 */
  { 0x004df8ffUL, 0x3b6fece9UL }, /* 841 */
  { 0x004de146UL, 0xb9195edaUL }, /* 842 */
  { 0x004dc99cUL, 0xa11e3526UL }, /* 843 */
  { 0x004db200UL, 0xe65e6507UL }, /* 844 */
  { 0x004d9a73UL, 0x7bc9cdf1UL }, /* 845 */
  { 0x004d82f4UL, 0x54602179UL }, /* 846 */
  { 0x004d6b83UL, 0x6330cb6bUL }, /* 847 */
  { 0x004d5420UL, 0x9b5ada09UL }, /* 848 */
  { 0x004d3ccbUL, 0xf00ce672UL }, /* 849 */
  { 0x004d2585UL, 0x5484fd3bUL }, /* 850 */
  { 0x004d0e4cUL, 0xbc10872aUL }, /* 851 */
  { 0x004cf722UL, 0x1a0c3224UL }, /* 852 */
  { 0x004ce005UL, 0x61e3da3bUL }, /* 853 */
  { 0x004cc8f6UL, 0x871272eaUL }, /* 854 */
  { 0x004cb1f5UL, 0x7d21f07eUL }, /* 855 */
  { 0x004c9b02UL, 0x37ab31a0UL }, /* 856 */
  { 0x004c841cUL, 0xaa55e909UL }, /* 857 */
  { 0x004c6d44UL, 0xc8d88765UL }, /* 858 */
  { 0x004c567aUL, 0x86f82553UL }, /* 859 */
  { 0x004c3fbdUL, 0xd8886d95UL }, /* 860 */
  { 0x004c290eUL, 0xb16b8760UL }, /* 861 */
  { 0x004c126dUL, 0x059200d9UL }, /* 862 */
  { 0x004bfbd8UL, 0xc8fab9b5UL }, /* 863 */
  { 0x004be551UL, 0xefb2cdfdUL }, /* 864 */
  { 0x004bced8UL, 0x6dd580fdUL }, /* 865 */
  { 0x004bb86cUL, 0x378c2853UL }, /* 866 */
  { 0x004ba20dUL, 0x410e1728UL }, /* 867 */
  { 0x004b8bbbUL, 0x7ea0898bUL }, /* 868 */
  { 0x004b7576UL, 0xe4968feeUL }, /* 869 */
  { 0x004b5f3fUL, 0x6750facfUL }, /* 870 */
  { 0x004b4914UL, 0xfb3e467cUL }, /* 871 */
  { 0x004b32f7UL, 0x94da86ffUL }, /* 872 */
  { 0x004b1ce7UL, 0x28af5430UL }, /* 873 */
  { 0x004b06e3UL, 0xab53b5e5UL }, /* 874 */
  { 0x004af0edUL, 0x116c1048UL }, /* 875 */
  { 0x004adb03UL, 0x4faa104cUL }, /* 876 */
  { 0x004ac526UL, 0x5acc9848UL }, /* 877 */
  { 0x004aaf56UL, 0x279facb5UL }, /* 878 */
  { 0x004a9992UL, 0xaafc6107UL }, /* 879 */
  { 0x004a83dbUL, 0xd9c8c4b0UL }, /* 880 */
  { 0x004a6e31UL, 0xa8f7d03cUL }, /* 881 */
  { 0x004a5894UL, 0x0d895294UL }, /* 882 */
  { 0x004a4302UL, 0xfc89de5fUL }, /* 883 */
  { 0x004a2d7eUL, 0x6b12b783UL }, /* 884 */
  { 0x004a1806UL, 0x4e49c0c7UL }, /* 885 */
  { 0x004a029aUL, 0x9b616998UL }, /* 886 */
  { 0x0049ed3bUL, 0x47989be4UL }, /* 887 */
  { 0x0049d7e8UL, 0x483aaa21UL }, /* 888 */
  { 0x0049c2a1UL, 0x929f3d67UL }, /* 889 */
  { 0x0049ad67UL, 0x1c2a43b5UL }, /* 890 */
  { 0x00499838UL, 0xda4bde49UL }, /* 891 */
  { 0x00498316UL, 0xc280501dUL }, /* 892 */
  { 0x00496e00UL, 0xca4fec83UL }, /* 893 */
  { 0x004958f6UL, 0xe74f05dbUL }, /* 894 */
  { 0x004943f9UL, 0x0f1ddc68UL }, /* 895 */
  { 0x00492f07UL, 0x37688d45UL }, /* 896 */
  { 0x00491a21UL, 0x55e70171UL }, /* 897 */
  { 0x00490547UL, 0x605cdd00UL }, /* 898 */
  { 0x0048f079UL, 0x4c996e65UL }, /* 899 */
  { 0x0048dbb7UL, 0x10779dd5UL }, /* 900 */
  { 0x0048c700UL, 0xa1dddccbUL }, /* 901 */
  { 0x0048b255UL, 0xf6be15a9UL }, /* 902 */
  { 0x00489db7UL, 0x05159b70UL }, /* 903 */
  { 0x00488923UL, 0xc2ed1998UL }, /* 904 */
  { 0x0048749cUL, 0x265883ffUL }, /* 905 */
  { 0x00486020UL, 0x257706fbUL }, /* 906 */
  { 0x00484bafUL, 0xb672f77cUL }, /* 907 */
  { 0x0048374aUL, 0xcf81c354UL }, /* 908 */
  { 0x004822f1UL, 0x66e3e191UL }, /* 909 */
  { 0x00480ea3UL, 0x72e4c2f8UL }, /* 910 */
  { 0x0047fa60UL, 0xe9dac293UL }, /* 911 */
  { 0x0047e629UL, 0xc2271660UL }, /* 912 */
  { 0x0047d1fdUL, 0xf235c016UL }, /* 913 */
  { 0x0047bdddUL, 0x707d7e06UL }, /* 914 */
  { 0x0047a9c8UL, 0x337fbc11UL }, /* 915 */
  { 0x004795beUL, 0x31c884baUL }, /* 916 */
  { 0x004781bfUL, 0x61ee7257UL }, /* 917 */
  { 0x00476dcbUL, 0xba92a04cUL }, /* 918 */
  { 0x004759e3UL, 0x32609c6eUL }, /* 919 */
  { 0x00474605UL, 0xc00e5878UL }, /* 920 */
  { 0x00473233UL, 0x5a5c1b92UL }, /* 921 */
  { 0x00471e6bUL, 0xf8147400UL }, /* 922 */
  { 0x00470aafUL, 0x900c28d5UL }, /* 923 */
  { 0x0046f6feUL, 0x19222bd0UL }, /* 924 */
  { 0x0046e357UL, 0x8a3f8b46UL }, /* 925 */
  { 0x0046cfbbUL, 0xda576424UL }, /* 926 */
  { 0x0046bc2bUL, 0x0066d40bUL }, /* 927 */
  { 0x0046a8a4UL, 0xf374eb86UL }, /* 928 */
  { 0x00469529UL, 0xaa92a04bUL }, /* 929 */
  { 0x004681b9UL, 0x1cdabfa2UL }, /* 930 */
  { 0x00466e53UL, 0x4171e0d9UL }, /* 931 */
  { 0x00465af8UL, 0x0f8657ccUL }, /* 932 */
  { 0x004647a7UL, 0x7e50278fUL }, /* 933 */
  { 0x00463461UL, 0x8510f521UL }, /* 934 */
  { 0x00462126UL, 0x1b13fa3cUL }, /* 935 */
  { 0x00460df5UL, 0x37adf83aUL }, /* 936 */
  { 0x0045faceUL, 0xd23d2b0dUL }, /* 937 */
  { 0x0045e7b2UL, 0xe2293c4eUL }, /* 938 */
  { 0x0045d4a1UL, 0x5ee33664UL }, /* 939 */
  { 0x0045c19aUL, 0x3fe577b8UL }, /* 940 */
  { 0x0045ae9dUL, 0x7cb3a608UL }, /* 941 */
  { 0x00459babUL, 0x0cdaa1c6UL }, /* 942 */
  { 0x004588c2UL, 0xe7f07991UL }, /* 943 */
  { 0x004575e5UL, 0x05945dc0UL }, /* 944 */
  { 0x00456311UL, 0x5d6e9403UL }, /* 945 */
  { 0x00455047UL, 0xe7306b14UL }, /* 946 */
  { 0x00453d88UL, 0x9a942e85UL }, /* 947 */
  { 0x00452ad3UL, 0x6f5d1a93UL }, /* 948 */
  { 0x00451828UL, 0x5d57501eUL }, /* 949 */
  { 0x00450587UL, 0x5c57c8a7UL }, /* 950 */
  { 0x0044f2f0UL, 0x643c4a66UL }, /* 951 */
  { 0x0044e063UL, 0x6ceb5c78UL }, /* 952 */
  { 0x0044cde0UL, 0x6e543b16UL }, /* 953 */
  { 0x0044bb67UL, 0x606ecbe9UL }, /* 954 */
  { 0x0044a8f8UL, 0x3b3b9268UL }, /* 955 */
  { 0x00449692UL, 0xf6c3a454UL }, /* 956 */
  { 0x00448437UL, 0x8b189e36UL }, /* 957 */
  { 0x004471e5UL, 0xf0549804UL }, /* 958 */
  { 0x00445f9eUL, 0x1e9a19c3UL }, /* 959 */
  { 0x00444d60UL, 0x0e14104bUL }, /* 960 */
  { 0x00443b2bUL, 0xb6f5c219UL }, /* 961 */
  { 0x00442901UL, 0x117ac42aUL }, /* 962 */
  { 0x004416e0UL, 0x15e6eef7UL }, /* 963 */
  { 0x004404c8UL, 0xbc865377UL }, /* 964 */
  { 0x0043f2baUL, 0xfdad3035UL }, /* 965 */
  { 0x0043e0b6UL, 0xd1b7e67aUL }, /* 966 */
  { 0x0043cebcUL, 0x310aef87UL }, /* 967 */
  { 0x0043bccbUL, 0x1412d1dfUL }, /* 968 */
  { 0x0043aae3UL, 0x734416a4UL }, /* 969 */
  { 0x00439905UL, 0x471b3f01UL }, /* 970 */
  { 0x00438730UL, 0x881cb9aaUL }, /* 971 */
  { 0x00437565UL, 0x2ed4d868UL }, /* 972 */
  { 0x004363a3UL, 0x33d7c5baUL }, /* 973 */
  { 0x004351eaUL, 0x8fc17a82UL }, /* 974 */
  { 0x0043403bUL, 0x3b35b3c3UL }, /* 975 */
  { 0x00432e95UL, 0x2edfe874UL }, /* 976 */
  { 0x00431cf8UL, 0x63733f5cUL }, /* 977 */
  { 0x00430b64UL, 0xd1aa8502UL }, /* 978 */
  { 0x0042f9daUL, 0x724821acUL }, /* 979 */
  { 0x0042e859UL, 0x3e160f70UL }, /* 980 */
  { 0x0042d6e1UL, 0x2de5d04eUL }, /* 981 */
  { 0x0042c572UL, 0x3a906461UL }, /* 982 */
  { 0x0042b40cUL, 0x5cf6401bUL }, /* 983 */
  { 0x0042a2afUL, 0x8dff4291UL }, /* 984 */
  { 0x0042915bUL, 0xc69aabd9UL }, /* 985 */
  { 0x00428010UL, 0xffbf1370UL }, /* 986 */
  { 0x00426ecfUL, 0x326a5eb7UL }, /* 987 */
  { 0x00425d96UL, 0x57a1b779UL }, /* 988 */
  { 0x00424c66UL, 0x68718286UL }, /* 989 */
  { 0x00423b3fUL, 0x5ded5652UL }, /* 990 */
  { 0x00422a21UL, 0x312ff1b0UL }, /* 991 */
  { 0x0042190bUL, 0xdb5b328eUL }, /* 992 */
  { 0x004207ffUL, 0x55980ccbUL }, /* 993 */
  { 0x0041f6fbUL, 0x99168115UL }, /* 994 */
  { 0x0041e600UL, 0x9f0d93d3UL }, /* 995 */
  { 0x0041d50eUL, 0x60bb4422UL }, /* 996 */
  { 0x0041c424UL, 0xd76482dfUL }, /* 997 */
  { 0x0041b343UL, 0xfc5529bdUL }, /* 998 */
  { 0x0041a26bUL, 0xc8dff269UL }, /* 999 */
  { 0x0041919cUL, 0x365e6dbcUL }, /* 1000 */
  { 0x004180d5UL, 0x3e30fafcUL }, /* 1001 */
  { 0x00417016UL, 0xd9bebf28UL }, /* 1002 */
  { 0x00415f61UL, 0x02759c52UL }, /* 1003 */
  { 0x00414eb3UL, 0xb1ca2906UL }, /* 1004 */
  { 0x00413e0eUL, 0xe137a7c2UL }, /* 1005 */
  { 0x00412d72UL, 0x8a3ffe74UL }, /* 1006 */
  { 0x00411cdeUL, 0xa66bae0cUL }, /* 1007 */
  { 0x00410c53UL, 0x2f49ca14UL }, /* 1008 */
  { 0x0040fbd0UL, 0x1e6ff05bUL }, /* 1009 */
  { 0x0040eb55UL, 0x6d7a40abUL }, /* 1010 */
  { 0x0040dae3UL, 0x160b5488UL }, /* 1011 */
  { 0x0040ca79UL, 0x11cc3700UL }, /* 1012 */
  { 0x0040ba17UL, 0x5a6c5c83UL }, /* 1013 */
  { 0x0040a9bdUL, 0xe9a19acfUL }, /* 1014 */
  { 0x0040996cUL, 0xb92820e0UL }, /* 1015 */
  { 0x00408923UL, 0xc2c26eedUL }, /* 1016 */
  { 0x004078e3UL, 0x00394e7aUL }, /* 1017 */
  { 0x004068aaUL, 0x6b5bca6aUL }, /* 1018 */
  { 0x00405879UL, 0xfdff2724UL }, /* 1019 */
  { 0x00404851UL, 0xb1fedac5UL }, /* 1020 */
  { 0x00403831UL, 0x813c8555UL }, /* 1021 */
  { 0x00402819UL, 0x659fe914UL }, /* 1022 */
  { 0x00401809UL, 0x5916e2caUL }, /* 1023 */
  { 0x00400801UL, 0x55956225UL }, /* 1024 */
  { 0x003ff801UL, 0x5515621fUL }, /* 1025 */
  { 0x003fe809UL, 0x5196e17aUL }, /* 1026 */
  { 0x003fd819UL, 0x451fdb39UL }, /* 1027 */
  { 0x003fc831UL, 0x29bc3f2fUL }, /* 1028 */
  { 0x003fb850UL, 0xf97dea93UL }, /* 1029 */
  { 0x003fa878UL, 0xae7ca0a5UL }, /* 1030 */
  { 0x003f98a8UL, 0x42d60355UL }, /* 1031 */
  { 0x003f88dfUL, 0xb0ad8c00UL }, /* 1032 */
  { 0x003f791eUL, 0xf22c842eUL }, /* 1033 */
  { 0x003f6966UL, 0x0181fe61UL }, /* 1034 */
  { 0x003f59b4UL, 0xd8e2ceebUL }, /* 1035 */
  { 0x003f4a0bUL, 0x728984d2UL }, /* 1036 */
  { 0x003f3a69UL, 0xc8b662bbUL }, /* 1037 */
  { 0x003f2acfUL, 0xd5af57e2UL }, /* 1038 */
  { 0x003f1b3dUL, 0x93bff91dUL }, /* 1039 */
  { 0x003f0bb2UL, 0xfd3979e5UL }, /* 1040 */
  { 0x003efc30UL, 0x0c72a56eUL }, /* 1041 */
  { 0x003eecb4UL, 0xbbc7d7c9UL }, /* 1042 */
  { 0x003edd41UL, 0x059af70aUL }, /* 1043 */
  { 0x003ecdd4UL, 0xe4536c81UL }, /* 1044 */
  { 0x003ebe70UL, 0x525e1df6UL }, /* 1045 */
  { 0x003eaf13UL, 0x4a2d66f3UL }, /* 1046 */
  { 0x003e9fbdUL, 0xc6391217UL }, /* 1047 */
  { 0x003e906fUL, 0xc0fe526eUL }, /* 1048 */
  { 0x003e8129UL, 0x34ffbcddUL }, /* 1049 */
  { 0x003e71eaUL, 0x1cc5418cUL }, /* 1050 */
  { 0x003e62b2UL, 0x72dc2562UL }, /* 1051 */
  { 0x003e5382UL, 0x31d6fb89UL }, /* 1052 */
  { 0x003e4459UL, 0x544d9ef7UL }, /* 1053 */
  { 0x003e3537UL, 0xd4dd2c03UL }, /* 1054 */
  { 0x003e261dUL, 0xae27fa0aUL }, /* 1055 */
  { 0x003e170aUL, 0xdad59510UL }, /* 1056 */
  { 0x003e07ffUL, 0x5592b776UL }, /* 1057 */
  { 0x003df8fbUL, 0x191143b2UL }, /* 1058 */
  { 0x003de9feUL, 0x20083e14UL }, /* 1059 */
  { 0x003ddb08UL, 0x6533c692UL }, /* 1060 */
  { 0x003dcc19UL, 0xe35512a0UL }, /* 1061 */
  { 0x003dbd32UL, 0x9532670bUL }, /* 1062 */
  { 0x003dae52UL, 0x759711e2UL }, /* 1063 */
  { 0x003d9f79UL, 0x7f536467UL }, /* 1064 */
  { 0x003d90a7UL, 0xad3cad08UL }, /* 1065 */
  { 0x003d81dcUL, 0xfa2d3160UL }, /* 1066 */
  { 0x003d7319UL, 0x61042841UL }, /* 1067 */
  { 0x003d645cUL, 0xdca5b3c9UL }, /* 1068 */
  { 0x003d55a7UL, 0x67fadb79UL }, /* 1069 */
  { 0x003d46f8UL, 0xfdf18660UL }, /* 1070 */
  { 0x003d3851UL, 0x997c753fUL }, /* 1071 */
  { 0x003d29b1UL, 0x35933cc5UL }, /* 1072 */
  { 0x003d1b17UL, 0xcd323fc7UL }, /* 1073 */
  { 0x003d0c85UL, 0x5b5aa988UL }, /* 1074 */
  { 0x003cfdf9UL, 0xdb126804UL }, /* 1075 */
  { 0x003cef75UL, 0x47642649UL }, /* 1076 */
  { 0x003ce0f7UL, 0x9b5f46d2UL }, /* 1077 */
  { 0x003cd280UL, 0xd217ddebUL }, /* 1078 */
  { 0x003cc410UL, 0xe6a6ac24UL }, /* 1079 */
  { 0x003cb5a7UL, 0xd42918c2UL }, /* 1080 */
  { 0x003ca745UL, 0x95c12c3dUL }, /* 1081 */
  { 0x003c98eaUL, 0x26958ac8UL }, /* 1082 */
  { 0x003c8a95UL, 0x81d16edbUL }, /* 1083 */
  { 0x003c7c47UL, 0xa2a4a3c8UL }, /* 1084 */
  { 0x003c6e00UL, 0x8443805bUL }, /* 1085 */
  { 0x003c5fc0UL, 0x21e6e179UL }, /* 1086 */
  { 0x003c5186UL, 0x76cc24cfUL }, /* 1087 */
  { 0x003c4353UL, 0x7e352385UL }, /* 1088 */
  { 0x003c3527UL, 0x33682cf6UL }, /* 1089 */
  { 0x003c2701UL, 0x91b00175UL }, /* 1090 */
  { 0x003c18e2UL, 0x945bcd16UL }, /* 1091 */
  { 0x003c0acaUL, 0x36bf227eUL }, /* 1092 */
  { 0x003bfcb8UL, 0x7431f5baUL }, /* 1093 */
  { 0x003beeadUL, 0x48109721UL }, /* 1094 */
  { 0x003be0a8UL, 0xadbbae39UL }, /* 1095 */
  { 0x003bd2aaUL, 0xa09834a4UL }, /* 1096 */
  { 0x003bc4b3UL, 0x1c0f7116UL }, /* 1097 */
  { 0x003bb6c2UL, 0x1b8ef251UL }, /* 1098 */
  { 0x003ba8d7UL, 0x9a888a27UL }, /* 1099 */
  { 0x003b9af3UL, 0x94724883UL }, /* 1100 */
  { 0x003b8d16UL, 0x04c6767dUL }, /* 1101 */
  { 0x003b7f3eUL, 0xe703916dUL }, /* 1102 */
  { 0x003b716eUL, 0x36ac460fUL }, /* 1103 */
  { 0x003b63a3UL, 0xef476ba1UL }, /* 1104 */
  { 0x003b55e0UL, 0x0c5fff18UL }, /* 1105 */
  { 0x003b4822UL, 0x89851e4bUL }, /* 1106 */
  { 0x003b3a6bUL, 0x624a032eUL }, /* 1107 */
  { 0x003b2cbaUL, 0x9245ff18UL }, /* 1108 */
  { 0x003b1f10UL, 0x15147601UL }, /* 1109 */
  { 0x003b116bUL, 0xe654d9d3UL }, /* 1110 */
  { 0x003b03ceUL, 0x01aaa5c0UL }, /* 1111 */
  { 0x003af636UL, 0x62bd5999UL }, /* 1112 */
  { 0x003ae8a5UL, 0x0538752eUL }, /* 1113 */
  { 0x003adb19UL, 0xe4cb73b8UL }, /* 1114 */
  { 0x003acd94UL, 0xfd29c742UL }, /* 1115 */
  { 0x003ac016UL, 0x4a0ad421UL }, /* 1116 */
  { 0x003ab29dUL, 0xc729ec6aUL }, /* 1117 */
  { 0x003aa52bUL, 0x70464b76UL }, /* 1118 */
  { 0x003a97bfUL, 0x41231164UL }, /* 1119 */
  { 0x003a8a59UL, 0x35873ea6UL }, /* 1120 */
  { 0x003a7cf9UL, 0x493daf97UL }, /* 1121 */
  { 0x003a6f9fUL, 0x78151811UL }, /* 1122 */
  { 0x003a624bUL, 0xbddfff09UL }, /* 1123 */
  { 0x003a54feUL, 0x1674ba38UL }, /* 1124 */
  { 0x003a47b6UL, 0x7dad69c3UL }, /* 1125 */
  { 0x003a3a74UL, 0xef67f3ecUL }, /* 1126 */
  { 0x003a2d39UL, 0x678600c8UL }, /* 1127 */
  { 0x003a2003UL, 0xe1ecf5fbUL }, /* 1128 */
  { 0x003a12d4UL, 0x5a85f27aUL }, /* 1129 */
  { 0x003a05aaUL, 0xcd3dca54UL }, /* 1130 */
  { 0x0039f887UL, 0x3605027bUL }, /* 1131 */
  { 0x0039eb69UL, 0x90cfcc9eUL }, /* 1132 */
  { 0x0039de51UL, 0xd99602faUL }, /* 1133 */
  { 0x0039d140UL, 0x0c532441UL }, /* 1134 */
  { 0x0039c434UL, 0x25064f75UL }, /* 1135 */
  { 0x0039b72eUL, 0x1fb23fd8UL }, /* 1136 */
  { 0x0039aa2dUL, 0xf85d48d9UL }, /* 1137 */
  { 0x00399d33UL, 0xab115207UL }, /* 1138 */
  { 0x0039903fUL, 0x33dbd30eUL }, /* 1139 */
  { 0x00398350UL, 0x8ecdcfb4UL }, /* 1140 */
  { 0x00397667UL, 0xb7fbd3e0UL }, /* 1141 */
  { 0x00396984UL, 0xab7def9fUL }, /* 1142 */
  { 0x00395ca7UL, 0x656fb33eUL }, /* 1143 */
  { 0x00394fcfUL, 0xe1f02b51UL }, /* 1144 */
  { 0x003942feUL, 0x1d21dcdaUL }, /* 1145 */
  { 0x00393632UL, 0x132ac15eUL }, /* 1146 */
  { 0x0039296bUL, 0xc0344312UL }, /* 1147 */
  { 0x00391cabUL, 0x206b38feUL }, /* 1148 */
  { 0x00390ff0UL, 0x2fffe32fUL }, /* 1149 */
  { 0x0039033aUL, 0xeb25e6ebUL }, /* 1150 */
  { 0x0038f68bUL, 0x4e144ae7UL }, /* 1151 */
  { 0x0038e9e1UL, 0x5505738aUL }, /* 1152 */
  { 0x0038dd3cUL, 0xfc371f2cUL }, /* 1153 */
  { 0x0038d09eUL, 0x3fea625fUL }, /* 1154 */
  { 0x0038c405UL, 0x1c63a43bUL }, /* 1155 */
  { 0x0038b771UL, 0x8dea9ab2UL }, /* 1156 */
  { 0x0038aae3UL, 0x90ca46e7UL }, /* 1157 */
  { 0x00389e5bUL, 0x2150f186UL }, /* 1158 */
  { 0x003891d8UL, 0x3bd02727UL }, /* 1159 */
  { 0x0038855aUL, 0xdc9cb4b4UL }, /* 1160 */
  { 0x003878e3UL, 0x000ea3cfUL }, /* 1161 */
  { 0x00386c70UL, 0xa2813748UL }, /* 1162 */
  { 0x00386003UL, 0xc052e787UL }, /* 1163 */
  { 0x0038539cUL, 0x55e55f0cUL }, /* 1164 */
  { 0x0038473aUL, 0x5f9d76e8UL }, /* 1165 */
  { 0x00383addUL, 0xd9e33341UL }, /* 1166 */
  { 0x00382e86UL, 0xc121bfd2UL }, /* 1167 */
  { 0x00382235UL, 0x11c76c7eUL }, /* 1168 */
  { 0x003815e8UL, 0xc845a9d9UL }, /* 1169 */
  { 0x003809a1UL, 0xe11105beUL }, /* 1170 */
  { 0x0037fd60UL, 0x58a127e6UL }, /* 1171 */
  { 0x0037f124UL, 0x2b70ce87UL }, /* 1172 */
  { 0x0037e4edUL, 0x55fdcaf2UL }, /* 1173 */
  { 0x0037d8bbUL, 0xd4c8fe3aUL }, /* 1174 */
  { 0x0037cc8fUL, 0xa45655deUL }, /* 1175 */
  { 0x0037c068UL, 0xc12cc877UL }, /* 1176 */
  { 0x0037b447UL, 0x27d65269UL }, /* 1177 */
  { 0x0037a82aUL, 0xd4dff29eUL }, /* 1178 */
  { 0x00379c13UL, 0xc4d9a73bUL }, /* 1179 */
  { 0x00379001UL, 0xf4566a66UL }, /* 1180 */
  { 0x003783f5UL, 0x5fec2f05UL }, /* 1181 */
  { 0x003777eeUL, 0x0433dd86UL }, /* 1182 */
  { 0x00376bebUL, 0xddc950adUL }, /* 1183 */
  { 0x00375feeUL, 0xe94b5265UL }, /* 1184 */
  { 0x003753f7UL, 0x235b9891UL }, /* 1185 */
  { 0x00374804UL, 0x889ec1e5UL }, /* 1186 */
  { 0x00373c17UL, 0x15bc52c7UL }, /* 1187 */
  { 0x0037302eUL, 0xc75eb228UL }, /* 1188 */
  { 0x0037244bUL, 0x9a33266fUL }, /* 1189 */
  { 0x0037186dUL, 0x8ae9d260UL }, /* 1190 */
  { 0x00370c94UL, 0x9635b206UL }, /* 1191 */
  { 0x003700c0UL, 0xb8cc97a5UL }, /* 1192 */
  { 0x0036f4f1UL, 0xef6728b2UL }, /* 1193 */
  { 0x0036e928UL, 0x36c0dac4UL }, /* 1194 */
  { 0x0036dd63UL, 0x8b97f099UL }, /* 1195 */
  { 0x0036d1a3UL, 0xeaad770eUL }, /* 1196 */
  { 0x0036c5e9UL, 0x50c54229UL }, /* 1197 */
  { 0x0036ba33UL, 0xbaa5ea1eUL }, /* 1198 */
  { 0x0036ae83UL, 0x2518c85bUL }, /* 1199 */
  { 0x0036a2d7UL, 0x8ce9f498UL }, /* 1200 */
  { 0x00369730UL, 0xeee841eaUL }, /* 1201 */
  { 0x00368b8fUL, 0x47e53bdbUL }, /* 1202 */
  { 0x00367ff2UL, 0x94b52380UL }, /* 1203 */
  { 0x0036745aUL, 0xd22eec9fUL }, /* 1204 */
  { 0x003668c7UL, 0xfd2c3accUL }, /* 1205 */
  { 0x00365d3aUL, 0x12895e8dUL }, /* 1206 */
  { 0x003651b1UL, 0x0f255289UL }, /* 1207 */
  { 0x0036462cUL, 0xefe1b8b0UL }, /* 1208 */
  { 0x00363aadUL, 0xb1a2d76fUL }, /* 1209 */
  { 0x00362f33UL, 0x514f96e0UL }, /* 1210 */
  { 0x003623bdUL, 0xcbd17e08UL }, /* 1211 */
  { 0x0036184dUL, 0x1e14b00aUL }, /* 1212 */
  { 0x00360ce1UL, 0x4507e96cUL }, /* 1213 */
  { 0x0036017aUL, 0x3d9c7d58UL }, /* 1214 */
  { 0x0035f618UL, 0x04c652e0UL }, /* 1215 */
  { 0x0035eabaUL, 0x977be246UL }, /* 1216 */
  { 0x0035df61UL, 0xf2b6324cUL }, /* 1217 */
  { 0x0035d40eUL, 0x1370d583UL }, /* 1218 */
  { 0x0035c8beUL, 0xf6a9e79cUL }, /* 1219 */
  { 0x0035bd74UL, 0x99620ac1UL }, /* 1220 */
  { 0x0035b22eUL, 0xf89c64eeUL }, /* 1221 */
  { 0x0035a6eeUL, 0x115e9d4dUL }, /* 1222 */
  { 0x00359bb1UL, 0xe0b0d99bUL }, /* 1223 */
  { 0x0035907aUL, 0x639dbb87UL }, /* 1224 */
  { 0x00358547UL, 0x97325e1dUL }, /* 1225 */
  { 0x00357a19UL, 0x787e532dUL }, /* 1226 */
  { 0x00356ef0UL, 0x0493a0bcUL }, /* 1227 */
  { 0x003563cbUL, 0x3886be72UL }, /* 1228 */
  { 0x003558abUL, 0x116e9312UL }, /* 1229 */
  { 0x00354d8fUL, 0x8c6471ecUL }, /* 1230 */
  { 0x00354278UL, 0xa684185dUL }, /* 1231 */
  { 0x00353766UL, 0x5cebab48UL }, /* 1232 */
  { 0x00352c58UL, 0xacbbb49bUL }, /* 1233 */
  { 0x0035214fUL, 0x931720cdUL }, /* 1234 */
  { 0x0035164bUL, 0x0d233c6cUL }, /* 1235 */
  { 0x00350b4bUL, 0x1807b1a2UL }, /* 1236 */
  { 0x0035004fUL, 0xb0ee85c5UL }, /* 1237 */
  { 0x0034f558UL, 0xd50416e3UL }, /* 1238 */
  { 0x0034ea66UL, 0x8177195cUL }, /* 1239 */
  { 0x0034df78UL, 0xb378956fUL }, /* 1240 */
  { 0x0034d48fUL, 0x683be4deUL }, /* 1241 */
  { 0x0034c9aaUL, 0x9cf6b07fUL }, /* 1242 */
  { 0x0034becaUL, 0x4ee0ede3UL }, /* 1243 */
  { 0x0034b3eeUL, 0x7b34dcf5UL }, /* 1244 */
  { 0x0034a917UL, 0x1f2f05a0UL }, /* 1245 */
  { 0x00349e44UL, 0x380e3576UL }, /* 1246 */
  { 0x00349375UL, 0xc3137d58UL }, /* 1247 */
  { 0x003488abUL, 0xbd822f2aUL }, /* 1248 */
  { 0x00347de6UL, 0x249fdb7eUL }, /* 1249 */
  { 0x00347324UL, 0xf5b44f4bUL }, /* 1250 */
  { 0x00346868UL, 0x2e09919eUL }, /* 1251 */
  { 0x00345dafUL, 0xcaebe159UL }, /* 1252 */
  { 0x003452fbUL, 0xc9a9b2eaUL }, /* 1253 */
  { 0x0034484cUL, 0x2793ae0eUL }, /* 1254 */
  { 0x00343da0UL, 0xe1fcab8dUL }, /* 1255 */
  { 0x003432f9UL, 0xf639b301UL }, /* 1256 */
  { 0x00342857UL, 0x61a1f89eUL }, /* 1257 */
  { 0x00341db9UL, 0x218edaf8UL }, /* 1258 */
  { 0x0034131fUL, 0x335be0d2UL }, /* 1259 */
  { 0x00340889UL, 0x9466b6ebUL }, /* 1260 */
  { 0x0033fdf8UL, 0x420f2dd1UL }, /* 1261 */
  { 0x0033f36bUL, 0x39b737b4UL }, /* 1262 */
  { 0x0033e8e2UL, 0x78c2e63dUL }, /* 1263 */
  { 0x0033de5dUL, 0xfc986869UL }, /* 1264 */
  { 0x0033d3ddUL, 0xc2a00863UL }, /* 1265 */
  { 0x0033c961UL, 0xc8442961UL }, /* 1266 */
  { 0x0033beeaUL, 0x0af1458cUL }, /* 1267 */
  { 0x0033b476UL, 0x8815ebdcUL }, /* 1268 */
  { 0x0033aa07UL, 0x3d22be03UL }, /* 1269 */
  { 0x00339f9cUL, 0x278a6e54UL }, /* 1270 */
  { 0x00339535UL, 0x44c1bdafUL }, /* 1271 */
  { 0x00338ad2UL, 0x923f7971UL }, /* 1272 */
  { 0x00338074UL, 0x0d7c7960UL }, /* 1273 */
  { 0x00337619UL, 0xb3f39da1UL }, /* 1274 */
  { 0x00336bc3UL, 0x8321ccacUL }, /* 1275 */
  { 0x00336171UL, 0x7885f146UL }, /* 1276 */
  { 0x00335723UL, 0x91a0f878UL }, /* 1277 */
  { 0x00334cd9UL, 0xcbf5cf91UL }, /* 1278 */
  { 0x00334294UL, 0x2509621cUL }, /* 1279 */
  { 0x00333852UL, 0x9a6297edUL }, /* 1280 */
  { 0x00332e15UL, 0x298a531cUL }, /* 1281 */
  { 0x003323dbUL, 0xd00b6e0eUL }, /* 1282 */
  { 0x003319a6UL, 0x8b72b980UL }, /* 1283 */
  { 0x00330f75UL, 0x594efa90UL }, /* 1284 */
  { 0x00330548UL, 0x3730e8c9UL }, /* 1285 */
  { 0x0032fb1fUL, 0x22ab2c35UL }, /* 1286 */
  { 0x0032f0faUL, 0x19525b70UL }, /* 1287 */
  { 0x0032e6d9UL, 0x18bcf9b7UL }, /* 1288 */
  { 0x0032dcbcUL, 0x1e837506UL }, /* 1289 */
  { 0x0032d2a3UL, 0x2840242bUL }, /* 1290 */
  { 0x0032c88eUL, 0x338f44e2UL }, /* 1291 */
  { 0x0032be7dUL, 0x3e0ef9f7UL }, /* 1292 */
  { 0x0032b470UL, 0x455f4960UL }, /* 1293 */
  { 0x0032aa67UL, 0x47221a61UL }, /* 1294 */
  { 0x0032a062UL, 0x40fb33b1UL }, /* 1295 */
  { 0x00329661UL, 0x309039a0UL }, /* 1296 */
  { 0x00328c64UL, 0x1388ac3dUL }, /* 1297 */
  { 0x0032826aUL, 0xe78de585UL }, /* 1298 */
  { 0x00327875UL, 0xaa4b178dUL }, /* 1299 */
  { 0x00326e84UL, 0x596d4aafUL }, /* 1300 */
  { 0x00326496UL, 0xf2a35bc1UL }, /* 1301 */
  { 0x00325aadUL, 0x739dfa43UL }, /* 1302 */
  { 0x003250c7UL, 0xda0fa697UL }, /* 1303 */
  { 0x003246e6UL, 0x23acb037UL }, /* 1304 */
  { 0x00323d08UL, 0x4e2b33f0UL }, /* 1305 */
  { 0x0032332eUL, 0x57431a1dUL }, /* 1306 */
  { 0x00322958UL, 0x3cae14e4UL }, /* 1307 */
  { 0x00321f85UL, 0xfc279e79UL }, /* 1308 */
  { 0x003215b7UL, 0x936cf75aUL }, /* 1309 */
  { 0x00320bedUL, 0x003d249aUL }, /* 1310 */
  { 0x00320226UL, 0x4058ee21UL }, /* 1311 */
  { 0x0031f863UL, 0x5182dcf7UL }, /* 1312 */
  { 0x0031eea4UL, 0x317f398eUL }, /* 1313 */
  { 0x0031e4e8UL, 0xde140a0eUL }, /* 1314 */
  { 0x0031db31UL, 0x550910a0UL }, /* 1315 */
  { 0x0031d17dUL, 0x9427c9c3UL }, /* 1316 */
  { 0x0031c7cdUL, 0x993b6a9aUL }, /* 1317 */
  { 0x0031be21UL, 0x6210df41UL }, /* 1318 */
  { 0x0031b478UL, 0xec76c923UL }, /* 1319 */
  { 0x0031aad4UL, 0x363d7d54UL }, /* 1320 */
  { 0x0031a133UL, 0x3d3702e6UL }, /* 1321 */
  { 0x00319795UL, 0xff371148UL }, /* 1322 */
  { 0x00318dfcUL, 0x7a130ea3UL }, /* 1323 */
  { 0x00318466UL, 0xaba20e38UL }, /* 1324 */
  { 0x00317ad4UL, 0x91bccec5UL }, /* 1325 */
  { 0x00317146UL, 0x2a3db8e4UL }, /* 1326 */
  { 0x003167bbUL, 0x7300dd70UL }, /* 1327 */
  { 0x00315e34UL, 0x69e3f3f1UL }, /* 1328 */
  { 0x003154b1UL, 0x0cc658ffUL }, /* 1329 */
  { 0x00314b31UL, 0x59890cb0UL }, /* 1330 */
  { 0x003141b5UL, 0x4e0eb104UL }, /* 1331 */
  { 0x0031383cUL, 0xe83b8851UL }, /* 1332 */
  { 0x00312ec8UL, 0x25f573b7UL }, /* 1333 */
  { 0x00312557UL, 0x0523f18dUL }, /* 1334 */
  { 0x00311be9UL, 0x83b01bd9UL }, /* 1335 */
  { 0x0031127fUL, 0x9f84a6c4UL }, /* 1336 */
  { 0x00310919UL, 0x568ddf10UL }, /* 1337 */
  { 0x0030ffb6UL, 0xa6b9a891UL }, /* 1338 */
  { 0x0030f657UL, 0x8df77cacUL }, /* 1339 */
  { 0x0030ecfcUL, 0x0a3868ccUL }, /* 1340 */
  { 0x0030e3a4UL, 0x196f0ce6UL }, /* 1341 */
  { 0x0030da4fUL, 0xb98f99f8UL }, /* 1342 */
  { 0x0030d0feUL, 0xe88fd089UL }, /* 1343 */
  { 0x0030c7b1UL, 0xa466ff2cUL }, /* 1344 */
  { 0x0030be67UL, 0xeb0e0109UL }, /* 1345 */
  { 0x0030b521UL, 0xba7f3c5dUL }, /* 1346 */
  { 0x0030abdfUL, 0x10b6a10bUL }, /* 1347 */
  { 0x0030a29fUL, 0xebb1a71eUL }, /* 1348 */
  { 0x00309964UL, 0x496f4d59UL }, /* 1349 */
  { 0x0030902cUL, 0x27f017c4UL }, /* 1350 */
  { 0x003086f7UL, 0x85360e3aUL }, /* 1351 */
  { 0x00307dc6UL, 0x5f44bafcUL }, /* 1352 */
  { 0x00307498UL, 0xb421293fUL }, /* 1353 */
  { 0x00306b6eUL, 0x81d1e3c5UL }, /* 1354 */
  { 0x00306247UL, 0xc65ef36fUL }, /* 1355 */
  { 0x00305924UL, 0x7fd1ddd5UL }, /* 1356 */
  { 0x00305004UL, 0xac35a3e0UL }, /* 1357 */
  { 0x003046e8UL, 0x4996c063UL }, /* 1358 */
  { 0x00303dcfUL, 0x560326b7UL }, /* 1359 */
  { 0x003034b9UL, 0xcf8a4159UL }, /* 1360 */
  { 0x00302ba7UL, 0xb43cf08aUL }, /* 1361 */
  { 0x00302299UL, 0x022d88ecUL }, /* 1362 */
  { 0x0030198dUL, 0xb76fd229UL }, /* 1363 */
  { 0x00301085UL, 0xd2190594UL }, /* 1364 */
  { 0x00300781UL, 0x503fccccUL }, /* 1365 */
  { 0x002ffe80UL, 0x2ffc406aUL }, /* 1366 */
  { 0x002ff582UL, 0x6f67e69fUL }, /* 1367 */
  { 0x002fec88UL, 0x0c9db1e9UL }, /* 1368 */
  { 0x002fe391UL, 0x05b9ffb5UL }, /* 1369 */
  { 0x002fda9dUL, 0x58da9711UL }, /* 1370 */
  { 0x002fd1adUL, 0x041ea756UL }, /* 1371 */
  { 0x002fc8c0UL, 0x05a6c6dfUL }, /* 1372 */
  { 0x002fbfd6UL, 0x5b94f1b3UL }, /* 1373 */
  { 0x002fb6f0UL, 0x040c8839UL }, /* 1374 */
  { 0x002fae0cUL, 0xfd324defUL }, /* 1375 */
  { 0x002fa52dUL, 0x452c681fUL }, /* 1376 */
  { 0x002f9c50UL, 0xda225c94UL }, /* 1377 */
  { 0x002f9377UL, 0xba3d1052UL }, /* 1378 */
  { 0x002f8aa1UL, 0xe3a6c653UL }, /* 1379 */
  { 0x002f81cfUL, 0x548b1e41UL }, /* 1380 */
  { 0x002f7900UL, 0x0b171331UL }, /* 1381 */
  { 0x002f7034UL, 0x0578fa62UL }, /* 1382 */
  { 0x002f676bUL, 0x41e081ffUL }, /* 1383 */
  { 0x002f5ea5UL, 0xbe7eafdcUL }, /* 1384 */
  { 0x002f55e3UL, 0x7985e03bUL }, /* 1385 */
  { 0x002f4d24UL, 0x7129c48eUL }, /* 1386 */
  { 0x002f4468UL, 0xa39f623cUL }, /* 1387 */
  { 0x002f3bb0UL, 0x0f1d116bUL }, /* 1388 */
  { 0x002f32faUL, 0xb1da7bc2UL }, /* 1389 */
  { 0x002f2a48UL, 0x8a109b36UL }, /* 1390 */
  { 0x002f2199UL, 0x95f9b8d3UL }, /* 1391 */
  { 0x002f18edUL, 0xd3d16b88UL }, /* 1392 */
  { 0x002f1045UL, 0x41d496f6UL }, /* 1393 */
  { 0x002f079fUL, 0xde416a39UL }, /* 1394 */
  { 0x002efefdUL, 0xa7575ebfUL }, /* 1395 */
  { 0x002ef65eUL, 0x9b573713UL }, /* 1396 */
  { 0x002eedc2UL, 0xb882fdb2UL }, /* 1397 */
  { 0x002ee529UL, 0xfd1e03e0UL }, /* 1398 */
  { 0x002edc94UL, 0x676ce079UL }, /* 1399 */
  { 0x002ed401UL, 0xf5b56eccUL }, /* 1400 */
  { 0x002ecb72UL, 0xa63ecd6cUL }, /* 1401 */
  { 0x002ec2e6UL, 0x77515d10UL }, /* 1402 */
  { 0x002eba5dUL, 0x6736bf69UL }, /* 1403 */
  { 0x002eb1d7UL, 0x7439d5fdUL }, /* 1404 */
  { 0x002ea954UL, 0x9ca6c106UL }, /* 1405 */
  { 0x002ea0d4UL, 0xdecade4cUL }, /* 1406 */
  { 0x002e9858UL, 0x38f4c809UL }, /* 1407 */
  { 0x002e8fdeUL, 0xa97453c3UL }, /* 1408 */
  { 0x002e8768UL, 0x2e9a9133UL }, /* 1409 */
  { 0x002e7ef4UL, 0xc6b9c927UL }, /* 1410 */
  { 0x002e7684UL, 0x70257c61UL }, /* 1411 */
  { 0x002e6e17UL, 0x29326281UL }, /* 1412 */
  { 0x002e65acUL, 0xf03668ecUL }, /* 1413 */
  { 0x002e5d45UL, 0xc388b1afUL }, /* 1414 */
  { 0x002e54e1UL, 0xa1819269UL }, /* 1415 */
  { 0x002e4c80UL, 0x887a9339UL }, /* 1416 */
  { 0x002e4422UL, 0x76ce6da4UL }, /* 1417 */
  { 0x002e3bc7UL, 0x6ad90b82UL }, /* 1418 */
  { 0x002e336fUL, 0x62f785f1UL }, /* 1419 */
  { 0x002e2b1aUL, 0x5d882439UL }, /* 1420 */
  { 0x002e22c8UL, 0x58ea5ac8UL }, /* 1421 */
  { 0x002e1a79UL, 0x537eca17UL }, /* 1422 */
  { 0x002e122dUL, 0x4ba73da5UL }, /* 1423 */
  { 0x002e09e4UL, 0x3fc6aae7UL }, /* 1424 */
  { 0x002e019eUL, 0x2e413039UL }, /* 1425 */
  { 0x002df95bUL, 0x157c13dbUL }, /* 1426 */
  { 0x002df11aUL, 0xf3ddc2dfUL }, /* 1427 */
  { 0x002de8ddUL, 0xc7cdd025UL }, /* 1428 */
  { 0x002de0a3UL, 0x8fb4f356UL }, /* 1429 */
  { 0x002dd86cUL, 0x49fd07d9UL }, /* 1430 */
  { 0x002dd037UL, 0xf5110bd0UL }, /* 1431 */
  { 0x002dc806UL, 0x8f5d1f15UL }, /* 1432 */
  { 0x002dbfd8UL, 0x174e8235UL }, /* 1433 */
  { 0x002db7acUL, 0x8b539572UL }, /* 1434 */
  { 0x002daf83UL, 0xe9dbd7bdUL }, /* 1435 */
  { 0x002da75eUL, 0x3157e5bbUL }, /* 1436 */
  { 0x002d9f3bUL, 0x603978c3UL }, /* 1437 */
  { 0x002d971bUL, 0x74f365e3UL }, /* 1438 */
  { 0x002d8efeUL, 0x6df99ce3UL }, /* 1439 */
  { 0x002d86e4UL, 0x49c12748UL }, /* 1440 */
  { 0x002d7ecdUL, 0x06c0275cUL }, /* 1441 */
  { 0x002d76b8UL, 0xa36dd733UL }, /* 1442 */
  { 0x002d6ea7UL, 0x1e4287b3UL }, /* 1443 */
  { 0x002d6698UL, 0x75b79f9dUL }, /* 1444 */
  { 0x002d5e8cUL, 0xa8479a98UL }, /* 1445 */
  { 0x002d5683UL, 0xb46e0839UL }, /* 1446 */
  { 0x002d4e7dUL, 0x98a78b11UL }, /* 1447 */
  { 0x002d467aUL, 0x5371d7baUL }, /* 1448 */
  { 0x002d3e79UL, 0xe34bb3e3UL }, /* 1449 */
  { 0x002d367cUL, 0x46b4f561UL }, /* 1450 */
  { 0x002d2e81UL, 0x7c2e813eUL }, /* 1451 */
  { 0x002d2689UL, 0x823a4ac7UL }, /* 1452 */
  { 0x002d1e94UL, 0x575b52a4UL }, /* 1453 */
  { 0x002d16a1UL, 0xfa15a5e6UL }, /* 1454 */
  { 0x002d0eb2UL, 0x68ee5d1bUL }, /* 1455 */
  { 0x002d06c5UL, 0xa26b9b65UL }, /* 1456 */
  { 0x002cfedbUL, 0xa5148d8eUL }, /* 1457 */
  { 0x002cf6f4UL, 0x6f71691fUL }, /* 1458 */
  { 0x002cef10UL, 0x000b6b7aUL }, /* 1459 */
  { 0x002ce72eUL, 0x556cd8eeUL }, /* 1460 */
  { 0x002cdf4fUL, 0x6e20fbd4UL }, /* 1461 */
  { 0x002cd773UL, 0x48b423a8UL }, /* 1462 */
  { 0x002ccf99UL, 0xe3b3a427UL }, /* 1463 */
  { 0x002cc7c3UL, 0x3dadd468UL }, /* 1464 */
  { 0x002cbfefUL, 0x55320df9UL }, /* 1465 */
  { 0x002cb81eUL, 0x28d0ac03UL }, /* 1466 */
  { 0x002cb04fUL, 0xb71b0a62UL }, /* 1467 */
  { 0x002ca883UL, 0xfea384c8UL }, /* 1468 */
  { 0x002ca0baUL, 0xfdfd75e2UL }, /* 1469 */
  { 0x002c98f4UL, 0xb3bd3672UL }, /* 1470 */
  { 0x002c9131UL, 0x1e781c77UL }, /* 1471 */
  { 0x002c8970UL, 0x3cc47a52UL }, /* 1472 */
  { 0x002c81b2UL, 0x0d399de5UL }, /* 1473 */
  { 0x002c79f6UL, 0x8e6fcfbfUL }, /* 1474 */
  { 0x002c723dUL, 0xbf00523cUL }, /* 1475 */
  { 0x002c6a87UL, 0x9d8560b2UL }, /* 1476 */
  { 0x002c62d4UL, 0x289a2e95UL }, /* 1477 */
  { 0x002c5b23UL, 0x5edae6a4UL }, /* 1478 */
  { 0x002c5375UL, 0x3ee4aa0fUL }, /* 1479 */
  { 0x002c4bc9UL, 0xc7558fa3UL }, /* 1480 */
  { 0x002c4420UL, 0xf6cca2f7UL }, /* 1481 */
  { 0x002c3c7aUL, 0xcbe9e397UL }, /* 1482 */
  { 0x002c34d7UL, 0x454e4432UL }, /* 1483 */
  { 0x002c2d36UL, 0x619ba9c9UL }, /* 1484 */
  { 0x002c2598UL, 0x1f74eaddUL }, /* 1485 */
  { 0x002c1dfcUL, 0x7d7dce9eUL }, /* 1486 */
  { 0x002c1663UL, 0x7a5b0c1eUL }, /* 1487 */
  { 0x002c0ecdUL, 0x14b24981UL }, /* 1488 */
  { 0x002c0739UL, 0x4b2a1b30UL }, /* 1489 */
  { 0x002bffa8UL, 0x1c6a030dUL }, /* 1490 */
  { 0x002bf819UL, 0x871a6fa4UL }, /* 1491 */
  { 0x002bf08dUL, 0x89e4bb63UL }, /* 1492 */
  { 0x002be904UL, 0x23732bd1UL }, /* 1493 */
  { 0x002be17dUL, 0x5270f0c0UL }, /* 1494 */
  { 0x002bd9f9UL, 0x158a2386UL }, /* 1495 */
  { 0x002bd277UL, 0x6b6bc637UL }, /* 1496 */
  { 0x002bcaf8UL, 0x52c3c2deUL }, /* 1497 */
  { 0x002bc37bUL, 0xca40eab3UL }, /* 1498 */
  { 0x002bbc01UL, 0xd092f559UL }, /* 1499 */
  { 0x002bb48aUL, 0x646a8018UL }, /* 1500 */
  { 0x002bad15UL, 0x84790d1bUL }, /* 1501 */
  { 0x002ba5a3UL, 0x2f7102a7UL }, /* 1502 */
  { 0x002b9e33UL, 0x6405aa63UL }, /* 1503 */
  { 0x002b96c6UL, 0x20eb308aUL }, /* 1504 */
  { 0x002b8f5bUL, 0x64d6a336UL }, /* 1505 */
  { 0x002b87f3UL, 0x2e7df197UL }, /* 1506 */
  { 0x002b808dUL, 0x7c97eb39UL }, /* 1507 */
  { 0x002b792aUL, 0x4ddc3f43UL }, /* 1508 */
  { 0x002b71c9UL, 0xa1037bbaUL }, /* 1509 */
  { 0x002b6a6bUL, 0x74c70cc5UL }, /* 1510 */
  { 0x002b630fUL, 0xc7e13befUL }, /* 1511 */
  { 0x002b5bb6UL, 0x990d2f6cUL }, /* 1512 */
  { 0x002b545fUL, 0xe706e960UL }, /* 1513 */
  { 0x002b4d0bUL, 0xb08b4722UL }, /* 1514 */
  { 0x002b45b9UL, 0xf4580085UL }, /* 1515 */
  { 0x002b3e6aUL, 0xb12ba720UL }, /* 1516 */
  { 0x002b371dUL, 0xe5c5a594UL }, /* 1517 */
  { 0x002b2fd3UL, 0x90e63ed4UL }, /* 1518 */
  { 0x002b288bUL, 0xb14e8d74UL }, /* 1519 */
  { 0x002b2146UL, 0x45c082eeUL }, /* 1520 */
  { 0x002b1a03UL, 0x4cfee6f0UL }, /* 1521 */
  { 0x002b12c2UL, 0xc5cd56a5UL }, /* 1522 */
  { 0x002b0b84UL, 0xaef04406UL }, /* 1523 */
  { 0x002b0449UL, 0x072cf522UL }, /* 1524 */
  { 0x002afd0fUL, 0xcd498370UL }, /* 1525 */
  { 0x002af5d9UL, 0x000cdb1dUL }, /* 1526 */
  { 0x002aeea4UL, 0x9e3eba57UL }, /* 1527 */
  { 0x002ae772UL, 0xa6a7b0a5UL }, /* 1528 */
  { 0x002ae043UL, 0x18111e2fUL }, /* 1529 */
  { 0x002ad915UL, 0xf1453316UL }, /* 1530 */
  { 0x002ad1ebUL, 0x310eeec2UL }, /* 1531 */
  { 0x002acac2UL, 0xd63a1f39UL }, /* 1532 */
  { 0x002ac39cUL, 0xdf93606dUL }, /* 1533 */
  { 0x002abc79UL, 0x4be81b94UL }, /* 1534 */
};

/*****************************************************************************/

static void
norm_mul32 (unsigned long a, unsigned long b,
            unsigned long *hi, unsigned long *lo)
{
#if ULONG_MAX > 0xffffffffUL && !defined(NORM_FORCE_32BIT_MUL)
  unsigned long product = a * b;

  *hi = (product >> 32) & 0xffffffffUL;
  *lo = product & 0xffffffffUL;
#else
  unsigned long a0 = a & 0xffffUL;
  unsigned long a1 = (a >> 16) & 0xffffUL;
  unsigned long b0 = b & 0xffffUL;
  unsigned long b1 = (b >> 16) & 0xffffUL;
  unsigned long p0 = a0 * b0;
  unsigned long p1 = a0 * b1;
  unsigned long p2 = a1 * b0;
  unsigned long p3 = a1 * b1;
  unsigned long low, part, carry1, carry2;

  low = p0;
  part = (p1 & 0xffffUL) << 16;
  carry1 = part > 0xffffffffUL - low ? 1UL : 0UL;
  low += part;
  low &= 0xffffffffUL;
  part = (p2 & 0xffffUL) << 16;
  carry2 = part > 0xffffffffUL - low ? 1UL : 0UL;
  low += part;
  low &= 0xffffffffUL;

  *lo = low;
  *hi = p3 + (p1 >> 16) + (p2 >> 16) + carry1 + carry2;
#endif
}

/*****************************************************************************/

static void
norm_ticket_cost (const NormTicket *ticket, const unsigned int *counts,
                  NormTicketCost *cost)
{
  unsigned long p0_hi, p0_lo, p1_hi, p1_lo, middle;
  unsigned long count = counts[ticket->symbol];
  const NormLogQ64 *log_value = &norm_log_q64[ticket->j];

  norm_mul32 (count, log_value->lo, &p0_hi, &p0_lo);
  norm_mul32 (count, log_value->hi, &p1_hi, &p1_lo);
  middle = (p0_hi + p1_lo) & 0xffffffffUL;
  cost->hi = (unsigned int)(p1_hi + (middle < p0_hi ? 1UL : 0UL));
  cost->mid = (unsigned int)middle;
  cost->lo = (unsigned int)p0_lo;
}

/*****************************************************************************/

static LZ9_INLINE int
norm_ticket_compare (const NormTicket *a, const NormTicket *b)
{
  if (a->cost_hi != b->cost_hi)
    {
      return a->cost_hi < b->cost_hi ? -1 : 1;
    }

  if (a->cost_mid != b->cost_mid)
    {
      return a->cost_mid < b->cost_mid ? -1 : 1;
    }

  if (a->cost_lo != b->cost_lo)
    {
      return a->cost_lo < b->cost_lo ? -1 : 1;
    }

  return 0;
}

/*****************************************************************************/

#if defined(NORM_KL_BFPRT)
static LZ9_INLINE int
norm_ticket_compare_total (const NormTicket *a, const NormTicket *b)
{
  int cmp = norm_ticket_compare (a, b);

  if (cmp)
    {
      return cmp;
    }

  if (a->symbol != b->symbol)
    {
      return a->symbol < b->symbol ? -1 : 1;
    }

  if (a->j != b->j)
    {
      return a->j > b->j ? -1 : 1;
    }

  return 0;
}
#endif /* if defined(NORM_KL_BFPRT) */

/*****************************************************************************/

#if defined(NORM_KL_BFPRT)
static void
norm_ticket_sort_small (NormTicket *tickets, unsigned int n)
{
  unsigned int i;

  for (i = 1U; i < n; ++i)
    {
      NormTicket value = tickets[i];
      unsigned int j = i;

      while (j && norm_ticket_compare_total (&value, &tickets[j - 1U]) < 0)
        {
          tickets[j] = tickets[j - 1U];
          --j;
        }

      tickets[j] = value;
    }
}

/*****************************************************************************/

static NormTicket
norm_ticket_select_bfprt (NormTicket *tickets, unsigned int n,
                          unsigned int kth)
{
  while (n > 5U)
    {
      unsigned int i, groups = 0U, lt, gt;
      NormTicket pivot;

      for (i = 0U; i < n; i += 5U)
        {
          unsigned int group_n = n - i;
          unsigned int median;
          NormTicket tmp;

          if (group_n > 5U)
            {
              group_n = 5U;
            }

          norm_ticket_sort_small (tickets + i, group_n);
          median = i + group_n / 2U;
          tmp = tickets[groups];
          tickets[groups] = tickets[median];
          tickets[median] = tmp;
          ++groups;
        }

      pivot = norm_ticket_select_bfprt (tickets, groups, groups / 2U);
      lt = 0U;
      gt = n;
      i = 0U;

      while (i < gt)
        {
          int cmp = norm_ticket_compare_total (&tickets[i], &pivot);

          if (cmp < 0)
            {
              NormTicket tmp = tickets[lt];

              tickets[lt] = tickets[i];
              tickets[i] = tmp;
              ++lt;
              ++i;
            }
          else if (cmp > 0)
            {
              NormTicket tmp;

              --gt;
              tmp = tickets[gt];
              tickets[gt] = tickets[i];
              tickets[i] = tmp;
            }
          else
            {
              ++i;
            }
        }

      if (kth < lt)
        {
          n = lt;
        }
      else if (kth < gt)
        {
          return pivot;
        }
      else
        {
          tickets += gt;
          kth -= gt;
          n -= gt;
        }
    }

  norm_ticket_sort_small (tickets, n);

  return tickets[kth];
}
#endif /* if defined(NORM_KL_BFPRT) */

/*****************************************************************************/

#if !defined(NORM_KL_BFPRT)
static unsigned int
norm_ticket_key_byte (const NormTicket *ticket, unsigned int pass)
{
  unsigned int value, shift;

  if (pass < 3U)
    {
      value = ticket->cost_hi;
      shift = (2U - pass) * 8U;
    }
  else if (pass < 7U)
    {
      value = ticket->cost_mid;
      shift = (6U - pass) * 8U;
    }
  else
    {
      value = ticket->cost_lo;
      shift = (10U - pass) * 8U;
    }

  return (value >> shift) & 255U;
}

/*****************************************************************************/

static NormTicket
norm_ticket_select (const NormTicket *tickets, unsigned int n,
                    unsigned int kth, unsigned short *equal_idx,
                    unsigned int *equal_selected)
{
  unsigned int i, pass, max_hi = 0U, first_pass = 0U;

  for (i = 0U; i < n; ++i)
    {
      equal_idx[i] = (unsigned short)i;

      if (tickets[i].cost_hi > max_hi)
        {
          max_hi = tickets[i].cost_hi;
        }
    }

  if (max_hi < 0x00010000U)
    {
      first_pass = 1U;
    }

  if (max_hi < 0x00000100U)
    {
      first_pass = max_hi ? 2U : 3U;
    }

  for (pass = first_pass; pass < 11U; ++pass)
    {
      unsigned int histogram[256];
      unsigned int bucket, below = 0U, out = 0U;

      (void)memset (histogram, 0, sizeof (histogram));

      for (i = 0U; i < n; ++i)
        {
          ++histogram[norm_ticket_key_byte (&tickets[equal_idx[i]], pass)];
        }

      for (bucket = 0U; bucket < 256U; ++bucket)
        {
          if (kth < below + histogram[bucket])
            {
              break;
            }

          below += histogram[bucket];
        }

      if (bucket >= 256U || !histogram[bucket])
        {
          *equal_selected = 0U;

          return tickets[equal_idx[0]];
        }

      kth -= below;

      for (i = 0U; i < n; ++i)
        {
          unsigned short index = equal_idx[i];

          if (norm_ticket_key_byte (&tickets[index], pass) == bucket)
            {
              equal_idx[out++] = index;
            }
        }

      n = out;

      if (n == 1U)
        {
          *equal_selected = 1U;

          return tickets[equal_idx[0]];
        }
    }

  *equal_selected = kth + 1U;

  return tickets[equal_idx[0]];
}
#endif /* if !defined(NORM_KL_BFPRT) */

/*****************************************************************************/

static unsigned int
norm_muldiv_small (unsigned int a, unsigned int b, unsigned int divisor,
                   unsigned int reciprocal, unsigned int *remainder)
{
#if ULONG_MAX > 0xffffffffUL && !defined(NORM_FORCE_32BIT_MULDIV)
  unsigned long product = (unsigned long)a * b;
  unsigned long scaled = product * reciprocal;
  unsigned int q = (unsigned int)(scaled >> 32);
  unsigned long r = product - (unsigned long)q * divisor;

  while (r >= divisor)
    {
      ++q;
      r -= divisor;
    }

  *remainder = (unsigned int)r;

  return q;
#else
  unsigned int q = 0U, r = 0U;
  unsigned int bit = 1024U;

  (void)reciprocal;

  while (bit)
    {
      unsigned int value = r * 2U;

      if (b & bit)
        {
          value += a;
        }

      q = q * 2U + value / divisor;
      r = value % divisor;
      bit >>= 1;
    }

  *remainder = r;

  return q;
#endif
}

/*****************************************************************************/

static unsigned int
norm_reciprocal (unsigned int divisor)
{
  unsigned int q = UINT_MAX / divisor;
  unsigned int r = UINT_MAX % divisor;

  if (r + 1U >= divisor)
    {
      ++q;
    }

  return q;
}

/*****************************************************************************/

static int
norm_active_kl_with_scratch (const unsigned int *counts,
                             const unsigned int *idx, unsigned int active,
                             unsigned int total, unsigned short *freq,
                             NormScratch *scratch)
{
  NormTicket *tickets = scratch->tickets;
  unsigned int k, ticket_n = 0U, upper_sum = 0U;
  unsigned int reciprocal, decrement_n;

  if (active < 2U || active > 512U || total < active
      || total > 0x3fffffU)
    {
      return 0;
    }

  reciprocal = norm_reciprocal (total);

  for (k = 0U; k < active; ++k)
    {
      unsigned int symbol = idx[k];
      unsigned int count = counts[symbol];
      unsigned int rem, lower_q, upper_q;
      unsigned int lower, upper, span, j;

      if (!count || count > total)
        {
          return 0;
        }

      lower_q = norm_muldiv_small (count, TABLE_N - active + 2U,
                                   total, reciprocal, &rem);
      lower = rem ? lower_q : lower_q - 1U;

      if (lower < 1U)
        {
          lower = 1U;
        }

      upper_q = norm_muldiv_small (count, TABLE_N + active - 2U,
                                   total, reciprocal, &rem);
      upper = upper_q + 1U;

      if (upper > NORM_TICKET_MAX_J || upper < lower)
        {
          return 0;
        }

      span = upper - lower;

      if (span > NORM_TICKET_MAX
          || upper_sum > 0x7fffffffU - upper
          || ticket_n > NORM_TICKET_MAX - span)
        {
          return 0;
        }

      freq[symbol] = (unsigned short)upper;
      upper_sum += upper;

      for (j = lower + 1U; j <= upper; ++j)
        {
          NormTicketCost cost;

          tickets[ticket_n].symbol = (unsigned short)symbol;
          tickets[ticket_n].j = (unsigned short)j;
          norm_ticket_cost (&tickets[ticket_n], counts, &cost);

          if (cost.hi >= 0x01000000U)
            {
              return 0;
            }

          tickets[ticket_n].cost_hi = cost.hi;
          tickets[ticket_n].cost_mid = cost.mid;
          tickets[ticket_n].cost_lo = cost.lo;
          ++ticket_n;
        }
    }

  if (upper_sum < TABLE_N)
    {
      return 0;
    }

  decrement_n = upper_sum - TABLE_N;

  if (decrement_n > ticket_n)
    {
      return 0;
    }

  if (decrement_n)
    {
#if defined(NORM_KL_BFPRT)
      NormTicket threshold
          = norm_ticket_select_bfprt (tickets, ticket_n, decrement_n - 1U);

      for (k = 0U; k < ticket_n; ++k)
        {
          if (norm_ticket_compare_total (&tickets[k], &threshold) <= 0)
            {
              unsigned int symbol = tickets[k].symbol;

              if (freq[symbol] <= 1U)
                {
                  return 0;
                }

              --freq[symbol];
            }
        }
#else
      unsigned int equal_selected, selected = 0U;
      NormTicket threshold
          = norm_ticket_select (tickets, ticket_n, decrement_n - 1U,
                                scratch->equal_idx, &equal_selected);

      for (k = 0U; k < ticket_n; ++k)
        {
          if (norm_ticket_compare (&tickets[k], &threshold) < 0)
            {
              unsigned int symbol = tickets[k].symbol;

              if (freq[symbol] <= 1U)
                {
                  return 0;
                }

              --freq[symbol];
              ++selected;
            }
        }

      if (!equal_selected || selected + equal_selected != decrement_n)
        {
          return 0;
        }

      for (k = 0U; k < equal_selected; ++k)
        {
          unsigned int symbol = tickets[scratch->equal_idx[k]].symbol;

          if (freq[symbol] <= 1U)
            {
              return 0;
            }

          --freq[symbol];
        }
#endif
    }

  return 1;
}

/*****************************************************************************/

#if defined(LZ9ANS_TEST_NORMALIZER)
static int
norm_active_kl ( /*cppcheck-suppress unusedFunction */
                const unsigned int *counts, const unsigned int *idx,
                unsigned int active, unsigned int total,
                unsigned short *freq)
{
  NormScratch scratch;

  return norm_active_kl_with_scratch (counts, idx, active, total, freq,
                                     &scratch);
}
#endif /* if defined(LZ9ANS_TEST_NORMALIZER) */

/*****************************************************************************/

static int
norm_active_with_scratch (const unsigned int *counts,
                          const unsigned int *idx, unsigned int active,
                          unsigned int total, unsigned short *freq,
                          NormScratch *scratch)
{
  return norm_active_kl_with_scratch (counts, idx, active, total, freq,
                                     scratch);
}

/*****************************************************************************/

static int
norm_counts_with_scratch (const unsigned int *counts, unsigned int alphabet,
                          unsigned int total, unsigned short *freq,
                          NormScratch *scratch)
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

  return norm_active_with_scratch (counts, idx, active, total, freq,
                                   scratch);
}

/*****************************************************************************/

#if LZ9ANS_EARLY_STORED_LITERAL_PERCENT != 0
static int
norm_counts (const unsigned int *counts, unsigned int alphabet,
             unsigned int total, unsigned short *freq)
{
  NormScratch scratch;

  return norm_counts_with_scratch (counts, alphabet, total, freq, &scratch);
}
#endif /* if LZ9ANS_EARLY_STORED_LITERAL_PERCENT != 0 */

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

#define CHAIN_BASE_MAX 0x20000000U

/*****************************************************************************/

static LZ9_INLINE void
chain_insert (Encoder *e, unsigned int pos, unsigned int h)
{
  const unsigned int a = pos + e->base;

  e->link[a & (e->window_n - 1U)] = e->head[h];
  e->head[h] = (int)a;
}

/*****************************************************************************/

static void
chain_index_range (Encoder *e, unsigned int from, unsigned int to)
{
  unsigned int j;

  for (j = from; j < to; ++j)
    {
      chain_insert (e, j, hash3 (e->buffer + j));
    }
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

#if defined(LZ9ANS_X86_64_MATCH_FASTPATH)
  if (max >= 4U)
    {
      lz9_u64 a, b, diff;

      (void)memcpy (&a, buf + cand, sizeof (a));
      (void)memcpy (&b, buf + pos, sizeof (b));
      diff = a ^ b;

      if (diff)
        {
          k = (unsigned int)(__builtin_ctzll (diff) >> 4);

          if (k < 3U)
            {
              return;
            }

          goto match_done;
        }

      k = 4U;
    }
  else
#endif /* if defined(LZ9ANS_X86_64_MATCH_FASTPATH) */
    {
      if (buf[cand] != p0 || buf[cand + 1U] != p1 || buf[cand + 2U] != p2)
        {
          return;
        }

      k = 3U;
    }

#if defined(LZ9ANS_X86_64_MATCH_FASTPATH)
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
#endif /* if defined(LZ9ANS_X86_64_MATCH_FASTPATH) */

  while (k < max && buf[cand + k] == buf[pos + k])
    {
      ++k;
    }

#if defined(LZ9ANS_X86_64_MATCH_FASTPATH)
match_done:
#endif /* if defined(LZ9ANS_X86_64_MATCH_FASTPATH) */

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
  int checks = 0;
  unsigned int cand_a, pos_a;
  unsigned int best = 0, d = 0;
  long best_gain = 0;
  unsigned int max;
  unsigned long base_ll_price = 0;
  const unsigned short *buf;
  unsigned int p0, p1, p2, i;
  unsigned int history_start = e->window_n - e->hlen;

  if (pos + 2U >= end)
    {
      if (pos + 1U < end && e->use_cost
          && e->min_match == MIN_MATCH_SHORT)
        {
          unsigned int pos_rel = pos - e->window_n;

          base_ll_price = ll_price (e, pending);

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

  pos_a = pos + e->base;
  cand_a = (unsigned int)e->head[pos_hash];

  while (checks < (int)e->search_depth && cand_a < pos_a
         && pos_a <= cand_a + e->window_n)
    {
      const unsigned int cand = cand_a - e->base;

      if (best >= 3U && best < max && buf[cand + best] != buf[pos + best])
        {
          cand_a = (unsigned int)e->link[cand_a & (e->window_n - 1U)];
          ++checks;

          continue;
        }

      consider_match (e, pos, max, base_ll_price, cand,
                      p0, p1, p2, &best, &d, &best_gain);

      if (!e->use_cost && best == max)
        {
          break;
        }

      cand_a = (unsigned int)e->link[cand_a & (e->window_n - 1U)];
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
  unsigned int litstart = pos, k, length, distance;
  unsigned int pos_hash, next_hash = 0, cached_hash = 0;
  int have_cached_hash = 0;
  long gain = 0;

  e->sn = 0;
  e->lit_count = 0;
  e->max_literal = 0;

  {
    unsigned int index_end = end - 2U;

    if (index_end > e->window_n)
      {
        index_end = e->window_n;
      }

    if (e->chain_state == 1)
      {
        chain_index_range (e, e->window_n - (e->hlen < 2U ? e->hlen : 2U),
                           index_end);
      }
    else
      {
        unsigned int j;

        for (j = 0; j < HASH_N; ++j)
          {
            e->head[j] = -1;
          }

        chain_index_range (e, e->window_n - e->hlen, index_end);
      }

    e->chain_state = 0;
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
                  chain_insert (e, pos, pos_hash);
                }

              ++pos;
              cached_hash = next_hash;
              have_cached_hash = 1;

              continue;
            }
        }

      if (length >= 2)
        {
          if (e->sn >= e->seq_capacity)
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
              chain_insert (e, pos, pos_hash);
            }

          if (pos + 3U < end)
            {
              unsigned int at = pos + 1U;
              unsigned int h = have_next_hash ? next_hash
                                              : hash3 (e->buffer + at);

              chain_insert (e, at, h);
            }

          for (k = 2U; k < length; ++k)
            {
              unsigned int at = pos + k;

              if (at + 2U >= end)
                {
                  break;
                }

              {
                chain_insert (e, at, hash3 (e->buffer + at));
              }
            }

          pos += length;
          litstart = pos;
        }
      else
        {
          if (pos + 2U < end)
            {
              chain_insert (e, pos, pos_hash);
            }

          ++pos;
        }
    }

  e->last_lit = end - litstart;
  e->lit_count += e->last_lit;
  e->chain_state = 2;

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
early_stored_block (Encoder *e, unsigned int stored_width,
                    NormScratch *norm_scratch)
{
#if LZ9ANS_EARLY_STORED_LITERAL_PERCENT == 0
  (void)e;
  (void)stored_width;
  (void)norm_scratch;

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
  else if (used >= 2U
           && (norm_scratch
                   ? norm_counts_with_scratch (e->counts, 512U, total, freq,
                                               norm_scratch)
                   : norm_counts (e->counts, 512U, total, freq)))
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
estimate_one_stream (Encoder *e, unsigned int kind,
                     NormScratch *norm_scratch)
{
  unsigned int i;
  unsigned short freq[512];
  unsigned int alphabet = stream_alphabet (kind, e->of_alphabet), lone = 0;
  unsigned int used, width, total, invariant_cost = 0;
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

  if (used >= 2
      && norm_counts_with_scratch (e->counts, alphabet, total, freq,
                                   norm_scratch))
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

  if (used == 1 && total)
    {
      invariant_cost = (unsigned int)((24UL * COST_SCALE) / total);
    }
  else if (use_tans && total)
    {
      invariant_cost
          = (unsigned int)((header * 8UL * COST_SCALE) / total);
    }

  for (i = 0; i < alphabet; ++i)
    {
      unsigned int c = width * COST_SCALE;

      if (used == 1)
        {
          c = (i == lone) ? invariant_cost : (width + 2U) * COST_SCALE;
        }
      else if (use_tans)
        {
          c = freq[i] ? entropy_cost (freq[i]) + invariant_cost
                      : (width + 2U) * COST_SCALE;
        }

      if (c > 65535U)
        {
          c = 65535U;
        }

      e->costs[kind][i] = (unsigned short)c;
    }
}

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
static void
estimate_stream_task (void *arg)
{
  ThreadEncoderWorkspace *workspace = (ThreadEncoderWorkspace *)arg;

  estimate_one_stream (&workspace->encoder, workspace->kind,
                       (NormScratch *)(void *)workspace->stage);
}
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

static void
estimate_costs (Encoder *e, ThreadPool *pool,
                ThreadEncoderWorkspace *workspaces,
                NormScratch *norm_scratch)
{
  unsigned int kind;

#if defined(LZ9ANS_PTHREADS)
  if (pool && pool->participants > 1U && workspaces)
    {
      ThreadTask tasks[ENTROPY_STREAM_COUNT];

      for (kind = 0; kind < ENTROPY_STREAM_COUNT; ++kind)
        {
          workspaces[kind].encoder = *e;
          workspaces[kind].kind = kind;
          tasks[kind].function = estimate_stream_task;
          tasks[kind].context = &workspaces[kind];
        }

      (void)thread_pool_run (pool, tasks, ENTROPY_STREAM_COUNT);

      e->max_literal = workspaces[STREAM_LIT].encoder.max_literal;

      for (kind = 0; kind < ENTROPY_STREAM_COUNT; ++kind)
        {
          (void)memcpy (e->costs[kind],
                        workspaces[kind].encoder.costs[kind],
                        sizeof (e->costs[kind]));
        }
    }
  else
#endif /* if defined(LZ9ANS_PTHREADS) */
    {
#if !defined(LZ9ANS_PTHREADS)
      (void)pool;
      (void)workspaces;
#endif /* if !defined(LZ9ANS_PTHREADS) */
      for (kind = 0; kind < ENTROPY_STREAM_COUNT; ++kind)
        {
          NormScratch *scratch = norm_scratch
                                     ? norm_scratch
                                     : (NormScratch *)(void *)e->link;

          estimate_one_stream (e, kind, scratch);
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
                  unsigned char *is_reuse, unsigned long *desc_bytes,
                  NormScratch *norm_scratch)
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

      if (!norm_active_with_scratch (tmp, idx, 2U, total + 1U, freq,
                                     norm_scratch))
        {
          return ~0UL;
        }

      used = 2U;
    }
  else if (!norm_active_with_scratch (hist, idx, used, total, freq,
                                      norm_scratch))
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
               unsigned long *desc_bytes_out,
               NormScratch *norm_scratch)
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
                                 tf[g], &tmp_empty[g], &tmp_reuse[g], &db,
                                 norm_scratch);

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
encode_one_stream_with_scratch (Encoder *e, unsigned int kind,
                                NormScratch *norm_scratch)
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
      if (norm_counts_with_scratch (e->counts, alphabet, total, freq,
                                    norm_scratch))
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
                                    pfreq, pempty, preuse, &phased_bytes,
                                    norm_scratch))
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

static int
encode_one_stream (Encoder *e, unsigned int kind)
{
  NormScratch scratch;

  return encode_one_stream_with_scratch (e, kind, &scratch);
}

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
static void
free_thread_encoder_workspaces (ThreadEncoderWorkspace *workspaces)
{
  unsigned int i;

  if (!workspaces)
    {
      return;
    }

  for (i = 0; i < ENTROPY_STREAM_COUNT; ++i)
    {
      free (workspaces[i].stage);
      free (workspaces[i].desc);
      free (workspaces[i].table);
      free (workspaces[i].gtable);
    }

  free (workspaces);
}

/*****************************************************************************/

static ThreadEncoderWorkspace *
allocate_thread_encoder_workspaces (void)
{
  unsigned int i;
  ThreadEncoderWorkspace *workspaces
      = (ThreadEncoderWorkspace *)calloc (ENTROPY_STREAM_COUNT,
                                           sizeof (ThreadEncoderWorkspace));

  if (!workspaces)
    {
      return (ThreadEncoderWorkspace *)0;
    }

  for (i = 0; i < ENTROPY_STREAM_COUNT; ++i)
    {
      workspaces[i].stage = (unsigned char *)malloc (STAGE_MAX);
      workspaces[i].desc = (unsigned char *)malloc (DESC_MAX);
      workspaces[i].table = (AnsTable *)malloc (sizeof (AnsTable));
      workspaces[i].gtable
          = (AnsTable *)malloc (PHASE_GROUPS_MAX * sizeof (AnsTable));

      if (!workspaces[i].stage || !workspaces[i].desc
          || !workspaces[i].table || !workspaces[i].gtable)
        {
          free_thread_encoder_workspaces (workspaces);

          return (ThreadEncoderWorkspace *)0;
        }
    }

  return workspaces;
}

/*****************************************************************************/

static void
encode_stream_task (void *arg)
{
  ThreadEncoderWorkspace *workspace = (ThreadEncoderWorkspace *)arg;

  workspace->okay
      = encode_one_stream_with_scratch (
          &workspace->encoder, workspace->kind,
          (NormScratch *)(void *)workspace->stage);
}

/*****************************************************************************/

static int
encode_streams_parallel (Encoder *e, ThreadPool *pool,
                         ThreadEncoderWorkspace *workspaces)
{
  unsigned int kind;
  ThreadTask tasks[ENTROPY_STREAM_COUNT];

  for (kind = 0; kind < ENTROPY_STREAM_COUNT; ++kind)
    {
      ThreadEncoderWorkspace *workspace = &workspaces[kind];

      workspace->encoder = *e;
      workspace->encoder.stage = workspace->stage;
      workspace->encoder.desc = workspace->desc;
      workspace->encoder.table = workspace->table;
      workspace->encoder.gtable = workspace->gtable;
      workspace->encoder.desc_len = 0UL;
      workspace->encoder.stage_len = 0UL;
      workspace->kind = kind;
      workspace->okay = 0;
      (void)memcpy (workspace->stage + STAGE_DATA_MAX,
                    e->stage + STAGE_DATA_MAX, TABLE_CACHE_BYTES);
      tasks[kind].function = encode_stream_task;
      tasks[kind].context = workspace;
    }

  (void)thread_pool_run (pool, tasks, ENTROPY_STREAM_COUNT);

  e->desc_len = 0UL;
  e->stage_len = 0UL;

  for (kind = 0; kind < ENTROPY_STREAM_COUNT; ++kind)
    {
      ThreadEncoderWorkspace *workspace = &workspaces[kind];
      Encoder *worker = &workspace->encoder;

      if (!workspace->okay
          || e->desc_len > DESC_MAX
          || worker->desc_len > DESC_MAX - e->desc_len
          || e->stage_len > STAGE_DATA_MAX
          || worker->stage_len > STAGE_DATA_MAX - e->stage_len)
        {
          return 0;
        }

      (void)memcpy (e->desc + e->desc_len, worker->desc,
                    (size_t)worker->desc_len);
      e->desc_len += worker->desc_len;
      (void)memcpy (e->stage + e->stage_len, worker->stage,
                    (size_t)worker->stage_len);
      e->stage_len += worker->stage_len;
      e->stream_bits[kind] = worker->stream_bits[kind];
      e->stream_bytes[kind] = worker->stream_bytes[kind];
    }

  return 1;
}

/*****************************************************************************/

static void
commit_thread_stream_caches (const Encoder *e,
                             ThreadEncoderWorkspace *workspaces)
{
  unsigned int kind;

  for (kind = 0; kind < ENTROPY_STREAM_COUNT; ++kind)
    {
      unsigned int first = kind;
      const unsigned char *source = workspaces[kind].stage + STAGE_DATA_MAX;
      unsigned char *target = e->stage + STAGE_DATA_MAX;

      (void)memcpy (target + first * 512U * sizeof (unsigned short),
                    source + first * 512U * sizeof (unsigned short),
                    512U * sizeof (unsigned short));

      if (kind <= STREAM_ML)
        {
          first = phase_table_base[kind];
          (void)memcpy (target + first * 512U * sizeof (unsigned short),
                        source + first * 512U * sizeof (unsigned short),
                        2U * 512U * sizeof (unsigned short));
        }
    }
}
#endif /* if defined(LZ9ANS_PTHREADS) */

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
block_output_write (BlockOutput *output, const void *source, size_t size)
{
  if (output->file)
    {
      return write_exact (output->file, source, size);
    }

  if (output->size > output->capacity
      || size > output->capacity - output->size)
    {
      return 0;
    }

  if (size)
    {
      (void)memcpy (output->buffer + output->size, source, size);
      output->size += size;
    }

  return 1;
}

/*****************************************************************************/

static int
build_stored_block (Encoder *e, unsigned long crc, unsigned int width,
                    BlockOutput *output)
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

  return block_output_write (output, h, sizeof (h))
         && block_output_write (output, e->stage, (size_t)nbytes);
}

/*****************************************************************************/

static int
encode_block (Encoder *e, ThreadPool *pool,
              ThreadEncoderWorkspace *workspaces,
              NormScratch *worker_norm_scratch, BlockOutput *output)
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
      if (early_stored_block (e, width, worker_norm_scratch))
        {
          (void)memcpy (e->reps, saved_reps, sizeof (saved_reps));

          return build_stored_block (e, crc, width, output);
        }

      estimate_costs (e, pool, workspaces, worker_norm_scratch);
      (void)memcpy (e->reps, saved_reps, sizeof (saved_reps));
      e->use_cost = 1;

      if (!parse_block (e))
        {
          return 0;
        }

      if (e->level == 9)
        {
          estimate_costs (e, pool, workspaces, worker_norm_scratch);
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

#if defined(LZ9ANS_PTHREADS)
  if (pool && pool->participants > 1U && workspaces)
    {
      compressed_ok = encode_streams_parallel (e, pool, workspaces);
    }
  else
#endif /* if defined(LZ9ANS_PTHREADS) */
    {
      if (worker_norm_scratch)
        {
          if (!encode_one_stream_with_scratch (e, STREAM_LIT,
                                               worker_norm_scratch)
              || !encode_one_stream_with_scratch (e, STREAM_LL,
                                                  worker_norm_scratch)
              || !encode_one_stream_with_scratch (e, STREAM_ML,
                                                  worker_norm_scratch)
              || !encode_one_stream_with_scratch (e, STREAM_OF,
                                                  worker_norm_scratch))
            {
              compressed_ok = 0;
            }
        }
      else if (!encode_one_stream (e, STREAM_LIT)
               || !encode_one_stream (e, STREAM_LL)
               || !encode_one_stream (e, STREAM_ML)
               || !encode_one_stream (e, STREAM_OF))
        {
          compressed_ok = 0;
        }
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

      return build_stored_block (e, crc, width, output);
    }

#if defined(LZ9ANS_PTHREADS)
  if (pool && pool->participants > 1U && workspaces)
    {
      commit_thread_stream_caches (e, workspaces);
    }
#endif /* if defined(LZ9ANS_PTHREADS) */

  make_block_header (h, BLOCK_LZ, e->n, e->sn, e->desc_len,
                     e->stream_bits, crc, block_aligned);

  return block_output_write (output, h, sizeof (h))
         && block_output_write (output, e->desc, (size_t)e->desc_len)
         && block_output_write (output, e->stage, (size_t)e->stage_len);
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
parse_descriptor (Stream *s, DecTable *tables, DecTable **phase_tables,
                  unsigned int kind, unsigned int of_alphabet, int phased,
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

      if (!*phase_tables)
        {
          *phase_tables = (DecTable *)calloc (DEC_PHASE_TABLE_COUNT,
                                              sizeof (DecTable));

          if (!*phase_tables)
            {
              return 0;
            }
        }

      pp = &phase_partitions[kind][pid];
      gt = *phase_tables + decoder_phase_table_base[kind];

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

static HOT LZ9_INLINE int
store_symbol8 (Output *o, unsigned int v)
{
  if (v > 255U)
    {
      return 0;
    }

  o->ring8[o->produced & (o->window_n - 1U)] = (unsigned char)v;
  ++o->produced;
  ++o->in_block;

  return 1;
}

/*****************************************************************************/

static HOT LZ9_INLINE int
store_symbol9 (Output *o, unsigned int v)
{
  if (v > 511U)
    {
      return 0;
    }

  o->ring[o->produced & (o->window_n - 1U)] = (unsigned short)v;
  ++o->produced;
  ++o->in_block;

  return 1;
}

/*****************************************************************************/

static HOT LZ9_ALWAYS_INLINE int
store_symbol (Output *o, unsigned int v)
{
  return o->out_mode == 8 ? store_symbol8 (o, v) : store_symbol9 (o, v);
}

/*****************************************************************************/

static LZ9_INLINE int
store_stored_symbols (Output *o, const unsigned char *data, unsigned int n,
                      unsigned int width)
{
  unsigned int mask = o->window_n - 1U;
  unsigned int start = (unsigned int)(o->produced & mask);
  unsigned int i = 0;

  if (width == 8U)
    {
      unsigned int first = o->window_n - start;

      if (first > n)
        {
          first = n;
        }

      if (o->out_mode == 8)
        {
          (void)memcpy (o->ring8 + start, data, first);

          if (n > first)
            {
              (void)memcpy (o->ring8, data + first, n - first);
            }
        }
      else
        {
          while (i < first)
            {
              o->ring[start + i] = data[i];
              ++i;
            }

          while (i < n)
            {
              o->ring[i - first] = data[i];
              ++i;
            }
        }
    }
  else
    {
      unsigned int group_bytes = width == 9U ? 9U : 7U;
      unsigned int groups = n / 8U;
      unsigned int g;

      if (width != 7U && width != 9U)
        {
          return 0;
        }

      for (g = 0; g < groups; ++g)
        {
          const unsigned char *p = data + g * group_bytes;
          unsigned int v[8];
          unsigned int j;

          if (width == 9U)
            {
              v[0] = ((unsigned int)p[0] << 1) | (p[1] >> 7);
              v[1] = (((unsigned int)p[1] & 0x7fU) << 2) | (p[2] >> 6);
              v[2] = (((unsigned int)p[2] & 0x3fU) << 3) | (p[3] >> 5);
              v[3] = (((unsigned int)p[3] & 0x1fU) << 4) | (p[4] >> 4);
              v[4] = (((unsigned int)p[4] & 0x0fU) << 5) | (p[5] >> 3);
              v[5] = (((unsigned int)p[5] & 0x07U) << 6) | (p[6] >> 2);
              v[6] = (((unsigned int)p[6] & 0x03U) << 7) | (p[7] >> 1);
              v[7] = (((unsigned int)p[7] & 0x01U) << 8) | p[8];
            }
          else
            {
              v[0] = p[0] >> 1;
              v[1] = (((unsigned int)p[0] & 0x01U) << 6) | (p[1] >> 2);
              v[2] = (((unsigned int)p[1] & 0x03U) << 5) | (p[2] >> 3);
              v[3] = (((unsigned int)p[2] & 0x07U) << 4) | (p[3] >> 4);
              v[4] = (((unsigned int)p[3] & 0x0fU) << 3) | (p[4] >> 5);
              v[5] = (((unsigned int)p[4] & 0x1fU) << 2) | (p[5] >> 6);
              v[6] = (((unsigned int)p[5] & 0x3fU) << 1) | (p[6] >> 7);
              v[7] = (unsigned int)p[6] & 0x7fU;
            }

          if (o->out_mode == 8)
            {
              for (j = 0; j < 8U; ++j)
                {
                  unsigned int at = (start + i + j) & mask;

                  if (v[j] > 255U)
                    {
                      return 0;
                    }

                  o->ring8[at] = (unsigned char)v[j];
                }
            }
          else
            {
              for (j = 0; j < 8U; ++j)
                {
                  unsigned int at = (start + i + j) & mask;

                  o->ring[at] = (unsigned short)v[j];
                }
            }

          i += 8U;
        }

      if (i < n)
        {
          BitReader tail;
          const unsigned int tail_n = n - i;
          const unsigned long tail_bits = (unsigned long)tail_n * width;
          const unsigned int byte_offset = groups * group_bytes;

          br_init (&tail, data + byte_offset, tail_bits, 0);

          while (i < n)
            {
              unsigned int v = br_get (&tail, width);

              if (tail.failed || (o->out_mode == 8 && v > 255U))
                {
                  return 0;
                }

              if (o->out_mode == 8)
                {
                  o->ring8[(start + i) & mask] = (unsigned char)v;
                }
              else
                {
                  o->ring[(start + i) & mask] = (unsigned short)v;
                }

              ++i;
            }

          if (tail.failed || tail.position != tail.bits)
            {
              return 0;
            }
        }
    }

  o->produced += n;
  o->in_block += n;

  return 1;
}

/*****************************************************************************/

static LZ9_INLINE void
copy_ring_range (unsigned short *ring, unsigned int window_n,
                 unsigned int dst, unsigned int src, unsigned int count)
{
  const unsigned int mask = window_n - 1U;

  while (count)
    {
      unsigned int chunk = count;
      unsigned int src_left = window_n - src;
      unsigned int dst_left = window_n - dst;

      if (chunk > src_left)
        {
          chunk = src_left;
        }

      if (chunk > dst_left)
        {
          chunk = dst_left;
        }

      (void)memcpy (ring + dst, ring + src,
                    (size_t)chunk * sizeof (unsigned short));
      src = (src + chunk) & mask;
      dst = (dst + chunk) & mask;
      count -= chunk;
    }
}

/*****************************************************************************/

static LZ9_INLINE void
copy_ring_range8 (unsigned char *ring, unsigned int window_n,
                  unsigned int dst, unsigned int src, unsigned int count)
{
  const unsigned int mask = window_n - 1U;

  while (count)
    {
      unsigned int chunk = count;
      unsigned int src_left = window_n - src;
      unsigned int dst_left = window_n - dst;

      if (chunk > src_left)
        {
          chunk = src_left;
        }

      if (chunk > dst_left)
        {
          chunk = dst_left;
        }

      (void)memcpy (ring + dst, ring + src, (size_t)chunk);
      src = (src + chunk) & mask;
      dst = (dst + chunk) & mask;
      count -= chunk;
    }
}

/*****************************************************************************/

static void
store_match (Output *o, unsigned int d, unsigned int ml)
{
  const unsigned int mask = o->window_n - 1U;
  unsigned short * const ring = o->ring;
  const unsigned long prod = o->produced;
  unsigned int src, dst;

  if (o->out_mode == 8)
    {
      unsigned char * const ring8 = o->ring8;

      src = (unsigned int)((prod - d) & mask);
      dst = (unsigned int)(prod & mask);

      if (d >= ml && d <= o->window_n - ml)
        {
          if (dst + ml <= o->window_n && src + ml <= o->window_n)
            {
              (void)memcpy (ring8 + dst, ring8 + src, (size_t)ml);
            }
          else
            {
              copy_ring_range8 (ring8, o->window_n, dst, src, ml);
            }
        }
      else if (d == 1U)
        {
          unsigned char v = ring8[src];
          unsigned int remaining = ml;

          while (remaining)
            {
              unsigned int count = remaining;
              unsigned int dst_left = o->window_n - dst;

              if (count > dst_left)
                {
                  count = dst_left;
                }

              (void)memset (ring8 + dst, v, count);
              dst = (dst + count) & mask;
              remaining -= count;
            }
        }
      else if (d < ml && d <= o->window_n / 2U && ml >= 8U)
        {
          unsigned int copied = d;

          copy_ring_range8 (ring8, o->window_n, dst, src, d);

          while (copied < ml)
            {
              unsigned int count = ml - copied;

              if (count > copied)
                {
                  count = copied;
                }

              copy_ring_range8 (ring8, o->window_n, (dst + copied) & mask,
                                dst, count);
              copied += count;
            }
        }
      else if (dst + ml <= o->window_n && src + ml <= o->window_n)
        {
          unsigned int i;

          for (i = 0; i < ml; ++i)
            {
              ring8[dst + i] = ring8[src + i];
            }
        }
      else
        {
          unsigned int i;

          for (i = 0; i < ml; ++i)
            {
              unsigned char v = ring8[src];

              ring8[dst] = v;
              src = (src + 1U) & mask;
              dst = (dst + 1U) & mask;
            }
        }

      o->produced += ml;
      o->in_block += ml;
      return;
    }

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
  else if (d < ml && d <= o->window_n / 2U && ml >= 8U)
    {
      unsigned int copied = d;

      copy_ring_range (ring, o->window_n, dst, src, d);

      while (copied < ml)
        {
          unsigned int count = ml - copied;

          if (count > copied)
            {
              count = copied;
            }

          copy_ring_range (ring, o->window_n, (dst + copied) & mask,
                           dst, count);
          copied += count;
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
          const unsigned char *src;

          if (seg > n)
            {
              seg = n;
            }

          src = o->ring8 + at;

          while (seg)
            {
              unsigned int avail
                  = (unsigned int)(sizeof (o->outbuf) - o->out_used);
              unsigned int chunk = seg < avail ? seg : avail;

              (void)memcpy (o->outbuf + o->out_used, src, chunk);
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
block_crc_end8 (Output *o, unsigned int n, unsigned long expected)
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
      unsigned char values[8];
      const unsigned char *source;

      if (used > CRC_BUFFER_N - 9U)
        {
          crc = crc_update (crc, o->crc_buf, used);
          used = 0;
        }

      if (at <= mask - 7U)
        {
          source = o->ring8 + at;
        }
      else
        {
          unsigned int j;

          for (j = 0; j < 8U; ++j)
            {
              values[j] = o->ring8[(at + j) & mask];
            }

          source = values;
        }

      pack8_bytes (source, o->crc_buf + used);
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
          values[j] = o->ring8[(at + j) & mask];
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
block_crc_end (Output *o, unsigned int n, unsigned long expected)
{
  unsigned long start, crc = 0UL;
  unsigned int i = 0, used = 0;
  const unsigned int mask = o->window_n - 1U;

  if (o->out_mode == 8)
    {
      return block_crc_end8 (o, n, expected);
    }

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

#if defined(LZ9ANS_PTHREADS)
static void
free_thread_decoder_buffers (ThreadDecoderBuffers *buffers)
{
  unsigned int i;

  if (!buffers)
    {
      return;
    }

  for (i = 0; i < ENTROPY_STREAM_COUNT - 1U; ++i)
    {
      free (buffers->symbols[i]);
      buffers->symbols[i] = (unsigned char *)0;
    }

  buffers->capacity = 0U;
}

/*****************************************************************************/

static int
allocate_thread_decoder_buffers (ThreadDecoderBuffers *buffers,
                                 unsigned int min_match)
{
  unsigned char *symbols[ENTROPY_STREAM_COUNT - 1U];
  unsigned int i, capacity = decoder_symbol_capacity (min_match);

  if (buffers->capacity >= capacity)
    {
      return 1;
    }

  (void)memset (symbols, 0, sizeof (symbols));

  for (i = 0; i < ENTROPY_STREAM_COUNT - 1U; ++i)
    {
      symbols[i] = (unsigned char *)malloc (capacity);

      if (!symbols[i])
        {
          unsigned int j;

          for (j = 0; j < ENTROPY_STREAM_COUNT - 1U; ++j)
            {
              if (symbols[j])
                {
                  free (symbols[j]);
                }
            }

          return 0;
        }
    }

  for (i = 0; i < ENTROPY_STREAM_COUNT - 1U; ++i)
    {
      free (buffers->symbols[i]);
      buffers->symbols[i] = symbols[i];
    }

  buffers->capacity = capacity;

  return 1;
}

/*****************************************************************************/

static void
decode_stream_task (void *arg)
{
  ThreadDecodeWorkspace *workspace = (ThreadDecodeWorkspace *)arg;
  unsigned int i;

  workspace->okay = 1;

  for (i = 0; i < workspace->count; ++i)
    {
      unsigned int symbol = stream_get (&workspace->stream);

      if (workspace->stream.br.failed || symbol > 255U)
        {
          workspace->okay = 0;
          return;
        }

      workspace->symbols[i] = (unsigned char)symbol;
    }
}

/*****************************************************************************/

static int
decode_streams_parallel (Stream streams[ENTROPY_STREAM_COUNT],
                         unsigned int count, unsigned int min_match,
                         ThreadPool *pool, ThreadDecoderBuffers *buffers)
{
  unsigned int kind;
  ThreadDecodeWorkspace workspaces[ENTROPY_STREAM_COUNT - 1U];
  ThreadTask tasks[ENTROPY_STREAM_COUNT - 1U];

  if (!allocate_thread_decoder_buffers (buffers, min_match))
    {
      return 0;
    }

  for (kind = STREAM_LL; kind <= STREAM_OF; ++kind)
    {
      ThreadDecodeWorkspace *workspace = &workspaces[kind - STREAM_LL];

      workspace->stream = streams[kind];
      workspace->symbols = buffers->symbols[kind - STREAM_LL];
      workspace->count = count;
      workspace->okay = 0;
      tasks[kind - STREAM_LL].function = decode_stream_task;
      tasks[kind - STREAM_LL].context = workspace;
    }

  (void)thread_pool_run (pool, tasks, ENTROPY_STREAM_COUNT - 1U);

  for (kind = STREAM_LL; kind <= STREAM_OF; ++kind)
    {
      const ThreadDecodeWorkspace *workspace = &workspaces[kind - STREAM_LL];

      streams[kind] = workspace->stream;

      if (!workspace->okay)
        {
          return 0;
        }
    }

  return 1;
}
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

static int
decode_block (FILE *in, Output *o, unsigned int max_n, unsigned char *stage,
              unsigned char *desc, DecTable *tables, DecTable **phase_tables,
              int have_control, unsigned char control, ThreadPool *pool,
              ThreadDecoderBuffers *decoder_buffers)
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
  int parallel_codes = 0;
  BitReader raw_extra;
  ExtraReader extra;

  if (have_control)
    {
      h[0] = control;

      if ((h[0] & 3U) != BLOCK_STORED && (h[0] & 3U) != BLOCK_LZ)
        {
          return 0;
        }

      if (!read_exact (in, h + 1, BLOCK_PREFIX_N - 1U))
        {
          return 0;
        }
    }
  else if (!read_exact (in, h, BLOCK_PREFIX_N))
    {
      return 0;
    }

  type = h[0] & 3U;

  if (type == BLOCK_STORED)
    {
      unsigned long stored_bits, stored_bytes;

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

      if (!n || n > max_n || n > BLOCK_N || o->produced > ULONG_MAX - n)
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

      return store_stored_symbols (o, stage, n, width)
             && o->in_block == n && block_crc_end (o, n, expected);
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
      || width != 0U || !n || n > max_n || n > BLOCK_N
      || o->produced > ULONG_MAX - n || q > n / min_match
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

      if (!parse_descriptor (&s[i], tables, phase_tables, kind,
                             o->of_alphabet, o->phased, desc, &off,
                             desc_len))
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

#if defined(LZ9ANS_PTHREADS)
  if (pool && pool->participants > 1U && q
      && s[STREAM_LL].mode != MODE_PHASED
      && s[STREAM_ML].mode != MODE_PHASED)
    {
      if (!decode_streams_parallel (s, q, min_match, pool, decoder_buffers))
        {
          return 0;
        }

      parallel_codes = 1;
    }
#else
  (void)pool;
  (void)decoder_buffers;
#endif /* if defined(LZ9ANS_PTHREADS) */

  {
    unsigned int produced = 0, literals = 0;

    for (i = 0; i < q; ++i)
      {
        unsigned int j, lr, ml, d, code;

        code = parallel_codes ? decoder_buffers->symbols[0][i] /* //-V547 */
                              : stream_get_ph (&s[STREAM_LL],
                                               (unsigned int)o->produced & 3U);
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
        code =
          parallel_codes ? /* //-V547 */
               decoder_buffers->symbols[STREAM_ML - STREAM_LL][i]
                         : stream_get_ph (&s[STREAM_ML],
                                          (unsigned int)o->produced & 3U);
        ml = decode_len (code, min_match, &extra);
        code =
          parallel_codes ? /* //-V547 */
               decoder_buffers->symbols[STREAM_OF - STREAM_LL][i]
                         : stream_get (&s[STREAM_OF]);
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
allocated_encoder (Encoder *e, unsigned int window_n,
                    unsigned int min_match)
{
  (void)memset (e, 0, sizeof (*e));
  e->window_n = window_n;
  e->seq_capacity = sequence_capacity (min_match);
  e->of_alphabet = window_n == WINDOW_64K ? OF_ALPHABET_64K : OF_ALPHABET_32K;
  e->buffer = (unsigned short *)calloc (window_n + BLOCK_N,
                                         sizeof (unsigned short));
  e->seq = (Sequence *)malloc ((size_t)e->seq_capacity * sizeof (Sequence));
  e->head = (int *)malloc (HASH_N * sizeof (int));
  e->link = (int *)malloc (window_n * sizeof (int));
  e->stage = (unsigned char *)malloc (STAGE_MAX);
  e->desc = (unsigned char *)malloc (DESC_MAX);
  e->table = (AnsTable *)malloc (sizeof (AnsTable));
  e->gtable = (AnsTable *)malloc (PHASE_GROUPS_MAX * sizeof (AnsTable));
#if defined(LZ9ANS_PTHREADS)
  e->prefix = (unsigned int *)malloc ((BLOCK_N + 1U) * sizeof (unsigned int));
#endif /* if defined(LZ9ANS_PTHREADS) */

  if (e->stage)
    {
      (void)memset (e->stage + STAGE_DATA_MAX, 0, TABLE_CACHE_BYTES);
    }

  if (e->link)
    {
      (void)memset (e->link, 0, window_n * sizeof (int));
    }

  return e->buffer && e->seq && e->head && e->link && e->stage && e->desc
         && e->table && e->gtable
#if defined(LZ9ANS_PTHREADS)
         && e->prefix
#endif /* if defined(LZ9ANS_PTHREADS) */
         ;
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
#if defined(LZ9ANS_PTHREADS)
  free (e->prefix);
#endif /* if defined(LZ9ANS_PTHREADS) */
}

/*****************************************************************************/

static int
cleanup_encode_run (Encoder *e, ThreadPool *pool,
                    ThreadEncoderWorkspace *workspaces)
{
  int okay = thread_pool_destroy (pool);

#if defined(LZ9ANS_PTHREADS)
  free_thread_encoder_workspaces (workspaces);
#else
  (void)workspaces;
#endif /* if defined(LZ9ANS_PTHREADS) */

  free_encoder (e);

  return okay;
}

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
static int
read_next_input_block (FILE *in, unsigned int mode, unsigned long elements,
                       int unknown_count, unsigned long done,
                       unsigned int *acc, unsigned int *nbits,
                       unsigned int *input_eof, unsigned char *raw,
                       unsigned short *symbols, unsigned int *n_out)
{
  unsigned int n, i;
  size_t input_bytes = 0U;

  if (!unknown_count && done >= elements)
    {
      return 0;
    }

  n = unknown_count ? BLOCK_N
                    : (unsigned int)((elements - done) > BLOCK_N
                                         ? BLOCK_N : (elements - done));

  if (mode == 8U)
    {
      if (unknown_count)
        {
          if (!read_some (in, raw, BLOCK_N, &input_bytes))
            {
              return -1;
            }

          if (feof (in))
            {
              *input_eof = 1U;
            }

          if (!input_bytes)
            {
              return *input_eof ? 0 : -1;
            }

          n = (unsigned int)input_bytes;
        }
      else if (!read_exact (in, raw, (size_t)n))
        {
          return -1;
        }

      for (i = 0; i < n; ++i)
        {
          symbols[i] = raw[i];
        }
    }
  else
    {
      unsigned int byte_pos = 0U;

      if (unknown_count)
        {
          unsigned int target_bits = BLOCK_N * 9U;
          unsigned int target_bytes
              = target_bits <= *nbits ? 0U
                : (target_bits - *nbits + 7U) / 8U;
          unsigned long available_bits;

          if (!read_some (in, raw, (size_t)target_bytes, &input_bytes))
            {
              return -1;
            }

          if (feof (in))
            {
              *input_eof = 1U;
            }

          available_bits = (unsigned long)*nbits
                           + (unsigned long)input_bytes * 8UL;
          n = (unsigned int)(available_bits / 9UL);

          if (n > BLOCK_N)
            {
              n = BLOCK_N;
            }

          if (*input_eof && input_bytes)
            {
              unsigned long tail_bits
                  = available_bits - (unsigned long)n * 9UL;

              if (tail_bits > 7UL
                  || (tail_bits
                      && (raw[input_bytes - 1U]
                          & ((1U << tail_bits) - 1U))))
                {
                  return -1;
                }
            }

          if (!n)
            {
              if (*input_eof && !input_bytes)
                {
                  if (*acc != 0U)
                    {
                      return -1;
                    }

                  *nbits = 0U;

                  return 0;
                }

              return -1;
            }
        }
      else
        {
          unsigned int needed_bits = n * 9U;
          unsigned int needed_bytes
              = needed_bits <= *nbits ? 0U
                : (needed_bits - *nbits + 7U) / 8U;

          if (needed_bytes
              && !read_exact (in, raw, (size_t)needed_bytes))
            {
              return -1;
            }

          input_bytes = (size_t)needed_bytes;
        }

      i = 0U;

      if (*nbits == 0U)
        {
# if defined(__GNUC__) && (defined(__x86_64__) || defined(__amd64__))
          while (i + 8U <= n)
            {
              const unsigned char *p = raw + byte_pos;
              unsigned short *dst = symbols + i;
              lz9_u64 hi_be, hi, w0, w1;

              (void)memcpy (&hi_be, p, 8U);
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
# else
          while (i + 8U <= n)
            {
              const unsigned char *p = raw + byte_pos;
              unsigned short *dst = symbols + i;

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
# endif /* if defined(__GNUC__) && (defined(__x86_64__) \
           || defined(__amd64__)) */
        }

      while (i < n)
        {
          while (*nbits < 9U)
            {
              if (byte_pos >= input_bytes)
                {
                  return -1;
                }

              *acc = (*acc << 8) | (unsigned int)raw[byte_pos++];
              *nbits += 8U;
            }

          *nbits -= 9U;
          symbols[i] = (unsigned short)((*acc >> *nbits) & 511U);
          *acc &= (1U << *nbits) - 1U;
          ++i;
        }
    }

  *n_out = n;

  return 1;
}

/*****************************************************************************/

static void
parallel_block_encode_task (void *arg)
{
  ThreadBlockJob *job = (ThreadBlockJob *)arg;

  BlockOutput output;

  output.file = (FILE *)0;
  output.buffer = job->output;
  output.capacity = job->output_capacity;
  output.size = 0U;
  job->okay = encode_block (&job->encoder, (ThreadPool *)0,
                            (ThreadEncoderWorkspace *)0,
                            (NormScratch *)(void *)job->encoder.link,
                            &output);
  job->output_size = output.size;
}

/*****************************************************************************/

static unsigned int
block_thread_capacity (unsigned long elements, int unknown_count,
                       unsigned int thread_limit)
{
  unsigned int capacity = compression_participants (thread_limit);

  if (!unknown_count)
    {
      unsigned long blocks = elements / BLOCK_N
                             + (elements % BLOCK_N != 0UL ? 1UL : 0UL);

      if (blocks < (unsigned long)capacity)
        {
          capacity = (unsigned int)blocks;
        }
    }

  return capacity ? capacity : 1U;
}

/*****************************************************************************/

static int
encode_file_parallel_blocks (FILE *in, FILE *out, unsigned int mode,
                             unsigned long elements, int unknown_count,
                             int level, unsigned int window_n,
                             unsigned int features, unsigned int thread_limit)
{
  static const unsigned short depth[9]
      = { 2, 4, 8, 12, 24, 48, 96, 192, 512 };
  ThreadPool pool;
  ThreadBlockJob *jobs = (ThreadBlockJob *)0;
  ThreadTask *tasks = (ThreadTask *)0;
  unsigned short *history = (unsigned short *)0;
  unsigned char *raw = (unsigned char *)0;
  unsigned int history_n = 0U;
  unsigned long done = 0UL;
  unsigned int acc = 0U, nbits = 0U, input_eof = 0U;
  unsigned char hdr[FRAME_HEADER_PREFIX_N + 3U + 4U];
  unsigned int header_len = unknown_count ? FRAME_HEADER_PREFIX_N + 7U
                                          : FHEAD_N;
  unsigned int participants, i;
  int okay = 0, pool_ready = 0;

  if (!thread_pool_init (&pool, thread_limit,
                         block_thread_capacity (elements, unknown_count,
                                                thread_limit)))
    {
      return 0;
    }

  pool_ready = 1;
  participants = pool.participants;
  jobs = (ThreadBlockJob *)calloc (participants, sizeof (ThreadBlockJob));
  tasks = (ThreadTask *)calloc (participants, sizeof (ThreadTask));
  history = (unsigned short *)malloc (window_n * sizeof (unsigned short));
  raw = (unsigned char *)calloc (1U, STAGE_MAX);

  if (!jobs || !tasks || !history || !raw)
    {
      goto cleanup;
    }

  for (i = 0; i < participants; ++i)
    {
      Encoder *e = &jobs[i].encoder;

      if (!allocated_encoder (e, window_n,
                              (features & FEATURE_MIN_MATCH_2)
                                  ? MIN_MATCH_SHORT : MIN_MATCH_DEFAULT))
        {
          goto cleanup;
        }

      jobs[i].output = (unsigned char *)malloc ((size_t)BLOCK_OUTPUT_MAX);
      jobs[i].output_capacity = (size_t)BLOCK_OUTPUT_MAX;

      if (!jobs[i].output)
        {
          goto cleanup;
        }

      e->search_depth = depth[level - 1];
      e->lazy = level >= 5;
      e->level = level;
      e->of_alphabet = window_n == WINDOW_64K ? OF_ALPHABET_64K
                                              : OF_ALPHABET_32K;
      e->input_mode = mode;
      e->features = features;
      e->min_match = (features & FEATURE_MIN_MATCH_2) ? MIN_MATCH_SHORT
                                                     : MIN_MATCH_DEFAULT;
      tasks[i].function = parallel_block_encode_task;
      tasks[i].context = &jobs[i];
    }

  (void)memset (hdr, 0, sizeof (hdr));
  (void)memcpy (hdr, "LZ9A", 4U);
  hdr[4] = FORMAT_V9;
  hdr[5] = (unsigned char)mode;
  store16 (hdr + 6, header_len);
  store32 (hdr + 8, unknown_count ? 0UL : elements);
  hdr[12] = (unsigned char)((window_n == WINDOW_64K ? FEATURE_WINDOW_64K : 0U)
                            | (features & (FEATURE_PHASED_TABLES
                                           | FEATURE_MIN_MATCH_2))
                            | FEATURE_ALIGNED_DISTANCES
                            | FEATURE_AUTO_ALIGNED_DISTANCES);

  if (unknown_count)
    {
      hdr[13] = FRAME_EXT_UNKNOWN_COUNT;
      store16 (hdr + 14, 0U);
      store32 (hdr + 16, crc_bytes (hdr, 16U));
    }
  else
    {
      store32 (hdr + 13, crc_bytes (hdr, 13U));
    }

  if (!write_exact (out, hdr, header_len))
    {
      goto cleanup;
    }

  for (;;)
    {
      unsigned int batch_count = 0U;
      int read_status = 1;

      while (batch_count < participants && read_status > 0)
        {
          ThreadBlockJob *job = &jobs[batch_count];
          Encoder *e = &job->encoder;
          unsigned int n = 0U;

          read_status = read_next_input_block (in, mode, elements,
                                               unknown_count, done, &acc,
                                               &nbits, &input_eof, raw,
                                               e->buffer + window_n, &n);

          if (read_status < 0)
            {
              goto cleanup;
            }

          if (read_status == 0)
            {
              break;
            }

          if (done > ULONG_MAX - n)
            {
              goto cleanup;
            }

          if (history_n)
            {
              (void)memcpy (e->buffer + window_n - history_n, history,
                            history_n * sizeof (unsigned short));
            }

          e->n = n;
          e->hlen = history_n;
          e->base = 0U;
          e->chain_state = 0;
          e->frame_pos = done;
          e->distance_flags = 0U;
          e->use_cost = 0;
          e->sn = 0U;
          e->lit_count = 0U;
          e->last_lit = 0U;
          e->max_literal = 0U;
          e->desc_len = 0UL;
          e->stage_len = 0UL;
          (void)memset (e->reps, 0, sizeof (e->reps));
          (void)memset (e->stream_bits, 0, sizeof (e->stream_bits));
          (void)memset (e->stream_bytes, 0, sizeof (e->stream_bytes));
          (void)memset (e->stage + STAGE_DATA_MAX, 0, TABLE_CACHE_BYTES);

          if (n >= window_n)
            {
              (void)memcpy (history, e->buffer + window_n + n - window_n,
                            window_n * sizeof (unsigned short));
              history_n = window_n;
            }
          else
            {
              unsigned int keep = history_n;

              if (keep + n > window_n)
                {
                  unsigned int drop = keep + n - window_n;

                  (void)memmove (history, history + drop,
                                 (keep - drop) * sizeof (unsigned short));
                  keep -= drop;
                }

              (void)memcpy (history + keep, e->buffer + window_n,
                            n * sizeof (unsigned short));
              history_n = keep + n;
            }

          done += n;
          ++batch_count;
        }

      if (!batch_count)
        {
          break;
        }

      if (read_status < 0
          || !thread_pool_run (&pool, tasks, batch_count))
        {
          goto cleanup;
        }

      for (i = 0; i < batch_count; ++i)
        {
          if (!jobs[i].okay
              || !write_exact (out, jobs[i].output, jobs[i].output_size))
            {
              goto cleanup;
            }
        }
    }

  if (mode == 9U && acc != 0U)
    {
      goto cleanup;
    }

  if (unknown_count)
    {
      unsigned char end_control = BLOCK_END_CONTROL;

      if (!write_exact (out, &end_control, 1U))
        {
          goto cleanup;
        }
    }
  else if (fgetc (in) != EOF)
    {
      goto cleanup;
    }

  if (ferror (in) || ferror (out))
    {
      goto cleanup;
    }

  okay = 1;

cleanup:
  if (jobs)
    {
      for (i = 0; i < participants; ++i)
        {
          free (jobs[i].output);
          free_encoder (&jobs[i].encoder);
        }
    }

  free (jobs);
  free (tasks);
  free (history);
  free (raw);

  if (pool_ready && !thread_pool_destroy (&pool))
    {
      okay = 0;
    }

  return okay;
}
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

static int
encode_file (FILE *in, FILE *out, unsigned int mode, unsigned long elements,
             int unknown_count, int level, unsigned int window_n,
             unsigned int features, unsigned int thread_limit)
{
  static const unsigned short depth[9] = { 2, 4, 8, 12, 24, 48, 96, 192, 512 };
  Encoder e;
  ThreadPool pool;
  ThreadEncoderWorkspace *workspaces = (ThreadEncoderWorkspace *)0;
  unsigned long done = 0;
  unsigned int acc = 0, nbits = 0;
  unsigned int input_eof = 0;
  unsigned char hdr[FRAME_HEADER_PREFIX_N + 3U + 4U];
  unsigned int header_len = unknown_count ? FRAME_HEADER_PREFIX_N + 7U
                                          : FHEAD_N;

#if defined(LZ9ANS_PTHREADS)
  if (compression_participants (thread_limit) > 1U)
    {
      return encode_file_parallel_blocks (in, out, mode, elements,
                                          unknown_count, level, window_n,
                                          features, thread_limit);
    }
#endif /* if defined(LZ9ANS_PTHREADS) */

  if (!allocated_encoder (&e, window_n,
                          (features & FEATURE_MIN_MATCH_2)
                              ? MIN_MATCH_SHORT : MIN_MATCH_DEFAULT))
    {
      free_encoder (&e);

      return 0;
    }

  if (!thread_pool_init (&pool, thread_limit, ENTROPY_STREAM_COUNT))
    {
      free_encoder (&e);

      return 0;
    }

#if defined(LZ9ANS_PTHREADS)
  if (pool.participants > 1U)
    {
      workspaces = allocate_thread_encoder_workspaces ();

      if (!workspaces)
        {
          (void)thread_pool_destroy (&pool);
          free_encoder (&e);

          return 0;
        }
    }
#endif /* if defined(LZ9ANS_PTHREADS) */

  e.search_depth = depth[level - 1];
  e.lazy = level >= 5;
  e.level = level;
  e.of_alphabet = window_n == WINDOW_64K ? OF_ALPHABET_64K
                                         : OF_ALPHABET_32K;
  e.input_mode = mode;
  e.features = features;
  e.min_match = (features & FEATURE_MIN_MATCH_2) ? MIN_MATCH_SHORT
                                                 : MIN_MATCH_DEFAULT;
  (void)memset (hdr, 0, sizeof (hdr));
  (void)memcpy (hdr, "LZ9A", 4);
  hdr[4] = FORMAT_V9;
  hdr[5] = (unsigned char)mode;
  store16 (hdr + 6, header_len);
  store32 (hdr + 8, unknown_count ? 0UL : elements);
  hdr[12] = (unsigned char)((window_n == WINDOW_64K ? FEATURE_WINDOW_64K : 0U)
                            | (features & (FEATURE_PHASED_TABLES
                                           | FEATURE_MIN_MATCH_2))
                            | FEATURE_ALIGNED_DISTANCES
                            | FEATURE_AUTO_ALIGNED_DISTANCES);
  if (unknown_count)
    {
      hdr[13] = FRAME_EXT_UNKNOWN_COUNT;
      store16 (hdr + 14, 0U);
      store32 (hdr + 16, crc_bytes (hdr, 16U));
    }
  else
    {
      store32 (hdr + 13, crc_bytes (hdr, 13U));
    }

  if (!write_exact (out, hdr, header_len))
    {
      (void)cleanup_encode_run (&e, &pool, workspaces);

      return 0;
    }

  for (;;)
    {
      unsigned long remaining = unknown_count ? BLOCK_N : elements - done;
      unsigned int i, n = (unsigned int)(remaining > BLOCK_N ? BLOCK_N
                                                             : remaining);
      unsigned int hist_new;
      size_t input_bytes = 0;

      if (unknown_count)
        {
          if (input_eof)
            {
              break;
            }

          if (mode == 8)
            {
              if (!read_some (in, e.stage, BLOCK_N, &input_bytes))
                {
                  (void)cleanup_encode_run (&e, &pool, workspaces);

                  return 0;
                }

              if (feof (in))
                {
                  input_eof = 1;
                }

              if (!input_bytes)
                {
                  if (!input_eof)
                    {
                      (void)cleanup_encode_run (&e, &pool, workspaces);

                      return 0;
                    }

                  break;
                }

              n = (unsigned int)input_bytes;
            }
          else
            {
              unsigned int target_bits = BLOCK_N * 9U;
              unsigned int target_bytes
                  = target_bits <= nbits ? 0U
                    : (target_bits - nbits + 7U) / 8U;
              unsigned long available_bits;

              if (!read_some (in, e.stage, (size_t)target_bytes, &input_bytes))
                {
                  (void)cleanup_encode_run (&e, &pool, workspaces);

                  return 0;
                }

              if (feof (in))
                {
                  input_eof = 1;
                }

              available_bits = (unsigned long)nbits
                               + (unsigned long)input_bytes * 8UL;
              n = (unsigned int)(available_bits / 9UL);

              if (n > BLOCK_N)
                {
                  n = BLOCK_N;
                }

              if (input_eof)
                {
                  unsigned long tail_bits
                      = available_bits - (unsigned long)n * 9UL;

                  if (tail_bits > 7UL
                      || (tail_bits
                          && (!input_bytes
                              || (e.stage[input_bytes - 1U]
                                  & ((1U << tail_bits) - 1U)))))
                    {
                      (void)cleanup_encode_run (&e, &pool, workspaces);

                      return 0;
                    }
                }

              if (!n)
                {
                  if (input_eof && !input_bytes && !nbits)
                    {
                      break;
                    }

                  (void)cleanup_encode_run (&e, &pool, workspaces);

                  return 0;
                }
            }
        }
      else if (done >= elements)
        {
          break;
        }

      if (done > ULONG_MAX - n)
        {
          (void)cleanup_encode_run (&e, &pool, workspaces);

          return 0;
        }

      e.n = n;
      e.frame_pos = done;

      if (mode == 8)
        {
          if (!unknown_count && !read_exact (in, e.stage, (size_t)n))
            {
              (void)cleanup_encode_run (&e, &pool, workspaces);

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

          if (unknown_count)
            {
              if ((size_t)needed_bytes > input_bytes)
                {
                  (void)cleanup_encode_run (&e, &pool, workspaces);

                  return 0;
                }
            }
          else if (needed_bytes
                   && !read_exact (in, e.stage, (size_t)needed_bytes))
            {
              (void)cleanup_encode_run (&e, &pool, workspaces);

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

      {
        BlockOutput block_output;

        block_output.file = out;
        block_output.buffer = (unsigned char *)0;
        block_output.capacity = 0U;
        block_output.size = 0U;

        if (!encode_block (&e, &pool, workspaces, (NormScratch *)0,
                           &block_output))
          {
            (void)cleanup_encode_run (&e, &pool, workspaces);

            return 0;
          }
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
      e.base += n;

      if (e.chain_state == 2 && e.base <= CHAIN_BASE_MAX)
        {
          e.chain_state = 1;
        }
      else
        {
          e.chain_state = 0;
          e.base = e.base <= CHAIN_BASE_MAX ? e.base : 0U;
        }

      done += n;
    }

  if (mode == 9 && acc != 0)
    {
      (void)cleanup_encode_run (&e, &pool, workspaces);

      return 0;
    }

  if (unknown_count)
    {
      unsigned char end_control = BLOCK_END_CONTROL;

      if (!write_exact (out, &end_control, 1U))
        {
          (void)cleanup_encode_run (&e, &pool, workspaces);

          return 0;
        }
    }
  else if (fgetc (in) != EOF)
    {
      (void)cleanup_encode_run (&e, &pool, workspaces);

      return 0;
    }

  if (ferror (in) || ferror (out))
    {
      (void)cleanup_encode_run (&e, &pool, workspaces);

      return 0;
    }

  return cleanup_encode_run (&e, &pool, workspaces);
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
decode_end_record (FILE *in, unsigned char control, unsigned int mode,
                   unsigned long total)
{
  unsigned char record[FRAME_HEADER_MAX_N + 3U];
  unsigned char crcbuf[4];
  unsigned char seen[128];
  unsigned int length, pos, end;

  if (control == BLOCK_END_CONTROL)
    {
      return 1;
    }

  if (control != BLOCK_END_EXT_CONTROL
      || !read_exact (in, record + 1, 2U))
    {
      return 0;
    }

  record[0] = control;
  length = load16 (record + 1);

  if (!length || length > FRAME_HEADER_MAX_N
      || !read_exact (in, record + 3, (size_t)length)
      || !read_exact (in, crcbuf, sizeof (crcbuf))
      || load32 (crcbuf) != crc_bytes (record, length + 3U))
    {
      return 0;
    }

  (void)memset (seen, 0, sizeof (seen));
  pos = 3U;
  end = length + 3U;

  while (pos < end)
    {
      unsigned int id, n, index;

      if (end - pos < 3U)
        {
          return 0;
        }

      id = record[pos++];
      n = load16 (record + pos);
      pos += 2U;

      if (!(id & 0x7fU) || n > end - pos)
        {
          return 0;
        }

      index = id & 0x7fU;

      if (seen[index])
        {
          return 0;
        }

      seen[index] = 1;

      if (id == 0x82U)
        {
          if (n != 1U || mode != 9U || !total || record[pos] != 0U)
            {
              return 0;
            }
        }
      else if (id & 0x80U)
        {
          return 0;
        }

      pos += n;
    }

  return pos == end;
}

/*****************************************************************************/

static int
decode_file (FILE *in, FILE *out, unsigned int thread_limit)
{
  unsigned char hdr[FRAME_HEADER_MAX_N];
  ThreadPool pool;
  ThreadDecoderBuffers decoder_buffers;
  unsigned long total, done = 0;
  unsigned int feature_flags, window_n, header_len;
  unsigned char *stage = (unsigned char *)0, *desc = (unsigned char *)0;
  DecTable *tables = (DecTable *)0, *phase_tables = (DecTable *)0;
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
      || !parse_frame_extensions (hdr, header_len, total, &unknown_count))
    {
      return 0;
    }

#if defined(LZ9ANS_PTHREADS)
  if (!thread_pool_init (&pool, thread_limit, 3U))
    {
      return 0;
    }
#else
  (void)thread_pool_init (&pool, thread_limit, 3U);
#endif /* if defined(LZ9ANS_PTHREADS) */

  (void)memset (&decoder_buffers, 0, sizeof (decoder_buffers));

  window_n
      = (feature_flags & FEATURE_WINDOW_64K) ? WINDOW_64K : WINDOW_DEFAULT;
  stage = (unsigned char *)malloc (STAGE_MAX);
  desc = (unsigned char *)malloc (DESC_MAX);
  tables = (DecTable *)calloc (DEC_BASE_TABLE_COUNT, sizeof (DecTable));
  o = (Output *)calloc (1U, sizeof (Output));

  if (!stage || !desc || !tables || !o)
    {
      goto cleanup;
    }

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

  if (o->out_mode == 8)
    {
      o->ring8 = (unsigned char *)malloc (window_n * sizeof (unsigned char));

      if (!o->ring8)
        {
          goto cleanup;
        }
    }
  else
    {
      o->ring = (unsigned short *)calloc (window_n, sizeof (unsigned short));

      if (!o->ring)
        {
          goto cleanup;
        }
    }

  if (unknown_count)
    {
      for (;;)
        {
          int control = fgetc (in);

          if (control == EOF)
            {
              goto cleanup;
            }

          if (((unsigned int)control & 3U) == (BLOCK_END_CONTROL & 3U))
            {
              if (!decode_end_record (in, (unsigned char)control,
                                      (unsigned int)o->out_mode, o->produced))
                {
                  goto cleanup;
                }

              break;
            }

          if (!decode_block (in, o, BLOCK_N, stage, desc, tables,
                             &phase_tables, 1, (unsigned char)control, &pool,
                             &decoder_buffers))
            {
              goto cleanup;
            }

          done += o->in_block;
        }
    }
  else
    {
      while (done < total)
        {
          unsigned long left = total - done;
          unsigned int remaining
              = (unsigned int)(left > BLOCK_N ? BLOCK_N : left);

          if (!decode_block (in, o, remaining, stage, desc, tables,
                             &phase_tables, 0, 0U, &pool, &decoder_buffers))
            {
              goto cleanup;
            }

          done += o->in_block;
        }
    }

  if (!unknown_count)
    {
      int control = fgetc (in);

      if (control != EOF)
        {
          if (((unsigned int)control & 3U) != (BLOCK_END_CONTROL & 3U)
              || !decode_end_record (in, (unsigned char)control,
                                     (unsigned int)o->out_mode, o->produced))
            {
              goto cleanup;
            }
        }
      else if (ferror (in))
        {
          goto cleanup;
        }
    }

  if (o->out_mode == 9 && o->out_bits)
    {
      if (!output_octet (o, o->out_acc << (8 - o->out_bits)))
        {
          goto cleanup;
        }
    }

  if (!output_flush (o) || (!feof (in) && fgetc (in) != EOF)
      || ferror (in) || ferror (out))
    {
      goto cleanup;
    }

  okay = 1;

cleanup:
#if defined(LZ9ANS_PTHREADS)
  if (!thread_pool_destroy (&pool))
    {
      okay = 0;
    }
#else
  (void)thread_pool_destroy (&pool);
#endif /* if defined(LZ9ANS_PTHREADS) */

#if defined(LZ9ANS_PTHREADS)
  free_thread_decoder_buffers (&decoder_buffers);
#endif /* if defined(LZ9ANS_PTHREADS) */
  free (stage);
  free (desc);
  free (tables);
  free (phase_tables);

  if (o)
    {
      free (o->ring);
      free (o->ring8);
    }

  free (o);

  return okay;
}

/*****************************************************************************/

static int
file_size (FILE *in, LZ9FileSize *size)
{
#if defined(_WIN32) || defined(_WIN64)
  LZ9FileOffset bytes;

  if (_fseeki64 (in, 0, SEEK_END) != 0)
    {
      return 0;
    }

  bytes = _ftelli64 (in);

  if (bytes < 0 || _fseeki64 (in, 0, SEEK_SET) != 0)
    {
      return 0;
    }

  *size = (LZ9FileSize)bytes;

  return 1;
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
  off_t bytes;

  if (fseeko (in, (off_t)0, SEEK_END) != 0)
    {
      return 0;
    }

  bytes = ftello (in);

  if (bytes < (off_t)0 || fseeko (in, (off_t)0, SEEK_SET) != 0)
    {
      return 0;
    }

  *size = (LZ9FileSize)bytes;

  return 1;
#else
  long bytes;

  if (fseek (in, 0L, SEEK_END) != 0)
    {
      return 0;
    }

  bytes = ftell (in);

  if (bytes < 0L || fseek (in, 0L, SEEK_SET) != 0)
    {
      return 0;
    }

  *size = (LZ9FileSize)bytes;

  return 1;
#endif /* if defined(_WIN32) || defined(_WIN64) */
}

/*****************************************************************************/

static FILE *
open_output_file (FILE *input, const char *path, int check_input,
                  int *same_file)
{
  *same_file = 0;

#if defined(_WIN32) || defined(_WIN64)
  {
    HANDLE output_handle;
    DWORD output_type;
    int fd;
    FILE *out;

    output_handle = CreateFileA (path, GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 (LPSECURITY_ATTRIBUTES)0, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, (HANDLE)0);

    if (output_handle == INVALID_HANDLE_VALUE)
      {
        errno = EIO;

        return (FILE *)0;
      }

    SetLastError (NO_ERROR);
    output_type = GetFileType (output_handle);

    if (output_type == FILE_TYPE_UNKNOWN && GetLastError () != NO_ERROR)
      {
        (void)CloseHandle (output_handle);
        errno = EIO;

        return (FILE *)0;
      }

    if (output_type == FILE_TYPE_DISK)
      {
        if (check_input)
          {
            HANDLE input_handle
                = (HANDLE)_get_osfhandle (_fileno (input));
            DWORD input_type;

            if (input_handle == INVALID_HANDLE_VALUE)
              {
                (void)CloseHandle (output_handle);
                errno = EIO;

                return (FILE *)0;
              }

            SetLastError (NO_ERROR);
            input_type = GetFileType (input_handle);

            if (input_type == FILE_TYPE_UNKNOWN
                && GetLastError () != NO_ERROR)
              {
                (void)CloseHandle (output_handle);
                errno = EIO;

                return (FILE *)0;
              }

            if (input_type == FILE_TYPE_DISK)
              {
                BY_HANDLE_FILE_INFORMATION input_info, output_info;

                if (!GetFileInformationByHandle (input_handle, &input_info)
                    || !GetFileInformationByHandle (output_handle,
                                                    &output_info))
                  {
                    (void)CloseHandle (output_handle);
                    errno = EIO;

                    return (FILE *)0;
                  }

                if (input_info.dwVolumeSerialNumber
                        == output_info.dwVolumeSerialNumber
                    && input_info.nFileIndexHigh == output_info.nFileIndexHigh
                    && input_info.nFileIndexLow == output_info.nFileIndexLow)
                  {
                    (void)CloseHandle (output_handle);
                    *same_file = 1;
                    errno = EINVAL;

                    return (FILE *)0;
                  }
              }
          }

        {
          if (SetFilePointer (output_handle, 0L, (PLONG)0, FILE_BEGIN)
                  == INVALID_SET_FILE_POINTER
              || !SetEndOfFile (output_handle))
            {
              (void)CloseHandle (output_handle);
              errno = EIO;

              return (FILE *)0;
            }
        }
      }

    fd = _open_osfhandle ((intptr_t)output_handle, _O_WRONLY | _O_BINARY);

    if (fd < 0)
      {
        (void)CloseHandle (output_handle);

        return (FILE *)0;
      }

    out = _fdopen (fd, "wb");

    if (!out)
      {
        (void)_close (fd);

        return (FILE *)0;
      }

    return out;
  }
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
  {
    int fd;
    FILE *out;

    fd = open (path, O_WRONLY | O_CREAT, (mode_t)0666);

    if (fd < 0)
      {
        return (FILE *)0;
      }

    {
      struct stat output_stat;

      if (fstat (fd, &output_stat) != 0)
        {
          int saved_errno = errno;

          (void)close (fd);
          errno = saved_errno;

          return (FILE *)0;
        }

      if (check_input)
        {
          struct stat input_stat;

          if (fstat (fileno (input), &input_stat) != 0)
            {
              int saved_errno = errno;

              (void)close (fd);
              errno = saved_errno;

              return (FILE *)0;
            }

          if (input_stat.st_dev == output_stat.st_dev
              && input_stat.st_ino == output_stat.st_ino)
            {
              (void)close (fd);
              *same_file = 1;
              errno = EINVAL;

              return (FILE *)0;
            }
        }

      if (S_ISREG (output_stat.st_mode) && ftruncate (fd, (off_t)0) != 0)
        {
          int saved_errno = errno;

          (void)close (fd);
          errno = saved_errno;

          return (FILE *)0;
        }
    }

    out = fdopen (fd, "wb");

    if (!out)
      {
        int saved_errno = errno;

        (void)close (fd);
        errno = saved_errno;

        return (FILE *)0;
      }

    return out;
  }
#else
  (void)input;
  (void)check_input;

  return fopen (path, "wb");
#endif /* if defined(_WIN32) || defined(_WIN64) */
}

/*****************************************************************************/

static int
input_count (FILE *in, unsigned int mode, unsigned long *count)
{
  LZ9FileSize bytes, symbols;

  if (!file_size (in, &bytes))
    {
      return 0;
    }

  if (mode == 8)
    {
      symbols = bytes;
    }
  else
    {
      LZ9FileSize check;

      /*
       * ceil(9*N/8) == bytes.
       * All byte positions aren't valid for N.
       * NOTE: Keep this arithmetic 64-bit because a valid 32-bit symbol
       * count can occupy more than 4 GiB!
       */

      symbols = (bytes / 9U) * 8U
                + ((bytes % 9U) * 8U) / 9U;
      check = (symbols / 8U) * 9U
              + (((symbols % 8U) * 9U + 7U) / 8U);

      if (check != bytes)
        {
          return 0;
        }
    }

  if (symbols > (LZ9FileSize)0xffffffffUL)
    {
      return 0;
    }

  *count = (unsigned long)symbols;

  return 1;
}

/*****************************************************************************/

static void
print_file_size (FILE *stream, const char *label, LZ9FileSize size)
{
  char digits[32];
  unsigned int length = 0, i;

  do
    {
      digits[length++] = (char)('0' + (unsigned int)(size % 10U));
      size /= 10U;
    }
  while (size && length < sizeof (digits) - 1U);

  digits[length] = '\0';

  for (i = 0; i < length / 2U; ++i)
    {
      char tmp = digits[i];

      digits[i] = digits[length - i - 1U];
      digits[length - i - 1U] = tmp;
    }

  (void)fprintf (stream, "%s%s bytes\n", label, digits);
}

/*****************************************************************************/

static unsigned int
default_features (int level)
{
  return level >= 6 ? FEATURE_PHASED_TABLES | FEATURE_MIN_MATCH_2 : 0U;
}

/*****************************************************************************/

static int
parse_thread_limit (const char *option, unsigned int *limit)
{
  const char *p = option;
  unsigned int value = 0U;

  if (strncmp (p, "--threads=", 10U) != 0)
    {
      return 0;
    }

  p += 10;

  if (!*p)
    {
      return 0;
    }

  while (*p)
    {
      unsigned int digit;

      if (*p < '0' || *p > '9')
        {
          return 0;
        }

      digit = (unsigned int)(*p - '0');

      if (value > (UINT_MAX - digit) / 10U)
        {
          return 0;
        }

      value = value * 10U + digit;
      ++p;
    }

  *limit = clamp_thread_limit (value);

  return 1;
}

/*****************************************************************************/

#if defined(LZ9ANS_PTHREADS)
static unsigned int
memory_thread_participants (unsigned int limit, unsigned int max_participants)
{
  unsigned int participants = compression_participants (limit);

  if (participants > max_participants)
    {
      participants = max_participants;
    }

  return participants ? participants : 1U;
}
#endif /* if defined(LZ9ANS_PTHREADS) */

/*****************************************************************************/

static void
print_memory_usage (unsigned int window_n, unsigned int thread_limit)
{
  unsigned long enc_fixed, enc, enc_levels_1_5;
  unsigned long enc_seq_max, enc_seq_levels_1_5;
  unsigned long dec_base, dec, dec_base8, dec8, dec_phase;

  enc_seq_max = (unsigned long)sequence_capacity (MIN_MATCH_SHORT)
                * sizeof (Sequence);
  enc_seq_levels_1_5 = (unsigned long)sequence_capacity (MIN_MATCH_DEFAULT)
                       * sizeof (Sequence);
  enc_fixed = (unsigned long)sizeof (Encoder)
#if defined(LZ9ANS_PTHREADS)
              + (unsigned long)(BLOCK_N + 1U) * sizeof (unsigned int)
#endif /* if defined(LZ9ANS_PTHREADS) */
              + (unsigned long)(window_n + BLOCK_N) * sizeof (unsigned short)
              + (unsigned long)HASH_N * sizeof (int)
              + (unsigned long)window_n * sizeof (int) + STAGE_MAX + DESC_MAX
              + (unsigned long)sizeof (AnsTable)
              + (unsigned long)PHASE_GROUPS_MAX * sizeof (AnsTable);
  enc = enc_fixed + enc_seq_max;
  enc_levels_1_5 = enc_fixed + enc_seq_levels_1_5;

  dec_base = (unsigned long)sizeof (Output)
             + (unsigned long)window_n * sizeof (unsigned short)
             + STAGE_MAX + DESC_MAX
             + (unsigned long)DEC_BASE_TABLE_COUNT * sizeof (DecTable)
             + (unsigned long)ENTROPY_STREAM_COUNT * sizeof (Stream);
  dec_base8 = (unsigned long)sizeof (Output)
              + (unsigned long)window_n * sizeof (unsigned char)
              + STAGE_MAX + DESC_MAX
              + (unsigned long)DEC_BASE_TABLE_COUNT * sizeof (DecTable)
              + (unsigned long)ENTROPY_STREAM_COUNT * sizeof (Stream);
  dec_phase = (unsigned long)DEC_PHASE_TABLE_COUNT * sizeof (DecTable);
  dec = dec_base + dec_phase;
  dec8 = dec_base8 + dec_phase;

  (void)printf ("window: %u symbols\n", window_n);
  (void)printf ("static CRC lookup table: %lu bytes\n",
                (unsigned long)sizeof (crc_table));
  (void)printf ("encoder maximum allocated state: %lu bytes"
                " (levels 6-9)\n", enc);
  (void)printf ("encoder allocated state at levels 1-5: %lu bytes\n",
                enc_levels_1_5);
  (void)printf ("decoder base allocated state: %lu bytes"
                " (without phased tables)\n", dec_base);
  (void)printf ("decoder maximum allocated state: %lu bytes"
                " (9-bit output; including %lu bytes of lazily allocated"
                " phased tables)\n", dec, dec_phase);
  (void)printf ("decoder maximum allocated state for 8-bit output: %lu"
                " bytes (including phased tables)\n", dec8);

#if defined(LZ9ANS_PTHREADS)
  {
    unsigned int cpus = available_processors ();
    unsigned int cp = compression_participants (thread_limit);
    unsigned int dp = memory_thread_participants (thread_limit, 3U);
    unsigned long block_state = 0UL;
    unsigned long block_state_levels_1_5 = 0UL;
    unsigned long block_stack = (unsigned long)(cp - 1U)
                                * THREAD_STACK_BYTES;
    unsigned long decoder_symbols = 0UL;
    unsigned long decoder_symbols_levels_1_5 = 0UL;
    unsigned long decoder_pool_state = 0UL;
    unsigned long decoder_stack = (unsigned long)(dp - 1U)
                                  * THREAD_STACK_BYTES;

    if (cp > 1U)
      {
        block_state
            = (unsigned long)cp * enc
              + (unsigned long)cp
                    * ((unsigned long)sizeof (ThreadBlockJob)
                       - (unsigned long)sizeof (Encoder)
                       + (unsigned long)BLOCK_OUTPUT_MAX
                       + 2UL * sizeof (ThreadTask))
              + (unsigned long)window_n * sizeof (unsigned short)
              + STAGE_MAX
              + (unsigned long)(cp - 1U)
                    * (sizeof (pthread_t) + sizeof (ThreadWorker));
        block_state_levels_1_5
            = (unsigned long)cp * enc_levels_1_5
              + (unsigned long)cp
                    * ((unsigned long)sizeof (ThreadBlockJob)
                       - (unsigned long)sizeof (Encoder)
                       + (unsigned long)BLOCK_OUTPUT_MAX
                       + 2UL * sizeof (ThreadTask))
              + (unsigned long)window_n * sizeof (unsigned short)
              + STAGE_MAX
              + (unsigned long)(cp - 1U)
                    * (sizeof (pthread_t) + sizeof (ThreadWorker));
      }

    if (dp > 1U)
      {
        decoder_symbols
            = (unsigned long)(ENTROPY_STREAM_COUNT - 1U)
              * decoder_symbol_capacity (MIN_MATCH_SHORT);
        decoder_symbols_levels_1_5
            = (unsigned long)(ENTROPY_STREAM_COUNT - 1U)
              * decoder_symbol_capacity (MIN_MATCH_DEFAULT);
        decoder_pool_state
            = (unsigned long)(dp - 1U)
                  * (sizeof (pthread_t) + sizeof (ThreadWorker))
              + 3UL * sizeof (ThreadTask);
      }

    (void)printf ("pthread build: %u available logical processors;"
                  " --threads=%u\n", cpus, thread_limit);
    (void)printf ("compression mode: %s\n",
                  cp > 1U ? "independent blocks" : "sequential");
    (void)printf ("compression participants: %u (main thread included)\n",
                  cp);
    if (cp > 1U)
      {
        (void)printf ("parallel-block encoder state (maximum allocated): %lu"
                      " bytes (levels 6-9; all worker contexts, output"
                      " buffers, history, input scratch, and thread-pool"
                      " allocations)\n", block_state);
        (void)printf ("parallel-block encoder state at levels 1-5: %lu"
                      " bytes\n", block_state_levels_1_5);
      }
    (void)printf ("compression worker-stack budget: %lu bytes\n",
                  block_stack);
    (void)printf ("decompression participants: %u (main thread included)\n",
                  dp);
    (void)printf ("decompression symbol buffers: %lu bytes maximum"
                  " (levels 6-9); %lu bytes at levels 1-5\n",
                  decoder_symbols, decoder_symbols_levels_1_5);
    (void)printf ("decompression thread-pool allocations: %lu bytes\n",
                  decoder_pool_state);
    (void)printf ("decompression worker-stack budget: %lu bytes\n",
                  decoder_stack);
    (void)printf ("worker stack size: %lu bytes per pthread\n",
                  THREAD_STACK_BYTES);
  }
#else
  (void)thread_limit;
#endif /* if defined(LZ9ANS_PTHREADS) */
}

/*****************************************************************************/

int
main (int argc, char **argv)
{
  FILE *in, *out;
  unsigned long count = 0;
  LZ9FileSize input_bytes = 0, output_bytes = 0;
  unsigned int mode = 0;
  int level = 5, verbose = 0, level_seen = 0, window_seen = 0, i, okay;
  int threads_seen = 0;
  int input_is_stdio, output_is_stdio, unknown_count, size_reporting = 1;
  int output_is_same_file = 0;
  unsigned int window_n = WINDOW_DEFAULT;
  unsigned int features, thread_limit = 0U;
  clock_t start, finish;

  if (argc >= 2
      && (!strcmp (argv[1], "--memory")
          || !strcmp (argv[1], "--memory-32k")
          || !strcmp (argv[1], "--memory-64k")))
    {
      int seen = 0;

      if (argc > 4)
        {
          (void)fprintf (stderr, "Invalid memory-reporting option.\n");

          return 2;
        }

      for (i = 2; i < argc; ++i)
        {
          if (parse_thread_limit (argv[i], &thread_limit))
            {
              if (seen)
                {
                  (void)fprintf (stderr, "Duplicate --threads option.\n");

                  return 2;
                }

              seen = 1;
            }
          else
            {
              (void)fprintf (stderr, "Invalid memory-reporting option: %s\n",
                             argv[i]);

              return 2;
            }
        }

      print_memory_usage (!strcmp (argv[1], "--memory-64k")
                              ? WINDOW_64K : WINDOW_DEFAULT,
                          thread_limit);

      return 0;
    }

  if (argc < 4 || argc > 15
      || (strcmp (argv[1], "c8") != 0 && strcmp (argv[1], "c9") != 0
          && strcmp (argv[1], "d") != 0))
    {
      (void)fprintf (stderr,
              "LZ9ANS reference compressor/decompressor " LZ9VER "\n\n");
#if defined(LZ9ANS_PTHREADS)
      (void)fprintf (stderr,
              "Usage:\n"
              "  lz9ans <c8|c9> <input> <output> [-1..-9] [-v]"
              " [--window=W] [--threads=N]\n");
      (void)fprintf (stderr,
              "  lz9ans d <input> <output> [-v] [--threads=N]\n\n");
#else
      (void)fprintf (stderr,
              "Usage:\n"
              "  lz9ans <c8|c9> <input> <output> [-1..-9] [-v]"
              " [--window=W]\n");
      (void)fprintf (stderr,
              "  lz9ans d <input> <output> [-v]\n\n");
#endif /* if defined(LZ9ANS_PTHREADS) */
      (void)fprintf (stderr,
              "  Specify an input or output name of '-' for stdin");
      (void)fprintf (stderr,
              " or stdout.\n");
      (void)fprintf (stderr,
              "  c8: 8-bit input;");
      (void)fprintf (stderr,
              " c9: mxload-style packed 9-bit input\n\n");
      (void)fprintf (stderr,
              "  -1..-9:              ");
      (void)fprintf (stderr,
              "1-4: fast; 5-8: tight; 9: maximum (default: -5)\n");
      (void)fprintf (stderr,
              "  -v:       ");
      (void)fprintf (stderr,
              "           report file size and compression ratio details\n");
      (void)fprintf (stderr,
              "  --window=W:");
      (void)fprintf (stderr,
              "          use W (32k or 64k) history window (default: 32k)\n");
#if defined(LZ9ANS_PTHREADS)
      (void)fprintf (stderr,
              "  --threads=N:         limit to N"
              " concurrent threads (default: automatic)\n");
#endif /* if defined(LZ9ANS_PTHREADS) */
      return 2;
    }

  if (argv[1][0] == 'c')
    {
      mode = (unsigned int)(argv[1][1] - '0');
    }

  for (i = 4; i < argc; ++i)
    {
      if (strncmp (argv[i], "--threads", 9U) == 0)
        {
          if (threads_seen || !parse_thread_limit (argv[i], &thread_limit))
            {
              (void)fprintf (stderr,
                  "Invalid or duplicate --threads option: %s\n", argv[i]);

              return 2;
            }

          threads_seen = 1;
        }
      else if (!strcmp (argv[i], "-v"))
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

  input_is_stdio = strcmp (argv[2], "-") == 0;
  output_is_stdio = strcmp (argv[3], "-") == 0;
  unknown_count = mode && input_is_stdio;

  if (!input_is_stdio && !output_is_stdio && !strcmp (argv[2], argv[3]))
    {
      (void)fprintf (stderr, "Input and output paths must differ.\n");

      return 2;
    }

  in = input_is_stdio ? stdin : fopen (argv[2], "rb");

  if (!in)
    {
      perror (argv[2]);

      return 2;
    }

  if (verbose && (input_is_stdio || output_is_stdio))
    {
      size_reporting = 0;
      (void)fprintf (stderr,
              "warning: -v is unavailable when stdin/stdout is used.\n");
    }
  else if (verbose && !file_size (in, &input_bytes))
    {
      (void)fprintf (stderr,
              "Cannot determine input size for -v reporting.\n");

      if (!input_is_stdio)
        {
          (void)fclose (in);
        }

      return 2;
    }

  if (mode && !unknown_count && !input_count (in, mode, &count))
    {
      (void)fprintf (stderr, "Invalid input size.\n");

      if (!input_is_stdio)
        {
          (void)fclose (in);
        }

      return 2;
    }

  out = output_is_stdio
            ? stdout
            : open_output_file (in, argv[3], !input_is_stdio,
                                &output_is_same_file);

  if (!out)
    {
      if (output_is_same_file)
        {
          (void)fprintf (stderr,
                         "Input and output paths refer to the same file.\n");
        }
      else
        {
          perror (argv[3]);
        }

      if (!input_is_stdio)
        {
          (void)fclose (in);
        }

      return 2;
    }

  features = default_features (level);
  init_crc ();
  init_costs ();
  start = clock ();

  if (mode)
    {
      okay = encode_file (in, out, mode, count, unknown_count, level,
                          window_n, features, thread_limit);
    }
  else
    {
      okay = decode_file (in, out, thread_limit);
    }

  finish = clock ();

  if (output_is_stdio || verbose)
    {
      if (fflush (out) != 0)
        {
          okay = 0;
        }
    }

  if (verbose && size_reporting && !file_size (out, &output_bytes))
    {
      okay = 0;
    }

  if (!output_is_stdio && fclose (out) != 0)
    {
      okay = 0;
    }

  if (!input_is_stdio && fclose (in) != 0)
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

  if (verbose && size_reporting)
    {
      if (mode)
        {
          print_file_size (stderr, "Input size: ", input_bytes);
          print_file_size (stderr, "Output size: ", output_bytes);

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
          print_file_size (stderr, "Input size: ", input_bytes);
          print_file_size (stderr, "Output size: ", output_bytes);

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
