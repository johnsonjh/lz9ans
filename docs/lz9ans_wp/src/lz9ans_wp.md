---
# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT
# scspell-id: 67d168c6-e37f-11f1-b079-80ee73e9b8e7
title: LZ9ANS
subtitle: A High-Performance Modern 9-bit Compression Architecture
author: Jeffrey H. Johnson
date: DRAFT&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;November 19th 2026&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;DRAFT
documentclass: extarticle
fontsize: 10pt
classoption:
  - oneside
  - twocolumn
geometry:
  - margin=0.75in
  - columnsep=0.25in
header-includes:
  - \usepackage{fancyvrb}
  - \fvset{fontsize=\footnotesize}
  - \usepackage{etoolbox}
  - |
    \makeatletter
    \patchcmd\longtable{\if@twocolumn\LT@err{longtable not in 1-column mode}\@ehc\fi}{}{}{}
    \makeatother
---
\hyphenation{Compression}
\hyphenation{Designs}
\hyphenation{Implementations}
\hyphenation{Normalization}
\hyphenation{REDUCE}

Most general-purpose compression formats are designed around the assumption
that their input consists of conventional `8`‑bit bytes.

That assumption is appropriate for contemporary computing environments, but it
is not universally applicable. Historical and specialized architectures
frequently use different logical data widths, and systems built around `36`‑bit
words present a particularly interesting case: four `9`‑bit characters fit
naturally into a single machine word.

**LZ9ANS** is a compression format and reference implementation designed to
address this class of requirements without sacrificing the flexibility of
a modern general‑purpose compression architecture.

Combining `LZ77`‑style dictionary compression with table-based Asymmetric
Numeral Systems (`tANS`), **LZ9ANS** introduces a number of architectural and
algorithmic adaptations that distinguish it from conventional byte-oriented
codecs.

These include native `9`‑bit symbol handling, `36`‑bit-word‑aware entropy
coding, bounded block processing, independently reset entropy states,
persistent dictionary history, model reuse, adaptive entropy representations,
and cost‑aware parsing.

The design also places considerable emphasis on predictable resource
consumption and implementation flexibility. Its explicit framing,
bounded coding structures, deterministic entropy‑table construction,
and well‑defined decoding semantics make it suitable for implementation
in portable software as well as environments where memory, processing
resources, or hardware architecture impose additional constraints.

The current **LZ9ANS** bitstream is `V9`.  The `V9` format supports both
`8`‑bit and `9`‑bit representations.  Consequently, **LZ9ANS** is not limited
to historical data formats or specialized computing environments. It can also
operate as a conventional compression codec while retaining architectural
capabilities that are unusual among general-purpose compression formats.

This paper examines the principal strengths of **LZ9ANS**, with particular
attention to the features that are most valuable when developing independent,
compatible production implementations.

### Introduction

Compression formats are often optimized around a relatively narrow set
of assumptions about their target environment. Most contemporary formats
operate on `8`‑bit bytes, use conventional dictionary-based compression,
and apply an entropy coder to the resulting symbols.

These approaches work well for ordinary files, but they can become
inefficient or unnecessarily complicated when the underlying data has a
different logical organization.

The GE/Honeywell/Bull Multics and Unisys OS2200 environments provide a useful
example. Their historical architectures use `36`‑bit words, with four `9`‑bit
characters fitting naturally into each word.

Representing such data in an ordinary byte-oriented format requires additional
interpretation or transformation. Treating every `9`‑bit character as a
“`16`‑bit” value, for example, introduces *substantial* padding overhead before
compression even begins, and naïvely repacking to `8`‑bit octets destroys
natural repeats.

**LZ9ANS** addresses this problem directly by using a native `9`‑bit logical
symbol model, but native symbol width is only one part of its design. The
codec also takes advantage of the structure of `36`‑bit words when selecting
entropy models and representing dictionary distances. Its architecture combines
these specialized features with a block-oriented `LZ77` and `tANS` design
intended to remain efficient and predictable on conventional `8`‑bit systems.

The result is a codec with several complementary characteristics:

- Native support for `9`‑bit symbols without requiring `16`‑bit external
  representation or `8`‑bit repacking.

- Compatibility with conventional `8`‑bit input and output.

- Entropy coding designed to exploit positional characteristics within
  `36`‑bit words.

- Dictionary compression that preserves useful history across block boundaries.

- Bounded block sizes, history windows, entropy states, and descriptor
  structures.

- Multiple entropy representations selected according to the characteristics
  of individual streams.

- A cost-aware `LZ` parser that considers the downstream entropy
  representation.

- Optional parallel processing and hardware-accelerated checksum calculation.

These characteristics are best understood as a coordinated architecture
rather than a collection of unrelated compression optimizations.

### Native `9`‑Bit Symbol Processing

#### A logical symbol model independent of the host architecture:

The fundamental architectural distinction in **LZ9ANS** is that its primary
data model is based on `9`‑bit symbols rather than octets.  The logical
alphabet is:

$$
0 \leq s \leq 511
$$

This permits the codec to represent the complete range of `9`‑bit character
values without converting them into a larger logical representation.

For conventional `8`‑bit data, symbols occupy the range `0`–`255`. In native
`9`‑bit mode, all `512` possible values are available.

The distinction is maintained throughout the compression architecture. The
`LZ` match finder, literal stream, entropy models, history management, and
checksum calculation operate on logical symbols rather than assuming that
every symbol is an `8`‑bit byte.

This separation is important for portability. A host implementation can
use whatever internal storage representation is convenient without changing
the logical data model or the external format.

