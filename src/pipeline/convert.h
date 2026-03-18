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

    double t_p1dec=0, t_p1hist=0, t_palgen=0, t_precomp=0, t_p2dec=0, t_p2dith=0, t_p2gif=0;
    auto t_total_start = std::chrono::steady_clock::now();

    Hist3D hist{};
    int frames1 = 0;
    int64_t total_px = 0;

    if (use_threads) {
        SPSCRing<FrameData, 4> ring1;
        std::exception_ptr prod_err;

        std::thread t1([&] {
            try {
                Scaler tscl;
                tscl.dst_w = dw;
                tscl.dst_h = dh;
                int64_t last_pts = AV_NOPTS_VALUE;

                auto on_frame = [&](AVFrame* f) {
                    if (last_pts != AV_NOPTS_VALUE && f->pts - last_pts < pts_step) return;
                    last_pts = f->pts;

                    BenchTimer bt(&t_p1dec);
                    AVFrame* rgb = tscl.scale(f, AV_PIX_FMT_RGB24);
                    int w = rgb->width, h = rgb->height;

                    FrameData fd;
                    fd.w = w; fd.h = h; fd.pts = f->pts;
                    fd.rgb.resize(w * h * 3);
                    for (int y = 0; y < h; ++y)
                        memcpy(fd.rgb.data() + y * w * 3,
                               rgb->data[0] + y * rgb->linesize[0], w * 3);

                    while (!ring1.try_push(std::move(fd)))
                        ;
                };

                AVPacket* pkt;
                while (dmx.read(pkt))
                    dec.decode(pkt, on_frame);
                dec.flush(on_frame);

                FrameData eof_fd;
                eof_fd.eof = true;
                while (!ring1.try_push(std::move(eof_fd)))
                    ;
            } catch (...) {
                prod_err = std::current_exception();
                FrameData eof; eof.eof = true;
                while (!ring1.try_push(std::move(eof)))
                    ;
            }
        });

        FrameData fd;
        for (;;) {
            while (!ring1.try_pop(fd))
                ;
            if (fd.eof) break;

            { BenchTimer bt(&t_p1hist);
              for (int y = 0; y < fd.h; ++y) {
                  const uint8_t* row = fd.rgb.data() + y * fd.w * 3;
                  for (int x = 0; x < fd.w; ++x) {
                      int off = x * 3;
                      hist.add(row[off], row[off+1], row[off+2]);
                  }
              }
            }
            total_px += (int64_t)fd.w * fd.h;
            ++frames1;
            if (on_progress) on_progress(frames1, total);
        }

        t1.join();
        if (prod_err) std::rethrow_exception(prod_err);
    } else {
        int64_t last_pts1 = AV_NOPTS_VALUE;

        auto pass1 = [&](AVFrame* f) {
            if (last_pts1 != AV_NOPTS_VALUE && f->pts - last_pts1 < pts_step) return;
            last_pts1 = f->pts;

            AVFrame* rgb;
            { BenchTimer _(&t_p1dec); rgb = scl.scale(f, AV_PIX_FMT_RGB24); }
            int w = rgb->width, h = rgb->height;
            { BenchTimer _(&t_p1hist);
            for (int y = 0; y < h; ++y) {
                const uint8_t* row = rgb->data[0] + y * rgb->linesize[0];
                for (int x = 0; x < w; ++x) {
                    int off = x * 3;
                    hist.add(row[off], row[off + 1], row[off + 2]);
                }
            }
            }
            total_px += (int64_t)w * h;
            ++frames1;
            if (on_progress) on_progress(frames1, total);
        };

        AVPacket* pkt;
        while (dmx.read(pkt))
            dec.decode(pkt, pass1);
        dec.flush(pass1);
    }

    fprintf(stderr, "\rpass 1: %d frames, %lld pixels accumulated\n",
            frames1, (long long)total_px);

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

    dmx.seek_start();
    avcodec_flush_buffers(dec.ctx.get());
    { BenchTimer _(&t_precomp); precompute_nearest_cache(pal); }

    scl.src_w = 0;

    GifEnc gif(cfg.output, dw, dh, pal, dmx.time_base());

    int frames2 = 0;
    std::vector<uint8_t> idx_buf(dw * dh);
    std::vector<int16_t> fs_buf;
    std::vector<uint8_t> prev_idx;
    int64_t prev_pts = AV_NOPTS_VALUE;
    int64_t last_dur_us = 0;

    if (use_threads) {
        SPSCRing<FrameData, 4> ring2;
        std::exception_ptr prod_err;

        std::thread t2([&] {
            try {
                Scaler tscl;
                tscl.dst_w = dw;
                tscl.dst_h = dh;
                int64_t last_pts = AV_NOPTS_VALUE;

                auto on_frame = [&](AVFrame* f) {
                    if (last_pts != AV_NOPTS_VALUE && f->pts - last_pts < pts_step) return;
                    last_pts = f->pts;

                    BenchTimer bt(&t_p2dec);
                    AVFrame* rgb = tscl.scale(f, AV_PIX_FMT_RGB24);
                    int w = rgb->width, h = rgb->height;

                    FrameData fd;
                    fd.w = w; fd.h = h; fd.pts = f->pts;
                    fd.rgb.resize(w * h * 3);
                    for (int y = 0; y < h; ++y)
                        memcpy(fd.rgb.data() + y * w * 3,
                               rgb->data[0] + y * rgb->linesize[0], w * 3);

                    while (!ring2.try_push(std::move(fd)))
                        ;
                };

                AVPacket* pkt;
                while (dmx.read(pkt))
                    dec.decode(pkt, on_frame);
                dec.flush(on_frame);

                FrameData eof_fd;
                eof_fd.eof = true;
                while (!ring2.try_push(std::move(eof_fd)))
                    ;
            } catch (...) {
                prod_err = std::current_exception();
                FrameData eof; eof.eof = true;
                while (!ring2.try_push(std::move(eof)))
                    ;
            }
        });

        FrameData fd;
        for (;;) {
            while (!ring2.try_pop(fd))
                ;
            if (fd.eof) break;

            { BenchTimer bt(&t_p2dith);
              dither(fd.rgb.data(), idx_buf.data(), fd.w, fd.h, pal, cfg.dither, fs_buf);
            }

            if (prev_pts != AV_NOPTS_VALUE) {
                BenchTimer bt(&t_p2gif);
                gif.frame(prev_idx.data(), prev_pts, fd.pts);
                last_dur_us = av_rescale_q(fd.pts - prev_pts, dmx.time_base(), {1, 1000000});
            }

            if (prev_idx.size() != idx_buf.size()) prev_idx.resize(idx_buf.size());
            std::swap(prev_idx, idx_buf);
            prev_pts = fd.pts;
            ++frames2;
            if (on_progress) on_progress(frames2, frames1);
        }

        t2.join();
        if (prod_err) std::rethrow_exception(prod_err);
    } else {
        int64_t last_pts2 = AV_NOPTS_VALUE;

        auto pass2 = [&](AVFrame* f) {
            if (last_pts2 != AV_NOPTS_VALUE && f->pts - last_pts2 < pts_step) return;
            last_pts2 = f->pts;

            AVFrame* rgb;
            { BenchTimer _(&t_p2dec); rgb = scl.scale(f, AV_PIX_FMT_RGB24); }
            int w = rgb->width, h = rgb->height;
            int64_t cur_pts = f->pts;

            if (prev_pts != AV_NOPTS_VALUE) {
                BenchTimer _(&t_p2gif);
                gif.frame(prev_idx.data(), prev_pts, cur_pts);
                last_dur_us = av_rescale_q(cur_pts - prev_pts,
                                           dmx.time_base(), {1, 1000000});
            }

            { BenchTimer _(&t_p2dith);
            dither(rgb->data[0], idx_buf.data(), w, h, pal, cfg.dither, fs_buf);
            }

            if (prev_idx.size() != idx_buf.size()) prev_idx.resize(idx_buf.size());
            std::swap(prev_idx, idx_buf);
            prev_pts = cur_pts;
            ++frames2;
            if (on_progress) on_progress(frames2, frames1);
        };

        AVPacket* pkt;
        while (dmx.read(pkt))
            dec.decode(pkt, pass2);
        dec.flush(pass2);
    }

    if (prev_pts != AV_NOPTS_VALUE && !prev_idx.empty()) {
        if (last_dur_us <= 0)
            last_dur_us = av_rescale_q(1, {1, 30}, {1, 1000000}); // 30fps fallback
        gif.frame_last(prev_idx.data(), last_dur_us);
    }

    fprintf(stderr, "\rpass 2: %d frames encoded\n", frames2);

    double t_total = std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now() - t_total_start).count();
    fprintf(stderr, "[bench] pass1-decode:    %.1f ms\n", t_p1dec);
    fprintf(stderr, "[bench] pass1-histogram: %.1f ms\n", t_p1hist);
    fprintf(stderr, "[bench] palette-gen:     %.1f ms\n", t_palgen);
    fprintf(stderr, "[bench] cache-precomp:   %.1f ms\n", t_precomp);
    fprintf(stderr, "[bench] pass2-decode:    %.1f ms\n", t_p2dec);
    fprintf(stderr, "[bench] pass2-dither:    %.1f ms\n", t_p2dith);
    fprintf(stderr, "[bench] pass2-gifenc:    %.1f ms\n", t_p2gif);
    fprintf(stderr, "[bench] total:           %.1f ms\n", t_total);

    return frames2 > 0 ? 0 : 1;
}
