#!/usr/bin/env python3

from pathlib import Path
import subprocess
import tempfile

from inspect_lz9ans import pack9

ROOT = Path(__file__).resolve().parent
BINARY = (ROOT / "lz9ans").resolve()


def run(*args):
    return subprocess.run(
        [str(BINARY), *map(str, args)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=60,
    )


def main():
    with tempfile.TemporaryDirectory(prefix="lz9ans-verbose-") as temp:
        temp = Path(temp)
        source = temp / "input"
        plain_archive = temp / "plain.lz9a"
        verbose_archive = temp / "verbose.lz9a"
        decoded = temp / "decoded"
        empty = temp / "empty"
        empty_archive = temp / "empty.lz9a"

        sample = b"LZ9ANS verbose reporting must not alter the stream.\n" * 80
        source.write_bytes(sample)
        result = run("c8", source, plain_archive, "-6")
        assert result.returncode == 0, result.stderr.decode()
        result = run("c8", source, verbose_archive, "-v", "-6")
        assert result.returncode == 0, result.stderr.decode()
        assert plain_archive.read_bytes() == verbose_archive.read_bytes()
        result = run("c8", source, temp / "verbose-after.lz9a", "-6", "-v")
        assert result.returncode == 0, result.stderr.decode()
        assert plain_archive.read_bytes() == (temp / "verbose-after.lz9a").read_bytes()
        report = result.stderr.decode()
        assert f"Input size: {len(sample)} bytes" in report
        assert f"Output size: {len(verbose_archive.read_bytes())} bytes" in report
        assert "Compression ratio (output/input):" in report
        assert "Compression factor (input/output):" in report

        result = run("d", verbose_archive, decoded, "-v")
        assert result.returncode == 0, result.stderr.decode()
        assert decoded.read_bytes() == sample
        report = result.stderr.decode()
        assert f"Input size: {len(verbose_archive.read_bytes())} bytes" in report
        assert f"Output size: {len(sample)} bytes" in report
        assert "Output/input size ratio:" in report

        # Mode 9 reports physical packed-input bytes, not logical symbol count.
        packed9 = temp / "input9.packed"
        archive9 = temp / "input9.lz9a"
        values9 = [511, 256, 0, 320, 17] * 32
        raw9 = pack9(values9)
        packed9.write_bytes(raw9)
        result = run("c9", packed9, archive9, "-v", "-6")
        assert result.returncode == 0, result.stderr.decode()
        assert f"Input size: {len(raw9)} bytes" in result.stderr.decode()
        result = run("d", archive9, decoded, "-v")
        assert result.returncode == 0, result.stderr.decode()
        assert decoded.read_bytes() == raw9
        assert f"Output size: {len(raw9)} bytes" in result.stderr.decode()

        empty.write_bytes(b"")
        result = run("c8", empty, empty_archive, "-v")
        assert result.returncode == 0, result.stderr.decode()
        assert "Input size: 0 bytes" in result.stderr.decode()
        assert "Compression ratio: n/a (empty input)" in result.stderr.decode()

        result = run("c8", source, temp / "bad.lz9a", "-v", "-6", "-7")
        assert result.returncode == 2
        print(
            "PASS verbose size/ratio reporting, mode 8/9, stream identity, and empty input"
        )


if __name__ == "__main__":
    main()
