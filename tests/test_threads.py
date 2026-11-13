#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "lz9ans.c"


def run(command, *, data=None, expected=0):
    result = subprocess.run(
        command,
        input=data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if expected is None:
        return result
    if expected == 0 and result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {command!r}\n"
            f"stdout={result.stdout!r}\nstderr={result.stderr.decode(errors='replace')}"
        )
    if expected != 0 and result.returncode == 0:
        raise AssertionError(f"command unexpectedly succeeded: {command!r}")
    return result


def build(binary, threaded):
    cc = shlex.split(os.environ.get("CC", "cc"))
    cflags = shlex.split(
        os.environ.get(
            "CFLAGS",
            "-D_ALL_SOURCE -D_GNU_SOURCE -O2 -ansi -pedantic " "-Wall -Wextra -Werror",
        )
    )
    ldflags = shlex.split(os.environ.get("LDFLAGS", ""))
    thread_cflags = shlex.split(os.environ.get("THREAD_CFLAGS", "-pthread"))
    thread_ldflags = shlex.split(os.environ.get("THREAD_LDFLAGS", "-pthread"))
    command = cc + cflags
    if threaded:
        command += ["-DLZ9ANS_PTHREADS"] + thread_cflags
    command += ["-o", str(binary), str(SOURCE)] + ldflags
    if threaded:
        command += thread_ldflags
    command.append("-lm")
    run(command)


def check_input(serial, threaded, work, source, mode, name, levels, windows):
    for window in windows:
        for level in levels:
            common = [f"-{level}"]
            if window == "64k":
                common.append("--window=64k")
            serial_path = work / f"{name}-{window}-{level}-serial.lz9a"
            thread_path = work / f"{name}-{window}-{level}-thread.lz9a"
            run([str(serial), mode, str(source), str(serial_path)] + common)
            run(
                [
                    str(threaded),
                    mode,
                    str(source),
                    str(thread_path),
                ]
                + common
                + ["--threads=1"]
            )
            expected = serial_path.read_bytes()
            actual = thread_path.read_bytes()
            assert actual == expected, (name, window, level, "compressed bytes")

            decoded = work / f"{name}-{window}-{level}.decoded"
            run([str(threaded), "d", str(thread_path), str(decoded), "--threads=0"])
            assert decoded.read_bytes() == source.read_bytes(), (
                name,
                window,
                level,
                "threaded decode",
            )


def check_stdio(serial, threaded, work, source):
    raw = source.read_bytes()
    serial_encoded = run([str(serial), "c8", "-", "-", "-6"], data=raw).stdout
    thread_encoded = run(
        [str(threaded), "c8", "-", "-", "-6", "--threads=1"], data=raw
    ).stdout
    assert serial_encoded == thread_encoded, "stdin/stdout encoding mismatch"
    serial_decoded = run([str(serial), "d", "-", "-"], data=thread_encoded).stdout
    thread_decoded = run(
        [str(threaded), "d", "-", "-", "--threads=2"], data=thread_encoded
    ).stdout
    assert (
        serial_decoded == raw and thread_decoded == raw
    ), "stdin/stdout decode mismatch"


