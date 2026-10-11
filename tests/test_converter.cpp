// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// The converter's logic, with every service replaced by a fake: no LibRaw,
// no files, no DNG encoding.

#include <gtest/gtest.h>

#include <fstream>
#include <set>

#include "rawconv/converter.hpp"
#include "test_support.hpp"

namespace rawconv::test
{
namespace
{

using Status = ConvertResult::Status;

struct Fixture : public ::testing::Test
{
  std::shared_ptr<MemoryFileStore> files = std::make_shared<MemoryFileStore>();
  std::shared_ptr<FakeDetector> detector = std::make_shared<FakeDetector>();
  std::shared_ptr<CapturingEncoder> encoder = std::make_shared<CapturingEncoder>();

  Converter make(ConvertOptions options)
  {
    return Converter(options, { std::make_shared<FakeDecoder>(), detector, encoder, files });
  }

  // `n` raw files of one camera, named in/0.CR2 ...
  std::vector<ConvertJob> shots(int n, const std::string& serial = "A")
  {
    std::vector<ConvertJob> jobs;
    for (int i = 0; i < n; ++i)
    {
      const std::string name = serial + std::to_string(i) + ".CR2";
      files->put("in/" + name, serial);
      jobs.push_back({ "in/" + name, "out/" + serial + std::to_string(i) + ".dng" });
    }
    return jobs;
  }
};

TEST_F(Fixture, PerImageRepairsConfidentPixelsOnly)
{
  ConvertOptions options;
  options.bad_pixels = BadPixelMode::PER_IMAGE;
  options.repair = BadPixelRepair::INTERPOLATE;
  auto jobs = shots(3);
  for (int i = 0; i < 3; ++i)
  {
    detector->by_file["A" + std::to_string(i) + ".CR2"] = { { { 5, 5 } }, { { 7, 7 } } };
  }
  auto converter = make(options);
  converter.defectMap();  // Nothing learnt: PER_IMAGE does not record.
  const auto results = converter.convert(jobs);
  for (const auto& r : results)
  {
    EXPECT_EQ(r.status, Status::CONVERTED) << r.error;
    EXPECT_EQ(r.bad_pixels_fixed, 1u);
  }
  const RawImage& written = encoder->call("A0.CR2").image;
  EXPECT_EQ(written.at(5, 5), 2024);  // Repaired: the flat neighbours' value.
  EXPECT_TRUE(written.opcode_bad_pixels.empty());
  EXPECT_TRUE(files->exists("out/A0.dng"));
}

TEST_F(Fixture, BatchConfirmsSuspectsAcrossScenesOfTheSameCamera)
{
  ConvertOptions options;
  options.bad_pixels = BadPixelMode::BATCH;
  options.batch_min_scenes = 2;
  options.batch_min_fraction = 0.5;
  auto jobs = shots(4, "A");
  const auto other = shots(2, "B");
  jobs.insert(jobs.end(), other.begin(), other.end());
  // (9, 9) is suspect in shots of 3 of the 4 scenes of camera A: a defect of
  // that camera, repaired in all four. (11, 11) only once: never repaired.
  detector->by_file["A0.CR2"] = { {}, { { 9, 9 }, { 11, 11 } } };
  detector->by_file["A1.CR2"] = { {}, { { 9, 9 } } };
  detector->by_file["A2.CR2"] = { {}, { { 9, 9 } } };
  // Camera B shows (9, 9) once in two shots: 50%, but one shot only.
  detector->by_file["B0.CR2"] = { {}, { { 9, 9 } } };

  auto converter = make(options);
  const auto results = converter.convert(jobs);
  for (const auto& r : results)
  {
    EXPECT_EQ(r.status, Status::CONVERTED) << r.error;
  }
  EXPECT_EQ(results[3].bad_pixels_fixed, 1u);  // A3 showed nothing, yet is repaired.
  EXPECT_EQ(results[4].bad_pixels_fixed, 0u);
  EXPECT_EQ(converter.defectMap().confirmed("Canon EOS Test|A|32x32", 2, 0.5), (std::vector<PixelPos>{ { 9, 9 } }));
}

TEST_F(Fixture, BatchDoesNotConfirmDetailOfARepeatedScene)
{
  // Four shots of one scene (A1, A33, A65, A97: the fake decoder's scene is
  // the number modulo 32), the same suspect in each.
  ConvertOptions options;
  options.bad_pixels = BadPixelMode::BATCH;
  std::vector<ConvertJob> jobs;
  for (int n : { 1, 33, 65, 97 })
  {
    const std::string name = "A" + std::to_string(n) + ".CR2";
    files->put("in/" + name, "A");
    jobs.push_back({ "in/" + name, "out/" + std::to_string(n) + ".dng" });
    detector->by_file[name] = { {}, { { 9, 9 } } };
  }
  auto converter = make(options);
  for (const auto& r : converter.convert(jobs))
  {
    EXPECT_EQ(r.bad_pixels_fixed, 0u);
  }
  EXPECT_EQ(converter.defectMap().scenes("Canon EOS Test|A|32x32"), 1u);
}

TEST_F(Fixture, CancelStopsTheRest)
{
  ConvertOptions options;
  options.threads = 1;
  auto jobs = shots(5);
  auto converter = make(options);
  const auto results = converter.convert(jobs, [&](const ConvertResult&, size_t done, size_t) {
    if (done == 2)
    {
      converter.cancel();
    }
  });
  EXPECT_EQ(results[1].status, Status::CONVERTED);
  EXPECT_EQ(results[2].status, Status::CANCELLED);
  EXPECT_EQ(results[4].status, Status::CANCELLED);
  // The next batch starts anew.
  options.overwrite = true;
  EXPECT_EQ(converter.convert(jobs)[4].status, Status::CONVERTED);
}

TEST_F(Fixture, ThrowingProgressCallbackDoesNotStopTheBatch)
{
  ConvertOptions options;
  options.threads = 4;
  auto jobs = shots(6);
  const auto results = make(options).convert(jobs, [](const ConvertResult&, size_t, size_t) {
    throw std::runtime_error("the application failed");
  });
  for (const auto& r : results)
  {
    EXPECT_EQ(r.status, Status::CONVERTED);
  }
}

TEST_F(Fixture, OpcodeRepairKeepsTheData)
{
  ConvertOptions options;
  options.bad_pixels = BadPixelMode::PER_IMAGE;
  options.repair = BadPixelRepair::OPCODE;
  auto jobs = shots(1);
  detector->by_file["A0.CR2"] = { { { 5, 5 } }, {} };
  auto converter = make(options);
  ASSERT_EQ(converter.convert(jobs)[0].status, Status::CONVERTED);
  const auto& call = encoder->call("A0.CR2");
  EXPECT_EQ(call.image.opcode_bad_pixels, (std::vector<PixelPos>{ { 5, 5 } }));
  EXPECT_EQ(call.image.at(5, 5), FakeDecoder().decode(reinterpret_cast<const uint8_t*>("A"), 1, "x").at(5, 5));
}

TEST_F(Fixture, RepairsNothingByDefault)
{
  // As Adobe DNG Converter: the data as the camera recorded it.
  ConvertOptions options;
  EXPECT_EQ(options.bad_pixels, BadPixelMode::OFF);
  auto jobs = shots(1);
  detector->by_file["A0.CR2"] = { { { 5, 5 } }, {} };
  EXPECT_EQ(make(options).convert(jobs)[0].bad_pixels_fixed, 0u);
  const auto& call = encoder->call("A0.CR2");
  EXPECT_TRUE(call.image.opcode_bad_pixels.empty());
  EXPECT_EQ(call.image.data, FakeDecoder().decode(reinterpret_cast<const uint8_t*>("A"), 1, "x").data);
}

TEST_F(Fixture, DefectsOutsideTheImageAreIgnored)
{
  // A defect map made for another camera mode, or edited by hand.
  ConvertOptions options;
  options.bad_pixels = BadPixelMode::PER_IMAGE;
  auto jobs = shots(1);
  detector->by_file["A0.CR2"] = { { { 5, 5 }, { 32, 0 }, { 0, 99 } }, {} };
  EXPECT_EQ(make(options).convert(jobs)[0].bad_pixels_fixed, 1u);
}

TEST_F(Fixture, SkipsExistingOutputsUnlessOverwriting)
{
  auto jobs = shots(2);
  files->put("out/A0.dng", "old");
  ConvertOptions options;
  auto results = make(options).convert(jobs);
  EXPECT_EQ(results[0].status, Status::SKIPPED);
  EXPECT_EQ(results[1].status, Status::CONVERTED);
  EXPECT_EQ(files->read("out/A0.dng"), (std::vector<uint8_t>{ 'o', 'l', 'd' }));

  options.overwrite = true;
  results = make(options).convert(jobs);
  EXPECT_EQ(results[0].status, Status::CONVERTED);
}

TEST_F(Fixture, OneBadFileDoesNotStopTheBatch)
{
  for (auto mode : { BadPixelMode::PER_IMAGE, BadPixelMode::BATCH })
  {
    auto jobs = shots(3);
    files->put("in/A1.CR2", "corrupt");
    jobs.push_back({ "in/missing.CR2", "out/missing.dng" });
    ConvertOptions options;
    options.bad_pixels = mode;
    options.overwrite = true;
    std::set<size_t> progress;
    const auto results = make(options).convert(jobs, [&](const ConvertResult&, size_t done, size_t total) {
      EXPECT_EQ(total, 4u);
      progress.insert(done);
    });
    EXPECT_EQ(results[0].status, Status::CONVERTED);
    EXPECT_EQ(results[1].status, Status::FAILED);
    EXPECT_NE(results[1].error.find("cannot decode"), std::string::npos);
    EXPECT_EQ(results[2].status, Status::CONVERTED);
    EXPECT_EQ(results[3].status, Status::FAILED);
    EXPECT_EQ(progress, (std::set<size_t>{ 1, 2, 3, 4 }));  // Each file reported once.
  }
}

TEST_F(Fixture, ManyFilesInParallel)
{
  ConvertOptions options;
  options.threads = 8;
  options.bad_pixels = BadPixelMode::BATCH;
  auto jobs = shots(40);
  const auto results = make(options).convert(jobs);
  for (size_t i = 0; i < jobs.size(); ++i)
  {
    EXPECT_EQ(results[i].input, jobs[i].input);  // In the order of the jobs.
    EXPECT_EQ(results[i].status, Status::CONVERTED);
  }
  EXPECT_EQ(files->size(), 80u);
}

TEST(PlanJobs, MapsFilesAndFolders)
{
  TempDir dir;
  const auto in = dir.path() / "in";
  std::filesystem::create_directories(in / "day2");
  for (const auto* name :
       { "IMG_1.CR2", "img_2.cr2", "notes.txt", "IMG_3.JPG", "day2/IMG_4.NEF", "old.dng", "day2/IMG_4.CR3" })
  {
    std::ofstream(in / name) << "x";
  }

  auto jobs = planJobs(in, dir.path() / "out");
  ASSERT_EQ(jobs.size(), 2u);
  EXPECT_EQ(jobs[0].output, dir.path() / "out" / "IMG_1.dng");
  EXPECT_EQ(jobs[1].output, dir.path() / "out" / "img_2.dng");

  jobs = planJobs(in, dir.path() / "out", true);
  ASSERT_EQ(jobs.size(), 4u);
  EXPECT_EQ(jobs[0].output, dir.path() / "out" / "IMG_1.dng");
  // Two raw files of one name do not overwrite each other's DNG.
  EXPECT_EQ(jobs[1].output, dir.path() / "out" / "day2" / "IMG_4.dng");
  EXPECT_EQ(jobs[2].output, dir.path() / "out" / "day2" / "IMG_4_NEF.dng");

  jobs = planJobs(in / "IMG_1.CR2", dir.path() / "single");
  ASSERT_EQ(jobs.size(), 1u);
  EXPECT_EQ(jobs[0].output, dir.path() / "single" / "IMG_1.dng");

  EXPECT_THROW(planJobs(in / "nope", dir.path()), Error);
}

}  // namespace
}  // namespace rawconv::test
