/* LZ9ANS - CRC-32/ISO-HDLC hardware acceleration
 * Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
 * Copyright (c) 2025-2026 The DPS8M Development Team
 * SPDX-License-Identifier: MIT
 * scspell-id: 94503f56-d733-11f1-abc1-80ee73e9b8e7
 */

/*****************************************************************************/

/*
 * NOTE: This code is NOT strictly ANSI C89 conforming.
 * Define LZ9ANS_NO_CRC32_HW to use software-only CRC-32.
 */

/*****************************************************************************/

#ifndef LZ9ANS_CRC32_HW_H
# define LZ9ANS_CRC32_HW_H
# include <stddef.h>
# if !defined(LZ9ANS_NO_CRC32_HW) && defined(__GNUC__)
#  if defined(__x86_64__) || defined(__amd64__)
#   define LZ9_CRC32_PCLMUL 1

/*****************************************************************************/

# if defined(__GNUC__) && !defined(__CPPCHECK__) && !defined(__LINT__)
#  if !defined(USE_BI_CRC)
#   define USE_BI_CRC
#  endif /* if !defined(USE_BI_CRC) */
# else
#  include <string.h>
# endif /* if defined(__GNUC__) && !defined(__CPPCHECK__) \
           && !defined(__LINT__) */

/*****************************************************************************/

static int
lz9_crc32_pclmul_available (void)
{
  static int cached = 0; /* 0 = unknown, 1 = absent, 2 = present */
  int v;
  unsigned int a, b, c, d;
#   if defined(__clang__) || (__GNUC__ > 4) \
    || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
  v = __atomic_load_n (&cached, __ATOMIC_RELAXED);

  if (v != 0)
    {
      return v == 2;
    }
#   else  /* if defined( __clang__ ) || ( __GNUC__ > 4 ) \
             || ( __GNUC__ == 4 && __GNUC_MINOR__ >= 7 ) */
  (void)cached;
#   endif /* if defined( __clang__ ) || ( __GNUC__ > 4 ) \
             || ( __GNUC__ == 4 && __GNUC_MINOR__ >= 7 ) */
  a = 1U;
  c = 0U;

  __asm__ __volatile__ ("cpuid"
                        : "+a"(a), "=b"(b), "+c"(c), "=d"(d)
                        :
                        : "memory");
  (void)a;
  (void)b;
  (void)d;
  v = (c & 2U) ? 2 : 1; /* CPUID.01H:ECX.PCLMULQDQ[1] */
#   if defined(__clang__) || (__GNUC__ > 4) \
    || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
  __atomic_store_n (&cached, v, __ATOMIC_RELAXED);
#   endif /* if defined( __clang__ ) || ( __GNUC__ > 4 ) \
             || ( __GNUC__ == 4 && __GNUC_MINOR__ >= 7 ) */

  return v == 2;
}

/*****************************************************************************/

