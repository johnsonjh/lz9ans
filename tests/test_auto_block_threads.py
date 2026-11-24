#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT
# scspell-id: 8a19e7a0-dfa1-11f1-b65c-80ee73e9b8e7

import os
from pathlib import Path
import random
import re
import shlex
import struct
import subprocess
import tempfile

from inspect_lz9ans import decode as independent_decode, pack9

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
    if expected == 0 and result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {command!r}\n"
            f"stdout={result.stdout!r}\n"
            f"stderr={result.stderr.decode(errors='replace')}"
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
        command += ["-DLZ9ANS_PTHREAD"] + thread_cflags
    command += ["-o", str(binary), str(SOURCE)] + ldflags
    if threaded:
        command += thread_ldflags
    run(command)


def make_inputs(work):
    rng = random.Random(0x9A6B10C)
    # Four blocks A, A, B, A exercise both history limits: block 2 can refer
    # 32K backward to block 1, and block 4 can refer 64K backward to block 2.
    a = bytes(rng.randrange(256) for _ in range(32768))
    b = bytes(rng.randrange(256) for _ in range(32768))
    c8 = a + a + b + a
    p8 = work / "history-8.bin"
    p8.write_bytes(c8)

    a9 = [rng.randrange(512) for _ in range(32768)]
    b9 = [rng.randrange(512) for _ in range(32768)]
    c9 = pack9(a9 + a9 + b9 + a9)
    p9 = work / "history-9.packed"
    p9.write_bytes(c9)
    return (("c8", p8), ("c9", p9))


def check_roundtrips(serial, threaded, work, inputs):
    levels = tuple(range(1, 10))
    windows = ("32k", "64k")
    parallel_limits = (0, 2, 3, 4, 999)

    for mode, source in inputs:
        raw = source.read_bytes()
        for window in windows:
            for level in levels:
                args = [f"-{level}", f"--window={window}"]
                serial_archive = work / f"{mode}-{window}-{level}-serial.lz9a"
                run([str(serial), mode, str(source), str(serial_archive)] + args)
                reference = serial_archive.read_bytes()

                # A pthread build with one participant must retain the original
                # sequential encoder, byte for byte.
                one_archive = work / f"{mode}-{window}-{level}-t1.lz9a"
                run(
                    [str(threaded), mode, str(source), str(one_archive)]
                    + args
                    + ["--threads=1"]
                )
                assert one_archive.read_bytes() == reference, (
                    mode,
                    window,
                    level,
                    "threads=1 must use serial encoding",
                )

                decoded = work / f"{mode}-{window}-{level}-serial.out"
                run([str(serial), "d", str(serial_archive), str(decoded)])
                assert decoded.read_bytes() == raw, (
                    mode,
                    window,
                    level,
                    "serial decode",
                )

                independent_output, statistics = independent_decode(reference)
                assert independent_output == raw, (
                    mode,
                    window,
                    level,
                    "independent Python decoder",
                )
                if mode == "c8" and window == "32k":
                    assert (
                        statistics.get("offset_19", 0) > 0
                    ), "32K history was not used by the 32K test vector"
                if mode == "c8" and window == "64k":
                    assert (
                        statistics.get("offset_20", 0) > 0
                    ), "64K history was not used by the 64K test vector"
                    assert (
                        statistics.get("stored", 0) > 0
                    ), "the 64K history vector must cross a STORED block"

                # Exercise automatic and several explicit limits on selected
                # cases.  Every limit above one must select the same block
                # algorithm and produce deterministic bytes.
                if mode == "c8" and (
                    (window == "32k" and level in (1, 4, 6, 9))
                    or (window == "64k" and level == 9)
                ):
                    block_reference = None
                    for limit in parallel_limits:
                        archive = work / (f"{mode}-{window}-{level}-t{limit}.lz9a")
                        run(
                            [str(threaded), mode, str(source), str(archive)]
                            + args
                            + [f"--threads={limit}"]
                        )
                        actual = archive.read_bytes()
                        if block_reference is None:
                            block_reference = actual
                        else:
                            assert actual == block_reference, (
                                mode,
                                window,
                                level,
                                limit,
                                "block-mode output differs by participant count",
                            )

                    # Strategy selection is asserted independently through
                    # --memory in check_options(); some content can legitimately
                    # produce identical bytes under both encoder strategies.

                    decoded = work / f"{mode}-{window}-{level}-auto.out"
                    run(
                        [
                            str(threaded),
                            "d",
                            str(work / f"{mode}-{window}-{level}-t0.lz9a"),
                            str(decoded),
                        ]
                    )
                    assert decoded.read_bytes() == raw, (
                        mode,
                        window,
                        level,
                        "automatic block-mode decode",
                    )

    # This fixture exercises all three phased alphabets and two-symbol matches.
    phased_source = ROOT / "vectors" / "phased9.input"
    phased_raw = phased_source.read_bytes()
    reference = None
    for limit in (0, 3):
        archive = work / f"phased9-auto-t{limit}.lz9a"
        run(
            [
                str(threaded),
                "c9",
                str(phased_source),
                str(archive),
                "-9",
                f"--threads={limit}",
            ]
        )
        actual = archive.read_bytes()
        if reference is None:
            reference = actual
        else:
            assert actual == reference, "phased9 output differs by participant count"
        independent_output, statistics = independent_decode(actual)
        assert independent_output == phased_raw, "phased9 independent decoder"
        assert all(
            statistics.get(f"phased_kind{k}", 0) > 0 for k in (0, 1, 2)
        ), statistics
        assert statistics.get("match_2", 0) > 0, statistics
        decoded = work / f"phased9-auto-t{limit}.out"
        run([str(serial), "d", str(archive), str(decoded)])
        assert decoded.read_bytes() == phased_raw, "phased9 block-mode C decode"


