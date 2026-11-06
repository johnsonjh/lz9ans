#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

from pathlib import Path
import os
import struct
import subprocess
import tempfile
import zlib

from inspect_lz9ans import FormatError, block_header, decode, pack9

ROOT = Path(__file__).resolve().parent
BINARY = Path(os.environ.get("LZ9ANS_BIN", str(ROOT / "../lz9ans"))).resolve()


def pack_bits(values, width):
    result = bytearray()
    acc = bits = 0
    for value in values:
        assert 0 <= value < (1 << width)
        acc = (acc << width) | value
        bits += width
        while bits >= 8:
            bits -= 8
            result.append((acc >> bits) & 255)
            acc &= (1 << bits) - 1
    if bits:
        result.append((acc << (8 - bits)) & 255)
    return bytes(result)


def make_frame_header(mode, count, flags=0, extensions=b""):
    length = 13 + len(extensions) + 4
    header = bytearray(b"LZ9A" + bytes((9, mode)) + struct.pack("<H", length))
    header += struct.pack("<I", count) + bytes((flags,)) + extensions
    header += struct.pack("<I", zlib.crc32(header))
    return bytes(header)


def make_literal_frame(values, width=7, mode=8):
    payload = pack_bits(values, width)
    meaningful = len(values) * width
    padding = (8 - (meaningful & 7)) & 7
    block = bytearray(23)
    block[0] = 1  # LZ, no optional flags
    struct.pack_into("<HHH", block, 1, len(values), 0, 5)
    lengths = (len(payload), 0, 0, 0, 0)
    struct.pack_into("<5H", block, 7, *lengths)
    struct.pack_into("<H", block, 17, padding)
    struct.pack_into("<I", block, 19, zlib.crc32(pack9(values)))
    descriptor = bytes((1, width, 0, 0, 0))  # RAW LIT, EMPTY LL/ML/OF
    return make_frame_header(mode, len(values)) + bytes(block) + descriptor + payload


def add_header_extensions(frame, extensions):
    old_length = struct.unpack_from("<H", frame, 6)[0]
    prefix = bytearray(frame[:13])
    new_length = 13 + len(extensions) + 4
    struct.pack_into("<H", prefix, 6, new_length)
    header = prefix + extensions
    header += struct.pack("<I", zlib.crc32(header))
    return bytes(header) + frame[old_length:]


def tlv(identifier, value=b""):
    return bytes((identifier,)) + struct.pack("<H", len(value)) + value


