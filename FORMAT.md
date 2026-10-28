# LZ9ANS experimental stream format

## 1. Scope and terminology

LZ9ANS compresses a stream of **symbols** with values from **0 to 511**.
Every symbol occupies nine meaningful bits logically, regardless of how it
is represented in the compressor's working RAM. The Linux host tool supports:

- **Mode 8:** Ordinary binary file, with one logical symbol per 8-bit octet.
  This is a subset of the 9-bit alphabet, values 0–255.

- **Mode 9:** A packed MSB-first stream of consecutive 9-bit symbols. There
  are exactly eight symbols per nine octets, with a zero-padded final octet
  if the number of symbols is not a multiple of eight.

**Bit position 0** of any packed stream denotes the most significant bit of
its first octet. *Within the compressed format*, headers and descriptors are
always represented as **8-bit octets** and integer fields are little-endian.
Input symbol width and compressed header byte width are distinct concepts.

The format does **not** attach file names, timestamps, source-system types,
record boundaries, 36-bit word attributes, or Multics segment bit counts. It
currently preserves complete 9-bit symbols only, not an arbitrary final
1–8-bit fragment. Although the bit representation is intended to suit
`mxload`-style 9-in-8 transport, compatibility with a particular mxload
version or tape encoding is *not* asserted.

### 1.1 Constants

| Identifier           | Value  | Meaning                                           |
|---------------------:|:-------|:--------------------------------------------------|
| `BLOCK_N`            | 32,768 | Maximum logical output symbols in a block         |
| `WINDOW_N`           | 32,768 | Maximum backward match distance, **in symbols**   |
| `MIN_MATCH`          | 3      | Minimum matched symbols                           |
| `MAX_MATCH`          | 32,768 | Maximum matched symbols in one sequence           |
| `TABLE_LOG`          | 10     | Base-two log of tANS state count                  |
| `TABLE_N`            | 1,024  | tANS states, numbered 0–1023                      |
| `STREAM_COUNT`       | 5      | LIT, LL, ML, OF, EXTRA                            |
| `MAX_DESC_OCTETS`    | 8,192  | Maximum total entropy descriptors in a block      |
| `MAX_PAYLOAD_OCTETS` | 81,920 | Maximum combined compressed substreams in a block |

The **block limit** and **window limit** are both 32,768 symbols in v3. A
match may refer to the entire immediately preceding block, and may also refer
backward within the current block. The history at the start of a frame is
**empty**, not populated with synthetic zeros.

## 2. 9-bit bit packing and checksums

For each symbol `s`, emit its nine bits from bit 8 (most significant) to
bit 0. Concatenate the bit strings without gaps and divide into successive
8-bit octets, most significant bit first. At end of the logical stream, write
zero bits to finish the last octet if necessary.

The first eight values of `vectors/mixed9.input` are:

```text
values:      0, 127, 255, 256, 511, 1, 320, 17
packed hex:  00 1f df f0 0f f8 06 80 11
```

The reverse process reads nine MSB-first bits at a time. For the compressed
frame in mode 9, a final partly used octet MUST have zero unused bits.  If `N`
is the symbol count, packed mode-9 output occupies `ceil(9*N/8)` octets;
mode-8 output occupies `N` octets.

The frame header's CRC covers **its first 16 octets**. Each block CRC covers
its **decoded logical symbols re-packed as a fresh 9-bit MSB-first stream**,
zero-padded at the *end of that block*, regardless of the frame's mode 8 or
9. Therefore a mode-8 file's block CRC is **not** the CRC of its ordinary
input octets. When mode-9 blocks share a frame, the final octet padding is
applied anew for each block CRC, but the actual frame output packing carries
unused bits across block boundaries without introducing gaps.

CRC algorithm: CRC-32/ISO-HDLC (reflected polynomial `0xEDB88320`, initial
state `0xFFFFFFFF`, process bits least-significant-first **within each already
constructed octet**, final XOR `0xFFFFFFFF`). This equals the usual
`zlib.crc32()` over the prescribed octets.

## 3. Frame and block headers

Every frame begins with one **20-octet header**, followed by enough blocks
to produce exactly its stated number of symbols. The frame has no trailer.
For a zero-symbol frame there are no blocks. All header widths are unsigned
and little-endian except the ASCII magics and single-byte flags.