def check_streaming_pipeline(serial, threaded, work):
    """Exercise continuously scheduled work across many more blocks than workers."""
    rng = random.Random(0xB10C5EED)
    block_n = 32768
    c8_blocks = (
        b"A" * block_n,
        bytes(rng.randrange(256) for _ in range(block_n)),
        (bytes(range(251)) * ((block_n + 250) // 251))[:block_n],
        bytes(rng.randrange(256) for _ in range(block_n)),
        b"B" * block_n,
        bytes(rng.randrange(256) for _ in range(block_n)),
        (b"parallel-history" * ((block_n + 15) // 16))[:block_n],
        bytes(rng.randrange(256) for _ in range(block_n)),
        b"C" * block_n,
    )
    c8_raw = b"".join(c8_blocks) + b"tail-8"
    c8_source = work / "pipeline-many-blocks-8.bin"
    c8_source.write_bytes(c8_raw)

    c9_blocks = []
    for index in range(9):
        if index in (0, 4, 8):
            symbols = [index + 1] * block_n
        elif index in (2, 6):
            symbols = [(value * 13 + index) & 511 for value in range(block_n)]
        else:
            symbols = [rng.randrange(512) for _ in range(block_n)]
        c9_blocks.extend(symbols)
    c9_symbols = c9_blocks + [17, 511, 0, 3, 255]
    c9_raw = pack9(c9_symbols)
    c9_source = work / "pipeline-many-blocks-9.packed"
    c9_source.write_bytes(c9_raw)

    for mode, source, raw in (("c8", c8_source, c8_raw), ("c9", c9_source, c9_raw)):
        for window in ("32k", "64k"):
            for level in (1, 6, 9):
                reference = None
                for limit in (2, 3, 999):
                    archive = work / (
                        f"pipeline-{mode}-{window}-L{level}-t{limit}.lz9a"
                    )
                    run(
                        [
                            str(threaded),
                            mode,
                            str(source),
                            str(archive),
                            f"-{level}",
                            f"--window={window}",
                            f"--threads={limit}",
                        ]
                    )
                    actual = archive.read_bytes()
                    if reference is None:
                        reference = actual
                    else:
                        assert actual == reference, (
                            mode,
                            window,
                            level,
                            limit,
                            "continuous pipeline output differs by participant count",
                        )

                decoded = work / f"pipeline-{mode}-{window}-L{level}.out"
                run([str(serial), "d", str(archive), str(decoded)])
                assert decoded.read_bytes() == raw, (
                    mode,
                    window,
                    level,
                    "continuous pipeline round trip",
                )

        # Unknown-length stdin/stdout must use the same rolling history and
        # ordered retirement path, not a different block scheduler.
        encoded = run(
            [str(threaded), mode, "-", "-", "-6", "--threads=3"],
            data=raw,
        ).stdout
        decoded = run([str(serial), "d", "-", "-"], data=encoded).stdout
        assert decoded == raw, (mode, "continuous pipeline unknown-count stdio")


def check_stdio(threaded, work, inputs):
    for mode, source in inputs:
        raw = source.read_bytes()
        encoded = run(
            [str(threaded), mode, "-", "-", "-7", "--threads=3"],
            data=raw,
        ).stdout
        decoded = run([str(threaded), "d", "-", "-"], data=encoded).stdout
        assert decoded == raw, (mode, "unknown-count stdio round trip")

    rng = random.Random(0x9B17)
    for count in range(1, 9):
        raw = pack9([rng.randrange(512) for _ in range(count)])
        encoded = run(
            [str(threaded), "c9", "-", "-", "-5", "--threads=2"],
            data=raw,
        ).stdout
        decoded = run([str(threaded), "d", "-", "-"], data=encoded).stdout
        assert decoded == raw, ("c9", count, "streamed packed-9 tail alignment")

        source = work / f"packed-9-tail-{count}.bin"
        archive = work / f"packed-9-tail-{count}.lz9a"
        output = work / f"packed-9-tail-{count}.out"
        source.write_bytes(raw)
        run([str(threaded), "c9", str(source), str(archive), "-5", "--threads=2"])
        run([str(threaded), "d", str(archive), str(output)])
        assert output.read_bytes() == raw, ("c9", count, "file packed-9 tail alignment")

    bad_padding = bytearray(pack9([17]))
    bad_padding[-1] |= 1
    run(
        [str(threaded), "c9", "-", "-", "--threads=2"],
        data=bytes(bad_padding),
        expected=1,
    )
    run(
        [str(threaded), "c9", "-", "-", "--threads=2"],
        data=b"\xff",
        expected=1,
    )


def check_options(serial, threaded, cpu_count):
    with tempfile.TemporaryDirectory(prefix="lz9ans-auto-block-options-") as tmp:
        work = Path(tmp)
        source = work / "input"
        source.write_bytes(
            (bytes(range(256)) * 256) + (b"parallel option validation\n" * 10)
        )
        out1 = work / "serial1.lz9a"
        out2 = work / "serial2.lz9a"
        out3 = work / "thread1.lz9a"
        out4 = work / "threadauto.lz9a"
        out5 = work / "threaddefault.lz9a"

        # Explicit help is successful, writes to stdout, and advertises the
        # aliases supported by each build configuration.
        for binary, threaded_build in ((serial, False), (threaded, True)):
            for option in ("-h", "--help"):
                help_result = run([str(binary), option])
                assert help_result.stderr == b""
                assert b"Usage:" in help_result.stdout
                assert b"-v, --verbose" in help_result.stdout
                assert b"-w, --window=W" in help_result.stdout
                assert b"-h, --help" in help_result.stdout
                assert b"-L, --license" in help_result.stdout
                if threaded_build:
                    assert b"-T, --threads=N" in help_result.stdout
                else:
                    assert b"--threads" not in help_result.stdout
            help_after_operands = run(
                [str(binary), "c8", str(source), str(out2), "--help"]
            )
            assert help_after_operands.returncode == 0
            assert b"Usage:" in help_after_operands.stdout

        expected_license = (ROOT / "LICENSE").read_bytes()
        for binary in (serial, threaded):
            for option in ("-L", "--license"):
                license_result = run([str(binary), option])
                assert license_result.stdout == expected_license
                assert license_result.stderr == b""
            combined_license = run([str(binary), "-L", "--help"], expected=2)
            assert combined_license.stdout == b""
            combined_memory = run([str(binary), "--memory", "-L"], expected=2)
            assert combined_memory.stdout == b""
            combined_compress = run(
                [str(binary), "c8", str(source), str(out2), "--license"],
                expected=2,
            )
            assert combined_compress.stdout == b""
            assert not out2.exists()
            combined_decode = run(
                [str(binary), "d", str(source), str(out2), "-L"],
                expected=2,
            )
            assert combined_decode.stdout == b""

        # The short window spelling accepts both equals and separate values.
        window_reference = work / "window-reference.lz9a"
        window_equals = work / "window-equals.lz9a"
        window_separate = work / "window-separate.lz9a"
        window_32k_equals = work / "window-32k-equals.lz9a"
        window_32k_separate = work / "window-32k-separate.lz9a"
        run(
            [
                str(serial),
                "c8",
                str(source),
                str(window_reference),
                "-5",
                "--window=64k",
            ]
        )
        run([str(serial), "c8", str(source), str(window_equals), "-5", "-w=64k"])
        run(
            [
                str(serial),
                "c8",
                str(source),
                str(window_separate),
                "-5",
                "-w",
                "64k",
            ]
        )
        assert window_equals.read_bytes() == window_reference.read_bytes()
        assert window_separate.read_bytes() == window_reference.read_bytes()
        run(
            [
                str(serial),
                "c8",
                str(source),
                str(window_32k_equals),
                "-5",
                "-w=32k",
            ]
        )
        run(
            [
                str(serial),
                "c8",
                str(source),
                str(window_32k_separate),
                "-5",
                "-w",
                "32k",
            ]
        )
        default_window = work / "window-default.lz9a"
        run([str(serial), "c8", str(source), str(default_window), "-5"])
        assert window_32k_equals.read_bytes() == default_window.read_bytes()
        assert window_32k_separate.read_bytes() == default_window.read_bytes()
        for option in ("-w=", "-w"):
            command = [str(serial), "c8", str(source), str(out2), option]
            run(command, expected=2)
        run(
            [str(serial), "c8", str(source), str(out2), "-5", "-w", "16k"],
            expected=2,
        )
        run(
            [str(serial), "c8", str(source), str(out2), "-5", "-w=16k"],
            expected=2,
        )
        run(
            [
                str(serial),
                "c8",
                str(source),
                str(out2),
                "-5",
                "-w=64k",
                "-w",
                "32k",
            ],
            expected=2,
        )

        # --verbose is an exact synonym for -v and does not change the stream.
        verbose_short = work / "verbose-short.lz9a"
        verbose_long = work / "verbose-long.lz9a"
        short_report = run(
            [str(serial), "c8", str(source), str(verbose_short), "-5", "-v"]
        )
        long_report = run(
            [
                str(serial),
                "c8",
                str(source),
                str(verbose_long),
                "-5",
                "--verbose",
            ]
        )
        assert verbose_short.read_bytes() == verbose_long.read_bytes()
        assert b"Input size:" in short_report.stderr
        assert short_report.stderr == long_report.stderr
        run(
            [str(serial), "c8", str(source), str(out2), "-v", "--verbose"],
            expected=2,
        )

        run([str(serial), "c8", str(source), str(out1), "-5"])
        run([str(serial), "c8", str(source), str(out2), "-5", "--threads=999"])
        assert out1.read_bytes() == out2.read_bytes()
        serial_short_thread = work / "serial-short-thread.lz9a"
        run(
            [
                str(serial),
                "c8",
                str(source),
                str(serial_short_thread),
                "-5",
                "-T",
                "999",
            ]
        )
        assert serial_short_thread.read_bytes() == out1.read_bytes()
        run([str(threaded), "c8", str(source), str(out3), "-5", "--threads=1"])
        assert out3.read_bytes() == out1.read_bytes()
        thread_equals = work / "thread-equals.lz9a"
        thread_separate = work / "thread-separate.lz9a"
        run([str(threaded), "c8", str(source), str(thread_equals), "-5", "-T=1"])
        run(
            [
                str(threaded),
                "c8",
                str(source),
                str(thread_separate),
                "-5",
                "-T",
                "1",
            ]
        )
        assert thread_equals.read_bytes() == out1.read_bytes()
        assert thread_separate.read_bytes() == out1.read_bytes()
        thread_two_long = work / "thread-two-long.lz9a"
        thread_two_equals = work / "thread-two-equals.lz9a"
        thread_two_separate = work / "thread-two-separate.lz9a"
        run(
            [
                str(threaded),
                "c8",
                str(source),
                str(thread_two_long),
                "-5",
                "--threads=2",
            ]
        )
        run(
            [
                str(threaded),
                "c8",
                str(source),
                str(thread_two_equals),
                "-5",
                "-T=2",
            ]
        )
        run(
            [
                str(threaded),
                "c8",
                str(source),
                str(thread_two_separate),
                "-5",
                "-T",
                "2",
            ]
        )
        assert thread_two_equals.read_bytes() == thread_two_long.read_bytes()
        assert thread_two_separate.read_bytes() == thread_two_long.read_bytes()
        thread_two_64k_long = work / "thread-two-64k-long.lz9a"
        combined_short = work / "combined-short.lz9a"
        combined_reverse = work / "combined-reverse.lz9a"
        run(
            [
                str(threaded),
                "c8",
                str(source),
                str(thread_two_64k_long),
                "-5",
                "--window=64k",
                "--threads=2",
            ]
        )
        run(
            [
                str(threaded),
                "c8",
                str(source),
                str(combined_short),
                "-5",
                "-w",
                "64k",
                "-T",
                "2",
            ]
        )
        run(
            [
                str(threaded),
                "c8",
                str(source),
                str(combined_reverse),
                "-5",
                "-T",
                "2",
                "-w",
                "64k",
            ]
        )
        assert combined_short.read_bytes() == thread_two_64k_long.read_bytes()
        assert combined_reverse.read_bytes() == thread_two_64k_long.read_bytes()
        decoded_short_thread = work / "decoded-short-thread"
        run([str(threaded), "d", str(out1), str(decoded_short_thread), "-T", "2"])
        assert decoded_short_thread.read_bytes() == source.read_bytes()
        run([str(threaded), "c8", str(source), str(out4), "-5", "--threads=0"])
        run([str(threaded), "c8", str(source), str(out5), "-5"])
        assert (
            out4.read_bytes() == out5.read_bytes()
        ), "threaded default must be equivalent to --threads=0"

        run(
            [str(serial), "c8", str(source), str(out2), "--parallel-blocks"], expected=2
        )
        run(
            [str(threaded), "c8", str(source), str(out2), "--parallel-blocks"],
            expected=2,
        )
        run([str(threaded), "d", str(out1), str(out2), "--parallel-blocks"], expected=2)
        run([str(serial), "--memory", "--threads=2"])
        run([str(threaded), "c8", str(source), str(out2), "-T="], expected=2)
        run([str(threaded), "c8", str(source), str(out2), "-T"], expected=2)
        run(
            [str(threaded), "c8", str(source), str(out2), "-T", "-1"],
            expected=2,
        )
        run([str(threaded), "c8", str(source), str(out2), "-T=abc"], expected=2)
        run(
            [str(threaded), "c8", str(source), str(out2), "-T=1", "-T", "2"],
            expected=2,
        )
        run([str(threaded), "--memory", "-T=1", "-T", "2"], expected=2)

        serial_help = run([str(serial)], expected=2).stderr
        threaded_help = run([str(threaded)], expected=2).stderr
        assert b"--parallel-blocks" not in serial_help
        assert b"--parallel-blocks" not in threaded_help
        assert b"--threads" not in serial_help
        assert b"--threads=N" in threaded_help

        serial_report = run([str(serial), "--memory"]).stdout
        assert b"pthread build" not in serial_report
        assert b"parallel-block encoder state" not in serial_report
        assert b"encoder maximum allocated state" in serial_report
        assert b"encoder allocated state at levels 1-5" in serial_report
        assert b"decoder base allocated state" in serial_report
        assert b"decoder maximum allocated state" in serial_report

        one = run([str(threaded), "--memory", "--threads=1"]).stdout
        assert run([str(threaded), "--memory", "-T=1"]).stdout == one
        assert run([str(threaded), "--memory", "-T", "1"]).stdout == one
        assert run([str(serial), "--memory", "-T=1"]).stdout == serial_report
        assert run([str(serial), "--memory", "-T", "1"]).stdout == serial_report
        two = run([str(threaded), "--memory", "--threads=2"]).stdout
        assert run([str(threaded), "--memory", "-T=2"]).stdout == two
        assert run([str(threaded), "--memory", "-T", "2"]).stdout == two
        auto = run([str(threaded), "--memory"]).stdout
        many = run([str(threaded), "--memory", "--threads=999"]).stdout
        maximum = 32 if struct.calcsize("P") == 4 else 128
        match = re.search(
            rb"pthread build: (\d+) available logical processors; --threads=(\d+)",
            many,
        )
        assert match, many.decode(errors="replace")
        cpus, requested = (int(value) for value in match.groups())
        assert cpus == cpu_count
        assert requested == maximum, (requested, maximum)
        participants = re.search(rb"compression participants: (\d+)", many)
        assert participants, many.decode(errors="replace")
        assert int(participants.group(1)) == min(cpus, maximum)
        auto_participants = re.search(rb"compression participants: (\d+)", auto)
        assert auto_participants, auto.decode(errors="replace")
        assert int(auto_participants.group(1)) == min(cpus, maximum)
        assert b"compression participants: 1" in one
        assert b"compression mode: sequential" in one
        assert b"parallel-block encoder state" not in one
        assert b"compression participants: " in two
        assert (b"compression mode: independent blocks" in two) == (cpu_count > 1)
        assert (b"parallel-block encoder state" in two) == (cpu_count > 1)
        assert (b"parallel-block encoder state at levels 1-5" in two) == (cpu_count > 1)
        assert (b"compression mode: independent blocks" in auto) == (cpu_count > 1)
        assert b"window: 32768 symbols" in auto
        assert b"decompression symbol buffers: 49152 bytes maximum" in auto
        assert b"32766 bytes at levels 1-5" in auto
        assert b"decompression thread-pool allocations" in auto


def read_cpu_count(threaded):
    report = run([str(threaded), "--memory", "--threads=0"]).stdout
    match = re.search(rb"pthread build: (\d+) available logical processors", report)
    assert match, report.decode(errors="replace")
    return int(match.group(1))


def main():
    with tempfile.TemporaryDirectory(prefix="lz9ans-auto-block-threads-") as tmp:
        work = Path(tmp)
        serial = work / "lz9ans-serial"
        threaded = work / "lz9ans-pthread"
        build(serial, False)
        build(threaded, True)
        inputs = make_inputs(work)
        cpu_count = read_cpu_count(threaded)
        check_roundtrips(serial, threaded, work, inputs)
        check_streaming_pipeline(serial, threaded, work)
        check_stdio(threaded, work, inputs)
        check_options(serial, threaded, cpu_count)

    print(
        "PASS automatic block encoding: serial fallback at one participant; "
        "deterministic parallel output, continuous multi-block scheduling, "
        "32K/64K history, c8/c9, all levels, V9 decoding, stdio, and options"
    )


if __name__ == "__main__":
    main()
