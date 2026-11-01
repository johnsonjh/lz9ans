<!-- Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com> -->
<!-- Copyright (c) 2025-2026 The DPS8M Development Team -->
<!-- SPDX-License-Identifier: MIT -->
# LZ9ANS

**LZ9ANS** is a 9‑bit [LZ77](https://en.wikipedia.org/wiki/LZ77_and_LZ78) +
[tANS](https://en.wikipedia.org/wiki/Asymmetric_numeral_systems)
codec designed to efficiently compress 9‑bit mainframe input data
(*e.g.*, Multics, OS2200) to standard 8‑bit output streams.  It offers nine
compression profiles: four "fast", four "tight", one "maximum".

## Performance

**LZ9ANS‑32K** (corpus of `446,584,168` input bits) [486 KiB memory usage]:

| Level | Compressed size    | Percent   | Ratio     | Delta vs. last level |
|------:|:-------------------|:----------|:----------|---------------------:|
| ‑1    | `156,524,320` bits | `64.951%` | `2.853:1` | `‑290,059,848` delta |
| ‑2    | `153,334,488` bits | `65.665%` | `2.912:1` |   `‑3,189,832` delta |
| ‑3    | `151,348,568` bits | `66.110%` | `2.951:1` |   `‑1,985,920` delta |
| ‑4    | `150,208,096` bits | `66.365%` | `2.973:1` |   `‑1,140,472` delta |
| ‑5    | `144,297,288` bits | `67.689%` | `3.095:1` |   `‑5,910,808` delta |
| ‑6    | `143,680,056` bits | `67.827%` | `3.108:1` |     `‑617,232` delta |
| ‑7    | `143,324,992` bits | `67.906%` | `3.116:1` |     `‑355,064` delta |
| ‑8    | `143,028,680` bits | `67.973%` | `3.122:1` |     `‑296,312` delta |
| ‑9    | `142,763,040` bits | `68.032%` | `3.128:1` |     `‑265,640` delta |

**LZ9ANS‑64K** (corpus of `446,584,168` input bits) [678 KiB memory usage]:

| Level | Compressed size    | Percent   | Ratio     | Delta vs. last level |
|------:|:-------------------|:----------|:----------|---------------------:|
| ‑1    | `155,032,776` bits | `65.285%` | `2.881:1` | `‑291,551,392` delta |
| ‑2    | `151,466,024` bits | `66.083%` | `2.948:1` |   `‑3,566,752` delta |
| ‑3    | `149,144,208` bits | `66.603%` | `2.994:1` |   `‑2,321,816` delta |
| ‑4    | `147,730,272` bits | `66.920%` | `3.023:1` |   `‑1,413,936` delta |
| ‑5    | `140,807,840` bits | `68.470%` | `3.172:1` |   `‑6,922,432` delta |
| ‑6    | `140,053,104` bits | `68.639%` | `3.189:1` |     `‑754,736` delta |
| ‑7    | `139,593,320` bits | `68.742%` | `3.199:1` |     `‑459,784` delta |
| ‑8    | `139,226,584` bits | `68.824%` | `3.208:1` |     `‑366,736` delta |
| ‑9    | `138,901,784` bits | `68.897%` | `3.215:1` |     `‑324,800` delta |

### Competition

Compare the same corpus with popular (8‑bit) compression algorithms
(at their highest settings):

| Utility | Algorithm       | Compressed size         | Memory usage | Performance     |
|--------:|:----------------|:------------------------|:-------------|:----------------|
| `gzip`  | DEFLATE         | `202,579,712`&nbsp;bits | 350&nbsp;KiB | 46%&nbsp;larger |
| `bzip2` | BWT+MTF+HC      | `177,850,592`&nbsp;bits | 1.2&nbsp;MiB | 28%&nbsp;larger |
| `zstd`  | LZ77+FSE+HC     | `170,129,736`&nbsp;bits | 10&nbsp;MiB  | 22%&nbsp;larger |
| `lzip`  | LZMA            | `158,016,800`&nbsp;bits | 13&nbsp;MiB  | 14%&nbsp;larger |
| `zpaq`  | M5&nbsp;(CM+AC) | `131,954,176`&nbsp;bits | 246&nbsp;MiB | 5%&nbsp;smaller |

## Specification

See [`FORMAT.md`](FORMAT.md) for the complete specification.
