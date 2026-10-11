// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// raw-converter: converts camera raw files to DNG.
//
//   #include <rawconv/rawconv.hpp>
//
//   rawconv::Converter converter;
//   auto results = converter.convert(rawconv::planJobs("in", "out"));
//
// or, step by step:
//
//   rawconv::RawImage image = rawconv::readRawFile("IMG_0001.CR2");
//   // Optional, and off in Converter by default, as in Adobe DNG Converter:
//   rawconv::repairBadPixels(image, rawconv::detectBadPixels(image).confident);
//   rawconv::writeDng(image, "IMG_0001.dng");

#pragma once

#include "rawconv/bad_pixels.hpp"
#include "rawconv/camera_table.hpp"
#include "rawconv/converter.hpp"
#include "rawconv/dng_writer.hpp"
#include "rawconv/error.hpp"
#include "rawconv/file_store.hpp"
#include "rawconv/raw_image.hpp"
#include "rawconv/raw_reader.hpp"
#include "rawconv/version.hpp"
