#pragma once
#include "../cli/args.h"
#include "../ffmpeg/demuxer.h"
#include "../ffmpeg/decoder.h"
#include "../ffmpeg/scaler.h"
#include "../quant/histogram.h"
#include "../quant/palette.h"
#include "../quant/wu_quant.h"
#include "../quant/median_cut.h"
#include "../dither/dither.h"
#include "../gif/gif_enc.h"
#include "spsc_ring.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <thread>
#include <vector>

struct BenchTimer {
    using C = std::chrono::steady_clock;
    C::time_point t0;
    double* out;
    BenchTimer(double* o) : t0(C::now()), out(o) {}
    ~BenchTimer() { *out += std::chrono::duration<double,std::milli>(C::now()-t0).count(); }
};

using ProgressFn = std::function<void(int done, int total)>;

struct IdxFrame {
    std::vector<uint8_t> idx;
    int64_t pts;
    bool eof = false;
};

inline int convert(const Cfg& cfg, ProgressFn on_progress = nullptr) {
    Demuxer dmx(cfg.input);
    Decoder dec(dmx.codec, dmx.par());

    int sw = dmx.par()->width, sh = dmx.par()->height;
    auto [dw, dh] = Scaler::factor(sw, sh, cfg.resize);

    Scaler scl;
    scl.dst_w = dw;
    scl.dst_h = dh;

    AVRational src_tb = dmx.time_base();
    int64_t pts_step = (int64_t)(1.0 / cfg.fps / av_q2d(src_tb));
    if (pts_step <= 0) pts_step = 1;

    int total = dmx.nb_frames();
    bool use_threads = total >= 30;

    double t_dec=0, t_hist=0, t_palgen=0, t_precomp=0, t_dith=0, t_gif=0;
    auto t_total_start = std::chrono::steady_clock::now();

    // --- Single decode: histogram + cache RGB frames ---
    Hist3D hist{};
    std::vector<FrameData> frames;
    frames.reserve(total > 0 ? total / 3 : 128);
    int64_t total_px = 0;

    if (use_threads) {
        SPSCRing<FrameData, 4> ring;
        std::exception_ptr prod_err;

        std::thread producer([&] {
            try {
                Scaler tscl;
                tscl.dst_w = dw;
                tscl.dst_h = dh;
                int64_t last_pts = AV_NOPTS_VALUE;

                auto on_frame = [&](AVFrame* f) {
                    if (last_pts != AV_NOPTS_VALUE && f->pts - last_pts < pts_step) return;
                    last_pts = f->pts;

                    BenchTimer bt(&t_dec);
                    AVFrame* rgb = tscl.scale(f, AV_PIX_FMT_RGB24);
                    int w = rgb->width, h = rgb->height;

                    FrameData fd;
                    fd.w = w; fd.h = h; fd.pts = f->pts;
                    fd.rgb.resize(w * h * 3);
                    for (int y = 0; y < h; ++y)
                        memcpy(fd.rgb.data() + y * w * 3,
                               rgb->data[0] + y * rgb->linesize[0], w * 3);

                    while (!ring.try_push(std::move(fd)))
                        ;
                };

                AVPacket* pkt;
                while (dmx.read(pkt))
                    dec.decode(pkt, on_frame);
                dec.flush(on_frame);

                FrameData eof_fd;
                eof_fd.eof = true;
                while (!ring.try_push(std::move(eof_fd)))
                    ;
            } catch (...) {
                prod_err = std::current_exception();
                FrameData eof; eof.eof = true;
                while (!ring.try_push(std::move(eof)))
                    ;
            }
        });

        FrameData fd;
        for (;;) {
            while (!ring.try_pop(fd))
                ;
            if (fd.eof) break;

            { BenchTimer bt(&t_hist);
              for (int y = 0; y < fd.h; ++y) {
                  const uint8_t* row = fd.rgb.data() + y * fd.w * 3;
                  for (int x = 0; x < fd.w; ++x) {
                      int off = x * 3;
                      hist.add(row[off], row[off+1], row[off+2]);
                  }
              }
            }
            total_px += (int64_t)fd.w * fd.h;
            frames.push_back(std::move(fd));
            if (on_progress) on_progress((int)frames.size(), total);
        }

        producer.join();
        if (prod_err) std::rethrow_exception(prod_err);
    } else {
        int64_t last_pts1 = AV_NOPTS_VALUE;

        auto pass1 = [&](AVFrame* f) {
            if (last_pts1 != AV_NOPTS_VALUE && f->pts - last_pts1 < pts_step) return;
            last_pts1 = f->pts;

            AVFrame* rgb;
            { BenchTimer _(&t_dec); rgb = scl.scale(f, AV_PIX_FMT_RGB24); }
            int w = rgb->width, h = rgb->height;

            { BenchTimer _(&t_hist);
            for (int y = 0; y < h; ++y) {
                const uint8_t* row = rgb->data[0] + y * rgb->linesize[0];
                for (int x = 0; x < w; ++x) {
                    int off = x * 3;
                    hist.add(row[off], row[off + 1], row[off + 2]);
                }
            }
            }

            FrameData fd;
            fd.w = w; fd.h = h; fd.pts = f->pts;
            fd.rgb.resize(w * h * 3);
            for (int y = 0; y < h; ++y)
                memcpy(fd.rgb.data() + y * w * 3,
                       rgb->data[0] + y * rgb->linesize[0], w * 3);

            total_px += (int64_t)w * h;
            frames.push_back(std::move(fd));
            if (on_progress) on_progress((int)frames.size(), total);
        };

        AVPacket* pkt;
        while (dmx.read(pkt))
            dec.decode(pkt, pass1);
        dec.flush(pass1);
    }

    int nf = (int)frames.size();
    fprintf(stderr, "\rdecode: %d frames, %lld pixels cached\n",
            nf, (long long)total_px);

    Palette pal;
    { BenchTimer _(&t_palgen);
    if (cfg.quant == QuantMode::Wu) {
        pal = wu_quantize(hist, 256);
        fprintf(stderr, "wu palette:  %d colors\n", pal.n);
    } else {
        pal = median_cut(hist, 256);
        fprintf(stderr, "median-cut palette:  %d colors\n", pal.n);
    }
    }

    { BenchTimer _(&t_precomp); precompute_nearest_cache(pal); }

    // --- Encode: dither (main thread) ∥ gifenc (worker thread) via SPSC ---
    GifEnc gif(cfg.output, dw, dh, pal, dmx.time_base());

    if (use_threads) {
        SPSCRing<IdxFrame, 4> gif_ring;
        std::exception_ptr enc_err;

        std::thread encoder([&] {
            try {
                int64_t prev_pts = AV_NOPTS_VALUE;
                int64_t last_dur_us = 0;
                std::vector<uint8_t> prev_idx;

                IdxFrame ifr;
                for (;;) {
                    while (!gif_ring.try_pop(ifr))
                        ;
                    if (ifr.eof) break;

                    if (prev_pts != AV_NOPTS_VALUE) {
                        BenchTimer bt(&t_gif);
                        gif.frame(prev_idx.data(), prev_pts, ifr.pts);
                        last_dur_us = av_rescale_q(ifr.pts - prev_pts,
                                                   dmx.time_base(), {1, 1000000});
                    }

                    prev_idx = std::move(ifr.idx);
                    prev_pts = ifr.pts;
                }

                if (prev_pts != AV_NOPTS_VALUE && !prev_idx.empty()) {
                    if (last_dur_us <= 0)
                        last_dur_us = av_rescale_q(1, {1, 30}, {1, 1000000});
                    gif.frame_last(prev_idx.data(), last_dur_us);
                }
            } catch (...) {
                enc_err = std::current_exception();
            }
        });

        std::vector<uint8_t> idx_buf(dw * dh);
        std::vector<int16_t> fs_buf;

        for (int i = 0; i < nf; ++i) {
            auto& fd = frames[i];

            { BenchTimer bt(&t_dith);
              dither(fd.rgb.data(), idx_buf.data(), fd.w, fd.h, pal, cfg.dither, fs_buf);
            }
            std::vector<uint8_t>().swap(fd.rgb);

            IdxFrame ifr;
            ifr.idx.resize(idx_buf.size());
            memcpy(ifr.idx.data(), idx_buf.data(), idx_buf.size());
            ifr.pts = fd.pts;

            while (!gif_ring.try_push(std::move(ifr)))
                ;
            if (on_progress) on_progress(i + 1, nf);
        }

        IdxFrame eof_ifr;
        eof_ifr.eof = true;
        while (!gif_ring.try_push(std::move(eof_ifr)))
            ;

        encoder.join();
        if (enc_err) std::rethrow_exception(enc_err);
    } else {
        std::vector<uint8_t> idx_buf(dw * dh);
        std::vector<int16_t> fs_buf;
        std::vector<uint8_t> prev_idx;
        int64_t prev_pts = AV_NOPTS_VALUE;
        int64_t last_dur_us = 0;

        for (int i = 0; i < nf; ++i) {
            auto& fd = frames[i];

            { BenchTimer bt(&t_dith);
              dither(fd.rgb.data(), idx_buf.data(), fd.w, fd.h, pal, cfg.dither, fs_buf);
            }
            std::vector<uint8_t>().swap(fd.rgb);

            if (prev_pts != AV_NOPTS_VALUE) {
                BenchTimer bt(&t_gif);
                gif.frame(prev_idx.data(), prev_pts, fd.pts);
                last_dur_us = av_rescale_q(fd.pts - prev_pts,
                                           dmx.time_base(), {1, 1000000});
            }

            if (prev_idx.size() != idx_buf.size()) prev_idx.resize(idx_buf.size());
            std::swap(prev_idx, idx_buf);
            prev_pts = fd.pts;
            if (on_progress) on_progress(i + 1, nf);
        }

        if (prev_pts != AV_NOPTS_VALUE && !prev_idx.empty()) {
            if (last_dur_us <= 0)
                last_dur_us = av_rescale_q(1, {1, 30}, {1, 1000000});
            gif.frame_last(prev_idx.data(), last_dur_us);
        }
    }

    fprintf(stderr, "\rencode: %d frames\n", nf);

    double t_total = std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now() - t_total_start).count();
    fprintf(stderr, "[bench] decode+scale:  %.1f ms\n", t_dec);
    fprintf(stderr, "[bench] histogram:     %.1f ms\n", t_hist);
    fprintf(stderr, "[bench] palette-gen:   %.1f ms\n", t_palgen);
    fprintf(stderr, "[bench] cache-precomp: %.1f ms\n", t_precomp);
    fprintf(stderr, "[bench] dither:        %.1f ms\n", t_dith);
    fprintf(stderr, "[bench] gifenc:        %.1f ms\n", t_gif);
    fprintf(stderr, "[bench] total:         %.1f ms\n", t_total);

    return nf > 0 ? 0 : 1;
}
