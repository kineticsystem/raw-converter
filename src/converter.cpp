// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "dngconv/converter.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#include "dngconv/error.hpp"
#include "parallel.hpp"

namespace dngconv
{

namespace
{

// Each file in flight takes some 150 MB (the file, LibRaw's buffers, the
// image and the encoded DNG): one per core, but no more than this by default.
constexpr unsigned MAX_DEFAULT_THREADS = 8;

ConverterServices withDefaults(ConverterServices services, const ConvertOptions& options)
{
  if (!services.decoder)
  {
    services.decoder = std::make_shared<LibRawDecoder>();
  }
  if (!services.detector)
  {
    services.detector = std::make_shared<NoiseBadPixelDetector>(options.detection);
  }
  if (!services.encoder)
  {
    services.encoder = std::make_shared<StandardDngEncoder>();
  }
  if (!services.files)
  {
    services.files = std::make_shared<LocalFileStore>();
  }
  return services;
}

// Serialises the progress callback and counts the files done. An exception
// from the callback is the application's problem, not the batch's: it is
// swallowed rather than allowed to abandon the other files.
class ProgressReporter
{
public:
  ProgressReporter(const Converter::Progress& progress, size_t total) : progress_(progress), total_(total)
  {
  }
  void report(const ConvertResult& result)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++done_;
    if (progress_)
    {
      try
      {
        progress_(result, done_, total_);
      }
      catch (...)
      {
      }
    }
  }

private:
  const Converter::Progress& progress_;
  size_t total_;
  size_t done_ = 0;
  std::mutex mutex_;
};

}  // namespace

Converter::Converter(ConvertOptions options, ConverterServices services)
  : options_(std::move(options)), services_(withDefaults(std::move(services), options_))
{
}

RawImage Converter::decode(const std::filesystem::path& input) const
{
  const std::vector<uint8_t> data = services_.files->read(input);
  return services_.decoder->decode(data.data(), data.size(), input.filename().string());
}

size_t Converter::applyRepairs(RawImage& image, const BadPixelCandidates& candidates) const
{
  std::set<PixelPos> pixels(candidates.confident.begin(), candidates.confident.end());
  const auto confirmed =
      defects_.confirmed(DefectMap::cameraKey(image), options_.batch_min_scenes, options_.batch_min_fraction);
  pixels.insert(confirmed.begin(), confirmed.end());
  std::vector<PixelPos> list;
  for (const auto& p : pixels)
  {
    if (p.row < image.height && p.col < image.width)
    {
      list.push_back(p);
    }
  }
  if (list.empty())
  {
    return 0;
  }
  if (options_.repair == BadPixelRepair::OPCODE && image.layout == RawLayout::CFA)
  {
    image.opcode_bad_pixels.insert(image.opcode_bad_pixels.end(), list.begin(), list.end());
  }
  else
  {
    repairBadPixels(image, list);
  }
  return list.size();
}

size_t Converter::repair(RawImage& image) const
{
  if (options_.bad_pixels == BadPixelMode::OFF)
  {
    return 0;
  }
  return applyRepairs(image, services_.detector->detect(image));
}

unsigned Converter::threads(size_t jobs) const
{
  unsigned threads = options_.threads;
  if (threads == 0)
  {
    threads = std::min(MAX_DEFAULT_THREADS, std::max(1u, std::thread::hardware_concurrency()));
  }
  return static_cast<unsigned>(std::max<size_t>(1, std::min<size_t>(threads, jobs)));
}

ConvertResult Converter::convertFile(const std::filesystem::path& input, const std::filesystem::path& output)
{
  return convert({ { input, output } }).front();
}