#### Efficient external representation:

**LZ9ANS** packs `9`‑bit symbols consecutively using most-significant-bit-first
ordering. Eight complete `9`‑bit symbols (nonets) occupy exactly nine octets:

$$
8 \times 9 = 72\text{ bits} = 9\text{ octets}
$$

This provides a compact external representation without padding each symbol
to `16` bits.

The reference implementation includes optimized packing and unpacking
paths for groups of symbols, together with portable fallback implementations.

This makes it possible to support native `9`‑bit data while retaining
conventional octet-oriented framing and I/O.

The distinction between logical symbols and physical storage is particularly
valuable when developing implementations for architectures whose native
word sizes do not correspond to `8`‑bit bytes.

#### Conventional `8`‑bit operation:

Native `9`‑bit support does not require sacrificing conventional byte-oriented
operation.  **LZ9ANS** also supports an `8`‑bit representation, in which each
input octet corresponds to one logical symbol.

This allows the same underlying format architecture to handle ordinary binary
files, text, and specialized `9`‑bit data.  The two modes share the same
fundamental compression mechanisms. The difference is the representation of
the logical input and output.

The result is a codec designed around a more general data model than the
conventional `8`‑bit assumption, without requiring ordinary `8`‑bit
applications to adopt a different compression architecture.

### Design Considerations for `36`‑Bit Architectures

Native `9`‑bit symbols provide the foundation for compatibility with `36`‑bit
systems. **LZ9ANS** goes further by adapting parts of its entropy and
dictionary coding to exploit the organization of four `9`‑bit characters per
word.  These adaptations are among the most distinctive features of the format.

#### Phase-dependent entropy coding:

In a conventional byte-oriented codec, all literal symbols within a block
typically draw from the same entropy model.

**LZ9ANS** can instead distinguish symbols according to their position within
a `36`‑bit word.  The four possible positions are represented by:

$$
phase = position \bmod 4
$$

For data beginning on a word boundary, the four phases correspond to the
four `9`‑bit characters within each `36`‑bit word.

This matters when the character distributions differ according to
position. Structured binary data, for example, may exhibit distinct
statistical characteristics in different word positions.

Rather than forcing all four positions into one combined statistical model,
the encoder can build separate histograms and evaluate permitted groupings
of the four phases.

Different `tANS` models can then be selected for different phase groups.

The phase-dependent mechanism applies to the `LIT`, `LL`, and `ML` streams.
The `OF` stream retains its ordinary entropy representation.

An important aspect of the design is that switching between phase-dependent
models does not require resetting the `tANS` state. The models operate within
the same `1,024`-state space.

The decoder derives the phase from the absolute position within the frame. It
must therefore maintain the frame position across block boundaries rather
than restarting phase at the beginning of every block.

Phase-dependent entropy coding offers a useful opportunity to exploit
structure that conventional byte-oriented codecs cannot directly express.

It is especially relevant when the underlying data is organized around
`36`‑bit words, but it remains optional. Data that does not benefit from
phase-dependent modeling can use ordinary entropy coding.

#### Aligned-distance coding:

The structure of `36`‑bit words can also be exploited by the `LZ` dictionary.
When a repeated sequence appears at the same character position in successive
words, its displacement is divisible by four.

For eligible explicit match distances, **LZ9ANS** provides an alternative
aligned-distance representation.

A divisible-by-four distance can be represented using two fewer residual
bits, with an alignment selector bit.

The benefit is therefore conditional: the aligned representation is
advantageous when the distance encoding saves more than the selector overhead.

This approach complements phase-dependent entropy coding:

- Phase-dependent coding exploits differences in symbol statistics according
  to position within a word.

- Aligned-distance coding exploits repeated structures whose displacement
  preserves their position within a word.

Together, these features adapt both the entropy and dictionary portions
of the compression architecture to the same underlying data organization.

They are not simply alternative packing schemes. They are format-level
mechanisms that allow the encoder to exploit structural information that
would otherwise be lost in a generic byte-oriented model.

### Bounded Design for Predictable Resource Usage

A central design goal of **LZ9ANS** is to make the amount of work and storage
associated with decoding explicit and bounded.

This is especially important for production implementations intended to
operate in constrained environments or to be translated into hardware.

#### Explicit block and window limits:

`V9` defines a maximum block output of `32,768` logical symbols.

The history window is configurable:

- `32,768` symbols as the default.

- `65,536` symbols with the extended-window feature.

The maximum match length is `32,768` symbols.

The entropy coder uses `1,024` states, and the format places explicit limits
on the sizes of descriptors and compressed payloads.

These limits allow a decoder to establish its resource requirements before
processing untrusted block contents.

The principal format bounds include:

|                     Resource | `V9` limit       |
|-----------------------------:|:-----------------|
| Block output                 | `32,768` symbols |
| History window               | `65,536` symbols |
| Maximum match length         | `32,768` symbols |
| `tANS` state count           | `1,024`          |
| Block descriptor area        | `8,192` octets   |
| Combined compressed payloads | `81,920` octets  |
| Frame header                 | `4,096` octets   |

These are format limits rather than recommendations to allocate all possible
resources unconditionally.

An implementation can use smaller allocations when its selected configuration
permits them.

#### Separation of block size and history size:

An important architectural distinction is that the block output limit and
history window are independent.

Each block has bounded output and entropy-coding requirements. However,
its dictionary can still reference data decoded in earlier blocks within
the same frame.

This preserves cross-block compression opportunities without requiring
arbitrarily large output blocks.

