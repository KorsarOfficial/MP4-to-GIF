#pragma once
#include "ff_types.h"

struct Demuxer {
    FormatCtx fmt;
    Packet    pkt;
    const AVCodec* codec = nullptr;
    int       vidx = -1;

    explicit Demuxer(const char* path) {
        AVFormatContext* raw = nullptr;
        av_check(avformat_open_input(&raw, path, nullptr, nullptr),
                 "avformat_open_input");
        fmt.reset(raw);
        av_check(avformat_find_stream_info(fmt.get(), nullptr),
                 "avformat_find_stream_info");
        vidx = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO,
                                   -1, -1, &codec, 0);
        if (vidx < 0)
            throw PipelineError("no video stream found", vidx);
        pkt.reset(av_packet_alloc());
        if (!pkt) throw PipelineError("av_packet_alloc");
    }

    AVCodecParameters* par() const { return fmt->streams[vidx]->codecpar; }
    AVRational time_base() const   { return fmt->streams[vidx]->time_base; }
    int64_t duration() const       { return fmt->streams[vidx]->duration; }
    int nb_frames() const          { return (int)fmt->streams[vidx]->nb_frames; }

    void seek_start() {
        av_check(avformat_seek_file(fmt.get(), vidx, INT64_MIN, 0, 0, 0),
                 "avformat_seek_file");
    }

    bool read(AVPacket*& out) {
        for (;;) {
            av_packet_unref(pkt.get());
            int r = av_read_frame(fmt.get(), pkt.get());
            if (r == AVERROR_EOF) return false;
            if (r < 0) throw PipelineError("av_read_frame", r);
            if (pkt->stream_index == vidx) {
                out = pkt.get();
                return true;
            }
        }
    }
};