### 3.1 Frame header (20 octets)

| Offset | Octets | Required value / interpretation                   |
|-------:|:-------|:--------------------------------------------------|
| 0      | 4      | ASCII `LZ9A` (`4c 5a 39 41`)                      |
| 4      | 1      | Version **3**                                     |
| 5      | 1      | Output representation **8** or **9**              |
| 6      | 2      | Header length **20** (`0x0014`)                   |
| 8      | 4      | Total logical symbol count, `N`, 0–4,294,967,295  |
| 12     | 4      | Reserved count-high field; **MUST be zero** in v3 |
| 16     | 4      | CRC-32/ISO-HDLC of octets 0–15                    |

The Linux CLI uses seekable input and may impose a smaller file size limit
through `ftell`/`long`; that is a host implementation limit, not an additional
wire-format restriction.

### 3.2 Block header (48 octets)

| Offset | Octets | Interpretation                                                                     |
|-------:|:-------|:-----------------------------------------------------------------------------------|
| 0      | 4      | ASCII `LZ9B` (`4c 5a 39 42`)                                                       |
| 4      | 1      | Version **3**                                                                      |
| 5      | 1      | Bits 0–1: type 0=STORED, 1=LZ. Bits 2–7: symbol width (7/8/9 for STORED, 0 for LZ) |
| 6      | 2      | Header length **48** (`0x0030`)                                                    |
| 8      | 4      | `n`: decoded symbols in this block, 1–32,768                                       |
| 12     | 4      | `q`: number of LZ match sequences, 0–`floor(n/3)`                                  |
| 16     | 4      | `lc`: total number of literal symbols, at most `n`                                 |
| 20     | 4      | `dl`: combined length of four entropy descriptors, in octets                       |
| 24     | 4      | Meaningful bits in LIT stream                                                      |
| 28     | 4      | Meaningful bits in LL stream                                                       |
| 32     | 4      | Meaningful bits in ML stream                                                       |
| 36     | 4      | Meaningful bits in OF stream                                                       |
| 40     | 4      | Meaningful bits in EXTRA stream                                                    |
| 44     | 4      | CRC-32/ISO-HDLC of this block's decoded canonical packed-9 symbols                 |

The values in the five bit-length fields do **not** include per-stream
byte-alignment padding. The physical length of each is `ceil(bits/8)`.

The five payloads are concatenated in header order, *each* beginning on a new
octet boundary. Any unused low bits in the last octet of a payload MUST be
zero. A zero-length stream contributes no octets.

The number of blocks is not stored. Continue until the accumulated `n` equals
frame `N`. Neither an incomplete frame nor trailing octets are valid.  A block
MUST NOT claim more symbols than remain in the frame.

### 3.3 STORED blocks

`type=0`, `width=7`, `8`, or `9`; `dl=0`; `q=0`; `lc=n`; and only LIT
bit-length is nonzero, equal to `n * width`. All symbols are packed MSB-first
at the specified width. A width-7 block contains values 0–127, width-8 values
0–255, and width-9 values 0–511. Mode-8 frames MUST NOT output values above
255. STORED blocks leave the LZ history **intact** and do **not** change the
three repeat-distance slots. They are *not* reset points for the dictionary.

The encoder chooses the narrowest width that can represent all block symbols.
The decoder accepts any permitted width with valid symbol values.

### 3.4 LZ blocks

`type=1`, `width=0`. Immediately after the 48-octet block header:

1. Four entropy descriptors, exactly `dl` octets in total, in the order
   **LIT**, **LL**, **ML**, **OF**.

2. The four encoded entropy bitstreams in that same order.

3. The fifth, forward-oriented **EXTRA** bitstream.

The count of LIT symbols is `lc`. Each of LL, ML, and OF has exactly `q`
symbols. EXTRA has no standalone symbols or entropy descriptor; it holds only
the additional bits required by length and explicit-distance codes.

When `q=0`, `lc` MUST equal `n`, and the three sequence streams MUST be EMPTY;
an LZ block containing only entropy-coded literals is legal.

## 4. Decoded sequence semantics

Each of the `q` sequences consists of one **literal-run length** LL, one
**match length** ML, and one **offset** OF. These are logically synchronous,
even though their codes occupy separate entropy streams.