The arrangement also simplifies memory management and allows an implementation
to process data incrementally.

#### Bounded entropy structures:

The `tANS` state count is fixed at `1,024`.  The decoder does not need to
construct an entropy state space whose size depends on untrusted input.

Instead, it reconstructs transition tables from normalized frequency
descriptors within the defined limits.

This provides a predictable memory footprint and a straightforward mapping
to table-based software or hardware implementations.

#### Explicit descriptor and payload lengths:

Every compressed block describes its component streams and their associated
lengths.

The format therefore provides explicit boundaries for the individual
entropy streams and the raw `EXTRA` bitstream.

A decoder can validate the relevant lengths and padding before reading or
decoding the corresponding data.

This is preferable to architectures in which stream boundaries must be
inferred from the entropy state or discovered by speculative parsing.

Bounded structures are not merely a convenience for embedded
implementations. They make validation, testing, memory accounting, and
hardware implementation substantially more manageable.

### Hybrid `LZ77` and `tANS` Compression Architecture

**LZ9ANS** combines dictionary compression with table-based Asymmetric Numeral
Systems.  This combination addresses two different forms of redundancy:

- `LZ77` identifies repeated sequences and represents them as references into
  previously decoded data.

- `tANS` efficiently represents the resulting literals, lengths, and offsets
  according to their statistical distributions.

The two stages are designed together rather than treated as completely
independent operations.

#### Separate entropy-coded streams:

An `LZ` block uses independently described streams for its principal sequence
components.

|   Stream | Alphabet     | Purpose                             |
|---------:|:------------:|:------------------------------------|
| `LIT`    | `512`        | Literal symbols                     |
| `LL`     | `32`         | Literal-run lengths                 |
| `ML`     | `32`         | Match lengths                       |
| `OF`     | `20` or `21` | Match offsets                       |
| `EXTRA`  | Raw          | Additional length and distance bits |

This separation allows each component to use an appropriate representation
and statistical model.

The literal stream can be modeled independently of the match-length and
offset streams.

Similarly, the sequence metadata does not need to share entropy state with the
literal data.

The `EXTRA` stream carries additional bits associated with length and distance
codes. It is independently bounded and forward-oriented.

This division provides a well-defined structure for both software and
hardware implementations.

#### Multiple entropy representations:

**LZ9ANS** does not require every stream to use `tANS`.

Each stream can select among several representations according to its contents.

|     Mode | Representation                | Purpose                                                    |
|---------:|------------------------------:|:-----------------------------------------------------------|
| `EMPTY`  | No symbols                    | Empty stream                                               |
|          |                               |                                                            |
| `RAW`    | Fixed-width symbols           | Simple representation for unsuitable entropy distributions |
|          |                               |                                                            |
| `RLE`    | One repeated symbol           | Constant-valued stream                                     |
|          |                               |                                                            |
| `tANS`   | Fresh normalized model        | Adaptive entropy coding                                    |
|          |                               |                                                            |
| `REUSE`  | Previously installed model    | Reduced model-description overhead                         |
|          |                               |                                                            |
| `PHASED` | Phase dependent `tANS` models | Exploitation of positional distributions                   |

This flexibility is important because the overhead of constructing and
transmitting an entropy model is not always justified.

An empty stream does not need a coding table. A stream containing only one
repeated symbol can be represented directly. A relatively small or poorly
distributed stream may be cheaper to encode using `RAW.` When an entropy model
provides a genuine benefit, `tANS` is available.

The encoder evaluates the complete representation rather than automatically
applying the most sophisticated mode to every stream.  This avoids the common
problem of using a theoretically powerful entropy coder even when its
model-description overhead exceeds the savings it provides.

#### Persistent entropy model reuse:

Entropy state and entropy models have different lifetimes in **LZ9ANS**. The
`tANS` state is initialized independently for each block, but normalized
frequency models can persist across blocks within a frame.

The `REUSE` descriptor allows a block to reference an already installed
model rather than transmitting a fresh frequency description.

At higher compression levels, the reference encoder can evaluate a cached
model against the current stream and use it when the resulting representation
is smaller.

Model reuse reduces descriptor overhead in data where successive blocks
have similar statistical characteristics.

The model caches are maintained separately by stream kind, preventing an
`LL` model, for example, from being incorrectly interpreted as a `LIT` model.

This is a useful balance between independent block decoding state and
persistent knowledge of the data’s statistical characteristics.

### Deterministic Table-Based ANS (tANS)

The entropy coder is a particularly important part of **LZ9ANS**. `V9` defines
a custom table-based `tANS` representation using a fixed `1,024`-state space.

Although it follows the general principles of table-based ANS coding,
its table construction and serialized descriptors are specific to **LZ9ANS**.

#### Deterministic state-table construction:

The normalized frequency distribution is expanded into `1,024` states.  The
format specifies a deterministic symbol-spreading procedure using the
recurrence:

$$
\mathrm{position} \leftarrow (\mathrm{position} + 643) \bmod 1024
$$

Symbols are placed in ascending order, with each symbol occupying the
number of states specified by its normalized frequency.

Because the spreading step is relatively prime to `1,024`, the procedure
visits every state.

The decoder then constructs its transition entries from the resulting
distribution.

Each entry contains the decoded symbol, the number of bits to consume,
and the baseline for the next state.

The reference implementation packs these fields into a `32`‑bit entry. This
supports efficient state lookup using a compact table representation.

An important compatibility point is that the format defines its own
construction rules. It should not be assumed that an `FSE` (Finite State
Entropy) implementation or generic `tANS` table generator will produce
compatible tables.

