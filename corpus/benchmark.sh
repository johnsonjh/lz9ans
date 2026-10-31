#!/bin/sh
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

set -eu

command -v ts > /dev/null 2>&1 || {
  printf '%s\n' "ERROR: ts (moreutils) not found."
  exit 1
}

# shellcheck disable=SC2065
test -x ../lz9ans > /dev/null 2>&1 || {
  printf '%s\n' "ERROR: ../lz9ans executable not found."
  exit 1
}

test -d ./obj > /dev/null 2>&1 || {
  printf '%s\n' "ERROR: ./obj directory not found; extract 9bit_corpus.tar.lz."
  exit 1
}

dobench()
{
  (
    cd obj
    printf '%s\n' "Benchmarking $1 ..."
    touch out.out

    for n in $(seq 1 9); do
      rm -f ./*.out > /dev/null 2>&1

      for i in ./*.archive; do
        # shellcheck disable=SC2086
        ../../lz9ans c9 "$i" "$i".out -"$n" $1 > /dev/null 2>&1
      done

      printf '-'
      printf '%s: ' "$n"
      wc -c ./*.out | grep 'total$'
    done | env ts -i -m '%.T'

    rm -f ./*.out > /dev/null 2>&1
    printf '%s\n' ""
  )
}

dobench "--window=32k"
dobench "--window=32k --extra-ans"
dobench "--window=64k"
dobench "--window=62k --extra-ans"
