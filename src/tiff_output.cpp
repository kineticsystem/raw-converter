// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "rawconv/tiff_output.hpp"

#include <lcms2.h>

#include <algorithm>
#include <array>
#include <memory>

#include "rawconv/error.hpp"
#include "rawconv/file_store.hpp"
#include "exif.hpp"
#include "tiff/tiff_writer.hpp"

namespace rawconv
{

namespace
{

using tiff::Ifd;

enum Tag : uint16_t
{
  NEW_SUBFILE_TYPE = 254,
  IMAGE_WIDTH = 256,
  IMAGE_LENGTH = 257,
  BITS_PER_SAMPLE = 258,
  COMPRESSION = 259,
  PHOTOMETRIC = 262,
  IMAGE_DESCRIPTION = 270,
  MAKE = 271,
  MODEL = 272,
  STRIP_OFFSETS = 273,
  ORIENTATION = 274,
  SAMPLES_PER_PIXEL = 277,
  ROWS_PER_STRIP = 278,
  STRIP_BYTE_COUNTS = 279,
  X_RESOLUTION = 282,
  Y_RESOLUTION = 283,
  PLANAR_CONFIGURATION = 284,
  RESOLUTION_UNIT = 296,
  SOFTWARE = 305,
  DATE_TIME = 306,
  ARTIST = 315,
  COPYRIGHT = 33432,
  EXIF_IFD = 34665,
  ICC_PROFILE = 34675,
  GPS_IFD = 34853,
};

enum ExifTag : uint16_t
{
  COLOR_SPACE = 0xa001,
  PIXEL_X_DIMENSION = 0xa002,
  PIXEL_Y_DIMENSION = 0xa003,
};

constexpr uint16_t GPS_VERSION_ID = 0;

constexpr uint16_t COMPRESSION_NONE = 1;
constexpr uint16_t PHOTOMETRIC_RGB = 2;
constexpr uint16_t RESOLUTION_UNIT_INCH = 2;
// The EXIF colour space of anything but gamma-encoded sRGB: the ICC profile
// says what it is.
constexpr uint16_t COLOR_SPACE_UNCALIBRATED = 0xffff;
// About 1 MB per strip for a 5D Mark II image, 33 KB per row.
constexpr uint32_t ROWS_PER_STRIP_MAX = 32;

struct ProfileDeleter
{
  void operator()(void* profile) const
  {
    cmsCloseProfile(profile);
  }
};

struct ToneCurveDeleter
{
  void operator()(cmsToneCurve* curve) const
  {
    cmsFreeToneCurve(curve);
  }
};

std::vector<uint8_t> makeProfile(const cmsCIExyY& white, const cmsCIExyYTRIPLE& primaries, const char* description)
{
  std::unique_ptr<cmsToneCurve, ToneCurveDeleter> linear(cmsBuildGamma(nullptr, 1.0));
  cmsToneCurve* curves[3] = { linear.get(), linear.get(), linear.get() };
  std::unique_ptr<void, ProfileDeleter> profile(cmsCreateRGBProfile(&white, &primaries, curves));
  if (!profile || !linear)
  {
    throw Error("cannot make the ICC profile");
  }
  std::unique_ptr<cmsMLU, decltype(&cmsMLUfree)> text(cmsMLUalloc(nullptr, 1), &cmsMLUfree);
  cmsMLUsetASCII(text.get(), "en", "US", description);
  cmsWriteTag(profile.get(), cmsSigProfileDescriptionTag, text.get());
  cmsMLUsetASCII(text.get(), "en", "US", "No copyright, use freely");
  cmsWriteTag(profile.get(), cmsSigCopyrightTag, text.get());
  cmsUInt32Number size = 0;
  if (!cmsSaveProfileToMem(profile.get(), nullptr, &size))
  {
    throw Error("cannot save the ICC profile");
  }
  std::vector<uint8_t> bytes(size);
  if (!cmsSaveProfileToMem(profile.get(), bytes.data(), &size))
  {
    throw Error("cannot save the ICC profile");
  }
  return bytes;
}

std::vector<uint8_t> makeProfile(ColorSpace color_space)
{
  const cmsCIExyY d65 = { 0.3127, 0.3290, 1.0 };
  const cmsCIExyY d50 = { 0.3457, 0.3585, 1.0 };
  switch (color_space)
  {
    case ColorSpace::SRGB:
      return makeProfile(d65, { { 0.640, 0.330, 1 }, { 0.300, 0.600, 1 }, { 0.150, 0.060, 1 } }, "Linear sRGB");
    case ColorSpace::PROPHOTO:
      return makeProfile(d50, { { 0.7347, 0.2653, 1 }, { 0.1596, 0.8404, 1 }, { 0.0366, 0.0001, 1 } },
                         "Linear ProPhoto RGB");
    case ColorSpace::REC2020:
      break;
  }
  return makeProfile(d65, { { 0.708, 0.292, 1 }, { 0.170, 0.797, 1 }, { 0.131, 0.046, 1 } }, "Linear Rec. 2020");
}

// Rows of 16-bit samples, little-endian, as the TIFF stores them.
std::vector<std::vector<uint8_t>> strips(const RenderedImage& image, uint32_t rows_per_strip)
{
  const size_t row_samples = static_cast<size_t>(image.width) * 3;
  std::vector<std::vector<uint8_t>> out;
  for (uint32_t top = 0; top < image.height; top += rows_per_strip)
  {
    const uint32_t rows = std::min(rows_per_strip, image.height - top);
    std::vector<uint8_t> strip(row_samples * rows * 2);
    const uint16_t* src = image.data.data() + top * row_samples;
    for (size_t i = 0; i < row_samples * rows; ++i)
    {
      strip[2 * i] = static_cast<uint8_t>(src[i]);
      strip[2 * i + 1] = static_cast<uint8_t>(src[i] >> 8);
    }
    out.push_back(std::move(strip));
  }
  return out;
}

}  // namespace

std::vector<uint8_t> iccProfile(ColorSpace color_space)
{
  // Made once, on the first call: a function's static variables are
  // initialised safely from several threads.
  static const std::array<std::vector<uint8_t>, 3> profiles = { makeProfile(ColorSpace::REC2020),
                                                                makeProfile(ColorSpace::PROPHOTO),
                                                                makeProfile(ColorSpace::SRGB) };
  return profiles[static_cast<size_t>(color_space)];
}

std::vector<uint8_t> encodeTiff(const RenderedImage& image, const TiffWriteOptions& options)
{
  if (image.width == 0 || image.height == 0 || image.data.size() != static_cast<size_t>(image.width) * image.height * 3)
  {
    throw Error("cannot encode the TIFF: the image is empty or its data has the wrong size");
  }
  const Metadata& meta = image.metadata;
  const uint32_t rows_per_strip = std::min(ROWS_PER_STRIP_MAX, image.height);

  Ifd ifd0;
  ifd0.addLong(NEW_SUBFILE_TYPE, { 0 });
  ifd0.addLong(IMAGE_WIDTH, { image.width });
  ifd0.addLong(IMAGE_LENGTH, { image.height });
  ifd0.addShort(BITS_PER_SAMPLE, { 16, 16, 16 });
  ifd0.addShort(COMPRESSION, { COMPRESSION_NONE });
  ifd0.addShort(PHOTOMETRIC, { PHOTOMETRIC_RGB });
  ifd0.addShort(SAMPLES_PER_PIXEL, { 3 });
  ifd0.addLong(ROWS_PER_STRIP, { rows_per_strip });
  ifd0.addShort(PLANAR_CONFIGURATION, { 1 });
  ifd0.addDataOffsets(STRIP_OFFSETS, STRIP_BYTE_COUNTS, strips(image, rows_per_strip));
  ifd0.addShort(ORIENTATION, { image.orientation });
  ifd0.addRationalPairs(X_RESOLUTION, { 300, 1 });
  ifd0.addRationalPairs(Y_RESOLUTION, { 300, 1 });
  ifd0.addShort(RESOLUTION_UNIT, { RESOLUTION_UNIT_INCH });
  ifd0.addBytes(ICC_PROFILE, tiff::UNDEFINED, iccProfile(image.color_space));

  if (!meta.make.empty())
  {
    ifd0.addAscii(MAKE, meta.make);
  }
  if (!meta.model.empty())
  {
    ifd0.addAscii(MODEL, meta.model);
  }
  if (!meta.description.empty())
  {
    ifd0.addAscii(IMAGE_DESCRIPTION, meta.description);
  }
  ifd0.addAscii(SOFTWARE, options.software);
  if (!meta.date_time.empty())
  {
    ifd0.addAscii(DATE_TIME, meta.date_time);
  }
  if (!meta.artist.empty())
  {
    ifd0.addAscii(ARTIST, meta.artist);
  }
  if (!meta.copyright.empty())
  {
    ifd0.addAscii(COPYRIGHT, meta.copyright);
  }
  // The camera's EXIF describes its JPEG: sRGB, at the camera's size. The
  // size of a TIFF is in its own tags only.
  auto exif = exifIfd(meta);
  exif->addShort(COLOR_SPACE, { COLOR_SPACE_UNCALIBRATED });
  exif->remove(PIXEL_X_DIMENSION);
  exif->remove(PIXEL_Y_DIMENSION);
  ifd0.addChild(EXIF_IFD, exif);
  // Canon writes a GPS directory with its version alone, which says nothing
  // and which the TIFF rules count as incomplete: only a position is kept.
  const bool has_position =
      std::any_of(meta.gps.begin(), meta.gps.end(), [](const ExifEntry& entry) { return entry.tag != GPS_VERSION_ID; });
  if (has_position)
  {
    ifd0.addChild(GPS_IFD, gpsIfd(meta));
  }

  try
  {
    return tiff::TiffWriter::write(ifd0);
  }
  catch (const std::exception& e)
  {
    throw Error(e.what());
  }
}

void writeTiff(const RawImage& image, const std::filesystem::path& path, const TiffWriteOptions& options)
{
  LocalFileStore().write(path, StandardTiffEncoder().encode(image, options));
}

}  // namespace rawconv
