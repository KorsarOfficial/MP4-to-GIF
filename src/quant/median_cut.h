#pragma once
#include "histogram.h"
#include "palette.h"
#include <algorithm>
#include <queue>

inline Palette median_cut(const Hist3D& h, int max_colors = 256) {
    struct MCBox {
        int rmin, rmax, gmin, gmax, bmin, bmax;
        int64_t count;

        int longest_axis() const {
            int dr = rmax - rmin, dg = gmax - gmin, db = bmax - bmin;
            if (dr >= dg && dr >= db) return 0;
            return dg >= db ? 1 : 2;
        }

        bool operator<(const MCBox& o) const { return count < o.count; }
    };

    MCBox init{1, 32, 1, 32, 1, 32, 0};
    for (int r = 1; r <= 32; ++r)
        for (int g = 1; g <= 32; ++g)
            for (int b = 1; b <= 32; ++b)
                init.count += h.wt[r * 33 * 33 + g * 33 + b];

    if (init.count == 0) { Palette p{}; p.n = 0; return p; }

    auto shrink = [&](MCBox& bx) {
        for (int r = bx.rmin; r <= bx.rmax; ++r) {
            bool any = false;
            for (int g = bx.gmin; g <= bx.gmax && !any; ++g)
                for (int b = bx.bmin; b <= bx.bmax && !any; ++b)
                    if (h.wt[r * 33 * 33 + g * 33 + b]) any = true;
            if (any) { bx.rmin = r; break; }
        }
        for (int r = bx.rmax; r >= bx.rmin; --r) {
            bool any = false;
            for (int g = bx.gmin; g <= bx.gmax && !any; ++g)
                for (int b = bx.bmin; b <= bx.bmax && !any; ++b)
                    if (h.wt[r * 33 * 33 + g * 33 + b]) any = true;
            if (any) { bx.rmax = r; break; }
        }
        for (int g = bx.gmin; g <= bx.gmax; ++g) {
            bool any = false;
            for (int r = bx.rmin; r <= bx.rmax && !any; ++r)
                for (int b = bx.bmin; b <= bx.bmax && !any; ++b)
                    if (h.wt[r * 33 * 33 + g * 33 + b]) any = true;
            if (any) { bx.gmin = g; break; }
        }
        for (int g = bx.gmax; g >= bx.gmin; --g) {
            bool any = false;
            for (int r = bx.rmin; r <= bx.rmax && !any; ++r)
                for (int b = bx.bmin; b <= bx.bmax && !any; ++b)
                    if (h.wt[r * 33 * 33 + g * 33 + b]) any = true;
            if (any) { bx.gmax = g; break; }
        }
        for (int b = bx.bmin; b <= bx.bmax; ++b) {
            bool any = false;
            for (int r = bx.rmin; r <= bx.rmax && !any; ++r)
                for (int g = bx.gmin; g <= bx.gmax && !any; ++g)
                    if (h.wt[r * 33 * 33 + g * 33 + b]) any = true;
            if (any) { bx.bmin = b; break; }
        }
        for (int b = bx.bmax; b >= bx.bmin; --b) {
            bool any = false;
            for (int r = bx.rmin; r <= bx.rmax && !any; ++r)
                for (int g = bx.gmin; g <= bx.gmax && !any; ++g)
                    if (h.wt[r * 33 * 33 + g * 33 + b]) any = true;
            if (any) { bx.bmax = b; break; }
        }
    };

    shrink(init);

    std::priority_queue<MCBox> pq;
    pq.push(init);

    while ((int)pq.size() < max_colors) {
        MCBox bx = pq.top(); pq.pop();

        int axis = bx.longest_axis();
        int amin, amax;
        if      (axis == 0) { amin = bx.rmin; amax = bx.rmax; }
        else if (axis == 1) { amin = bx.gmin; amax = bx.gmax; }
        else                { amin = bx.bmin; amax = bx.bmax; }

        if (amin == amax) {
            pq.push(bx);
            break;
        }

        int64_t accum[33]{};
        for (int a = amin; a <= amax; ++a) {
            int64_t s = 0;
            if (axis == 0) {
                for (int g = bx.gmin; g <= bx.gmax; ++g)
                    for (int b = bx.bmin; b <= bx.bmax; ++b)
                        s += h.wt[a * 33 * 33 + g * 33 + b];
            } else if (axis == 1) {
                for (int r = bx.rmin; r <= bx.rmax; ++r)
                    for (int b = bx.bmin; b <= bx.bmax; ++b)
                        s += h.wt[r * 33 * 33 + a * 33 + b];
            } else {
                for (int r = bx.rmin; r <= bx.rmax; ++r)
                    for (int g = bx.gmin; g <= bx.gmax; ++g)
                        s += h.wt[r * 33 * 33 + g * 33 + a];
            }
            accum[a] = s;
        }

        int64_t half = bx.count / 2, running = 0;
        int split = amin;
        for (int a = amin; a < amax; ++a) {
            running += accum[a];
            if (running >= half) { split = a; break; }
        }
        if (split == amax) split = amax - 1;

        MCBox lo = bx, hi = bx;
        if      (axis == 0) { lo.rmax = split; hi.rmin = split + 1; }
        else if (axis == 1) { lo.gmax = split; hi.gmin = split + 1; }
        else                { lo.bmax = split; hi.bmin = split + 1; }

        lo.count = 0;
        for (int r = lo.rmin; r <= lo.rmax; ++r)
            for (int g = lo.gmin; g <= lo.gmax; ++g)
                for (int b = lo.bmin; b <= lo.bmax; ++b)
                    lo.count += h.wt[r * 33 * 33 + g * 33 + b];
        hi.count = bx.count - lo.count;

        if (lo.count > 0) pq.push(lo);
        if (hi.count > 0) pq.push(hi);
    }

    Palette pal{};
    pal.n = 0;
    while (!pq.empty() && pal.n < 256) {
        MCBox bx = pq.top(); pq.pop();
        int64_t tw = 0, tr = 0, tg = 0, tb = 0;
        for (int r = bx.rmin; r <= bx.rmax; ++r)
            for (int g = bx.gmin; g <= bx.gmax; ++g)
                for (int b = bx.bmin; b <= bx.bmax; ++b) {
                    int i = r * 33 * 33 + g * 33 + b;
                    int32_t w = h.wt[i];
                    tw += w;
                    tr += h.mr[i];
                    tg += h.mg[i];
                    tb += h.mb[i];
                }
        if (tw > 0) {
            pal.rgb[pal.n][0] = (uint8_t)std::clamp<int64_t>(tr / tw, 0, 255);
            pal.rgb[pal.n][1] = (uint8_t)std::clamp<int64_t>(tg / tw, 0, 255);
            pal.rgb[pal.n][2] = (uint8_t)std::clamp<int64_t>(tb / tw, 0, 255);
            ++pal.n;
        }
    }
    return pal;
}
