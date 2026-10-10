// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <sstream>

#include "dngconv/bad_pixels.hpp"
#include "test_support.hpp"

namespace dngconv::test
{
namespace
{

bool contains(const std::vector<PixelPos>& pixels, PixelPos p)
{
  return std::find(pixels.begin(), pixels.end(), p) != pixels.end();
}

// A bright point of light as a lens and an anti-aliasing filter draw it: a
// small Gaussian spot, which lights the neighbours of its centre too.
void addStar(RawImage& image, uint32_t row, uint32_t col, double peak, double radius)
{
  for (int dy = -4; dy <= 4; ++dy)
  {
    for (int dx = -4; dx <= 4; ++dx)
    {
      const double v = peak * std::exp(-(dx * dx + dy * dy) / (2 * radius * radius));
      uint16_t& px = image.at(row + dy, col + dx);
      px = static_cast<uint16_t>(std::min(65535.0, px + v));
    }
  }
}

TEST(BadPixels, CleanImageHasNone)
{
  for (uint32_t seed = 1; seed <= 3; ++seed)
  {
    const auto found = detectBadPixels(makeCfaImage(400, 300, 500, 20, seed));
    EXPECT_TRUE(found.confident.empty()) << "seed " << seed;
    EXPECT_LE(found.suspect.size(), 2u) << "seed " << seed;
  }
}

TEST(BadPixels, FindsHotStuckAndDeadPixels)
{
  RawImage image = makeCfaImage(400, 300, 800, 15);
  const PixelPos hot{ 100, 101 };   // Well above the noise.
  const PixelPos stuck{ 50, 60 };   // At the white level.
  const PixelPos dead{ 200, 200 };  // At black.
  image.at(hot.row, hot.col) = 1024 + 6000;
  image.at(stuck.row, stuck.col) = 15000;
  image.at(dead.row, dead.col) = 1024;
  const auto found = detectBadPixels(image);
  EXPECT_TRUE(contains(found.confident, hot));
  EXPECT_TRUE(contains(found.confident, stuck));
  EXPECT_TRUE(contains(found.confident, dead));
  EXPECT_EQ(found.confident.size(), 3u);
}

TEST(BadPixels, FaintHotPixelsInTheDarkAreSuspects)
{
  // A warm pixel shows in the shadows; on a bright background the same
  // excess would be invisible, and is not reported.
  RawImage image = makeCfaImage(400, 300, 100, 15);
  const PixelPos warm{ 150, 151 };
  image.at(warm.row, warm.col) = 1024 + 100 + 15 * 12;
  const auto found = detectBadPixels(image);
  EXPECT_TRUE(contains(found.suspect, warm));
  EXPECT_TRUE(found.confident.empty());
}

TEST(BadPixels, LeavesStarsAndHighlightsAlone)
{
  RawImage image = makeCfaImage(400, 300, 50, 8);
  addStar(image, 100, 100, 12000, 0.8);  // Sharp, nearly a single pixel.
  addStar(image, 200, 250, 3000, 1.2);
  addStar(image, 60, 300, 14000, 0.7);
  const auto found = detectBadPixels(image);
  EXPECT_TRUE(found.confident.empty());
  EXPECT_TRUE(found.suspect.empty());
}

TEST(BadPixels, IgnoresTheEdgesAndLinearRaw)
{
  RawImage image = makeCfaImage(64, 64, 800, 15);
  image.at(0, 0) = 15000;
  image.at(63, 10) = 15000;
  EXPECT_TRUE(detectBadPixels(image).confident.empty());

  RawImage linear = makeRandomImage(RawLayout::LINEAR_RAW, 64, 64);
  EXPECT_TRUE(detectBadPixels(linear).confident.empty());
}

TEST(BadPixels, RepairUsesSameColourNeighbours)
{
  RawImage image = makeCfaImage(64, 64, 1000, 0);
  // Make the red pixels (GBRG: odd rows, even columns) distinct from the rest.
  for (uint32_t row = 1; row < 64; row += 2)
  {
    for (uint32_t col = 0; col < 64; col += 2)
    {
      image.at(row, col) = 3000;
    }
  }
  image.at(21, 20) = 15000;  // A hot red pixel...
  image.at(21, 22) = 15000;  // ...next to another one.
  image.at(30, 30) = 0;      // A dead green pixel.
  repairBadPixels(image, { { 21, 20 }, { 21, 22 }, { 30, 30 } });
  EXPECT_EQ(image.at(21, 20), 3000);
  EXPECT_EQ(image.at(21, 22), 3000);  // Not polluted by the other bad pixel.
  EXPECT_EQ(image.at(30, 30), 2024);
}

// Signatures of scenes that have nothing in common.
SceneSignature scene(int n)
{
  SceneSignature signature;
  for (size_t i = 0; i < signature.stops.size(); ++i)
  {
    signature.stops[i] = static_cast<float>((i * (n + 3) + n) % 7) - 3;
  }
  return signature;
}

TEST(DefectMap, ConfirmsPixelsSeenInEnoughScenes)
{
  DefectMap map;
  const std::string camera = "Canon|1|64x64";
  map.addShot(camera, { {}, { { 1, 1 }, { 2, 2 } } }, scene(0));
  map.addShot(camera, { { { 1, 1 } }, {} }, scene(1));
  map.addShot(camera, { {}, { { 3, 3 } } }, scene(2));
  map.addShot("other", { {}, { { 9, 9 } } }, scene(0));
  EXPECT_EQ(map.shots(camera), 3u);
  EXPECT_EQ(map.scenes(camera), 3u);
  EXPECT_EQ(map.confirmed(camera, 2, 0.25), (std::vector<PixelPos>{ { 1, 1 } }));
  // With a higher fraction, two scenes of three are not enough.
  EXPECT_TRUE(map.confirmed(camera, 2, 0.9).empty());
  EXPECT_TRUE(map.confirmed("unknown").empty());
}

TEST(DefectMap, RepeatedSceneIsNotConfirmation)
{
  // A tripod bracket or a time-lapse: the same bright detail in the same
  // place in every shot, and nowhere else. It is not a sensor defect.
  DefectMap map;
  const std::string camera = "Canon|1|64x64";
  for (int i = 0; i < 5; ++i)
  {
    SceneSignature same = scene(4);
    same.stops[3] += 0.1f * i;  // Small changes between the shots.
    map.addShot(camera, { {}, { { 5, 5 } } }, same);
  }
  EXPECT_EQ(map.shots(camera), 5u);
  EXPECT_EQ(map.scenes(camera), 1u);
  EXPECT_TRUE(map.confirmed(camera).empty());
}

TEST(DefectMap, SceneSignatureIgnoresExposure)
{
  RawImage image = makeCfaImage(64, 64, 200, 0);
  for (uint32_t row = 0; row < 32; ++row)
  {
    for (uint32_t col = 0; col < 32; ++col)
    {
      image.at(row, col) = 1024 + 2000;
    }
  }
  RawImage brighter = image;  // Two stops more exposure.
  for (auto& v : brighter.data)
  {
    v = static_cast<uint16_t>(1024 + (v - 1024) * 4);
  }
  const auto a = sceneSignature(image);
  const auto b = sceneSignature(brighter);
  for (size_t i = 0; i < a.stops.size(); ++i)
  {
    EXPECT_NEAR(a.stops[i], b.stops[i], 0.05);
  }
}

TEST(DefectMap, WritesAndReads)
{
  DefectMap map;
  const std::string camera = "Canon EOS 5D Mark II|330118337|5634x3752";
  map.addShot(camera, { {}, { { 10, 20 } } }, scene(0));
  map.addShot(camera, { {}, { { 10, 20 } } }, scene(1));
  std::stringstream saved;
  map.write(saved);

  DefectMap loaded;
  loaded.read(saved);
  EXPECT_EQ(loaded.confirmed(camera), (std::vector<PixelPos>{ { 10, 20 } }));

  std::stringstream windows("camera A|1|2x2\r\n3 4\r\n");
  loaded.read(windows);
  EXPECT_EQ(loaded.confirmed("A|1|2x2"), (std::vector<PixelPos>{ { 3, 4 } }));

  for (const char* bad : { "12 13\n", "camera X\n12\n", "camera X\n1 2 3\n" })
  {
    std::stringstream in(bad);
    EXPECT_THROW(loaded.read(in), Error) << bad;
  }
}

TEST(DefectMap, CameraKeyTellsBodiesApart)
{
  RawImage a = makeCfaImage(32, 32);
  RawImage b = a;
  b.metadata.serial_number = "43";
  RawImage c = a;
  c.width = 64;
  EXPECT_NE(DefectMap::cameraKey(a), DefectMap::cameraKey(b));
  EXPECT_NE(DefectMap::cameraKey(a), DefectMap::cameraKey(c));
}

}  // namespace
}  // namespace dngconv::test
