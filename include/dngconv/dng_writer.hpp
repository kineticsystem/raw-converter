// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "dngconv/raw_image.hpp"

namespace dngconv
{

enum class Compression
{
  // Lossless JPEG, as Adobe DNG Converter writes by default: about half the
  // size of an uncompressed file, and decoded by every DNG reader.
  LOSSLESS_JPEG,
  NONE,
};

struct DngWriteOptions
{
  Compression compression = Compression::LOSSLESS_JPEG;

  // Keep the camera's own JPEG as the DNG preview, which file browsers and
  // photo managers show before rendering the raw.
  bool embed_preview = true;

  // The DNG Software tag.
  std::string software = "dng-converter";

  // Threads compressing the tiles of one file. Leave at 1 when converting
  // several files in parallel already.
  unsigned threads = 1;
};

// Encodes the image as a DNG 1.4 file, described as Adobe DNG Converter
// describes the same raw file, which any DNG reader opens.
std::vector<uint8_t> encodeDng(const RawImage& image, const DngWriteOptions& options = {});

// Turns a RawImage into the bytes of a file: the seam between the converter
// and the output format, so that tests can capture what the converter would
// write.
class DngEncoder
{
public:
  virtual ~DngEncoder() = default;
  // Must be safe to call from several threads at once.
  virtual std::vector<uint8_t> encode(const RawImage& image, const DngWriteOptions& options) const = 0;
};

// The encoder of encodeDng().
class StandardDngEncoder : public DngEncoder
{
public:
  std::vector<uint8_t> encode(const RawImage& image, const DngWriteOptions& options) const override
  {
    return encodeDng(image, options);
  }
};

// Encodes the image and writes it to `path`, replacing an existing file only
// once the new one is complete. Throws dngconv::Error on failure.
void writeDng(const RawImage& image, const std::filesystem::path& path, const DngWriteOptions& options = {});

}  // namespace dngconv