#### Reverse-consumed entropy streams:

The `tANS` bitstream uses a specific reverse-consumption convention.

The final ten meaningful bits contain the initial decoder state.

The decoder then emits symbols in forward logical order while consuming
the preceding groups of bits in reverse group order.

The bits within each group retain their MSB-first interpretation. They
are not individually reversed.

After decoding the expected symbols, the decoder must verify:

- The final ANS state is zero.

- The expected symbol count has been produced.

- No meaningful bits remain unconsumed.

These conditions provide explicit entropy-stream termination checks and
help detect malformed or truncated input.

#### Why deterministic entropy construction matters:

The fixed state-space size and deterministic table construction are valuable
for more than interoperability.

They also provide:

- Predictable table memory requirements.

- Bounded decoder workspaces.

- Straightforward conformance testing.

- A stable basis for optimized lookup implementations.

- A practical path toward FPGA or ASIC implementations.

The format’s entropy representation is therefore a deliberate architectural
choice, not simply a replacement for a conventional Huffman coder.

### Integer-Based Linear KL-Optimal Frequency Normalization

An efficient entropy coder depends on the quality of its normalized
frequency distributions.

The reference encoder uses a deterministic integer-based implementation
of Linear KL‑Optimal frequency normalization.

The objective is to convert observed symbol counts into positive integer
frequencies that sum to exactly `1,024`.

Conceptually, this seeks an allocation that minimizes the coding penalty
associated with approximating the empirical probability distribution using
integer frequencies.

The marginal cost of reducing a symbol’s current allocation can be expressed
proportionally as:

$$
c_i \ln\left(\frac{j}{j-1}\right)
$$

where:

- $c_i$ is the observed count of symbol $i$.

- $j$ is its current normalized frequency.

The reference implementation uses fixed-point arithmetic, bounded frequency
allocations, and selection procedures to arrive at the final distribution.

It avoids requiring general-purpose floating-point logarithms and does
not need to sort every possible allocation adjustment.

This is valuable for portability and deterministic execution.

However, the normalization algorithm is an encoder implementation choice
rather than a mandatory decoding operation.

The `V9` format specifies the normalized frequencies consumed by the decoder.
An independent encoder can use a different normalization technique while
remaining compatible, provided it emits valid descriptors and frequencies.

This distinction gives production implementations room to use different
optimization strategies without changing the format.

### Cost-Aware `LZ` Parsing

A significant strength of **LZ9ANS** is that its reference encoder considers
the cost of the final encoded representation when selecting dictionary matches.

A conventional greedy `LZ` parser typically prioritizes the longest available
match.

That is a useful heuristic, but it is not necessarily optimal once entropy
coding is introduced.

A match can replace a sequence of literals, but it also introduces several
costs:

- A literal-run length code.
- A match-length code.
- An offset code.
- Additional distance or length bits.
- The loss of the literal representation that the match replaces.

A longer match is not automatically cheaper than a shorter one.

#### Estimated encoding costs:

The **LZ9ANS** reference encoder derives approximate symbol prices from
preliminary frequency distributions. These prices are represented using
fixed-point `Q8` values.  The encoder also builds a prefix-cost array for
literal symbols.

This allows the estimated cost of a literal interval to be obtained in
constant time:

$$
\operatorname{cost}(a, b) = \operatorname{prefix}[b] - \operatorname{prefix}[a]
$$

The parser can therefore compare the estimated cost of representing a
range as literals against the estimated cost of using a dictionary match.

This is substantially more informative than selecting matches based only
on their length.

#### Progressive search effort:

The reference implementation exposes compression levels `1`–`9`, where levels
`1`–`4` are “fast”, levels `5`–`8` are “tight”, and level `9` is “maximum”.

The levels progressively increase match-search effort and parsing
sophistication.

Lower levels use relatively shallow match searches and greedy
decisions. Higher levels perform deeper candidate searches, consider
additional match-length opportunities, and apply cost-guided refinement.

The highest levels can also consider two-symbol matches where their distance
representation is sufficiently inexpensive.

The purpose is to spend more processing time evaluating alternatives that
may produce a better final representation.

This is a bounded heuristic rather than an exact global optimal parser. Its
estimated costs are derived from preliminary distributions, so the compression
level should not be interpreted as a mathematical guarantee of monotonically
decreasing output size.

Nevertheless, the approach gives the encoder a way to reason about the
actual downstream representation rather than treating every dictionary
match as equally expensive.

#### Candidate pruning:

The cost-aware parser is also supported by match-finder optimizations.

The reference implementation uses an `8,192`-entry three-symbol hash table,
with hash-chain storage sized to the configured history window.

The search depth increases with the compression level.

It also probes the repeat-distance slots independently of the ordinary
hash-chain depth.

This allows frequently used recent distances to remain available candidates
even when their hash-chain entries fall outside the configured search depth.

Candidate pruning prevents the encoder from spending additional effort on
matches that cannot improve an already established best match length.

Together, cost estimates, bounded search depth, lazy matching, and candidate
pruning provide a practical compromise between compression quality and
encoder complexity.

### Repeat-Distance Optimization and `RUN` Coding

Dictionary offsets are not uniformly distributed in practical `LZ` data.

Recently used distances often recur, making it possible to represent them
more efficiently than arbitrary explicit offsets.

**LZ9ANS** maintains three repeat-distance slots using most-recently-used
(MRU) promotion.

Its offset alphabet includes:

- Three repeat-distance codes.
- A dedicated `RUN` code for distance one.
- Explicit distance buckets.
- A dedicated exact-distance representation for `32,768`.
- An additional code for the extended `64K` window.

#### Repeat-distance slots:

When a match uses a distance, the relevant repeat slot is updated according
to the specified MRU rules.

Subsequent matches can then refer to one of the initialized slots without
transmitting a complete explicit distance.

This is particularly effective in data containing repeated structures at
recurring offsets.

The three-slot design offers a useful balance between capturing common
distance reuse and keeping the offset alphabet relatively small.

#### Dedicated `RUN` representation:

Distance-one matches are especially common in some data.

**LZ9ANS** provides a separate `RUN` code for them.

Unlike an ordinary explicit distance-one match, `RUN` does not modify the
repeat-distance slots.

This distinction allows repeated-symbol sequences to be represented without
unnecessarily disturbing the history of other useful match distances.

The reference encoder can consider the estimated costs of `RUN` and initialized
repeat slots when selecting a representation.

The decoder semantics remain simple and explicit.

#### Short matches:

At higher compression levels, the encoder can consider two-symbol matches.

These are particularly useful when the distance is already represented by
an initialized repeat slot or `RUN`, making the match inexpensive.

The encoder evaluates whether the cost of the two-symbol match is lower
than the cost of retaining the corresponding literals.

This is another example of dictionary parsing and entropy representation
being optimized together.

### Adaptive Use of `STORED` Blocks

Compression is not beneficial for every input block.

A codec that always emits a compressed representation can expand
incompressible data, consume unnecessary CPU time, and increase memory
pressure.

**LZ9ANS** addresses this through explicit `STORED` blocks.

After evaluating the compressed representation, the encoder can store the
original block when compression does not provide a size advantage.

The comparison includes the complete compressed representation rather than
considering only the entropy-coded payload.

The encoder also uses an early heuristic for blocks that are overwhelmingly
literal.

When at least `97%` of a block is represented as literals, it can estimate
whether the literal representation alone is already too expensive compared
with storing the block.

When appropriate, this avoids more expensive cost-guided parsing passes.

`STORED` blocks preserve the existing `LZ` history and repeat-distance
slots. They are not dictionary reset points.

This is important because using an uncompressed block should not discard
useful compression context for subsequent blocks.

The result is a codec that can adapt between compressed and uncompressed
block representations without interrupting the logical dictionary history.

### Frame Design and Streaming

**LZ9ANS** uses an explicit frame format designed for portable, incremental
processing.

The `V9` frame includes:

- A recognizable `LZ9A` signature.
- A version field.
- An explicit output representation indicator.
- A header length field.
- Feature flags.
- Optional TLV extensions.
- A header CRC‑32 checksum.

The format supports both known-length and unknown-length (*e.g.*, piped or
streaming) input.

Unknown-length frames use an extension and an explicit `END` record, allowing
compression and decompression through standard input/output streams and
other non-seekable interfaces.

The frame format does not require platform-specific filenames, timestamps,
or record metadata.

These belong to higher-level archive or storage formats.

This separation keeps **LZ9ANS** focused on compression and symbol
representation while making it usable in a broader range of software
environments.

The `V9` specification also defines final-symbol truncation metadata. The
current ANSI C reference decoder accepts only the zero-truncation value, and
the reference encoder does not emit extended `END` records.

Independent implementations should follow the actual supported format
semantics rather than assuming that every specified extension is already
implemented by the reference program.

### Data Integrity and Defensive Decoding

Compression formats must be designed with malformed and corrupted input
in mind.

**LZ9ANS** incorporates several mechanisms that support validation and
controlled resource consumption.

#### Header and block validation:

Explicit framing, header CRCs, descriptor lengths, payload lengths, and
maximum block sizes allow a decoder to validate important properties before
allocating memory or processing payloads.

The fixed `tANS` state space and bounded descriptor structures further
constrain the decoder’s resource requirements.

#### Entropy termination checks:

Each `tANS` stream has explicit decoding requirements.  After decoding the
expected number of symbols, the decoder verifies the final state and
meaningful bit consumption.

This provides an additional layer of validation beyond simply producing
the expected symbol count.

#### Canonical block checksums:

**LZ9ANS** uses CRC‑32 block checksums.  The checksum is calculated over the
decoded symbols repacked into a canonical MSB-first `9`‑bit representation.

This is important because the checksum is independent of whether the
decoded output is presented as `8`‑bit or `9`‑bit data.

For a valid block, the decoder must reproduce the prescribed logical symbol
sequence and verify its checksum.

A production implementation should validate the block before committing
its decoded contents to its destination.

This provides block-level integrity checking and prevents a failed checksum
from silently producing accepted output for that block.

For applications that require whole-file atomicity, the caller should also
use a temporary output destination and only publish the result after the
complete frame has been successfully validated.

### Parallel Processing

The `V9` architecture provides opportunities for parallel processing because
blocks have bounded output and independently initialized entropy states.

The reference implementation includes optional `pthread`-based processing
paths.

These are implementation features rather than requirements of the compressed
format.

#### Parallel compression:

The parallel encoder uses independent-block compression workers.

Each worker receives a block and an appropriate snapshot of preceding
input history, then performs its own match finding, sequence generation,
and entropy coding.

The coordinator manages input and output and writes completed blocks in
their original order.

This permits multiple blocks to be compressed concurrently without requiring
workers to share mutable match-finder or entropy state.

The independent-block strategy has a deliberate trade-off.

