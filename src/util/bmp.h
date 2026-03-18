#pragma once
#include <cstdint>

void save_bmp(const char* path, const uint8_t* bgr, int w, int h, int stride);
