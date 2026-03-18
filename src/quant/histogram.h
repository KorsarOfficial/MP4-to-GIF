#pragma once
#include <cstdint>
#include <cstring>

struct Hist3D {
    static constexpr int N = 33 * 33 * 33;

    int32_t wt[N], mr[N], mg[N], mb[N];
    float   m2[N];

    Hist3D() { clear(); }

    void clear() {
        memset(wt, 0, sizeof wt);
        memset(mr, 0, sizeof mr);
        memset(mg, 0, sizeof mg);
        memset(mb, 0, sizeof mb);
        memset(m2, 0, sizeof m2);
    }

    void add(uint8_t r, uint8_t g, uint8_t b) {
        int i = ((r >> 3) + 1) * 33 * 33
              + ((g >> 3) + 1) * 33
              + ((b >> 3) + 1);
        ++wt[i];
        mr[i] += r;
        mg[i] += g;
        mb[i] += b;
        m2[i] += (float)(r * r + g * g + b * b);
    }

    void compute_moments() {
        for (int r = 1; r <= 32; ++r) {
            int32_t area_w[33]{}, area_r[33]{}, area_g[33]{}, area_b[33]{};
            float   area_2[33]{};
            for (int g = 1; g <= 32; ++g) {
                int32_t line_w = 0, line_r = 0, line_g = 0, line_b = 0;
                float   line_2 = 0;
                for (int b = 1; b <= 32; ++b) {
                    int i = r * 33 * 33 + g * 33 + b;
                    line_w += wt[i]; line_r += mr[i]; line_g += mg[i]; line_b += mb[i];
                    line_2 += m2[i];
                    area_w[b] += line_w; area_r[b] += line_r;
                    area_g[b] += line_g; area_b[b] += line_b;
                    area_2[b] += line_2;
                    int p = (r - 1) * 33 * 33 + g * 33 + b;
                    wt[i] = wt[p] + area_w[b];
                    mr[i] = mr[p] + area_r[b];
                    mg[i] = mg[p] + area_g[b];
                    mb[i] = mb[p] + area_b[b];
                    m2[i] = m2[p] + area_2[b];
                }
            }
        }
    }
};