Cross-block `LZ` matches remain possible because workers receive preceding
history, but the workers do not use the same persistent repeat-distance
and entropy-model state as the sequential encoder.

Consequently, the compressed representation may differ from sequential
compression even though the output remains a valid `V9` frame.

#### Parallel decompression:

Decompression is more constrained because dictionary history and
reconstruction order must be preserved.

The reference implementation addresses this by parallelizing independent
entropy work while maintaining sequential control over `LZ` reconstruction.

For known-length frames, it can process multiple blocks in a bounded pipeline.

The coordinator processes descriptors and snapshots the relevant entropy
tables. Workers decode ordinary `LL`, `ML`, and `OF` streams. The coordinator
then performs sequence reconstruction, processes `EXTRA`, updates history
and repeat distances, validates checksums, and emits output in order.

Blocks requiring phased `LL` or `ML` decoding use the sequential fallback.

Unknown-length frames retain a streaming-oriented strategy with per-block
parallel entropy processing.

This is an important architectural distinction: independent entropy decoding
can overlap in time, while the stateful dictionary reconstruction remains
correctly ordered.

#### Optional threading:

The format does not require multithreading.

A fully compatible implementation can be single-threaded, and the reference
build supports configurations without `pthread` functionality.

This keeps the format usable in environments where thread libraries are
unavailable or undesirable.

### Portable Software and Hardware Implementation

**LZ9ANS** has several characteristics that make it suitable for implementation
beyond a conventional computer processor.

These include:

- Fixed-size entropy state spaces.
- Explicit maximum block and history sizes.
- Bounded descriptor structures.
- Independent block entropy initialization.
- Deterministic table construction.
- Explicit stream lengths and padding.
- Well-defined sequence and history semantics.
- Portable baseline algorithms.

The reference ANSI C implementation also includes foreign architecture-specific
(`x86‑64` and `ARM64`) acceleration where appropriate.

#### CRC acceleration:

The checksum implementation provides a portable software path alongside
hardware-assisted implementations.

On `x86‑64`, the reference implementation can use `PCLMULQDQ` carry-less
multiplication with polynomial folding and Barrett reduction.

On `ARM64`, it can use available architecture-specific CRC instructions with
runtime feature detection.

These optimizations do not change the checksum definition or the compressed
format.

The portable software implementation remains the correctness baseline.

#### Optimized symbol processing:

The reference implementation includes optimized paths for `9`‑bit packing
and unpacking.

It also includes accelerated match comparison on `x86‑64`, using wider
comparisons and bit operations, with a portable fallback.

This illustrates a broader implementation principle: performance-specific
code can be isolated behind stable logical interfaces.

The format does not depend on a particular instruction set or host
architecture.

#### A useful basis for FPGA and ASIC implementations:

The combination of fixed entropy-state dimensions, explicit block limits, and
bounded history provides a useful starting point for hardware implementations.

The separate entropy streams also offer opportunities to organize hardware
into distinct processing units.

The literal decoder, sequence metadata decoders, `EXTRA` reader, and
reconstruction logic have defined interfaces and responsibilities.

A hardware implementation will still need to address the stateful dependencies
of `LZ` history, repeat distances, and phase tracking.

However, the format avoids several sources of uncertainty that complicate
hardware decoding in formats with unbounded or implicit structures.

**LZ9ANS** should therefore be viewed as hardware-conscious in its
architecture, although the existence of a software reference implementation
does not itself establish a complete FPGA or ASIC implementation or measured
hardware performance.

### Decoder Compatibility and Independent Implementations

A defining advantage of **LZ9ANS** is the separation between normative
decoding semantics and reference encoder optimization decisions.

An independent decoder does not need to reproduce the reference encoder’s
match finder, parsing heuristics, frequency normalizer, candidate pruning,
or model-selection strategy — it must implement the `V9` bitstream format
correctly.

This distinction permits substantial implementation freedom.

For example, an independent encoder can use a different match finder, a
different cost model, a different frequency-normalization algorithm, different
compression-level search strategies, different parallel scheduling,
architecture-specific vectorization, and alternative internal memory layouts.
Provided the resulting bitstream conforms to `V9`, it remains fully compatible.

The decoder’s requirements are more specific.  It must correctly interpret
frame and block headers, symbol representations, all entropy descriptor modes,
deterministic `tANS` table construction, reverse-consumed bitstreams, model
reuse, phase-dependent streams, length and distance codes, repeat-distance
updates, `RUN` semantics, overlapping match reconstruction, history
persistence, and canonical checksums.

In particular, overlapping matches must be reconstructed using the format’s
forward-copy semantics. A conventional C `memcpy` is not suitable when source
and destination ranges overlap.

Likewise, checksums must be calculated over the canonical logical symbol
representation rather than simply over whatever bytes happen to be written
to an `8`‑bit per character output file.

These details should be considered part of the interoperability contract.

### Memory Characteristics

The reference implementation provides useful evidence of the format’s
bounded memory architecture.

Its documented Linux `x86‑64` allocation measurements include:

|                        Configuration | Explicit allocation |
|-------------------------------------:|:-------------------:|
| `32K` encoder, levels `1`–`5`        | `634,990` bytes     |
| `32K` encoder, levels `6`–`9`        | `678,686` bytes     |
| `64K` encoder, levels `1`–`5`        | `831,598` bytes     |
| `64K` encoder, levels `6`–`9`        | `875,294` bytes     |
| `32K` `9`‑bit decoder                | `189,004` bytes     |
| `64K` `9`‑bit decoder                | `254,540` bytes     |
| `32K` decoder, max. phased tables    | `213,604` bytes     |
| `64K` decoder, max. phased tables    | `279,140` bytes     |