For each sequence in forward order:

1. Decode LL and emit exactly LL symbols from LIT.

2. Decode ML; it MUST be at least three.

3. Decode OF to a backward **distance** `d` between 1 and 32,768.

4. Copy ML symbols from `d` logical positions behind the current output
   cursor, one symbol at a time, allowing source and destination to overlap.

Once all `q` sequences are emitted, the decoder emits the remaining
`lc - sum(LL)` literal symbols, sometimes called the *terminal literal run*.
The output symbol count MUST be exactly `n`. No separate end-of-block or
literal-vs-match flag exists. The sequence count supplies framing.

The dictionary includes preceding decoded blocks in the **same frame**.
Distances MUST NOT reach before the start of the frame (there is no preset
dictionary), but may reach across a block boundary. No match can reference
future symbols.

### 4.1 Literal-run and match-length codes

LL and ML each use an alphabet of **32** codes numbered 0–31. The same
conversion uses `bias=0` for LL and `bias=3` for ML. Given nonnegative
`t = length - bias`:

```text
If 0 <= t <= 15:
    code = t
    no extra bits
Otherwise:
    k     = floor(log2(t - 15))
    code  = 16 + k
    extra = t - 15 - 2^k
    write extra in exactly k bits, MSB-first
```

Decoder equation for a `code >= 16`:

```text
k      = code - 16
length = bias + 15 + 2^k + read_extra(k)
```

For example, LL=0 maps to code 0, ML=3 maps to code 0, and LL=16 maps to code
16 with zero additional bits (`k=0`). Codes permit larger values than can fit
into one block; decoders MUST reject any decoded length that exceeds the
remaining block capacity. ML values larger than 32,768 cannot occur in a valid
v3 encoder output.

### 4.2 Offset alphabet, repeat slots, and RUN

OF has **20** codes, 0–19, with these exact meanings:

| OF code | Meaning                                             | EXTRA bits  | Repeat-slot action                |
|--------:|:----------------------------------------------------|:------------|:----------------------------------|
| 0       | Distance currently in slot `R0`                     | 0           | Promote that distance to `R0`     |
| 1       | Distance currently in slot `R1`                     | 0           | Promote that distance to `R0`     |
| 2       | Distance currently in slot `R2`                     | 0           | Promote that distance to `R0`     |
| 3       | **RUN:** distance exactly 1                         | 0           | **Do not modify** repeat slots    |
| 4–18    | Explicit distance bucket `k=code-4`, `0 <= k <= 14` | Exactly `k` | Promote explicit distance to `R0` |
| 19      | Explicit distance exactly **32,768**                | 0           | Promote 32,768 to `R0`            |

Initial slots at the beginning of each frame: **`R0=R1=R2=INVALID`**.  Code
0/1/2 is invalid unless its referenced slot has been initialized by a prior
explicit or repeat match. Repeat slots persist across **both** LZ and STORED
block boundaries, but not across frames. Code 3 works whenever there is at
least one previously decoded symbol; it is not dependent on the repeat slots.

For explicit code `c` in 4–18, `k=c-4`:

```text
distance = 2^k + read_extra(k)
```

Thus c=4 represents distance 1 with zero bits, c=5 represents 2–3, c=6
represents 4–7, c=17 represents 8,192–16,383, and c=18 represents
16,384–32,767. Code 19 is the exact 32,768 endpoint. An explicit distance1
(code4) is permitted and *does* update the repeat slots; RUN (code3) expresses
the same distance without updating them. This semantic distinction
is intentional.

**MRU promotion** takes a selected distance `d`, removes its previous
occurrence if present in any slot, shifts newer slots right, inserts `d` as
`R0`, and discards the former oldest slot if needed. No valid slot should be
duplicated.  Concretely:

```text
Initially:          [INVALID, INVALID, INVALID]
explicit distance5: [5,       INVALID, INVALID]
explicit distance8: [8,       5,       INVALID]
explicit distance3: [3,       8,       5      ]
repeat code2:       [5,       3,       8      ] (returned distance5)
RUN code3:          [5,       3,       8      ] (returned distance1; NO promotion)
```

Unlike Zstandard, **there is no special mapping or shift when LL=0**.  Repeat
code0 always means slot R0, including following a zero-length literal run. An
encoder and decoder must implement this same rule.

