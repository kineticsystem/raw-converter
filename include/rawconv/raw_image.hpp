// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rawconv/camera_table.hpp"

namespace rawconv
{

// How the samples of a raw image are arranged.
enum class RawLayout
{
  // One sample per pixel behind a colour filter array, e.g. a Bayer sensor.
  CFA,
  // Three samples (R, G, B in camera space) per pixel, already demosaiced,
  // e.g. Canon sRAW/mRAW. Written as a DNG "LinearRaw" image.
  LINEAR_RAW,
};

// A rectangle in pixels.
struct Rect
{
  uint32_t top = 0;
  uint32_t left = 0;
  uint32_t height = 0;
  uint32_t width = 0;
};

// A pixel position, row first.
struct PixelPos
{
  uint32_t row = 0;
  uint32_t col = 0;

  friend bool operator==(const PixelPos& a, const PixelPos& b)
  {
    return a.row == b.row && a.col == b.col;
  }
  friend bool operator<(const PixelPos& a, const PixelPos& b)
  {
    return a.row != b.row ? a.row < b.row : a.col < b.col;
  }
};

// One TIFF entry copied from the original file, value already little-endian.
// It holds no offsets into the original file.
struct ExifEntry
{
  uint16_t tag = 0;
  uint16_t type = 0;
  uint32_t count = 0;
  std::vector<uint8_t> data;
};

// What is known of the shot besides its pixels.
struct Metadata
{
  std::string make;                 // E.g. "Canon".
  std::string model;                // E.g. "Canon EOS 5D Mark II".
  std::string unique_camera_model;  // DNG UniqueCameraModel, e.g. "Canon EOS 5D Mark II".
  std::string serial_number;
  std::string artist;
  std::string copyright;
  std::string description;
  std::string date_time;  // EXIF format, "YYYY:MM:DD HH:MM:SS"; empty if unknown.
  std::string original_file_name;
  // The original file's own TIFF Orientation, 1 to 8, as the camera wrote it;
  // 0 when it has none.
  uint16_t orientation = 0;

  float iso = 0;
  float exposure_time = 0;  // Seconds.
  float f_number = 0;
  float focal_length = 0;  // Millimetres.

  std::string lens_model;
  // Min and max focal length, max aperture at min and at max focal length. A
  // zero means unknown.
  std::array<float, 4> lens_info = { 0, 0, 0, 0 };

  // The EXIF and GPS directories of the original file, when it is TIFF based.
  // They are copied into the DNG; when empty, a minimal EXIF directory is
  // built from the fields above instead.
  std::vector<ExifEntry> exif;
  std::vector<ExifEntry> gps;

  // The original maker note, which DNG keeps in DNGPrivateData the way Adobe
  // DNG Converter does, so that tools such as exiftool still decode it.
  std::vector<uint8_t> maker_note;
  bool maker_note_big_endian = false;
  uint32_t maker_note_offset = 0;  // Its offset in the original file.
};

// A decoded raw image, ready to be written as DNG: the sensor data, unchanged,
// plus what a raw editor needs to render it.
struct RawImage
{
  RawLayout layout = RawLayout::CFA;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t samples_per_pixel = 1;  // 1 for CFA, 3 for LINEAR_RAW.
  uint32_t bits_per_sample = 16;   // Significant bits, e.g. 14 for a Canon CR2.

  // Row-major, samples interleaved, width * height * samples_per_pixel values.
  std::vector<uint16_t> data;

  // The colour of each cell of the 2x2 CFA repeat pattern, row-major, starting
  // at the top-left pixel: 0 = red, 1 = green, 2 = blue. E.g. {0, 1, 1, 2} is
  // RGGB. Only meaningful for RawLayout::CFA.
  std::array<uint8_t, 4> cfa_pattern = { 0, 1, 1, 2 };

  // The black level of each cell of the CFA pattern (CFA), or of each sample
  // (LINEAR_RAW, first samples_per_pixel values).
  std::array<double, 4> black_level = { 0, 0, 0, 0 };
  // The value at which the sensor clips.
  uint32_t white_level = 65535;

  // The area of the image the camera itself would output; what is outside is
  // only there to help demosaicing at the edges.
  Rect default_crop;

  // TIFF/EXIF orientation, 1 to 8.
  uint16_t orientation = 1;

  // XYZ (D65) to camera colour matrix, row-major, the DNG ColorMatrix for the
  // D65 calibration illuminant.
  std::array<double, 9> color_matrix = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  // The camera's values for a neutral (white) object under the shot's light:
  // the as-shot white balance.
  std::array<double, 3> as_shot_neutral = { 1, 1, 1 };
  // What the DNG says about the camera model beyond its raw file: the
  // baseline exposure and the like, from a CameraTable.
  CameraTraits camera_traits;

  Metadata metadata;

  // Bad pixels the raw editor should repair when rendering, written as a
  // DNG FixBadPixelsList opcode: the values in `data` stay as they are.
  std::vector<PixelPos> opcode_bad_pixels;

  // The whole frame of the sensor, masked border included, which the DNG
  // stores as Adobe DNG Converter does, with `active_area` marking where
  // `data` lies in it. The samples under the active area are not written:
  // those of `data` are, repaired or not. Empty when only `data` is stored,
  // e.g. for a LINEAR_RAW image.
  std::vector<uint16_t> sensor;
  uint32_t sensor_width = 0;
  uint32_t sensor_height = 0;
  Rect active_area;

  // The camera's own JPEG preview, kept as the DNG preview. Empty if none.
  std::vector<uint8_t> preview_jpeg;
  uint32_t preview_width = 0;
  uint32_t preview_height = 0;

  uint16_t& at(uint32_t row, uint32_t col, uint32_t sample = 0)
  {
    return data[(static_cast<size_t>(row) * width + col) * samples_per_pixel + sample];
  }
  uint16_t at(uint32_t row, uint32_t col, uint32_t sample = 0) const
  {
    return data[(static_cast<size_t>(row) * width + col) * samples_per_pixel + sample];
  }

  // The colour (0 = red, 1 = green, 2 = blue) of a pixel of a CFA image.
  uint8_t cfaColor(uint32_t row, uint32_t col) const
  {
    return cfa_pattern[(row & 1) * 2 + (col & 1)];
  }
  double blackAt(uint32_t row, uint32_t col) const
  {
    return black_level[(row & 1) * 2 + (col & 1)];
  }
};

}  // namespace rawconv
