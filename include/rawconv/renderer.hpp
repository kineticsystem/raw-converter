// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <vector>

#include "rawconv/raw_image.hpp"

namespace rawconv
{

// The colour space of a rendered image. Each is linear: a value twice as
// large is twice as much light.
enum class ColorSpace
{
  // ITU-R BT.2020 primaries, D65 white: wide enough for every colour a camera
  // records, with few values out of gamut.
  REC2020,
  // ProPhoto RGB (ROMM) primaries, D50 white: wider still.
  PROPHOTO,
  // sRGB primaries, D65 white: what most programs assume, but saturated
  // colours, e.g. flowers and lights, fall out of it and are clipped.
  SRGB,
};

// A demosaiced image: linear RGB, 16 bits per sample, interleaved. The sensor's
// clipping level, for the colour the white balance amplifies least, is 65535,
// and so is a highlight the sensor clipped, whatever its colour.
struct RenderedImage
{
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint16_t> data;
  ColorSpace color_space = ColorSpace::REC2020;
  // TIFF/EXIF orientation, 1 to 8: the pixels are as the sensor saw them, and
  // this says how to turn them upright.
  uint16_t orientation = 1;
  Metadata metadata;

  uint16_t at(uint32_t row, uint32_t col, uint32_t sample) const
  {
    return data[(static_cast<size_t>(row) * width + col) * 3 + sample];
  }
};

// Renders the default crop of the image with LibRaw: the bad pixels listed in
// image.opcode_bad_pixels repaired, demosaiced with AHD, white balanced as
// shot, converted to `color_space` with the image's colour matrix. Nothing
// else: no exposure, no tone curve, no sharpening, no noise reduction, no
// rotation. Throws rawconv::Error when LibRaw cannot render it.
RenderedImage renderImage(const RawImage& image, ColorSpace color_space = ColorSpace::REC2020);

}  // namespace rawconv
