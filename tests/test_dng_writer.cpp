// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <tuple>

#include "rawconv/dng_writer.hpp"
#include "dng_tags.hpp"
#include "libraw_readback.hpp"
#include "test_support.hpp"
#include "tiff/tiff_reader.hpp"
#include "tiff/tiff_writer.hpp"

namespace rawconv::test
{
namespace
{

// Lossless means lossless: what LibRaw decodes from the DNG is exactly what
// was written, for every layout, size and compression.
class RoundTrip : public ::testing::TestWithParam<std::tuple<RawLayout, uint32_t, uint32_t, Compression>>
{
};

TEST_P(RoundTrip, LibRawReadsBackTheExactSamples)
{
  const auto [layout, width, height, compression] = GetParam();
  const RawImage image = makeRandomImage(layout, width, height);
  DngWriteOptions options;
  options.compression = compression;
  options.threads = 3;
  const Decoded decoded = readBack(encodeDng(image, options));

  ASSERT_EQ(decoded.width, width);
  ASSERT_EQ(decoded.height, height);
  ASSERT_EQ(decoded.data.size(), image.data.size());
  size_t differences = 0;
  for (size_t i = 0; i < image.data.size(); ++i)
  {
    differences += decoded.data[i] != image.data[i];
  }
  EXPECT_EQ(differences, 0u);
}

// Images are at least two tiles (512 pixels) wide, and not exactly that: the
// DNG reader of LibRaw 0.21 misplaces the samples of narrower images, whose
// one tile is wider than the image (src/decoders/dng.cpp:111 compares a
// column within the tile with the image width), and of images exactly 512
// wide (dng.cpp:100, a case for other cameras' files). The files are valid;
// no camera makes images that small.
INSTANTIATE_TEST_SUITE_P(LayoutsAndSizes, RoundTrip,
                         ::testing::Values(
                             // Tile aligned, ragged edges, less than a tile high, odd sizes.
                             std::make_tuple(RawLayout::CFA, 768u, 512u, Compression::LOSSLESS_JPEG),
                             std::make_tuple(RawLayout::CFA, 5634u / 8 * 2, 3752u / 8, Compression::LOSSLESS_JPEG),
                             std::make_tuple(RawLayout::CFA, 600u, 60u, Compression::LOSSLESS_JPEG),
                             std::make_tuple(RawLayout::CFA, 601u, 257u, Compression::LOSSLESS_JPEG),
                             std::make_tuple(RawLayout::CFA, 601u, 257u, Compression::NONE),
                             std::make_tuple(RawLayout::LINEAR_RAW, 600u, 200u, Compression::LOSSLESS_JPEG),
                             std::make_tuple(RawLayout::LINEAR_RAW, 600u, 200u, Compression::NONE)));

TEST(DngWriter, SmoothImageCompressesWell)
{
  const RawImage image = makeCfaImage(1024, 512, 2000, 5);
  const auto compressed = encodeDng(image);
  DngWriteOptions options;
  options.compression = Compression::NONE;
  const auto uncompressed = encodeDng(image, options);
  EXPECT_LT(compressed.size() * 2, uncompressed.size());
}

TEST(DngWriter, WritesTheTagsRawEditorsNeed)
{
  RawImage image = makeCfaImage(300, 200);
  image.black_level = { 1023, 1024, 1025, 1026 };
  image.white_level = 15312;
  image.default_crop = { 4, 10, 190, 280 };
  image.orientation = 6;
  image.metadata.date_time = "2009:09:02 14:51:56";
  image.metadata.original_file_name = "IMG_0001.CR2";
  const auto bytes = encodeDng(image);
  const DngTags tags(bytes);

  EXPECT_EQ(tags.values(tags.ifd0(), 50706), (std::vector<uint32_t>{ 1, 4, 0, 0 }));   // DNGVersion
  EXPECT_EQ(tags.values(tags.ifd0(), 50707), (std::vector<uint32_t>{ 1, 1, 0, 0 }));   // DNGBackwardVersion
  EXPECT_EQ(tags.ascii(tags.ifd0(), 50708), "Canon EOS Test");                         // UniqueCameraModel
  EXPECT_EQ(tags.values(tags.ifd0(), 274), (std::vector<uint32_t>{ 6 }));              // Orientation
  EXPECT_EQ(tags.values(tags.ifd0(), 50778), (std::vector<uint32_t>{ 21 }));           // D65
  EXPECT_EQ(tags.ascii(tags.ifd0(), 50827), "IMG_0001.CR2");                           // OriginalRawFileName
  EXPECT_EQ(tags.ascii(tags.ifd0(), 306), "2009:09:02 14:51:56");                      // DateTime
  EXPECT_EQ(tags.values(tags.sub_ifd, 262), (std::vector<uint32_t>{ 32803 }));         // CFA
  EXPECT_EQ(tags.values(tags.sub_ifd, 33422), (std::vector<uint32_t>{ 1, 2, 0, 1 }));  // GBRG
  EXPECT_EQ(tags.values(tags.sub_ifd, 50717), (std::vector<uint32_t>{ 15312 }));       // WhiteLevel
  EXPECT_EQ(tags.values(tags.sub_ifd, 50719), (std::vector<uint32_t>{ 10, 4 }));       // DefaultCropOrigin
  EXPECT_EQ(tags.values(tags.sub_ifd, 50720), (std::vector<uint32_t>{ 280, 190 }));    // DefaultCropSize
  // BlackLevel, as rationals over 100.
  EXPECT_EQ(tags.values(tags.sub_ifd, 50714),
            (std::vector<uint32_t>{ 102300, 100, 102400, 100, 102500, 100, 102600, 100 }));

  const Decoded decoded = readBack(bytes);
  EXPECT_EQ(decoded.cfa_colors, (std::array<int, 4>{ 1, 2, 0, 1 }));
  EXPECT_EQ(decoded.black, (std::array<unsigned, 4>{ 1023, 1024, 1025, 1026 }));
  EXPECT_EQ(decoded.white, 15312u);
  EXPECT_NEAR(decoded.neutral_mul[0], 1 / 0.5, 1e-3);
  EXPECT_NEAR(decoded.neutral_mul[2], 1 / 0.6, 1e-3);
}

// An image in a sensor frame whose active area starts on an odd row, as the
// 5D Mark II's does in Adobe's DNGs.
RawImage makeFramedImage()
{
  RawImage image = makeCfaImage(300, 200);
  image.sensor_width = 340;
  image.sensor_height = 230;
  image.active_area = { 13, 30, 200, 300 };
  image.sensor.resize(340 * 230);
  for (size_t i = 0; i < image.sensor.size(); ++i)
  {
    image.sensor[i] = static_cast<uint16_t>(1000 + i % 97);
  }
  for (uint32_t row = 0; row < 200; ++row)
  {
    for (uint32_t col = 0; col < 300; ++col)
    {
      // Not written: the image's own samples are.
      image.sensor[(row + 13) * 340 + col + 30] = 9999;
    }
  }
  return image;
}

TEST(DngWriter, StoresTheWholeSensorFrame)
{
  const RawImage image = makeFramedImage();
  const auto bytes = encodeDng(image);
  const DngTags tags(bytes);
  EXPECT_EQ(tags.values(tags.sub_ifd, 256), (std::vector<uint32_t>{ 340 }));                 // ImageWidth
  EXPECT_EQ(tags.values(tags.sub_ifd, 257), (std::vector<uint32_t>{ 230 }));                 // ImageLength
  EXPECT_EQ(tags.values(tags.sub_ifd, 50829), (std::vector<uint32_t>{ 13, 30, 213, 330 }));  // ActiveArea
  // Counted from the active area, as Adobe's are.
  EXPECT_EQ(tags.values(tags.sub_ifd, 33422), (std::vector<uint32_t>{ 1, 2, 0, 1 }));  // CFAPattern
  EXPECT_EQ(tags.values(tags.sub_ifd, 50719), (std::vector<uint32_t>{ 2, 2 }));        // DefaultCropOrigin

  // The frame, bit for bit, with the image in its active area.
  const Frame frame = readFrame(bytes);
  ASSERT_EQ(frame.width, 340u);
  ASSERT_EQ(frame.height, 230u);
  size_t differences = 0;
  for (uint32_t row = 0; row < 230; ++row)
  {
    for (uint32_t col = 0; col < 340; ++col)
    {
      const bool active = row >= 13 && row < 213 && col >= 30 && col < 330;
      const uint16_t expected = active ? image.at(row - 13, col - 30) : image.sensor[row * 340 + col];
      differences += frame.data[row * 340 + col] != expected;
    }
  }
  EXPECT_EQ(differences, 0u);

  // And LibRaw finds the image where ActiveArea says, but for one detail of
  // its own: it starts an active area that begins on an odd row one row
  // lower, as it does with Adobe's DNGs of the 5D Mark II.
  const Decoded decoded = readBack(bytes);
  ASSERT_EQ(decoded.width, 300u);
  ASSERT_EQ(decoded.height, 199u);
  EXPECT_TRUE(std::equal(decoded.data.begin(), decoded.data.end(), image.data.begin() + 300));
  EXPECT_EQ(decoded.cfa_colors, (std::array<int, 4>{ 0, 1, 1, 2 }));  // From row 14.
}

TEST(DngWriter, RejectsAFrameThatDoesNotFitTheImage)
{
  RawImage image = makeFramedImage();
  image.active_area.top = 31;  // 31 + 200 > 230.
  EXPECT_THROW(encodeDng(image), Error);
}

TEST(DngWriter, BadPixelOpcodeCountsFromTheFrame)
{
  // OpcodeList1 applies to the stored image, before ActiveArea: its pixels
  // and its phase are the frame's.
  RawImage image = makeFramedImage();
  image.opcode_bad_pixels = { { 3, 4 } };
  const auto bytes = encodeDng(image);
  const DngTags tags(bytes);
  const auto entry = tags.reader.find(tags.sub_ifd, 51008);
  ASSERT_TRUE(entry.has_value());
  const uint8_t* p = bytes.data() + entry->value_offset;
  auto be32 = [&](size_t i) {
    return uint32_t(p[i]) << 24 | uint32_t(p[i + 1]) << 16 | uint32_t(p[i + 2]) << 8 | p[i + 3];
  };
  // GBRG from row 13: the frame's row 0 is a red row, so it starts with red.
  EXPECT_EQ(be32(20), 0u);
  EXPECT_EQ(be32(24), 1u);
  EXPECT_EQ(be32(32), 16u);
  EXPECT_EQ(be32(36), 34u);
}

TEST(DngWriter, BadPixelOpcodeNeedsDng13Readers)
{
  RawImage image = makeCfaImage(64, 64);
  // Duplicates are removed, and pixels outside the image left out.
  image.opcode_bad_pixels = { { 10, 11 }, { 3, 4 }, { 10, 11 }, { 64, 0 } };
  const auto bytes = encodeDng(image);
  const DngTags tags(bytes);
  EXPECT_EQ(tags.values(tags.ifd0(), 50707), (std::vector<uint32_t>{ 1, 3, 0, 0 }));

  // OpcodeList1 is big-endian: count, then FixBadPixelsList (5), version
  // 1.3.0.0, flags, size, phase, 2 points (duplicates removed), 0 rects,
  // the points sorted.
  const auto entry = tags.reader.find(tags.sub_ifd, 51008);
  ASSERT_TRUE(entry.has_value());
  const uint8_t* p = bytes.data() + entry->value_offset;
  auto be32 = [&](size_t i) {
    return uint32_t(p[i]) << 24 | uint32_t(p[i + 1]) << 16 | uint32_t(p[i + 2]) << 8 | p[i + 3];
  };
  EXPECT_EQ(be32(0), 1u);
  EXPECT_EQ(be32(4), 5u);
  EXPECT_EQ(be32(8), 0x01030000u);
  EXPECT_EQ(be32(12), 1u);  // Optional: readers without opcodes still open the file.
  EXPECT_EQ(be32(16), 12u + 2 * 8);
  EXPECT_EQ(be32(20), 2u);  // GBRG: green on a blue row.
  EXPECT_EQ(be32(24), 2u);
  EXPECT_EQ(be32(28), 0u);
  EXPECT_EQ(be32(32), 3u);
  EXPECT_EQ(be32(36), 4u);
  EXPECT_EQ(be32(40), 10u);
  EXPECT_EQ(be32(44), 11u);
  EXPECT_EQ(entry->value_size, 48u);
}

TEST(DngWriter, CopiesExifAndAddsLensFields)
{
  RawImage image = makeCfaImage(64, 64);
  image.metadata.exif.push_back({ 0x8827, tiff::SHORT, 1, { 0x20, 0x03 } });  // ISO 800.
  image.metadata.lens_model = "EF24-70mm f/2.8L USM";
  image.metadata.lens_info = { 24, 70, 2.8f, 2.8f };
  const auto bytes = encodeDng(image);
  const DngTags tags(bytes);
  const size_t exif = tags.values(tags.ifd0(), 34665).at(0);
  EXPECT_EQ(tags.values(exif, 0x8827), (std::vector<uint32_t>{ 800 }));
  EXPECT_EQ(tags.ascii(exif, 0xa434), "EF24-70mm f/2.8L USM");
  EXPECT_EQ(tags.values(exif, 0xa432), (std::vector<uint32_t>{ 24000, 1000, 70000, 1000, 2800, 1000, 2800, 1000 }));
  EXPECT_EQ(tags.values(tags.ifd0(), 50736).size(), 8u);  // LensInfo.
}

TEST(DngWriter, RejectsInconsistentImages)
{
  RawImage image = makeCfaImage(64, 64);
  image.data.pop_back();
  EXPECT_THROW(encodeDng(image), Error);
  image = makeCfaImage(64, 64);
  image.samples_per_pixel = 3;
  EXPECT_THROW(encodeDng(image), Error);
}

TEST(DngWriter, WriteDngReplacesAtomically)
{
  TempDir dir;
  const auto path = dir.path() / "sub" / "out.dng";
  writeDng(makeCfaImage(64, 64), path);
  writeDng(makeCfaImage(32, 32), path);
  EXPECT_EQ(readBack(LocalFileStore().read(path)).width, 32u);
  size_t files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(path.parent_path()))
  {
    (void)entry;
    ++files;
  }
  EXPECT_EQ(files, 1u);  // No temporary file left behind.
}

}  // namespace
}  // namespace rawconv::test
