// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// The converter's DNG of a raw file, compared with the DNG Adobe DNG
// Converter made of the same file: samples/Canon_EOS_5D_Mark_III.CR2 and
// samples/adobe/Canon_EOS_5D_Mark_III.dng, from bin/fetch-samples.sh.
// Skipped when they are not there.

#include <gtest/gtest.h>

#include "dng_tags.hpp"
#include "rawconv/rawconv.hpp"
#include "libraw_readback.hpp"
#include "test_support.hpp"

namespace rawconv::test
{
namespace
{

enum Tag : uint16_t
{
  IMAGE_WIDTH = 256,
  IMAGE_LENGTH = 257,
  ORIENTATION = 274,
  CFA_PATTERN = 33422,
  UNIQUE_CAMERA_MODEL = 50708,
  BLACK_LEVEL = 50714,
  WHITE_LEVEL = 50717,
  DEFAULT_CROP_ORIGIN = 50719,
  DEFAULT_CROP_SIZE = 50720,
  COLOR_MATRIX_1 = 50721,
  COLOR_MATRIX_2 = 50722,
  CALIBRATION_ILLUMINANT_2 = 50779,
  AS_SHOT_NEUTRAL = 50728,
  BASELINE_EXPOSURE = 50730,
  BASELINE_NOISE = 50731,
  BASELINE_SHARPNESS = 50732,
  BAYER_GREEN_SPLIT = 50733,
  ACTIVE_AREA = 50829,
};

class AdobeReference : public ::testing::Test
{
protected:
  void SetUp() override
  {
    const std::filesystem::path samples = RAWCONV_SOURCE_DIR "/samples";
    raw_ = samples / "Canon_EOS_5D_Mark_III.CR2";
    adobe_path_ = samples / "adobe" / "Canon_EOS_5D_Mark_III.dng";
    if (!std::filesystem::exists(raw_) || !std::filesystem::exists(adobe_path_))
    {
      GTEST_SKIP() << "Run bin/fetch-samples.sh for the Adobe reference files";
    }
    adobe_ = LocalFileStore().read(adobe_path_);
    Converter converter;  // Defaults: as Adobe, no bad pixel repaired.
    const auto result = converter.convertFile(raw_, dir_.path() / "ours.dng");
    ASSERT_EQ(result.status, ConvertResult::Status::CONVERTED) << result.error;
    ours_ = LocalFileStore().read(dir_.path() / "ours.dng");
  }

  TempDir dir_;
  std::filesystem::path raw_, adobe_path_;
  std::vector<uint8_t> adobe_, ours_;
};

TEST_F(AdobeReference, SameFrame)
{
  // Both keep the whole frame of the sensor, masked border included, and mark
  // the image in it with ActiveArea.
  const DngTags adobe(adobe_);
  const DngTags ours(ours_);
  for (uint16_t tag : { IMAGE_WIDTH, IMAGE_LENGTH, ACTIVE_AREA })
  {
    EXPECT_EQ(ours.values(ours.sub_ifd, tag), adobe.values(adobe.sub_ifd, tag)) << "tag " << tag;
  }
  // Every sample, bit for bit, and those of the raw file.
  const Frame adobe_frame = readFrame(adobe_);
  const Frame ours_frame = readFrame(ours_);
  ASSERT_EQ(ours_frame.width, adobe_frame.width);
  ASSERT_EQ(ours_frame.height, adobe_frame.height);
  EXPECT_TRUE(ours_frame.data == adobe_frame.data);
  EXPECT_TRUE(ours_frame.data == readFrame(LocalFileStore().read(raw_)).data);
}

TEST_F(AdobeReference, SameRawData)
{
  // The image, as LibRaw reads it from each file.
  const Decoded adobe = readBack(adobe_);
  const Decoded ours = readBack(ours_);
  ASSERT_EQ(ours.width, adobe.width);
  ASSERT_EQ(ours.height, adobe.height);
  EXPECT_EQ(ours.data, adobe.data);
  EXPECT_EQ(ours.cfa_colors, adobe.cfa_colors);
  for (int i = 0; i < 4; ++i)
  {
    // Both measure the black level on the masked border; they may round it
    // differently.
    EXPECT_NEAR(ours.black[i], adobe.black[i], 1) << "cell " << i;
  }
  // Adobe uses a fixed 15000; ours is Canon's own clipping level at the
  // shot's ISO, from the maker notes.
  EXPECT_NEAR(ours.white, adobe.white, 0.03 * adobe.white);
}

TEST_F(AdobeReference, SameRenderingTags)
{
  const DngTags adobe(adobe_);
  const DngTags ours(ours_);

  EXPECT_EQ(ours.ascii(ours.ifd0(), UNIQUE_CAMERA_MODEL), adobe.ascii(adobe.ifd0(), UNIQUE_CAMERA_MODEL));
  EXPECT_EQ(ours.values(ours.ifd0(), ORIENTATION), adobe.values(adobe.ifd0(), ORIENTATION));
  EXPECT_EQ(ours.values(ours.sub_ifd, CFA_PATTERN), adobe.values(adobe.sub_ifd, CFA_PATTERN));

  // The crop of the final image, relative to the active area in both.
  EXPECT_EQ(ours.numbers(ours.sub_ifd, DEFAULT_CROP_ORIGIN), adobe.numbers(adobe.sub_ifd, DEFAULT_CROP_ORIGIN));
  EXPECT_EQ(ours.numbers(ours.sub_ifd, DEFAULT_CROP_SIZE), adobe.numbers(adobe.sub_ifd, DEFAULT_CROP_SIZE));

  // White balance, to the precision of the rationals.
  const auto neutral = ours.numbers(ours.ifd0(), AS_SHOT_NEUTRAL);
  const auto adobe_neutral = adobe.numbers(adobe.ifd0(), AS_SHOT_NEUTRAL);
  ASSERT_EQ(neutral.size(), 3u);
  for (int c = 0; c < 3; ++c)
  {
    EXPECT_NEAR(neutral[c], adobe_neutral[c], 1e-4);
  }

  // Our only colour matrix is Adobe's D65 one: Adobe's second, as its first
  // is for standard light A.
  ASSERT_EQ(adobe.values(adobe.ifd0(), CALIBRATION_ILLUMINANT_2), (std::vector<uint32_t>{ 21 }));
  const auto matrix = ours.numbers(ours.ifd0(), COLOR_MATRIX_1);
  const auto adobe_matrix = adobe.numbers(adobe.ifd0(), COLOR_MATRIX_2);
  ASSERT_EQ(matrix.size(), 9u);
  for (int i = 0; i < 9; ++i)
  {
    EXPECT_NEAR(matrix[i], adobe_matrix[i], 1e-4) << "element " << i;
  }

  // From the camera table: the photo opens as bright, as sharp and as noisy.
  for (uint16_t tag : { BASELINE_EXPOSURE, BASELINE_NOISE, BASELINE_SHARPNESS })
  {
    EXPECT_EQ(ours.numbers(ours.ifd0(), tag), adobe.numbers(adobe.ifd0(), tag)) << "tag " << tag;
  }
  EXPECT_EQ(ours.values(ours.sub_ifd, BAYER_GREEN_SPLIT), adobe.values(adobe.sub_ifd, BAYER_GREEN_SPLIT));
}

}  // namespace
}  // namespace rawconv::test
