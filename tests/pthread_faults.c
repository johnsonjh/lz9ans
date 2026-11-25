/*****************************************************************************/

/*
 * LZ9ANS - Pthread fault test
 * Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
 * Copyright (c) 2025-2026 The DPS8M Development Team
 * SPDX-License-Identifier: MIT
 * scspell-id: da0a3548-e45c-11f1-8a67-80ee73e9b8e7
 */

/*****************************************************************************/

/* NOTE: AI/LLM code generation was used for this file. */

/*****************************************************************************/

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*****************************************************************************/

static void
record_event (const char *kind, const char *operation, unsigned int call)
{
  /* flawfinder: ignore */
  const char *path = getenv ("LZ9ANS_TEST_PTHREAD_TRACE");

  if (path)
    {
      FILE *trace = fopen (path, "a");

      if (trace)
        {
          (void)fprintf (trace, "%s %s %u\n", kind, operation, call);
          (void)fclose (trace);
        }
    }
}

/*****************************************************************************/

static int
should_fail (const char *operation, unsigned int call)
{
  /* flawfinder: ignore */
  const char *fault = getenv ("LZ9ANS_TEST_PTHREAD_FAULT");
  char expected[64];
  const char *match;
  size_t expected_length;

  (void)sprintf (expected, "%s:%u", operation, call); /* flawfinder: ignore */

  if (!fault)
    {
      record_event ("call", operation, call);
      return 0;
    }

  expected_length = strlen (expected);
  match = fault;

  while ((match = strstr (match, expected)) != (const char *)0)
    {
      if ((match == fault || match[-1] == ',')
          && (match[expected_length] == '\0'
              || match[expected_length] == ','))
        {
          record_event ("injected", operation, call);
          return 1;
        }

      ++match;
    }

  record_event ("call", operation, call);
  return 0;
}

/*****************************************************************************/

long
lz9_test_sysconf (int name)
{
  if (name == _SC_NPROCESSORS_ONLN)
    {
      return 4L;
    }

  return sysconf (name);
}

/*****************************************************************************/

int
lz9_test_pthread_create (pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start_routine) (void *), void *arg)
{
  static unsigned int calls;

  ++calls;

  if (should_fail ("create", calls))
    {
      return EAGAIN;
    }

  return pthread_create (thread, attr, start_routine, arg);
}

/*****************************************************************************/

int
lz9_test_pthread_join (pthread_t thread, void **value_ptr)
{
  static unsigned int calls;

  ++calls;

  if (should_fail ("join", calls))
    {
      return EDEADLK;
    }

  return pthread_join (thread, value_ptr);
}

/*****************************************************************************/

int
lz9_test_pthread_cond_destroy (pthread_cond_t *cond)
{
  static unsigned int calls;

  ++calls;

  if (should_fail ("cond_destroy", calls))
    {
      return EBUSY;
    }

  return pthread_cond_destroy (cond);
}

/*****************************************************************************/

int
lz9_test_pthread_mutex_destroy (pthread_mutex_t *mutex)
{
  static unsigned int calls;

  ++calls;

  if (should_fail ("mutex_destroy", calls))
    {
      return EBUSY;
    }

  return pthread_mutex_destroy (mutex);
}

/*****************************************************************************/

int
lz9_test_pthread_attr_destroy (pthread_attr_t *attr)
{
  static unsigned int calls;

  ++calls;

  if (should_fail ("attr_destroy", calls))
    {
      return EINVAL;
    }

  return pthread_attr_destroy (attr);
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
