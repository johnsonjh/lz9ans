<!-- Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com> -->
<!-- Copyright (c) 2025-2026 The DPS8M Development Team -->
<!-- SPDX-License-Identifier: MIT -->
<!-- scspell-id: cdb21990-df71-11f1-82f3-80ee73e9b8e7 -->
# LZ9ANS

<div align="center">
“Entropy is a figure of speech, then… a metaphor.”<br>
&nbsp;&nbsp;—&nbsp;John Nefastis (Pynchon, <i>The Crying of Lot 49</i>)
</div>

## Overview

**LZ9ANS** is a **9‑bit**
[**LZ77**](https://en.wikipedia.org/wiki/LZ77_and_LZ78) +
[**tANS**](https://en.wikipedia.org/wiki/Asymmetric_numeral_systems) codec
designed to efficiently compress **9‑bit** mainframe input data (*e.g.*,
**Multics**, **OS2200**) to standard **8‑bit** portable output streams in
hardware and software.

It features two window sizes (32&nbsp;KiB and 64&nbsp;KiB), deterministic
runtime memory usage (with one thread, compressing in 663&nbsp;KiB or
855&nbsp;KiB, and decompressing in 209&nbsp;KiB or 273&nbsp;KiB), 8‑bit data
compatibility, word‑aligned offset optimizations for data with 36‑bit
boundaries, and fast decompression speeds.

**LZ9ANS** uses a custom table‑based asymmetric numeral system (tANS)
implementation.  Its state transitions and decoding‑table organization follow
the same general approach used by **Finite State Entropy** (FSE), but its
descriptor encoding, table conventions, and output bitstream representation are
original and specific to **LZ9ANS**, with frequency normalization based on
K.&nbsp;Szewczyk’s Linear KL‑Optimal method.

The algorithm may be tuned to favor saving time or space, with the ANSI C89
reference implementation offering nine different compression profiles: four
*fast*, four *tight*, and one *maximum*.

**LZ9ANS** is capable of highly parallel compression and modestly parallel
decompression, with the reference implementation providing single‑threaded and
multi‑threaded encoder and decoder variants.

The overall codec architecture was deliberately engineered with hardware
friendliness in mind (more so than most ANS‑based schemes), sometimes to the
detriment of compression ratio: memory usage is deterministic, all per‑block
lengths and storage requirements are explicitly bounded (symbols per block,
payload and descriptor sizes, and window sizes), ANS state is reset
independently for each block, decode tables change only through explicit
descriptors, `LIT` symbol decoding is independent of history and previously
emitted output, etc.

These intentional design choices provide a predictable resource envelope and
explicit block‑level control, making **LZ9ANS** well suited to efficient
hardware implementations in ASIC and FPGA environments.  Achieving high
throughput will still require careful handling of the sequential dependencies
in entropy decoding and LZ match processing.

## Specification

See the [`docs/FORMAT.md`](docs/FORMAT.md) document for the complete
specification.

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
  “**A Universal Algorithm for Sequential Data Compression**,”
  *IEEE Trans. Inf. Theory*, vol. 23, no. 3, pp. 337–343, May 1977,
  [doi:&nbsp;10.1109/TIT.1977.1055714](http://dx.doi.org/10.1109/TIT.1977.1055714).

* J. Duda,
  “**Asymmetric numeral systems**,” 2009,
  [arXiv:0902.0271](https://arxiv.org/abs/0902.0271).

* J. Duda,
  “**Asymmetric numeral systems: entropy coding combining speed of
  Huffman coding with compression rate of arithmetic coding**,” 2013,
  [arXiv:1311.2540](https://arxiv.org/abs/1311.2540).

* Y. Collet and M. Kucherawy,
  “**Zstandard Compression**,”
  *Internet Engineering Task Force (IETF), Request for Comments (RFC) 8878*,
  Feb. 2021, [doi:&nbsp;10.17487/RFC8878](http://dx.doi.org/10.17487/RFC8878).

* Y. Collet,
  “**Finite State Entropy**,” 2013–2024,
  [New Generation Entropy coders](https://github.com/cyan4973/finitestateentropy).

* K. Szewczyk,
  “**Linear KL–Optimal Frequency Normalisation**,” 2026,
  [arXiv:2605.00579](https://arxiv.org/abs/2605.00579).

* V. Gopal, E. Ozturk, J. Guilford, G. Wolrich, W. K. Feghali, and M. G. Dixon,
  “**Fast CRC Computation for Generic Polynomials Using `PCLMULQDQ`
  Instruction**,” *Intel Corporation*, *White Paper*, Order No. 323102,
  Dec. 2009,
  [Fossies:&nbsp;`crc‑pclmulqdq.pdf`](https://fossies.org/linux/zlib-ng/doc/crc-pclmulqdq.pdf).

* E. L. Glaser, J. F. Couleur, and G. A. Oliver,
  “**System design of a computer for time–sharing applications**,” in
  *Proceedings of the November 30–December 1, 1965, Fall Joint Computer
  Conference, Part I* (AFIPS ’65), pp. 197–202,
  [doi:&nbsp;10.1145/1463891.1463913](http://dx.doi.org/10.1145/1463891.1463913).

* A. C. Hearn, A. C. Norman, *et al*.,
  “**REDUCE Computer Algebra System**,” 1968–2026,
  [Reduce Algebra Project](https://reduce-algebra.sourceforge.io/).

## License

**LZ9ANS** is made available under the terms of the [MIT License](LICENSE).
