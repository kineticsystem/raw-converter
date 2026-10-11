// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// Helpers shared by the tests: synthetic images, and in-memory fakes of the
// converter's services.

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include <unistd.h>

#include "rawconv/rawconv.hpp"

namespace rawconv::test
{

// A CFA image of a Canon-like camera, of a grey scene: every pixel at `level`
// above a black of 1024, times the as-shot neutral of its colour, plus
// Gaussian noise of `noise`, reproducibly.
inline RawImage makeCfaImage(uint32_t width, uint32_t height, double level = 1000, double noise = 10, uint32_t seed = 1)
{
  RawImage image;
  image.layout = RawLayout::CFA;
  image.width = width;
  image.height = height;
  image.samples_per_pixel = 1;
  image.bits_per_sample = 14;
  image.cfa_pattern = { 1, 2, 0, 1 };  // GBRG, as the 5D Mark II.
  image.black_level = { 1024, 1024, 1024, 1024 };
  image.white_level = 15000;
  image.default_crop = { 2, 2, height - 4, width - 4 };
  image.color_matrix = { 0.4716, 0.0603, -0.0830, -0.7798, 1.5474, 0.2480, -0.1496, 0.1937, 0.6651 };
  image.as_shot_neutral = { 0.5, 1, 0.6 };
  image.metadata.make = "Canon";
  image.metadata.model = "Canon EOS Test";
  image.metadata.unique_camera_model = "Canon EOS Test";
  image.metadata.serial_number = "42";
  image.data.resize(static_cast<size_t>(width) * height);
  std::mt19937 rng(seed);
  std::normal_distribution<double> gauss(0, noise);
  for (uint32_t row = 0; row < height; ++row)
  {
    for (uint32_t col = 0; col < width; ++col)
    {
      const double v = 1024 + level * image.as_shot_neutral[image.cfaColor(row, col)] + gauss(rng);
      image.at(row, col) = static_cast<uint16_t>(std::clamp(v, 0.0, 65535.0));
    }
  }
  return image;
}

// An image whose every sample is random over the whole 16-bit range: the
// worst case for a lossless encoder, with differences of -32768.
inline RawImage makeRandomImage(RawLayout layout, uint32_t width, uint32_t height, uint32_t seed = 7)
{
  RawImage image = makeCfaImage(width, height);
  image.layout = layout;
  image.samples_per_pixel = layout == RawLayout::CFA ? 1 : 3;
  image.bits_per_sample = 16;
  image.white_level = 65535;
  image.black_level = { 0, 0, 0, 0 };
  image.default_crop = { 0, 0, height, width };
  image.data.resize(static_cast<size_t>(width) * height * image.samples_per_pixel);
  std::mt19937 rng(seed);
  for (auto& v : image.data)
  {
    // Mostly random, with runs of the extremes.
    const uint32_t r = rng();
    v = (r % 7 == 0) ? 0 : (r % 7 == 1) ? 65535 : static_cast<uint16_t>(r >> 8);
  }
  return image;
}

// A FileStore in memory.
class MemoryFileStore : public FileStore
{
public:
  std::vector<uint8_t> read(const std::filesystem::path& path) const override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(path.string());
    if (it == files_.end())
    {
      throw Error(path.string() + ": no such file");
    }
    return it->second;
  }
  void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    files_[path.string()] = bytes;
  }
  bool exists(const std::filesystem::path& path) const override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_.count(path.string()) != 0;
  }
  void put(const std::string& path, const std::string& content)
  {
    write(path, std::vector<uint8_t>(content.begin(), content.end()));
  }
  size_t size() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_.size();
  }

private:
  mutable std::mutex mutex_;
  std::map<std::string, std::vector<uint8_t>> files_;
};