def run_decode(data, directory, name):
    archive = directory / (name + ".lz9a")
    output = directory / (name + ".out")
    archive.write_bytes(data)
    return (
        subprocess.run(
            [str(BINARY), "d", str(archive), str(output)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=30,
        ),
        output,
    )


def must_reject(data, directory, name):
    result, _ = run_decode(data, directory, name)
    assert result.returncode != 0, (name, result.stderr.decode())
    try:
        decode(data)
    except (FormatError, IndexError, struct.error, OverflowError):
        pass
    else:
        raise AssertionError("independent decoder accepted " + name)


def main():
    with tempfile.TemporaryDirectory(prefix="lz9ans-v9-format-") as temporary:
        temp = Path(temporary)

        # Exercise every representable padding count in the LIT stream.
        for count in range(1, 9):
            values = [((index * 13) + 3) & 0x7F for index in range(count)]
            frame = make_literal_frame(values, 7)
            block = block_header(frame, 17)
            expected_padding = (8 - ((count * 7) & 7)) & 7
            assert block["padding"] & 7 == expected_padding
            independent, _ = decode(frame)
            assert independent == bytes(values)
            result, output = run_decode(frame, temp, "pad-" + str(expected_padding))
            assert result.returncode == 0, result.stderr.decode()
            assert output.read_bytes() == bytes(values)

            if expected_padding:
                bad_padding = bytearray(frame)
                bad_padding[-1] |= 1
                must_reject(bytes(bad_padding), temp, "bad-pad-" + str(count))

        # The high reserved bit in the packed 15-bit padding field is invalid.
        bad_reserved_padding = bytearray(make_literal_frame([1, 2, 3], 7))
        bad_reserved_padding[17 + 18] |= 0x80
        must_reject(bytes(bad_reserved_padding), temp, "reserved-padding-bit")

        # A valid unknown noncritical frame TLV is skipped by both decoders.
        sample = make_literal_frame([1, 2, 3, 4], 7)
        extended = add_header_extensions(sample, tlv(0x01, b"xy"))
        independent, _ = decode(extended)
        assert independent == bytes((1, 2, 3, 4))
        result, output = run_decode(extended, temp, "noncritical-extension")
        assert result.returncode == 0, result.stderr.decode()
        assert output.read_bytes() == independent

        # Identifier zero is invalid irrespective of its critical bit.
        must_reject(add_header_extensions(sample, tlv(0x00)), temp, "zero-extension-id")
        must_reject(
            add_header_extensions(sample, tlv(0x80)), temp, "critical-zero-extension-id"
        )

        # Unknown critical, duplicate, truncated, and malformed TLVs fail closed.
        must_reject(
            add_header_extensions(sample, tlv(0x83)), temp, "unknown-critical-extension"
        )
        must_reject(
            add_header_extensions(sample, tlv(0x01, b"a") + tlv(0x81)),
            temp,
            "duplicate-extension-id",
        )
        must_reject(
            add_header_extensions(sample, bytes((0x01, 0x08, 0x00, 0x41))),
            temp,
            "oversized-extension-length",
        )

        # The defined unknown-count extension is recognized but streaming is
        # deliberately not implemented in this release.
        empty = make_frame_header(8, 0)
        unknown_count = add_header_extensions(empty, tlv(0x81))
        must_reject(unknown_count, temp, "reserved-unknown-count-frame")
        bad_unknown_count = add_header_extensions(sample, tlv(0x81))
        must_reject(bad_unknown_count, temp, "unknown-count-with-nonzero-count")
        bad_unknown_count_length = add_header_extensions(empty, tlv(0x81, b"x"))
        must_reject(bad_unknown_count_length, temp, "unknown-count-bad-length")

        # END is reserved for the future streaming/terminal-metadata feature.
        # The current known-length decoder must reject both its minimal and
        # extended encodings rather than treating them as data or ignoring them.
        must_reject(sample + b"\x02", temp, "reserved-minimal-end-record")
        final_truncation_sample = make_literal_frame([0b101101000], 9, mode=9)
        end_tlv = tlv(0x82, b"\x03")  # truncate three zero low bits of final symbol
        end_prefix = b"\x06" + struct.pack("<H", len(end_tlv)) + end_tlv
        extended_end = end_prefix + struct.pack("<I", zlib.crc32(end_prefix))
        assert len(extended_end) == 1 + 2 + len(end_tlv) + 4
        must_reject(
            final_truncation_sample + extended_end,
            temp,
            "reserved-extended-end-record",
        )
        invalid_end_tlv = tlv(0x82, b"\x09")
        invalid_end_prefix = (
            b"\x06" + struct.pack("<H", len(invalid_end_tlv)) + invalid_end_tlv
        )
        invalid_extended_end = invalid_end_prefix + struct.pack(
            "<I", zlib.crc32(invalid_end_prefix)
        )
        must_reject(
            final_truncation_sample + invalid_extended_end,
            temp,
            "reserved-invalid-truncation-end",
        )

        # The maximum permitted frame header is accepted when its TLV area
        # consists of one maximum-sized noncritical extension.
        max_extension = tlv(0x01, b"x" * (4096 - 13 - 4 - 3))
        maximum_header = add_header_extensions(empty, max_extension)
        assert struct.unpack_from("<H", maximum_header, 6)[0] == 4096
        independent, _ = decode(maximum_header)
        assert independent == b""
        result, output = run_decode(maximum_header, temp, "maximum-frame-header")
        assert result.returncode == 0, result.stderr.decode()
        assert output.read_bytes() == b""

        # Header lengths are bounded even though the wire field is 16-bit.
        oversized_header = bytearray(empty)
        struct.pack_into("<H", oversized_header, 6, 4097)
        must_reject(bytes(oversized_header), temp, "oversized-frame-header")

    print(
        "PASS v9 compact headers, all LIT padding counts, TLV validation, and reserved streaming/END rejection"
    )


if __name__ == "__main__":
    main()