### 4.3 EXTRA bit ordering

For each sequence in forward order, append the following *to the one shared
EXTRA stream*, with no inter-field padding:

1. LL additional bits, if the LL code requires any.

2. ML additional bits, if the ML code requires any.

3. OF additional bits, if an explicit distance bucket requires any.

The bit groups are MSB-first. No EXTRA bits are emitted for RUN or repeat
codes. Literal payloads never contribute to EXTRA.

## 5. Entropy streams and descriptors

The alphabet sizes are **512 LIT**, **32 LL**, **32 ML**, and **20 OF**.  Each
stream independently chooses one of four entropy modes. The chosen mode is
specified by its *descriptor*, not inferred from the data.

Descriptors are concatenated and parsed without alignment gaps:

| Mode byte | Name  | Rest of descriptor                                          | Payload                                           |
|----------:|:------|:------------------------------------------------------------|:--------------------------------------------------|
| 0         | EMPTY | Nothing                                                     | Zero bits; allowed only when symbol count is zero |
| 1         | RAW   | One octet: symbol width                                     | Forward MSB-first fixed-width codes               |
| 2         | RLE   | One unsigned varint: sole symbol value                      | Zero bits, repeat symbol for entire count         |
| 3         | tANS  | One varint `nused`, then `nused` `(gap, freq)` varint pairs | Reverse-consumed tANS bit groups                  |

RAW LIT accepts width 7/8/9; RAW LL, ML, OF always use width **5**.  Five bits
suffice to encode all 20 OF codes (unused code values 20–31 are invalid). The
decoder checks that all symbols lie within the required alphabet, that mode
EMPTY matches a zero count and no payload, and that all meaningful payload
bits are consumed.

The varint is an **unsigned little-endian base-128** integer. Each octet
contributes its low seven bits; bit7 means continue. The v3 decoder permits at
most three octets per integer. Values are further bounded by the field's
semantic range. Encoders normally emit the shortest representation.

### 5.1 tANS descriptor and normalization

`nused` MUST be between 2 and the alphabet size. The descriptor contains
ascending nonzero-frequency symbols. Initially `previous_plus_one=0`; for
each pair:

```text
symbol = previous_plus_one + gap
frequency[symbol] = freq
previous_plus_one = symbol + 1
```

Every `freq` MUST be positive and no greater than 1024. All normalized
frequencies together MUST sum to **1024**. All unmentioned symbols have
frequency zero and may not occur in that stream. The exact frequency
normalization algorithm used by the encoder is **not normative**; all decoders
use the serialized normalized frequencies.

### 5.2 Deterministic 1024-state table construction

Given the normalized frequencies, construct `spread[0..1023]` as follows:

```text
place = 0
for symbol in ascending numerical order:
    repeat freq[symbol] times:
        spread[place] = symbol
        place = (place + 643) & 1023
```

There is no low-probability `-1` special case, FSE reserved tail, or other
hidden state permutation. Because 643 is odd, it visits every one of the 1024
states before repeating. At completion `place` is zero.

Then, scanning state positions `x=0..1023` in order, track `seen[s]` as the
number of preceding positions assigned to symbol `s`:

```text
s               = spread[x]
counter         = freq[s] + seen[s]
seen[s]++
nbits           = 10 - floor(log2(counter))
base            = (counter << nbits) - 1024

decode_table[x] = (symbol=s, nbits=nbits, base=base)
```

The invariant is `0 <= base` and `base + (1<<nbits) <= 1024`.

### 5.3 tANS decoding and the unusual reverse bit direction

For each tANS stream, the last **10 meaningful bits** of its stream represent
its initial decoder state, numbered 0–1023. The decoder then retrieves
*groups* of the specified length in reverse group order:

```text
state = read_last_10_meaningful_bits()
for each of the expected decoded symbols, in FORWARD symbol order:
    (symbol, nbits, base) = decode_table[state]
    value = read_preceding_group(nbits)
    state = base + value
    emit symbol
require state == 0
require exactly zero remaining meaningful bits
```

`read_preceding_group(n)` means: step backward by `n` bits from the current
reverse cursor, then interpret that *whole group MSB-first*. **Do not reverse
the bit order within a group.** For example if the original forward-written
groups were `101`, `01`, `1110`, final decoder retrieves `1110`, `01`, `101`,
**not** `0111`, `10`, `101`.

