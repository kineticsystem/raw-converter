// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "rawconv/raw_image.hpp"
#include "tiff/tiff_writer.hpp"

namespace rawconv
{

// A fraction close to `value`, as a numerator/denominator pair, exact for the
// usual exposure times (1/200) and apertures (5.6).
std::vector<uint32_t> fraction(double value);

// The EXIF directory of a file written from the image: the original's own,
// or the essentials rebuilt from the metadata, plus the EXIF 2.3 lens and body
// fields. Shared by the DNG and the TIFF.
std::shared_ptr<tiff::Ifd> exifIfd(const Metadata& metadata);

// The original's GPS directory. Only meaningful when metadata.gps is not
// empty.
std::shared_ptr<tiff::Ifd> gpsIfd(const Metadata& metadata);

}  // namespace rawconv
