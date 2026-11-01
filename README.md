<!-- Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com> -->
<!-- Copyright (c) 2025-2026 The DPS8M Development Team -->
<!-- SPDX-License-Identifier: MIT -->
# LZ9ANS

**LZ9ANS** is a 9‑bit [LZ77](https://en.wikipedia.org/wiki/LZ77_and_LZ78) +
[tANS](https://en.wikipedia.org/wiki/Asymmetric_numeral_systems)
codec designed to efficiently compress **9‑bit** mainframe input data (*e.g.*,
**Multics**, **OS2200**) to standard **8‑bit** output streams.  It offers nine
compression profiles: four *fast*, four *tight*, one *maximum*.

## Performance

**LZ9ANS‑32K** (corpus of `446,584,168` input bits) [486 KiB memory usage]**:**

| Level  | Compressed bits | Percent   | Ratio     | Delta          |
|-------:|:---------------:|:---------:|:---------:|---------------:|
| **‑1** | `156,524,320`   | `64.951%` | `2.853:1` | `‑290,059,848` |
| **‑2** | `153,334,488`   | `65.665%` | `2.912:1` |   `‑3,189,832` |
| **‑3** | `151,348,568`   | `66.110%` | `2.951:1` |   `‑1,985,920` |
| **‑4** | `150,208,096`   | `66.365%` | `2.973:1` |   `‑1,140,472` |
| **‑5** | `144,297,288`   | `67.689%` | `3.095:1` |   `‑5,910,808` |
| **‑6** | `143,680,056`   | `67.827%` | `3.108:1` |     `‑617,232` |
| **‑7** | `143,324,992`   | `67.906%` | `3.116:1` |     `‑355,064` |
| **‑8** | `143,028,680`   | `67.973%` | `3.122:1` |     `‑296,312` |
| **‑9** | `142,763,040`   | `68.032%` | `3.128:1` |     `‑265,640` |

**LZ9ANS‑64K** (corpus of `446,584,168` input bits) [678 KiB memory usage]**:**

| Level  | Compressed bits | Percent   | Ratio     | Delta          |
|-------:|:---------------:|:---------:|:---------:|---------------:|
| **‑1** | `155,032,776`   | `65.285%` | `2.881:1` | `‑291,551,392` |
| **‑2** | `151,466,024`   | `66.083%` | `2.948:1` |   `‑3,566,752` |
| **‑3** | `149,144,208`   | `66.603%` | `2.994:1` |   `‑2,321,816` |
| **‑4** | `147,730,272`   | `66.920%` | `3.023:1` |   `‑1,413,936` |
| **‑5** | `140,807,840`   | `68.470%` | `3.172:1` |   `‑6,922,432` |
| **‑6** | `140,053,104`   | `68.639%` | `3.189:1` |     `‑754,736` |
| **‑7** | `139,593,320`   | `68.742%` | `3.199:1` |     `‑459,784` |
| **‑8** | `139,226,584`   | `68.824%` | `3.208:1` |     `‑366,736` |
| **‑9** | `138,901,784`   | `68.897%` | `3.215:1` |     `‑324,800` |

### Competition

Compare the same corpus packed with popular (8‑bit) compression algorithms
(at their highest settings)**:**

|    Utility | Algorithm              | Compressed bits | Memory       | Performance      |
|-----------:|:-----------------------|:---------------:|-------------:|-----------------:|
| `compress` | LZW                    | `279,608,608`   | 400&nbsp;KiB | 101%&nbsp;larger |
| `lz4`      | LZ77                   | `221,797,064`   | 512&nbsp;KiB |  60%&nbsp;larger |
| `gzip`     | DEFLATE&nbsp;(LZ77+HC) | `202,579,712`   | 350&nbsp;KiB |  46%&nbsp;larger |
| `bzip2`    | BWT+MTF+HC             | `177,850,592`   | 1.2&nbsp;MiB |  28%&nbsp;larger |
| `zstd`     | LZ77+FSE+ANS           | `170,129,736`   | 10&nbsp;MiB  |  23%&nbsp;larger |
| `bzip3`    | BWT+MTF+HC             | `169,815,400`   | 8.2&nbsp;MiB |  22%&nbsp;larger |
| `brotli`   | LZ77+HC+CD             | `161,977,528`   | 210&nbsp;MiB |  17%&nbsp;larger |
| `lzip`     | LZMA                   | `158,016,800`   | 13&nbsp;MiB  |  14%&nbsp;larger |
| `zpaq`     | M5&nbsp;(CM+AC)        | `131,954,176`   | 246&nbsp;MiB |  5%&nbsp;smaller |

## Specification

See [`FORMAT.md`](FORMAT.md) for the complete specification.

## License

**LZ9ANS** is made available under the terms of the [MIT License](LICENSE).
