// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rawconv/raw_image.hpp"
#include "rawconv/renderer.hpp"

namespace rawconv
{

struct TiffWriteOptions
{
  ColorSpace color_space = ColorSpace::REC2020;

  // The TIFF Software tag.
  std::string software = "raw-converter";
};

// An ICC profile of the colour space, linear, as the TIFF embeds it, so that
// every program that reads the file knows what its values mean.
std::vector<uint8_t> iccProfile(ColorSpace color_space);

// Encodes a rendered image as an uncompressed, 16-bit RGB TIFF, with its ICC
// profile, its orientation and the original's EXIF and GPS data.
std::vector<uint8_t> encodeTiff(const RenderedImage& image, const TiffWriteOptions& options = {});

// Turns a RawImage into the bytes of a TIFF: the seam between the converter
// and the rendering, so that tests can capture what the converter would
// write.
class TiffEncoder
{
public:
  virtual ~TiffEncoder() = default;
  // Must be safe to call from several threads at once.
  virtual std::vector<uint8_t> encode(const RawImage& image, const TiffWriteOptions& options) const = 0;
};

// Renders with renderImage(), then encodes with encodeTiff().
class StandardTiffEncoder : public TiffEncoder
{
public:
  std::vector<uint8_t> encode(const RawImage& image, const TiffWriteOptions& options) const override
  {
    return encodeTiff(renderImage(image, options.color_space), options);
  }
};

// Renders the image and writes it to `path` as a TIFF, replacing an existing
// file only once the new one is complete. Throws rawconv::Error on failure.
void writeTiff(const RawImage& image, const std::filesystem::path& path, const TiffWriteOptions& options = {});

}  // namespace rawconv
