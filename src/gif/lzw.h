#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

struct LzwWriter {
    static constexpr int TABLE_SIZE = 8192;
    static constexpr int MAX_CODE   = 4095;
    static constexpr int BLOCK_MAX  = 255;
    static constexpr int OUT_BUF_SZ = 32768;

    struct alignas(8) Entry { uint32_t key; int16_t code; int16_t pad; };

    Entry    table[TABLE_SIZE];

    FILE*    fp;
    uint8_t  block[256];
    uint8_t  out_buf[OUT_BUF_SZ];
    int      out_pos;
    uint64_t accum;
    int      accum_bits;
    int      block_len;
    int      clear_code;
    int      eoi_code;
    int      next_code;
    int      code_size;
    int      init_code_size;
    int      cur_prefix;

    LzwWriter() : fp(nullptr) {}

    void begin_frame(FILE* f, int color_depth) {
        fp = f;
        init_code_size = color_depth;
        clear_code = 1 << color_depth;
        eoi_code   = clear_code + 1;

        uint8_t mcs = (uint8_t)color_depth;
        fwrite(&mcs, 1, 1, fp);

        accum      = 0;
        accum_bits = 0;
        block_len  = 0;
        out_pos    = 0;
        cur_prefix = -1;

        memset(table, 0xFF, sizeof(table));
        next_code = eoi_code + 1;
        code_size = init_code_size + 1;
        emit_slow(clear_code);
    }

    void compress(const uint8_t* __restrict pixels, int count) {
        int prefix = cur_prefix;
        uint64_t acc = accum;
        int acc_bits = accum_bits;
        int cs = code_size;
        int nc = next_code;
        int blen = block_len;

        for (int i = 0; i < count; ++i) {
            int c = pixels[i];

            if (__builtin_expect(prefix < 0, 0)) {
                prefix = c;
                continue;
            }

            uint32_t key = ((uint32_t)prefix << 8) | (uint32_t)c;
            int slot = (int)(((key >> 12) ^ key) & (TABLE_SIZE - 1));

            for (;;) {
                auto& e = table[slot];
                if (e.key == key) {
                    prefix = e.code;
                    goto next_pixel;
                }
                if (e.key == 0xFFFFFFFF) break;
                slot = (slot + 1) & (TABLE_SIZE - 1);
            }

            acc |= (uint64_t)(uint32_t)prefix << acc_bits;
            acc_bits += cs;
            while (acc_bits >= 8) {
                block[blen++] = (uint8_t)(acc & 0xFF);
                acc >>= 8;
                acc_bits -= 8;
                if (__builtin_expect(blen == BLOCK_MAX, 0)) {
                    if (__builtin_expect(out_pos + 256 > OUT_BUF_SZ, 0)) {
                        fwrite(out_buf, 1, out_pos, fp);
                        out_pos = 0;
                    }
                    out_buf[out_pos++] = (uint8_t)BLOCK_MAX;
                    memcpy(out_buf + out_pos, block, BLOCK_MAX);
                    out_pos += BLOCK_MAX;
                    blen = 0;
                }
            }

            if (__builtin_expect(nc <= MAX_CODE, 1)) {
                table[slot] = { key, (int16_t)nc, 0 };
                if (nc == (1 << cs) && cs < 12)
                    ++cs;
                ++nc;
            } else {
                acc |= (uint64_t)(uint32_t)clear_code << acc_bits;
                acc_bits += cs;
                while (acc_bits >= 8) {
                    block[blen++] = (uint8_t)(acc & 0xFF);
                    acc >>= 8;
                    acc_bits -= 8;
                    if (__builtin_expect(blen == BLOCK_MAX, 0)) {
                        if (__builtin_expect(out_pos + 256 > OUT_BUF_SZ, 0)) {
                            fwrite(out_buf, 1, out_pos, fp);
                            out_pos = 0;
                        }
                        out_buf[out_pos++] = (uint8_t)BLOCK_MAX;
                        memcpy(out_buf + out_pos, block, BLOCK_MAX);
                        out_pos += BLOCK_MAX;
                        blen = 0;
                    }
                }
                memset(table, 0xFF, sizeof(table));
                nc = eoi_code + 1;
                cs = init_code_size + 1;
            }

            prefix = c;
            next_pixel:;
        }

        cur_prefix = prefix;
        accum      = acc;
        accum_bits = acc_bits;
        code_size  = cs;
        next_code  = nc;
        block_len  = blen;
    }

    void finish() {
        if (cur_prefix >= 0)
            emit_slow(cur_prefix);
        emit_slow(eoi_code);

        while (accum_bits > 0) {
            block[block_len++] = (uint8_t)(accum & 0xFF);
            accum >>= 8;
            accum_bits -= 8;
            if (block_len == BLOCK_MAX)
                flush_block();
        }

        flush_block();

        if (out_pos > 0) {
            fwrite(out_buf, 1, out_pos, fp);
            out_pos = 0;
        }

        uint8_t zero = 0;
        fwrite(&zero, 1, 1, fp);
    }

private:
    void emit_slow(int code) {
        accum |= (uint64_t)(uint32_t)code << accum_bits;
        accum_bits += code_size;
        while (accum_bits >= 8) {
            block[block_len++] = (uint8_t)(accum & 0xFF);
            accum >>= 8;
            accum_bits -= 8;
            if (block_len == BLOCK_MAX)
                flush_block();
        }
    }

    void flush_block() {
        if (block_len > 0) {
            if (__builtin_expect(out_pos + 256 > OUT_BUF_SZ, 0)) {
                fwrite(out_buf, 1, out_pos, fp);
                out_pos = 0;
            }
            out_buf[out_pos++] = (uint8_t)block_len;
            memcpy(out_buf + out_pos, block, block_len);
            out_pos += block_len;
            block_len = 0;
        }
    }
};
