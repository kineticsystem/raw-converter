// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// The rendering with LibRaw, the ICC profiles and the TIFF encoder. The TIFF
// is read back with the library's own TIFF reader, the profiles with Little
// CMS.

#include <gtest/gtest.h>

#include <lcms2.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "rawconv/renderer.hpp"
#include "rawconv/tiff_output.hpp"
#include "test_support.hpp"
#include "tiff/tiff_reader.hpp"
#include "tiff/tiff_writer.hpp"

namespace rawconv::test
{
namespace
{

// Converts linear RGB in the profile's colour space to XYZ (D50, as ICC
// profiles are), or to another RGB profile's values, with no clipping.
std::array<double, 3> transform(const std::vector<uint8_t>& from, cmsHPROFILE to, std::array<double, 3> rgb,
                                cmsUInt32Number to_format = TYPE_XYZ_DBL)
{
  cmsHPROFILE source = cmsOpenProfileFromMem(from.data(), static_cast<cmsUInt32Number>(from.size()));
  EXPECT_NE(source, nullptr);
  cmsHTRANSFORM t = cmsCreateTransform(source, TYPE_RGB_DBL, to, to_format, INTENT_RELATIVE_COLORIMETRIC,
                                       cmsFLAGS_NOCACHE | cmsFLAGS_NOOPTIMIZE);
  std::array<double, 3> out{};
  cmsDoTransform(t, rgb.data(), out.data(), 1);
  cmsDeleteTransform(t);
  cmsCloseProfile(source);
  return out;
}

TEST(IccProfile, IsLinearWithAWhiteWhite)
{
  cmsHPROFILE xyz = cmsCreateXYZProfile();
  for (ColorSpace space : { ColorSpace::REC2020, ColorSpace::PROPHOTO, ColorSpace::SRGB })
  {
    const auto profile = iccProfile(space);
    const auto white = transform(profile, xyz, { 1, 1, 1 });
    EXPECT_NEAR(white[0], 0.9642, 2e-3);
    EXPECT_NEAR(white[1], 1.0, 2e-3);
    EXPECT_NEAR(white[2], 0.8249, 2e-3);
    // Linear: a quarter of the light is a quarter of the luminance.
    EXPECT_NEAR(transform(profile, xyz, { 0.25, 0.25, 0.25 })[1], 0.25, 2e-3);
  }
  cmsCloseProfile(xyz);
}

TEST(IccProfile, LinearSrgbMatchesSrgb)
{
  // The same colour, as linear values in our profile and gamma-encoded in
  // Little CMS's own sRGB profile, lands on the same XYZ.
  cmsHPROFILE xyz = cmsCreateXYZProfile();
  cmsHPROFILE srgb = cmsCreate_sRGBProfile();
  const std::vector<uint8_t> ours = iccProfile(ColorSpace::SRGB);
  const std::array<double, 3> encoded = { 0.8, 0.5, 0.2 };
  std::array<double, 3> linear{};
  for (int c = 0; c < 3; ++c)
  {
    linear[c] = std::pow((encoded[c] + 0.055) / 1.055, 2.4);
  }
  cmsHTRANSFORM t =
      cmsCreateTransform(srgb, TYPE_RGB_DBL, xyz, TYPE_XYZ_DBL, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOCACHE);
  std::array<double, 3> expected{};
  cmsDoTransform(t, encoded.data(), expected.data(), 1);
  cmsDeleteTransform(t);
  const auto actual = transform(ours, xyz, linear);
  for (int c = 0; c < 3; ++c)
  {
    EXPECT_NEAR(actual[c], expected[c], 3e-3) << "component " << c;
  }
  cmsCloseProfile(srgb);
  cmsCloseProfile(xyz);
}

TEST(IccProfile, Rec2020IsWiderThanSrgb)
{
  // Rec. 2020's red is out of sRGB's gamut: more than full red, with negative
  // green and blue.
  const std::vector<uint8_t> srgb = iccProfile(ColorSpace::SRGB);
  cmsHPROFILE to = cmsOpenProfileFromMem(srgb.data(), static_cast<cmsUInt32Number>(srgb.size()));
  const auto red = transform(iccProfile(ColorSpace::REC2020), to, { 1, 0, 0 }, TYPE_RGB_DBL);
  EXPECT_GT(red[0], 1.5);
  EXPECT_LT(red[1], -0.05);
  EXPECT_LT(red[2], -0.01);
  cmsCloseProfile(to);
}

// A rendered image whose samples count up, so that any sample out of place
// shows.
RenderedImage countingImage(uint32_t width, uint32_t height)
{
  RenderedImage image;
  image.width = width;
  image.height = height;
  image.data.resize(static_cast<size_t>(width) * height * 3);
  for (size_t i = 0; i < image.data.size(); ++i)
  {
    image.data[i] = static_cast<uint16_t>(i * 251);
  }
  return image;
}

uint32_t readUInt(const tiff::TiffReader& reader, uint16_t tag)
{
  const auto entry = reader.find(reader.firstIfdOffset(), tag);
  EXPECT_TRUE(entry) << "tag " << tag;
  return entry ? reader.readUInt(*entry).value_or(0) : 0;
}

// Every value of an integer entry.
std::vector<uint32_t> readUInts(const std::vector<uint8_t>& file, const tiff::TiffReader::RawEntry& entry)
{
  std::vector<uint32_t> values;
  const size_t size = entry.type == tiff::SHORT ? 2 : 4;
  for (uint32_t i = 0; i < entry.count; ++i)
  {
    uint32_t v = 0;
    for (size_t b = 0; b < size; ++b)
    {
      v |= static_cast<uint32_t>(file[entry.value_offset + i * size + b]) << (8 * b);
    }
    values.push_back(v);
  }
  return values;
}

TEST(TiffEncoder, Writes16BitRgbWithItsProfile)
{
  // 70 rows: three strips of at most 32 rows.
  RenderedImage image = countingImage(5, 70);
  image.color_space = ColorSpace::PROPHOTO;
  image.orientation = 6;
  image.metadata.make = "Canon";
  image.metadata.model = "Canon EOS Test";
  image.metadata.exposure_time = 0.005f;
  TiffWriteOptions options;
  options.software = "raw-converter test";
  const std::vector<uint8_t> file = encodeTiff(image, options);

  auto reader = tiff::TiffReader::open(file.data(), file.size());
  ASSERT_TRUE(reader);
  EXPECT_EQ(readUInt(*reader, 256), 5u);   // ImageWidth.
  EXPECT_EQ(readUInt(*reader, 257), 70u);  // ImageLength.
  EXPECT_EQ(readUInt(*reader, 258), 16u);  // BitsPerSample.
  EXPECT_EQ(readUInt(*reader, 259), 1u);   // No compression.
  EXPECT_EQ(readUInt(*reader, 262), 2u);   // RGB.
  EXPECT_EQ(readUInt(*reader, 274), 6u);   // Orientation.
  EXPECT_EQ(readUInt(*reader, 277), 3u);   // SamplesPerPixel.
  EXPECT_EQ(readUInt(*reader, 284), 1u);   // Interleaved.

  const auto icc = reader->find(reader->firstIfdOffset(), 34675);
  ASSERT_TRUE(icc);
  const std::vector<uint8_t> profile(file.begin() + icc->value_offset,
                                     file.begin() + icc->value_offset + icc->value_size);
  EXPECT_EQ(profile, iccProfile(ColorSpace::PROPHOTO));

  // The samples, strip after strip, little-endian.
  const auto offsets = readUInts(file, *reader->find(reader->firstIfdOffset(), 273));
  const auto counts = readUInts(file, *reader->find(reader->firstIfdOffset(), 279));
  ASSERT_EQ(offsets.size(), 3u);
  std::vector<uint16_t> samples;
  for (size_t s = 0; s < offsets.size(); ++s)
  {
    for (uint32_t b = 0; b + 1 < counts[s]; b += 2)
    {
      samples.push_back(static_cast<uint16_t>(file[offsets[s] + b] | file[offsets[s] + b + 1] << 8));
    }
  }
  EXPECT_EQ(samples, image.data);

  // The EXIF: the camera's values, and no colour space but the profile's.
  const auto exif_pointer = reader->find(reader->firstIfdOffset(), 34665);
  ASSERT_TRUE(exif_pointer);
  const size_t exif = *reader->readUInt(*exif_pointer);
  EXPECT_TRUE(reader->find(exif, 0x829a));  // ExposureTime.
  const auto color_space = reader->find(exif, 0xa001);
  ASSERT_TRUE(color_space);
  EXPECT_EQ(*reader->readUInt(*color_space), 0xffffu);
}

TEST(TiffEncoder, KeepsTheCameraGpsPositionOnly)
{
  RenderedImage image = countingImage(4, 4);
  // Canon's GPS directory: its version, and nothing else.
  image.metadata.gps = { { 0, tiff::BYTE, 4, { 2, 2, 0, 0 } } };
  auto file = encodeTiff(image);
  auto reader = tiff::TiffReader::open(file.data(), file.size());
  EXPECT_FALSE(reader->find(reader->firstIfdOffset(), 34853));

  image.metadata.gps.push_back({ 1, tiff::ASCII, 2, { 'N', 0 } });  // GPSLatitudeRef.
  file = encodeTiff(image);
  reader = tiff::TiffReader::open(file.data(), file.size());
  EXPECT_TRUE(reader->find(reader->firstIfdOffset(), 34853));
}

TEST(TiffEncoder, RejectsAnInconsistentImage)
{
  RenderedImage image = countingImage(4, 4);
  image.data.pop_back();
  EXPECT_THROW(encodeTiff(image), Error);
  EXPECT_THROW(encodeTiff(RenderedImage{}), Error);
}

// The largest sample of each colour around a pixel of the rendering.
std::array<uint16_t, 3> maxAround(const RenderedImage& image, uint32_t row, uint32_t col, uint32_t radius)
{
  std::array<uint16_t, 3> max{};
  for (uint32_t r = row - radius; r <= row + radius; ++r)
  {
    for (uint32_t c = col - radius; c <= col + radius; ++c)
    {
      for (uint32_t s = 0; s < 3; ++s)
      {
        max[s] = std::max(max[s], image.at(r, c, s));
      }
    }
  }
  return max;
}

TEST(Renderer, RendersTheDefaultCropOfAGreySceneGrey)
{
  RawImage raw = makeCfaImage(64, 48, 1000, 0);
  raw.orientation = 8;
  raw.metadata.original_file_name = "grey.CR2";
  const RenderedImage image = renderImage(raw, ColorSpace::SRGB);
  EXPECT_EQ(image.width, 60u);
  EXPECT_EQ(image.height, 44u);
  EXPECT_EQ(image.color_space, ColorSpace::SRGB);
  EXPECT_EQ(image.orientation, 8);
  EXPECT_EQ(image.metadata.original_file_name, "grey.CR2");
  // White balanced as shot, the grey is grey; linear, 1000 above black of a
  // 15000 white is 1000 / 13976 of the full range.
  const double expected = 1000.0 / (15000 - 1024) * 65535;
  for (uint32_t s = 0; s < 3; ++s)
  {
    EXPECT_NEAR(image.at(22, 30, s), expected, expected * 0.02) << "sample " << s;
  }
}

TEST(Renderer, RepairsTheOpcodePixels)
{
  // A hot pixel, at (24, 32) of the image, (22, 30) of the crop.
  RawImage raw = makeCfaImage(64, 48, 1000, 0);
  raw.at(24, 32) = 15000;
  const auto hot = maxAround(renderImage(raw), 22, 30, 2);
  raw.opcode_bad_pixels = { { 24, 32 } };
  const auto repaired = maxAround(renderImage(raw), 22, 30, 2);
  const auto flat = maxAround(renderImage(makeCfaImage(64, 48, 1000, 0)), 22, 30, 2);
  EXPECT_GT(*std::max_element(hot.begin(), hot.end()), 2 * *std::max_element(flat.begin(), flat.end()));
  EXPECT_EQ(repaired, flat);
}

TEST(Renderer, RendersALinearRawImage)
{
  RawImage raw = makeCfaImage(64, 48, 1000, 0);
  raw.layout = RawLayout::LINEAR_RAW;
  raw.samples_per_pixel = 3;
  raw.black_level = { 1024, 1024, 1024, 0 };
  raw.data.assign(static_cast<size_t>(raw.width) * raw.height * 3, 0);
  for (uint32_t row = 0; row < raw.height; ++row)
  {
    for (uint32_t col = 0; col < raw.width; ++col)
    {
      for (uint32_t s = 0; s < 3; ++s)
      {
        raw.at(row, col, s) = static_cast<uint16_t>(1024 + 1000 * raw.as_shot_neutral[s]);
      }
    }
  }
  const RenderedImage image = renderImage(raw, ColorSpace::SRGB);
  EXPECT_EQ(image.width, 60u);
  EXPECT_EQ(image.height, 44u);
  EXPECT_NEAR(image.at(22, 30, 0), image.at(22, 30, 1), image.at(22, 30, 1) * 0.02);
  EXPECT_NEAR(image.at(22, 30, 2), image.at(22, 30, 1), image.at(22, 30, 1) * 0.02);
}

}  // namespace
}  // namespace rawconv::test
