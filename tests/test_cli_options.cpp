// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "cli_options.hpp"

namespace rawconv::cli
{
namespace
{

TEST(CommandLine, InputAndOutput)
{
  const auto options = parseCommandLine({ "photos", "dng" });
  EXPECT_EQ(options.input, "photos");
  EXPECT_EQ(options.output, "dng");
  EXPECT_FALSE(options.auto_bad_pixel_mode);
  EXPECT_EQ(options.convert.bad_pixels, BadPixelMode::OFF);
  EXPECT_FALSE(options.recursive);
  EXPECT_EQ(options.convert.dng.compression, Compression::LOSSLESS_JPEG);
  EXPECT_EQ(options.convert.repair, BadPixelRepair::INTERPOLATE);
  EXPECT_EQ(options.convert.format, OutputFormat::DNG);
  EXPECT_EQ(options.convert.tiff.color_space, ColorSpace::REC2020);
}

TEST(CommandLine, Options)
{
  const auto options =
      parseCommandLine({ "-r", "--jobs", "4", "--bad-pixels", "batch", "--repair", "opcode", "--defect-map", "map.txt",
                         "--uncompressed", "--no-preview", "--overwrite", "-q", "in", "out" });
  EXPECT_TRUE(options.recursive);
  EXPECT_EQ(options.convert.threads, 4u);
  EXPECT_FALSE(options.auto_bad_pixel_mode);
  EXPECT_EQ(options.convert.bad_pixels, BadPixelMode::BATCH);
  EXPECT_EQ(options.convert.repair, BadPixelRepair::OPCODE);
  EXPECT_EQ(options.defect_map, std::filesystem::path("map.txt"));
  EXPECT_EQ(options.convert.dng.compression, Compression::NONE);
  EXPECT_FALSE(options.convert.dng.embed_preview);
  EXPECT_TRUE(options.convert.overwrite);
  EXPECT_TRUE(options.quiet);
}

TEST(CommandLine, TiffOptions)
{
  EXPECT_EQ(parseCommandLine({ "--format", "tiff", "in", "out" }).convert.format, OutputFormat::TIFF);
  EXPECT_EQ(parseCommandLine({ "--format", "both", "in", "out" }).convert.format, OutputFormat::DNG_AND_TIFF);
  EXPECT_EQ(parseCommandLine({ "--format", "dng", "in", "out" }).convert.format, OutputFormat::DNG);
  EXPECT_EQ(parseCommandLine({ "--colour-space", "prophoto", "in", "out" }).convert.tiff.color_space,
            ColorSpace::PROPHOTO);
  EXPECT_EQ(parseCommandLine({ "--colour-space", "srgb", "in", "out" }).convert.tiff.color_space, ColorSpace::SRGB);
  EXPECT_EQ(parseCommandLine({ "--colour-space", "rec2020", "in", "out" }).convert.tiff.color_space,
            ColorSpace::REC2020);
  EXPECT_THROW(parseCommandLine({ "--format", "jpeg", "in", "out" }), std::invalid_argument);
  EXPECT_THROW(parseCommandLine({ "--colour-space", "adobe", "in", "out" }), std::invalid_argument);
}

TEST(CommandLine, HelpNeedsNoPaths)
{
  EXPECT_TRUE(parseCommandLine({ "--help" }).help);
  EXPECT_TRUE(parseCommandLine({ "--version" }).version);
}

TEST(CommandLine, Errors)
{
  EXPECT_THROW(parseCommandLine({}), std::invalid_argument);
  EXPECT_THROW(parseCommandLine({ "in" }), std::invalid_argument);
  EXPECT_THROW(parseCommandLine({ "in", "out", "extra" }), std::invalid_argument);
  EXPECT_THROW(parseCommandLine({ "--frobnicate", "in", "out" }), std::invalid_argument);
  EXPECT_THROW(parseCommandLine({ "--jobs", "many", "in", "out" }), std::invalid_argument);
  EXPECT_THROW(parseCommandLine({ "--jobs", "-1", "in", "out" }), std::invalid_argument);
  EXPECT_THROW(parseCommandLine({ "--bad-pixels", "some", "in", "out" }), std::invalid_argument);
  EXPECT_THROW(parseCommandLine({ "in", "out", "--repair" }), std::invalid_argument);
  // Repair is off by default, and a defect map without it would do nothing.
  EXPECT_THROW(parseCommandLine({ "--defect-map", "map.txt", "in", "out" }), std::invalid_argument);
  EXPECT_NO_THROW(parseCommandLine({ "--defect-map", "map.txt", "--bad-pixels", "auto", "in", "out" }));
}

TEST(CommandLine, AutomaticBadPixelMode)
{
  EXPECT_EQ(autoBadPixelMode(1), BadPixelMode::PER_IMAGE);
  EXPECT_EQ(autoBadPixelMode(2), BadPixelMode::PER_IMAGE);
  EXPECT_EQ(autoBadPixelMode(3), BadPixelMode::BATCH);
}

}  // namespace
}  // namespace rawconv::cli
