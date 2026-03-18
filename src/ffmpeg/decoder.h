#pragma once
#include "ff_types.h"

struct Decoder {
    CodecCtx ctx;
    Frame    frm;

    Decoder(const AVCodec* codec, AVCodecParameters* par) {
        ctx.reset(avcodec_alloc_context3(codec));
        if (!ctx) throw PipelineError("avcodec_alloc_context3");
        av_check(avcodec_parameters_to_context(ctx.get(), par),
                 "avcodec_parameters_to_context");
        av_check(avcodec_open2(ctx.get(), codec, nullptr),
                 "avcodec_open2");
        frm.reset(av_frame_alloc());
        if (!frm) throw PipelineError("av_frame_alloc");
    }

    int width()  const { return ctx->width; }
    int height() const { return ctx->height; }

    template<typename F>
    void decode(AVPacket* pkt, F&& on_frame) {
        int r = avcodec_send_packet(ctx.get(), pkt);
        if (r < 0 && r != AVERROR(EAGAIN)) return;
        drain(on_frame);
    }

    template<typename F>
    void flush(F&& on_frame) {
        avcodec_send_packet(ctx.get(), nullptr);
        drain(on_frame);
    }

private:
    template<typename F>
    void drain(F& on_frame) {
        for (;;) {
            int r = avcodec_receive_frame(ctx.get(), frm.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            if (r < 0) throw PipelineError("avcodec_receive_frame", r);
            on_frame(frm.get());
        }
    }
};
