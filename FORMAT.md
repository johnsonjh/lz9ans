<!-- Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com> -->
<!-- Copyright (c) 2025-2026 The DPS8M Development Team -->
<!-- SPDX-License-Identifier: MIT -->
# LZ9ANS version 6 stream format

<!-- toc -->

- [1. Scope and terminology](#1-scope-and-terminology)
  * [1.1 Constants](#11-constants)
- [2. 9-bit bit packing and checksums](#2-9-bit-bit-packing-and-checksums)
- [3. Frame and block headers](#3-frame-and-block-headers)
  * [3.1 Frame header (20 octets)](#31-frame-header-20-octets)
  * [3.2 Block header (48 octets)](#32-block-header-48-octets)
  * [3.3 STORED blocks](#33-stored-blocks)
  * [3.4 LZ blocks](#34-lz-blocks)
- [4. Decoded sequence semantics](#4-decoded-sequence-semantics)
  * [4.1 Literal-run and match-length codes](#41-literal-run-and-match-length-codes)
  * [4.2 Offset alphabet, repeat slots, and RUN](#42-offset-alphabet-repeat-slots-and-run)
  * [4.3 EXTRA bit ordering](#43-extra-bit-ordering)
- [5. Entropy streams and descriptors](#5-entropy-streams-and-descriptors)
  * [5.1 Compact tANS descriptors, table reuse, and normalization](#51-compact-tans-descriptors-table-reuse-and-normalization)
  * [5.2 Deterministic 1024-state table construction](#52-deterministic-1024-state-table-construction)
  * [5.3 tANS decoding and the unusual reverse bit direction](#53-tans-decoding-and-the-unusual-reverse-bit-direction)
- [6. Decoder procedure and validation requirements](#6-decoder-procedure-and-validation-requirements)
- [7. Encoder reference choices (informative, not required for decoding)](#7-encoder-reference-choices-informative-not-required-for-decoding)
- [8. Golden vectors and interoperability](#8-golden-vectors-and-interoperability)
- [9. Format gaps and future design choices](#9-format-gaps-and-future-design-choices)
- [10. Version 6 feature flags and fixed stream layout](#10-version-6-feature-flags-and-fixed-stream-layout)
  * [10.1 Fixed block-header size and stream layout](#101-fixed-block-header-size-and-stream-layout)
  * [10.2 64K history window (`0x01`)](#102-64k-history-window-0x01)
  * [10.3 Binary tANS EXTRA stream (`0x02`)](#103-binary-tans-extra-stream-0x02)
  * [10.4 Aligned explicit distances (`0x08`)](#104-aligned-explicit-distances-0x08)
  * [10.5 Per-block aligned-distance selection (`0x18`)](#105-per-block-aligned-distance-selection-0x18)

<!-- tocstop -->

## 1. Scope and terminology

LZ9ANS compresses a stream of **symbols** with values from **0 to 511**.
Every symbol occupies nine meaningful bits logically, regardless of how it
is represented in the compressor's working RAM.  The UNIX host tool supports:

- **Mode 8:** "Ordinary" binary file, with one logical symbol per 8‑bit octet.
  This is a subset of the support 9‑bit alphabet, values 0–255.

- **Mode 9:** A packed MSB‑first stream of consecutive 9‑bit symbols.  There
  are exactly eight symbols per nine octets, with a zero‑padded final octet
  if the number of symbols is not a multiple of eight.

**Bit position 0** of any packed stream denotes the most significant bit of
its first octet.  *Within the compressed format*, headers and descriptors are
always represented as **8‑bit octets** and integer fields are little‑endian.
Input symbol width and compressed header byte width are distinct concepts.

The format does **not** attach file names, timestamps, source‑system types,
record boundaries, 36‑bit word attributes, or Multics segment bit counts.  It
currently preserves complete 9‑bit symbols only, not an arbitrary final one
8‑bit fragment.  Although the bit representation is intended to suit
`mxload`‑style 9‑in‑8 transport, compatibility with a particular mxload
version or tape encoding is *not* asserted.

### 1.1 Constants

| Identifier           | Value  | Meaning                                              |
|---------------------:|:-------|:-----------------------------------------------------|
| `BLOCK_N`            | 32,768 | Maximum logical output symbols in a block            |
| `WINDOW_DEFAULT`     | 32,768 | Default maximum backward match distance, in symbols  |
| `WINDOW_64K`         | 65,536 | Optional maximum backward match distance, in symbols |
| `MIN_MATCH`          | 3      | Minimum matched symbols                              |
| `MAX_MATCH`          | 32,768 | Maximum matched symbols in one sequence              |
| `TABLE_LOG`          | 10     | Base‑two log of tANS state count                     |
| `TABLE_N`            | 1,024  | tANS states, numbered 0–1023                         |
| `STREAM_COUNT`       | 5      | LIT, LL, ML, OF, EXTRA                               |
| `MAX_DESC_OCTETS`    | 8,192  | Maximum total entropy descriptors in a block         |
| `MAX_PAYLOAD_OCTETS` | 81,920 | Maximum combined compressed substreams in a block    |

The block limit is 32,768 symbols in v6.  The default window is 32,768 symbols;
frame flag bit 0 enables a 65,536‑symbol window.  The reference encoder selects
aligned‑distance coding automatically for each LZ block when the block contains
at least one ninth‑bit symbol and the aligned representation reduces the raw
EXTRA bit count.  A match may refer to the immediately preceding history and
may refer backward within the current block.
The history at the start of a frame is **empty**, not populated with synthetic
zeros.

## 2. 9-bit bit packing and checksums

For each symbol `s`, emit its nine bits from bit 8 (most significant) to
bit 0.  Concatenate the bit strings without gaps and divide into successive
8‑bit octets, most significant bit first.  At end of the logical stream, write
zero bits to finish the last octet if necessary.

The first eight values of `vectors/mixed9.input` are:

```text
values:      0, 127, 255, 256, 511, 1, 320, 17
packed hex:  00 1f df f0 0f f8 06 80 11
```

The reverse process reads nine MSB‑first bits at a time.  For the compressed
frame in mode 9, a final partly used octet MUST have zero unused bits.  If `N`
is the symbol count, packed mode‑9 output occupies `ceil(9*N/8)` octets;
mode‑8 output occupies `N` octets.

The frame header's CRC covers **its first 16 octets**.  Each block CRC covers
its **decoded logical symbols re‑packed as a fresh 9‑bit MSB‑first stream**,
zero‑padded at the *end of that block*, regardless of the frame's mode 8 or
9.  Therefore a mode‑8 file's block CRC is **not** the CRC of its ordinary
input octets.  When mode‑9 blocks share a frame, the final octet padding is
applied anew for each block CRC, but the actual frame output packing carries
unused bits across block boundaries without introducing gaps.

CRC algorithm: CRC‑32/ISO‑HDLC (reflected polynomial `0xEDB88320`, initial
state `0xFFFFFFFF`, process bits least‑significant‑first **within each already
constructed octet**, final XOR `0xFFFFFFFF`).  This equals the usual
`zlib.crc32()` over the prescribed octets.

## 3. Frame and block headers

Every frame begins with one **20‑octet header**, followed by enough blocks
to produce exactly its stated number of symbols.  The frame has no trailer.
For a zero‑symbol frame there are no blocks.  All header widths are unsigned
and little‑endian except the ASCII magics and single‑byte flags.

### 3.1 Frame header (20 octets)

| Offset | Octets | Required value / interpretation                                                                                                                                                                |
|-------:|:-------|:-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 0      | 4      | ASCII `LZ9A` (`4c 5a 39 41`)                                                                                                                                                                   |
| 4      | 1      | Version **6**                                                                                                                                                                                  |
| 5      | 1      | Output representation **8** or **9**                                                                                                                                                           |
| 6      | 2      | Header length **20** (`0x0014`)                                                                                                                                                                |
| 8      | 4      | Total logical symbol count, `N`, 0–4,294,967,295                                                                                                                                               |
| 12     | 4      | Feature flags: bit 0 = 64K window, bit 1 = EXTRA‑ANS, bit 2 = reserved (zero), bit 3 = aligned‑distance support, bit 4 = per‑block aligned‑distance selection; all other bits **MUST** be zero |
| 16     | 4      | CRC‑32/ISO‑HDLC of octets 0–15                                                                                                                                                                 |

The UNIX CLI uses seekable input and may impose a smaller file size limit
through `ftell`/`long`; that is a host implementation limit, not an additional
wire‑format restriction.

### 3.2 Block header (48 octets)

| Offset | Octets | Interpretation                                                                                                                                                                              |
|-------:|:-------|:--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 0      | 4      | ASCII `LZ9B` (`4c 5a 39 42`)                                                                                                                                                                |
| 4      | 1      | Version **6**                                                                                                                                                                               |
| 5      | 1      | Bits 0–1: type 0=STORED, 1=LZ. STORED uses bits 2–7 for width (7/8/9). LZ uses width 0; when frame feature bit 4 is set, bit 4 of this octet selects aligned‑distance coding for that block |
| 6      | 2      | Header length **48** (`0x0030`); fixed for every v6 block                                                                                                                                   |
| 8      | 4      | `n`: decoded symbols in this block, 1–32,768                                                                                                                                                |
| 12     | 4      | `q`: number of LZ match sequences, 0–`floor(n/3)`                                                                                                                                           |
| 16     | 4      | `lc`: total number of literal symbols, at most `n`                                                                                                                                          |
| 20     | 4      | `dl`: descriptor length; four entropy descriptors, plus EXTRA if EXTRA‑ANS is set                                                                                                           |
| 24     | 4      | Meaningful bits in LIT stream                                                                                                                                                               |
| 28     | 4      | Meaningful bits in LL stream                                                                                                                                                                |
| 32     | 4      | Meaningful bits in ML stream                                                                                                                                                                |
| 36     | 4      | Meaningful bits in OF stream                                                                                                                                                                |
| 40     | 4      | Meaningful bits in EXTRA stream                                                                                                                                                             |
| 44     | 4      | CRC‑32/ISO‑HDLC of this block's decoded canonical packed‑9 symbols                                                                                                                          |

The values in the five bit‑length fields do **not** include per‑stream
byte‑alignment padding.  The physical length of each is `ceil(bits/8)`.

The five payloads are concatenated in header order, *each* beginning on a new
octet boundary.  Any unused low bits in the last octet of a payload MUST be
zero.  A zero‑length stream contributes no octets.

The number of blocks is not stored.  Continue until the accumulated `n` equals
frame `N`.  Neither an incomplete frame nor trailing octets are valid.  A block
MUST NOT claim more symbols than remain in the frame.

### 3.3 STORED blocks

`type=0`, `width=7`, `8`, or `9`; `dl=0`; `q=0`; `lc=n`; and only LIT
bit‑length is nonzero, equal to `n * width`.  All symbols are packed MSB‑first
at the specified width.  A width‑7 block contains values 0–127, width‑8 values
0–255, and width‑9 values 0–511.  Mode‑8 frames MUST NOT output values above
255.  STORED blocks leave the LZ history **intact** and do **not** change the
three repeat‑distance slots.  They are *not* reset points for the dictionary.

The encoder chooses the narrowest width that can represent all block symbols.
The decoder accepts any permitted width with valid symbol values.

### 3.4 LZ blocks

`type=1`, `width=0`.  Immediately after the 48‑octet block header:

1. Descriptors for **LIT**, **LL**, **ML**, and **OF**, exactly `dl` octets in
   total.  If EXTRA‑ANS is enabled, its EXTRA descriptor follows these four.

2. The four encoded entropy bitstreams in that same order.

3. The fifth, forward‑oriented **EXTRA** bitstream, encoded as raw bits unless
   EXTRA‑ANS is enabled.

The count of LIT symbols is `lc`.  Each of LL, ML, and OF has exactly `q`
symbols.  EXTRA has no standalone symbols or entropy descriptor; it holds only
the additional bits required by length and explicit‑distance codes.

When `q=0`, `lc` MUST equal `n`, and the three sequence streams MUST be EMPTY;
an LZ block containing only entropy‑coded literals is legal.

## 4. Decoded sequence semantics

Each of the `q` sequences consists of one **literal‑run length** LL, one
**match length** ML, and one **offset** OF.  These are logically synchronous,
even though their codes occupy separate entropy streams.

For each sequence in forward order:

1. Decode LL and emit exactly LL symbols from LIT.

2. Decode ML; it MUST be at least three.

3. Decode OF to a backward **distance** `d` between 1 and the selected
   window limit (32,768 or 65,536 symbols).

4. Copy ML symbols from `d` logical positions behind the current output
   cursor, one symbol at a time, allowing source and destination to overlap.

Once all `q` sequences are emitted, the decoder emits the remaining
`lc - sum(LL)` literal symbols, sometimes called the *terminal literal run*.
The output symbol count MUST be exactly `n`.  No separate end‑of‑block or
literal‑vs‑match flag exists.  The sequence count supplies framing.

The dictionary includes preceding decoded blocks in the **same frame**.
Distances MUST NOT reach before the start of the frame (there is no preset
dictionary), but may reach across a block boundary.  No match can reference
future symbols.

Matches referencing positions before the beginning of the stream/frame (where
`distance > produced_symbols_in_frame`) are termed **pre‑history** matches.

In LZ9ANS, pre‑history offsets are strictly illegal: decoders MUST validate
every match distance and reject the stream immediately if `d` exceeds the
number of already decoded symbols in the frame.  Pre‑history references
**MUST NOT** be resolved by reading uninitialized memory, by wrapping around
the history buffer, or by pre‑seeding dummy symbols (such as zeros or spaces).

### 4.1 Literal-run and match-length codes

LL and ML each use an alphabet of **32** codes numbered 0–31.  The same
conversion uses `bias=0` for LL and `bias=3` for ML.  Given non‑negative
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
16 with zero additional bits (`k=0`).  Codes permit larger values than can fit
into one block; decoders MUST reject any decoded length that exceeds the
remaining block capacity.  ML values larger than 32,768 cannot occur in a valid
v6 encoder output.

### 4.2 Offset alphabet, repeat slots, and RUN

OF has **20** codes by default and 21 codes (0–20) with a 64K window:

| OF code | Meaning                                             | EXTRA bits  | Repeat‑slot action                |
|--------:|:----------------------------------------------------|:------------|:----------------------------------|
| 0       | Distance currently in slot `R0`                     | 0           | Promote that distance to `R0`     |
| 1       | Distance currently in slot `R1`                     | 0           | Promote that distance to `R0`     |
| 2       | Distance currently in slot `R2`                     | 0           | Promote that distance to `R0`     |
| 3       | **RUN:** distance exactly 1                         | 0           | **Do not modify** repeat slots    |
| 4–18    | Explicit distance bucket `k=code-4`, `0 <= k <= 14` | Exactly `k` | Promote explicit distance to `R0` |
| 19      | Explicit distance exactly **32,768**                | 0           | Promote 32,768 to `R0`            |
| 20      | Explicit distance 32,769–65,536 (64K window only)   | 15          | Promote explicit distance to `R0` |

Initial slots at the beginning of each frame: **`R0=R1=R2=INVALID`**.  Code
0/1/2 is invalid unless its referenced slot has been initialized by a prior
explicit or repeat match.  Repeat slots persist across **both** LZ and STORED
block boundaries, but not across frames.  Code 3 works whenever there is at
least one previously decoded symbol; it is not dependent on the repeat slots.

For explicit code `c` in 4–18, `k=c-4`:

```text
distance = 2^k + read_extra(k)
```

Thus c=4 represents distance 1 with zero bits, c=5 represents 2–3, c=6
represents 4–7, c=17 represents 8,192–16,383, and c=18 represents
16,384–32,767.  Code 19 is the exact 32,768 endpoint.  An explicit distance1
(code4) is permitted and *does* update the repeat slots; RUN (code3) expresses
the same distance without updating them.  This semantic distinction
is intentional.

**MRU promotion** takes a selected distance `d`, removes its previous
occurrence if present in any slot, shifts newer slots right, inserts `d` as
`R0`, and discards the former oldest slot if needed.  No valid slot should be
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
code0 always means slot R0, including following a zero‑length literal run.  An
encoder and decoder must implement this same rule.

### 4.3 EXTRA bit ordering

For each sequence in forward order, append the following *to the one shared
EXTRA stream*, with no inter‑field padding:

1. LL additional bits, if the LL code requires any.

2. ML additional bits, if the ML code requires any.

3. If aligned‑distance coding is enabled and OF selects an eligible explicit
   bucket (codes 6–18, or code 20 with the 64K window), one alignment flag bit.

4. OF additional bits, if an explicit distance bucket requires any.

The bit groups are MSB‑first.  No EXTRA bits are emitted for RUN or repeat
codes.  Literal payloads never contribute to EXTRA.  The alignment flag is
part of EXTRA and is entropy‑coded when EXTRA‑ANS is enabled.

## 5. Entropy streams and descriptors

The alphabet sizes are **512 LIT**, **32 LL**, **32 ML**, and **20 OF**
(21 OF with the 64K‑window flag).  If EXTRA‑ANS is enabled, EXTRA has a binary
alphabet of **2**.  Each stream independently chooses one of five descriptor
modes.  The selected mode is specified by its descriptor, not inferred from the
data.

Descriptors are concatenated and parsed without alignment gaps:

| Mode byte | Name  | Rest of descriptor                                          | Payload                                            |
|----------:|:------|:------------------------------------------------------------|:---------------------------------------------------|
| 0         | EMPTY | Nothing                                                     | Zero bits; allowed only when symbol count is zero  |
| 1         | RAW   | One octet: symbol width                                     | Forward MSB‑first fixed‑width codes                |
| 2         | RLE   | One unsigned varint: sole symbol value                      | Zero bits, repeat symbol for entire count          |
| 3         | tANS  | Presence bitmap followed by packed normalized frequencies   | Reverse‑consumed tANS bit groups                   |
| 4         | REUSE | Nothing                                                     | Reverse‑consumed tANS bit groups using prior table |

RAW LIT accepts width 7/8/9; RAW LL, ML, OF always use width **5**.  Five bits
suffice for the 21‑symbol OF alphabet used with a 64K window; codes 21–31 are
always invalid, and code 20 is invalid unless the 64K‑window flag is set.  RAW
EXTRA, when enabled, uses width 1.  The decoder checks that all symbols lie
within the required alphabet, that mode EMPTY matches a zero count and no
payload, and that all meaningful payload bits are consumed.

RLE values use the existing unsigned little‑endian base‑128 varint.  Each
octet contributes its low seven bits; bit 7 means continue.  The decoder
permits at most three octets per integer.  Values are further bounded by the
field's semantic range.  Encoders normally emit the shortest representation.

### 5.1 Compact tANS descriptors, table reuse, and normalization

A mode‑3 tANS descriptor begins with a fixed‑size **presence bitmap** of
`ceil(alphabet/8)` octets.  Within each octet, the most significant bit names
the lowest‑numbered symbol in that octet.  A set bit means that symbol has a
nonzero normalized frequency.  Symbols are then enumerated in ascending
numerical order.  Unused low‑order bits in the final bitmap octet MUST be zero.

The frequency stream immediately follows the bitmap.  It is a continuous
MSB‑first bitstream; it is padded with zero bits to the next octet boundary
when the descriptor ends.  Frequencies MUST be positive, and their sum MUST be
exactly **1024**.  The encoding depends on the stream kind:

- **LIT:** Each positive frequency is represented by Elias gamma coding of
  the frequency itself.  For positive integer `f`, let `k=floor(log2(f))`;
  write `k` zero bits followed by the `k+1`‑bit binary representation of `f`.
- **LL, ML, OF, EXTRA:** Frequencies use truncated‑binary coding in ascending
  symbol order.  Let `assigned` be the sum of frequencies already decoded and
  `remaining` the number of set bitmap entries not yet decoded.  For the next
  frequency, define `R = 1024 - assigned - remaining` and `x = f - 1`.
  Let `b=floor(log2(R))` and `cutoff=2^(b+1)-R`.  If `x < cutoff`, read/write
  `x` in `b` bits; otherwise read/write `x+cutoff` in `b+1` bits.  Then add
  `f` to `assigned`.

The decoder MUST reject a descriptor if a decoded frequency is zero, a value
exceeds the remaining normalization budget, the frequencies do not sum to
1024, fewer than two symbols are present, unused bitmap bits are nonzero, or
nonzero padding follows the final frequency.  The exact frequency normalization
algorithm used by the encoder is **not normative**; all decoders use the
serialized normalized frequencies.

Mode 4, **REUSE**, has no descriptor payload.  It selects the most recently
installed mode‑3 tANS model for the same stream kind in the current frame.
The first use of REUSE for a stream is invalid until that stream has installed
a model with mode 3.  A mode‑3 descriptor replaces that stream's cached model.
EMPTY, RAW, and RLE do not replace it.  Models persist across LZ and STORED
block boundaries within one frame, but never across frames.  The ANS state
itself is initialized independently for every block; REUSE saves only the
normalized model description, not ANS state.

The encoder may use REUSE only when the candidate normalized frequency vector
matches the model already installed for that stream.  If a speculative LZ
block is ultimately emitted as STORED, its untransmitted candidate models are
not considered installed.

### 5.2 Deterministic 1024-state table construction

Given the normalized frequencies, construct `spread[0..1023]` as follows:

```text
place = 0
for symbol in ascending numerical order:
    repeat freq[symbol] times:
        spread[place] = symbol
        place = (place + 643) & 1023
```

There is no low‑probability `-1` special case, FSE reserved tail, or other
hidden state permutation.  Because 643 is odd, it visits every one of the 1024
states before repeating.  At completion `place` is zero.

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
its initial decoder state, numbered 0–1023.  The decoder then retrieves
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
reverse cursor, then interpret that *whole group MSB‑first*.  **Do not reverse
the bit order within a group.** For example if the original forward‑written
groups were `101`, `01`, `1110`, final decoder retrieves `1110`, `01`, `101`,
**not** `0111`, `10`, `101`.

The encoder constructs the inverse transition starting at state zero,
processing the stream's symbols in **reverse** symbol order.  For a symbol with
frequency `f`, let `y=1024+encoder_state`.  Compute:

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
state‑index order.  Finally append `encoder_state` as exactly ten bits.
Each tANS stream resets its ANS state to zero at every block boundary.
Its normalized frequency model may persist across blocks and be selected with
mode‑4 REUSE; LZ history and repeat distances also persist across blocks.

The current C implementation can be used to test all table equations;
`inspect_lz9ans.py` deliberately reconstructs the same tables without calling
any part of the C implementation.

## 6. Decoder procedure and validation requirements

A conforming v6 decoder SHOULD implement the following stages, enforcing all
checks before trusting an offset, size, state, or output count:

1. Read and validate the 20‑octet frame header, including CRC, version,
   representation, header length, and feature flags.

2. Initialize an empty 32,768‑ or 65,536‑symbol history as selected by the
   frame flags (zero‑initializing memory as defense‑in‑depth against memory
   disclosure) and three INVALID repeat slots.

3. For each block, validate magic/version/header length/type/width, counts,
   length limits, and the announced size against the remaining frame.

4. Bound `dl` by 8192; bound the sum of the five rounded payload sizes by
   81920; read *exactly* the announced descriptor and payload octets.

5. Check the unused low‑order pad bits of **every** substream, including
   stored data.

6. For STORED, read `n` fixed‑width symbols, updating history but not MRU.

7. For LZ, reconstruct the LIT/LL/ML/OF descriptors and, when EXTRA‑ANS is
   set, the EXTRA descriptor.  Validate frequencies and counts, initialize
   entropy states, and decode sequences plus the terminal literal run.

8. For every copy, validate `1 <= distance <= window_limit`, and ensure the
   distance does not exceed the already reconstructed frame prefix
   (`distance <= produced`), rejecting all pre‑history references.  Validate
   `ML >= 3`, and verify the copy does not exceed this block's remaining
   output capacity.  Copy forwards one symbol at a time to support overlaps.

9. Verify each entropy stream consumes exactly its meaningful bits and its
   prescribed symbol count; tANS must finish in state zero; EXTRA must be
   exhausted.  Verify the actual literal and block symbol counts.

10. Compute the canonical packed‑9 block CRC; reject a mismatch.

11. At the frame symbol count, flush the mode‑9 terminal partial octet
    with zero fill; reject trailing compressed octets.

Never allocate memory directly from an unchecked on‑disk length.  Streaming
implementations can reject early; the Python independent decoder prioritizes
clarity over bounded memory and should not be used as a hardened service.

The current host CLI may leave a partially written output file when a
subsequent block is corrupt.  Its caller must use the exit status and should
write to a temporary name if destination atomicity is required.

## 7. Encoder reference choices (informative, not required for decoding)

LZ9ANS v6 uses an **8,192‑entry hash table** keyed from three consecutive
symbols and a hash‑chain ring sized to the selected history window to locate
match candidates.  The parser maintains a 32K‑symbol block plus up to 64K
history in 16‑bit C containers, records `(LL, ML, distance)` sequences, and
emits four entropy streams and the shared EXTRA bitstream.  The distance is
stored in a 32‑bit C field so the encoder can carry a 17‑bit distance plus the
repeat‑slot tag; this is an implementation detail, not a wire field.

The levels from `-1` through `-9` all produce **the same v6 wire format**.
The default is **level 5** and the default window remains **32,768 symbols**.
The search‑depth limit is the number of hash‑chain candidates examined; the
three repeat‑distance slots are probed separately and therefore do not consume
this budget:

| Level | Hash‑chain depth | Parser behavior                                      |
|------:|:-----------------|:-----------------------------------------------------|
| 1     | 1                | Greedy, no entropy‑price pass                        |
| 2     | 2                | Greedy, no entropy‑price pass                        |
| 3     | 4                | Greedy, no entropy‑price pass                        |
| 4     | 8                | Initial greedy pass; entropy‑price reparse           |
| 5     | 24               | Level 4 plus one‑position lazy choice; default       |
| 6     | 48               | Level 5 with deeper candidate search                 |
| 7     | 96               | Level 5 with deeper candidate search                 |
| 8     | 192              | Level 5 plus near‑longest alternative match trials   |
| 9     | 512              | Level 8 plus a further cost‑refinement pass          |

The match finder explicitly probes R0, R1, and R2 before walking the hash
chain, so repeat distances remain candidates even when their chain entries
fall outside the configured depth.  During both greedy and cost‑aware searches,
a candidate is skipped when it differs at the best already‑known match
length; this cannot improve the current match and avoids extending and pricing
dominated candidates.  The prune is applied only when that comparison is within
the available input and candidate history.

The cost estimator produces per‑symbol prices in fixed‑point **Q8 bits** (256
cost units per bit).  It counts codes in a preliminary parse, normalizes their
tANS frequencies to 1024, estimates negative log probability plus an amortized
descriptor cost, and compares tANS with RAW and RLE.  It adds exact extra‑bit
counts for the chosen LL/ML/OF codes and estimates a fixed per‑sequence
overhead.  Literal sums are accelerated with a cumulative prefix every 16
symbols.  Levels 8–9 consider slightly shorter alternatives when costed.  When
distance 1 could use either RUN or an initialized repeat slot, the cost‑aware
parser may choose the lower estimated offset‑code cost.

This is **bounded heuristic optimization**, not exact optimal parsing.  Prices
come from one or two block‑level frequency snapshots; the final entropy
distribution may differ after reparse.  Compression size is not mathematically
guaranteed to improve monotonically with increasing level.

For each symbol stream the encoder chooses EMPTY, RLE, RAW, a new compact tANS
model, or REUSE by estimated serialized size.  REUSE is available only when the
normalized model equals the most recently installed model for that stream.  If
the *whole* LZ payload (including descriptors) is no smaller than storing the
block at its narrowest 7/8/9 bit width, it writes STORED instead; speculative
repeat‑slot changes and untransmitted table models are discarded.  The decoder
never needs to know which parser or level was used.

Linux reference implementation explicit state on one x86‑64/8‑bit‑octet build:
**497,730 octets encoder state** and **207,392 octets base decoder state** as
reported by `./lz9ans --memory` (plus stack, C library file buffers, allocator
overhead).  The base decoder figure includes four ordinary ANS tables;
`--extra-ans` adds one `AnsTable` (10,754 octets on the stated host).  The
accounted encoder state remains 497,730 bytes, below 512 KiB (524,288 bytes)
by 26,558 bytes.  No encoder or decoder allocation was added for the table
cache: the five 512‑entry frequency vectors occupy 5,120 bytes at the tail
of the encoder's existing staging allocation.  The usable staged payload area
is reduced accordingly, but remains larger than the maximum STORED block.  The
reported allocation totals are unchanged.  The figure includes the `Encoder`
structure and its associated arrays, but excludes small helper stack
temporaries, C runtime state, `FILE` buffers, allocator overhead, and the
process itself.  It is therefore a useful state budget, not a guarantee of
total resident memory.  This is **not** a proof of native Multics segment
occupancy: ANSI C89's 8‑bit‑octet host assumptions and Multics PL/I word
representation are different and must be sized separately.

## 8. Golden vectors and interoperability

The checked‑in `.lz9a` vectors are the byte‑exact **v6** frames produced by
the reference encoder with automatic per‑block aligned‑distance selection.  The
frames advertise feature mask `0x18`; `mixed9` also exercises an LZ block whose
aligned‑distance selector is set.  `test_vectors.py` verifies their SHA‑256
digests, decodes them with both the C decoder and the independent Python
decoder, and re‑encodes the original inputs to ensure byte‑for‑byte
reproducibility.  These vectors exercise the published wire representation;
they are not intended as a general compression benchmark.  Historical v3
frame bytes remain documented below solely as a legacy example and are rejected
by the v6 decoder.

| Fixture       | Input mode | Level | Raw octets | Frame octets  | SHA‑256 of compressed frame                                        |
|--------------:|:-----------|:------|:-----------|:--------------|:-------------------------------------------------------------------|
| `empty8`      | 8          | 1     | 0          | 20            | `9bc9b0a79566eda38946773b3ace46c25652eb95a7f9d9a020e57fd746807b61` |
| `abc8`        | 8          | 1     | 12         | 79            | `6bc328d99e4a01b9733b6534290c8531a2a47f29a9fe61d6296c06cce8cbb3bc` |
| `run8`        | 8          | 6     | 96         | 77            | `a589c141902aa34fe17e37601f247c6a4e8d7217adf378a4cb61bc8607d17659` |
| `mixed9`      | 9          | 6     | 216        | 87            | `f63c3c56a8d2ee11e547b7c5712d1e0fc05e6e2891558e2e0c9458ee4d7c8ce9` |
| `switch8`     | 8          | 6     | 1,152      | 98            | `9e5ab81c964c216a4f59db7dac2342eb40284468aeab4a1ca8fbf42484639056` |
| `longdist16k` | 8          | 9     | 32,768     | 16,465        | `3cfb9c5336e9fecc333df7160dc8f542c97ee1f4ede9c199aa027b5b3cc85c50` |
| `longdist32k` | 8          | 9     | 65,536     | 32,893        | `bb544e11fffd452aeed4a9694da5a95461b3e72ef373469375397b690fa639d0` |

The legacy v3 empty‑mode‑8 frame is exactly these **20 octets**:

```text
4c 5a 39 41 03 08 14 00 00 00 00 00 00 00 00 00 be 16 f2 0a
```

The `run8` frame uses one LZ sequence and **OF code 3** (RUN).  The `mixed9`
fixture exercises actual values over 255.  The `longdist16k` and `longdist32k`
fixtures exercise explicit offset codes 18 and 19, respectively; `longdist32k`
references the full preceding stored block.  Bigger real ELFs in the regression
suite exercise all three MRU codes and literal, length, and offset
tANS streams.  `test_features.py` also builds a two‑block frame whose second
block reuses an installed LIT model, then checks both C and Python decoding.

Independent implementations should decode the published **bytes**, not just
their own re‑encodings, and should reject the negative cases above.  For
interoperability the actual uncompressed content, packed9 bit order, block
checksums, MRU state across block types, and tANS state termination all have
to match exactly.

## 9. Format gaps and future design choices

- **Format stability:** V6 is the only format emitted and accepted by this
  implementation.  It uses a fixed 48‑octet block header and fixed five‑stream
  layout; the 64K‑window and EXTRA‑ANS flags do not change that layout.  Frames
  with any version other than 6 are rejected.  Future incompatible changes
  require a new format version.

- **Container semantics:** ZIP / archive method numbers, independent blocks,
  indexing, streaming inputs of unknown length, and concatenated frames
  are outside this experimental format.

- **Authenticity:** CRC can detect accidental damage but is **not** a
  cryptographic integrity or authenticity mechanism.

- **Security considerations and pre‑history buffer isolation:** LZ77 codecs
  must explicitly define how to handle backward distances referencing positions
  before the beginning of the file ("pre‑history").

  LZ9ANS prevents this attack surface through several distinct layers:

  1. *Grammar and validation:* Pre‑history references are strictly illegal.
     Every distance `d` must satisfy `1 <= d <= produced` (where `produced` is
     the count of symbols emitted so far in the current frame).  Any stream
     with `d > produced` is malformed and MUST be rejected immediately prior
     to any buffer access or copy.

  2. *Repeat‑slot integrity:* Repeat distance slots are initialized to
     `INVALID` at frame start; referencing an uninitialized slot is an
     immediate format error.

  3. *No synthetic pre‑seeding:* The history at the beginning of a frame is
     logically empty.  There is no implicit dictionary pre‑seeded with zeros,
     spaces, or static tables.

  4. *Defense‑in‑depth zeroing:* Conforming encoders and decoders SHOULD
     explicitly zero‑initialize (`memset` / `calloc`) their history and sliding
     window buffers upon allocation, ensuring that even under unexpected fault
     conditions or partial decoding inspection, no residual host heap memory
     can ever be accessed or disclosed.

- **Portability:** This specific UNIX reference implementation expects 8‑bit
  C `char`, 16‑bit `short`, 32‑bit `int` and seekable files.  Any Multics PL/I
  port should carry the *wire equations* and use 36‑bit‑native representations,
  not emulate these host C types unnecessarily.

## 10. Version 6 feature flags and fixed stream layout

Version 6 is the only format emitted or accepted by this implementation.  Its
20‑octet frame header uses the same field widths as the earlier layout, but
octet 4 MUST be 6 and the 32‑bit field at offset 12 is a feature bitmask.  A v6
frame may set any combination of the following flags:

| Bit | Mask         | Meaning                                                        |
|----:|:-------------|:---------------------------------------------------------------|
| 0   | `0x00000001` | 65,536‑symbol history window                                   |
| 1   | `0x00000002` | Entropy‑coded EXTRA bitstream                                  |
| 2   | `0x00000004` | Reserved; MUST be zero                                         |
| 3   | `0x00000008` | Aligned‑distance coding is available throughout the frame      |
| 4   | `0x00000010` | Aligned‑distance coding is selected independently per LZ block |

Bits outside `0x1b` are invalid.  Reserved bit 2 MUST be zero.  Bit 4 MUST NOT
be set unless bit 3 is also set.  Bits 0, 1, and 3 apply to the whole frame;
bit 4 enables the per‑block selection syntax described in section 10.5.  When
bit 4 is clear, bit 3 selects the legacy frame‑wide aligned‑distance form;
when both bits 3 and 4 are clear, the ordinary distance representation is used
throughout the frame.  The reference encoder always sets bits 3 and 4 and
records its decision separately in each LZ block.  V6 retains the
32,768‑symbol block maximum.  Frames and blocks whose version byte is not 6
MUST be rejected; v3, v4, and v5 compatibility is intentionally not
implemented.  Frames that set reserved bit 2 MUST be rejected.

### 10.1 Fixed block-header size and stream layout

Every v6 block has a 48‑octet header.  The five bit‑length fields at offsets
24, 28, 32, 36, and 40 always describe **LIT, LL, ML, OF, EXTRA**,
respectively; the block CRC is always at offset 44.  Neither frame flag changes
this layout.  All payloads begin on octet boundaries and their unused low‑order
padding bits **MUST** be zero.  A STORED block uses only LIT; all other bit
lengths and `dl` **MUST** be zero.

An LZ block always has four logical entropy streams: LIT, LL, ML, and OF.
The descriptor area contains exactly one descriptor for each stream, in that
order.  If EXTRA‑ANS is enabled, its descriptor follows the four entropy
stream descriptors.

If EXTRA‑ANS is enabled, an additional EXTRA descriptor follows the
LIT/LL/ML/OF descriptors; otherwise the fifth EXTRA payload is a raw
forward‑oriented bitstream.  The EXTRA bit‑length field is present in both
modes, and the decoder derives the number of EXTRA bits from the decoded LL,
ML, and OF codes.

### 10.2 64K history window (`0x01`)

The window flag increases the history limit to **65,536 logical symbols**;
blocks and maximum match length remain 32,768 symbols.  The OF alphabet grows
from 20 to 21 symbols; repeat slots and RUN semantics are unchanged.  Codes
0–19 retain the ordinary offset meanings, with code 19 representing exactly
32,768.  Code 20 represents distances 32,769 through 65,536 and carries exactly
15 EXTRA bits:

```text
distance = 32769 + read_extra(15)
```

The encoder writes `distance - 32769` in 15 MSB‑first bits.  Code 20 is invalid
unless the 64K‑window flag is set.  With the default 32K window the OF alphabet
remains 20 and code 20 is invalid.  An implementation must use at least 17 bits
for the logical distance; 16‑bit storage is insufficient for distance 65,536.

### 10.3 Binary tANS EXTRA stream (`0x02`)

With EXTRA‑ANS enabled, EXTRA represents the same sequence of bits in the same
logical order: LL extra bits, ML extra bits, an eligible offset‑alignment flag
when enabled, then OF extra bits for each sequence in forward order.  Instead
of writing those bits as a raw bitstream,
the encoder selects an entropy mode using the binary alphabet `0` and `1`:

- EMPTY if the block has no EXTRA bits;
- RLE if all EXTRA bits have the same value;
- tANS with a normalized 1,024‑state binary model when both values occur and
  the encoded payload plus descriptor is smaller than RAW;
- otherwise RAW with width 1.

The descriptor uses the same EMPTY/RAW/RLE/tANS syntax as other entropy
streams.  The decoder counts the consumed EXTRA bits while decoding LL, ML, and
OF codes and validates exact stream consumption and terminal tANS state.  In
tANS mode the encoder feeds bits in reverse sequence/component/bit order so
the decoder reconstructs the original forward EXTRA bit order.  If the tANS
representation does not beat RAW after descriptor cost, the encoder chooses
RAW.  Neither feature changes the fixed five‑stream block layout.


### 10.4 Aligned explicit distances (`0x08`)

With this flag, selected explicit distances carry one alignment flag in EXTRA.
It is present for OF codes 6–18 (the explicit buckets with at least two
ordinary extra bits) and, when the 64K window is enabled, code 20.  It is not
present for repeat codes 0–2, RUN code 3, explicit codes 4–5, or exact‑distance
code 19.

For codes 6–18, let `k = code - 4`.  The alignment flag precedes the ordinary
distance extra bits:

- Flag 1 means the distance is divisible by four and is decoded as
  `distance = 2^k + 4 * read_extra(k - 2)`.
- Flag 0 means the ordinary representation is used:
  `distance = 2^k + read_extra(k)`.  The resulting distance MUST NOT be
  divisible by four; this canonicality rule prevents duplicate encodings.

For code 20 with the 64K window:

- Flag 1 means `distance = 32772 + 4 * read_extra(13)`.
- Flag 0 means `distance = 32769 + read_extra(15)` and the resulting distance
  MUST NOT be divisible by four.

The encoder sets the flag from the distance residue.  This feature does not
change the OF alphabet or its RAW width.  The additional flag is included in
the ordinary forward EXTRA bit order and, when enabled, in the binary
EXTRA‑ANS model.  A decoder MUST reject noncanonical flag‑0 forms whose
decoded distance is divisible by four.

### 10.5 Per-block aligned-distance selection (`0x18`)

When frame feature bits 3 and 4 are both set, alignment is selected separately
for each LZ block.  The decoder MUST use aligned‑distance decoding only when
bit 4 of block‑header octet 5 is set.  For an LZ block this bit is otherwise
zero; bits 5–7 of that octet MUST be zero.  For a STORED block, octet 5
continues to encode the stored symbol width in bits 2–7, and bit 4 MUST NOT be
interpreted as an alignment selector.  The reference encoder always emits
both frame feature bits and makes a fresh selection for every LZ block.

The per‑block selector is a coding decision, not a new payload stream.  The
encoder can select the aligned form using the exact raw‑EXTRA bit‑count delta:
let `E` be the number of eligible explicit‑distance codes (6–18, plus code 20
with a 64K window), and let `A` be the number of those distances divisible by
four.  Aligned coding adds one selector bit for each eligible distance and
removes two residual bits for each aligned distance, so its raw‑EXTRA bit
delta is `E - 2*A`.  A cost‑based encoder SHOULD select aligned coding only
when this delta is negative; ties select the ordinary representation.

The reference encoder requires at least one symbol with value 256–511 in the
block before considering aligned coding.  This ninth‑bit gate is a
compression‑selection heuristic, not a decoder requirement; the decoder
follows only the block‑header selector.  The encoder then selects aligned
coding only when the exact raw‑EXTRA bit delta is negative.  Blocks containing
only values below 256 therefore always use the ordinary distance
representation.  This gate is intentionally retained for both ordinary
EXTRA and EXTRA‑ANS streams so the same deterministic selection rule applies
regardless of the entropy coding of EXTRA.
