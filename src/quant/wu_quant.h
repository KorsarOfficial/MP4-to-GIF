#pragma once
#include "histogram.h"
#include "palette.h"
#include <algorithm>
#include <cmath>

namespace wu {

struct Box {
    int r0, r1, g0, g1, b0, b1;
};

inline int32_t vol(const Box& b, const int32_t* m) {
    return m[b.r1*33*33 + b.g1*33 + b.b1]
         - m[b.r1*33*33 + b.g1*33 + b.b0]
         - m[b.r1*33*33 + b.g0*33 + b.b1]
         + m[b.r1*33*33 + b.g0*33 + b.b0]
         - m[b.r0*33*33 + b.g1*33 + b.b1]
         + m[b.r0*33*33 + b.g1*33 + b.b0]
         + m[b.r0*33*33 + b.g0*33 + b.b1]
         - m[b.r0*33*33 + b.g0*33 + b.b0];
}

inline float vol_f(const Box& b, const float* m) {
    return m[b.r1*33*33 + b.g1*33 + b.b1]
         - m[b.r1*33*33 + b.g1*33 + b.b0]
         - m[b.r1*33*33 + b.g0*33 + b.b1]
         + m[b.r1*33*33 + b.g0*33 + b.b0]
         - m[b.r0*33*33 + b.g1*33 + b.b1]
         + m[b.r0*33*33 + b.g1*33 + b.b0]
         + m[b.r0*33*33 + b.g0*33 + b.b1]
         - m[b.r0*33*33 + b.g0*33 + b.b0];
}

inline float variance(const Box& b, const Hist3D& h) {
    int32_t w = vol(b, h.wt);
    if (w <= 0) return 0.0f;
    int32_t dr = vol(b, h.mr);
    int32_t dg = vol(b, h.mg);
    int32_t db = vol(b, h.mb);
    float   d2 = vol_f(b, h.m2);
    return d2 - ((float)dr * dr + (float)dg * dg + (float)db * db) / w;
}

inline int32_t bottom(const Box& b, int axis, int split, const int32_t* m) {
    Box h = b;
    if      (axis == 0) h.r1 = split;
    else if (axis == 1) h.g1 = split;
    else                h.b1 = split;
    return vol(h, m);
}

inline float bottom_f(const Box& b, int axis, int split, const float* m) {
    Box h = b;
    if      (axis == 0) h.r1 = split;
    else if (axis == 1) h.g1 = split;
    else                h.b1 = split;
    return vol_f(h, m);
}

inline float maximize(const Box& b, const Hist3D& h, int axis,
                      int& cut_pos, bool& found) {
    int lo, hi;
    if      (axis == 0) { lo = b.r0 + 1; hi = b.r1; }
    else if (axis == 1) { lo = b.g0 + 1; hi = b.g1; }
    else                { lo = b.b0 + 1; hi = b.b1; }

    int32_t whole_w  = vol(b, h.wt);
    int32_t whole_mr = vol(b, h.mr);
    int32_t whole_mg = vol(b, h.mg);
    int32_t whole_mb = vol(b, h.mb);
    float   whole_m2 = vol_f(b, h.m2);

    found = false;
    float best = 0.0f;
    cut_pos = lo;

    for (int i = lo; i < hi; ++i) {
        int32_t hw = bottom(b, axis, i, h.wt);
        if (hw <= 0) continue;
        int32_t tw = whole_w - hw;
        if (tw <= 0) continue;

        int32_t hr = bottom(b, axis, i, h.mr);
        int32_t hg = bottom(b, axis, i, h.mg);
        int32_t hb = bottom(b, axis, i, h.mb);

        float val = ((float)hr * hr + (float)hg * hg + (float)hb * hb) / hw;

        int32_t tr = whole_mr - hr;
        int32_t tg = whole_mg - hg;
        int32_t tb = whole_mb - hb;
        val += ((float)tr * tr + (float)tg * tg + (float)tb * tb) / tw;

        if (val > best) {
            best = val;
            cut_pos = i;
            found = true;
        }
    }
    return best;
}

} // namespace wu

inline Palette wu_quantize(Hist3D& h, int max_colors = 256) {
    h.compute_moments();

    wu::Box boxes[256];
    float   vv[256];   // variance of each box
    int     nb = 1;

    boxes[0] = {0, 32, 0, 32, 0, 32};
    vv[0] = wu::variance(boxes[0], h);

    for (int k = 1; k < max_colors; ++k) {
        int best_idx = -1;
        float best_var = 0.0f;
        for (int i = 0; i < nb; ++i) {
            if (vv[i] > best_var) {
                best_var = vv[i];
                best_idx = i;
            }
        }
        if (best_idx < 0 || best_var <= 0.0f) break;

        wu::Box& cur = boxes[best_idx];

        int    cut_r, cut_g, cut_b;
        bool   fr, fg, fb;
        float  vr = wu::maximize(cur, h, 0, cut_r, fr);
        float  vg = wu::maximize(cur, h, 1, cut_g, fg);
        float  vb = wu::maximize(cur, h, 2, cut_b, fb);

        if (!fr && !fg && !fb) {
            vv[best_idx] = 0.0f;  // mark as unsplittable
            --k;  // don't count this iteration
            continue;
        }

        int best_axis, best_cut;
        if (vr >= vg && vr >= vb && fr)      { best_axis = 0; best_cut = cut_r; }
        else if (vg >= vr && vg >= vb && fg)  { best_axis = 1; best_cut = cut_g; }
        else                                  { best_axis = 2; best_cut = cut_b; }

        boxes[nb] = cur;
        if      (best_axis == 0) { boxes[nb].r0 = best_cut; cur.r1 = best_cut; }
        else if (best_axis == 1) { boxes[nb].g0 = best_cut; cur.g1 = best_cut; }
        else                     { boxes[nb].b0 = best_cut; cur.b1 = best_cut; }

        vv[best_idx] = wu::variance(cur, h);
        vv[nb]       = wu::variance(boxes[nb], h);
        ++nb;
    }

    Palette pal{};
    pal.n = 0;
    for (int i = 0; i < nb && pal.n < 256; ++i) {
        int32_t w = wu::vol(boxes[i], h.wt);
        if (w <= 0) continue;
        int32_t sr = wu::vol(boxes[i], h.mr);
        int32_t sg = wu::vol(boxes[i], h.mg);
        int32_t sb = wu::vol(boxes[i], h.mb);
        pal.rgb[pal.n][0] = (uint8_t)std::clamp(sr / w, (int32_t)0, (int32_t)255);
        pal.rgb[pal.n][1] = (uint8_t)std::clamp(sg / w, (int32_t)0, (int32_t)255);
        pal.rgb[pal.n][2] = (uint8_t)std::clamp(sb / w, (int32_t)0, (int32_t)255);
        ++pal.n;
    }
    return pal;
}
