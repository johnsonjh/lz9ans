#!/usr/bin/env python3

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


class Symbols:
    def __init__(self, descriptor, kind, count, data, meaningful, of_alphabet=20):
        self.count = count
        self.used = 0
        self.mode = descriptor[0]
        self.bits = Bits(data, meaningful, self.mode == 3)
        is_literal = kind in (0, 5, 6)
        is_extra = kind == 4
        alphabet = (
            512
            if is_literal
            else 2 if is_extra else of_alphabet if kind in (3, 7, 8) else 32
        )
        check(self.mode in range(4), "invalid entropy mode")
        if count is not None:
            check((self.mode == 0) == (count == 0), "empty descriptor mismatch")
        if self.mode == 0:
            check(len(descriptor) == 1 and meaningful == 0, "bad empty stream")
        elif self.mode == 1:
            check(len(descriptor) == 2, "bad raw descriptor")
            self.width = descriptor[1]
            check(
                (
                    self.width in (7, 8, 9)
                    if is_literal
                    else self.width == 1 if is_extra else self.width == 5
                ),
                "bad raw width",
            )
        elif self.mode == 2:
            self.symbol, end = read_var(descriptor, 1)
            check(
                end == len(descriptor) and self.symbol < alphabet and meaningful == 0,
                "bad RLE descriptor",
            )
        else:
            position = 1
            nused, position = read_var(descriptor, position)
            check(2 <= nused <= alphabet, "bad normalized symbol count")
            freq = [0] * alphabet
            symbol = 0
            for _ in range(nused):
                gap, position = read_var(descriptor, position)
                value, position = read_var(descriptor, position)
                symbol += gap
                check(
                    symbol < alphabet and 1 <= value <= 1024,
                    "invalid normalized frequency",
                )
                freq[symbol] = value
                symbol += 1
            check(
                position == len(descriptor) and sum(freq) == 1024,
                "bad normalized frequency sum",
            )
            spread = [None] * 1024
            place = 0
            for symbol, frequency in enumerate(freq):
                for _ in range(frequency):
                    spread[place] = symbol
                    place = (place + 643) & 1023
            check(place == 0, "bad ANS table spread")
            seen = [0] * alphabet
            self.transition = []
            for symbol in spread:
                counter = freq[symbol] + seen[symbol]
                seen[symbol] += 1
                nbits = 10 - (counter.bit_length() - 1)
                baseline = (counter << nbits) - 1024
                self.transition.append((symbol, nbits, baseline))
            self.state = self.bits.get(10)

    def get(self):
        if self.count is not None:
            check(self.used < self.count, "too many entropy symbols")
        self.used += 1
        if self.mode == 1:
            return self.bits.get(self.width)
        if self.mode == 2:
            return self.symbol
        check(self.mode == 3, "read from empty entropy stream")
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
        if self.mode == 3:
            check(self.state == 0, "bad terminal ANS state")


class ExtraBits:
    def __init__(self, raw, stream=None):
        self.raw = raw
        self.stream = stream
        self.used = 0

    def get(self, count):
        if self.stream is None:
            self.used += count
            return self.raw.get(count)
        value = 0
        for _ in range(count):
            value = (value << 1) | self.stream.get()
            self.used += 1
        return value

    def finish(self):
        if self.stream is None:
            self.raw.finish()
        else:
            self.stream.finish(self.used)


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


