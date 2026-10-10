// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "cli_options.hpp"

#include <stdexcept>

namespace dngconv::cli
{

namespace
{

unsigned parseUnsigned(const std::string& option, const std::string& value)
{
  size_t used = 0;
  unsigned long n = 0;
  try
  {
    n = std::stoul(value, &used);
  }
  catch (const std::exception&)
  {
    used = 0;
  }
  if (used != value.size() || value.empty() || value[0] == '-' || n > 1024)
  {
    throw std::invalid_argument(option + " expects a number from 0 to 1024, not '" + value + "'");
  }
  return static_cast<unsigned>(n);
}

}  // namespace

std::string usage()
{
  return R"(Usage: dng-convert [options] <raw file or folder> <output folder>

Converts camera raw files (Canon CR2 and the other formats LibRaw reads) to
DNG files that Lightroom opens as it opens those of Adobe DNG Converter.

Options:
  -r, --recursive         Also convert the raw files in sub-folders, mirroring
                          them in the output folder.
  -j, --jobs N            Files converted in parallel (default: one per core).
  --bad-pixels MODE       How to find hot and dead pixels:
                            image  each file on its own;
                            batch  also learn each camera's defects from all
                                   the files, confirming the faint ones by
                                   their showing in different scenes, and
                                   repair them in all of the camera's files;
                            off    leave the pixels as they are.
                          Default: batch for 3 files or more, image otherwise.
  --repair METHOD         How to repair them:
                            opcode       keep the raw data as it is, and tell
                                         the raw converter to repair them (a
                                         DNG FixBadPixelsList, which Lightroom
                                         applies) (default);
                            interpolate  replace them in the raw data, for
                                         raw converters that ignore opcodes.
  --defect-map FILE       Known defects to repair, in addition to those found;
                          the file is created or updated with what this run
                          learns, for the next run.
  --uncompressed          Do not compress the raw data (lossless JPEG is the
                          default, as with Adobe DNG Converter).
  --no-preview            Do not keep the camera's JPEG preview.
  --overwrite             Replace DNG files that exist (default: skip them).
  -q, --quiet             Only print errors.
  -h, --help              Print this help.
  --version               Print the version.
)";
}

CliOptions parseCommandLine(const std::vector<std::string>& args)
{
  CliOptions options;
  std::vector<std::string> positional;
  for (size_t i = 0; i < args.size(); ++i)
  {
    const std::string& arg = args[i];
    auto value = [&]() -> const std::string& {
      if (i + 1 >= args.size())
      {
        throw std::invalid_argument(arg + " expects a value");
      }
      return args[++i];
    };

    if (arg == "-h" || arg == "--help")
    {
      options.help = true;
    }
    else if (arg == "--version")
    {
      options.version = true;
    }
    else if (arg == "-r" || arg == "--recursive")
    {
      options.recursive = true;
    }
    else if (arg == "-j" || arg == "--jobs")
    {
      options.convert.threads = parseUnsigned(arg, value());
    }
    else if (arg == "--bad-pixels")
    {
      const std::string& mode = value();
      options.auto_bad_pixel_mode = false;
      if (mode == "image")
      {
        options.convert.bad_pixels = BadPixelMode::PER_IMAGE;
      }
      else if (mode == "batch")
      {
        options.convert.bad_pixels = BadPixelMode::BATCH;
      }
      else if (mode == "off")
      {
        options.convert.bad_pixels = BadPixelMode::OFF;
      }
      else if (mode == "auto")
      {
        options.auto_bad_pixel_mode = true;
      }
      else
      {
        throw std::invalid_argument("--bad-pixels expects image, batch, off or auto, not '" + mode + "'");
      }
    }
    else if (arg == "--repair")
    {
      const std::string& method = value();
      if (method == "interpolate")
      {
        options.convert.repair = BadPixelRepair::INTERPOLATE;
      }
      else if (method == "opcode")
      {
        options.convert.repair = BadPixelRepair::OPCODE;
      }
      else
      {
        throw std::invalid_argument("--repair expects interpolate or opcode, not '" + method + "'");
      }
    }
    else if (arg == "--defect-map")
    {
      options.defect_map = value();
    }
    else if (arg == "--uncompressed")
    {
      options.convert.dng.compression = Compression::NONE;
    }
    else if (arg == "--no-preview")
    {
      options.convert.dng.embed_preview = false;
    }
    else if (arg == "--overwrite")
    {
      options.convert.overwrite = true;
    }
    else if (arg == "-q" || arg == "--quiet")
    {
      options.quiet = true;
    }
    else if (arg.size() > 1 && arg[0] == '-')
    {
      throw std::invalid_argument("Unknown option " + arg);
    }
    else
    {
      positional.push_back(arg);
    }
  }

  if (options.help || options.version)
  {
    return options;
  }
  if (positional.size() != 2)
  {
    throw std::invalid_argument("Expected a raw file or folder, and an output folder");
  }
  options.input = positional[0];
  options.output = positional[1];
  return options;
}

BadPixelMode autoBadPixelMode(size_t files)
{
  return files >= 3 ? BadPixelMode::BATCH : BadPixelMode::PER_IMAGE;
}

}  // namespace dngconv::cli
