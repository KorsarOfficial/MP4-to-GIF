#pragma once
#include <memory>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
}

template<auto Fn>
struct Deleter {
    template<typename T>
    void operator()(T* p) const noexcept { if (p) Fn(p); }
};

template<auto Fn>
struct Deleterp {
    template<typename T>
    void operator()(T* p) const noexcept { if (p) Fn(&p); }
};

using FormatCtx = std::unique_ptr<AVFormatContext, Deleterp<avformat_close_input>>;
using CodecCtx  = std::unique_ptr<AVCodecContext,  Deleterp<avcodec_free_context>>;
using Frame     = std::unique_ptr<AVFrame,         Deleterp<av_frame_free>>;
using Packet    = std::unique_ptr<AVPacket,        Deleterp<av_packet_free>>;
using SwsCtx    = std::unique_ptr<SwsContext,      Deleter<sws_freeContext>>;

inline std::string av_err(int e) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_make_error_string(buf, sizeof buf, e);
    return buf;
}

struct PipelineError : std::runtime_error {
    int code;
    PipelineError(const char* what, int c = -1)
        : std::runtime_error(c < 0 ? std::string(what) + ": " + av_err(c)
                                   : std::string(what)), code(c) {}
};

inline void av_check(int r, const char* msg) {
    if (r < 0) throw PipelineError(msg, r);
}

using FrameCallback = void(*)(AVFrame*, void*);
