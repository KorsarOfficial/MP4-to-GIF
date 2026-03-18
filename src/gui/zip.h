#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <zlib.h>

struct ZipEntry {
    std::string name;
    std::vector<uint8_t> data;
    uint32_t crc;
    uint32_t offset;
};

struct ZipWriter {
    std::vector<ZipEntry> entries;

    void add(const char* nm, const uint8_t* d, size_t len) {
        ZipEntry e;
        e.name = nm;
        e.data.assign(d, d + len);
        e.crc  = crc32(0, d, (uInt)len);
        e.offset = 0;
        entries.push_back(std::move(e));
    }

    void add_file(const char* nm, const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> buf(sz);
        fread(buf.data(), 1, sz, f);
        fclose(f);
        add(nm, buf.data(), (size_t)sz);
    }

    bool write(const char* path) {
        FILE* f = fopen(path, "wb");
        if (!f) return false;

        auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
        auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };

        for (auto& e : entries) {
            e.offset = (uint32_t)ftell(f);
            w32(0x04034b50); w16(20); w16(0); w16(0);
            w16(0); w16(0);
            w32(e.crc);
            w32((uint32_t)e.data.size());
            w32((uint32_t)e.data.size());
            w16((uint16_t)e.name.size()); w16(0);
            fwrite(e.name.c_str(), 1, e.name.size(), f);
            fwrite(e.data.data(), 1, e.data.size(), f);
        }

        uint32_t cd_off = (uint32_t)ftell(f);
        for (auto& e : entries) {
            w32(0x02014b50); w16(20); w16(20); w16(0); w16(0);
            w16(0); w16(0);
            w32(e.crc);
            w32((uint32_t)e.data.size());
            w32((uint32_t)e.data.size());
            w16((uint16_t)e.name.size());
            w16(0); w16(0); w16(0); w16(0);
            w32(0); w32(e.offset);
            fwrite(e.name.c_str(), 1, e.name.size(), f);
        }
        uint32_t cd_sz = (uint32_t)ftell(f) - cd_off;

        w32(0x06054b50); w16(0); w16(0);
        w16((uint16_t)entries.size());
        w16((uint16_t)entries.size());
        w32(cd_sz); w32(cd_off); w16(0);

        fclose(f);
        return true;
    }
};
