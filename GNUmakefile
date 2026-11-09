# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT
##############################################################################

CC ?= cc
CFLAGS = -D_ALL_SOURCE -D_GNU_SOURCE -O3 -ansi -pedantic -Wall -Wextra \
	-Werror
THREADS ?= none

##############################################################################

ifeq ($(THREADS),pthreads)
  THREAD_DEFS = -DLZ9ANS_PTHREADS
  THREAD_CFLAGS ?= -pthread
  THREAD_LDFLAGS ?= -pthread
else ifneq ($(THREADS),none)
  $(error THREADS must be either 'none' or 'pthreads')
endif

##############################################################################

all:
	$(MAKE) lz9ans THREADS=pthreads || $(MAKE) lz9ans THREADS=none

##############################################################################

FORCE:

##############################################################################

.build-config: FORCE
	@printf '%s\n' 'CC=$(CC)' 'CFLAGS=$(CFLAGS)' \
		'EXTRA_CFLAGS=$(EXTRA_CFLAGS)' 'LDFLAGS=$(LDFLAGS)' \
		'EXTRA_LDFLAGS=$(EXTRA_LDFLAGS)' 'THREADS=$(THREADS)' \
		'THREAD_CFLAGS=$(THREAD_CFLAGS)' \
		'THREAD_LDFLAGS=$(THREAD_LDFLAGS)' | \
		cmp -s - $@ || printf '%s\n' 'CC=$(CC)' 'CFLAGS=$(CFLAGS)' \
		'EXTRA_CFLAGS=$(EXTRA_CFLAGS)' 'LDFLAGS=$(LDFLAGS)' \
		'EXTRA_LDFLAGS=$(EXTRA_LDFLAGS)' 'THREADS=$(THREADS)' \
		'THREAD_CFLAGS=$(THREAD_CFLAGS)' \
		'THREAD_LDFLAGS=$(THREAD_LDFLAGS)' > $@

##############################################################################

lz9ans: lz9ans.c crc32_hw.h .build-config
	@eval echo \
		$${CC:-$(CC)} $${EXTRA_CFLAGS:-} $(CFLAGS) $(THREAD_DEFS) \
		$(THREAD_CFLAGS) -o $@ lz9ans.c \
		$${EXTRA_LDFLAGS:-} $(LDFLAGS) $(THREAD_LDFLAGS) -lm || :
	@eval $${CC:-$(CC)} $${EXTRA_CFLAGS:-} $(CFLAGS) $(THREAD_DEFS) \
		$(THREAD_CFLAGS) -o $@ lz9ans.c \
		$${EXTRA_LDFLAGS:-} $(LDFLAGS) $(THREAD_LDFLAGS) -lm

##############################################################################

test: lz9ans
	./tests/test_lz9ans.py --fuzz --independent
	./tests/test_stdio.py
	./tests/test_large_files.py
	./tests/test_levels.py
	./tests/test_vectors.py
	./tests/test_window.py
	./tests/test_verbose.py
	./tests/test_features.py
	./tests/test_phased.py
	./tests/test_v9_format.py
	./tests/test_early_stored.py
	./tests/test_auto_block_threads.py
	./tests/test_crc_batch.py
	env CC="$${CC:-$(CC)}" CFLAGS="$${EXTRA_CFLAGS:-} $(CFLAGS)" \
		LDFLAGS="$${EXTRA_LDFLAGS:-} $(LDFLAGS)" \
		./tests/test_crc_accel.py

##############################################################################

test-auto-block-threads:
	env CC="$${CC:-$(CC)}" CFLAGS="$${EXTRA_CFLAGS:-} $(CFLAGS)" \
		LDFLAGS="$${EXTRA_LDFLAGS:-} $(LDFLAGS)" \
		THREAD_CFLAGS="$${THREAD_CFLAGS:--pthread}" \
		THREAD_LDFLAGS="$${THREAD_LDFLAGS:--pthread}" \
		./tests/test_auto_block_threads.py

##############################################################################

test-threads:
	env CC="$${CC:-$(CC)}" CFLAGS="$${EXTRA_CFLAGS:-} $(CFLAGS)" \
		LDFLAGS="$${EXTRA_LDFLAGS:-} $(LDFLAGS)" \
		THREAD_CFLAGS="$${THREAD_CFLAGS:--pthread}" \
		THREAD_LDFLAGS="$${THREAD_LDFLAGS:--pthread}" \
		./tests/test_threads.py

##############################################################################