def check_options(serial, threaded):
    invalid = (
        "--threads=",
        "--threads=-1",
        "--threads=abc",
        "--threads=2x",
        "--threads=4294967296",
        "--threads=+1",
    )
    with tempfile.TemporaryDirectory(prefix="lz9ans-thread-options-") as tmp:
        work = Path(tmp)
        source = work / "in"
        source.write_bytes(b"parallel option validation\n")
        target = work / "out"
        for binary in (serial, threaded):
            for option in invalid:
                run(
                    [str(binary), "c8", str(source), str(target), option],
                    expected=2,
                )
            run(
                [
                    str(binary),
                    "c8",
                    str(source),
                    str(target),
                    "--threads=1",
                    "--threads=2",
                ],
                expected=2,
            )
            run([str(binary), "--memory", "--threads=999"])

        serial_ignored = work / "serial-ignored-threads.lz9a"
        serial_default = work / "serial-default-threads.lz9a"
        run([str(serial), "c8", str(source), str(serial_default), "-6"])
        run(
            [str(serial), "c8", str(source), str(serial_ignored), "-6", "--threads=999"]
        )
        assert serial_ignored.read_bytes() == serial_default.read_bytes()
        run([str(serial), "--memory", "--threads=1", "--threads=2"], expected=2)

        serial_help = run([str(serial)], expected=2).stderr
        threaded_help = run([str(threaded)], expected=2).stderr
        assert b"--threads" not in serial_help
        assert b"--threads=N" in threaded_help
        serial_memory = run([str(serial), "--memory"]).stdout
        threaded_memory = run([str(threaded), "--memory"]).stdout
        assert b"pthread build" not in serial_memory
        assert b"pthread build" in threaded_memory
        assert b"encoder maximum allocated state" in serial_memory
        assert b"encoder allocated state at levels 1-5" in serial_memory
        assert b"decoder base allocated state" in serial_memory
        assert b"decoder maximum allocated state" in serial_memory
        assert b"decompression symbol buffers: 49152 bytes maximum" in threaded_memory
        assert b"32766 bytes at levels 1-5" in threaded_memory
        one = run([str(threaded), "--memory", "--threads=1"]).stdout
        two = run([str(threaded), "--memory", "--threads=2"]).stdout
        many = run([str(threaded), "--memory-64k", "--threads=999"]).stdout
        assert b"compression participants: 1" in one
        assert b"compression mode: sequential" in one
        assert b"parallel-block encoder state" not in one
        encoder_memory = (
            b"encoder maximum allocated state:",
            b"encoder allocated state at levels 1-5:",
        )
        assert all(
            next(line for line in two.splitlines() if line.startswith(prefix))
            in one.splitlines()
            for prefix in encoder_memory
        ), "--threads=1 must retain the base encoder allocation sizes"
        assert b"compression participants: 2" in two
        assert b"compression mode: " in two
        assert b"compression participants: " in many
        assert b"window: 65536 symbols" in many


def main():
    with tempfile.TemporaryDirectory(prefix="lz9ans-threads-") as tmp:
        work = Path(tmp)
        serial = work / "lz9ans-serial"
        threaded = work / "lz9ans-pthread"
        build(serial, False)
        build(threaded, True)

        vectors = (
            ("abc8", "c8"),
            ("empty8", "c8"),
            ("longdist16k", "c8"),
            ("longdist32k", "c8"),
            ("mixed9", "c9"),
            ("phased9", "c9"),
            ("run8", "c8"),
            ("switch8", "c8"),
        )
        for name, mode in vectors:
            source = ROOT / "vectors" / f"{name}.input"
            check_input(
                serial,
                threaded,
                work,
                source,
                mode,
                name,
                range(1, 10),
                ("32k", "64k"),
            )
            print(f"PASS serial/threaded equivalence: {name}")

            if name == "abc8":
                limited = work / "abc8-limit-1.lz9a"
                run(
                    [
                        str(threaded),
                        mode,
                        str(source),
                        str(limited),
                        "-9",
                        "--threads=1",
                    ]
                )
                expected = work / "abc8-32k-9-serial.lz9a"
                assert (
                    limited.read_bytes() == expected.read_bytes()
                ), "--threads=1 must select the original encoder"

        # Exercise multiple blocks, model-cache commits, STORED fallback, and
        # later return to LZ coding after a stored block.
        rng = random.Random(0x9A115)
        repeated = bytes(range(256)) * 128
        random_block = bytes(rng.randrange(256) for _ in range(32768))
        multi = work / "multi-block.input"
        multi.write_bytes(repeated + repeated + random_block + repeated)
        check_input(
            serial,
            threaded,
            work,
            multi,
            "c8",
            "multi-block",
            (4, 6, 9),
            ("32k", "64k"),
        )
        print("PASS multi-block cache/fallback equivalence")

        check_stdio(serial, threaded, work, ROOT / "vectors" / "switch8.input")
        print("PASS threaded stdin/stdout")
        check_options(serial, threaded)
        print("PASS --threads parsing, help, and memory reporting")


if __name__ == "__main__":
    main()
