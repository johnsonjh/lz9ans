# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT
CC = cc
CFLAGS = -D_ALL_SOURCE -D_GNU_SOURCE -O3 -ansi -pedantic -flto=auto \
	 -Wall -Wextra -Werror

all: lz9ans

lz9ans: lz9ans.c crc32_hw.h
	@eval echo \
		$${CC:-$(CC)} $${EXTRA_CFLAGS:-} $(CFLAGS) -o $@ lz9ans.c \
		$${EXTRA_LDFLAGS:-} $(LDFLAGS) -lm || :
	@eval $${CC:-$(CC)} $${EXTRA_CFLAGS:-} $(CFLAGS) -o $@ lz9ans.c \
		$${EXTRA_LDFLAGS:-} $(LDFLAGS) -lm

test: lz9ans
	./tests/test_lz9ans.py --fuzz --independent
	./tests/test_levels.py
	./tests/test_vectors.py
	./tests/test_window.py
	./tests/test_verbose.py
	./tests/test_features.py
	./tests/test_v8_format.py
	./tests/test_early_stored.py
	./tests/test_crc_batch.py
	env CC="$${CC:-$(CC)}" CFLAGS="$${EXTRA_CFLAGS:-} $(CFLAGS)" \
		LDFLAGS="$${EXTRA_LDFLAGS:-} $(LDFLAGS)" \
		./tests/test_crc_accel.py

sanitizers: lz9ans.c crc32_hw.h
	eval $${CC:-$(CC)} -Og -g -ansi -pedantic -Wall -Wextra -Werror \
		-Wno-overlength-strings -fsanitize=address,undefined \
		-fno-omit-frame-pointer -D_ALL_SOURCE -D_GNU_SOURCE \
		-o lz9ans_asan lz9ans.c $${EXTRA_LDFLAGS:-} $(LDFLAGS) -lm

cppcheck: lz9ans.c crc32_hw.h
	cppcheck \
		--enable=warning,style,performance,portability,unusedFunction \
		--force --check-level=exhaustive --std=c89 --inline-suppr \
		--inconclusive --error-exitcode=99 -D__CPPCHECK__ -D__LINT__ \
		-D_ALL_SOURCE -D_GNU_SOURCE -j 1 --platform=unix64 lz9ans.c

oracle: lz9ans.c crc32_hw.h
	/opt/oracle/developerstudio12.6/bin/lint -O -D__ORACLE_LINT__ -fd \
		-std=c89 -XCC=no -errchk=structarg,parentheses,locfmtchk \
		-errtags -D_ALL_SOURCE -D_GNU_SOURCE lz9ans.c

lint: lz9ans.c crc32_hw.h
	$(MAKE) clean
	eval $(MAKE) sanitizers CC="$${CLANG:-clang}"
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

pvs pvs-studio:
	$(MAKE) clean
	scan-build bear -- $(MAKE) all
	pvs-studio-analyzer analyze --intermodular -j 12 -o log.pvs
	plog-converter -a "GA:1,2,3" -t fullhtml log.pvs -o pvsreport -w


clean:
	rm -f lz9ans lz9ans.exe lz9ans_asan lz9ans_asan.exe a.out a.exe || :
	rm -f log.pvs compile_commands.json || :
	rm -f -r __pycache__ tests/__pycache__ pvsreport > /dev/null 2>&1 || :

distclean: clean
	rm -f lz9ans.gcda
	@git clean -ndx 2> /dev/null || :

.PHONY: all test sanitizers cppcheck oracle lint pvs pvs-studio clean distclean
