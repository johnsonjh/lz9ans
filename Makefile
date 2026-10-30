# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT
CC = cc
CFLAGS = -O3 -ansi -pedantic -flto=auto -Wall -Wextra -Werror

all: lz9ans

lz9ans: lz9ans.c
	@eval echo \
		$${CC:-$(CC)} $${EXTRA_CFLAGS:-} $(CFLAGS) -o $@ lz9ans.c \
		$${EXTRA_LDFLAGS:-} $(LDFLAGS) -lm || :
	@eval $${CC:-$(CC)} $${EXTRA_CFLAGS:-} $(CFLAGS) -o $@ lz9ans.c \
		$${EXTRA_LDFLAGS:-} $(LDFLAGS) -lm

test: lz9ans
	./test_lz9ans.py --fuzz --independent
	./test_levels.py
	./test_vectors.py
	./test_window.py
	./test_verbose.py
	./test_features.py

sanitizers: lz9ans.c
	eval $${CC:-$(CC)} -Og -g -ansi -pedantic -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o lz9ans_asan lz9ans.c $${EXTRA_LDFLAGS:-} $(LDFLAGS) -lm

cppcheck: lz9ans.c
	cppcheck \
		--enable=warning,style,performance,portability,unusedFunction \
		--force --check-level=exhaustive --std=c89 --inline-suppr \
		--inconclusive --error-exitcode=99 -D__CPPCHECK__ -D__LINT__ \
		-j 1 --platform=unix64 lz9ans.c

oracle: lz9ans.c
	/opt/oracle/developerstudio12.6/bin/lint -O -D__ORACLE_LINT__ -fd \
		-std=c89 -XCC=no -errchk=structarg,parentheses,locfmtchk \
		-errtags lz9ans.c

lint: lz9ans.c
	$(MAKE) clean
	eval $(MAKE) sanitizers CC="$${CLANG:-clang}"
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) cppcheck
	$(MAKE) oracle
	eval $${CLANG:-clang} -Weverything -Wno-unsafe-buffer-usage \
		-Wno-missing-noreturn -Wno-padded -Werror lz9ans.c \
		$${EXTRA_LDFLAGS:-} $(LDFLAGS) -lm
	$(MAKE) clean
	scan-build bear -- $(MAKE) all
	pvs-studio-analyzer analyze --intermodular -j 12 -o log.pvs
	plog-converter -a "GA:1,2,3" -t fullhtml log.pvs -o pvsreport -w
	$(MAKE) clean

clean distclean:
	rm -f lz9ans lz9ans.exe lz9ans_asan lz9ans_asan.exe a.out a.exe || :
	rm -f log.pvs compile_commands.json || :
	rm -f -r __pycache__ pvsreport > /dev/null 2>&1 || :
	@git clean -ndx 2> /dev/null || :

.PHONY: all test sanitizers cppcheck oracle lint clean distclean
