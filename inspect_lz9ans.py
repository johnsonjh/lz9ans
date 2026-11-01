#!/usr/bin/env python3
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT

import argparse
from collections import Counter
from pathlib import Path
import struct
import zlib


class FormatError(ValueError):
    pass


def check(condition, message):
    if not condition:
        raise FormatError(message)


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def frame_header(data):
    check(len(data) >= 17 and data[:4] == b"LZ9A", "invalid frame header")
    length = u16(data, 6)
    check(17 <= length <= 4096 and length <= len(data), "invalid frame-header length")
    return {
        "version": data[4],
        "mode": data[5],
        "length": length,
        "count": u32(data, 8),
        "flags": data[12],
    }


def block_header(data, position):
    check(position + 7 <= len(data), "truncated block prefix")
    check(data[position : position + 4] == b"LZ9B", "invalid block magic")
    block_type = data[position + 4] & 3
    if block_type == 0:
        header_size = 11
        check(position + header_size <= len(data), "truncated STORED header")
        h = data[position : position + header_size]
        result = {
            "type": 0,
            "width": h[4] >> 2,
            "n": u16(h, 5),
            "q": 0,
            "desc_length": 0,
            "byte_lengths": (((u16(h, 5) * (h[4] >> 2) + 7) // 8), 0, 0, 0, 0),
            "bit_lengths": (0, 0, 0, 0, 0),
            "crc": u32(h, 7),
            "header_size": header_size,
        }
        result["bit_lengths"] = (result["n"] * result["width"], 0, 0, 0, 0)
    elif block_type == 1:
        header_size = 27
        check(position + header_size <= len(data), "truncated LZ header")
        h = data[position : position + header_size]
        n, q, desc_length = struct.unpack_from("<HHH", h, 5)
        byte_lengths = struct.unpack_from("<5H", h, 11)
        padding = u16(h, 21)
        bit_lengths = tuple(
            byte_lengths[i] * 8 - ((padding >> (3 * i)) & 7) for i in range(5)
        )
        result = {
            "type": 1,
            "width": (h[4] >> 2) & 3,
            "aligned": bool(h[4] & 16),
            "n": n,
            "q": q,
            "desc_length": desc_length,
            "byte_lengths": byte_lengths,
            "bit_lengths": bit_lengths,
            "padding": padding,
            "crc": u32(h, 23),
            "header_size": header_size,
        }
    else:
        raise FormatError("reserved block type")

    descriptor_start = position + result["header_size"]
    descriptor_end = descriptor_start + result["desc_length"]
    payload_end = descriptor_end + sum(result["byte_lengths"])
    check(payload_end <= len(data), "truncated block payload")
    result["descriptor"] = data[descriptor_start:descriptor_end]
    result["next_position"] = payload_end
    result["payload_start"] = descriptor_end
    return result


def read_var(data, position):
    value = 0
    for shift in (0, 7, 14):
        check(position < len(data), "truncated varint")
        octet = data[position]
        position += 1
        value |= (octet & 127) << shift
        if octet < 128:
            return value, position
    raise FormatError("oversized varint")


class Bits:
    def __init__(self, data, meaningful, backwards=False):
        self.data = data
        self.length = meaningful
        self.position = 0
        self.backwards = backwards
        check(len(data) == (meaningful + 7) // 8, "incorrect bitstream size")
        if meaningful % 8:
            check(not (data[-1] & ((1 << (8 - meaningful % 8)) - 1)), "nonzero padding")

    def get(self, count):
        check(self.position + count <= self.length, "bitstream exhausted")
        start = self.length - self.position - count if self.backwards else self.position
        value = 0
        for at in range(start, start + count):
            value = (value << 1) | ((self.data[at // 8] >> (7 - at % 8)) & 1)
        self.position += count
        return value

    def finish(self):
        check(self.position == self.length, "unused meaningful bits")


def truncated_value(bits, count):
    check(count > 0, "invalid truncated-binary range")
    width = count.bit_length() - 1
    cutoff = (1 << (width + 1)) - count
    value = bits.get(width)
    if value >= cutoff:
        value = ((value << 1) | bits.get(1)) - cutoff
    return value


def build_transition(freq):
    check(2 <= sum(value != 0 for value in freq) <= 1024, "bad table occupancy")
    check(sum(freq) == 1024, "bad normalized frequency sum")
    spread = [None] * 1024
    place = 0
    for symbol, frequency in enumerate(freq):
        for _ in range(frequency):
            spread[place] = symbol
            place = (place + 643) & 1023
    check(place == 0, "bad ANS table spread")
    seen = [0] * len(freq)
    transition = []
    for symbol in spread:
        counter = freq[symbol] + seen[symbol]
        seen[symbol] += 1
        nbits = 10 - (counter.bit_length() - 1)
        baseline = (counter << nbits) - 1024
        transition.append((symbol, nbits, baseline))
    return transition


def parse_descriptor(desc, position, kind, alphabet, table_cache):
    check(position < len(desc), "truncated descriptor")
    mode = desc[position]
    position += 1
    symbol = width = None
    transition = None
    check(mode in (0, 1, 2, 3, 4), "invalid entropy mode")
    if mode == 0:
        pass
    elif mode == 1:
        check(position < len(desc), "truncated raw descriptor")
        width = desc[position]
        position += 1
        allowed = (7, 8, 9) if kind == 0 else (5,)
        check(width in allowed, "bad raw width")
    elif mode == 2:
        symbol, position = read_var(desc, position)
        check(symbol < alphabet, "bad RLE symbol")
    elif mode == 4:
        transition = table_cache[kind]
        check(transition is not None, "reuse before table initialization")
    else:
        bitmap_bytes = (alphabet + 7) // 8
        check(position + bitmap_bytes <= len(desc), "truncated table bitmap")
        bitmap = desc[position : position + bitmap_bytes]
        position += bitmap_bytes
        if alphabet % 8:
            check(
                not (bitmap[-1] & ((1 << (8 - alphabet % 8)) - 1)),
                "nonzero unused bitmap bits",
            )
        present = [
            symbol
            for symbol in range(alphabet)
            if bitmap[symbol // 8] & (1 << (7 - symbol % 8))
        ]
        check(2 <= len(present) <= alphabet, "bad table occupancy")
        freq = [0] * alphabet
        bits = Bits(desc[position:], (len(desc) - position) * 8)
        assigned = 0
        remaining = len(present)
        for symbol in present:
            if kind == 0:
                zeros = 0
                while bits.get(1) == 0:
                    zeros += 1
                    check(zeros <= 10, "oversized Elias-gamma frequency")
                value = (1 << zeros) | bits.get(zeros)
            else:
                remaining -= 1
                limit = 1024 - assigned - remaining
                value = truncated_value(bits, limit) + 1
            check(1 <= value <= 1024 - assigned, "invalid normalized frequency")
            freq[symbol] = value
            assigned += value
        check(assigned == 1024, "bad normalized frequency sum")
        padding = (-bits.position) & 7
        if padding:
            check(bits.get(padding) == 0, "nonzero descriptor padding")
        position += bits.position // 8
        transition = build_transition(freq)
        table_cache[kind] = transition
    return (mode, symbol, width, transition), position


class Symbols:
    def __init__(self, descriptor, count, data, meaningful):
        self.count = count
        self.used = 0
        self.mode, self.symbol, self.width, self.transition = descriptor
        self.bits = Bits(data, meaningful, self.mode in (3, 4))
        self.state = self.bits.get(10) if self.mode in (3, 4) else 0

    def get(self):
        if self.count is not None:
            check(self.used < self.count, "too many entropy symbols")
        self.used += 1
        if self.mode == 1:
            return self.bits.get(self.width)
        if self.mode == 2:
            return self.symbol
        check(self.mode in (3, 4), "read from empty entropy stream")
        symbol, count, baseline = self.transition[self.state]
        self.state = baseline + self.bits.get(count)
        check(self.state < 1024, "ANS state out of range")
        return symbol

    def finish(self, expected_count=None):
        count = self.count if expected_count is None else expected_count
        if count is not None:
            check(self.used == count, "wrong entropy symbol count")
            check((self.mode == 0) == (count == 0), "empty descriptor mismatch")
        self.bits.finish()
        if self.mode in (3, 4):
            check(self.state == 0, "bad terminal ANS state")


def pack9(values):
    result = bytearray()
    pending = bits = 0
    for value in values:
        check(0 <= value <= 511, "not a nine-bit symbol")
        pending = (pending << 9) | value
        bits += 9
        while bits >= 8:
            bits -= 8
            result.append((pending >> bits) & 255)
            pending &= (1 << bits) - 1
    if bits:
        result.append(pending << (8 - bits))
    return bytes(result)


def promote(reps, distance):
    if distance in reps:
        reps.remove(distance)
    else:
        reps.pop()
    reps.insert(0, distance)


def decode_length(code, bias, extra):
    check(0 <= code <= 31, "invalid length code")
    if code < 16:
        return code + bias
    exponent = code - 16
    return bias + 15 + (1 << exponent) + extra.get(exponent)


def parse_frame_extensions(data, header_length, target):
    position = 13
    end = header_length - 4
    seen = set()
    unknown_count = False
    while position < end:
        check(end - position >= 3, "truncated frame extension")
        identifier = data[position]
        length = u16(data, position + 1)
        position += 3
        check(
            (identifier & 0x7F) != 0 and length <= end - position, "bad frame extension"
        )
        key = identifier & 0x7F
        check(key not in seen, "duplicate frame extension")
        seen.add(key)
        if identifier == 0x81:
            check(length == 0 and target == 0, "invalid unknown-count extension")
            unknown_count = True
        elif identifier & 0x80:
            raise FormatError("unknown critical frame extension")
        position += length
    check(position == end, "invalid frame extension area")
    check(not unknown_count, "unknown-length streams are not supported by this decoder")


def decode(data):
    check(len(data) >= 17 and data[:4] == b"LZ9A", "invalid frame magic")
    version, mode = data[4], data[5]
    header_length = u16(data, 6)
    check(
        version == 8
        and mode in (8, 9)
        and 17 <= header_length <= 4096
        and header_length <= len(data),
        "unsupported frame header",
    )
    flags = data[12]
    check(
        zlib.crc32(data[: header_length - 4]) == u32(data, header_length - 4),
        "corrupt frame header",
    )
    check((flags & ~0x19) == 0, "unsupported or reserved feature flags")
    auto_aligned = bool(flags & 16)
    aligned_global = bool(flags & 8)
    check(not auto_aligned or aligned_global, "invalid auto-alignment flags")
    target = u32(data, 8)
    parse_frame_extensions(data, header_length, target)
    window = 65536 if flags & 1 else 32768
    of_alphabet = 21 if window == 65536 else 20
    position = header_length
    output = []
    reps = [0, 0, 0]
    stats = Counter()
    table_cache = [None] * 4

    while len(output) < target:
        check(position + 7 <= len(data), "missing block prefix")
        prefix = data[position : position + 7]
        check(prefix[:4] == b"LZ9B", "invalid block magic")
        block_type = prefix[4] & 3
        block_start = len(output)

        if block_type == 0:
            check(position + 11 <= len(data), "truncated STORED header")
            h = data[position : position + 11]
            position += 11
            width = h[4] >> 2
            n = u16(h, 5)
            expected_crc = u32(h, 7)
            check(0 < n <= min(32768, target - len(output)), "bad STORED dimensions")
            check(width in (7, 8, 9), "bad STORED width")
            meaningful = n * width
            size = (meaningful + 7) // 8
            check(position + size <= len(data), "truncated STORED payload")
            bits = Bits(data[position : position + size], meaningful)
            position += size
            stats["stored"] += 1
            for _ in range(n):
                value = bits.get(width)
                check(
                    value < (256 if mode == 8 else 512),
                    "stored symbol exceeds frame input width",
                )
                output.append(value)
            bits.finish()
        elif block_type == 1:
            check(position + 27 <= len(data), "truncated LZ header")
            h = data[position : position + 27]
            position += 27
            check((h[4] & 0xE0) == 0, "invalid LZ block flags")
            width = (h[4] >> 2) & 3
            block_aligned = bool(h[4] & 16)
            check(width == 0, "nonzero LZ block width")
            check(
                not block_aligned or auto_aligned,
                "block alignment flag without per-block mode",
            )
            aligned_distances = block_aligned if auto_aligned else aligned_global
            n, sequence_count, desc_length = struct.unpack_from("<HHH", h, 5)
            byte_lengths = struct.unpack_from("<5H", h, 11)
            padding = u16(h, 21)
            expected_crc = u32(h, 23)
            check(
                0 < n <= min(32768, target - len(output))
                and sequence_count <= n // 3
                and desc_length <= 8192
                and (padding & 0x8000) == 0,
                "bad LZ block dimensions",
            )
            meaningful_lengths = []
            max_bits = (
                [32768 * 10 + 10] + [32768 // 3 * 10 + 10] * 3 + [32768 // 3 * 46]
            )
            for index, byte_length in enumerate(byte_lengths):
                pad = (padding >> (3 * index)) & 7
                check(not (byte_length == 0 and pad), "padding on empty stream")
                check(pad <= byte_length * 8, "padding exceeds stream")
                meaningful = byte_length * 8 - pad
                check(meaningful <= max_bits[index], "excessive stream length")
                meaningful_lengths.append(meaningful)
            sizes = list(byte_lengths)
            check(sum(sizes) <= 81920, "excessive combined payload")
            check(
                position + desc_length + sum(sizes) <= len(data),
                "truncated block payload",
            )
            desc = data[position : position + desc_length]
            position += desc_length
            raw = []
            for size, meaningful in zip(sizes, meaningful_lengths):
                raw.append(Bits(data[position : position + size], meaningful))
                position += size
            stats["lz"] += 1
            kinds = (0, 1, 2, 3)
            descriptors = []
            index = 0
            for kind in kinds:
                alphabet = 512 if kind == 0 else of_alphabet if kind == 3 else 32
                descriptor, index = parse_descriptor(
                    desc, index, kind, alphabet, table_cache
                )
                descriptors.append(descriptor)
            check(index == len(desc), "extra entropy descriptor bytes")
            for stream_descriptor in descriptors[1:]:
                check(
                    (stream_descriptor[0] == 0) == (sequence_count == 0),
                    "empty sequence stream mismatch",
                )
            streams = []
            for i, descriptor in enumerate(descriptors):
                count = None if i == 0 else sequence_count
                streams.append(Symbols(descriptor, count, raw[i].data, raw[i].length))
            extra = raw[4]
            used_literals = 0
            produced = 0
            for _ in range(sequence_count):
                run = decode_length(streams[1].get(), 0, extra)
                check(produced + run <= n, "invalid literal run")
                for _ in range(run):
                    value = streams[0].get()
                    check(
                        value < (256 if mode == 8 else 512),
                        "literal exceeds frame input width",
                    )
                    output.append(value)
                used_literals += run
                produced += run
                match_length = decode_length(streams[2].get(), 3, extra)
                stats["match_lt19" if match_length < 19 else "match_ge19"] += 1
                code = streams[3].get()
                stats[f"offset_{code}"] += 1
                if code <= 2:
                    distance = reps[code]
                    check(distance != 0, "uninitialized repeat distance")
                    promote(reps, distance)
                elif code == 3:
                    distance = 1
                elif code == 19:
                    distance = 32768
                    check(distance <= window, "distance exceeds window")
                    promote(reps, distance)
                elif code == 20 and window == 65536:
                    if aligned_distances:
                        if extra.get(1):
                            distance = 32772 + (extra.get(13) << 2)
                        else:
                            distance = 32769 + extra.get(15)
                            check(distance % 4 != 0, "noncanonical unaligned distance")
                    else:
                        distance = 32769 + extra.get(15)
                    check(distance <= window, "distance exceeds window")
                    promote(reps, distance)
                else:
                    check(4 <= code <= 18, "bad explicit offset code")
                    exponent = code - 4
                    if aligned_distances and code >= 6:
                        if extra.get(1):
                            distance = (1 << exponent) + (extra.get(exponent - 2) << 2)
                        else:
                            distance = (1 << exponent) + extra.get(exponent)
                            check(distance % 4 != 0, "noncanonical unaligned distance")
                    else:
                        distance = (1 << exponent) + extra.get(exponent)
                    promote(reps, distance)
                check(
                    1 <= distance <= window
                    and distance <= len(output)
                    and match_length >= 3
                    and produced + match_length <= n,
                    "invalid match length or distance",
                )
                for _ in range(match_length):
                    output.append(output[-distance])
                produced += match_length
            trailing = n - produced
            for _ in range(trailing):
                value = streams[0].get()
                check(
                    value < (256 if mode == 8 else 512),
                    "terminal literal exceeds frame input width",
                )
                output.append(value)
            actual_literals = used_literals + trailing
            check(
                (descriptors[0][0] == 0) == (actual_literals == 0),
                "empty literal stream mismatch",
            )
            for stream in streams:
                stream.finish()
            extra.finish()
        else:
            raise FormatError("unsupported or reserved block type")

        check(
            zlib.crc32(pack9(output[block_start:])) == expected_crc,
            "invalid decoded block CRC",
        )
        check(len(output) - block_start == n, "block length mismatch")

    check(position == len(data), "trailing compressed data")
    return (bytes(output) if mode == 8 else pack9(output)), stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    result, statistics = decode(args.archive.read_bytes())
    if args.output:
        args.output.write_bytes(result)
    print(f"independent decode: {len(result)} octets")
    print("offset code counts:", dict(sorted(statistics.items())))


if __name__ == "__main__":
    main()