The encoder constructs the inverse transition starting at state zero,
processing the stream's symbols in **reverse** symbol order. For a symbol with
frequency `f`, let `y=1024+encoder_state`. Compute:

```text
nb = 10 - floor(log2(f))
if y < (f << nb):
    nb = nb - 1
counter = y >> nb
write (y & ((1<<nb)-1)) as an nb-bit MSB-first group
encoder_state = inverse_table[cumulative[symbol] + counter - f]
```

Here `cumulative[s]` is the sum of frequencies for symbols below `s`;
`inverse_table[cumulative[s]+seen_index]` equals the state position of the
`(seen_index+1)`th occurrence of that symbol when scanning `spread` in
state-index order. Finally append `encoder_state` as exactly ten bits.  Each
tANS stream is independent and resets its state to zero at block boundaries.
The only entropy state carried between blocks is the **LZ history and repeat
distances**, **not** the tANS model.

The current C implementation can be used to test all table equations;
`inspect_lz9ans.py` deliberately reconstructs the same tables without calling
any part of the C implementation.

## 6. Decoder procedure and validation requirements

A conforming v3 decoder SHOULD implement the following stages, enforcing all
checks before trusting an offset, size, state, or output count:

1. Read and validate the 20-octet frame header, including CRC, version,
   representation, header length, and reserved field.

2. Initialize an empty 32,768-symbol history and three INVALID repeat slots.

3. For each block, validate magic/version/header length/type/width, counts,
   length limits, and the announced size against the remaining frame.

4. Bound `dl` by 8192; bound the sum of the five rounded payload sizes by
   81920; read *exactly* the announced descriptor and payload octets.

5. Check the unused low-order pad bits of **every** substream, including
   stored data.

6. For STORED, read `n` fixed-width symbols, updating history but not MRU.

7. For LZ, reconstruct all four descriptors, validate the frequencies,
   set each stream's expected count, initialize entropy decoding states,
   and decode sequences plus terminal literal run.

8. For every copy, validate `1 <= distance <= 32768`, distance does not
   exceed the already reconstructed frame prefix, `ML >= 3`, and the copy
   does not exceed this block's remaining output capacity. Copy forwards
   one symbol at a time to support overlaps.

9. Verify each entropy stream consumes exactly its meaningful bits and its
   prescribed symbol count; tANS must finish in state zero; EXTRA must be
   exhausted. Verify the actual literal and block symbol counts.

10. Compute the canonical packed-9 block CRC; reject a mismatch.

11. At the frame symbol count, flush the mode-9 terminal partial octet
    with zero fill; reject trailing compressed octets.

Never allocate memory directly from an unchecked on-disk length. Streaming
implementations can reject early; the Python independent decoder prioritizes
clarity over bounded memory and should not be used as a hardened service.

The current host CLI may leave a partially written output file when a
subsequent block is corrupt. Its caller must use the exit status and should
write to a temporary name if destination atomicity is required.

## 7. Encoder reference choices (informative, not required for decoding)

LZ9ANS v3 uses an **8,192-entry hash table** keyed from three consecutive
symbols and a **32,768-position** hash-chain ring to locate match candidates.
The parser maintains a 32K-symbol block plus 32K history in 16-bit C
containers, records `(LL, ML, distance)` sequences, and emits four entropy
streams and the shared EXTRA bitstream.  The distance is stored in a 32-bit C
field so the encoder can carry a 15-bit distance plus the repeat-slot tag;
this is an implementation detail, not a wire field.

The levels from `-1` through `-9` all produce **the same v3 wire format**:

| Level | Max hash-chain candidates | Parser behavior                                    |
|------:|:--------------------------|:---------------------------------------------------|
| 1     | 1                         | Greedy, no entropy-price pass                      |
| 2     | 2                         | Greedy, no entropy-price pass                      |
| 3     | 4                         | Greedy, no entropy-price pass                      |
| 4     | 8                         | Initial greedy pass; entropy-price reparse         |
| 5     | 12                        | Level4 plus one-position lazy choice               |
| 6     | 16                        | Level5, deeper candidate search; default           |
| 7     | 24                        | Same parser, greater search depth                  |
| 8     | 32                        | Adds near-longest alternative match trials         |
| 9     | 48                        | Adds a **third parsing pass** with refreshed costs |