These figures describe the documented codec allocations, not total process
resident memory. They exclude factors such as allocator overhead, some
I/O buffers, worker-stack reservations, and platform-specific runtime
requirements.

The decoder also allocates phased tables lazily, avoiding their cost when
ordinary entropy descriptors are sufficient.

For `8`‑bit output, the history ring can use one byte per symbol rather than
two, reducing its memory requirement.

The measurements demonstrate that the reference architecture has been
designed with bounded memory consumption in mind.

For deployments with strict resource requirements, these figures should be
treated as implementation-specific baselines. Each production implementation
should measure its own complete memory footprint under the intended
configuration, including any concurrency-related allocations.

### Practical Advantages and Intended Applications

The combination of the following described capabilities gives **LZ9ANS** a
distinctive range of applications.

#### Historical computing environments:

Native `9`‑bit symbol support makes **LZ9ANS** directly relevant to
environments involving Multics, OS2200, and other systems with `36`‑bit
word (non-octet) oriented logical data.

The codec can preserve the complete logical symbol range without forcing
the user to repack the bitstream or represent every character as a padded
“`16`‑bit” value.

#### Archival and interchange of specialized data:

The explicit framing and portable representation provide a defined format
for storing and exchanging `9`‑bit data.

The separation between compression and external metadata also permits
**LZ9ANS** to be incorporated into larger archival or interchange systems.

#### Conventional binary compression:

The `8`‑bit mode allows **LZ9ANS** to operate on ordinary binary data without
requiring specialized preprocessing.

The adaptive choice between `RAW`, `RLE`, `tANS`, `REUSE`, and `STORED`
representations helps avoid unnecessary overhead on blocks that do not benefit
from compression.

#### Constrained and embedded systems:

The fixed `tANS` state space, bounded blocks, explicit length limits, and
predictable history requirements provide a basis for implementation in
resource-constrained environments.

#### Multicore systems:

Optional independent-block compression and bounded entropy-decoding pipelines
provide opportunities to use multiple processors without changing the `V9`
wire format.

#### Hardware-assisted and hardware-oriented implementations:

The format’s bounded state and explicit data dependencies make it a
candidate for optimized hardware implementation.

The deterministic entropy tables, limited state dimensions, and separate
streams are particularly useful architectural properties in this context.

### Comparison with Conventional Compression Designs

The strengths of **LZ9ANS** are easier to understand when considered against
common assumptions in compression software.

Conventional compression software assumes that all input is fundamentally a
sequence of `8`‑bit bytes, symbols are modeled using one common literal
distribution, longer `LZ` matches are generally preferred, every stream uses
the same entropy-coding approach, entropy tables must be transmitted
repeatedly, blocks are natural dictionary reset points, distance codes
represent offsets uniformly, and codec runtime resource requirements emerge
or evolve from implementation behavior.

Conversely, for the **LZ9ANS** design, first-class input is sequence of logical
`9`‑bit symbols (with an `8`‑bit mode also supported), optional phase-dependent
distributions exploit `36`‑bit word positions, cost-aware parsing considers the
estimated entropy cost of the complete representation, each stream
independently selects an appropriate representation, persistent model caches
support model reuse across blocks, entropy state resets per block while
dictionary history persists, repeat-distance slots are supported, `RUN` and
aligned-distance coding exploit recurring structures, and all important format
structures have explicit bounds which define the runtime resource requirements.

This comparison is not intended to suggest that these features are absent
from every other compression format. Rather, it illustrates the particular
combination of decisions made by **LZ9ANS** and the way they support its
specialized goals.

### Trade-offs and Implementation Considerations

The architecture provides considerable flexibility, but it also introduces
requirements that independent implementations must handle carefully.

* First, a `9`‑bit symbol architecture requires deliberate handling of packing,
  unpacking, and canonical checksums. Implementations must not assume that host
  byte representation and logical symbol representation are interchangeable.

* Second, phase-dependent entropy coding adds state and descriptor
  complexity. The decoder must calculate the correct absolute frame phase
  and correctly select the model associated with each symbol.

* Third, persistent dictionary history and repeat-distance state mean
  that blocks cannot be decoded as wholly independent units. Parallel
  implementations must preserve the specified dependencies even when entropy
  decoding is performed concurrently.

* Fourth, the entropy coder’s bitstream convention is specific to **LZ9ANS**.

* Finally, encoder optimization is inherently a trade-off between CPU time,
  memory, and output size. Higher compression levels increase search effort,
  but the format does not guarantee that every higher level will produce a
  smaller file on every input.

These considerations are manageable, but they reinforce the importance of
treating the `V9` specification and conformance tests as the authoritative
basis for independent implementations.

### Conclusion

**LZ9ANS** is distinguished not by a single isolated compression algorithm,
but by the way several architectural and algorithmic techniques have been
brought together into one bounded, portable format.

Its native `9`‑bit symbol model addresses a genuine limitation of conventional
byte-oriented codecs. Its `36`‑bit-word-aware entropy and distance coding
extend that capability beyond simple symbol packing, allowing the codec
to exploit structural properties of the target data.

The combination of `LZ77` and deterministic table-based `tANS` provides a
flexible compression architecture. Separate streams, multiple entropy
representations, persistent model reuse, and integer-based frequency
normalization allow the encoder to adapt its representation to the contents
of each block.

