#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

struct FrameData {
    std::vector<uint8_t> rgb;
    int w, h;
    int64_t pts;
    bool eof;

    FrameData() : w(0), h(0), pts(0), eof(false) {}
};

template<typename T, size_t N>
struct SPSCRing {
    static_assert((N & (N - 1)) == 0, "N must be power of 2");

    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    alignas(64) T buf[N];

    bool try_push(T&& val) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t nxt = (h + 1) & (N - 1);
        if (nxt == tail.load(std::memory_order_acquire)) return false;
        buf[h] = std::move(val);
        head.store(nxt, std::memory_order_release);
        return true;
    }

    bool try_pop(T& val) {
        size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        val = std::move(buf[t]);
        tail.store((t + 1) & (N - 1), std::memory_order_release);
        return true;
    }
};
