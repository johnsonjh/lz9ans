<!-- Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com> -->
<!-- Copyright (c) 2025-2026 The DPS8M Development Team -->
<!-- SPDX-License-Identifier: MIT -->
# LZ9ANS

**LZ9ANS** is a **9‑bit**
[**LZ77**](https://en.wikipedia.org/wiki/LZ77_and_LZ78) +
[**tANS**](https://en.wikipedia.org/wiki/Asymmetric_numeral_systems) codec
designed to efficiently compress **9‑bit** mainframe input data (*e.g.*,
**Multics**, **OS2200**) to standard **8‑bit** portable output streams in
hardware and software.

It features two window sizes (32&nbsp;KiB and 64&nbsp;KiB), deterministic
runtime memory usage (compressing in 663&nbsp;KiB or 855&nbsp;KiB,
decompressing in 209&nbsp;KiB or 273&nbsp;KiB), 8‑bit data compatibility,
word‑aligned offset optimizations for data with 36‑bit boundaries, and fast
decompression speeds.

**LZ9ANS** uses a custom table‑based asymmetric numeral system (tANS)
implementation.  Its state transitions and decoding‑table organization follow
the same general approach used by **Finite State Entropy** (FSE), but its
normalization, descriptor encoding, table conventions, and output bitstream
representation are original and specific to **LZ9ANS**.

The algorithm may be tuned to favor saving time or space, with the ANSI C89
reference implementation offering nine different compression profiles: four
*fast*, four *tight*, and one *maximum*.

## Performance

**LZ9ANS‑32K** (corpus of `446,584,168` input bits)<br>
[**663** **KiB** encoder state; **209** **KiB** decoder state]:

|    Level | Compressed bits |  Percent  |   Ratio   |          Delta |
|---------:|:---------------:|:---------:|:---------:|---------------:|
| **`‑1`** |  `156,469,760`  | `64.963%` | `2.854:1` | `‑290,114,408` |
| **`‑2`** |  `153,279,928`  | `65.677%` | `2.914:1` |   `‑3,189,832` |
| **`‑3`** |  `151,294,008`  | `66.122%` | `2.952:1` |   `‑1,985,920` |
| **`‑4`** |  `150,118,896`  | `66.385%` | `2.975:1` |   `‑1,175,112` |
| **`‑5`** |  `144,158,008`  | `67.720%` | `3.098:1` |   `‑5,960,888` |
| **`‑6`** |  `140,781,144`  | `68.476%` | `3.172:1` |   `‑3,376,864` |
| **`‑7`** |  `140,425,104`  | `68.556%` | `3.180:1` |     `‑356,040` |
| **`‑8`** |  `140,142,088`  | `68.619%` | `3.187:1` |     `‑283,016` |
| **`‑9`** |  `139,847,432`  | `68.685%` | `3.193:1` |     `‑294,656` |

**LZ9ANS‑64K** (corpus of `446,584,168` input bits)<br>
[**855** **KiB** encoder state; **273** **KiB** decoder state]:

|    Level | Compressed bits |  Percent  |   Ratio   |          Delta |
|---------:|:---------------:|:---------:|:---------:|---------------:|
| **`‑1`** |  `154,978,216`  | `65.297%` | `2.882:1` | `‑291,605,952` |
| **`‑2`** |  `151,411,464`  | `66.096%` | `2.949:1` |   `‑3,566,752` |
| **`‑3`** |  `149,089,648`  | `66.616%` | `2.995:1` |   `‑2,321,816` |
| **`‑4`** |  `147,625,704`  | `66.943%` | `3.025:1` |   `‑1,463,944` |
| **`‑5`** |  `140,638,184`  | `68.508%` | `3.175:1` |   `‑6,987,520` |
| **`‑6`** |  `137,295,304`  | `69.257%` | `3.253:1` |   `‑3,342,880` |
| **`‑7`** |  `136,834,096`  | `69.360%` | `3.264:1` |     `‑461,208` |
| **`‑8`** |  `136,477,824`  | `69.440%` | `3.272:1` |     `‑356,272` |
| **`‑9`** |  `136,155,360`  | `69.512%` | `3.280:1` |     `‑322,464` |

On Multics (and OS2200) systems, an **11%** on‑disk savings can be realized by
packing the 8‑bit **LZ9ANS** output stream to a more compact (but less
portable) 9‑bit per character storage representation.

### Competition

Compare the numbers above with the results of the same corpus packed with
popular 8‑bit compression algorithms (all at their maximum compression
settings), as well as **LZ9ANS** operating in 8‑bit mode:

|      Utility | Algorithm              | Compressed bits | Memory           | Performance      |
|-------------:|:-----------------------|:---------------:|-----------------:|-----------------:|
| `lzfx`       | LZF&nbsp;(LZSS)        |  `281,115,248`  |      70&nbsp;KiB | 106%&nbsp;larger |
| `compress`   | LZW                    |  `279,608,608`  |     400&nbsp;KiB | 106%&nbsp;larger |
| `lz4`        | LZ77+VLE               |  `221,797,064`  |     512&nbsp;KiB |  63%&nbsp;larger |
| `lzop`       | LZO1X&nbsp;(LZ77+BC)   |  `212,130,184`  |      70&nbsp;KiB |  56%&nbsp;larger |
| `gzip`       | DEFLATE&nbsp;(LZ77+HC) |  `202,579,712`  |     350&nbsp;KiB |  49%&nbsp;larger |
| `lzfse`      | LZFSE&nbsp;(LZ77+FSE)  |  `194,587,720`  |      74&nbsp;KiB |  43%&nbsp;larger |
| **`lz9ans`** | **LZ9ANS‑32K**         |  `191,003,576`  | **663&nbsp;KiB** |  40%&nbsp;larger |
| **`lz9ans`** | **LZ9ANS‑64K**         |  `184,093,112`  | **855&nbsp;KiB** |  35%&nbsp;larger |
| `bzip2`      | BWT+MTF+HC             |  `177,850,592`  |     1.2&nbsp;MiB |  31%&nbsp;larger |
| `zstd`       | LZ77+HC+FSE            |  `170,129,736`  |      10&nbsp;MiB |  26%&nbsp;larger |
| `bzip3`      | BWT+LZP+CM             |  `169,815,400`  |     8.2&nbsp;MiB |  25%&nbsp;larger |
| `brotli`     | LZ77+HC+CD             |  `161,977,528`  |     210&nbsp;MiB |  19%&nbsp;larger |
| `lzip`       | LZMA&nbsp;(LZ77+MA+RC) |  `158,016,800`  |      13&nbsp;MiB |  16%&nbsp;larger |
| `zpaq`       | M5&nbsp;(CM+AC)        |  `131,954,176`  |     246&nbsp;MiB |  3%&nbsp;smaller |

<sup>Performance percentages above are relative to maximum 9‑bit **LZ9ANS**
performance.</sup>

Even when processing the corpus *in 8‑bit mode* (that is, as `55,823,021`
8‑bit octets, rather than as `49,620,463` 9‑bit nonets), **LZ9ANS**, which is
tuned *explicitly* for compressing **9‑bit** data, more than holds its own and
is only surpassed by algorithms that require *substantially* more memory (2× to
500×) and/or processing time (enough to make native **Multics** implementations
impractical).

## Hardware

The overall codec architecture was deliberately engineered with hardware
friendliness in mind (more so than most ANS‑based schemes), sometimes to the
detriment of compression ratio: memory usage is deterministic, all per‑block
lengths and storage requirements are explicitly bounded (symbols per block,
payload and descriptor sizes, and window sizes), ANS state is reset
independently for each block, decode tables change only through explicit
descriptors, LIT symbol decoding is independent of history and previously
emitted output, etc.

These intentional design choices provide a predictable resource envelope and
explicit block‑level control, making **LZ9ANS** well suited to efficient
hardware implementations in ASIC and FPGA environments.  Achieving high
throughput will still require careful handling of the sequential dependencies
in entropy decoding and LZ match processing.

## Specification

See the [`FORMAT.md`](FORMAT.md) document for the complete specification.

## Contributing

* Do **not** open unsolicited pull requests with large amounts of
  LLM‑generated code.  These will be immediately rejected.

* Usage of AI (artificial intelligence) tools by contributors *is* currently
  permitted, subject to the same terms and conditions as the
  [LLVM AI Tool Use Policy](https://llvm.org/docs/AIToolPolicy.html), but
  this permission may be withdrawn at any time and without notice.

## Notes

The author gratefully acknowledges the use of the **REDUCE Computer Algebra
System**, which was instrumental in the prototyping, design evaluation, and
early testing phases of this work.

## References

* J. Ziv and A. Lempel,
  "**A Universal Algorithm for Sequential Data Compression**,"
  *IEEE Trans. Inf. Theory*, vol. 23, no. 3, pp. 337–343, May 1977,
  [doi:&nbsp;10.1109/TIT.1977.1055714](http://dx.doi.org/10.1109/TIT.1977.1055714).

* J. Duda,
  "**Asymmetric numeral systems**," 2009,
  [arXiv:0902.0271](https://arxiv.org/abs/0902.0271).

* J. Duda,
  "**Asymmetric numeral systems: entropy coding combining speed of
  Huffman coding with compression rate of arithmetic coding**," 2013,
  [arXiv:1311.2540](https://arxiv.org/abs/1311.2540).

* Y. Collet and M. Kucherawy,
  "**Zstandard Compression**,"
  *Internet Engineering Task Force (IETF), Request for Comments (RFC) 8878*,
  Feb. 2021, [doi:&nbsp;10.17487/RFC8878](http://dx.doi.org/10.17487/RFC8878).

* V. Gopal, E. Ozturk, J. Guilford, G. Wolrich, W. K. Feghali, and M. G. Dixon,
  "**Fast CRC Computation for Generic Polynomials Using PCLMULQDQ
  Instruction**," *Intel Corporation*, *White Paper*, Order No. 323102,
  Dec. 2009,
  [Fossies:&nbsp;`crc‑pclmulqdq.pdf`](https://fossies.org/linux/zlib-ng/doc/crc-pclmulqdq.pdf).

* E. L. Glaser, J. F. Couleur, and G. A. Oliver,
  "**System design of a computer for time–sharing applications**," in
  *Proceedings of the November 30–December 1, 1965, Fall Joint Computer
  Conference, Part I* (AFIPS '65), pp. 197–202,
  [doi:&nbsp;10.1145/1463891.1463913](http://dx.doi.org/10.1145/1463891.1463913).

* A. C. Hearn, A. C. Norman, *et al*., "**REDUCE Computer Algebra System**,"
  1968–2026,
  [Reduce Algebra Project](https://reduce-algebra.sourceforge.io/).

## License

**LZ9ANS** is made available under the terms of the [MIT License](LICENSE).
