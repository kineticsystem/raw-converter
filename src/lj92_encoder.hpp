// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <vector>

namespace rawconv
{

// Encodes 16-bit samples as a lossless JPEG (ITU T.81, process 14, predictor
// 1, i.e. the left neighbour), the compression DNG calls 7.
//
// `samples` holds `height` rows of `width` samples each. They are encoded as a
// JPEG frame of `width / components` columns of `components` interleaved
// components, the layout Adobe DNG Converter uses for a Bayer tile with
// components = 2: each sample is then predicted from the one two columns to
// its left, which has the same colour. `width` must be a multiple of
// `components`.
//
// The Huffman table is optimised for the samples, so the output is as small
// as this predictor allows.
std::vector<uint8_t> encodeLosslessJpeg(const uint16_t* samples, uint32_t width, uint32_t height, uint32_t components);

}  // namespace rawconv