thread-sanitizer:
	env CC="$${CC:-$(CC)}" CFLAGS="-D_ALL_SOURCE -D_GNU_SOURCE -O1 -g \
		-ansi -pedantic -Wall -Wextra -Werror" \
		LDFLAGS="-fsanitize=thread" TSAN_OPTIONS="halt_on_error=1" \
		./tests/test_thread_sanitizer.py

##############################################################################

sanitizers: lz9ans.c crc32_hw.h
	eval $${CC:-$(CC)} -Og -g -ansi -pedantic -Wall -Wextra -Werror \
		-Wno-overlength-strings -fsanitize=address,undefined \
		-fno-omit-frame-pointer -D_ALL_SOURCE -D_GNU_SOURCE \
		$(THREAD_DEFS) $(THREAD_CFLAGS) -o lz9ans_asan lz9ans.c \
		$${EXTRA_LDFLAGS:-} $(LDFLAGS) $(THREAD_LDFLAGS) -lm

##############################################################################

cppcheck: lz9ans.c crc32_hw.h
	cppcheck \
		--enable=warning,style,performance,portability,unusedFunction \
		--force --check-level=exhaustive --std=c89 --inline-suppr \
		--inconclusive --error-exitcode=99 -D__CPPCHECK__ -D__LINT__ \
		-D_ALL_SOURCE -D_GNU_SOURCE -j 1 --platform=unix64 lz9ans.c

##############################################################################

oracle: lz9ans.c crc32_hw.h
	/opt/oracle/developerstudio12.6/bin/lint -O -D__ORACLE_LINT -fd \
		-std=c89 -XCC=no -errchk=structarg,parentheses,locfmtchk \
		-errtags -D_ALL_SOURCE -D_GNU_SOURCE lz9ans.c
	/opt/oracle/developerstudio12.6/bin/lint -O -D__ORACLE_LINT -fd \
		-std=c89 -XCC=no -errchk=structarg,parentheses,locfmtchk \
		-errtags -D_ALL_SOURCE -D_GNU_SOURCE -DLZ9ANS_PTHREADS lz9ans.c

##############################################################################

lint: lz9ans.c crc32_hw.h
	$(MAKE) clean
	eval $${MAKE:-$(MAKE)} sanitizers CC="$${CLANG:-clang}"
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) cppcheck
	$(MAKE) oracle
	eval $${CLANG:-clang} -Weverything -Wno-unsafe-buffer-usage \
		-Wno-missing-noreturn -Wno-padded -Wno-overlength-strings \
		-Werror -D_ALL_SOURCE -D_GNU_SOURCE lz9ans.c \
		$${EXTRA_LDFLAGS:-} $(LDFLAGS) -lm
	$(MAKE) pvs-studio
	$(MAKE) clean

##############################################################################

pvs pvs-studio:
	$(MAKE) clean
	scan-build bear -- $(MAKE) lz9ans
	pvs-studio-analyzer analyze --intermodular -j $$(nproc) -o log.pvs
	plog-converter -a "GA:1,2,3" -t fullhtml log.pvs -o pvsreport -w
	$(MAKE) clean
	scan-build bear -- $(MAKE) lz9ans THREADS=pthreads
	pvs-studio-analyzer analyze --intermodular -j $$(nproc) -o log.pvs
	plog-converter -a "GA:1,2,3" -t fullhtml log.pvs -o pvsreport -w

##############################################################################

clean:
	rm -f lz9ans lz9ans.exe lz9ans-pthread lz9ans_asan lz9ans_asan.exe \
		a.out a.exe || :
	rm -f FORCE log.pvs compile_commands.json .build-config || :
	rm -f -r tests/__pycache__ pvsreport > /dev/null 2>&1 || :

##############################################################################

distclean: clean
	rm -f lz9ans.gcda .build-config
	@git clean -ndx 2> /dev/null || :

##############################################################################

.PHONY: all test test-threads thread-sanitizer sanitizers cppcheck oracle \
	lint pvs pvs-studio clean distclean FORCE test-auto-block-threads

##############################################################################
# Local Variables:
# mode: makefile
# indent-tabs-mode: t
# tab-width: 4
# whitespace-style: (tabs tab-mark)
# whitespace-display-mappings: ((tab-mark 9 [45] [45]))
# fill-column: 78
# eval: (setq-local whitespace-display-mappings
#                   '((tab-mark 9
#                               [45 45 62]
#                               [45 45 62])))
# eval: (whitespace-mode 1)
# eval: (setq-local display-fill-column-indicator-column 78)
# eval: (display-fill-column-indicator-mode 1)
# End:
##############################################################################
# vim: set ft=make ts=4 ai noexpandtab list listchars=tab\:\>\- cc=78 :
##############################################################################
