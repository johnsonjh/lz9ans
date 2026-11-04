<!-- Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com> -->
<!-- Copyright (c) 2025-2026 The DPS8M Development Team -->
<!-- SPDX-License-Identifier: MIT -->
# LZ9ANS

**LZ9ANS** is a **9‑bit** [LZ77](https://en.wikipedia.org/wiki/LZ77_and_LZ78) +
[tANS](https://en.wikipedia.org/wiki/Asymmetric_numeral_systems) codec designed
to efficiently compress **9‑bit** mainframe input data (*e.g.*, **Multics**,
**OS2200**) to standard **8‑bit** portable output streams in hardware
and software.

It features two window sizes (32&nbsp;KiB and 64&nbsp;KiB), deterministic
runtime memory usage (compressing in 486&nbsp;KiB or 678&nbsp;KiB,
decompressing in 206&nbsp;KiB or 270&nbsp;KiB), 8‑bit data compatibility,
word‑aligned offset optimizations for data with 36‑bit boundaries, and fast
decompression speeds.

The algorithm may be tuned to favor saving time or space, with the ANSI C89
reference implementation offering nine different compression profiles: four
*fast*, four *tight*, and one *maximum*.

## Performance

**LZ9ANS‑32K** (corpus of `446,584,168` input bits)
[**486** **KiB** memory usage]:

|    Level | Compressed bits |  Percent  |   Ratio   |          Delta |
|---------:|:---------------:|:---------:|:---------:|---------------:|
| **`‑1`** |  `156,524,320`  | `64.951%` | `2.853:1` | `‑290,059,848` |
| **`‑2`** |  `153,334,488`  | `65.665%` | `2.912:1` |   `‑3,189,832` |
| **`‑3`** |  `151,348,568`  | `66.110%` | `2.951:1` |   `‑1,985,920` |
| **`‑4`** |  `150,173,456`  | `66.373%` | `2.974:1` |   `‑1,175,112` |
| **`‑5`** |  `144,212,568`  | `67.708%` | `3.097:1` |   `‑5,960,888` |
| **`‑6`** |  `143,597,728`  | `67.845%` | `3.110:1` |     `‑614,840` |
| **`‑7`** |  `143,247,824`  | `67.924%` | `3.118:1` |     `‑349,904` |
| **`‑8`** |  `142,979,328`  | `67.984%` | `3.123:1` |     `‑268,496` |
| **`‑9`** |  `142,759,776`  | `68.033%` | `3.128:1` |     `‑205,480` |

**LZ9ANS‑64K** (corpus of `446,584,168` input bits)
[**678** **KiB** memory usage]:

|    Level | Compressed bits |  Percent  |   Ratio   |          Delta |
|---------:|:---------------:|:---------:|:---------:|---------------:|
| **`‑1`** |  `155,032,776`  | `65.285%` | `2.881:1` | `‑291,551,392` |
| **`‑2`** |  `151,466,024`  | `66.083%` | `2.948:1` |   `‑3,566,752` |
| **`‑3`** |  `149,144,208`  | `66.603%` | `2.994:1` |   `‑2,321,816` |
| **`‑4`** |  `147,680,264`  | `66.931%` | `3.024:1` |   `‑1,463,944` |
| **`‑5`** |  `140,692,744`  | `68.496%` | `3.174:1` |   `‑6,987,520` |
| **`‑6`** |  `139,938,088`  | `68.665%` | `3.191:1` |     `‑754,656` |
| **`‑7`** |  `139,483,248`  | `68.767%` | `3.202:1` |     `‑454,840` |
| **`‑8`** |  `139,146,440`  | `68.842%` | `3.209:1` |     `‑336,808` |
| **`‑9`** |  `138,890,944`  | `68.899%` | `3.215:1` |     `‑240,992` |

On Multics (and OS2200) systems, an **11%** on‑disk savings can be realized by
packing the 8‑bit **LZ9ANS** output stream to a more compact (but less
portable) 9‑bit per character storage representation.

### Competition

Compare the numbers above with the results of the same corpus packed with
popular 8‑bit compression algorithms (all at their maximum compression
settings), as well as **LZ9ANS** operating in 8‑bit mode:

|      Utility | Algorithm              | Compressed bits | Memory           | Performance      |
|-------------:|:-----------------------|:---------------:|-----------------:|-----------------:|
| `lzfx`       | LZF&nbsp;(LZSS)        |  `281,115,248`  |      70&nbsp;KiB | 102%&nbsp;larger |
| `compress`   | LZW                    |  `279,608,608`  |     400&nbsp;KiB | 101%&nbsp;larger |
| `lz4`        | LZ77+VLE               |  `221,797,064`  |     512&nbsp;KiB |  60%&nbsp;larger |
| `lzop`       | LZO1X&nbsp;(LZ77+BC)   |  `212,130,184`  |      70&nbsp;KiB |  53%&nbsp;larger |
| `gzip`       | DEFLATE&nbsp;(LZ77+HC) |  `202,579,712`  |     350&nbsp;KiB |  46%&nbsp;larger |
| `lzfse`      | LZFSE&nbsp;(LZ77+FSE)  |  `194,587,720`  |      74&nbsp;KiB |  40%&nbsp;larger |
| **`lz9ans`** | **LZ9ANS‑32K**         |  `192,950,176`  | **486&nbsp;KiB** |  39%&nbsp;larger |
| **`lz9ans`** | **LZ9ANS‑64K**         |  `185,895,464`  | **678&nbsp;KiB** |  34%&nbsp;larger |
| `bzip2`      | BWT+MTF+HC             |  `177,850,592`  |     1.2&nbsp;MiB |  28%&nbsp;larger |
| `zstd`       | LZ77+HC+FSE            |  `170,129,736`  |      10&nbsp;MiB |  23%&nbsp;larger |
| `bzip3`      | BWT+LZP+CM             |  `169,815,400`  |     8.2&nbsp;MiB |  22%&nbsp;larger |
| `brotli`     | LZ77+HC+CD             |  `161,977,528`  |     210&nbsp;MiB |  17%&nbsp;larger |
| `lzip`       | LZMA&nbsp;(LZ77+MA+RC) |  `158,016,800`  |      13&nbsp;MiB |  14%&nbsp;larger |
| `zpaq`       | M5&nbsp;(CM+AC)        |  `131,954,176`  |     246&nbsp;MiB |  5%&nbsp;smaller |

Even when processing the corpus *in 8‑bit mode* (that is, as `55,823,021`
8‑bit octets, rather than as `49,620,463` 9‑bit nonets), **LZ9ANS**, which is
tuned *explicitly* for compressing **9‑bit** data, more than holds it own and
is only surpassed by algorithms that require *substantially* more memory (2× to
500×) and/or processing time (enough to make native **Multics** implementations
impractical).

For most 8‑bit data, **LZ9ANS** **`-3`** meets or exceeds the performance of
**GNU Gzip** **`-6`**.

## Hardware

The overall codec architecture was deliberately engineered with hardware
friendliness in mind, sometimes to the detriment of compression ratio: all
lengths are hard‑bounded (symbols per block, and payload, descriptor, and
window sizes), ANS state is reset each block, decode tables change only via
explicit descriptors, LIT decode is independent of history and prior output,
etc.

These intentional design choices should make it straightforward to create
efficient and high‑throughput **LZ9ANS** implementations in ASIC and FPGA
environments.

## Specification

See the [`FORMAT.md`](FORMAT.md) document for the complete specification.

## License

**LZ9ANS** is made available under the terms of the [MIT License](LICENSE).
