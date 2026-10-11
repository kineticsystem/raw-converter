// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// End to end, on real raw files: those of bin/fetch-samples.sh, and any other
// folder listed in RAWCONV_SAMPLE_DIRS. Skipped when there are none.

#include <gtest/gtest.h>

#include "rawconv/rawconv.hpp"
#include "libraw_readback.hpp"
#include "test_support.hpp"
#include "tiff/tiff_reader.hpp"

namespace rawconv::test
{
namespace
{

class Samples : public ::testing::TestWithParam<std::filesystem::path>
{
};

std::string uniqueCameraModel(const std::vector<uint8_t>& dng)
{
  auto reader = tiff::TiffReader::open(dng.data(), dng.size());
  auto entry = reader->find(reader->firstIfdOffset(), 50708);
  return std::string(reinterpret_cast<const char*>(dng.data() + entry->value_offset), entry->value_size - 1);
}

TEST_P(Samples, ConvertsLosslessly)
{
  const auto input = GetParam();
  TempDir dir;
  const auto output = dir.path() / "out.dng";

  ConvertOptions options;
  options.bad_pixels = BadPixelMode::PER_IMAGE;
  options.repair = BadPixelRepair::INTERPOLATE;
  Converter converter(options);
  const ConvertResult result = converter.convertFile(input, output);
  ASSERT_EQ(result.status, ConvertResult::Status::CONVERTED) << result.error;

  const auto dng_bytes = LocalFileStore().read(output);
  const Decoded original = readBack(LocalFileStore().read(input));
  const Decoded dng = readBack(dng_bytes);

  ASSERT_EQ(dng.width, original.width);
  ASSERT_EQ(dng.height, original.height);
  ASSERT_EQ(dng.samples, original.samples);
  size_t differences = 0;
  for (size_t i = 0; i < original.data.size(); ++i)
  {
    differences += dng.data[i] != original.data[i];
  }
  // Only the repaired pixels differ.
  EXPECT_EQ(differences, result.bad_pixels_fixed);
  if (original.samples == 1)
  {
    EXPECT_EQ(dng.cfa_colors, original.cfa_colors);
    for (int i = 0; i < 4; ++i)
    {
      EXPECT_NEAR(dng.black[i], original.black[i], 1) << "cell " << i;
    }
  }
  for (int c = 0; c < 3; ++c)
  {
    EXPECT_NEAR(dng.neutral_mul[c], original.neutral_mul[c], 1e-3);
  }

  // Raw editors find their camera profiles by UniqueCameraModel, and knows each
  // camera by one name only, whatever the market it was sold in.
  const std::string model = uniqueCameraModel(dng_bytes);
  const std::string name = input.filename().string();
  if (name.find("Rebel_XSi") != std::string::npos || name.find("450D") != std::string::npos)
  {
    EXPECT_EQ(model, "Canon EOS 450D");
  }
  if (name.find("5D_Mark_III") != std::string::npos)
  {
    EXPECT_EQ(model, "Canon EOS 5D Mark III");
  }
  else if (name.find("5D_Mark_II") != std::string::npos)
  {
    EXPECT_EQ(model, "Canon EOS 5D Mark II");
  }
}

TEST_P(Samples, KeepsTheWholeFrameUnchangedByDefault)
{
  // As Adobe DNG Converter: every sample of the sensor, masked border
  // included, as the camera recorded it.
  const auto input = GetParam();
  const auto original = LocalFileStore().read(input);
  if (readBack(original).samples != 1)
  {
    GTEST_SKIP() << "not a CFA image";
  }
  TempDir dir;
  Converter converter;
  const ConvertResult result = converter.convertFile(input, dir.path() / "out.dng");
  ASSERT_EQ(result.status, ConvertResult::Status::CONVERTED) << result.error;
  EXPECT_EQ(result.bad_pixels_fixed, 0u);
  const Frame raw = readFrame(original);
  const Frame dng = readFrame(LocalFileStore().read(dir.path() / "out.dng"));
  ASSERT_EQ(dng.width, raw.width);
  ASSERT_EQ(dng.height, raw.height);
  EXPECT_TRUE(dng.data == raw.data);
}

TEST_P(Samples, KeepsTheOriginalsOrientation)
{
  // As Adobe DNG Converter does: the orientation the camera wrote in the
  // original's first directory, not the physical one of Canon's maker note,
  // which can differ.
  const auto input = GetParam();
  const auto original = LocalFileStore().read(input);
  auto reader = tiff::TiffReader::open(original.data(), original.size());
  if (!reader)
  {
    GTEST_SKIP() << "not TIFF based";
  }
  auto entry = reader->find(reader->firstIfdOffset(), 274);
  if (!entry)
  {
    GTEST_SKIP() << "no orientation";
  }
  TempDir dir;
  Converter converter;
  const ConvertResult result = converter.convertFile(input, dir.path() / "out.dng");
  ASSERT_EQ(result.status, ConvertResult::Status::CONVERTED) << result.error;
  const auto dng = LocalFileStore().read(dir.path() / "out.dng");
  auto dng_reader = tiff::TiffReader::open(dng.data(), dng.size());
  EXPECT_EQ(dng_reader->readUInt(*dng_reader->find(dng_reader->firstIfdOffset(), 274)), reader->readUInt(*entry));
}

TEST_P(Samples, OpcodeRepairKeepsEveryValue)
{
  // The opcode route, which keeps every value: the data must then be exact.
  const auto input = GetParam();
  TempDir dir;
  ConvertOptions options;
  options.repair = BadPixelRepair::OPCODE;
  Converter converter(options);
  const ConvertResult result = converter.convertFile(input, dir.path() / "out.dng");
  ASSERT_EQ(result.status, ConvertResult::Status::CONVERTED) << result.error;
  const Decoded original = readBack(LocalFileStore().read(input));
  const Decoded dng = readBack(LocalFileStore().read(dir.path() / "out.dng"));
  EXPECT_EQ(dng.data, original.data);
}

TEST_P(Samples, RendersTheDefaultCropAsATiff)
{
  const auto input = GetParam();
  const RawImage raw = readRawFile(input);
  const RenderedImage image = renderImage(raw);
  // The camera's own size, e.g. 5616 x 3744 for the 5D Mark II.
  EXPECT_EQ(image.width, raw.default_crop.width);
  EXPECT_EQ(image.height, raw.default_crop.height);
  EXPECT_EQ(image.orientation, raw.orientation);

  // Linear, and not blank: a photo's mean is a few percent of the range, and
  // most of it is neither black nor clipped.
  double sum = 0;
  size_t black = 0;
  size_t clipped = 0;
  for (uint16_t v : image.data)
  {
    sum += v;
    black += v == 0;
    clipped += v == 65535;
  }
  const double mean = sum / image.data.size();
  EXPECT_GT(mean, 65535 * 0.01);
  EXPECT_LT(mean, 65535 * 0.5);
  EXPECT_LT(black, image.data.size() / 10);
  EXPECT_LT(clipped, image.data.size() / 10);

  const std::vector<uint8_t> file = encodeTiff(image);
  auto reader = tiff::TiffReader::open(file.data(), file.size());
  ASSERT_TRUE(reader);
  EXPECT_TRUE(reader->find(reader->firstIfdOffset(), 34675));  // The ICC profile.
}

std::string sampleName(const ::testing::TestParamInfo<std::filesystem::path>& info)
{
  std::string name = info.param.stem().string();
  for (auto& c : name)
  {
    if (!std::isalnum(static_cast<unsigned char>(c)))
    {
      c = '_';
    }
  }
  return name + "_" + std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(RawFiles, Samples, ::testing::ValuesIn(sampleFiles()), sampleName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(Samples);

}  // namespace
}  // namespace rawconv::test
