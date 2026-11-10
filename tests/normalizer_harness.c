/*
 * LZ9ANS - Normalizer test
 * Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
 * Copyright (c) 2025-2026 The DPS8M Development Team
 * SPDX-License-Identifier: MIT
 * scspell-id: fb203c4c-dd17-11f1-a854-80ee73e9b8e7
 */

/*****************************************************************************/

#define main lz9ans_program_main
#define LZ9ANS_TEST_NORMALIZER 1
#include "../lz9ans.c"
#undef main

int
main (void)
{
  unsigned int counts[512], idx[512];
  unsigned short old_freq[512], fast_freq[512], kl_freq[512];
  unsigned int active, total, i, symbol, count;

  while (scanf ("%u %u", &active, &total) == 2)
    {
      if (active < 2U || active > 512U)
        {
          return 2;
        }

      (void)memset (counts, 0, sizeof (counts));
      (void)memset (old_freq, 0, sizeof (old_freq));
      (void)memset (fast_freq, 0, sizeof (fast_freq));
      (void)memset (kl_freq, 0, sizeof (kl_freq));

      for (i = 0U; i < active; ++i)
        {
          if (scanf ("%u %u", &symbol, &count) != 2 || symbol >= 512U
              || !count)
            {
              return 3;
            }

          idx[i] = symbol;
          counts[symbol] = count;
        }

      if (!norm_active_legacy (counts, idx, active, total, old_freq)
          || !norm_active_legacy_select (counts, idx, active, total, fast_freq)
          || !norm_active_kl (counts, idx, active, total, kl_freq))
        {
          (void)printf ("FAIL\n");

          continue;
        }

      (void)printf ("OK");

      for (i = 0U; i < active; ++i)
        {
          (void)printf (" %u", (unsigned int)old_freq[idx[i]]);
        }

      (void)printf (" |");

      for (i = 0U; i < active; ++i)
        {
          (void)printf (" %u", (unsigned int)fast_freq[idx[i]]);
        }

      (void)printf (" |");

      for (i = 0U; i < active; ++i)
        {
          (void)printf (" %u", (unsigned int)kl_freq[idx[i]]);
        }

      (void)printf ("\n");
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