std::vector<ConvertResult> Converter::convert(const std::vector<ConvertJob>& jobs, const Progress& progress)
{
  cancelled_ = false;
  std::vector<ConvertResult> results(jobs.size());
  // Whether a job is settled: skipped, failed in the first pass or
  // cancelled. One element per job, each written by one thread only.
  std::vector<char> settled(jobs.size(), 0);
  ProgressReporter reporter(progress, jobs.size());
  for (size_t i = 0; i < jobs.size(); ++i)
  {
    results[i].input = jobs[i].input;
    results[i].output = jobs[i].output;
    if (!options_.overwrite && services_.files->exists(jobs[i].output))
    {
      results[i].status = ConvertResult::Status::SKIPPED;
      settled[i] = 1;
      reporter.report(results[i]);
    }
  }

  const unsigned threads = this->threads(jobs.size());
  // When there are fewer files than threads, the threads left over compress
  // the tiles of each file instead.
  DngWriteOptions dng_options = options_.dng;
  dng_options.threads = std::max(1u, this->threads(SIZE_MAX) / threads);

  auto settle = [&](size_t i, ConvertResult::Status status, const std::string& error) {
    results[i].status = status;
    results[i].error = error;
    settled[i] = 1;
    reporter.report(results[i]);
  };
  auto cancelledBefore = [&](size_t i) {
    if (cancelled_ && !settled[i])
    {
      settle(i, ConvertResult::Status::CANCELLED, {});
    }
    return settled[i] != 0;
  };

  // BATCH: first learn the defects of each camera from every file, keeping
  // what each file showed so that the second pass need not look again.
  std::vector<std::optional<BadPixelCandidates>> found(jobs.size());
  if (options_.bad_pixels == BadPixelMode::BATCH)
  {
    parallelFor(jobs.size(), threads, [&](size_t i) {
      if (cancelledBefore(i))
      {
        return;
      }
      try
      {
        const RawImage image = decode(jobs[i].input);
        found[i] = services_.detector->detect(image);
        defects_.addShot(DefectMap::cameraKey(image), *found[i], sceneSignature(image));
      }
      catch (const std::exception& e)
      {
        settle(i, ConvertResult::Status::FAILED, e.what());
      }
    });
  }

  parallelFor(jobs.size(), threads, [&](size_t i) {
    if (cancelledBefore(i))
    {
      return;
    }
    try
    {
      RawImage image = decode(jobs[i].input);
      if (options_.bad_pixels != BadPixelMode::OFF)
      {
        const BadPixelCandidates candidates = found[i] ? *found[i] : services_.detector->detect(image);
        results[i].bad_pixels_fixed = applyRepairs(image, candidates);
      }
      services_.files->write(jobs[i].output, services_.encoder->encode(image, dng_options));
      settle(i, ConvertResult::Status::CONVERTED, {});
    }
    catch (const std::exception& e)
    {
      settle(i, ConvertResult::Status::FAILED, e.what());
    }
  });
  return results;
}

std::vector<ConvertJob> planJobs(const std::filesystem::path& input, const std::filesystem::path& output_dir,
                                 bool recursive)
{
  namespace fs = std::filesystem;
  std::error_code error;
  if (!fs::exists(input, error))
  {
    throw Error(input.string() + ": no such file or folder");
  }

  std::vector<fs::path> inputs;
  fs::path base = input.parent_path();
  if (!fs::is_directory(input, error))
  {
    inputs.push_back(input);
  }
  else
  {
    base = input;
    auto add = [&](const fs::directory_entry& entry) {
      if (entry.is_regular_file(error) && isRawFileName(entry.path()))
      {
        inputs.push_back(entry.path());
      }
    };
    if (recursive)
    {
      for (const auto& entry : fs::recursive_directory_iterator(input, fs::directory_options::skip_permission_denied))
      {
        add(entry);
      }
    }
    else
    {
      for (const auto& entry : fs::directory_iterator(input))
      {
        add(entry);
      }
    }
    std::sort(inputs.begin(), inputs.end());
  }

  // The DNG of each raw file, in its folder relative to the input folder.
  // The first raw file of a name gets the plain DNG name; the others, of
  // another format, get their extension added.
  std::vector<ConvertJob> jobs;
  std::map<fs::path, int> taken;
  for (const auto& raw : inputs)
  {
    const fs::path folder = output_dir / raw.parent_path().lexically_relative(base);
    fs::path output = (folder / raw.stem()).lexically_normal();
    output += ".dng";
    if (taken[output]++ > 0)
    {
      std::string extension = raw.extension().string().substr(1);
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char c) { return std::toupper(c); });
      output = (folder / (raw.stem().string() + "_" + extension + ".dng")).lexically_normal();
    }
    jobs.push_back({ raw, output });
  }
  return jobs;
}

}  // namespace dngconv