The cost estimator produces per-symbol prices in fixed-point **Q8 bits** (256
cost units per bit). It counts codes in a preliminary parse, normalizes their
tANS frequencies to 1024, estimates negative log probability plus an amortized
descriptor cost, and compares tANS with RAW and RLE. It adds exact extra-bit
counts for the chosen LL/ML/OF codes and estimates a fixed per-sequence
overhead. Literal sums are accelerated with a cumulative prefix every 16
symbols. It tests a bounded number of match candidates; levels 8–9 consider
slightly shorter alternatives when costed. When distance 1 could use either
RUN or an initialized repeat slot, the cost-aware parser may choose the lower
estimated offset-code cost.

This is **bounded heuristic optimization**, not exact optimal parsing.  Prices
come from one or two block-level frequency snapshots; the final entropy
distribution may differ after reparse. Compression size is not mathematically
guaranteed to improve monotonically with increasing level.

For each symbol stream the encoder chooses EMPTY, RLE, RAW, or tANS by an
estimated actual serialized size. If the *whole* LZ payload (including
descriptors) is no smaller than storing the block at its narrowest 7/8/9 bit
width, it writes STORED instead and restores any speculative MRU changes made
during parsing. The decoder never needs to know which parser or level was used.

Linux reference explicit state on one x86-64/8-bit-octet build:
**497,714 octets encoder state** and **207,248 octets decoder state** as
reported by `./lz9ans --memory` (plus stack, C library file buffers, allocator
overhead). The accounted encoder state is 497,714 bytes, below 512 KiB
(524,288 bytes) by 26,574 bytes. The figure includes the `Encoder` structure
and its associated arrays, but excludes small helper stack temporaries, C
runtime state, `FILE` buffers, allocator overhead, and the process itself. It
is therefore a useful state budget, not a guarantee of total resident memory.
It is **not** a proof of native Multics segment occupancy: ANSI C89's
8-bit-octet host assumptions and Multics PL/I word representation are
different and must be sized separately.

## 8. Golden vectors and interoperability

The repository's `vectors/` directory contains **exact compressed v3
bitstreams** and their input files.  The first five are short format examples;
the two long-distance fixtures are larger and specifically exercise v3
distance limits. None is intended as a general compression benchmark.

| Fixture       | Input mode | Level | Raw octets | Frame octets  | SHA-256 of compressed frame                                        |
|--------------:|:-----------|:------|:-----------|:--------------|:-------------------------------------------------------------------|
| `empty8`      | 8          | 1     | 0          | 20            | `3d5a4c6bfb436c5d37bf598f67176678c0b791823da1a4f82e337e72c9f2e117` |
| `abc8`        | 8          | 1     | 12         | 79            | `0f9e26d6f5e14c382bcda0c74e30ac064be58e5c89fcb0e0ff77e0fd1892d563` |
| `run8`        | 8          | 6     | 96         | 77            | `bd1c000a5da8bc944ff97da8442eafe7261459fcb1a117184bf81ac47433921c` |
| `mixed9`      | 9          | 6     | 216        | 87            | `b1b33489ebb293371ddb6e2d4ade65de43be81b2e35933f310a5674c83e5ecd9` |
| `switch8`     | 8          | 6     | 1,152      | 98            | `78557387e91d9ff33db2f97c804dc327ff3450b506cd8d42d37c53dd59cae669` |
| `longdist16k` | 8          | 9     | 32,768     | 16,465        | `83e3f89be3579929a981fc3c222e301e442a8f56ae17170791cd641167b3b909` |
| `longdist32k` | 8          | 9     | 65,536     | 32,893        | `832d58995ae2092f2099f6b2e2f16c63d4acf1f084c4fd8f8bf9938074dc3226` |

The empty-mode-8 frame is exactly these **20 octets**:

```text
4c 5a 39 41 03 08 14 00 00 00 00 00 00 00 00 00 be 16 f2 0a
```

The `run8` frame uses one LZ sequence and **OF code 3** (RUN). The `mixed9`
fixture exercises actual values over 255. The `longdist16k` and `longdist32k`
fixtures exercise explicit offset codes 18 and 19, respectively; `longdist32k`
references the full preceding stored block. Bigger real ELFs in the regression
suite exercise all three MRU codes and literal, length, and offset
tANS streams.

