// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "dngconv/camera_table.hpp"
#include "dngconv/raw_image.hpp"

namespace dngconv
{

// Turns the bytes of a camera raw file into a RawImage. The seam between the
// converter and the raw decoding library: tests give the converter a fake one
// that makes images without any file.
class RawDecoder
{
public:
  virtual ~RawDecoder() = default;
  // Throws dngconv::Error when the data cannot be decoded. Must be safe to call
  // from several threads at once.
  virtual RawImage decode(const uint8_t* data, size_t size, const std::string& file_name) const = 0;
};

// The decoder built on LibRaw: Canon CR2, and every other format LibRaw
// supports. The sensor data is kept as it is, with the black level, white
// level, colour matrix and white balance needed to render it, and the traits
// the camera table knows of the camera model.
class LibRawDecoder : public RawDecoder
{
public:
  // With the built-in camera table.
  LibRawDecoder();
  explicit LibRawDecoder(std::shared_ptr<const CameraTable> cameras);

  RawImage decode(const uint8_t* data, size_t size, const std::string& file_name) const override;

private:
  std::shared_ptr<const CameraTable> cameras_;
};

// Decodes a camera raw file with LibRawDecoder. Throws dngconv::Error when the
// file cannot be read or decoded.
RawImage readRawFile(const std::filesystem::path& path);

// The same, from a file already in memory, e.g. one just downloaded from a
// camera. `file_name` is recorded as the original file name in the DNG.
// Equivalent to LibRawDecoder().decode(data, size, file_name).
RawImage readRawBuffer(const uint8_t* data, size_t size, const std::string& file_name);

// Whether the file name has the extension of a raw format the converter
// handles, e.g. ".CR2". DNG files are not included.
bool isRawFileName(const std::filesystem::path& path);

// The LibRaw version the library was built with.
std::string librawVersion();

}  // namespace dngconv
