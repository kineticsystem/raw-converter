// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "dngconv/converter.hpp"

namespace dngconv::cli
{

// What the command line asks for.
struct CliOptions
{
  std::filesystem::path input;
  std::filesystem::path output;
  bool recursive = false;
  // --bad-pixels auto: BATCH for three files or more, PER_IMAGE otherwise,
  // decided once the files are known.
  bool auto_bad_pixel_mode = false;
  std::optional<std::filesystem::path> defect_map;
  bool quiet = false;
  bool help = false;
  bool version = false;
  ConvertOptions convert;
};

// Parses the arguments, without the program name. Throws
// std::invalid_argument with a message for the user on a bad command line.
CliOptions parseCommandLine(const std::vector<std::string>& args);

std::string usage();

// The bad pixel mode for an automatic choice, given the files to convert.
BadPixelMode autoBadPixelMode(size_t files);

}  // namespace dngconv::cli
