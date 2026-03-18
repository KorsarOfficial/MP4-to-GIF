#include "bmp.h"
#include <cstdio>
#include <stdexcept>

#pragma pack(push, 1)
struct BmpHdr {
    uint16_t sig = 0x4D42;    // 'BM'
    uint32_t fsz;             // file size
    uint32_t reserved = 0;
    uint32_t off = 54;        // pixel data offset
    uint32_t hsz = 40;        // info header size
    int32_t  w, h;            // width, height (negative = top-down)
    uint16_t planes = 1;
    uint16_t bpp = 24;
    uint32_t comp = 0;        // BI_RGB
    uint32_t isz;             // image size
    int32_t  xppm = 2835, yppm = 2835; // 72 DPI
    uint32_t clr = 0, imp = 0;
};
#pragma pack(pop)

void save_bmp(const char* path, const uint8_t* bgr, int w, int h, int stride) {
    if (!bgr || w <= 0 || h <= 0)
        throw std::runtime_error("save_bmp: invalid parameters");

    int row_bytes = w * 3;
    int pad = (4 - row_bytes % 4) % 4;
    int img_sz = (row_bytes + pad) * h;

    BmpHdr hdr;
    hdr.fsz = 54 + img_sz;
    hdr.w = w;
    hdr.h = -h;  // negative height = top-down storage
    hdr.isz = img_sz;

    FILE* f = fopen(path, "wb");
    if (!f) throw std::runtime_error(std::string("save_bmp: cannot open ") + path);
    uint8_t zeros[4] = {};
    bool ok = fwrite(&hdr, 54, 1, f) == 1;
    for (int y = 0; ok && y < h; ++y) {
        ok = fwrite(bgr + y * stride, row_bytes, 1, f) == 1;
        if (ok && pad) ok = fwrite(zeros, pad, 1, f) == 1;
    }
    fclose(f);
    if (!ok) throw std::runtime_error(std::string("save_bmp: write failed ") + path);
}