def decode(data):
    check(len(data) >= 20 and data[:4] == b"LZ9A", "invalid frame magic")
    version, mode = data[4], data[5]
    check(
        version in (3, 4) and mode in (8, 9) and u16(data, 6) == 20,
        "unsupported frame header",
    )
    flags = u32(data, 12)
    check(zlib.crc32(data[:16]) == u32(data, 16), "corrupt frame header")
    check(
        (flags & ~15) == 0
        and ((version == 3 and flags == 0) or (version == 4 and flags != 0)),
        "invalid feature flags",
    )
    context_literals = bool(flags & 1)
    window = 65536 if flags & 2 else 32768
    distance_context = bool(flags & 4)
    extra_ans = bool(flags & 8)
    of_alphabet = 21 if window == 65536 else 20
    target = u32(data, 8)
    position = 20
    output = []
    reps = [0, 0, 0]
    stats = Counter()
    while len(output) < target:
        stream_count = 5 + int(context_literals) + int(distance_context)
        entropy_count = stream_count - 1
        descriptor_count = entropy_count + int(extra_ans)
        header_size = 28 + 4 * stream_count
        check(position + header_size <= len(data), "missing block header")
        h = data[position : position + header_size]
        position += header_size
        check(
            h[:4] == b"LZ9B" and h[4] == version and u16(h, 6) == header_size,
            "unsupported block header",
        )
        flag, width = h[5] & 3, h[5] >> 2
        n, sequence_count, literal_count = struct.unpack_from("<III", h, 8)
        desc_length = u32(h, 20)
        lengths = struct.unpack_from("<" + "I" * stream_count, h, 24)
        expected_crc = u32(h, 24 + 4 * stream_count)
        check(
            0 < n <= min(32768, target - len(output))
            and sequence_count <= n // 3
            and literal_count <= n
            and desc_length <= 8192
            and flag in (0, 1),
            "bad block dimensions",
        )
        sizes = [(length + 7) // 8 for length in lengths]
        check(
            sum(sizes) <= 81920 and all(length <= 81920 * 8 for length in lengths),
            "excessive bitstreams",
        )
        check(
            position + desc_length + sum(sizes) <= len(data), "truncated block payload"
        )
        desc = data[position : position + desc_length]
        position += desc_length
        raw = []
        for size, length in zip(sizes, lengths):
            raw.append(Bits(data[position : position + size], length))
            position += size
        block_start = len(output)
        if flag == 0:
            stats["stored"] += 1
            check(
                width in (7, 8, 9)
                and desc_length == 0
                and sequence_count == 0
                and literal_count == n
                and lengths == tuple([n * width] + [0] * (stream_count - 1)),
                "bad stored block",
            )
            for _ in range(n):
                value = raw[0].get(width)
                check(
                    value < (256 if mode == 8 else 512),
                    "stored symbol exceeds frame input width",
                )
                output.append(value)
            raw[0].finish()
        else:
            stats["lz"] += 1
            check(width == 0, "nonzero LZ block width")
            if context_literals and distance_context:
                kinds = (6, 5, 1, 2, 7, 8)
            elif context_literals:
                kinds = (6, 5, 1, 2, 3)
            elif distance_context:
                kinds = (0, 1, 2, 7, 8)
            else:
                kinds = (0, 1, 2, 3)
            if extra_ans:
                kinds = kinds + (4,)
            of_index = 3 + int(context_literals)
            split_indices = set()
            if context_literals:
                split_indices.update((0, 1))
            if distance_context:
                split_indices.update((of_index, of_index + 1))
            descriptors = []
            index = 0
            for _ in range(descriptor_count):
                check(index < len(desc), "truncated descriptor")
                start = index
                entropy_mode = desc[index]
                index += 1
                if entropy_mode == 1:
                    index += 1
                elif entropy_mode == 2:
                    _, index = read_var(desc, index)
                elif entropy_mode == 3:
                    pairs, index = read_var(desc, index)
                    for _ in range(pairs):
                        _, index = read_var(desc, index)
                        _, index = read_var(desc, index)
                check(index <= len(desc), "truncated entropy descriptor")
                descriptors.append(desc[start:index])
            check(index == len(desc), "extra entropy descriptor bytes")
            counts = [
                (
                    None
                    if i in split_indices or (extra_ans and i == entropy_count)
                    else (literal_count if kind == 0 else sequence_count)
                )
                for i, kind in enumerate(kinds)
            ]
            streams = []
            for i, kind in enumerate(kinds):
                b = raw[i]
                streams.append(
                    Symbols(
                        descriptors[i], kind, counts[i], b.data, b.length, of_alphabet
                    )
                )
            extra = ExtraBits(
                raw[stream_count - 1], streams[entropy_count] if extra_ans else None
            )
            used_literals = 0
            observed_counts = [0] * entropy_count

            def literal_context():
                previous = output[-1] if output else 0
                return int(bool(previous & (128 if mode == 8 else 256)))

            ll_index = 2 if context_literals else 1
            ml_index = ll_index + 1
            for _ in range(sequence_count):
                run = decode_length(streams[ll_index].get(), 0, extra)
                check(
                    used_literals + run <= literal_count
                    and len(output) - block_start + run <= n,
                    "invalid literal run",
                )
                for _ in range(run):
                    ctx = literal_context() if context_literals else 0
                    lit_wire = ctx if context_literals else 0
                    value = streams[lit_wire].get()
                    check(
                        value < (256 if mode == 8 else 512),
                        "literal exceeds frame input width",
                    )
                    output.append(value)
                    if lit_wire in split_indices:
                        observed_counts[lit_wire] += 1
                used_literals += run
                match_length = decode_length(streams[ml_index].get(), 3, extra)
                stats["match_lt19" if match_length < 19 else "match_ge19"] += 1
                if distance_context:
                    of_wire = of_index + int(match_length >= 19)
                else:
                    of_wire = of_index
                code = streams[of_wire].get()
                if of_wire in split_indices:
                    observed_counts[of_wire] += 1
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
                    distance = 32769 + extra.get(15)
                    check(distance <= window, "distance exceeds window")
                    promote(reps, distance)
                else:
                    check(4 <= code <= 18, "bad explicit offset code")
                    exponent = code - 4
                    distance = (1 << exponent) + extra.get(exponent)
                    promote(reps, distance)
                check(
                    1 <= distance <= window
                    and distance <= len(output)
                    and match_length >= 3
                    and len(output) - block_start + match_length <= n,
                    "invalid match length or distance",
                )
                for _ in range(match_length):
                    output.append(output[-distance])
            trailing = n - (len(output) - block_start)
            check(
                trailing == literal_count - used_literals,
                "incorrect terminal literal count",
            )
            for _ in range(trailing):
                ctx = literal_context() if context_literals else 0
                lit_wire = ctx if context_literals else 0
                value = streams[lit_wire].get()
                check(
                    value < (256 if mode == 8 else 512),
                    "terminal literal exceeds frame input width",
                )
                output.append(value)
                if lit_wire in split_indices:
                    observed_counts[lit_wire] += 1
            for i, stream in enumerate(streams[:entropy_count]):
                if i in split_indices:
                    stream.finish(observed_counts[i])
                else:
                    stream.finish()
            extra.finish()
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
