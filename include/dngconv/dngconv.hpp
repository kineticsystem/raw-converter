// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// dng-converter: converts camera raw files to DNG.
//
//   #include <dngconv/dngconv.hpp>
//
//   dngconv::Converter converter;
//   auto results = converter.convert(dngconv::planJobs("in", "out"));
//
// or, step by step:
//
//   dngconv::RawImage image = dngconv::readRawFile("IMG_0001.CR2");
//   // Optional, and off in Converter by default, as in Adobe DNG Converter:
//   dngconv::repairBadPixels(image, dngconv::detectBadPixels(image).confident);
//   dngconv::writeDng(image, "IMG_0001.dng");

#pragma once

#include "dngconv/bad_pixels.hpp"
#include "dngconv/camera_table.hpp"
#include "dngconv/converter.hpp"
#include "dngconv/dng_writer.hpp"
#include "dngconv/error.hpp"
#include "dngconv/file_store.hpp"
#include "dngconv/raw_image.hpp"
#include "dngconv/raw_reader.hpp"
#include "dngconv/version.hpp"
