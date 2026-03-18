#pragma GCC optimize("O2")
#include "pipeline/convert.h"
#include <cstdio>

int main(int argc, char** argv) {
    Cfg cfg = parse(argc, argv);
    int total_est = 0;
    auto prog = [&](int done, int total) {
        total_est = total;
        if (done % 10 == 0 || done == total)
            fprintf(stderr, "\r%d/%d frames (%.0f%%)", done, total,
                    total > 0 ? 100.0 * done / total : 0.0);
    };
    try {
        int r = convert(cfg, prog);
        if (total_est > 0) fprintf(stderr, "\n");
        fprintf(stderr, "-> %s\n", cfg.output);
        return r;
    } catch (const PipelineError& e) {
        fprintf(stderr, "\npipeline error: %s\n", e.what());
        return 2;
    } catch (const std::exception& e) {
        fprintf(stderr, "\nerror: %s\n", e.what());
        return 3;
    }
}
