#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT
# scspell-id: 8d8ece78-dfa1-11f1-80ad-80ee73e9b8e7

"""Run focused pthread encoder/decoder paths under ThreadSanitizer."""

import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

from inspect_lz9ans import pack9

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "lz9ans.c"


def run(command, *, data=None):
    result = subprocess.run(
        command,
        input=data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        env=os.environ.copy(),
    )
    if result.returncode:
        raise AssertionError(
            f"command failed ({result.returncode}): {command!r}\n"
            f"stdout={result.stdout!r}\n"
            f"stderr={result.stderr.decode(errors='replace')}"
        )
    return result


def main():
    with tempfile.TemporaryDirectory(prefix="lz9ans-tsan-") as tmp:
        work = Path(tmp)
        binary = work / "lz9ans-tsan"
        cc = shlex.split(os.environ.get("CC", "cc"))
        cflags = shlex.split(
            os.environ.get(
                "CFLAGS",
                "-D_ALL_SOURCE -D_GNU_SOURCE -O1 -g -ansi -pedantic "
                "-Wall -Wextra -Werror",
            )
        )
        ldflags = shlex.split(os.environ.get("LDFLAGS", ""))
        thread_cflags = shlex.split(os.environ.get("THREAD_CFLAGS", "-pthread"))
        thread_ldflags = shlex.split(os.environ.get("THREAD_LDFLAGS", "-pthread"))
        command = (
            cc
            + cflags
            + ["-DLZ9ANS_PTHREADS", "-fsanitize=thread", "-fno-omit-frame-pointer"]
            + thread_cflags
            + ["-o", str(binary), str(SOURCE)]
            + ldflags
            + thread_ldflags
            + ["-lm"]
        )
        run(command)

        cases = (
            ("run8", "c8", 6, ()),
            ("mixed9", "c9", 9, ()),
            ("phased9", "c9", 9, ()),
            ("longdist32k", "c8", 9, ("--window=64k",)),
        )
        for name, mode, level, options in cases:
            source = ROOT / "vectors" / f"{name}.input"
            compressed = work / f"{name}.lz9a"
            decoded = work / f"{name}.decoded"
            run(
                [
                    str(binary),
                    mode,
                    str(source),
                    str(compressed),
                    f"-{level}",
                    "--threads=0",
                ]
                + list(options)
            )
            run([str(binary), "d", str(compressed), str(decoded), "--threads=0"])
            assert decoded.read_bytes() == source.read_bytes(), name
            print(f"PASS ThreadSanitizer: {name}")

        rng = random.Random(0x9A115)
        repeated = bytes(range(256)) * 128
        incompressible = bytes(rng.randrange(256) for _ in range(32768))
        multi = work / "multi-block.input"
        multi.write_bytes(repeated + repeated + incompressible + repeated)
        compressed = work / "multi-block.lz9a"
        decoded = work / "multi-block.decoded"
        run([str(binary), "c8", str(multi), str(compressed), "-9", "--threads=0"])
        run([str(binary), "d", str(compressed), str(decoded), "--threads=0"])
        assert decoded.read_bytes() == multi.read_bytes(), "multi-block"
        print("PASS ThreadSanitizer: multi-block cache/fallback")

        block_parallel = work / "block-parallel.lz9a"
        run([str(binary), "c8", str(multi), str(block_parallel), "-9", "--threads=3"])
        decoded = work / "block-parallel.decoded"
        run([str(binary), "d", str(block_parallel), str(decoded), "--threads=2"])
        assert decoded.read_bytes() == multi.read_bytes(), "parallel blocks"
        print("PASS ThreadSanitizer: independent block encoding")

        # Exercise continuous worker refill and ordered result retirement over
        # substantially more blocks than there are participants.
        pipeline_blocks = [
            repeated,
            incompressible,
            bytes((i * 17 + 3) & 0xFF for i in range(32768)),
            bytes(rng.randrange(256) for _ in range(32768)),
            repeated,
            bytes(rng.randrange(256) for _ in range(32768)),
            repeated,
            incompressible[::-1],
            repeated,
        ]
        pipeline_raw = b"".join(pipeline_blocks) + b"pipeline-tail"
        pipeline_source = work / "pipeline-many-blocks.input"
        pipeline_source.write_bytes(pipeline_raw)
        pipeline_archive = work / "pipeline-many-blocks.lz9a"
        run(
            [
                str(binary),
                "c8",
                str(pipeline_source),
                str(pipeline_archive),
                "-6",
                "--threads=3",
            ]
        )
        pipeline_decoded = work / "pipeline-many-blocks.decoded"
        run([str(binary), "d", str(pipeline_archive), str(pipeline_decoded)])
        assert pipeline_decoded.read_bytes() == pipeline_raw, "pipeline c8"
        print("PASS ThreadSanitizer: continuous multi-block c8 pipeline")

        pipeline_symbols = []
        for block in range(9):
            if block in (0, 4, 8):
                pipeline_symbols.extend([block + 1] * 32768)
            else:
                pipeline_symbols.extend(rng.randrange(512) for _ in range(32768))
        pipeline_9_raw = pack9(pipeline_symbols + [17, 511, 0])
        pipeline_9_source = work / "pipeline-many-blocks-9.input"
        pipeline_9_source.write_bytes(pipeline_9_raw)
        pipeline_9_archive = work / "pipeline-many-blocks-9.lz9a"
        run(
            [
                str(binary),
                "c9",
                str(pipeline_9_source),
                str(pipeline_9_archive),
                "-6",
                "--window=64k",
                "--threads=3",
            ]
        )
        pipeline_9_decoded = work / "pipeline-many-blocks-9.decoded"
        run([str(binary), "d", str(pipeline_9_archive), str(pipeline_9_decoded)])
        assert pipeline_9_decoded.read_bytes() == pipeline_9_raw, "pipeline c9"
        print("PASS ThreadSanitizer: continuous multi-block c9 pipeline")

        phased = ROOT / "vectors" / "phased9.input"
        compressed = work / "phased9-block-parallel.lz9a"
        run([str(binary), "c9", str(phased), str(compressed), "-9", "--threads=3"])
        decoded = work / "phased9-block-parallel.decoded"
        run([str(binary), "d", str(compressed), str(decoded), "--threads=2"])
        assert decoded.read_bytes() == phased.read_bytes(), "phased9 blocks"
        print("PASS ThreadSanitizer: phased block encoding")


if __name__ == "__main__":
    main()
