#pragma once
#include <cstdint>
#include <climits>
#include <cstring>
#include <immintrin.h>

struct Palette {
    uint8_t rgb[256][3];
    int n;
};

namespace detail {
    inline thread_local uint8_t inv_cache[32][32][32];
    inline thread_local bool    inv_cache_valid = false;
    inline thread_local int16_t pal_r[256] __attribute__((aligned(32)));
    inline thread_local int16_t pal_g[256] __attribute__((aligned(32)));
    inline thread_local int16_t pal_b[256] __attribute__((aligned(32)));
}

inline void nearest_reset_cache() {
    memset(detail::inv_cache, 0xFF, sizeof detail::inv_cache);
    detail::inv_cache_valid = true;
}

inline uint8_t nearest(const Palette& p, uint8_t r, uint8_t g, uint8_t b) {
    if (!detail::inv_cache_valid) nearest_reset_cache();

    uint8_t& c = detail::inv_cache[r >> 3][g >> 3][b >> 3];
    if (c != 0xFF) return c;

    int best = INT_MAX, bi = 0;
    for (int i = 0; i < p.n; ++i) {
        int dr = r - p.rgb[i][0];
        int dg = g - p.rgb[i][1];
        int db = b - p.rgb[i][2];
        int d = dr * dr + dg * dg + db * db;
        if (d < best) { best = d; bi = i; }
    }
    return c = (uint8_t)bi;
}

inline void precompute_nearest_cache_scalar(const Palette& p) {
    nearest_reset_cache();
    for (int ri = 0; ri < 32; ++ri) {
        uint8_t qr = (uint8_t)((ri << 3) | 4);
        for (int gi = 0; gi < 32; ++gi) {
            uint8_t qg = (uint8_t)((gi << 3) | 4);
            for (int bi = 0; bi < 32; ++bi) {
                uint8_t qb = (uint8_t)((bi << 3) | 4);
                int best = INT_MAX, best_i = 0;
                for (int i = 0; i < p.n; ++i) {
                    int dr = qr - p.rgb[i][0];
                    int dg = qg - p.rgb[i][1];
                    int db = qb - p.rgb[i][2];
                    int d = dr*dr + dg*dg + db*db;
                    if (d < best) { best = d; best_i = i; }
                }
                detail::inv_cache[ri][gi][bi] = (uint8_t)best_i;
            }
        }
    }
}

#ifdef __AVX2__
__attribute__((target("avx2")))
inline void precompute_nearest_cache_avx2(const Palette& p) {
    for (int i = 0; i < p.n; ++i) {
        detail::pal_r[i] = p.rgb[i][0];
        detail::pal_g[i] = p.rgb[i][1];
        detail::pal_b[i] = p.rgb[i][2];
    }
    int padded = (p.n + 15) & ~15;
    for (int i = p.n; i < padded; ++i) {
        detail::pal_r[i] = 0;
        detail::pal_g[i] = 0;
        detail::pal_b[i] = 0;
    }

    nearest_reset_cache();

    static constexpr int map_lo[] = {0,1,2,3, 8,9,10,11};
    static constexpr int map_hi[] = {4,5,6,7, 12,13,14,15};

    for (int ri = 0; ri < 32; ++ri) {
        int16_t qr = (int16_t)((ri << 3) | 4);
        for (int gi = 0; gi < 32; ++gi) {
            int16_t qg = (int16_t)((gi << 3) | 4);
            for (int bi = 0; bi < 32; ++bi) {
                int16_t qb = (int16_t)((bi << 3) | 4);

                __m256i vr = _mm256_set1_epi16(qr);
                __m256i vg = _mm256_set1_epi16(qg);
                __m256i vb = _mm256_set1_epi16(qb);

                int best = INT_MAX, bi_idx = 0;

                for (int i = 0; i < padded; i += 16) {
                    __m256i pr = _mm256_load_si256((__m256i*)(detail::pal_r + i));
                    __m256i pg = _mm256_load_si256((__m256i*)(detail::pal_g + i));
                    __m256i pb = _mm256_load_si256((__m256i*)(detail::pal_b + i));

                    __m256i dr = _mm256_sub_epi16(vr, pr);
                    __m256i dg = _mm256_sub_epi16(vg, pg);
                    __m256i db = _mm256_sub_epi16(vb, pb);

                    __m256i drg_lo = _mm256_unpacklo_epi16(dr, dg);
                    __m256i dist_rg_lo = _mm256_madd_epi16(drg_lo, drg_lo);
                    __m256i db_lo = _mm256_unpacklo_epi16(db, _mm256_setzero_si256());
                    __m256i db2_lo = _mm256_madd_epi16(db_lo, db_lo);
                    __m256i dist_lo = _mm256_add_epi32(dist_rg_lo, db2_lo);

                    __m256i drg_hi = _mm256_unpackhi_epi16(dr, dg);
                    __m256i dist_rg_hi = _mm256_madd_epi16(drg_hi, drg_hi);
                    __m256i db_hi = _mm256_unpackhi_epi16(db, _mm256_setzero_si256());
                    __m256i db2_hi = _mm256_madd_epi16(db_hi, db_hi);
                    __m256i dist_hi = _mm256_add_epi32(dist_rg_hi, db2_hi);

                    alignas(32) int32_t d_lo[8], d_hi[8];
                    _mm256_store_si256((__m256i*)d_lo, dist_lo);
                    _mm256_store_si256((__m256i*)d_hi, dist_hi);

                    for (int k = 0; k < 8; ++k) {
                        int idx = i + map_lo[k];
                        if (idx < p.n && d_lo[k] < best) { best = d_lo[k]; bi_idx = idx; }
                    }
                    for (int k = 0; k < 8; ++k) {
                        int idx = i + map_hi[k];
                        if (idx < p.n && d_hi[k] < best) { best = d_hi[k]; bi_idx = idx; }
                    }
                }

                detail::inv_cache[ri][gi][bi] = (uint8_t)bi_idx;
            }
        }
    }
}
#endif

inline void precompute_nearest_cache(const Palette& p) {
#ifdef __AVX2__
    if (__builtin_cpu_supports("avx2")) {
        precompute_nearest_cache_avx2(p);
        return;
    }
#endif
    precompute_nearest_cache_scalar(p);
}
