# dmcomp

DMOD compression: a **DEFLATE** ([RFC 1951](https://www.rfc-editor.org/rfc/rfc1951)) coder and decoder in one small C library,
so that the host tools and the target run the very same implementation.

* **Decoder** (`dmcomp_inflate`) – for the target. 1.7 kB of code on Cortex-M, no heap, no static data (reentrant),
  nothing but `<stddef.h>`, `<stdint.h>` and `memcpy`. The output buffer is the window. The tables of the Huffman codes
  (~2.9 kB) are on the stack in `dmcomp_inflate`, or in memory of the caller's choice in `dmcomp_inflate_ws` (then the
  stack of the decoder is 192 B) – use that one in a thread with a small stack.
* **Encoder** (`dmcomp_deflate`) – for the host (`todmfc`, the build). Hash chains, lazy matching, nine levels, length
  limited Huffman codes; every block is written as the smallest of stored / fixed / dynamic. ~330 kB of work memory.
  `-DDMCOMP_BUILD_ENCODER=OFF` leaves it out.
* The streams are **plain DEFLATE** (no zlib or gzip wrapper – the sizes travel beside the data, e.g. in the DMFC
  header): zlib reads what `dmcomp_deflate` writes (`inflateInit2(..., -15)`) and `dmcomp_inflate` reads what zlib writes.

## Numbers

The 35 modules of the internal flash of the `stm32f746g-disco` firmware of
[dmod-boot](https://github.com/choco-technologies/dmod-boot) (316 890 bytes as `.dmf`):

| | size | |
|---|---:|---|
| FastLZ level 2 (the DMFC files now) | 214 595 B | 67.7 % |
| **dmcomp, level 9** | **172 331 B** | 54.4 % (**-42 kB**) |
| dmcomp, level 6 | 172 468 B | 54.4 % |
| zlib -9 (reference) | 172 337 B | 54.4 % |

Decoding on a PC (all 35 modules): FastLZ 643 MB/s, dmcomp 197 MB/s, zlib 318 MB/s. On Cortex-M7 the decoder is
`text` 1700 B (M0+: 1806 B), stack 2760 B (192 B with `dmcomp_inflate_ws`) – `scripts/check_size.sh` checks it in the CI.

## API

```c
#include "dmcomp.h"

size_t packed_size, size;

// Host
size_t bound = dmcomp_deflate_bound( size );               // enough for any data (incompressible data is stored)
int result = dmcomp_deflate( data, size, packed, bound, &packed_size, 9 /* level: 0 stored, 1 fast .. 9 small */ );

// Target: the size of the result is known (e.g. the OriginalSize of the DMFC header)
result = dmcomp_inflate( packed, packed_size, out, size, &out_size );

// ...or with the tables not on the stack (a thread with a small one)
void* workspace = malloc( dmcomp_inflate_workspace_size() );
result = dmcomp_inflate_ws( packed, packed_size, out, size, &out_size, workspace );
if( result != DMCOMP_OK ) log( "%s", dmcomp_strerror( result ) );
```

`dmcomp_inflate` does not trust its input: nothing is read beyond `src + src_size` and nothing written beyond
`dst + dst_capacity`, whatever the bytes are. The results are `DMCOMP_ERR_DATA` (not valid DEFLATE),
`DMCOMP_ERR_TRUNCATED` (the data ends too early), `DMCOMP_ERR_OVERFLOW` (the output buffer is too small),
`DMCOMP_ERR_PARAM`; the encoder also has `DMCOMP_ERR_MEMORY`.

RAM of the decoder: `DMCOMP_LIT_FAST_BITS` (default 9: 1 kB) and `DMCOMP_DIST_FAST_BITS` (7: 256 B) are the sizes of the
lookup tables – fewer bits is less stack and a bit slower.

## Building

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
build/dmcomp_tool t -l 9 file...        # sizes after compression, with a round trip check
```

Options: `DMCOMP_BUILD_ENCODER` (ON), `DMCOMP_BUILD_TESTS`, `DMCOMP_BUILD_TOOLS`, `DMCOMP_WITH_ZLIB` (ON: test against zlib
when it is installed), `DMCOMP_SANITIZE` (AddressSanitizer + UBSanitizer), `DMCOMP_WERROR`. To use it in another project:
`add_subdirectory(dmcomp)` (or `FetchContent`) and link `dmcomp`; it is plain C99 and has no dependencies.

## Tests

| test | what it checks |
|---|---|
| `test_vectors` | streams made by zlib (fixed, dynamic, stored, Huffman only) and by hand (every kind of invalid stream: reserved block type, bad `LEN`/`NLEN`, distance too far, reserved codes, over-subscribed and incomplete codes, repeat with nothing to repeat, missing end of block, every cut of a stream) |
| `test_roundtrip` | 9 kinds of data × 34 sizes (0 … 400 000 bytes, around every boundary: 258, 32768, 65535) × levels 0-9, in buffers of *exactly* the right size (AddressSanitizer sees every access outside), the size bound, an output buffer one byte too small, megabyte inputs with dozens of blocks, Huffman trees deeper than 15 |
| `test_huffman` | the length limited Huffman codes on their own: 100 000 random frequency sets must give a complete prefix code within the limit |
| `test_corrupt` | damaged streams (flipped bits, cuts, random bytes): may fail, must not crash, hang or touch memory outside the buffers |
| `test_zlib` | everything zlib writes (levels 0-9, all strategies) is read here, everything written here is read by zlib; the sizes stay in the neighbourhood of zlib's |

The CI (`.github/workflows/ci.yml`) runs them with gcc and clang, in Release and with the sanitizers, in 32 bit, and builds
the decoder for Cortex-M0+/M4/M7 (code ≤ 2 kB, stack ≤ 3 kB, no static data).

## Plan

* `dmod`: a `deflate` method beside `fastlz` in `Dmod_Compression_*` (`todmfc` then writes it, the loader reads it);
* `dmod-boot`: a build step that repacks the modules of the firmware (`fastlz` → `deflate`), −42 kB of the internal flash.
