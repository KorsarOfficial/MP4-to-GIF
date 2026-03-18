#pragma once
#include "../quant/palette.h"
#include "../ffmpeg/ff_types.h"
#include "lzw.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

struct GifEnc {
    FILE*   fp  = nullptr;
    int w, h;
    int64_t acc_us  = 0;
    int64_t emit_us = 0;
    AVRational tb;
    std::unique_ptr<LzwWriter> lzw;

    GifEnc(const char* path, int w, int h, const Palette& pal, AVRational tb)
        : w(w), h(h), tb(tb), lzw(std::make_unique<LzwWriter>())
    {
        fp = fopen(path, "wb");
        if (!fp) throw PipelineError("fopen failed for GIF output");

        setvbuf(fp, nullptr, _IOFBF, 65536);

        fwrite("GIF89a", 1, 6, fp);

        write16(w);
        write16(h);
        uint8_t packed = 0x80 | 0x70 | 0x07; // GCT=256, cr=8
        fwrite(&packed, 1, 1, fp);
        uint8_t bg = 0;
        fwrite(&bg, 1, 1, fp);
        uint8_t ar = 0;
        fwrite(&ar, 1, 1, fp);

        uint8_t gct[768];
        memset(gct, 0, sizeof gct);
        for (int i = 0; i < pal.n; ++i) {
            gct[i * 3 + 0] = pal.rgb[i][0];
            gct[i * 3 + 1] = pal.rgb[i][1];
            gct[i * 3 + 2] = pal.rgb[i][2];
        }
        fwrite(gct, 1, 768, fp);

        static const uint8_t netscape[] = {
            0x21, 0xFF, 0x0B,
            'N','E','T','S','C','A','P','E','2','.','0',
            0x03, 0x01, 0x00, 0x00, 0x00
        };
        fwrite(netscape, 1, sizeof netscape, fp);
    }

    void frame(const uint8_t* idx, int64_t pts, int64_t next_pts) {
        int64_t dur_us = av_rescale_q(next_pts - pts, tb, {1, 1000000});
        acc_us += dur_us;
        int64_t total_cs = (acc_us + 5000) / 10000;
        int64_t ecur_cs  = (emit_us + 5000) / 10000;
        int cs = (int)(total_cs - ecur_cs);
        if (cs < 2) cs = 2;
        emit_us += cs * 10000LL;

        write_frame(idx, cs);
    }

    void frame_last(const uint8_t* idx, int64_t dur_us) {
        acc_us += dur_us;
        int64_t total_cs = (acc_us + 5000) / 10000;
        int64_t ecur_cs  = (emit_us + 5000) / 10000;
        int cs = (int)(total_cs - ecur_cs);
        if (cs < 2) cs = 2;
        emit_us += cs * 10000LL;

        write_frame(idx, cs);
    }

    ~GifEnc() {
        if (fp) {
            uint8_t trailer = 0x3B;
            fwrite(&trailer, 1, 1, fp);
            fclose(fp);
        }
    }

    GifEnc(const GifEnc&) = delete;
    GifEnc& operator=(const GifEnc&) = delete;

    GifEnc(GifEnc&& o) noexcept
        : fp(o.fp), w(o.w), h(o.h),
          acc_us(o.acc_us), emit_us(o.emit_us), tb(o.tb),
          lzw(std::move(o.lzw)) {
        o.fp = nullptr;
    }
    GifEnc& operator=(GifEnc&& o) noexcept {
        if (this != &o) {
            if (fp) {
                uint8_t trailer = 0x3B;
                fwrite(&trailer, 1, 1, fp);
                fclose(fp);
            }
            fp = o.fp; w = o.w; h = o.h;
            acc_us = o.acc_us; emit_us = o.emit_us; tb = o.tb;
            lzw = std::move(o.lzw);
            o.fp = nullptr;
        }
        return *this;
    }

private:
    void write16(int v) {
        uint8_t buf[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF) };
        fwrite(buf, 1, 2, fp);
    }

    void write_frame(const uint8_t* idx, int cs) {
        uint8_t gce[8] = {
            0x21, 0xF9, 0x04, 0x04,
            (uint8_t)(cs & 0xFF), (uint8_t)((cs >> 8) & 0xFF),
            0x00, 0x00
        };
        fwrite(gce, 1, 8, fp);

        uint8_t imd[10] = {
            0x2C, 0x00, 0x00, 0x00, 0x00,
            (uint8_t)(w & 0xFF), (uint8_t)((w >> 8) & 0xFF),
            (uint8_t)(h & 0xFF), (uint8_t)((h >> 8) & 0xFF),
            0x00
        };
        fwrite(imd, 1, 10, fp);

        lzw->begin_frame(fp, 8);
        for (int y = 0; y < h; ++y)
            lzw->compress(idx + y * w, w);
        lzw->finish();
    }
};
