#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT
# scspell-id: 1bab442e-e45d-11f1-9194-80ee73e9b8e7

import os
from pathlib import Path

try:
    import resource
except ImportError:
    resource = None
import shlex
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "lz9ans.c"
WRAPPER = ROOT / "tests" / "pthread_faults.c"


def run(command, *, env=None, expected=0):
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        env=env,
        preexec_fn=disable_core_dumps if os.name == "posix" else None,
    )
    if expected is None:
        return result
    if expected == 0 and result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {command!r}\n"
            f"stdout={result.stdout!r}\n"
            f"stderr={result.stderr.decode(errors='replace')}"
        )
    return result


def disable_core_dumps():
    if resource is not None:
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def build(binary, work):
    cc = shlex.split(os.environ.get("CC", "cc"))
    cflags = shlex.split(
        os.environ.get(
            "CFLAGS",
            "-D_ALL_SOURCE -D_GNU_SOURCE -O2 -ansi -pedantic " "-Wall -Wextra -Werror",
        )
    )
    for flag in ("-ansi", "-pedantic", "-Wall", "-Wextra", "-Werror"):
        if flag not in cflags:
            cflags.append(flag)

    thread_cflags = shlex.split(os.environ.get("THREAD_CFLAGS", "-pthread"))
    thread_ldflags = shlex.split(os.environ.get("THREAD_LDFLAGS", "-pthread"))
    ldflags = shlex.split(os.environ.get("LDFLAGS", ""))
    app_object = work / "lz9ans.o"
    wrapper_object = work / "pthread_faults.o"

    wrapper_command = (
        cc + cflags + thread_cflags + ["-c", str(WRAPPER), "-o", str(wrapper_object)]
    )
    run(wrapper_command)

    aliases = (
        ("pthread_create", "lz9_test_pthread_create"),
        ("pthread_join", "lz9_test_pthread_join"),
        ("pthread_cond_destroy", "lz9_test_pthread_cond_destroy"),
        ("pthread_mutex_destroy", "lz9_test_pthread_mutex_destroy"),
        ("pthread_attr_destroy", "lz9_test_pthread_attr_destroy"),
        ("sysconf", "lz9_test_sysconf"),
    )
    app_command = cc + cflags + thread_cflags + ["-DLZ9ANS_PTHREADS"]
    app_command += [f"-D{name}={alias}" for name, alias in aliases]
    app_command += ["-c", str(SOURCE), "-o", str(app_object)]
    run(app_command)

    link_command = (
        cc
        + cflags
        + [str(app_object), str(wrapper_object), "-o", str(binary)]
        + ldflags
        + thread_ldflags
        + ["-lm"]
    )
    run(link_command)


def invoke(binary, source, archive, work, fault=None):
    trace = work / "pthread-trace.txt"
    try:
        trace.unlink()
    except FileNotFoundError:
        pass

    env = os.environ.copy()
    env["LZ9ANS_TEST_PTHREAD_TRACE"] = str(trace)
    if fault:
        env["LZ9ANS_TEST_PTHREAD_FAULT"] = fault
    else:
        env.pop("LZ9ANS_TEST_PTHREAD_FAULT", None)

    result = run(
        [str(binary), "c8", str(source), str(archive), "-1", "--threads=3"],
        env=env,
        expected=None,
    )
    events = trace.read_text().splitlines() if trace.exists() else []
    return result, events


def expect_abort(binary, source, work, fault, operation):
    archive = work / f"{fault.replace(':', '-')}.lz9a"
    result, events = invoke(binary, source, archive, work, fault)
    assert result.returncode == -signal.SIGABRT, (
        fault,
        result.returncode,
        result.stderr.decode(errors="replace"),
    )
    stderr = result.stderr.decode(errors="replace")
    assert f"{operation} failed (pthread error" in stderr, (fault, stderr)
    for item in fault.split(","):
        assert f"injected {item.replace(':', ' ')}" in events, (fault, events)
    return events