static unsigned int
lz9_crc32_pclmul (unsigned int crc, const unsigned char *buf, size_t len)
{
  static const unsigned int keys[4][4] = {
    { 0x54442bd4U, 1U, 0xc6e41596U, 1U }, /* 512-bit fold      */
    { 0x751997d0U, 1U, 0xccaa009eU, 0U }, /* 128-bit fold      */
    { 0x63cd6124U, 1U,          0U, 0U }, /* 128 -> 64 -> 32   */
    { 0xdb710641U, 1U, 0xf7011641U, 1U }  /* Barrett reduction */
  };

  const unsigned char *p = buf;
  size_t n = len;
  unsigned int result;
  unsigned int seed = crc ^ 0xffffffffU;

  __asm__ __volatile__ (
      "movdqu   0(%[p]), %%xmm0\n\t"
      "movdqu  16(%[p]), %%xmm1\n\t"
      "movdqu  32(%[p]), %%xmm2\n\t"
      "movdqu  48(%[p]), %%xmm3\n\t"
      "movd %[seed], %%xmm5\n\t"
      "pxor %%xmm5, %%xmm0\n\t"
      "addq $64, %[p]\n\t"
      "subq $64, %[n]\n\t"
      "movdqu 0(%[keys]), %%xmm4\n\t"
      "cmpq $64, %[n]\n\t"
      "jb 2f\n\t"
      "1:\n\t"
      /* Each lane represents every fourth 16-byte block. */
      "movdqa %%xmm0, %%xmm5\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm0\n\t"
      "pclmulqdq $0x11, %%xmm4, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm0\n\t"
      "movdqu 0(%[p]), %%xmm6\n\t"
      "pxor %%xmm6, %%xmm0\n\t"
      "movdqa %%xmm1, %%xmm5\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm1\n\t"
      "pclmulqdq $0x11, %%xmm4, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm1\n\t"
      "movdqu 16(%[p]), %%xmm6\n\t"
      "pxor %%xmm6, %%xmm1\n\t"
      "movdqa %%xmm2, %%xmm5\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm2\n\t"
      "pclmulqdq $0x11, %%xmm4, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm2\n\t"
      "movdqu 32(%[p]), %%xmm6\n\t"
      "pxor %%xmm6, %%xmm2\n\t"
      "movdqa %%xmm3, %%xmm5\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm3\n\t"
      "pclmulqdq $0x11, %%xmm4, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm3\n\t"
      "movdqu 48(%[p]), %%xmm6\n\t"
      "pxor %%xmm6, %%xmm3\n\t"
      "addq $64, %[p]\n\t"
      "subq $64, %[n]\n\t"
      "cmpq $64, %[n]\n\t"
      "jae 1b\n\t"
      "2:\n\t"
      /* Merge four independent accumulators in stream order. */
      "movdqu 16(%[keys]), %%xmm4\n\t"
      "movdqa %%xmm0, %%xmm5\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm0\n\t"
      "pclmulqdq $0x11, %%xmm4, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm0\n\t"
      "pxor %%xmm1, %%xmm0\n\t"
      "movdqa %%xmm0, %%xmm5\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm0\n\t"
      "pclmulqdq $0x11, %%xmm4, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm0\n\t"
      "pxor %%xmm2, %%xmm0\n\t"
      "movdqa %%xmm0, %%xmm5\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm0\n\t"
      "pclmulqdq $0x11, %%xmm4, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm0\n\t"
      "pxor %%xmm3, %%xmm0\n\t"
      /* Remaining full 16-byte blocks, using 128-bit folding. */
      "cmpq $16, %[n]\n\t"
      "jb 4f\n\t"
      "3:\n\t"
      "movdqa %%xmm0, %%xmm5\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm0\n\t"
      "pclmulqdq $0x11, %%xmm4, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm0\n\t"
      "movdqu (%[p]), %%xmm6\n\t"
      "pxor %%xmm6, %%xmm0\n\t"
      "addq $16, %[p]\n\t"
      "subq $16, %[n]\n\t"
      "cmpq $16, %[n]\n\t"
      "jae 3b\n\t"
      "4:\n\t"
      /* Fold 128 bits to 64, then reduce with Barrett. */
      "movdqa %%xmm0, %%xmm5\n\t"
      "pclmulqdq $0x10, %%xmm4, %%xmm0\n\t"
      "psrldq $8, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm0\n\t"
      "pcmpeqd %%xmm7, %%xmm7\n\t"
      "psrldq $12, %%xmm7\n\t"
      "movdqa %%xmm0, %%xmm5\n\t"
      "pand %%xmm7, %%xmm5\n\t"
      "movdqu 32(%[keys]), %%xmm4\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm5\n\t"
      "psrldq $4, %%xmm0\n\t"
      "pxor %%xmm5, %%xmm0\n\t"
      "movdqa %%xmm0, %%xmm5\n\t"
      "pand %%xmm7, %%xmm5\n\t"
      "movdqu 48(%[keys]), %%xmm4\n\t"
      "pclmulqdq $0x10, %%xmm4, %%xmm5\n\t"
      "pand %%xmm7, %%xmm5\n\t"
      "pclmulqdq $0x00, %%xmm4, %%xmm5\n\t"
      "pxor %%xmm5, %%xmm0\n\t"
      "psrlq $32, %%xmm0\n\t"
      "movd %%xmm0, %[out]\n\t"
      "notl %[out]\n\t"
      : [out] "=r"(result), [p] "+r"(p), [n] "+r"(n)
      : [seed] "r"(seed), [keys] "r"(keys)
      : "cc", "memory", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6",
        "xmm7");

  return result;
}

