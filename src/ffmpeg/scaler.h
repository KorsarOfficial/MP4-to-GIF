#pragma once
#include "ff_types.h"
#include <algorithm>

struct Scaler {
    SwsCtx sws;
    Frame  dst;
    int    dst_w = 0, dst_h = 0;
    int    src_w = 0, src_h = 0;
    int    src_fmt = -1;

    static std::pair<int,int> fit(int sw, int sh, int mw, int mh,
                                  AVRational sar = {0,0}) {
        double ew = sw;
        if (sar.num > 0 && sar.den > 0 && !(sar.num == 1 && sar.den == 1))
            ew = sw * (double)sar.num / sar.den;
        double sc = std::min((double)mw / ew, (double)mh / sh);
        int dw = std::max(2, (int)(ew * sc) & ~1);
        int dh = std::max(2, (int)(sh * sc) & ~1);
        return {dw, dh};
    }

    static std::pair<int,int> factor(int sw, int sh, double f) {
        int dw = std::max(2, (int)(sw * f) & ~1);
        int dh = std::max(2, (int)(sh * f) & ~1);
        return {dw, dh};
    }

    static int detect_cs(const AVFrame* f) {
        switch (f->colorspace) {
            case AVCOL_SPC_BT709:     return SWS_CS_ITU709;
            case AVCOL_SPC_BT470BG:   return SWS_CS_ITU601;
            case AVCOL_SPC_SMPTE170M: return SWS_CS_SMPTE170M;
            default:
                return f->width <= 720 ? SWS_CS_ITU601 : SWS_CS_ITU709;
        }
    }

    AVFrame* scale(AVFrame* f, AVPixelFormat out_fmt = AV_PIX_FMT_BGR24) {
        if (!f) return nullptr;
        bool need = !sws
            || f->width  != src_w
            || f->height != src_h
            || f->format != src_fmt;

        if (need) {
            src_w   = f->width;
            src_h   = f->height;
            src_fmt = f->format;

            sws.reset(sws_getContext(
                src_w, src_h, (AVPixelFormat)src_fmt,
                dst_w, dst_h, out_fmt,
                SWS_BILINEAR, nullptr, nullptr, nullptr));
            if (!sws) throw PipelineError("sws_getContext");

            dst.reset(av_frame_alloc());
            if (!dst) throw PipelineError("av_frame_alloc (scaler dst)");
            dst->format = out_fmt;
            dst->width  = dst_w;
            dst->height = dst_h;
            av_check(av_frame_get_buffer(dst.get(), 32),
                     "av_frame_get_buffer");
        }

        int cs = detect_cs(f);
        int sr = (f->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;
        sws_setColorspaceDetails(sws.get(),
            sws_getCoefficients(cs), sr,
            sws_getCoefficients(SWS_CS_DEFAULT), 1,
            0, 1 << 16, 1 << 16);

        sws_scale(sws.get(),
                  f->data, f->linesize, 0, src_h,
                  dst->data, dst->linesize);
        return dst.get();
    }
};