def main():
    if os.name != "posix":
        print("SKIP pthread teardown fault injection: POSIX host required")
        return

    with tempfile.TemporaryDirectory(prefix="lz9ans-pthread-cleanup-") as tmp:
        work = Path(tmp)
        binary = work / "lz9ans-pthread-faults"
        source = work / "multi-block.input"
        archive = work / "baseline.lz9a"
        source.write_bytes(bytes(range(256)) * 512)
        build(binary, work)

        # A normal run exercises creation, attribute destruction, worker joins,
        # both condition-variable destructions, and mutex destruction.
        result, events = invoke(binary, source, archive, work)
        assert result.returncode == 0
        assert events.count("call create 1") == 1
        assert events.count("call create 2") == 1
        assert events.count("call attr_destroy 1") == 1
        assert events.count("call join 1") == 1
        assert events.count("call join 2") == 1
        assert events.count("call cond_destroy 1") == 1
        assert events.count("call cond_destroy 2") == 1
        assert events.count("call mutex_destroy 1") == 1
        print("PASS normal pthread pool teardown")

        # Fail the second create after the first worker exists.  Initialization
        # must stop and join that worker and destroy only initialized objects,
        # then return an ordinary initialization failure rather than abort.
        failed_archive = work / "partial-create.lz9a"
        result, events = invoke(binary, source, failed_archive, work, "create:2")
        assert result.returncode != 0
        assert result.returncode != -signal.SIGABRT
        assert "injected create 2" in events
        assert "call join 1" in events
        assert "call cond_destroy 1" in events
        assert "call cond_destroy 2" in events
        assert "call mutex_destroy 1" in events
        print("PASS recoverable partial pthread_create failure cleanup")

        # A join failure while rolling back a partially created pool must be
        # fatal before any initialized synchronization object is destroyed.
        events = expect_abort(
            binary,
            source,
            work,
            "create:2,join:1",
            "pthread_join",
        )
        assert "injected create 2" in events
        assert "call attr_destroy 1" in events
        assert "injected join 1" in events
        assert not any("cond_destroy" in event for event in events)
        assert not any("mutex_destroy" in event for event in events)
        print("PASS fail-fast pthread_join during partial-init rollback")

        events = expect_abort(
            binary,
            source,
            work,
            "create:2,attr_destroy:1",
            "pthread_attr_destroy",
        )
        assert "injected create 2" in events
        assert "injected attr_destroy 1" in events
        assert not any("join" in event for event in events)
        print("PASS fail-fast pthread_attr_destroy during partial-init rollback")

        events = expect_abort(
            binary,
            source,
            work,
            "create:2,cond_destroy:1",
            "pthread_cond_destroy(work_done)",
        )
        assert "call join 1" in events
        assert "injected cond_destroy 1" in events
        assert "call cond_destroy 2" not in events
        assert not any("mutex_destroy" in event for event in events)
        print("PASS fail-fast pthread_cond_destroy during partial-init rollback")

        events = expect_abort(
            binary,
            source,
            work,
            "create:2,mutex_destroy:1",
            "pthread_mutex_destroy",
        )
        assert "call join 1" in events
        assert "call cond_destroy 1" in events
        assert "call cond_destroy 2" in events
        assert "injected mutex_destroy 1" in events
        print("PASS fail-fast pthread_mutex_destroy during partial-init rollback")

        # Every teardown API failure must abort before later synchronization
        # objects or pool-owned memory can be destroyed/freed.
        events = expect_abort(binary, source, work, "join:1", "pthread_join")
        assert not any("cond_destroy" in event for event in events)
        assert not any("mutex_destroy" in event for event in events)
        print("PASS fail-fast pthread_join failure")

        events = expect_abort(
            binary,
            source,
            work,
            "cond_destroy:1",
            "pthread_cond_destroy(work_done)",
        )
        assert "call join 1" in events and "call join 2" in events
        assert "call cond_destroy 2" not in events
        assert not any("mutex_destroy" in event for event in events)
        print("PASS fail-fast first pthread_cond_destroy failure")

        events = expect_abort(
            binary,
            source,
            work,
            "cond_destroy:2",
            "pthread_cond_destroy(work_ready)",
        )
        assert "call cond_destroy 1" in events
        assert "injected cond_destroy 2" in events
        assert not any("mutex_destroy" in event for event in events)
        print("PASS fail-fast second pthread_cond_destroy failure")

        events = expect_abort(
            binary, source, work, "mutex_destroy:1", "pthread_mutex_destroy"
        )
        assert "call cond_destroy 1" in events
        assert "call cond_destroy 2" in events
        assert "injected mutex_destroy 1" in events
        print("PASS fail-fast pthread_mutex_destroy failure")

        events = expect_abort(
            binary, source, work, "attr_destroy:1", "pthread_attr_destroy"
        )
        assert "call create 1" in events and "call create 2" in events
        assert "injected attr_destroy 1" in events
        assert not any("join" in event for event in events)
        print("PASS fail-fast pthread_attr_destroy failure")


if __name__ == "__main__":
    main()
