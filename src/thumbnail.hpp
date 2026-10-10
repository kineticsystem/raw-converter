// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "dngconv/raw_image.hpp"

namespace dngconv
{

struct RgbImage
{
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgb;  // 8-bit sRGB, interleaved.
};

// The camera-to-linear-sRGB matrix of an image, for its as-shot white
// balance: camera values divided by the neutral, times this matrix, give
// linear sRGB with white at (1, 1, 1). Row-major.
std::array<double, 9> cameraToSrgb(const RawImage& image);

// A small rendering of the default crop of the image, at most `max_side`
// pixels wide or high: the DNG thumbnail.
RgbImage renderThumbnail(const RawImage& image, uint32_t max_side = 256);

}  // namespace dngconv