// A decoder that makes a small flat image for any file. The file content
// selects the camera serial number, so that tests can mix cameras; a file
// containing "corrupt" fails to decode. The number in the file name selects
// the scene, a pattern of bright 4x4 patches outside the top-left quarter,
// which tests keep for their pixels: A0 and A1 are shots of different scenes,
// and A1 and A33 of the same.
class FakeDecoder : public RawDecoder
{
public:
  RawImage decode(const uint8_t* data, size_t size, const std::string& file_name) const override
  {
    const std::string content(reinterpret_cast<const char*>(data), size);
    if (content == "corrupt")
    {
      throw Error(file_name + ": cannot decode");
    }
    RawImage image = makeCfaImage(32, 32, 1000, 0);
    image.metadata.serial_number = content;
    image.metadata.original_file_name = file_name;
    const auto digits = file_name.find_first_of("0123456789");
    const int scene = digits == std::string::npos ? 0 : std::atoi(file_name.c_str() + digits) % 32;
    for (uint32_t cell = 0; cell < 64; ++cell)
    {
      const uint32_t top = (cell / 8) * 4;
      const uint32_t left = (cell % 8) * 4;
      if ((top < 16 && left < 16) || (cell * (scene + 3) + scene) % 7 >= 3)
      {
        continue;
      }
      for (uint32_t row = 0; row < 4; ++row)
      {
        for (uint32_t col = 0; col < 4; ++col)
        {
          image.at(top + row, left + col) = 15000;
        }
      }
    }
    return image;
  }
};

// A detector that returns, for each file name, the candidates a test chose.
class FakeDetector : public BadPixelDetector
{
public:
  std::map<std::string, BadPixelCandidates> by_file;

  BadPixelCandidates detect(const RawImage& image) const override
  {
    auto it = by_file.find(image.metadata.original_file_name);
    return it == by_file.end() ? BadPixelCandidates{} : it->second;
  }
};

// An encoder that records the images and options it is given, by original
// file name, and returns a tiny placeholder.
class CapturingEncoder : public DngEncoder
{
public:
  struct Call
  {
    RawImage image;
    DngWriteOptions options;
  };

  std::vector<uint8_t> encode(const RawImage& image, const DngWriteOptions& options) const override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    calls_[image.metadata.original_file_name] = { image, options };
    return { 'D', 'N', 'G' };
  }
  const Call& call(const std::string& file) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_.at(file);
  }

private:
  mutable std::mutex mutex_;
  mutable std::map<std::string, Call> calls_;
};

// The raw files of the folders the end-to-end tests convert: those of the
// RAWCONV_SAMPLE_DIRS environment variable (colon separated) if set, else
// those the tests were configured with (CMake's RAWCONV_SAMPLE_DIRS).
inline std::vector<std::filesystem::path> sampleFiles()
{
  std::vector<std::filesystem::path> files;
  const char* dirs = std::getenv("RAWCONV_SAMPLE_DIRS");
#ifdef RAWCONV_SAMPLE_DIRS
  if (dirs == nullptr)
  {
    dirs = RAWCONV_SAMPLE_DIRS;
  }
#endif
  if (dirs == nullptr)
  {
    return files;
  }
  std::string list(dirs);
  size_t start = 0;
  while (start <= list.size())
  {
    const size_t end = std::min(list.find(':', start), list.size());
    const std::filesystem::path dir = list.substr(start, end - start);
    std::error_code error;
    if (!dir.empty() && std::filesystem::is_directory(dir, error))
    {
      for (const auto& entry : std::filesystem::directory_iterator(dir))
      {
        if (isRawFileName(entry.path()))
        {
          files.push_back(entry.path());
        }
      }
    }
    start = end + 1;
  }
  std::sort(files.begin(), files.end());
  return files;
}

// A TIFF encoder that records the images and options it is given, by original
// file name, and returns a tiny placeholder.
class CapturingTiffEncoder : public TiffEncoder
{
public:
  struct Call
  {
    RawImage image;
    TiffWriteOptions options;
  };

  std::vector<uint8_t> encode(const RawImage& image, const TiffWriteOptions& options) const override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    calls_[image.metadata.original_file_name] = { image, options };
    return { 'T', 'I', 'F' };
  }
  const Call& call(const std::string& file) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_.at(file);
  }
  size_t calls() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_.size();
  }

private:
  mutable std::mutex mutex_;
  mutable std::map<std::string, Call> calls_;
};

// A fresh, empty temporary folder, removed with the object.
class TempDir
{
public:
  TempDir()
  {
    static std::atomic<int> counter{ 0 };
    path_ = std::filesystem::temp_directory_path() /
            ("rawconv-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir()
  {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  const std::filesystem::path& path() const
  {
    return path_;
  }

private:
  std::filesystem::path path_;
};

}  // namespace rawconv::test
