// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// raw-convert: converts a raw file, or a folder of them, to DNG, TIFF or both.

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cli_options.hpp"
#include "rawconv/rawconv.hpp"

namespace
{

// The converter Ctrl-C cancels: the files being converted are finished, the
// others are not started. Converter::cancel() only stores to a lock-free
// atomic, which a signal handler may do.
std::atomic<rawconv::Converter*> running{ nullptr };

void onInterrupt(int)
{
  if (rawconv::Converter* converter = running.load())
  {
    converter->cancel();
  }
}

void printResult(const rawconv::ConvertResult& r, size_t done, size_t total, bool quiet)
{
  const std::string counter = "[" + std::to_string(done) + "/" + std::to_string(total) + "] ";
  using Status = rawconv::ConvertResult::Status;
  switch (r.status)
  {
    case Status::CONVERTED:
      if (!quiet)
      {
        std::cout << counter << r.input.string() << " -> ";
        for (size_t i = 0; i < r.outputs.size(); ++i)
        {
          std::cout << (i > 0 ? ", " : "") << r.outputs[i].string();
        }
        if (r.bad_pixels_fixed > 0)
        {
          std::cout << " (" << r.bad_pixels_fixed << " bad pixel" << (r.bad_pixels_fixed > 1 ? "s" : "") << ")";
        }
        std::cout << std::endl;
      }
      break;
    case Status::SKIPPED:
      if (!quiet)
      {
        std::cout << counter << r.input.string() << ": skipped, " << r.output.string() << " exists (use --overwrite)"
                  << std::endl;
      }
      break;
    case Status::CANCELLED:
      break;
    case Status::FAILED:
      std::cerr << counter << r.input.string() << ": " << r.error << std::endl;
      break;
  }
}

int run(const rawconv::cli::CliOptions& cli)
{
  rawconv::ConvertOptions options = cli.convert;
  options.dng.software = std::string("raw-converter ") + RAWCONV_VERSION;
  options.tiff.software = options.dng.software;

  const auto jobs = rawconv::planJobs(cli.input, cli.output, cli.recursive);
  if (jobs.empty())
  {
    std::cerr << "No raw files in " << cli.input.string() << '\n';
    return 1;
  }
  if (cli.auto_bad_pixel_mode)
  {
    options.bad_pixels = rawconv::cli::autoBadPixelMode(jobs.size());
  }

  rawconv::Converter converter(options);
  if (cli.defect_map && std::filesystem::exists(*cli.defect_map))
  {
    std::ifstream in(*cli.defect_map);
    if (!in)
    {
      throw rawconv::Error(cli.defect_map->string() + ": cannot open");
    }
    converter.defectMap().read(in);
  }

  running = &converter;
  std::signal(SIGINT, onInterrupt);
  const auto results = converter.convert(jobs, [&](const rawconv::ConvertResult& r, size_t done, size_t total) {
    printResult(r, done, total, cli.quiet);
  });
  std::signal(SIGINT, SIG_DFL);
  running = nullptr;

  if (cli.defect_map)
  {
    std::ofstream out(*cli.defect_map, std::ios::trunc);
    converter.defectMap().write(out, options.batch_min_scenes, options.batch_min_fraction);
    if (!out)
    {
      throw rawconv::Error(cli.defect_map->string() + ": cannot write");
    }
  }

  size_t counts[4] = {};
  for (const auto& r : results)
  {
    ++counts[static_cast<int>(r.status)];
  }
  using Status = rawconv::ConvertResult::Status;
  if (!cli.quiet)
  {
    std::cout << counts[static_cast<int>(Status::CONVERTED)] << " converted, "
              << counts[static_cast<int>(Status::SKIPPED)] << " skipped, " << counts[static_cast<int>(Status::FAILED)]
              << " failed";
    if (counts[static_cast<int>(Status::CANCELLED)] > 0)
    {
      std::cout << ", " << counts[static_cast<int>(Status::CANCELLED)] << " cancelled";
    }
    std::cout << std::endl;
  }
  if (counts[static_cast<int>(Status::FAILED)] > 0)
  {
    return 1;
  }
  return counts[static_cast<int>(Status::CANCELLED)] > 0 ? 130 : 0;
}

}  // namespace

int main(int argc, char** argv)
{
  rawconv::cli::CliOptions cli;
  try
  {
    cli = rawconv::cli::parseCommandLine(std::vector<std::string>(argv + 1, argv + argc));
  }
  catch (const std::invalid_argument& e)
  {
    std::cerr << "raw-convert: " << e.what() << "\n\n" << rawconv::cli::usage();
    return 2;
  }
  if (cli.help)
  {
    std::cout << rawconv::cli::usage();
    return 0;
  }
  if (cli.version)
  {
    std::cout << "raw-convert " << RAWCONV_VERSION << " (LibRaw " << rawconv::librawVersion() << ")\n";
    return 0;
  }
  try
  {
    return run(cli);
  }
  catch (const std::exception& e)
  {
    std::cerr << "raw-convert: " << e.what() << '\n';
    return 1;
  }
}