Independent implementations should decode the published **bytes**, not just
their own re-encodings, and should reject the negative cases above. For
interoperability the actual uncompressed content, packed9 bit order, block
checksums, MRU state across block types, and tANS state termination all have
to match exactly.

## 9. Format gaps and future design choices

- **Native Multics fidelity:** Obtain representative native PL/I source,
  object code, directory information, and exact segment bit counts.
  Decide whether trailing 1–8 meaningful bits require a new frame feature.

- **Format stability:** V3 is the default baseline; the optional v4 extensions
  remain experimental and need broader fuzzing, cross-language validation, and
  explicit freezing before archival use.

- **Compression/speed:** The historical v0.3 tables are baseline measurements.
  The appended experiment results use this v4-capable source and are specific
  to the measured local files; they do not establish universal superiority.

- **Container semantics:** Archive method numbers, independent blocks,
  indexing, streaming inputs of unknown length, and concatenated frames
  are outside this experimental format.

- **Authenticity:** CRC can detect accidental damage but is **not** a
  cryptographic integrity or authenticity mechanism.

- **Portability:** This specific Linux reference expects 8-bit C `char`,
  16-bit `short`, 32-bit `int` and seekable files. A Multics PL/I port
  should carry the *wire equations* and use 36-bit-native representations,
  not emulate those host C types unnecessarily.

## 10. Experimental version 4 extensions

V4 is an opt-in extension of the v3 frame and block grammar. It is not frozen.
The reference encoder emits v4 only when at least one feature below is enabled;
with no options, it emits byte-identical v3 output. V4 decoders must reject
unknown feature bits and inconsistent version/flag combinations.

### 10.1 Frame version and feature flags

The 20-octet frame header retains the v3 layout. Octet 4 is version **4** and
the 32-bit little-endian field at offset 12 is a feature bitmask:

| Bit | Mask         | Meaning                                 |
|----:|:-------------|:----------------------------------------|
| 0   | `0x00000001` | Context-conditioned literal streams     |
| 1   | `0x00000002` | 65,536-symbol history window            |
| 2   | `0x00000004` | Match-length-conditioned offset streams |
| 3   | `0x00000008` | Entropy-coded EXTRA bitstream           |

A v3 frame MUST have version 3 and flags zero. A v4 frame MUST have version 4
and at least one known flag set. Bits outside `0x0f` are invalid. The features
apply to every block in the frame; features cannot be changed block by block.
The 32,768-symbol block maximum is unchanged.

### 10.2 V4 block-header size and stream layout

The number of payload streams is `5 + context_flag + distance_context_flag`.
EXTRA is always the last payload stream. The block header length is
`28 + 4 * payload_stream_count` octets; the CRC follows the stream bit lengths.
Consequently, the header is 48 octets with neither split-table feature, 52
when exactly one is enabled, and 56 when both are enabled. The 64K-window and
EXTRA-ANS flags do not add payload streams and do not affect header length.

The bit-length fields at offset 24 are in the following order:

| Literal context | Distance context | Payload order                                  |
|----------------:|:-----------------|:-----------------------------------------------|
| Off             | Off              | `LIT, LL, ML, OF, EXTRA`                       |
| On              | Off              | `LIT0, LIT1, LL, ML, OF, EXTRA`                |
| Off             | On               | `LIT, LL, ML, OF-short, OF-long, EXTRA`        |
| On              | On               | `LIT0, LIT1, LL, ML, OF-short, OF-long, EXTRA` |

For every row, the final field is EXTRA. The block CRC offset is
`24 + 4 * payload_stream_count`. Payload byte alignment and zero padding are
otherwise identical to v3. A STORED block uses only the first payload stream;
all remaining stream bit lengths and `dl` MUST be zero, even when v4 features
are enabled for the frame.

The `dl` field remains the length of the concatenated descriptors. LZ blocks
have one descriptor for each LIT/LL/ML/OF entropy stream. If EXTRA-ANS is
enabled, one additional descriptor for the EXTRA stream follows those normal
stream descriptors. The EXTRA payload already has a bit-length field in v3;
there is no extra payload field for EXTRA-ANS.

