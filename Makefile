CC = cc
CFLAGS = -O3 -ansi -pedantic -flto=auto -Wall -Wextra -Werror

all: lz9ans

lz9ans: lz9ans.c
	$(CC) $(CFLAGS) -o $@ lz9ans.c $(LDFLAGS) -lm

test: lz9ans
	./test_lz9ans.py --fuzz --independent
	./test_levels.py
	./test_vectors.py
	./test_window.py
	./test_verbose.py
	./test_experimental.py

sanitizers: lz9ans.c
	$(CC) -Og -g -ansi -pedantic -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o lz9ans_asan lz9ans.c -lm

cppcheck: lz9ans.c
	cppcheck \
		--enable=warning,style,performance,portability,unusedFunction \
		--force --check-level=exhaustive --std=c89 --inline-suppr \
		--inconclusive --error-exitcode=99 -D__CPPCHECK__ -D__LINT__ \
		-j 1 --platform=unix64 lz9ans.c

lint: lz9ans.c
	$(MAKE) clean
	$(MAKE) sanitizers CC=clang
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) cppcheck
	scan-build bear -- $(MAKE) all
	rm -f -r pvsreport > /dev/null 2>&1 || :
	pvs-studio-analyzer analyze --intermodular -j 12 -o log.pvs
	plog-converter -a "GA:1,2,3" -t fullhtml log.pvs -o pvsreport
	$(MAKE) clean

clean:
	rm -f lz9ans lz9ans_asan || :
	rm -f -r __pycache__ || :
	rm -f -r pvsreport log.pvs compile_commands.json || :

.PHONY: all test sanitizers cppcheck lint clean