The reference parser goes beyond conventional longest-match
selection by incorporating estimated entropy costs into dictionary
decisions. Repeat-distance slots, `RUN` coding, aligned distances, and
selective short matches further demonstrate how the dictionary and entropy
layers are designed to complement one another.

At the implementation level, bounded block sizes, history windows, entropy
states, descriptor areas, and payload lengths provide predictable resource
requirements. Explicit stream framing, validation rules, checksums, and
deterministic entropy construction support independent implementations
and provide a foundation for optimized software and hardware designs.

Equally important, the format does not require every implementation to
reproduce the reference encoder’s particular optimization strategies.

The division between normative decoding behavior and encoder-specific decisions
permits independent implementations to improve parsing, vectorization, memory
layout, concurrency, and hardware utilization while preserving compatibility.

**LZ9ANS** therefore represents a useful approach to a broader compression
design problem: how to build a general-purpose codec that can efficiently
support resource-constrained legacy mainframes (or low-memory embedded systems)
using non-standard logical symbol widths and specialized machine architectures
without giving up the practical characteristics expected of modern
compression software.

Its distinctive strengths are the combination of:

 1. **Native `9`‑bit data support**, without requiring repacking or padding.

 2. **Direct adaptation to `36`‑bit architectures**, through phase-dependent
    entropy coding and aligned-distance representations.

 3. **A bounded block and history architecture**, with explicit resource
    limits.

 4. **Deterministic table-based `tANS`**, with a compact fixed state space.

 5. **Adaptive entropy representations and persistent model reuse**, avoiding
    unnecessary model overhead.

 6. **Cost-aware `LZ` parsing**, considering the final entropy-coded cost of
    dictionary matches.

 7. **Optimized repeat-distance and `RUN` handling**, improving the
    representation of recurring structures.

 8. **Selective `STORED` blocks**, efficiently avoiding unnecessary compression
    work and expansion.

 9. **Optional parallel processing**, without introducing a separate or
    otherwise specialized wire format.

10. **A portable, hardware-conscious implementation architecture**, supporting
   both conventional software and future specialized implementations.

The most compelling aspect of **LZ9ANS** is that these capabilities are *not*
independent additions; they reinforce one another: the symbol architecture
informs the phase model; the phase model complements dictionary alignment;
the dictionary parser uses entropy costs; the entropy streams use persistent
models; and the bounded block architecture provides the foundation for
validation, parallelism, and hardware implementation.

### Acknowledgments

The author gratefully acknowledges the use of the
**REDUCE Computer Algebra System**, which was instrumental in the prototyping,
design evaluation, and early testing phases of this work.

### More Information

* **GitHub**: [`https://github.com/johnsonjh/lz9ans`](https://github.com/johnsonjh/lz9ans)

* **GitLab**: [`https://gitlab.com/johnsonjh/lz9ans`](https://gitlab.com/johnsonjh/lz9ans)

### See Also

* J. Ziv and A. Lempel,
  “**A Universal Algorithm for Sequential Data Compression**,”
  *IEEE Trans. Inf. Theory*, vol. 23, no. 3, pp. 337–343, May 1977,
  [doi: 10.1109/TIT.1977.1055714](http://dx.doi.org/10.1109/TIT.1977.1055714).

* J. Duda,
  “**Asymmetric numeral systems**,” 2009,
  [arXiv:0902.0271](https://arxiv.org/abs/0902.0271).

* J. Duda,
  “**Asymmetric numeral systems: entropy coding combining speed of Huffman
  coding with compression rate of arithmetic coding**,”
  2013, [arXiv:1311.2540](https://arxiv.org/abs/1311.2540).

* Y. Collet and M. Kucherawy,
  “**Zstandard Compression**,”
  *Internet Engineering Task Force (IETF), Request for Comments (RFC) 8878*,
  Feb. 2021, [doi: 10.17487/RFC8878](http://dx.doi.org/10.17487/RFC8878).

* Y. Collet,
  “**Finite State Entropy**,” 2013–2024,
  [New Generation Entropy coders](https://github.com/cyan4973/finitestateentropy),
  [https://github.com/cyan4973/finitestateentropy](https://github.com/cyan4973/finitestateentropy).

* K. Szewczyk,
  “**Linear KL–Optimal Frequency Normalisation**,” 2026,
  [arXiv:2605.00579](https://arxiv.org/abs/2605.00579).

* V. Gopal, E. Ozturk, J. Guilford, G. Wolrich, W. K. Feghali, and M. G. Dixon,
  “**Fast CRC Computation for Generic Polynomials Using `PCLMULQDQ` Instruction**,”
  [*Intel Corporation, White Paper*, Order No. 323102, Dec. 2009](https://fossies.org/linux/zlib-ng/doc/crc-pclmulqdq.pdf).

* E. L. Glaser, J. F. Couleur, and G. A. Oliver,
  “**System design of a computer for time–sharing applications**,”
  in *Proceedings of the November 30–December 1, 1965,
  Fall Joint Computer Conference, Part I* (AFIPS ’65), pp. 197–202,
  [doi: 10.1145/1463891.1463913](http://dx.doi.org/10.1145/1463891.1463913).

* A. C. Hearn, A. C. Norman, et al.,
  “**REDUCE Computer Algebra System**,” 1968–2026,
  [Reduce Algebra Project](https://reduce-algebra.sourceforge.io/),
  [https://reduce‑algebra.sourceforge.io/](https://reduce-algebra.sourceforge.io/).

<!-- vim: set ft=pandoc expandtab conceallevel=0 cc=80 : -->
<!-- EOF -->
