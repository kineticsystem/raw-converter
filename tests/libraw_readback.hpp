// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// Reads a DNG back with LibRaw, a DNG reader independent of the code that
// wrote it, so that the tests check the files rather than the writer's idea of
// them.

#pragma once

#include <libraw/libraw.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace dngconv::test
{

struct Decoded
{
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t samples = 1;
  std::vector<uint16_t> data;       // The visible area, samples interleaved.
  std::array<int, 4> cfa_colors{};  // 0 = R, 1 = G, 2 = B, of the 2x2 cells.
  std::array<unsigned, 4> black{};  // Of the 2x2 cells.
  unsigned white = 0;
  std::array<float, 3> neutral_mul{};  // Camera multipliers, green = 1.
  std::string make, model;
};

inline Decoded readBack(const std::vector<uint8_t>& dng)
{
  auto raw = std::make_unique<LibRaw>();
  if (raw->open_buffer(dng.data(), dng.size()) != LIBRAW_SUCCESS || raw->unpack() != LIBRAW_SUCCESS)
  {
    throw std::runtime_error("LibRaw cannot read the DNG");
  }
  const auto& d = raw->imgdata;
  Decoded out;
  out.width = d.sizes.width;
  out.height = d.sizes.height;
  out.make = d.idata.make;
  out.model = d.idata.model;
  out.white = d.color.maximum;
  for (int c = 0; c < 3; ++c)
  {
    out.neutral_mul[c] = d.color.cam_mul[c] / d.color.cam_mul[1];
  }
  if (d.rawdata.raw_image != nullptr)
  {
    const size_t pitch = d.sizes.raw_pitch / 2;
    out.data.resize(static_cast<size_t>(out.width) * out.height);
    for (uint32_t row = 0; row < out.height; ++row)
    {
      for (uint32_t col = 0; col < out.width; ++col)
      {
        out.data[static_cast<size_t>(row) * out.width + col] =
            d.rawdata.raw_image[(row + d.sizes.top_margin) * pitch + col + d.sizes.left_margin];
      }
    }
    for (int i = 0; i < 4; ++i)
    {
      const int slot = raw->COLOR(i / 2, i % 2);
      const char c = d.idata.cdesc[slot];
      out.cfa_colors[i] = c == 'R' ? 0 : c == 'G' ? 1 : 2;
      // A DNG BlackLevel with a 2x2 repeat lands in cblack[6..9].
      out.black[i] = d.color.black + d.color.cblack[slot] +
                     (d.color.cblack[4] == 2 && d.color.cblack[5] == 2 ? d.color.cblack[6 + i] : 0);
    }
  }
  else
  {
    out.samples = 3;
    out.data.resize(static_cast<size_t>(out.width) * out.height * 3);
    const auto* four = d.rawdata.color4_image;
    const auto* three = d.rawdata.color3_image;
    for (uint32_t row = 0; row < out.height; ++row)
    {
      for (uint32_t col = 0; col < out.width; ++col)
      {
        const size_t src = (row + d.sizes.top_margin) * d.sizes.raw_width + col + d.sizes.left_margin;
        for (int c = 0; c < 3; ++c)
        {
          out.data[(static_cast<size_t>(row) * out.width + col) * 3 + c] = four ? four[src][c] : three[src][c];
        }
      }
    }
  }
  return out;
}

}  // namespace dngconv::test
