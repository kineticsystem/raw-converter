// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "exif.hpp"

#include <algorithm>
#include <cmath>

namespace rawconv
{

namespace
{

using tiff::Ifd;

enum ExifTag : uint16_t
{
  EXPOSURE_TIME = 0x829a,
  F_NUMBER = 0x829d,
  ISO_SPEED = 0x8827,
  EXIF_VERSION = 0x9000,
  DATE_TIME_ORIGINAL = 0x9003,
  FOCAL_LENGTH = 0x920a,
  BODY_SERIAL_NUMBER = 0xa431,
  LENS_SPECIFICATION = 0xa432,
  LENS_MODEL = 0xa434,
};

}  // namespace

std::vector<uint32_t> fraction(double value)
{
  if (value <= 0)
  {
    return { 0, 1 };
  }
  if (value < 1)
  {
    const double inverse = 1 / value;
    if (std::abs(inverse - std::round(inverse)) < 1e-3)
    {
      return { 1, static_cast<uint32_t>(std::lround(inverse)) };
    }
  }
  return { static_cast<uint32_t>(std::lround(value * 1000)), 1000 };
}

std::shared_ptr<Ifd> exifIfd(const Metadata& metadata)
{
  auto exif = std::make_shared<Ifd>();
  for (const auto& entry : metadata.exif)
  {
    exif->addRaw(entry.tag, entry.type, entry.count, entry.data);
  }

  // A file that is not TIFF based has no EXIF to copy: rebuild the essentials.
  if (metadata.exif.empty())
  {
    if (metadata.exposure_time > 0)
    {
      exif->addRationalPairs(EXPOSURE_TIME, fraction(metadata.exposure_time));
    }
    if (metadata.f_number > 0)
    {
      exif->addRationalPairs(F_NUMBER, fraction(metadata.f_number));
    }
    if (metadata.iso > 0)
    {
      exif->addShort(ISO_SPEED, { static_cast<uint16_t>(std::min(65535.0f, metadata.iso)) });
    }
    if (!metadata.date_time.empty())
    {
      exif->addAscii(DATE_TIME_ORIGINAL, metadata.date_time);
    }
    if (metadata.focal_length > 0)
    {
      exif->addRationalPairs(FOCAL_LENGTH, fraction(metadata.focal_length));
    }
  }

  // The EXIF 2.3 lens and body fields, which older cameras such as the 5D Mark
  // II only have in their maker notes, and which photo managers show and
  // filter by.
  bool added = false;
  if (!exif->has(LENS_MODEL) && !metadata.lens_model.empty())
  {
    exif->addAscii(LENS_MODEL, metadata.lens_model);
    added = true;
  }
  if (!exif->has(LENS_SPECIFICATION) && metadata.lens_info[0] > 0)
  {
    std::vector<uint32_t> pairs;
    for (float v : metadata.lens_info)
    {
      if (v > 0)
      {
        const auto f = fraction(v);
        pairs.insert(pairs.end(), f.begin(), f.end());
      }
      else
      {
        pairs.insert(pairs.end(), { 0, 0 });  // Unknown, as EXIF 2.3 allows.
      }
    }
    exif->addRationalPairs(LENS_SPECIFICATION, pairs);
    added = true;
  }
  if (!exif->has(BODY_SERIAL_NUMBER) && !metadata.serial_number.empty())
  {
    exif->addAscii(BODY_SERIAL_NUMBER, metadata.serial_number);
    added = true;
  }
  if (added || !exif->has(EXIF_VERSION))
  {
    exif->addBytes(EXIF_VERSION, tiff::UNDEFINED, { '0', '2', '3', '0' });
  }
  return exif;
}

std::shared_ptr<Ifd> gpsIfd(const Metadata& metadata)
{
  auto gps = std::make_shared<Ifd>();
  for (const auto& entry : metadata.gps)
  {
    gps->addRaw(entry.tag, entry.type, entry.count, entry.data);
  }
  return gps;
}

}  // namespace rawconv
