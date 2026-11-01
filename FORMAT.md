<!-- Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com> -->
<!-- Copyright (c) 2025-2026 The DPS8M Development Team -->
<!-- SPDX-License-Identifier: MIT -->
# LZ9ANS version 8 (2026‑11‑01) stream format

> Document revision **17** (2026‑11‑01)

<!-- toc -->

- [1. Scope and terminology](#1-scope-and-terminology)
  * [1.1. Constants](#11-constants)
- [2. 9-bit bit packing and checksums](#2-9-bit-bit-packing-and-checksums)
- [3. Frame and block headers](#3-frame-and-block-headers)
  * [3.1. Frame header (17 to 4096 octets)](#31-frame-header-17-to-4096-octets)
  * [3.2. Block prefix and type-specific headers](#32-block-prefix-and-type-specific-headers)
  * [3.3. STORED blocks](#33-stored-blocks)
  * [3.4. LZ blocks](#34-lz-blocks)
  * [3.5. Reserved END block](#35-reserved-end-block)
- [4. Decoded sequence semantics](#4-decoded-sequence-semantics)
  * [4.1. Literal-run and match-length codes](#41-literal-run-and-match-length-codes)
  * [4.2. Offset alphabet, repeat slots, and RUN](#42-offset-alphabet-repeat-slots-and-run)
  * [4.3. EXTRA bit ordering](#43-extra-bit-ordering)
- [5. Entropy streams and descriptors](#5-entropy-streams-and-descriptors)
  * [5.1. Compact tANS descriptors, table reuse, and normalization](#51-compact-tans-descriptors-table-reuse-and-normalization)
  * [5.2. Deterministic 1024-state table construction](#52-deterministic-1024-state-table-construction)
  * [5.3. tANS decoding and the unusual reverse bit direction](#53-tans-decoding-and-the-unusual-reverse-bit-direction)
- [6. Decoder procedure and validation requirements](#6-decoder-procedure-and-validation-requirements)
- [7. Encoder reference choices (informative, not required for decoding)](#7-encoder-reference-choices-informative-not-required-for-decoding)
- [8. Golden vectors and interoperability](#8-golden-vectors-and-interoperability)
- [9. Format gaps and future design choices](#9-format-gaps-and-future-design-choices)
- [10. Version 8 features, extensions, and reserved control records](#10-version-8-features-extensions-and-reserved-control-records)
  * [10.1. Frame feature flags](#101-frame-feature-flags)
  * [10.2. TLV extension convention](#102-tlv-extension-convention)
  * [10.3. Unknown-length frame extension](#103-unknown-length-frame-extension)
  * [10.4. END record and final-symbol truncation](#104-end-record-and-final-symbol-truncation)
  * [10.5. 64K history window (`0x01`)](#105-64k-history-window-0x01)
  * [10.6. Aligned explicit distances (`0x08`)](#106-aligned-explicit-distances-0x08)
  * [10.7. Per-block aligned-distance selection (`0x18`)](#107-per-block-aligned-distance-selection-0x18)
  * [10.8. v8 stream-size bounds](#108-v8-stream-size-bounds)

<!-- tocstop -->

## 1. Scope and terminology

**LZ9ANS** compresses a stream of **symbols** with values from **0 to 511**.
Every symbol occupies nine meaningful bits logically, regardless of how it
is represented in the compressor's working RAM.

The UNIX host tool supports both 8‑bit and 9‑bit input:

- **Mode 8:** "Ordinary" UNIX binary files stored with one logical symbol per
  8‑bit octet.  This is a subset of the supported 9‑bit alphabet using only
  values 0–255.

- **Mode 9:** A packed MSB‑first stream of consecutive 9‑bit symbols.  Eight
  complete symbols occupy nine octets.  A final incomplete group is
  zero‑padded to an octet boundary.  The reserved `END` truncation extension
  can later shorten the meaningful bit count in the final symbol; the current
  UNIX implementation does not implement it.

**Bit position 0** of any packed stream denotes the most significant bit of
its first octet.  *Within the compressed format*, headers and descriptors are
always represented as **8‑bit octets** and integer fields are little‑endian.
Input symbol width and compressed header byte width are distinct concepts.

The format does **not** attach file names, timestamps, source‑system types,
record boundaries, or 36‑bit word attributes.  The current UNIX implementation
preserves complete 9‑bit symbols only; the reserved `END` metadata defines how
a future Multics implementation can represent an arbitrary final bit count.
Although the bit representation is intended to suit
`mxload`‑style "9‑in‑8" transports, compatibility with a particular `mxload`
version or tape encoding is *not* asserted.

### 1.1. Constants

| Identifier           | Value  | Meaning                                              |
|---------------------:|:-------|:-----------------------------------------------------|
| `BLOCK_N`            | 32,768 | Maximum logical output symbols in a block            |
| `WINDOW_DEFAULT`     | 32,768 | Default maximum backward match distance, in symbols  |
| `WINDOW_64K`         | 65,536 | Optional maximum backward match distance, in symbols |
| `MIN_MATCH`          | 3      | Minimum matched symbols                              |
| `MAX_MATCH`          | 32,768 | Maximum matched symbols in one sequence              |
| `TABLE_LOG`          | 10     | Base‑two log of tANS state count                     |
| `TABLE_N`            | 1,024  | tANS states, numbered 0–1023                         |
| `STREAM_COUNT`       | 5      | `LIT`, `LL`, `ML`, `OF`, raw `EXTRA` payload         |
| `MAX_DESC_OCTETS`    | 8,192  | Maximum total entropy descriptors in a block         |
| `MAX_PAYLOAD_OCTETS` | 81,920 | Maximum combined compressed substreams in a block    |
| `FRAME_HEADER_MAX_N` | 4,096  | Maximum frame‑header length, including TLVs and CRC  |

The block limit is 32,768 symbols in v8.  The default window is 32,768 symbols;
frame flag bit 0 enables a 65,536‑symbol window.  The reference encoder selects
aligned‑distance coding automatically for each `LZ` block when the block
contains at least one ninth‑bit symbol and the aligned representation reduces
the raw `EXTRA` bit count.  A match may refer to the immediately preceding
history and may refer backward within the current block.  The history at the
start of a frame is **empty**, **not** populated with synthetic zeros or other
values.

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

The reverse process reads nine MSB‑first bits at a time.  For the current
complete‑symbol mode‑9 representation, a final partly used octet **MUST** have
zero unused bits.  If `N` is the symbol count and no future `END` truncation
metadata is present, packed mode‑9 output occupies `ceil(9*N/8)` octets;
mode‑8 output occupies `N` octets.  The reserved final‑symbol truncation
extension is defined in section 10.4.

The frame‑header CRC covers **all header octets preceding its final four‑octet
CRC field** (the first 13 octets when there are no extensions).  Each block CRC
covers
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

Every v8 frame begins with a frame header whose length is 17–4,096 octets.
The header is followed by block records.  In the currently implemented
known‑length form, the blocks produce exactly the symbol count in the frame
header and there is no trailer.  A zero‑symbol known‑length frame has no
blocks.  A future unknown‑length form and its terminal record are reserved in
section 10; the current reference encoder does not emit that form and the
current decoders reject it.  All integer fields are unsigned and little‑endian
except the ASCII magics and single‑byte control/feature fields.

### 3.1. Frame header (17 to 4096 octets)

The fixed 13‑octet prefix is followed by zero or more TLV extensions and a
four‑octet CRC.  With no extensions, the header is exactly 17 octets.

| Offset              | Octets   | Required value / interpretation                                         |
|--------------------:|:---------|:------------------------------------------------------------------------|
| `0`                 | `4`      | ASCII `LZ9A` (`4c 5a 39 41`)                                            |
| `4`                 | `1`      | Version **`8`**                                                         |
| `5`                 | `1`      | Output representation **`8`** or **`9`**                                |
| `6`                 | `2`      | Total frame‑header length, 17–4,096 octets, including the final CRC     |
| `8`                 | `4`      | Total logical symbol count `N`, 0–4,294,967,295 for known‑length frames |
| `12`                | `1`      | Feature flags, defined in section 10.1                                  |
| `13`                | variable | Zero or more TLV extensions, ending immediately before the CRC          |
| `header_length - 4` | `4`      | CRC‑32/ISO‑HDLC of all preceding frame‑header octets                    |

The TLV convention is defined in section 10.2.  The extension area may be
empty.  A decoder **MUST** validate the complete header length before reading
the extension area, **MUST** bound it to 4,096 octets, and **MUST** validate
the CRC over the complete header prefix, including all TLVs.  Unknown
noncritical TLVs are skipped; unknown critical TLVs are rejected.  Duplicate
extension identifiers and identifier zero are invalid.

For known‑length frames, block decoding stops when the sum of decoded block
symbols equals `N`; any trailing octets are invalid.  The `0x81` unknown‑count
extension reserves a future streaming form in which `N` is zero and an `END`
record is mandatory.  The current implementation recognizes but rejects that
extension because unknown‑length streaming is not yet implemented.

The UNIX CLI currently counts its input before encoding and uses a known‑length
frame.  It uses seekable input for that count; this is a host implementation
limitation, not a requirement of the reserved future streaming representation.

### 3.2. Block prefix and type-specific headers

Every data block (`STORED` or `LZ`) begins with a seven‑octet common prefix.
The reserved `END` control record is a five‑octet exception and is defined in
section 3.5.


| Offset | Octets | Interpretation                                       |
|-------:|:-------|:-----------------------------------------------------|
| `0`    | `4`    | ASCII `LZ9B` (`4c 5a 39 42`)                         |
| `4`    | `1`    | Block type and type‑specific flags/width             |
| `5`    | `2`    | `n`: decoded logical symbols in this block, 1–32,768 |

The low two bits of octet 4 identify the data‑block type: `0=STORED` and
`1=LZ`.  Type `2=END` is a reserved control record with its own five‑octet
minimum syntax; type `3` is reserved and invalid.  For data blocks, the
remainder of octet 4 is interpreted according to the block type.  The current
reader reads the seven‑octet data‑block prefix first, then reads the remainder
of the 11‑octet `STORED` header or 27‑octet `LZ` header.  There is no per‑block
version or header‑length field: the enclosing frame version determines the
block layout.

A `STORED` block has an **11‑octet** header:

| Offset | Octets | Interpretation                                                     |
|-------:|:-------|:-------------------------------------------------------------------|
| `0–6`  | `7`    | Common prefix                                                      |
| `7`    | `4`    | CRC‑32/ISO‑HDLC of this block's decoded canonical packed‑9 symbols |

For a `STORED` block, octet 4 bits 0–1 are zero and bits 2–7 encode the symbol
width as `width << 2`.  The resulting octet values are `0x1c` for width 7,
`0x20` for width 8, and `0x24` for width 9.  No other width encoding is valid.
The payload immediately follows the 11‑octet header.

An `LZ` block has a **27‑octet** header:

| Offset | Octets | Interpretation                                                                |
|-------:|:-------|:------------------------------------------------------------------------------|
| `0–6`  | `7`    | Common prefix                                                                 |
| `7`    | `2`    | `q`: number of LZ match sequences, 0–`floor(n/3)`                             |
| `9`    | `2`    | `dl`: total descriptor length, 0–8,192 octets                                 |
| `11`   | `2`    | `LIT` payload byte length                                                     |
| `13`   | `2`    | `LL` payload byte length                                                      |
| `15`   | `2`    | `ML` payload byte length                                                      |
| `17`   | `2`    | `OF` payload byte length                                                      |
| `19`   | `2`    | `EXTRA` payload byte length                                                   |
| `21`   | `2`    | Five packed three‑bit padding counts; bit 15 is reserved and **MUST** be zero |
| `23`   | `4`    | CRC‑32/ISO‑HDLC of this block's decoded canonical *packed‑9* symbols          |

For an `LZ` block, octet 4 bits 0–1 equal `1`; bits 2–3 **MUST** be zero; bit 4
is the per‑block aligned‑distance selector when the frame advertises that
feature; and bits 5–7 **MUST** be zero.  The `LZ` header's `n`, `q`, and `dl`
fields are unsigned 16‑bit little‑endian values.

Each payload byte length is an unsigned 16‑bit value.  The corresponding
meaningful bit length is reconstructed as:

```text
meaningful_bits[i] = 8 * payload_bytes[i] - padding_bits[i]
```

The five padding counts occupy bits 0–14 of the padding field in stream order:
`LIT` in bits 0–2, `LL` in bits 3–5, `ML` in bits 6–8, `OF` in bits 9–11,
and `EXTRA` in bits 12–14.  Each count is 0–7.  If a stream byte length is
zero, its padding count **MUST** be zero.  A decoder **MUST** reject a padding
count larger than the stream's total bits, nonzero reserved bit 15, or nonzero
unused low‑order bits in the last payload octet.

The individual byte lengths are bounded by the coding limits in section 10;
the combined five payloads **MUST NOT** exceed 81,920 octets.  All five
payloads begin on separate octet boundaries, including zero‑length payloads
(which occupy no octets).  The payload order is `LIT`, `LL`, `ML`, `OF`,
`EXTRA`.

### 3.3. STORED blocks

A `STORED` block has type `0`, width 7, 8, or 9, and the 11‑octet header
specified above.  Its payload contains exactly `n * width` meaningful bits,
packed MSB‑first into `ceil(n * width / 8)` octets.  Unused low‑order bits of
the last payload octet **MUST** be zero.  Width‑7 blocks contain values 0–127,
width‑8 values 0–255, and width‑9 values 0–511.  Mode‑8 frames **MUST NOT**
output values above 255.

There are no transmitted `q`, `lc`, `dl`, entropy‑stream lengths, or padding
counts in a `STORED` header.  Those values are either fixed at zero or derived
from `n` and `width`.  `STORED` blocks leave the `LZ` history intact and do not
change the three repeat‑distance slots.  They are not dictionary reset points.

The encoder chooses the narrowest width that can represent all block symbols.
The decoder accepts any permitted width with valid symbol values.

### 3.4. LZ blocks

An LZ block has type `1`, width bits 2–3 equal to zero, and the 27‑octet header
specified above.  Immediately after the header are:

1. Exactly four entropy descriptors in the order `LIT`, `LL`, `ML`, `OF`,
   occupying exactly `dl` octets in total.  There is no `EXTRA` descriptor.
2. The five payload streams in the order `LIT`, `LL`, `ML`, `OF`, `EXTRA`,
   each occupying the byte length announced in the header.

`LIT`, `LL`, `ML`, and `OF` are the four entropy streams.  `EXTRA` is always a
raw, forward‑oriented bitstream containing only the additional bits required
by length and explicit‑distance codes.  Its meaningful bit length is carried
in the same byte‑length/padding representation as the other streams.

There is no `lc` field in v8.  The actual literal count is derivable from the
block output equation:

```text
literal_count = n - sum(match_length for each of the q sequences)
```

Equivalently, the decoder emits each sequence's `LL` literals followed by its
`ML` match, then emits terminal literals until exactly `n` symbols have been
produced.  The `LIT` stream therefore contains exactly the number of literals
implied by the decoded sequence lengths.  The decoder **MUST** validate the
`LIT` stream's actual consumption and its `EMPTY`/non‑`EMPTY` mode against
that derived count; it **MUST NOT** rely on an advertised literal count.

Each of `LL`, `ML`, and `OF` has exactly `q` symbols.  When `q=0`, the `LL`,
`ML`, and `OF` descriptors **MUST** be `EMPTY`, and the `LIT` stream supplies
all `n` symbols.  When the derived literal count is zero, the `LIT` descriptor
**MUST** be `EMPTY`; otherwise it **MUST** be non‑`EMPTY`.  These conditions
are checked against the decoded block, not a header literal‑count field.

### 3.5. Reserved END block

Block type `2` is reserved for a future explicit end‑of‑frame record.  The
minimal record is the five‑octet sequence `LZ9B 02`.  An extended `END` record
uses type/control octet `0x06`, followed by a two‑octet extension‑area length,
TLV extensions, and a four‑octet CRC‑32 covering all preceding `END`‑record
octets.  The extension area is bounded to 4,096 octets and uses the TLV
convention in section 10.2.  The precise terminal semantics and the reserved
final‑symbol truncation extension are specified in section 10.4.

The current v8 encoder does not emit `END` records and the current C and Python
decoders reject them.  For known‑length frames, the current implementation
recognizes end‑of‑frame when exactly `N` symbols have been decoded and rejects
any following octets.  The `END` syntax is reserved now so a future nonseekable
streaming implementation can add explicit termination without changing the
`LZ` or `STORED` block layouts.

## 4. Decoded sequence semantics

Each of the `q` sequences consists of one **literal‑run length** `LL`, one
**match length** `ML`, and one **offset** `OF`.  These are logically
synchronous, even though their codes occupy separate entropy streams.

For each sequence in forward order:

1. Decode `LL` and emit exactly `LL` symbols from `LIT`.

2. Decode `ML`; it **MUST** be at least three.

3. Decode `OF` to a backward **distance** `d` between 1 and the selected
   window limit (32,768 or 65,536 symbols).

4. Copy `ML` symbols from `d` logical positions behind the current output
   cursor, one symbol at a time, allowing source and destination to overlap.

Once all `q` sequences are emitted, the decoder emits terminal literals until
the block has produced exactly `n` symbols.  The total literal count is
`n - sum(ML)`; it is not transmitted in the v8 header.  The output symbol count
**MUST** be exactly `n`.  No separate end‑of‑block or
literal‑vs‑match flag exists.  The sequence count supplies framing.

The dictionary includes preceding decoded blocks in the **same frame**.
Distances **MUST NOT** reach before the start of the frame (there is no preset
dictionary), but may reach across a block boundary.  No match can reference
future symbols.

Matches referencing positions before the beginning of the stream/frame (where
`distance > produced_symbols_in_frame`) are termed **pre‑history** matches.

In **LZ9ANS**, *pre‑history* offsets are strictly illegal: decoders **MUST**
validate every match distance and reject the stream immediately if `d` exceeds
the number of already decoded symbols in the frame.  Pre‑history references
**MUST NOT** be resolved by reading uninitialized memory, by wrapping around
the history buffer, or by pre‑seeding dummy symbols (such as zeros or spaces).

### 4.1. Literal-run and match-length codes

`LL` and `ML` each use an alphabet of **32** codes numbered 0–31.  The same
conversion uses `bias=0` for `LL` and `bias=3` for `ML`.  Given non‑negative
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

For example, `LL=0` maps to code 0, `ML=3` maps to code 0, and `LL=16` maps to
code 16 with zero additional bits (`k=0`).  Codes permit larger values than
can fit into one block; decoders **MUST** reject any decoded length that
exceeds the remaining block capacity.  `ML` values larger than 32,768 cannot
occur in a valid v8 encoder output.

### 4.2. Offset alphabet, repeat slots, and RUN

`OF` has **20** codes by default and 21 codes (0–20) with a 64K window:

| OF code   | Meaning                                             | EXTRA bits    | Repeat‑slot action                |
|----------:|:----------------------------------------------------|:--------------|:----------------------------------|
| `0`       | Distance currently in slot `R0`                     | `0`           | Promote that distance to `R0`     |
| `1`       | Distance currently in slot `R1`                     | `0`           | Promote that distance to `R0`     |
| `2`       | Distance currently in slot `R2`                     | `0`           | Promote that distance to `R0`     |
| `3`       | **`RUN`:** distance exactly 1                       | `0`           | **Do not modify** repeat slots    |
| `4–18`    | Explicit distance bucket `k=code-4`, `0 <= k <= 14` | Exactly `k`   | Promote explicit distance to `R0` |
| `19`      | Explicit distance exactly **32,768**                | `0`           | Promote 32,768 to `R0`            |
| `20`      | Explicit distance 32,769–65,536 (64K window only)   | `15`          | Promote explicit distance to `R0` |

Initial slots at the beginning of each frame: **`R0=R1=R2=INVALID`**.  Code
0/1/2 is invalid unless its referenced slot has been initialized by a prior
explicit or repeat match.  Repeat slots persist across **both** `LZ` and
`STORED` block boundaries, but not across frames.  Code 3 works whenever there
is at least one previously decoded symbol; it is **not** dependent on the
repeat slots.

For explicit code `c` in 4–18, `k=c-4`:

```text
distance = 2^k + read_extra(k)
```

Thus c=4 represents distance 1 with zero bits, c=5 represents 2–3, c=6
represents 4–7, c=17 represents 8,192–16,383, and c=18 represents
16,384–32,767.  Code 19 is the exact 32,768 endpoint.  An explicit distance1
(code4) is permitted and *does* update the repeat slots; `RUN` (code3)
expresses the same distance without updating them.  This semantic distinction
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

Unlike Zstandard, **there is no special mapping or shift when `LL`=0**.  Repeat
code0 always means slot `R0`, including following a zero‑length literal run.
An encoder and decoder must implement this same rule.

### 4.3. EXTRA bit ordering

For each sequence in forward order, append the following *to the one shared
`EXTRA` stream*, with no inter‑field padding:

1. `LL` additional bits, if the `LL` code requires any.

2. `ML` additional bits, if the `ML` code requires any.

3. If aligned‑distance coding is enabled and `OF` selects an eligible explicit
   bucket (codes 6–18, or code 20 with the 64K window), one alignment flag bit.

4. `OF` additional bits, if an explicit distance bucket requires any.

The bit groups are MSB‑first.  No `EXTRA` bits are emitted for `RUN` or repeat
codes.  *Literal payloads never contribute to `EXTRA`*.  The alignment flag is
part of the raw `EXTRA` bitstream.

## 5. Entropy streams and descriptors

The entropy alphabet sizes are **512 `LIT`**, **32 `LL`**, **32 `ML`**, and
**20 `OF`** (21 `OF` with the 64K‑window flag).  `EXTRA` is not an entropy
stream; it is always a raw forward‑oriented bitstream.  Each entropy stream
independently chooses one of five descriptor modes.  The selected mode is
specified by its descriptor, not inferred from the data.

Descriptors are concatenated and parsed without alignment gaps:

| Mode byte   | Name    | Rest of descriptor                                          | Payload                                            |
|------------:|:--------|:------------------------------------------------------------|:---------------------------------------------------|
| `0`         | `EMPTY` | Nothing                                                     | Zero bits; allowed only when symbol count is zero  |
| `1`         | `RAW`   | One octet: symbol width                                     | Forward MSB‑first fixed‑width codes                |
| `2`         | `RLE`   | One unsigned varint: sole symbol value                      | Zero bits, repeat symbol for entire count          |
| `3`         | `tANS`  | Presence bitmap followed by packed normalized frequencies   | Reverse‑consumed tANS bit groups                   |
| `4`         | `REUSE` | Nothing                                                     | Reverse‑consumed tANS bit groups using prior table |

`RAW` `LIT` accepts width 7/8/9; `RAW` `LL`, `ML`, `OF` always use width **5**.
Five bits suffice for the 21‑symbol `OF` alphabet used with a 64K window; codes
21–31 are always invalid, and code 20 is invalid unless the 64K‑window flag is
set.  `EXTRA` has no entropy descriptor or RAW entropy mode; it is carried only
in the fifth raw payload.  The decoder checks that all entropy symbols lie
within the required alphabet, that mode `EMPTY` matches a zero count and no
payload, and that all meaningful payload bits are consumed.

`RLE` values use the existing unsigned little‑endian base‑128 varint.  Each
octet contributes its low seven bits; bit 7 means continue.  The decoder
permits at most three octets per integer.  Values are further bounded by the
field's semantic range.  Encoders normally emit the shortest representation.

### 5.1. Compact tANS descriptors, table reuse, and normalization

A mode‑3 `tANS` descriptor begins with a fixed‑size **presence bitmap** of
`ceil(alphabet/8)` octets.  Within each octet, the most significant bit names
the lowest‑numbered symbol in that octet.  A set bit means that symbol has a
nonzero normalized frequency.  Symbols are then enumerated in ascending
numerical order.  Unused low‑order bits in the final bitmap octet **MUST**
be zero.

The frequency stream immediately follows the bitmap.  It is a continuous
MSB‑first bitstream; it is padded with zero bits to the next octet boundary
when the descriptor ends.  Frequencies **MUST** be positive, and their sum
**MUST** be exactly **1024**.  The encoding depends on the stream kind:

- **`LIT`:** Each positive frequency is represented by *Elias gamma coding* of
  the frequency itself.  For positive integer `f`, let `k=floor(log2(f))`;
  write `k` zero bits followed by the `k+1`‑bit binary representation of `f`.
- **`LL`, `ML`, `OF`:** Frequencies use truncated‑binary coding in ascending
  symbol order.  Let `assigned` be the sum of frequencies already decoded and
  `remaining` the number of set bitmap entries not yet decoded.  For the next
  frequency, define `R = 1024 - assigned - remaining` and `x = f - 1`.
  Let `b=floor(log2(R))` and `cutoff=2^(b+1)-R`.  If `x < cutoff`, read/write
  `x` in `b` bits; otherwise read/write `x+cutoff` in `b+1` bits.  Then add
  `f` to `assigned`.

The decoder **MUST** reject a descriptor if a decoded frequency is zero, a
value exceeds the remaining normalization budget, the frequencies do not sum to
1024, fewer than two symbols are present, unused bitmap bits are nonzero, or
nonzero padding follows the final frequency.  The exact frequency normalization
algorithm used by the encoder is **not normative**; all decoders use the
serialized normalized frequencies.

Mode 4, **`REUSE`**, has no descriptor payload.  It selects the most recently
installed mode‑3 `tANS` model for the same stream kind in the current frame.
The first use of `REUSE` for a stream is invalid until that stream has
installed a model with mode 3.  A mode‑3 descriptor replaces that stream's
cached model. `EMPTY`, `RAW`, and `RLE` do not replace it.  Models persist
across `LZ` and `STORED` block boundaries within one frame, but never across
frames.  The `ANS` state itself is initialized independently for every block;
`REUSE` saves only the normalized model description, not ANS state.

The encoder may use `REUSE` only when the candidate normalized frequency vector
matches the model already installed for that stream.  If a speculative `LZ`
block is ultimately emitted as `STORED`, its untransmitted candidate models are
not considered installed.

### 5.2. Deterministic 1024-state table construction

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

### 5.3. tANS decoding and the unusual reverse bit direction

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
mode‑4 `REUSE`; `LZ` history and repeat distances also persist across blocks.

The current C implementation can be used to test all table equations;
`inspect_lz9ans.py` deliberately reconstructs the same tables without calling
any part of the C implementation.

## 6. Decoder procedure and validation requirements

A conforming v8 decoder SHOULD implement the following stages, enforcing all
checks before trusting an offset, size, state, or output count:

1. Read the 13‑octet frame prefix, validate magic/version/representation, then
   validate the declared header length (17–4,096 octets), read the remainder,
   validate the final header CRC, and parse all TLVs.  Unknown noncritical TLVs
   are skipped; unknown critical TLVs, duplicate identifiers, malformed
   lengths, and identifier zero are rejected.  The defined unknown‑count TLV
   is recognized but is not supported by the current reference decoder.

2. Initialize an empty 32,768‑ or 65,536‑symbol history as selected by the
   frame flags (zero‑initializing memory as defense‑in‑depth against memory
   disclosure) and three INVALID repeat slots.

3. For each block, read and validate the seven‑octet common prefix.  For
   `STORED`, read the remaining four header octets; for `LZ`, read the
   remaining 20 header octets.  Type 2 (`END`) and type 3 are reserved and
   rejected by the current implementation.  Validate the type‑specific fields
   and ensure the block's `n` does not exceed the symbols remaining in the
   known‑length frame.

4. For `STORED`, derive the payload bit length as `n * width`, derive its byte
   length by rounding up, validate the final zero padding bits, and read
   exactly that payload.

5. For `LZ`, validate `q <= floor(n/3)`, `dl <= 8192`, each reconstructed
   meaningful bit length against its stream‑specific maximum, the packed
   padding field's reserved bit and values, and the sum of payload byte lengths
   against 81,920.  Read exactly `dl` descriptor octets and exactly the five
   announced payload byte lengths.  Check every stream's unused low‑order pad
   bits before initializing its reader.

6. For `STORED`, read `n` fixed‑width symbols, updating history but not MRU.

7. For LZ, reconstruct exactly the `LIT`/`LL`/`ML`/`OF` descriptors.  Require
   `LL`, `ML`, and `OF` to be `EMPTY` exactly when `q` is zero.  Initialize the
   entropy readers with the exact reconstructed meaningful bit lengths,
   including the exact reverse‑reader start position for tANS.  Decode the
   `q` sequences plus the terminal literal run, consuming `EXTRA` from its raw
   bitstream.  Derive the actual literal count from the decoded match lengths;
   validate the `LIT` `EMPTY`/non‑`EMPTY` condition against that actual count.

8. For every copy, validate `1 <= distance <= window_limit`, and ensure the
   distance does not exceed the already reconstructed frame prefix
   (`distance <= produced`), rejecting all pre‑history references.  Validate
   `ML >= 3`, and verify the copy does not exceed this block's remaining output
   capacity.  Copy forwards one symbol at a time to support overlaps.

9. Verify each entropy stream consumes exactly its meaningful bits and its
   prescribed sequence‑symbol count; tANS must finish in state zero; `EXTRA`
   must be exhausted.  Verify the actual literal and block symbol counts.

10. Compute the canonical packed‑9 block CRC; reject a mismatch.

11. At the known frame symbol count, flush the mode‑9 terminal partial octet
    with zero fill; reject trailing compressed octets.  Unknown‑length frames
    and `END` records are reserved but not supported by the current decoder.

Never allocate memory directly from an unchecked on‑disk length.  The current
frame‑header and payload bounds are checked before any corresponding read or
allocation.  The Python independent decoder prioritizes clarity over bounded
memory and should not be used as a hardened service.

The current host CLI may leave a partially written output file when a
subsequent block is corrupt.  Its caller must use the exit status and should
write to a temporary name if destination atomicity is required.

## 7. Encoder reference choices (informative, not required for decoding)

**LZ9ANS** v8 uses an **8,192‑entry hash table** keyed from three consecutive
symbols and a hash‑chain ring sized to the selected history window to locate
match candidates.  The parser maintains a 32K‑symbol block plus up to 64K
history in 16‑bit C containers, records `(LL, ML, distance)` sequences, and
emits four entropy streams and the shared `EXTRA` bitstream.  The distance is
stored in a 32‑bit C field so the encoder can carry a 17‑bit distance plus the
repeat‑slot tag; this is an implementation detail, not a wire field.

The levels from `-1` through `-9` all produce **the same v8 wire format**.
The default is **level 5** and the default window remains **32,768 symbols**.
The search‑depth limit is the number of hash‑chain candidates examined; the
three repeat‑distance slots are probed separately and therefore do not consume
this budget:

| Level | Hash‑chain depth | Parser behavior                                      |
|------:|:-----------------|:-----------------------------------------------------|
| `1`   | `2`              | Greedy, no entropy‑price pass                        |
| `2`   | `4`              | Greedy, no entropy‑price pass                        |
| `3`   | `8`              | Greedy, no entropy‑price pass                        |
| `4`   | `8`              | Initial greedy pass; entropy‑price reparse           |
| `5`   | `24`             | Level 4 plus one‑position lazy choice; default       |
| `6`   | `48`             | Level 5 with deeper candidate search                 |
| `7`   | `96`             | Level 5 with deeper candidate search                 |
| `8`   | `192`            | Level 5 plus near‑longest alternative match trials   |
| `9`   | `512`            | Level 8 plus a further cost‑refinement pass          |

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
descriptor cost, and compares `tANS` with `RAW` and `RLE`.  It adds exact
extra‑bit counts for the chosen `LL`/`ML`/`OF` codes and estimates a fixed
per‑sequence overhead.  Literal sums are accelerated with a cumulative prefix
every 16 symbols.  Levels 8–9 consider slightly shorter alternatives when
costed.  When distance 1 could use either `RUN` or an initialized repeat slot,
the cost‑aware parser may choose the lower estimated offset‑code cost.

This is **bounded heuristic optimization**, not exact optimal parsing.  Prices
come from one or two block‑level frequency snapshots; the final entropy
distribution may differ after reparse.  Compression size is not mathematically
guaranteed to improve monotonically with increasing level.

For each symbol stream the encoder chooses `EMPTY`, `RLE`, `RAW`, or `TANS`
models, or `REUSE` by estimated serialized size.  `REUSE` is available only
when the normalized model equals the most recently installed model for that
stream.  If the *whole* `LZ` payload (including descriptors) is no smaller than
storing the block at its narrowest 7/8/9 bit width, it writes `STORED` instead;
speculative repeat‑slot changes and untransmitted table models are discarded.
The decoder never needs to know which parser or level was used.

Linux reference implementation explicit state on one x86‑64/8‑bit‑octet build:
**497,730 octets of encoder state** and **207,304 octets of decoder state**
as reported by `./lz9ans --memory` (plus stack, C library file buffers, and
allocator overhead).  The decoder figure includes four `AnsTable` instances;
there is no `EXTRA` entropy table.  The encoder remains below 512 KiB
(524,288 bytes) by 26,558 bytes.  Its four 512‑entry frequency vectors occupy
4,096 bytes at the tail of the existing staging allocation.  The usable staged
payload area is reduced accordingly, but remains larger than the maximum
`STORED` block.  The reported allocation totals are unchanged.  The figure
includes the `Encoder` structure and its associated arrays, but excludes small
helper stack temporaries, C runtime state, `FILE` buffers, allocator overhead,
and the process itself.  It is therefore a useful state budget, not a guarantee
of total resident memory.  This is **not** a proof of native Multics segment
occupancy: ANSI C89's 8‑bit‑octet host assumptions and Multics PL/I word
representation are different and must be sized separately.

## 8. Golden vectors and interoperability

The checked‑in `.lz9a` vectors are the byte‑exact **v8** frames produced by
the reference encoder with automatic per‑block aligned‑distance selection.  The
frames advertise feature mask `0x18`; `mixed9` also exercises an `LZ` block
whose aligned‑distance selector is set.  `test_vectors.py` verifies their
SHA‑256 digests, decodes them with both the C decoder and the independent
Python decoder, and re‑encodes the original inputs to ensure byte‑for‑byte
reproducibility.  These vectors exercise the published wire representation;
they are not intended as a general compression benchmark.  The v8 decoder
rejects every other frame version; no legacy decoding is implemented.

| Fixture       | Input mode | Level | Raw octets | Frame octets  | SHA‑256 of compressed frame                                        |
|--------------:|:-----------|:------|:-----------|:--------------|:-------------------------------------------------------------------|
| `empty8`      | `8`        | `1`   | `0`        | `17`          | `fd76cddc8175fb63f9b16c3d082ceea254bf684dc8240a1f99b0964c2cf4265f` |
| `abc8`        | `8`        | `1`   | `12`       | `39`          | `c3c7e9ce160dcc5b767b462da109be9c6c163b2508a3520639275f8f651a05f4` |
| `run8`        | `8`        | `6`   | `96`       | `53`          | `0f8e1acd5795861afdb9da92eeafb5846391a794041f9aac2ed48c8825d0c743` |
| `mixed9`      | `9`        | `6`   | `216`      | `63`          | `c8cdb0550ebe11242a3ac2b664af92652bbd3e2c4bdcaeef2d1d1719a807a1af` |
| `switch8`     | `8`        | `6`   | `1,152`    | `74`          | `fd66b1558f29ea94f648c070b6d2a9358a6cc7296daa3c4b978ed52aba6c15e5` |
| `longdist16k` | `8`        | `9`   | `32,768`   | `16,441`      | `574ffc85c9bd00c2012f8626caebee42bf88f2caf49f72ba07aa0ac7531c53c7` |
| `longdist32k` | `8`        | `9`   | `65,536`   | `32,832`      | `170a43b6fe53b2bf19e20b20419213ba87a249975c781908983ffaf2cebd970a` |

The `run8` frame uses one `LZ` sequence and **`OF` code 3** (`RUN`).  The
`mixed9` fixture exercises actual values over 255.  The `longdist16k` and
`longdist32k` fixtures exercise explicit offset codes 18 and 19, respectively;
`longdist32k` references the full preceding stored block.  Bigger real files in
the regression suite exercise all three MRU codes and literal, length, and
offset `tANS` streams.  `test_features.py` also builds a two‑block frame whose
second block reuses an installed `LIT` model, then checks both C and Python
decoding.

Independent implementations should decode the published **bytes**, not just
their own re‑encodings, and should reject the negative cases above.  For
interoperability the actual uncompressed content, *packed9* bit order, block
checksums, MRU state across block types, and tANS state termination all have
to match exactly.

## 9. Format gaps and future design choices

- **Format stability:** Version 8 is the only format emitted and accepted by
  this implementation.  It uses an extensible 17–4,096‑octet frame header,
  11-octet `STORED` headers, 27‑octet `LZ` headers, four entropy streams, and
  one raw `EXTRA` stream.  Earlier frame versions are rejected; there is no
  legacy decoding path.

- **Streaming:** The frame TLV and `END`‑record conventions reserve an explicit
  unknown‑symbol‑count representation and an unambiguous terminal record.
  The current UNIX CLI still counts input before encoding and the current
  decoder supports only known‑length frames.  Reading from stdin and writing to
  stdout, unknown‑length decoding, and `END`‑record processing are future work.

- **Final partial Multics symbol:** An `END` TLV reserves the number of
  low‑order bits to truncate from the last 9‑bit symbol.  The current UNIX
  reference implementation does not emit `END` metadata or produce
  partial‑symbol output.  Its ordinary mode‑9 output continues to contain
  complete symbols with zero‑padding to the final octet.

- **Container semantics:** ZIP / archive method numbers, independent blocks,
  indexing, and concatenated frames are outside this experimental format.

- **Authenticity:** CRC can detect accidental damage but is **not** a
  cryptographic integrity or authenticity mechanism.

- **Security considerations and "pre‑history" buffer isolation:** LZ77 codecs
  must explicitly define how to handle backward distances referencing positions
  before the beginning of the file ("pre‑history").

  **LZ9ANS** prevents this attack surface through several distinct layers:

  1. *Grammar and validation:* Pre‑history references are strictly illegal.
     Every distance `d` must satisfy `1 <= d <= produced` (where `produced` is
     the count of symbols emitted so far in the current frame).  Any stream
     with `d > produced` is malformed and **MUST** be rejected immediately
     prior to any buffer access or copy.

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
  C `char`, 16‑bit `short`, 32‑bit `int` and seekable files for its current
  known‑length CLI.  A future streaming implementation need not seek.  Any
  Multics PL/I port should carry the *wire equations* and use 36‑bit‑native
  representations, not emulate these host C types unnecessarily.

## 10. Version 8 features, extensions, and reserved control records

Version 8 is the only format emitted or accepted by this implementation.  The
fixed frame‑header prefix has version 8 at offset 4, header length at offset 6,
a one‑octet feature mask at offset 12, and a four‑octet CRC at the end of the
header.  The CRC covers every preceding frame‑header octet.  With no TLVs, the
header is 17 octets and the CRC covers offsets 0–12.

### 10.1. Frame feature flags

| Bit   | Mask   | Meaning                                                          |
|------:|:-------|:-----------------------------------------------------------------|
| `0`   | `0x01` | 65,536‑symbol history window                                     |
| `1`   | `0x02` | Reserved; **MUST** be zero                                       |
| `2`   | `0x04` | Reserved; **MUST** be zero                                       |
| `3`   | `0x08` | Aligned‑distance coding is available throughout the frame        |
| `4`   | `0x10` | Aligned‑distance coding is selected independently per `LZ` block |
| `5–7` | —      | Reserved; **MUST** be zero                                       |

Only mask `0x19` is currently defined.  Bits outside that mask **MUST** be
zero.  Bit 4 **MUST NOT** be set unless bit 3 is also set.  Bit 0 selects the
history window for the whole frame.  When bit 4 is clear, bit 3 selects
frame‑wide aligned‑distance coding; when both bits 3 and 4 are clear, ordinary
distance representation is used throughout the frame.  The reference encoder
sets bits 3 and 4 and records its decision separately in each `LZ` block.  The
maximum block size remains 32,768 symbols.  Frames whose version byte is not 8
**MUST** be rejected; no earlier format version is supported.

### 10.2. TLV extension convention

Frame‑header extensions and extended `END`‑record metadata use this TLV form:

| Field      | Octets   | Meaning                                                                    |
|:-----------|---------:|:---------------------------------------------------------------------------|
| Identifier | `1`      | Low seven bits identify the extension; high bit marks a critical extension |
| Length     | `2`      | Unsigned little‑endian number of value octets                              |
| Value      | `Length` | Extension‑specific data                                                    |

A zero low‑seven‑bit identifier value is invalid, including `0x80`.  The
low‑seven‑bit identifier value **MUST** be unique within one extension area,
even if the critical bit differs.  A malformed or out‑of‑bounds TLV is
rejected.  Unknown noncritical TLVs (high bit clear) are skipped using their
length.  Unknown critical TLVs (high bit set) **MUST** be rejected.  A decoder
**MUST** reject duplicate identifiers.

The maximum frame‑header length, including the fixed prefix, all TLVs, and the
final CRC, is 4,096 octets.  The maximum `END`‑record extension area is 4,096
octets.  The TLV namespace is shared by frame and `END` extensions, but an
extension is valid only in the location specified for it.

The following identifiers are reserved:

| Identifier | Scope          | Length | Meaning                                                                              |
|:-----------|:---------------|-------:|:-------------------------------------------------------------------------------------|
| `0x81`     | Frame header   | `0`    | Total symbol count is not known when the frame header is written; `END` is mandatory |
| `0x82`     | `END` record   | `1`    | Number of low‑order bits to truncate from the final 9‑bit symbol (0–8)               |

Both identifiers are critical.  A decoder that does not implement their
semantics **MUST** reject them rather than silently ignoring them.  The current
reference decoder recognizes `0x81` and rejects the unknown‑length form as not
yet implemented.  The current implementation does not parse `END` records, so
it rejects type 2 regardless of its extension contents.

### 10.3. Unknown-length frame extension

A future streaming encoder that cannot know the total symbol count before
writing the frame header **MUST** set the frame `N` field at offset 8 to zero
and include exactly one frame TLV with identifier `0x81`, length zero, and no
value octets.  A zero count without this extension continues to mean an
ordinary empty known‑length frame.

An unknown‑length frame **MUST** end with exactly one `END` record (section
10.4).  The total decoded symbol count is the sum of the decoded `n` values of
all `STORED` and `LZ` blocks preceding `END`.  `END` is terminal: no blocks or
octets may follow it.  Missing `END`, duplicate `END`, malformed `END`, or
data after `END` **MUST** be rejected.  These rules provide an explicit end
condition for pipes and other nonseekable streams; `EOF` alone is **not** a
valid substitute for `END`.

The current encoder does not emit `0x81`, and the current decoder rejects it.
The present v8 CLI remains a known‑length, seekable‑file implementation.

### 10.4. END record and final-symbol truncation

Block type 2 is reserved for the `END` record.  Its type/control octet uses
bits 0–1 equal to `2`; bits 3–7 **MUST** be zero.  Bit 2 indicates whether an
extension area is present:

- **Minimal `END`:** control octet `0x02`; the entire record is exactly five
  octets: ASCII `LZ9B` followed by `0x02`.
- **Extended `END`:** control octet `0x06`; followed by a two‑octet unsigned
  little‑endian extension‑area length, that many TLV octets, and a four‑octet
  CRC‑32/ISO‑HDLC.  The CRC covers the `END` magic, control octet, length
  field, and all TLV octets, excluding the CRC itself.  The extension area
  **MUST** be nonempty and no larger than 4,096 octets.

An `END` record **MUST** occur exactly once and at the end of an unknown‑length
frame.  For a known‑length frame it **MAY** be used after exactly `N` decoded
symbols when terminal metadata is required; otherwise the current form ends
immediately after the last data block.  `END` **MUST NOT** occur before the
known symbol count has been produced.  No data may follow `END`.

The reserved `END` TLV `0x82` has length one.  Its value is the number of
least‑significant bits to truncate from the final 9‑bit symbol:

| Value          | Meaning                                                  |
|---------------:|:---------------------------------------------------------|
| `0`            | Retain the complete final 9‑bit symbol                   |
| `1`            | Truncate its least‑significant bit                       |
| `2`            | Truncate its two least‑significant bits                  |
| `...`          | `...`                                                    |
| `8`            | Retain only the most‑significant bit of the final symbol |
| `9` or greater | Invalid                                                  |

An absent `0x82` TLV is equivalent to value zero.  Encoders **SHOULD** omit the
TLV when the value is zero.  The extension is valid only for a nonempty mode‑9
frame.  It is invalid for mode 8, an empty frame, or a final value outside
0–8.  The low‑order bits being truncated **MUST** already be zero in the
decoded final symbol.  The logical block data and block CRC continue to cover
the complete 9‑bit final symbol, including those zero bits; the `END` CRC
protects the truncation metadata itself.

For a mode‑9 frame containing `N > 0` symbols and truncation value `t`, the
number of meaningful output bits for a consumer that implements this metadata
is `9*N - t`.  The final symbol still decodes as a complete 9‑bit value; only
its final `t` zero bits are outside the logical source bitstream.  This makes
the metadata useful for arbitrary‑bit‑length Multics segments without
complicating the symbol/block codec.  The current UNIX reference implementation
always writes complete symbols and does not process `END` metadata or truncate
its output.

The current v8 implementation reserves this record but does not emit or accept
it.  Its C and Python decoders **MUST** reject an `END` block until `END`
processing is implemented.

### 10.5. 64K history window (`0x01`)

The window flag increases the history limit to **65,536 logical symbols**;
blocks and maximum match length remain 32,768 symbols.  The `OF` alphabet grows
from 20 to 21 symbols; repeat slots and `RUN` semantics are unchanged.  Codes
0–19 retain the ordinary offset meanings, with code 19 representing exactly
32,768.  Code 20 represents distances 32,769 through 65,536 and carries exactly
15 `EXTRA` bits:

```text
distance = 32769 + read_extra(15)
```

The encoder writes `distance - 32769` in 15 MSB‑first bits.  Code 20 is invalid
unless the 64K‑window flag is set.  With the default 32K window the `OF`
alphabet remains 20 and code 20 is invalid.  An implementation must use at
least 17 bits for the logical distance; 16‑bit storage is insufficient for
distance 65,536.

### 10.6. Aligned explicit distances (`0x08`)

With this flag, selected explicit distances carry one alignment flag in
`EXTRA`.  It is present for `OF` codes 6–18 (the explicit buckets with at least
two ordinary extra bits) and, when the 64K window is enabled, code 20.  It is
not present for repeat codes 0–2, `RUN` code 3, explicit codes 4–5, or
exact‑distance code 19.

For codes 6–18, let `k = code - 4`.  The alignment flag precedes the ordinary
distance extra bits:

- Flag 1 means the distance is divisible by four and is decoded as
  `distance = 2^k + 4 * read_extra(k - 2)`.
- Flag 0 means the ordinary representation is used:
  `distance = 2^k + read_extra(k)`.  The resulting distance **MUST NOT** be
  divisible by four; this canonicality rule prevents duplicate encodings.

For code 20 with the 64K window:

- Flag 1 means `distance = 32772 + 4 * read_extra(13)`.
- Flag 0 means `distance = 32769 + read_extra(15)` and the resulting distance
  **MUST NOT** be divisible by four.

The encoder sets the flag from the distance residue.  This feature does not
change the `OF` alphabet or its RAW width.  The additional flag is included in
the ordinary forward `EXTRA` bit order.  A decoder **MUST** reject noncanonical
flag‑0 forms whose decoded distance is divisible by four.

### 10.7. Per-block aligned-distance selection (`0x18`)

When frame feature bits 3 and 4 are both set, alignment is selected separately
for each `LZ` block.  The decoder **MUST** use aligned‑distance decoding only
when bit 4 of block‑header octet 4 is set.  For an `LZ` block this bit is
otherwise zero; bits 5–7 of that octet **MUST** be zero.  For a `STORED` block,
octet 4 continues to encode the stored symbol width in bits 2–7, and bit 4
**MUST NOT** be interpreted as an alignment selector.  The reference encoder
always emits both frame feature bits and makes a fresh selection for every
`LZ` block.

The per‑block selector is a coding decision, not a new payload stream.  The
encoder can select the aligned form using the exact raw‑`EXTRA` bit‑count
delta: let `E` be the number of eligible explicit‑distance codes (6–18, plus
code 20 with a 64K window), and let `A` be the number of those distances
divisible by four.  Aligned coding adds one selector bit for each eligible
distance and removes two residual bits for each aligned distance, so its
raw‑`EXTRA` bit delta is `E - 2*A`.  A cost‑based encoder **SHOULD** select
aligned coding only when this delta is negative; ties select the ordinary
representation.

The reference encoder requires at least one symbol with value 256–511 in the
block before considering aligned coding.  This ninth‑bit gate is a
compression‑selection heuristic, not a decoder requirement; the decoder
follows only the block‑header selector.  The encoder then selects aligned
coding only when the exact raw‑`EXTRA` bit delta is negative.  Blocks
containing only values below 256 therefore always use the ordinary distance
representation.  The selection rule applies to the single raw `EXTRA` stream.

### 10.8. v8 stream-size bounds

The following conservative per‑stream bounds justify the 16‑bit byte‑length
fields.  They include the initial ten‑bit tANS state where applicable.

| Stream  | Maximum meaningful bits               | Maximum payload octets |
|:--------|--------------------------------------:|-----------------------:|
| `LIT`   | `32,768 * 10 + 10 = 327,690`          | `40,962`               |
| `LL`    | `floor(32,768/3) * 10 + 10 = 109,230` | `13,654`               |
| `ML`    | `floor(32,768/3) * 10 + 10 = 109,230` | `13,654`               |
| `OF`    | `floor(32,768/3) * 10 + 10 = 109,230` | `13,654`               |
| `EXTRA` | `floor(32,768/3) * 46 = 502,412`      | `62,802`               |

The `LIT` tANS bound allows at most ten bits per symbol plus its ten‑bit final
state.  Each sequence entropy stream allows at most ten bits per sequence plus
its final state.  The `EXTRA` bound allows up to 15 `LL` bits, 15 `ML` bits,
and 16 `OF` bits (including the alignment selector in the 64K case) per
sequence.  Every maximum byte length is below 65,536 and therefore fits in its
unsigned 16‑bit field.  Independently, the sum of the five payload byte lengths
**MUST NOT** exceed 81,920 octets, the decoder's combined staging bound.

The descriptor length `dl` is an unsigned 16‑bit value but is additionally
bounded to 8,192 octets.  `n` and `q` are unsigned 16‑bit values bounded to
32,768 and `floor(n/3)`, respectively.  These format limits **MUST** be checked
before payload reads or entropy‑table construction.