/*****************************************************************************/

#  endif /* if defined( __x86_64__ ) || defined( __amd64__ ) */
#  if defined(__aarch64__) && !defined(__AARCH64EB__) \
    && (defined(__clang__) || __GNUC__ >= 7)
#   define LZ9_CRC32_ARM 1
#   define LZ9_ARM_CRC_BIT (1UL << 7)
#   if defined(__linux__)
#    include <sys/auxv.h>
#    ifndef AT_HWCAP
#     define AT_HWCAP 16
#    endif /* ifndef AT_HWCAP */
extern unsigned long getauxval (unsigned long);
#   elif defined(__FreeBSD__)
#    include <sys/auxv.h>
#   elif defined(__OpenBSD__)
#    include <sys/param.h>
#    if defined(OpenBSD) && (OpenBSD >= 202409)
#     include <sys/auxv.h>
#    endif /* if defined( OpenBSD ) && ( OpenBSD >= 202409 ) */
#   elif defined(__APPLE__) && defined(__MACH__)
#    include <sys/sysctl.h>
#    include <sys/types.h>
#   endif /* if defined( __linux__ ) */
#   include <arm_acle.h>
#   include <stdint.h> /* C99 */

/*****************************************************************************/

static unsigned long
lz9_arm_crc_capabilities (void)
{
#   if defined(__linux__)
  return getauxval (AT_HWCAP);
#   elif defined(__FreeBSD__)
  unsigned long caps = 0;

  if (elf_aux_info (AT_HWCAP, &caps, sizeof (caps)) != 0)
    {
      return 0;
    }

  return caps;
#   elif defined(__OpenBSD__) && defined(OpenBSD) && (OpenBSD >= 202409) \
    && defined(AT_HWCAP)
  unsigned long caps = 0;

  if (elf_aux_info (AT_HWCAP, &caps, sizeof (caps)) != 0)
    {
      return 0;
    }

  return caps;
#   elif defined(__APPLE__) && defined(__MACH__)
  unsigned long caps = 0;
  int v = 0;
  size_t n = sizeof (v);

  if (sysctlbyname ("hw.optional.armv8_crc32", &v, &n, NULL, 0) == 0 && v)
    {
      caps |= LZ9_ARM_CRC_BIT;
    }

  return caps;
#   else /* if defined( __linux__ ) */
  return 0;
#   endif /* if defined( __linux__ ) */
}

/*****************************************************************************/

static int
lz9_crc32_arm_available (void)
{
  static unsigned long checked = 0;
  static int initialized = 0;

  if (!__atomic_load_n (&initialized, __ATOMIC_ACQUIRE))
    {
      unsigned long caps = lz9_arm_crc_capabilities ();
      __atomic_store_n (&checked, caps, __ATOMIC_RELAXED);
      __atomic_store_n (&initialized, 1, __ATOMIC_RELEASE);
    }

  return (__atomic_load_n (&checked, __ATOMIC_RELAXED) & LZ9_ARM_CRC_BIT) != 0;
}

/*****************************************************************************/

static __attribute__ ((target ("+crc"), noinline)) unsigned int
lz9_crc32_arm (unsigned int crc, const unsigned char *p, size_t n)
{
  unsigned int c = crc ^ 0xffffffffU;

  while (n >= 8U)
    {
      uint64_t v;
#if defined(USE_BI_CRC)
      __builtin_memcpy (&v, p, 8U);
#else
# include <string.h>
      (void)memcpy (&v, p, 8U);
#endif /* if defined(USE_BI_CRC) */
      c = __crc32d (c, v);
      p += 8;
      n -= 8U;
    }

  while (n--)
    {
      c = __crc32b (c, *p++);
    }

  return c ^ 0xffffffffU;
}
#  endif /* if defined( __aarch64__ ) && !defined( __AARCH64EB__ ) \
            && ( defined( __clang__ ) || __GNUC__ >= 7 ) */
# endif /* if !defined( LZ9ANS_NO_CRC32_HW ) && defined( __GNUC__ ) */
#endif /* ifndef LZ9ANS_CRC32_HW_H */

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
