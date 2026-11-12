<!-- Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com> -->
<!-- Copyright (c) 2025-2026 The DPS8M Development Team -->
<!-- SPDX-License-Identifier: MIT -->
# LZ9ANS version 9 (2026‑11‑07) stream format

> Document revision **`39`** (2026‑11‑12)

<!-- toc -->

- [1. Scope and terminology](#1-scope-and-terminology)
  * [1.1. Constants](#11-constants)
- [2. 9-bit bit packing and checksums](#2-9-bit-bit-packing-and-checksums)
- [3. Frame and block headers](#3-frame-and-block-headers)
  * [3.1. Frame header (17 to 4096 octets)](#31-frame-header-17-to-4096-octets)
  * [3.2. Block prefix and type-specific headers](#32-block-prefix-and-type-specific-headers)
  * [3.3. STORED blocks](#33-stored-blocks)
  * [3.4. LZ blocks](#34-lz-blocks)
  * [3.5. END block](#35-end-block)
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
  * [7.1. Reference implementation memory usage](#71-reference-implementation-memory-usage)
  * [7.2. Optional pthread details](#72-optional-pthread-details)
- [8. Golden vectors and interoperability](#8-golden-vectors-and-interoperability)
- [9. Format gaps and future design choices](#9-format-gaps-and-future-design-choices)
- [10. Version 9 features, extensions, and control records](#10-version-9-features-extensions-and-control-records)
  * [10.1. Frame feature flags](#101-frame-feature-flags)
  * [10.2. TLV extension convention](#102-tlv-extension-convention)
  * [10.3. Unknown-length frame extension](#103-unknown-length-frame-extension)
  * [10.4. END record and final-symbol truncation](#104-end-record-and-final-symbol-truncation)
  * [10.5. 64K history window (`0x01`)](#105-64k-history-window-0x01)
  * [10.6. Aligned explicit distances (`0x08`)](#106-aligned-explicit-distances-0x08)
  * [10.7. Per-block aligned-distance selection (`0x18`)](#107-per-block-aligned-distance-selection-0x18)
  * [10.8. V9 stream-size bounds](#108-v9-stream-size-bounds)
  * [10.9. Phased entropy tables (`0x02`)](#109-phased-entropy-tables-0x02)
  * [10.10. Two-symbol matches (`0x04`)](#1010-two-symbol-matches-0x04)
  * [10.11. Independent-block encoding in the reference implementation](#1011-independent-block-encoding-in-the-reference-implementation)

<!-- tocstop -->

## 1. Scope and terminology

**LZ9ANS** compresses a stream of **symbols** with values from **0 to 511**.
Every symbol occupies nine meaningful bits logically, regardless of how it
is represented in the compressor's working RAM.

* The UNIX host tool supports both 8‑bit and 9‑bit inputs:
  - **Mode 8:** "Ordinary" UNIX binary files stored with one logical symbol per
    8‑bit octet.  This is a subset of the supported 9‑bit alphabet using only
    values 0–255.
  - **Mode 9:** A packed MSB‑first stream of consecutive 9‑bit symbols.  Eight
    complete symbols occupy nine octets.  A final incomplete group is
    zero‑padded to an octet boundary.  The `END` truncation extension defines
    how a consumer may later shorten the meaningful bit count in the final
    symbol; the current C implementation accepts only the zero‑truncation value
    and does not emit partial‑symbol metadata.

**Bit position 0** of any packed stream denotes the most significant bit of
its first octet.  *Within the compressed format*, headers and descriptors are
always represented as **8‑bit octets** and integer fields are little‑endian.
Input symbol width and compressed header byte width are distinct concepts.

The format intentionally **does** **not** attach file names, timestamps,
source‑system types, record boundaries, or word attributes.

The current UNIX reference implementation preserves complete 9‑bit symbols
only; the defined `END` metadata specifies how an implementation that supports
partial symbols can represent an arbitrary final bit count.

### 1.1. Constants

| Identifier           |   Value  | Meaning                                              |
|---------------------:|---------:|:-----------------------------------------------------|
| `BLOCK_N`            | `32,768` | Maximum logical output symbols in a block            |
| `WINDOW_DEFAULT`     | `32,768` | Default maximum backward match distance, in symbols  |
| `WINDOW_64K`         | `65,536` | Optional maximum backward match distance, in symbols |
| `MIN_MATCH_DEFAULT`  | `3`      | Default minimum matched symbols                      |
| `MIN_MATCH_SHORT`    | `2`      | Minimum when frame flag `0x04` is set                |
| `MAX_MATCH`          | `32,768` | Maximum matched symbols in one sequence              |
| `TABLE_LOG`          | `10`     | Base‑two log of tANS state count                     |
| `TABLE_N`            | `1,024`  | tANS states, numbered 0–1023                         |
| `STREAM_COUNT`       | `5`      | `LIT`, `LL`, `ML`, `OF`, raw `EXTRA` payload         |
| `MAX_DESC_OCTETS`    | `8,192`  | Maximum total entropy descriptors in a block         |
| `MAX_PAYLOAD_OCTETS` | `81,920` | Maximum combined compressed substreams in a block    |
| `FRAME_HEADER_MAX_N` | `4,096`  | Maximum frame‑header length, including TLVs and CRC  |

The block limit is 32,768 symbols in V9.  The default window is 32,768 symbols;
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
zero unused bits.  If `N` is the symbol count and no `END` truncation
metadata is applied, packed mode‑9 output occupies `ceil(9*N/8)` octets;
mode‑8 output occupies `N` octets.  The final‑symbol truncation extension is
defined in section 10.4.

The frame‑header CRC covers **all header octets preceding its final four‑octet
CRC field** (the first 13 octets when there are no extensions).  Each block CRC
covers its **decoded logical symbols re‑packed as a fresh 9‑bit MSB‑first
stream**, zero‑padded at the *end of that block*, regardless of the frame's
mode 8 or 9.  Therefore a mode‑8 file's block CRC is **not** the CRC of its
ordinary input octets.  When mode‑9 blocks share a frame, the final octet
padding is applied anew for each block CRC, but the actual frame output
packing carries unused bits across block boundaries without introducing gaps.

CRC algorithm: CRC‑32/ISO‑HDLC (reflected polynomial `0xEDB88320`, initial
state `0xFFFFFFFF`, process bits least‑significant‑first **within each already
constructed octet**, final XOR `0xFFFFFFFF`).  This equals the usual zlib
`crc32()` over the prescribed octets.

## 3. Frame and block headers

Every V9 frame begins with a frame header whose length is 17–4,096 octets.
The header is followed by block records.  A known‑length frame's blocks produce
exactly the symbol count in the frame header and are followed by EOF or one
optional terminal `END` record.  A zero‑symbol known‑length frame has no data
blocks.  The reference encoder emits the unknown‑length form for `stdin` input,
and the C decoder accepts it from either files or `stdin`, as specified in
section 10.  All integer fields are unsigned and little‑endian except the ASCII
magic bytes and single‑byte control/feature fields.

### 3.1. Frame header (17 to 4096 octets)

The fixed 13‑octet prefix is followed by zero or more TLV
(*type*/*length*/*value*) extensions and a four‑octet CRC.

With **no** extensions, the header is exactly 17 octets.

| Offset              | Octets   | Required value / interpretation                                         |
|--------------------:|:--------:|:------------------------------------------------------------------------|
| `0`                 | `4`      | ASCII `LZ9A` (`4c 5a 39 41`)                                            |
| `4`                 | `1`      | Version **`9`**                                                         |
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
symbols equals `N`; the decoder then accepts EOF or one optional, valid `END`
record.  Any other trailing octets are invalid.  The `0x81` unknown‑count
extension identifies a streaming frame in which `N` is zero and an `END` record
is mandatory.  The C decoder supports both forms and rejects a missing `END`,
a duplicate `END`, or any octets after `END`.

For compression, the UNIX CLI reference implementation uses a known‑length
frame when the input is a named file: it counts the input before encoding,
which requires a seekable input.  When the input name is `-`, it reads `stdin`
incrementally and emits an unknown‑length frame without seeking.  Input is
consumed in block‑sized batches; this is a streaming file format, not a
low‑latency interactive record protocol. The output name may independently be
`-` for `stdout`.  Decompression accepts either frame form from a file or
`stdin` and can write to a file or `stdout`.  Size/ratio reporting with `-v` is
disabled when UNIX `stdin` or `stdout` streams are used.

### 3.2. Block prefix and type-specific headers

Every data block (`STORED` or `LZ`) begins directly with its one–octet control
field.  There is no per–block ASCII signature or common magic prefix.  The
containing frame is identified by its `LZ9A` frame header, and the frame's
version byte determines the block layout.  The `END` control record is also a
one–octet control value before any optional extension length.

| Offset | Octets | Interpretation                                            |
|-------:|-------:|:----------------------------------------------------------|
| `0`    | `1`    | Block type and type–specific flags/width                  |
| `1`    | `2`    | `n`: decoded logical symbols in this data block, 1–32,768 |

The two–octet `n` field applies only to `STORED` and `LZ` data blocks; an `END`
record consists of its control octet and, when extended, the extension‑length,
TLV, and CRC fields described in section 3.5.  The low two bits of octet 0
identify the block type: `0=STORED`, `1=LZ`, and `2=END`; type 3 is reserved
and invalid.  For data blocks, the current reader reads the three–octet prefix
first, then reads the remainder of the seven–octet `STORED` header or 23–octet
`LZ` header.  After the data blocks, it checks for the required `END` record in
an unknown‑length frame or an optional `END` record in a known‑length frame.
There is no per–block version or header–length field.

A `STORED` block has a **7–octet** header:

| Offset | Octets | Interpretation                                                     |
|-------:|-------:|:-------------------------------------------------------------------|
| `0`    | `1`    | Type and stored symbol width (`width << 2`)                        |
| `1`    | `2`    | `n`: decoded logical symbols                                       |
| `3`    | `4`    | CRC–32/ISO–HDLC of this block's decoded canonical packed–9 symbols |

For a `STORED` block, octet 0 bits 0–1 are zero and bits 2–7 encode the
symbol width as `width << 2`.  The resulting octet values are `0x1c` for width
7, `0x20` for width 8, and `0x24` for width 9.  No other width encoding is
valid.  The payload immediately follows the 7–octet header.

An `LZ` block has a **23–octet** header:

| Offset | Octets | Interpretation                                                                    |
|-------:|-------:|:----------------------------------------------------------------------------------|
| `0`    | `1`    | Type and type–specific flags                                                      |
| `1`    | `2`    | `n`: decoded logical symbols                                                      |
| `3`    | `2`    | `q`: number of `LZ` match sequences, 0–`floor(n/m)` (`m` = minimum match, 3 or 2) |
| `5`    | `2`    | `dl`: total descriptor length, 0–8,192 octets                                     |
| `7`    | `2`    | `LIT` payload byte length                                                         |
| `9`    | `2`    | `LL` payload byte length                                                          |
| `11`   | `2`    | `ML` payload byte length                                                          |
| `13`   | `2`    | `OF` payload byte length                                                          |
| `15`   | `2`    | `EXTRA` payload byte length                                                       |
| `17`   | `2`    | Five packed three–bit padding counts; bit 15 is reserved and **MUST** be zero     |
| `19`   | `4`    | CRC–32/ISO–HDLC of this block's decoded canonical *packed–9* symbols              |

For an `LZ` block, octet 0 bits 0–1 equal `1`; bits 2–3 **MUST** be zero; bit
4 is the per–block aligned–distance selector when the frame advertises that
feature; and bits 5–7 **MUST** be zero.  The `n`, `q`, and `dl` fields are
unsigned 16–bit little–endian values.

Each payload byte length is an unsigned 16–bit value.  The corresponding
meaningful bit length is reconstructed as:

```text
meaningful_bits[i] = 8 * payload_bytes[i] - padding_bits[i]
```

The five padding counts occupy bits 0–14 of the padding field in stream order:
`LIT` in bits 0–2, `LL` in bits 3–5, `ML` in bits 6–8, `OF` in bits 9–11,
and `EXTRA` in bits 12–14.  Each count is 0–7.  If a stream byte length is
zero, its padding count **MUST** be zero.  A decoder **MUST** reject a padding
count larger than the stream's total bits, nonzero reserved bit 15, or nonzero
unused low–order bits in the last payload octet.

The individual byte lengths are bounded by the coding limits in section 10;
the combined five payloads **MUST NOT** exceed 81,920 octets.  All five
payloads begin on separate octet boundaries, including zero–length payloads
(which occupy no octets).  The payload order is `LIT`, `LL`, `ML`, `OF`,
`EXTRA`.

### 3.3. STORED blocks

A `STORED` block has type `0`, width 7, 8, or 9, and the 7‑octet header
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

An `LZ` block has type `1`, width bits 2–3 equal to zero, and the 23‑octet
header specified above.  Immediately after the header are:

1. Exactly four entropy descriptors in the order `LIT`, `LL`, `ML`, `OF`,
   occupying exactly `dl` octets in total.  There is no `EXTRA` descriptor.
2. The five payload streams in the order `LIT`, `LL`, `ML`, `OF`, `EXTRA`,
   each occupying the byte length announced in the header.

`LIT`, `LL`, `ML`, and `OF` are the four entropy streams.  `EXTRA` is always a
raw, forward‑oriented bitstream containing only the additional bits required
by length and explicit‑distance codes.  Its meaningful bit length is carried
in the same byte‑length/padding representation as the other streams.

There is no `lc` field in V9.  The actual literal count is derivable from the
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

### 3.5. END block

Block type `2` is the explicit end‑of‑frame record used by unknown‑length
frames.  The minimal record is the single control octet `0x02`.  An extended
`END` record uses type/control octet `0x06`, followed by a two–octet
extension–area length, TLV extensions, and a four–octet CRC–32 covering the
control octet and all following `END`–record fields preceding the CRC.  The
extension area is bounded to 4,096 octets and uses the TLV convention in
section 10.2.  Terminal semantics and final‑symbol truncation are specified in
section 10.4.

The reference encoder emits the minimal `0x02` record after every
unknown‑length frame.  The C decoder requires that record for an unknown‑length
frame, validates its terminal position, and also accepts an optional `END`
after the symbol count of a known‑length frame.  It parses the extended form
and its CRC.  Its `0x82` handling currently accepts only value zero; nonzero
partial‑symbol truncation is not implemented.

The Python inspection decoder is a known‑length‑only test tool and does not
decode streaming frames or `END` records.

## 4. Decoded sequence semantics

Each of the `q` sequences consists of one **literal‑run length** `LL`, one
**match length** `ML`, and one **offset** `OF`.  These are logically
synchronous, even though their codes occupy separate entropy streams.

For each sequence in forward order:

1. Decode `LL` and emit exactly `LL` symbols from `LIT`.
2. Decode `ML`; it **MUST** be at least `m`, the frame's minimum match length
   (3 by default, or 2 when flag `0x04` is set).
3. Decode `OF` to a backward **distance** `d` between 1 and the selected
   window limit (32,768 or 65,536 symbols).
4. Copy `ML` symbols from `d` logical positions behind the current output
   cursor, one symbol at a time, allowing source and destination to overlap.

Once all `q` sequences are emitted, the decoder emits terminal literals until
the block has produced exactly `n` symbols.  The total literal count is
`n - sum(ML)`; it is not transmitted in the V9 header.  The output symbol count
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
conversion uses `bias=0` for `LL` and `bias=m` for `ML`, where `m` is the
frame's minimum match length: **3**, or **2** when the two‑symbol‑match flag
`0x04` is set (section 10.10).  Given non‑negative `t = length - bias`:

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

For example, `LL=0` maps to code 0, `ML=m` maps to code 0, and `LL=16` maps to
code 16 with zero additional bits (`k=0`).  Decoded `LL` or `ML` codes
outside the range 0–31 are invalid; decoders **MUST** reject such a code
before reading the corresponding length's `EXTRA` bits.  Codes permit larger
values than can fit into one block; decoders **MUST** reject any decoded
length that exceeds the remaining block capacity.  `ML` values larger than
32,768 cannot occur in a valid V9 encoder output.

### 4.2. Offset alphabet, repeat slots, and RUN

`OF` has **20** codes by default and 21 codes (0–20) with a 64K window:

| OF code   | Meaning                                             | EXTRA bits    | Repeat‑slot action                |
|----------:|:----------------------------------------------------|:-------------:|:----------------------------------|
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

Unlike **Zstandard**, **there is no special mapping or shift when `LL`=0**.
*This is a deliberate design choice*.  Repeat code0 **always** means slot `R0`,
including following a zero‑length literal run.  An encoder and decoder must
implement this same rule.

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
independently chooses one of six descriptor modes (mode 5 only when the
phased‑table flag is set).  The selected mode is
specified by its descriptor, not inferred from the data.

Descriptors are concatenated and parsed without alignment gaps:

| Mode byte   |  Name    | Rest of descriptor                                          | Payload                                                 |
|------------:|:--------:|:------------------------------------------------------------|:--------------------------------------------------------|
| `0`         | `EMPTY`  | Nothing                                                     | Zero bits; allowed only when symbol count is zero       |
| `1`         | `RAW`    | One octet: symbol width                                     | Forward MSB‑first fixed‑width codes                     |
| `2`         | `RLE`    | One unsigned varint: sole symbol value                      | Zero bits, repeat symbol for entire count               |
| `3`         | `tANS`   | Presence bitmap followed by packed normalized frequencies   | Reverse‑consumed tANS bit groups                        |
| `4`         | `REUSE`  | Nothing                                                     | Reverse‑consumed tANS bit groups using prior table      |
| `5`         | `PHASED` | Partition id, then one sub‑descriptor per group (10.9)      | Reverse‑consumed tANS bit groups, table chosen by phase |

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
  frequency, define `R = 1024 - assigned - remaining`,
  `b = floor(log2(R))`, and `cutoff = 2^(b+1) - R`.  To encode the frequency,
  let `x = f - 1`: if `x < cutoff`, write `x` in `b` bits; otherwise write
  `x + cutoff` in `b+1` bits.  To decode, read `b` bits as integer `v`.  If
  `v < cutoff`, set `x = v`; otherwise read one additional bit `bit` and set
  `x = ((v << 1) | bit) - cutoff`.  The decoded frequency is `f = x + 1`;
  add `f` to `assigned`.

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

The encoder may use `REUSE` whenever the installed model assigns a nonzero
frequency to every symbol that occurs in the stream; the candidate normalized
frequency vector need not equal the installed one.  (A symbol with frequency
zero cannot be coded, so a stream that needs one cannot be produced by an
encoder and is rejected by decoders as a corrupt ANS stream.)  If a
speculative `LZ` block is ultimately emitted as `STORED`, its untransmitted
candidate models are not considered installed.

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

Decoders are free to store the resulting `(symbol, nbits, baseline)` triple of
every state in any layout.  The reference decoder packs each triple into one
32‑bit entry (baseline in bits 0–9, `nbits` in bits 10–13, symbol in bits
14–23) so that a decoded symbol costs a single table load.

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

A conforming V9 decoder **SHOULD** implement the following stages, enforcing
**all** **checks** *before* trusting an offset, size, state, or output count:

1.  Read the 13‑octet frame prefix, validate magic/version/representation, then
    validate the declared header length (17–4,096 octets), read the remainder,
    validate the final header CRC, and parse all TLVs.  Unknown noncritical
    TLVs are skipped; unknown critical TLVs, duplicate identifiers, malformed
    lengths, and identifier zero are rejected.  The `0x81` unknown‑count TLV is
    supported when its length is zero and the frame symbol count is zero.
2.  Initialize an empty 32,768‑ or 65,536‑symbol history as selected by the
    frame flags (zero‑initializing memory as defense‑in‑depth against memory
    disclosure) and three `INVALID` repeat slots.
3.  Read the first control octet.  Type 2 is `END`: validate its control value
    and terminal record as specified in section 10.4; it is mandatory for an
    unknown‑length frame and optional after exactly `N` symbols in a
    known‑length frame.  Reject type 3.  For type 0 (`STORED`) or type 1 (`LZ`),
    read the remaining two octets of the data‑block prefix, then the remaining
    four or 20 octets of the corresponding block header.  For known‑length
    data blocks, ensure `n` does not exceed the symbols remaining in the frame.
4.  For `STORED`, derive the payload bit length as `n * width`, derive its byte
    length by rounding up, validate the final zero padding bits, and read
    exactly that payload.
5.  For `LZ`, validate `q <= floor(n/m)` (`m` = 3, or 2 with flag `0x04`),
    `dl <= 8192`, each reconstructed
    meaningful bit length against its stream‑specific maximum, the packed
    padding field's reserved bit and values, and the sum of payload byte
    lengths against 81,920.  Read exactly `dl` descriptor octets and exactly
    the five announced payload byte lengths.  Check every stream's unused
    low‑order pad bits before initializing its reader.
6.  For `STORED`, read `n` fixed‑width symbols into history, *without* updating
    MRU or committing them to the destination stream yet.
7.  For `LZ`, reconstruct exactly the `LIT`/`LL`/`ML`/`OF` descriptors.
    Require `LL`, `ML`, and `OF` to be `EMPTY` exactly when `q` is zero.
    Initialize the entropy readers with the exact reconstructed meaningful bit
    lengths, including the exact reverse‑reader start position for tANS.
    Decode the `q` sequences plus the terminal literal run, consuming `EXTRA`
    from its raw bitstream.  Derive the actual literal count from the decoded
    match lengths; validate the `LIT` `EMPTY`/non‑`EMPTY` condition against
    that actual count.
8.  For every copy, validate `1 <= distance <= window_limit`, and ensure the
    distance does not exceed the already reconstructed frame prefix
    (`distance <= produced`), rejecting all pre‑history references.  Validate
    `ML >= m`, where `m` is 3 by default or 2 when flag `0x04` is set, and
    verify the copy does not exceed this block's remaining output capacity.
    Preserve forward‑copy semantics for overlapping matches;
    use chunked copies only when the source and destination ranges are
    disjoint.
9.  Verify each entropy stream consumes exactly its meaningful bits and its
    prescribed sequence‑symbol count; tANS must finish in state zero; `EXTRA`
    must be exhausted.  Verify the actual literal and block symbol counts.
10. Compute the canonical packed‑9 block CRC from the reconstructed history
    ring; reject a mismatch.  Only after the CRC succeeds, pack and commit this
    block's symbols to the destination stream.  Mode‑9 output packing carries
    its partial‑byte state across block boundaries, independently of the
    per‑block CRC packing boundary.
11. For a known‑length frame, after exactly `N` symbols accept either EOF or
    one optional `END` record; validate the record if present.  For an
    unknown‑length frame, continue decoding data blocks until the required
    `END` record and validate it.  In either form, flush the mode‑9 terminal
    partial octet with zero fill and reject any octets after the terminal
    position.

Never allocate memory directly from any unchecked on‑disk length.  The current
frame‑header and payload bounds are checked before any corresponding read or
allocation.

**NB**: The Python implementation of the decoder prioritizes code clarity over
performance and is not designed to maintain bounded memory usage.
It should **not** be considered safe to use as a hardened service.

The current UNIX host CLI tool may leave output from earlier, successfully
validated blocks in the destination file when a subsequent block is corrupt.
No symbols from a block with an invalid CRC are committed.  *This is an
intentional design choice*.  Any caller **MUST** check and use the exit status
and **SHOULD** write to a temporary output name if destination atomicity is
required.

## 7. Encoder reference choices (informative, not required for decoding)

**LZ9ANS** V9 uses an **8,192‑entry hash table** keyed from three consecutive
symbols and a hash‑chain ring sized to the selected history window to locate
match candidates.  The parser maintains a 32K‑symbol block plus up to 64K
history in 16‑bit C containers, records `(LL, ML, distance)` sequences, and
emits four entropy streams and the shared `EXTRA` bitstream.  The distance is
stored in a 32‑bit C field so the encoder can carry a 17‑bit distance plus the
repeat‑slot tag; this is an implementation detail, not a wire field.

The levels from `-1` through `-9` all produce **the same V9 wire format**.
The default is **level 5** and the default window size is **32,768 symbols**.

The search‑depth limit is the number of hash‑chain candidates examined; the
three repeat‑distance slots are probed separately and therefore do not consume
this budget:

| Level | Hash‑chain depth | Parser behavior                                     |
|------:|-----------------:|:----------------------------------------------------|
| `1`   | `2`              | Greedy, no entropy‑price pass                       |
| `2`   | `4`              | Greedy, no entropy‑price pass                       |
| `3`   | `8`              | Greedy, no entropy‑price pass                       |
| `4`   | `12`             | Initial greedy pass with entropy‑price reparse      |
| `5`   | `24`             | Level 4 plus one‑position lazy choice (**default**) |
| `6`   | `48`             | Level 5 with deeper candidate search                |
| `7`   | `96`             | Level 6 with deeper candidate search                |
| `8`   | `192`            | Level 7 plus near‑longest alternative match trials  |
| `9`   | `512`            | Level 8 plus a further cost‑refinement pass         |

The match finder explicitly probes `R0`, `R1`, and `R2` before walking the hash
chain, so repeat distances remain candidates even when their chain entries
fall outside the configured depth.  During both greedy and cost‑aware searches,
a candidate is skipped when it differs at the best already‑known match
length; this cannot improve the current match and avoids extending and pricing
dominated candidates.  The prune is applied only when that comparison is within
the available input and candidate history.

The reference encoder normalizes each active symbol histogram to a total
frequency of 1024 using a deterministic, integer‑valued, KL‑oriented
normalizer.  It first establishes positive lower and upper frequency bounds
for each active symbol, starts from the upper‑bound allocation, and considers
the possible one‑count decrements.  For a symbol with observed count
`c_i`, decrementing its current frequency from `j` to `j-1` has marginal cost
proportional to `c_i * ln(j/(j-1))`.  The encoder selects the least‑cost
decrements needed to make the frequency sum exactly 1024.  The implementation
compares these costs using precomputed Q64 fixed‑point logarithms and a bounded
integer selection procedure.

This is a deterministic fixed‑precision approximation to the KL‑optimal
positive integer allocation; it is an implementation detail and it does not
change the descriptor syntax.  The normalization algorithm remains
non‑normative as stated in Section 5.1.

The cost estimator produces per‑symbol prices in fixed‑point **Q8 bits** (256
cost units per bit).  It counts codes in a preliminary parse, normalizes their
tANS frequencies to 1024 using this normalizer, estimates negative log
probability plus an amortized descriptor cost, and compares `tANS` with `RAW`
and `RLE`.  It adds exact extra‑bit counts for the chosen `LL`/`ML`/`OF` codes
and estimates a fixed per‑sequence overhead.  Literal sums are accelerated with
a per‑symbol cumulative prefix array containing `BLOCK_N + 1` entries (32,769
entries for the current 32K‑symbol block), allowing each literal‑range cost to
be computed in constant time.  Levels 8–9 consider slightly shorter
alternatives when costed.  When distance 1 could use either `RUN` or an
initialized repeat slot, the cost‑aware parser may choose the lower estimated
offset‑code cost.

This is **bounded heuristic optimization**, not exact optimal parsing.  Prices
come from one or two block‑level frequency snapshots; the final entropy
distribution may differ after reparse.  Compression size is not mathematically
guaranteed to improve monotonically with increasing level.

At levels 4–9, the preliminary parse uses search depth 2 with lazy matching
disabled; the configured level's normal search depth and lazy‑matching policy
are restored for the cost‑guided parse(s).  After that preliminary parse, the
encoder may bypass the cost‑guided parse(s) and emit a `STORED` block when at
least `LZ9ANS_EARLY_STORED_LITERAL_PERCENT` percent of the block is represented
as literals and the estimated `LIT` stream cost alone is no smaller than the
complete `STORED` data payload.  This intentionally conservative test reuses
the encoder's existing 512‑entry symbol‑count array and does not allocate
additional block storage.  The compile‑time threshold defaults to 97; setting
`LZ9ANS_EARLY_STORED_LITERAL_PERCENT=98` selects the 98‑percent variant, and
setting it to zero disables the heuristic.  The threshold changes only encoder
selection, not the V9 stream format or decoder requirements.

When lazy matching is enabled (levels 5–9), the encoder may compare a match
with a match beginning one symbol later.  This lookahead is attempted only when
the current match length is less than the configured threshold.  The threshold
is 64 by default, except at level 8, where the compile‑time macro
`LZ9ANS_L8_LAZY_MAX_LENGTH` controls it and defaults to 32.  The macro must be
at least 4.  It changes only the level‑8 encoder heuristic, not the V9 stream
format or decoder requirements.

For each symbol stream the encoder chooses `EMPTY`, `RLE`, `RAW`, or `TANS`,
or a cached `REUSE` model.  At levels 1–5, ordinary `REUSE` is emitted only
when the normalized model exactly matches the most recently installed model
for that stream.  At levels 6–9, the encoder may additionally trial–encode the
stream with a cached model that covers every symbol used; it selects that
candidate only when its actual payload plus one–octet `REUSE` descriptor is
smaller than the fresh–table payload plus its descriptor.  Phased groups have
independent `REUSE` slots as specified in section 10.9.  If the *whole* `LZ`
payload (including descriptors) is no smaller than storing the block at its
narrowest 7/8/9 bit width, it writes `STORED` instead; speculative repeat–slot
changes and untransmitted table models are discarded.  The decoder never needs
to know which parser or level was used.

### 7.1. Reference implementation memory usage

The non–pthread UNIX reference implementation's explicitly allocated state on
standard Linux x86_64 for the default 32K window size is **678,686 octets of
maximum encoder state** (levels 6–9), **634,990 octets of encoder state at
levels 1–5**, and **213,596 octets of maximum decoder state** when phased tables
are used.  The decoder's base allocation, before any phased descriptor is
encountered, is **188,996 octets**.  These figures can be verified with
`./lz9ans --memory`.

For the 64K window size, `./lz9ans --memory-64k` reports **875,294 octets of
maximum encoder state** (levels 6–9), **831,598 octets at levels 1–5**, and
**279,132 octets of maximum decoder state**.  The decoder's 64K base allocation
is **254,532 octets**.

*These figures describe explicitly allocated codec state, not total process
resident memory.*  The report excludes allocator metadata, C library and I/O
buffers, and helper stack temporaries.  The static CRC lookup table contains
8,192 bytes and is reported separately; it is not included in the encoder or
decoder allocated–state figures.

Encoder sequence storage is sized from the frame's minimum match length.  The
maximum allocation is used at levels 6–9, where two–symbol matches are enabled.
At levels 1–5, where the minimum match length is three, the sequence allocation
is smaller by 43,696 bytes per encoder context on this platform.  The
`--memory` reports show both values; the maximum is used for the primary
encoder–state figure.

The decoder always allocates four persistent per–stream transition tables and
one poison table.  The six phased–group transition tables (4,100 bytes each on
this platform) are allocated together, exactly once, when the first actual
`MODE_PHASED` descriptor is encountered.  They remain allocated for the rest
of that frame so that phased model reuse across blocks remains available; they
are not allocated and freed per block.  Thus the report shows both the base
allocation and the maximum allocation including all six optional tables.

In a pthread build, the report also accounts for the maximum per–participant
encoder contexts and private block–output buffers, shared history and input
scratch, thread–pool allocations, and worker–stack budgets when independent
block encoding is selected.  It reports both the maximum parallel–block encoder
state (levels 6–9) and the reduced level 1–5 state.  The threaded decoder's
three LL/ML/OF symbol buffers are allocated together on first use and retained
until decoding ends.  Their capacity is based on the frame's minimum match
length: the maximum is 49,152 bytes at levels 6–9, and 32,766 bytes at levels
1–5.  Thread–pool allocations and the requested/effective worker–stack budgets
are reported separately from the decoder's base and maximum allocated state.

The reported values are not a proof of native Multics segment occupancy:
ANSI C89 type widths (8–bit–octet size assumed) and Multics PL/I word
representations are different and must be sized separately.

### 7.2. Optional pthread details

The `--threads=N` limit (includes the main) thread: `0` (zero) means automatic
sizing, `1` (one) selects the original sequential encoder, and larger positive
values cap the number of concurrent threads.  The encoder uses
independent‑block encoding automatically when more than one logical processor
is available and the effective limit exceeds one; otherwise it uses the
original sequential encoder.  Participants are bounded by the available logical
processors and, for known‑length input, the number of input blocks.  The
maximum effective participant count is 32 on 32‑bit builds and 128 on 64‑bit
builds. Larger explicit requests are silently clamped to that maximum, and
automatic sizing is subject to the same cap.  These limits include the main
thread.  The default serial build accepts and validates this option but ignores
it; only the pthread‑enabled build advertises it in command help.

Threading is an *implementation detail* and does not alter any V9 frame, block,
descriptor, checksum, or entropy‑coding rule.  With more than one effective
compression participant, the coordinator partitions the input into existing
V9 blocks and dispatches complete block jobs.  Each block is parsed against a
private snapshot of preceding uncompressed frame history (up to the selected
32K or 64K window), including symbols from `STORED` blocks.  A worker uses
fresh match‑finder, repeat‑distance, and ANS model‑cache state.  Explicit
distances establish repeat slots in the same order within the current block,
so only within‑block repeat‑slot references are emitted.  ANS model reuse
within the block remains available; cross‑block model reuse is not used.  This
removes cross‑block coding‑state dependencies while preserving cross‑block
`LZ` matches.  Workers return complete block records in private memory; the
coordinator is the only code that accesses input/output streams and emits
records in original order.  Consequently, thread scheduling does not affect
the compressed bytes.  The independent‑block strategy can produce different
bytes and ratios from the original sequential encoder, but the result is an
ordinary V9 frame readable by the existing decoder.

`LL`, `ML`, and `OF` symbols can be decoded concurrently for ordinary
descriptors, followed by sequential `EXTRA` processing and history
reconstruction.  If either `LL` or `ML` uses a phased descriptor, the decoder
uses its sequential path.  A phased `LIT` descriptor alone does not require
that fallback.

In a pthread build, `--memory`, `--memory-32k`, and `--memory-64k` accept an
optional `--threads=N` and report the effective compression/decompression
participant limits, the per‑block encoder contexts and output buffers when
block encoding is selected, the maximum 49,155‑octet decoder symbol arrays
(allocated lazily only for ordinary `LL`/`ML` blocks), and separately budgeted
worker stacks.  Each additional pthread is given a 262,144‑octet stack; this
operating‑system‑managed stack reservation is reported separately from
LZ9ANS‑managed heap allocations.  The independent‑block encoder contexts are
allocated only when the pthread build selects more than one compression
participant.  A pthread build using `--threads=1` (or running where only one
logical processor is available) uses the original encoder path and does not
allocate block‑worker contexts.  On Linux x86_64, a pthread build with
`--threads=1` reports **678,690 octets** (32K window) and **875,298 octets**
(64K window) of encoder state, 12 octets above the respective serial figures
in section 7.1.  The pthread build stores `Encoder.prefix` as a pointer and
separately counts its 131,076‑octet backing array, whereas the serial build
embeds that array in `Encoder`; the 12‑octet net difference is unrelated to
worker‑context or output‑buffer allocation.

## 8. Golden vectors and interoperability

The checked–in `.lz9a` vectors are byte–exact **V9** frames produced by the
reference encoder with automatic per–block aligned–distance selection.
Fixtures encoded at levels 1–5 advertise feature mask `0x18`; fixtures encoded
at levels 6–9 advertise `0x1e`.  Phased tables and two–symbol matches are
selected automatically by compression level; there are no command–line
suppression switches.  `mixed9` also exercises an `LZ` block whose
aligned‑distance selector is set.  `test_vectors.py` verifies their SHA‑256
digests, decodes them with both the C decoder and the independent Python
decoder, and re‑encodes the original inputs to ensure byte‑for‑byte
reproducibility.  These vectors exercise the published wire representation;
they are not intended as a general compression benchmark.  The V9 decoder
rejects every other frame version; no legacy decoding is implemented.

| Fixture       | Input mode | Level | Raw octets | Frame octets  | SHA–256 of compressed frame                                        |
|--------------:|:----------:|:-----:|-----------:|--------------:|:-------------------------------------------------------------------|
| `empty8`      | `8`        | `1`   | `0`        | `17`          | `c2cc46e5e963872f68ee43ff3592cf72320110c97f0d270cc11ce0a2782e1d84` |
| `abc8`        | `8`        | `1`   | `12`       | `35`          | `775135fe7d9a110e1c4c0e7b46612fe292c174500c0a3bc7cb921d5bcafc0bc5` |
| `run8`        | `8`        | `6`   | `96`       | `49`          | `dab23891e60115f4b978b0e91465a91f0df66bb40e032ace094fc3ba229434a6` |
| `mixed9`      | `9`        | `6`   | `216`      | `59`          | `aaccb9f56512dd68a5f58bd68ca2f12a001a75935e596baed28c6ddcf18eec33` |
| `switch8`     | `8`        | `6`   | `1,152`    | `70`          | `352bd0b529f643a1a5e292c63b01e7c09b7500a39239895e768b2b467fceb1b0` |
| `longdist16k` | `8`        | `9`   | `32,768`   | `16,437`      | `b710c0a6e4fb656cfe84764c4c53857bf58c497d53a7014a9fce3c732af01d31` |
| `longdist32k` | `8`        | `9`   | `65,536`   | `32,824`      | `93c7724edf107fda106720e011d54f8a78c4b8771903aa451d2f25135b5d9d61` |
| `phased9`     | `9`        | `9`   | `89,604`   | `32,351`      | `ea0602478f02c4065f28ef27897872b48e250ab8bcf297b5704667c83f948cfb` |

The `run8` frame uses one `LZ` sequence and **`OF` code 3** (`RUN`).  The
`mixed9` fixture exercises actual values over 255.  The `longdist16k` and
`longdist32k` fixtures exercise explicit offset codes 18 and 19, respectively;
`longdist32k` references the full preceding stored block.  The `phased9`
fixture uses mode 5 in all three phased streams (`LIT`, `LL`, `ML`), including
group `REUSE` sub‑descriptors, and contains `ML=2` matches.  Bigger real files
in the regression suite exercise all three MRU codes and literal, length, and
offset `tANS` streams.  `test_features.py` also builds a two‑block frame whose
second block reuses an installed `LIT` model, then checks both C and Python
decoding.

Independent implementations should decode the published **bytes**, not just
their own re‑encodings, and should reject the negative cases above.  For
interoperability the actual uncompressed content, *packed‑9* bit order, block
checksums, MRU state across block types, and tANS state termination all have
to match exactly.

## 9. Format gaps and future design choices

- **Format stability:** Version 9 is the only format emitted and accepted by
  this implementation.  It uses an extensible 17–4,096‑octet frame header,
  7‑octet `STORED` headers, 23‑octet `LZ` headers, four entropy streams, and
  one raw `EXTRA` stream.  Earlier frame versions are rejected; there is no
  legacy decoding path.

- **Streaming:** The CLI reads `stdin` and writes `stdout` using `-` as the
  corresponding path.  Compression from `stdin` reads input in block‑sized
  batches, writes the `0x81` unknown‑count frame TLV, and terminates with a
  minimal `END` record.  This supports pipes and nonseekable streams but is not
  a low‑latency interactive record protocol.  Compression from a named input
  file counts the file first and emits a known‑length frame.  The C decoder
  accepts either frame form from a file or `stdin` and writes to a file or
  `stdout`.  The Python inspection decoder is limited to known‑length frames.
  The CLI's `-v` size/ratio reporting need not be available when `stdin` or
  `stdout` is used.

- **Final partial Multics symbol:** The `END` TLV defines the number of
  low‑order bits to truncate from the last 9‑bit symbol.  The C decoder parses
  extended `END` records and accepts an explicit `0x82` value of zero, but
  rejects nonzero truncation values.  The encoder emits only minimal `END`
  records and never emits `0x82`.  Consequently the current CLI continues to
  preserve complete 9‑bit symbols and zero‑pads the final output octet.

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

  4. *Defense‑in‑depth zeroing:* Conforming encoders and decoders **SHOULD**
     explicitly zero‑initialize (`memset` / `calloc`) their history and sliding
     window buffers upon allocation, ensuring that even under unexpected fault
     conditions or partial decoding inspection, no residual host heap memory
     can ever be accessed or disclosed.

- **Portability:** This specific UNIX reference implementation expects 8‑bit
  C `char`, 16‑bit `short`, and 32‑bit `int`.  Compression from a named input
  file requires seeking to count the input; the `stdin` path does not seek and
  uses the unknown‑length representation.  Decompression does not require
  seeking, and `stdout` is a non‑seekable destination supported by both encoder
  and decoder.  A named input file still must be seekable when it is
  compressed, even when the output is `stdout`.  On POSIX hosts, the CLI
  rejects an existing output path that resolves to the same file as the opened
  input, including hard‑link and symbolic‑link aliases.  Any Multics PL/I port
  should carry the *wire equations* and **use** **36‑bit‑native**
  representations, **not** emulate these C types unnecessarily.

## 10. Version 9 features, extensions, and control records

Version 9 is the only format emitted or accepted by this implementation.  The
fixed frame‑header prefix has version 9 at offset 4, header length at offset 6,
a one‑octet feature mask at offset 12, and a four‑octet CRC at the end of the
header.  The CRC covers every preceding frame‑header octet.  With no TLVs, the
header is 17 octets and the CRC covers offsets 0–12.

### 10.1. Frame feature flags

| Bit   | Mask   | Meaning                                                          |
|------:|:------:|:-----------------------------------------------------------------|
| `0`   | `0x01` | 65,536‑symbol history window                                     |
| `1`   | `0x02` | Phased entropy tables are available (descriptor mode 5)          |
| `2`   | `0x04` | Two‑symbol matches: minimum match length is 2                    |
| `3`   | `0x08` | Aligned‑distance coding is available throughout the frame        |
| `4`   | `0x10` | Aligned‑distance coding is selected independently per `LZ` block |
| `5–7` | —      | Reserved; **MUST** be zero                                       |

Only mask `0x1f` is currently defined.  Bits outside that mask **MUST** be
zero.  Bit 4 **MUST NOT** be set unless bit 3 is also set.  Bit 0 selects the
history window for the whole frame.  When bit 4 is clear, bit 3 selects
frame‑wide aligned‑distance coding; when both bits 3 and 4 are clear, ordinary
distance representation is used throughout the frame.  The reference encoder
sets bits 1 and 2 only for compression levels 6–9; levels 1–5 retain the
ordinary single–table tANS coding and minimum match length of three.  It
always sets bits 3 and 4 to advertise the per–block aligned–distance selector,
and records the alignment decision in each `LZ` block.  Phased partition
choices are recorded in the corresponding stream descriptors.  The maximum
block size is 32,768 symbols.  Frames whose version byte is not 9 **MUST** be
rejected; no earlier format version is supported.

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
C decoder implements `0x81` and requires an `END` record for such frames.  It
parses minimal and extended `END` records, including their CRC, but currently
accepts `0x82` only when its value is zero.  The Python inspection decoder does
not implement unknown‑length frames or `END` records.

### 10.3. Unknown-length frame extension

An encoder that cannot know the total symbol count before writing the frame
header **MUST** set the frame `N` field at offset 8 to zero and include exactly
one frame TLV with identifier `0x81`, length zero, and no value octets.  A zero
count without this extension continues to mean an ordinary empty known‑length
frame.  The reference encoder uses this representation when its input is
`stdin`.

An unknown‑length frame **MUST** end with exactly one `END` record (section
10.4).  The total decoded symbol count is the sum of the decoded `n` values of
all `STORED` and `LZ` blocks preceding `END`.  `END` is terminal: no blocks or
octets may follow it.  Missing `END`, duplicate `END`, malformed `END`, or
data after `END` **MUST** be rejected.  These rules provide an explicit end
condition for pipes and other nonseekable streams; `EOF` alone is **not** a
valid substitute for `END`.

The reference encoder emits `0x81` for `stdin` input.  The C decoder accepts
this frame form and decodes data blocks until the required `END` record.
File‑based input compression remains known‑length and seek‑counted; `stdout`
output does not change the frame‑length representation.

### 10.4. END record and final-symbol truncation

Block type 2 is assigned to the `END` record.  Its type/control octet uses
bits 0–1 equal to `2`; bits 3–7 **MUST** be zero.  Bit 2 indicates whether an
extension area is present:

- **Minimal `END`:** the entire record is the single control octet `0x02`.
- **Extended `END`:** control octet `0x06`; followed by a two‑octet unsigned
  little‑endian extension‑area length, that many TLV octets, and a four‑octet
  CRC‑32/ISO‑HDLC.  The CRC covers the control octet, length
  field, and all TLV octets, excluding the CRC itself.  The extension area
  **MUST** be nonempty and no larger than 4,096 octets.

An `END` record **MUST** occur exactly once and at the end of an unknown‑length
frame.  For a known‑length frame it **MAY** appear after exactly `N` decoded
symbols, either as an explicit terminal marker or to carry terminal metadata;
otherwise the frame ends immediately after the last data block.  `END`
**MUST NOT** occur before the known symbol count has been produced.  No data
may follow `END`.

The `END` TLV `0x82` has length one.  Its value is the number of
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
complicating the symbol/block codec.

The current C implementation does not implement nonzero truncation: its
extended‑`END` parser accepts `0x82` only when the value is zero, and it writes
all complete decoded symbols.  It emits only the minimal `END` form, not an
extended record.  The Python inspection decoder is known‑length‑only and
rejects `END` records.

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
the ordinary forward `EXTRA` bit order.  A decoder **MUST** reject
non‑canonical flag‑0 forms whose decoded distance is divisible by four.

### 10.7. Per-block aligned-distance selection (`0x18`)

When frame feature bits 3 and 4 are both set, alignment is selected separately
for each `LZ` block.  The decoder **MUST** use aligned‑distance decoding only
when bit 4 of block‑header octet 0 is set.  For an `LZ` block this bit is
otherwise zero; bits 5–7 of octet 0 **MUST** be zero.  For a `STORED` block,
octet 0 continues to encode the stored symbol width in bits 2–7, and bit 4
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

### 10.8. V9 stream-size bounds

The following conservative per‑stream bounds justify the 16‑bit byte‑length
fields.  They include the initial ten‑bit tANS state where applicable.

The table gives the bound for each minimum match length `m` (3 by default; 2
when flag `0x04` is set).

| Stream  | Maximum meaningful bits (`m=3`)       | Octets (`m=3`) | Maximum meaningful bits (`m=2`)       | Octets (`m=2`) |
|--------:|--------------------------------------:|---------------:|--------------------------------------:|---------------:|
| `LIT`   | `32,768 * 10 + 10 = 327,690`          | `40,962`       | `327,690`                             | `40,962`       |
| `LL`    | `floor(32,768/3) * 10 + 10 = 109,230` | `13,654`       | `floor(32,768/2) * 10 + 10 = 163,850` | `20,482`       |
| `ML`    | `floor(32,768/3) * 10 + 10 = 109,230` | `13,654`       | `163,850`                             | `20,482`       |
| `OF`    | `floor(32,768/3) * 10 + 10 = 109,230` | `13,654`       | `163,850`                             | `20,482`       |
| `EXTRA` | `floor(32,768/3) * 46 = 502,412`      | `62,802`       | `floor(32,768/2) * 46 = 753,664`      | `94,208`       |

For `m=2` the `EXTRA` figure exceeds what a 16‑bit length field can carry, so
the field itself (65,535 octets) is the effective limit there.
The `LIT` tANS bound allows at most ten bits per symbol plus its ten‑bit final
state.  Each sequence entropy stream allows at most ten bits per sequence plus
its final state.  The `EXTRA` bound allows up to 15 `LL` bits, 15 `ML` bits,
and 16 `OF` bits (including the alignment selector in the 64K case) per
sequence.  For `m=3` every maximum byte length is below 65,536 and therefore
fits in its unsigned 16‑bit field; with `m=2` the `EXTRA` field limit applies.
Independently, the sum of the five payload byte lengths **MUST NOT** exceed
81,920 octets, the decoder's combined staging bound.

The descriptor length `dl` is an unsigned 16‑bit value but is additionally
bounded to 8,192 octets.  `n` and `q` are unsigned 16‑bit values bounded to
32,768 and `floor(n/m)`, respectively.  These format limits **MUST** be checked
before payload reads or entropy‑table construction.

### 10.9. Phased entropy tables (`0x02`)

Frame feature flag `0x02` permits descriptor mode **5** (`PHASED`) in the
`LIT`, `LL` and `ML` streams.  Mode 5 is invalid in the `OF` stream and invalid
in every stream when the flag is clear.  A phased stream replaces the single
tANS model of the block with up to two models, and chooses between them using
the *phase* of the symbol being decoded.

**Phase.** The *frame position* of an output symbol is the number of symbols
the frame has produced before it: zero for the first symbol of the frame,
counting every symbol of every previous `STORED` and `LZ` block.  Its *phase*
is the frame position modulo 4 (the low two bits).  Phase is counted in
symbols, not octets, and does not depend on block boundaries.  For 36‑bit‑word
data that starts at a word boundary, the four phases are the four 9‑bit
quarters of each word.  The phase used for each entropy symbol is:

| Stream | Phase of …                                                             |
|-------:|:-----------------------------------------------------------------------|
| `LIT`  | the output position at which that literal is written                   |
| `LL`   | the output position at which the sequence starts (before its literals) |
| `ML`   | the output position of the first symbol of the match                   |

The `LL` phase therefore equals the phase of the first literal of the run (or of
the match when the run is empty), and the `ML` phase is the `LL` phase plus the
run length, modulo 4.  Both are known to a decoder before it reads the symbol.
Terminal literals (after the last sequence) use their own positions.

**Partitions.** A partition maps the four phases to group numbers.  Only the
following partitions are valid; `pid` is the partition identifier byte.

| Stream | `pid` | Name     | Group of phase 0 / 1 / 2 / 3 | Groups |
|-------:|:-----:|:--------:|:-----------------------------|:------:|
| `LIT`  | `0`   | `023\|1` | `0 / 1 / 0 / 0`              | 2      |
| `LIT`  | `1`   | `02\|13` | `0 / 1 / 0 / 1`              | 2      |
| `LL`   | `0`   | `02\|13` | `0 / 1 / 0 / 1`              | 2      |
| `ML`   | `0`   | `01\|23` | `0 / 0 / 1 / 1`              | 2      |

Any other `pid` for the stream is invalid.

**Descriptor.** A phased descriptor is the mode octet `05`, the `pid` octet, and
then, for group 0 and group 1 in order, one *sub‑descriptor*:

| First octet | Meaning                                                                                         |
|------------:|:------------------------------------------------------------------------------------------------|
| `00`        | `EMPTY`: no symbol of this block uses the group                                                 |
| `03`        | A fresh model follows, in the mode‑3 syntax of section 5.1; it is installed in the group's slot |
| `04`        | `REUSE`: use the model currently installed in the group's slot                                  |

No other sub‑descriptor is valid (`RAW` and `RLE` are not available for
groups).  At least one group **MUST** be non‑`EMPTY`.  A group model, like a
mode‑3 model, **MUST** contain at least two symbols with nonzero frequency; an
encoder whose group uses a single symbol adds a second, unused symbol with
frequency one.

**Group slots.** Each phased stream kind (`LIT`, `LL`, `ML`) owns two group
slots, numbered by group.  A slot holds one normalized model.  Slots are empty
at the start of a frame; a sub‑descriptor `03` installs its model in the slot
of its group, replacing the previous content; `04` reads the slot and is
invalid if the slot is empty.  Slot contents persist across `LZ` and `STORED`
blocks within the frame, are independent of the mode‑3/mode‑4 model of the same
stream kind (neither is touched by the other), and never persist across frames.
A slot is not changed by a block that uses `EMPTY`, `RAW`, `RLE`, `tANS`,
`REUSE` or an `EMPTY`/`REUSE` sub‑descriptor.  A model is interchangeable
between slots and partitions: the encoder may `REUSE` a slot whatever partition
last filled it, provided the model has nonzero frequency for every symbol the
new block needs.

**Payload.** The payload is a single reverse‑consumed tANS bitstream with one
decoder state, laid out exactly as for mode 3 (section 5.3): the last ten
meaningful bits are the initial state, every symbol reads `nbits` preceding
bits, and the final state **MUST** be zero.  Before decoding each symbol the
decoder chooses the table of the group that the partition assigns to the
symbol's phase; the state transition and bit count come from that table.  All
models use 1024 states, so the state carries over between tables without any
conversion.  A symbol whose group is `EMPTY` makes the stream invalid.  The
mode‑5 descriptor is non‑`EMPTY`, so for `LL` and `ML` it is invalid when the
block has no sequences and for `LIT` it is invalid when the block has no
literals, exactly like mode 3.

*Encoder note (informative).* The reference encoder gathers a per–phase
histogram of each of the three streams and evaluates the merged model and each
partition. The ordinary, non–phased `REUSE` candidate is trial–encoded with the
cached table and selected only when its actual payload plus one–octet
`REUSE` descriptor is smaller than the fresh–table payload plus its descriptor.
For phased candidates, each group may use a fresh model or `REUSE` of its slot;
the reference encoder compares estimated descriptor and payload costs and
requires a partition to beat the best non–phased candidate by at least two
octets.

### 10.10. Two-symbol matches (`0x04`)

Flag `0x04` lowers the minimum match length `m` from **3** to **2**.
Three things change, and nothing else:

1. The `ML` length conversion of section 4.1 uses `bias=2`, so `ML code 0`
   means a match of length 2.
2. A match whose decoded length is below 2 is invalid, and the sequence count
   bound is `q <= floor(n/2)`.
3. The stream‑size bounds of section 10.8 use `m=2`.

The format places no restriction on the distance of a two‑symbol match.  The
reference encoder (informative) only emits them when the offset is nearly free:
a repeat slot (`OF` codes 0–2) or `RUN` (code 3), and only in the cost‑guided
levels 6 to 9, where the price of the two literals they replace is known and
the frame advertises feature flag `0x04`.

### 10.11. Independent-block encoding in the reference implementation

In a pthread build, compression *automatically* uses independent–block encoding
when the effective participant limit exceeds one.  `--threads=0` selects the
available logical–processor count, capped at 32 participants on 32‑bit builds
and 128 on 64‑bit builds; a positive value caps participants, including the
coordinator, and requests above the architecture limit are silently clamped.
`--threads=1`, or a system with only one available logical processor, uses the
original sequential encoder.  The default non–threaded build never selects
independent–block encoding and retains the original serial behavior.  There is
no separate command–line switch for this strategy.

Independent–block encoding does not add a V9 feature bit or alter any
block/header/descriptor syntax.  The coordinator partitions input into the
existing block sizes and can encode multiple complete blocks concurrently.
Each block receives a private snapshot of immediately preceding uncompressed
frame history (up to the advertised 32K or 64K window), including input from
preceding blocks selected as `STORED`.  The worker uses a fresh match finder,
repeat–distance state, and ANS model cache for each block.  Repeat–slot and
model–reuse decisions therefore do not depend on the scheduling or completion
order of other blocks.  Explicit distances establish recent repeat slots in
the same order; workers only emit repeat–slot references to distances
established within the current block.  ANS model reuse within the current
block remains available, while cross–block model reuse is not used.

Workers return complete block records in private memory.  The coordinator
emits those records in original input order and is the only code that accesses
the input/output streams.  The serial build and a pthread build with
`--threads=1` use the original encoder path, not the independent–block path.
A pthread build with more than one effective participant uses the same
block–independent coding algorithm regardless of participant count, so
thread scheduling does not change the output.  This mode can produce different
compressed bytes and ratios than the original sequential encoder, but its
output remains an ordinary V9 frame readable by the existing decoder.  Decoding
remains sequential or uses the existing Phase 1 stream parallelism; no
block–parallel decoder is introduced here.

In a pthread build, the memory report estimates the per–participant encoder
contexts, private block–output buffers, shared history snapshot and raw–input
scratch buffer, plus the pthread worker–stack budget, when independent–block
encoding is selected automatically.  These allocations are absent from the
default serial build and from the pthread build when `--threads=1` or only one
logical processor is available.