### 10.3 Literal context (`0x01`)

The previous decoded output symbol selects the literal stream using the most
significant bit of the active symbol width:

- Mode 8: context 0 when bit 7 is zero, context 1 when bit 7 is one.
- Mode 9: context 0 when bit 8 is zero, context 1 when bit 8 is one.

At the beginning of a frame, before any output symbol exists, the context is
zero. The context follows the actual decoded output, including symbols copied
by matches and symbols from previous blocks. Each literal is coded by `LIT0`
or `LIT1` according to the context immediately before that literal is emitted.
The `lc` header field remains the total literal count; per-context counts are
not stored. A decoder counts each context while producing output and validates
that an EMPTY descriptor is used if and only if that context's literal count
is zero. Other streams retain their v3 meaning and counts.

### 10.4 64K history window (`0x02`)

The history window increases to **65,536 logical symbols**; blocks remain at
32,768 symbols and the maximum match length remains 32,768. Cross-block
references can now reach 65,536 symbols. The OF alphabet grows from 20 to 21
symbols; the repeat slots and RUN semantics are unchanged.

For a 64K-window frame, offset codes 0–19 retain their v3 meanings, with code
19 representing exactly 32,768. Code 20 represents distances 32,769 through
65,536 and carries exactly 15 EXTRA bits:

```text
distance = 32769 + read_extra(15)
```

The encoder writes `distance - 32769` in 15 MSB-first bits. This extended
range includes the exact 65,536 endpoint. Code 20 is invalid unless the 64K
window flag is set. For a 32K window the OF alphabet remains 20 and code 20 is
invalid. An implementation must use at least 17 bits for the logical distance;
16-bit storage is insufficient for the 65,536 endpoint.

### 10.5 Match-length-conditioned offset tables (`0x04`)

The encoder maintains two separate OF entropy streams, but retains one logical
offset code alphabet and one shared EXTRA stream. After decoding ML, the
sequence selects its OF stream as follows:

- `OF-short` when `ML < 19`;
- `OF-long` when `ML >= 19`.

The threshold aligns with the first ML code that requires additional length
bits. The OF symbol, repeat-slot update, RUN semantics, and distance EXTRA bits
are otherwise unchanged. The total sequence count `q` is unchanged; per-context
OF counts are not stored. The decoder counts matches in each class and checks
that the corresponding stream is EMPTY exactly when its count is zero.

This feature is an experiment, not a recommended default. The measurements in
`RESULTS.md` show that splitting offset tables can increase output size because
its per-block descriptor and table overhead is not always repaid by a better
symbol distribution.

### 10.6 Binary tANS EXTRA stream (`0x08`)

With EXTRA-ANS enabled, EXTRA still represents the same sequence of bits in the
same logical order: LL extra bits, ML extra bits, then OF extra bits for each
sequence in forward order. Instead of writing those bits as an un-described raw
bitstream, the encoder selects one of the existing entropy modes using a
2-symbol alphabet (`0` and `1`):

- EMPTY if the block has no EXTRA bits;
- RLE if all EXTRA bits are the same value;
- tANS with a normalized 1,024-state binary model when both values occur and
  the encoded payload plus descriptor is smaller than RAW;
- otherwise RAW with width 1.

The descriptor uses the same EMPTY/RAW/RLE/tANS syntax as other entropy
streams. The EXTRA bit count is not stored explicitly; the decoder derives it
while decoding LL, ML, and OF codes, counts each consumed bit, and validates
exact stream consumption and terminal tANS state. In tANS mode the encoder
feeds bits in reverse sequence/component/bit order so the decoder reconstructs
the original forward EXTRA bit order. If the tANS representation does not
beat the RAW representation after descriptor cost, the encoder chooses RAW.

### 10.7 Feature combinations and compatibility

Any subset of the four v4 flags may be combined. In particular, a frame may
combine a 64K window with context literals, distance-context tables, and
EXTRA-ANS. The frame flags determine the block stream layout, descriptor count,
OF alphabet, and distance limit. A decoder must not infer features from block
header length alone; it must validate the frame flags and then require each
block's exact expected header size. V4 remains experimental until a PL/I
implementation passes the same independent-decoder, malformed-stream, and
cross-language tests as the C and Python implementations.
