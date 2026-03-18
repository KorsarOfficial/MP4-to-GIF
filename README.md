# mp4togif

MP4 → GIF. Two-pass pipeline: histogram → quantization → dithered encoding.

$$\text{MP4} \xrightarrow{F_{\text{dec}}} \text{RGB} \xrightarrow{H} \text{Hist}_{33^3} \xrightarrow{Q} P_{K \leq 256} \xrightarrow{D} \text{Idx} \xrightarrow{\text{LZW}} \text{GIF89a}$$

## Algorithms

| Module | Method | Complexity |
|--------|--------|------------|
| Quantization | Wu's optimal ($\sigma^2$ minimization, $[0,32]^3$) | $O(N + K \cdot 33^3)$ |
| Quantization | Median-cut (longest-axis, population median) | $O(K \cdot 33^3)$ |
| Dithering | Floyd-Steinberg (serpentine, 2-row error ring) | $O(WH)$/frame |
| Dithering | Bayer 8x8 ordered | $O(WH)$/frame |
| Nearest color | 32K cache, AVX2 precompute | $O(1)$ lookup |
| GIF | Hash-table LZW (12-bit, 8192 slots) | $O(WH)$/frame |
| Threading | SPSC lock-free ring (decode ∥ process) | — |

## Build

```
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Requires: FFmpeg (`libavformat`, `libavcodec`, `libswscale`, `libavutil`), zlib.

Targets: `mp4togif` (CLI), `mp4togif_gui` (Win32 GUI, batch, ZIP export).

## Usage

```
mp4togif input.mp4 [-o out.gif] [-f 10] [-r 0.5] [-d fs|bayer|none] [-q wu|median-cut]
```
