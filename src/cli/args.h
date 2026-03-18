#pragma once
#include "../dither/dither.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

enum class QuantMode { Wu, MedianCut };

struct Cfg {
    const char* input = nullptr;
    char output[512] = {};
    int fps = 10;
    double resize = 1.0;
    DitherMode dither = DitherMode::FloydSteinberg;
    QuantMode quant = QuantMode::Wu;
};

[[noreturn]] inline void usage(const char* prog) {
    fprintf(stderr,
        "usage: %s <input.mp4> [options]\n"
        "  -o, --output <path>   output GIF path (default: input with .gif extension)\n"
        "  -f, --fps <1-60>      target FPS (default: 10)\n"
        "  -r, --resize <factor> scale factor, e.g. 0.5 (default: 1.0)\n"
        "  -d, --dither <mode>   none|fs|bayer (default: fs)\n"
        "  -q, --quant <algo>    wu|median-cut (default: wu)\n"
        "  -h, --help            show this help\n", prog);
    exit(1);
}

[[noreturn]] inline void die(const char* msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

inline Cfg parse(int argc, char** argv) {
    if (argc < 2) usage(argv[0]);
    Cfg c;
    auto eq = [](const char* a, const char* b) { return strcmp(a, b) == 0; };
    for (int i = 1; i < argc; ++i) {
        if (eq(argv[i], "-h") || eq(argv[i], "--help")) usage(argv[0]);
        if (eq(argv[i], "-o") || eq(argv[i], "--output")) {
            if (++i >= argc) die("--output requires an argument");
            snprintf(c.output, sizeof c.output, "%s", argv[i]);
        } else if (eq(argv[i], "-f") || eq(argv[i], "--fps")) {
            if (++i >= argc) die("--fps requires an argument");
            c.fps = atoi(argv[i]);
            if (c.fps < 1 || c.fps > 60) die("fps must be 1-60");
        } else if (eq(argv[i], "-r") || eq(argv[i], "--resize")) {
            if (++i >= argc) die("--resize requires an argument");
            c.resize = atof(argv[i]);
            if (c.resize <= 0.0) die("resize must be positive");
        } else if (eq(argv[i], "-d") || eq(argv[i], "--dither")) {
            if (++i >= argc) die("--dither requires an argument");
            if (eq(argv[i], "none"))      c.dither = DitherMode::None;
            else if (eq(argv[i], "fs"))   c.dither = DitherMode::FloydSteinberg;
            else if (eq(argv[i], "bayer")) c.dither = DitherMode::Bayer;
            else { char buf[128]; snprintf(buf, sizeof buf, "unknown dither mode: %s", argv[i]); die(buf); }
        } else if (eq(argv[i], "-q") || eq(argv[i], "--quant")) {
            if (++i >= argc) die("--quant requires an argument");
            if (eq(argv[i], "wu"))             c.quant = QuantMode::Wu;
            else if (eq(argv[i], "median-cut")) c.quant = QuantMode::MedianCut;
            else { char buf[128]; snprintf(buf, sizeof buf, "unknown quant: %s", argv[i]); die(buf); }
        } else if (argv[i][0] == '-') {
            char buf[128]; snprintf(buf, sizeof buf, "unknown option: %s", argv[i]); die(buf);
        } else {
            if (c.input) die("multiple inputs not supported");
            c.input = argv[i];
        }
    }
    if (!c.input) die("no input file specified");

    if (c.output[0] == '\0') {
        const char* dot = strrchr(c.input, '.');
        auto eq_ext = [](const char* a, const char* b) {
            for (; *a && *b; ++a, ++b)
                if (((*a >= 'A' && *a <= 'Z') ? *a + 32 : *a) !=
                    ((*b >= 'A' && *b <= 'Z') ? *b + 32 : *b)) return false;
            return *a == *b;
        };
        bool known = dot && (eq_ext(dot, ".mp4") || eq_ext(dot, ".avi") ||
                             eq_ext(dot, ".mkv") || eq_ext(dot, ".mov") ||
                             eq_ext(dot, ".webm"));
        if (known) {
            int plen = (int)(dot - c.input);
            snprintf(c.output, sizeof c.output, "%.*s.gif", plen, c.input);
        } else {
            snprintf(c.output, sizeof c.output, "%s.gif", c.input);
        }
    }
    return c;
}
