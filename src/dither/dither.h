#pragma once
#include "../quant/palette.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

enum class DitherMode { None, FloydSteinberg, Bayer };

static constexpr uint8_t bayer8[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21}
};

inline void dither_fs(const uint8_t* rgb, uint8_t* out,
                      int w, int h, const Palette& p,
                      std::vector<int16_t>& buf) {
    const int stride = (w + 2) * 3;
    buf.resize(2 * stride);
    int16_t* rows[2] = { buf.data(), buf.data() + stride };
    int cur = 0;

    memset(rows[0], 0, stride * sizeof(int16_t));
    memset(rows[1], 0, stride * sizeof(int16_t));

    for (int y = 0; y < h; ++y) {
        const int nxt = cur ^ 1;
        memset(rows[nxt], 0, stride * sizeof(int16_t));

        const bool rev = (y & 1);
        const int x0 = rev ? w - 1 : 0;
        const int x1 = rev ? -1 : w;
        const int dx = rev ? -1 : 1;

        int16_t* __restrict__ cr = rows[cur];
        int16_t* __restrict__ nr = rows[nxt];
        const uint8_t* __restrict__ row_rgb = rgb + y * w * 3;

        for (int x = x0; x != x1; x += dx) {
            const int i = (x + 1) * 3;
            const uint8_t r = (uint8_t)std::clamp(row_rgb[x*3]   + (int)cr[i],   0, 255);
            const uint8_t g = (uint8_t)std::clamp(row_rgb[x*3+1] + (int)cr[i+1], 0, 255);
            const uint8_t b = (uint8_t)std::clamp(row_rgb[x*3+2] + (int)cr[i+2], 0, 255);

            const uint8_t ci = detail::inv_cache[r >> 3][g >> 3][b >> 3];
            out[y * w + x] = ci;

            const int er = r - p.rgb[ci][0];
            const int eg = g - p.rgb[ci][1];
            const int eb = b - p.rgb[ci][2];

            // FS kernel: 7/16 fwd, 3/16 below-back, 5/16 below, 1/16 below-fwd
            const int fi = i + dx * 3;
            cr[fi]   += (int16_t)(er * 7 / 16);
            cr[fi+1] += (int16_t)(eg * 7 / 16);
            cr[fi+2] += (int16_t)(eb * 7 / 16);

            const int ni = i - dx * 3;
            nr[ni]   += (int16_t)(er * 3 / 16);
            nr[ni+1] += (int16_t)(eg * 3 / 16);
            nr[ni+2] += (int16_t)(eb * 3 / 16);

            nr[i]   += (int16_t)(er * 5 / 16);
            nr[i+1] += (int16_t)(eg * 5 / 16);
            nr[i+2] += (int16_t)(eb * 5 / 16);

            const int bi = i + dx * 3;
            nr[bi]   += (int16_t)(er * 1 / 16);
            nr[bi+1] += (int16_t)(eg * 1 / 16);
            nr[bi+2] += (int16_t)(eb * 1 / 16);
        }

        cur = nxt;
    }
}

inline void dither_bayer(const uint8_t* rgb, uint8_t* out,
                         int w, int h, const Palette& p, int spread = 48) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int i = (y * w + x) * 3;
            int t = ((int)bayer8[y & 7][x & 7] - 32) * spread / 64;

            uint8_t r = (uint8_t)std::clamp(rgb[i]   + t, 0, 255);
            uint8_t g = (uint8_t)std::clamp(rgb[i+1] + t, 0, 255);
            uint8_t b = (uint8_t)std::clamp(rgb[i+2] + t, 0, 255);

            out[y * w + x] = nearest(p, r, g, b);
        }
    }
}

inline void dither(const uint8_t* rgb, uint8_t* out,
                   int w, int h, const Palette& p, DitherMode mode,
                   std::vector<int16_t>& fs_buf) {
    switch (mode) {
    case DitherMode::None:
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int i = (y * w + x) * 3;
                out[y * w + x] = nearest(p, rgb[i], rgb[i+1], rgb[i+2]);
            }
        break;
    case DitherMode::FloydSteinberg:
        dither_fs(rgb, out, w, h, p, fs_buf);
        break;
    case DitherMode::Bayer:
        dither_bayer(rgb, out, w, h, p);
        break;
    }
}
